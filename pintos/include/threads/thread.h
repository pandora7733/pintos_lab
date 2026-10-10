#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* 스레드 생명 주기의 상태들. */
enum thread_status {
	THREAD_RUNNING,     /* 실행 중인 스레드. */
	THREAD_READY,       /* 실행 중은 아니지만 실행할 준비가 됨. */
	THREAD_BLOCKED,     /* 어떤 이벤트가 발생하기를 기다리는 중. */
	THREAD_DYING        /* 곧 파괴될 예정. */
};

/* 스레드 식별자 타입.
   원하는 어떤 타입으로든 재정의해도 된다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* tid_t의 오류 값. */

/* 스레드 우선순위. */
#define PRI_MIN 0                       /* 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. */
#define PRI_MAX 63                      /* 가장 높은 우선순위. */

/* 커널 스레드 또는 사용자 프로세스.
 *
 * 각 스레드 구조체는 자신만의 4 kB 페이지에 저장된다.  스레드
 * 구조체 자체는 페이지의 맨 아래(오프셋 0)에 위치한다.  페이지의
 * 나머지 부분은 스레드의 커널 스택을 위해 예약되어 있으며, 이 스택은
 * 페이지의 맨 위(오프셋 4 kB)에서부터 아래쪽으로 자란다.  그림으로
 * 나타내면 다음과 같다:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 이로부터 두 가지 결론이 나온다:
 *
 *    1. 첫째, `struct thread'가 너무 커지도록 두어서는 안 된다.
 *       그렇게 되면 커널 스택을 위한 공간이 부족해진다.  기본
 *       `struct thread'의 크기는 몇 바이트에 불과하다.  아마도
 *       1 kB보다 한참 작게 유지하는 것이 좋을 것이다.
 *
 *    2. 둘째, 커널 스택이 너무 커지도록 두어서는 안 된다.  스택이
 *       넘치면(overflow) 스레드 상태가 손상된다.  따라서 커널 함수는
 *       큰 구조체나 배열을 static이 아닌 지역 변수로 할당해서는 안
 *       된다.  대신 malloc()이나 palloc_get_page()를 이용한 동적
 *       할당을 사용하라.
 *
 * 이 두 문제 중 어느 쪽이든 첫 번째 증상은 아마도 thread_current()의
 * 단언(assertion) 실패일 것이다.  thread_current()는 실행 중인
 * 스레드의 `struct thread'에 있는 `magic' 멤버가 THREAD_MAGIC으로
 * 설정되어 있는지 검사한다.  스택 오버플로가 일어나면 보통 이 값이
 * 바뀌어 단언이 실패하게 된다. */
/* `elem' 멤버는 두 가지 용도를 가진다.  실행 큐(thread.c)의 원소가
 * 될 수도 있고, 세마포어 대기 리스트(synch.c)의 원소가 될 수도 있다.
 * 이렇게 두 가지로 사용할 수 있는 것은 두 경우가 서로 배타적이기
 * 때문이다: 준비(ready) 상태의 스레드만 실행 큐에 있고, 차단(blocked)
 * 상태의 스레드만 세마포어 대기 리스트에 있다. */
struct thread {
	/* thread.c가 소유한다. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름 (디버깅 용도). */
	int priority;                       /* 우선순위. */

	int64_t wakeup_tick;                 /* timer_sleep()으로 잠들었을 때 깨어날 시각(틱) */

	/* thread.c와 synch.c가 공유한다. */
	struct list_elem elem;              /* 리스트 원소. */

#ifdef USERPROG
	/* userprog/process.c가 소유한다. */
	uint64_t *pml4;                     /* 4단계 페이지 맵 (Page map level 4) */
#endif
#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리를 위한 테이블. */
	struct supplemental_page_table spt;
#endif

	/* thread.c가 소유한다. */
	struct intr_frame tf;               /* 스레드 전환을 위한 정보 */
	unsigned magic;                     /* 스택 오버플로를 감지한다. */
};

/* false(기본값)이면 라운드 로빈 스케줄러를 사용한다.
   true이면 다단계 피드백 큐(MLFQ) 스케줄러를 사용한다.
   커널 명령줄 옵션 "-o mlfqs"로 제어된다. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);
bool thread_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void thread_preempt (void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
