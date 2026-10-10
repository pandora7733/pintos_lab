#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 `magic' 멤버에 쓰이는 임의의 값.
   스택 오버플로를 감지하는 데 사용된다.  자세한 내용은 thread.h
   맨 위의 큰 주석을 참고하라. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드를 위한 임의의 값.
   이 값은 수정하지 말 것. */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태에 있는 프로세스들의 리스트.  즉, 실행할 준비는
   되었지만 실제로 실행 중이지는 않은 프로세스들이다. */
static struct list ready_list;

/* 유휴(idle) 스레드. */
static struct thread *idle_thread;

/* 최초 스레드.  init.c:main()을 실행하고 있는 스레드이다. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 락. */
static struct lock tid_lock;

/* 스레드 파괴 요청 목록 */
static struct list destruction_req;

/* 통계. */
static long long idle_ticks;    /* 유휴 상태로 보낸 타이머 틱 수. */
static long long kernel_ticks;  /* 커널 스레드에서 보낸 타이머 틱 수. */
static long long user_ticks;    /* 사용자 프로그램에서 보낸 타이머 틱 수. */

/* 스케줄링. */
#define TIME_SLICE 4            /* 각 스레드에 주어지는 타이머 틱 수. */
static unsigned thread_ticks;   /* 마지막 양보(yield) 이후 지난 타이머 틱 수. */

/* false(기본값)이면 라운드 로빈 스케줄러를 사용한다.
   true이면 다단계 피드백 큐(MLFQ) 스케줄러를 사용한다.
   커널 명령줄 옵션 "-o mlfqs"로 제어된다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드를 가리키는 것으로 보이면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 실행 중인 스레드를 반환한다.
 * CPU의 스택 포인터 `rsp'를 읽은 다음, 그 값을 페이지의 시작
 * 주소로 내림(round down)한다.  `struct thread'는 항상 페이지의
 * 시작 부분에 있고 스택 포인터는 그 페이지 중간 어딘가에 있으므로,
 * 이렇게 하면 현재 스레드의 위치를 찾을 수 있다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start를 위한 전역 디스크립터 테이블(GDT).
// GDT는 thread_init 이후에 설정되므로, 그 전에 임시 GDT를
// 먼저 설정해 두어야 한다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 현재 실행 중인 코드를 하나의 스레드로 변환하여 스레드 시스템을
   초기화한다.  이런 방식은 일반적으로는 동작할 수 없으며, 이 경우에
   가능한 이유는 loader.S가 스택의 바닥을 페이지 경계에 맞춰 두도록
   신경 썼기 때문이다.

   또한 실행 큐(run queue)와 tid 락을 초기화한다.

   이 함수를 호출한 뒤 thread_create()로 스레드를 만들기 전에
   반드시 페이지 할당자를 먼저 초기화해야 한다.

   이 함수가 끝나기 전에는 thread_current()를 호출하는 것이
   안전하지 않다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 커널용 임시 GDT를 다시 로드한다.
	 * 이 GDT에는 사용자 컨텍스트가 포함되어 있지 않다.
	 * 커널은 gdt_init ()에서 사용자 컨텍스트를 포함한 GDT를 다시 만든다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 전역 스레드 컨텍스트를 초기화한다 */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* 현재 실행 중인 스레드를 위한 스레드 구조체를 설정한다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작한다.
   또한 유휴 스레드를 생성한다. */
void
thread_start (void) {
	/* 유휴 스레드를 생성한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 선점형 스레드 스케줄링을 시작한다. */
	intr_enable ();

	/* 유휴 스레드가 idle_thread를 초기화할 때까지 기다린다. */
	sema_down (&idle_started);
}

/* 매 타이머 틱마다 타이머 인터럽트 핸들러가 호출한다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행된다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계를 갱신한다. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점을 강제한다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 스레드 통계를 출력한다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 이름이 NAME이고 초기 우선순위가 PRIORITY인 새 커널 스레드를
   생성한다.  이 스레드는 AUX를 인자로 넘겨 FUNCTION을 실행하며,
   생성 후 준비 큐(ready queue)에 추가된다.  새 스레드의 스레드
   식별자를 반환하고, 생성에 실패하면 TID_ERROR를 반환한다.

   thread_start()가 이미 호출되었다면, 새 스레드는 thread_create()가
   반환되기 전에 스케줄될 수도 있다.  심지어 thread_create()가
   반환되기 전에 종료될 수도 있다.  반대로, 원래 스레드가 새 스레드가
   스케줄되기 전까지 얼마든지 오래 실행될 수도 있다.  실행 순서를
   보장해야 한다면 세마포어나 다른 형태의 동기화를 사용하라.

   제공된 코드는 새 스레드의 `priority' 멤버를 PRIORITY로 설정하지만,
   실제 우선순위 스케줄링은 구현되어 있지 않다.
   우선순위 스케줄링은 Problem 1-3의 목표이다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 스레드를 할당한다. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드를 초기화한다. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 스케줄되면 kernel_thread를 호출한다.
	 * 참고) rdi는 첫 번째 인자, rsi는 두 번째 인자이다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 큐에 추가한다. */
	thread_unblock (t);

	thread_preempt(); // 새로 생성된 스레드의 우선순위가 현재 스레드보다 높으면 CPU를 양보

	return tid;
}

