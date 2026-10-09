#include <console.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

static void vprintf_helper (char, void *);
static void putchar_have_lock (uint8_t c);

/* 콘솔 락.
   vga 계층과 serial 계층은 각자 락을 걸기 때문에 언제든
   호출해도 안전하다.
   하지만 이 락은 동시에 호출된 printf()들의 출력이 뒤섞여
   혼란스러워 보이는 것을 막는 데 유용하다. */
static struct lock console_lock;

/* 일반적인 상황에서는 true: 위에서 설명했듯이 스레드 간에 출력이
   섞이지 않도록 콘솔 락을 사용하고 싶기 때문이다.

   락이 동작하기 전이나 콘솔 락이 초기화되기 전인 부팅 초기,
   또는 커널 패닉 이후에는 false. 전자의 경우 락을 획득하면
   단언(assertion) 실패가 일어나고, 이는 다시 패닉을 일으켜
   후자의 경우가 된다. 후자의 경우, 패닉의 원인이 버그 있는
   lock_acquire() 구현이라면 아마 재귀에 빠질 것이다. */
static bool use_console_lock;

/* Pintos에 디버그 출력을 충분히 많이 추가하면, 한 스레드가
   console_lock을 재귀적으로 획득하려 하는 일이 생길 수 있다.
   실제 예로, palloc_free()에 printf() 호출을 추가했더니
   다음과 같은 백트레이스가 나왔다:

   lock_console()
   vprintf()
   printf()             - palloc()이 락을 다시 획득하려 함
   palloc_free()
   schedule_tail()      - 스레드를 전환하는 중에 다른 스레드가 종료됨
   schedule()
   thread_yield()
   intr_handler()       - 타이머 인터럽트
   intr_set_level()
   serial_putc()
   putchar_have_lock()
   putbuf()
   sys_write()          - 한 프로세스가 콘솔에 쓰는 중
   syscall_handler()
   intr_handler()

   이런 종류의 문제는 디버깅하기가 매우 어려우므로, 깊이
   카운터로 재귀 락을 흉내 내어 문제를
   피한다. */
static int console_lock_depth;

/* 콘솔에 출력된 문자 수. */
static int64_t write_cnt;

/* 콘솔 락을 활성화한다. */
void
console_init (void) {
	lock_init (&console_lock);
	use_console_lock = true;
}

/* 커널 패닉이 진행 중임을 콘솔에 알려, 이제부터는
   콘솔 락을 획득하려 하지 않도록 한다. */
void
console_panic (void) {
	use_console_lock = false;
}

/* 콘솔 통계를 출력한다. */
void
console_print_stats (void) {
	printf ("Console: %lld characters output\n", write_cnt);
}

/* 콘솔 락을 획득한다. */
	static void
acquire_console (void) {
	if (!intr_context () && use_console_lock) {
		if (lock_held_by_current_thread (&console_lock)) 
			console_lock_depth++; 
		else
			lock_acquire (&console_lock); 
	}
}

/* 콘솔 락을 해제한다. */
static void
release_console (void) {
	if (!intr_context () && use_console_lock) {
		if (console_lock_depth > 0)
			console_lock_depth--;
		else
			lock_release (&console_lock); 
	}
}

/* 현재 스레드가 콘솔 락을 가지고 있으면 true,
   아니면 false를 반환한다. */
static bool
console_locked_by_current_thread (void) {
	return (intr_context ()
			|| !use_console_lock
			|| lock_held_by_current_thread (&console_lock));
}

/* 표준 vprintf() 함수.
   printf()와 비슷하지만 va_list를 사용한다.
   출력을 vga 화면과 시리얼 포트 양쪽에 쓴다. */
int
vprintf (const char *format, va_list args) {
	int char_cnt = 0;

	acquire_console ();
	__vprintf (format, args, vprintf_helper, &char_cnt);
	release_console ();

	return char_cnt;
}

/* 문자열 S를 콘솔에 쓰고, 이어서 개행 문자를
   쓴다. */
int
puts (const char *s) {
	acquire_console ();
	while (*s != '\0')
		putchar_have_lock (*s++);
	putchar_have_lock ('\n');
	release_console ();

	return 0;
}

/* BUFFER의 N개 문자를 콘솔에 쓴다. */
void
putbuf (const char *buffer, size_t n) {
	acquire_console ();
	while (n-- > 0)
		putchar_have_lock (*buffer++);
	release_console ();
}

/* C를 vga 화면과 시리얼 포트에 쓴다. */
int
putchar (int c) {
	acquire_console ();
	putchar_have_lock (c);
	release_console ();

	return c;
}

/* vprintf()의 도우미 함수. */
static void
vprintf_helper (char c, void *char_cnt_) {
	int *char_cnt = char_cnt_;
	(*char_cnt)++;
	putchar_have_lock (c);
}

/* C를 vga 화면과 시리얼 포트에 쓴다.
   필요한 경우 호출자가 이미 콘솔 락을 획득한
   상태이다. */
static void
putchar_have_lock (uint8_t c) {
	ASSERT (console_locked_by_current_thread ());
	write_cnt++;
	serial_putc (c);
	vga_putc (c);
}
