/* 메인 스레드는 자신의 우선순위를 PRI_MIN으로 설정하고, 우선순위가
   PRI_MIN + 3, 6, 9, 12, ...인 스레드 7개(thread 1..7)를 만든다.
   메인 스레드는 락 8개(lock 0..7)를 초기화하고 lock 0을 획득한다.

   thread[i]가 시작하면 먼저 lock[i]를 획득한다(i == 7인 경우 제외).
   이어서 thread[i]는 lock[i-1]을 획득하려 하는데, 이 락은
   thread[i-1]이 보유하고 있다(lock[0]은 메인 스레드가 보유).
   락이 이미 보유되어 있으므로 thread[i]는 thread[i-1]에게 우선순위를
   기부하고, thread[i-1]은 thread[i-2]에게 기부하는 식으로 이어져
   마침내 메인 스레드가 기부를 받는다.

   threads[1..7]이 생성되어 locks[0..7]에서 블록된 뒤, 메인
   스레드가 lock[0]을 해제하면 thread[1]의 블록이 풀리고 메인
   스레드는 thread[1]에게 선점된다.
   그러면 thread[1]은 lock[0] 획득을 마치고, lock[0]을 해제한 뒤,
   lock[1]을 해제하여 thread[2]의 블록을 푸는 식으로 진행된다.
   마지막으로 thread[7]이 lock[7]을 획득하고 해제한 뒤 종료하면,
   thread[6], thread[5] 등이 차례로 실행되고 종료하며 마침내
   메인 스레드가 종료한다.

   추가로, 우선순위 p = PRI_MIN + 2, 5, 8, 11, ...의 끼어드는
   (interloper) 스레드들이 생성되는데, 이들은 우선순위가 p + 1인
   대응 스레드가 끝날 때까지 실행되면 안 된다.

   작성자: Godmar Back <gback@cs.vt.edu> */ 

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define NESTING_DEPTH 8

struct lock_pair
  {
    struct lock *second;
    struct lock *first;
  };

static thread_func donor_thread_func;
static thread_func interloper_thread_func;

void
test_priority_donate_chain (void) 
{
  int i;  
  struct lock locks[NESTING_DEPTH - 1];
  struct lock_pair lock_pairs[NESTING_DEPTH];

  /* 이 테스트는 MLFQS에서는 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  thread_set_priority (PRI_MIN);

  for (i = 0; i < NESTING_DEPTH - 1; i++)
    lock_init (&locks[i]);

  lock_acquire (&locks[0]);
  msg ("%s got lock.", thread_name ());

  for (i = 1; i < NESTING_DEPTH; i++)
    {
      char name[16];
      int thread_priority;

      snprintf (name, sizeof name, "thread %d", i);
      thread_priority = PRI_MIN + i * 3;
      lock_pairs[i].first = i < NESTING_DEPTH - 1 ? locks + i: NULL;
      lock_pairs[i].second = locks + i - 1;

      thread_create (name, thread_priority, donor_thread_func, lock_pairs + i);
      msg ("%s should have priority %d.  Actual priority: %d.",
          thread_name (), thread_priority, thread_get_priority ());

      snprintf (name, sizeof name, "interloper %d", i);
      thread_create (name, thread_priority - 1, interloper_thread_func, NULL);
    }

  lock_release (&locks[0]);
  msg ("%s finishing with priority %d.", thread_name (),
                                         thread_get_priority ());
}

static void
donor_thread_func (void *locks_) 
{
  struct lock_pair *locks = locks_;

  if (locks->first)
    lock_acquire (locks->first);

  lock_acquire (locks->second);
  msg ("%s got lock", thread_name ());

  lock_release (locks->second);
  msg ("%s should have priority %d. Actual priority: %d", 
        thread_name (), (NESTING_DEPTH - 1) * 3,
        thread_get_priority ());

  if (locks->first)
    lock_release (locks->first);

  msg ("%s finishing with priority %d.", thread_name (),
                                         thread_get_priority ());
}

static void
interloper_thread_func (void *arg_ UNUSED)
{
  msg ("%s finished.", thread_name ());
}

// vim: sw=2