/* 현재 스레드를 잠재운다.  thread_unblock()으로 깨워지기 전까지는
   다시 스케줄되지 않는다.

   이 함수는 인터럽트가 꺼진 상태에서 호출해야 한다.  보통은
   synch.h에 있는 동기화 기본 요소(primitive) 중 하나를 사용하는
   것이 더 좋은 방법이다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* 차단(blocked)된 스레드 T를 실행 준비(ready-to-run) 상태로
   전환한다.  T가 차단 상태가 아니면 오류이다.  (실행 중인 스레드를
   준비 상태로 만들려면 thread_yield()를 사용하라.)

   이 함수는 실행 중인 스레드를 선점하지 않는다.  이 점은 중요할 수
   있다: 호출자가 직접 인터럽트를 꺼 두었다면, 스레드의 차단을
   해제하고 다른 데이터를 갱신하는 작업을 원자적으로 수행할 수 있다고
   기대할 수 있기 때문이다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL); // ready_list에 삽입, 우선순위 내림차순 정렬
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 실행 중인 스레드의 이름을 반환한다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 실행 중인 스레드를 반환한다.
   running_thread()에 몇 가지 정상성 검사(sanity check)를 더한 것이다.
   자세한 내용은 thread.h 맨 위의 큰 주석을 참고하라. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* T가 정말 스레드인지 확인한다.
	   이 두 단언(assertion) 중 하나라도 실패한다면, 스레드의 스택이
	   넘쳤을(overflow) 가능성이 있다.  각 스레드의 스택은 4 kB보다
	   작기 때문에, 큰 자동(지역) 배열 몇 개나 적당한 깊이의 재귀만으로도
	   스택 오버플로가 발생할 수 있다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 실행 중인 스레드의 tid를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드를 스케줄에서 제외하고 파괴한다.  호출자에게
   절대 돌아가지 않는다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* 상태를 dying으로 설정하고 다른 프로세스를 스케줄하기만 한다.
	   실제 파괴는 schedule_tail()을 호출하는 동안 이루어진다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* CPU를 양보한다.  현재 스레드는 잠들지 않으며, 스케줄러의 판단에
   따라 곧바로 다시 스케줄될 수도 있다. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered (&ready_list, &curr->elem, thread_priority_greater, NULL); // ready_list에 삽입, 우선순위 내림차순 정렬
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정한다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;

	/* 우선순위를 낮춘 결과 더 높은 READY 스레드가 생겼다면 즉시 양보한다. */
	thread_preempt();
}

/* 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}


/* A 스레드의 우선순위가 B보다 높으면 true를 반환한다.
listen_insert_ordered()에 넘기면 리스트가 우선순위 내림차순으로 정렬되고,
우선순위가 같은 스레드끼리는 먼저 들어온 순서(FIFO)가 유지됨 */
bool
thread_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);

	return ta->priority > tb->priority;
}

/* 
ready_list 맨 앞 스레드의 우선순위가 현재 스레드보다 높으면 CPU를 양보함
ready_list는 우선순위 내리마순으로 정렬되어 있으므로 맨 앞만 보면 됨.
인터럽트 핸들러 안에서는 thread_yield()를 쓸 수 없기 때문에 핸들러가 끝날 때 양보하도록 예약함
*/
void
thread_preempt (void) {
	enum intr_level old_level = intr_disable ();

	if (!list_empty (&ready_list)) {
		struct thread *front = list_entry (list_front (&ready_list), struct thread, elem);

		if (front->priority > thread_current ()->priority) {
			if (intr_context ()) {
				intr_yield_on_return ();
			} else {
				thread_yield ();
			}
		}
	}

	intr_set_level (old_level);
}

/* 현재 스레드의 nice 값을 NICE로 설정한다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: 여기에 구현하라 */
}

/* 현재 스레드의 nice 값을 반환한다. */
int
thread_get_nice (void) {
	/* TODO: 여기에 구현하라 */
	return 0;
}

/* 시스템 부하 평균(load average)의 100배를 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: 여기에 구현하라 */
	return 0;
}

/* 현재 스레드의 recent_cpu 값의 100배를 반환한다. */
int
thread_get_recent_cpu (void) {
	/* TODO: 여기에 구현하라 */
	return 0;
}

