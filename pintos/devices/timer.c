#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참고하라. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 지난 타이머 틱 수. */
static int64_t ticks;


// timer_sleep()으로 잠든 스레드들의 리스트.
// wakeup_tick이 작은(먼저 깨어날) 스레드가 앞에 오도록 정렬되어 있음
static struct list sleep_list;

/* 타이머 틱 하나당 루프 횟수.
   timer_calibrate()에서 초기화된다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

static bool wakeup_tick_less (const struct list_elem *a, const struct list_elem *b, void *aux); // wakeup_tick이 작은 스레드가 앞에 오도록 정렬하기 위한 비교 함수, 근데 왜 bool?

/* 8254 프로그래머블 인터벌 타이머(PIT)가 초당 PIT_FREQ번
   인터럽트를 발생시키도록 설정하고, 해당 인터럽트를
   등록한다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값,
	   가장 가까운 정수로 반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: 카운터 0, LSB 다음 MSB, 모드 2, 이진수. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	list_init(&sleep_list); // sleep_list 초기화

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연을 구현하는 데 쓰이는 loops_per_tick을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick을 타이머 틱 하나보다 여전히 작은
	   가장 큰 2의 거듭제곱으로 근사한다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 정밀하게 조정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후 지난 타이머 틱 수를 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN 이후 경과한 타이머 틱 수를 반환한다. THEN은
   timer_ticks()가 반환했던 값이어야 한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}


// sleep_list 정렬용 비교 함수. A의 깨어날 시각이 B보다 빠르면 true를 반환한다.
static bool
wakeup_tick_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);

	return ta->wakeup_tick < tb->wakeup_tick;
}


/* 약 TICKS 타이머 틱 동안 실행을 중단한다. */
// void
// timer_sleep (int64_t ticks) {
// 	int64_t start = timer_ticks ();

// 	ASSERT (intr_get_level () == INTR_ON);
// 	while (timer_elapsed (start) < ticks)
// 		thread_yield ();
// }

void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();
	struct thread *cur;
	enum intr_level old_level;

	ASSERT (intr_get_level () == INTR_ON);

	/* 0 이하의 시간 만큼 잘 필요는 없기 때문에 바로 반환*/
	if (ticks <= 0)
		return;

	cur = thread_current ();

	/* sleep_list는 타이머 인터럽트 핸들러도 접근하기 때문에
	리스트를 수정하고 잠드는 동안 인터럽트를 죽인다. */
	old_level = intr_disable ();

	cur->wakeup_tick = start + ticks; // 깨어날 시각 설정
	list_insert_ordered (&sleep_list, &cur->elem, wakeup_tick_less, NULL); // sleep_list에 삽입
	thread_block (); // 스레드를 잠근다

	// 깨어나면 여기서부터 재실행
	intr_set_level (old_level);

}

/* 약 MS 밀리초 동안 실행을 중단한다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 약 US 마이크로초 동안 실행을 중단한다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 약 NS 나노초 동안 실행을 중단한다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
}

/* LOOPS번 반복하는 데 타이머 틱 하나보다 오래 걸리면 true,
   아니면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 틱을 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번 루프를 돈다. */
	start = ticks;
	busy_wait (loops);

	/* 틱 카운트가 바뀌었다면 너무 오래 반복한 것이다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 단순한 루프를 LOOPS번
   반복한다.

   코드 정렬이 실행 시간에 크게 영향을 줄 수 있으므로
   NO_INLINE으로 지정했다. 이 함수가 위치마다 다르게
   인라인되면 결과를 예측하기 어려워지기
   때문이다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 약 NUM/DENOM초 동안 잠든다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM초를 타이머 틱으로 변환한다 (내림).

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 한 타이머 틱 전체를 기다려야 한다.
		   timer_sleep()을 사용하면 CPU를 다른 프로세스에게
		   양보하므로 이를 사용한다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면, 틱보다 짧은 시간을 더 정확하게 재기 위해
		   바쁜 대기 루프를 사용한다. 오버플로 가능성을 피하기 위해
		   분자와 분모를 1000으로 나누어 축소한다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