/* 유휴 스레드.  실행할 준비가 된 다른 스레드가 없을 때 실행된다.

   유휴 스레드는 처음에 thread_start()에 의해 준비 리스트에 들어간다.
   처음에 한 번 스케줄되며, 그때 idle_thread를 초기화하고, 전달받은
   세마포어를 "up"하여 thread_start()가 계속 진행할 수 있게 한 뒤,
   곧바로 차단(block)된다.  그 이후로 유휴 스레드는 준비 리스트에
   나타나지 않는다.  준비 리스트가 비어 있을 때 next_thread_to_run()이
   특별한 경우로서 유휴 스레드를 반환한다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* 다른 스레드가 실행되도록 한다. */
		intr_disable ();
		thread_block ();

		/* 인터럽트를 다시 켜고 다음 인터럽트를 기다린다.

		   `sti' 명령어는 바로 다음 명령어가 완료될 때까지 인터럽트를
		   비활성화된 상태로 유지하므로, 이 두 명령어는 원자적으로
		   실행된다.  이 원자성은 중요하다.  그렇지 않으면 인터럽트를
		   다시 켠 시점과 다음 인터럽트를 기다리기 시작하는 시점 사이에
		   인터럽트가 처리되어, 최대 한 클록 틱만큼의 시간을 낭비할 수
		   있다.

		   [IA32-v2a] "HLT", [IA32-v2b] "STI", [IA32-v3a]
		   7.11.1 "HLT Instruction"을 참고하라. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 기반으로 사용되는 함수. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* 스케줄러는 인터럽트가 꺼진 상태로 실행된다. */
	function (aux);       /* 스레드 함수를 실행한다. */
	thread_exit ();       /* function()이 반환되면 스레드를 종료한다. */
}


/* T를 이름이 NAME인 차단(blocked) 상태의 스레드로 기본 초기화한다. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* 다음에 스케줄될 스레드를 골라서 반환한다.  실행 큐가 비어 있지
   않다면 실행 큐에서 스레드를 반환해야 한다.  (실행 중인 스레드가
   계속 실행될 수 있다면, 그 스레드는 실행 큐에 들어 있을 것이다.)
   실행 큐가 비어 있다면 idle_thread를 반환한다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* iretq를 사용하여 스레드를 시작한다 */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고, 이전
   스레드가 dying 상태라면 그 스레드를 파괴한다.

   이 함수가 호출된 시점에는 막 스레드 PREV에서 전환된 상태이며,
   새 스레드는 이미 실행 중이고 인터럽트는 여전히 비활성화되어 있다.

   스레드 전환이 완료되기 전에는 printf()를 호출하는 것이 안전하지
   않다.  실제로는 printf()를 함수의 끝부분에 추가해야 한다는 뜻이다. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* 핵심 전환 로직.
	 * 먼저 전체 실행 컨텍스트를 intr_frame에 저장(복원 지점으로 기록)한
	 * 다음, do_iret를 호출하여 다음 스레드로 전환한다.
	 * 전환이 끝날 때까지는 여기서부터 어떤 스택도 사용해서는
	 * 안 된다는 점에 주의하라. */
	__asm __volatile (
			/* 사용할 레지스터들을 저장한다. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 입력값을 한 번만 가져온다 */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // 저장해 둔 rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // 저장해 둔 rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // 저장해 둔 rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // 현재 rip를 읽는다.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* 새 프로세스를 스케줄한다.  진입 시점에 인터럽트는 꺼져 있어야 한다.
 * 이 함수는 현재 스레드의 상태를 status로 바꾼 다음,
 * 실행할 다른 스레드를 찾아 그 스레드로 전환한다.
 * schedule() 안에서 printf()를 호출하는 것은 안전하지 않다. */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* 다음 스레드를 실행 중으로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새 타임 슬라이스를 시작한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새 주소 공간을 활성화한다. */
	process_activate (next);
#endif

	if (curr != next) {
		/* 전환되기 전의 스레드가 dying 상태라면 그 struct thread를
		   파괴한다.  thread_exit()가 자기 발밑의 양탄자를 스스로
		   빼 버리지 않도록(자신이 쓰는 메모리를 먼저 해제하지 않도록)
		   이 작업은 늦게 이루어져야 한다.
		   해당 페이지는 현재 스택으로 사용되고 있으므로 여기서는 페이지
		   해제 요청을 큐에 넣기만 한다.
		   실제 파괴 로직은 schedule()의 시작 부분에서 호출된다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* 스레드를 전환하기 전에, 먼저 현재 실행 중인 스레드의
		 * 정보를 저장한다. */
		thread_launch (next);
	}
}

/* 새 스레드에 사용할 tid를 반환한다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
