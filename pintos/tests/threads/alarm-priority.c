/* 알람 시계가 스레드들을 깨울 때 우선순위가 더 높은
   스레드가 먼저 실행되는지 확인한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func alarm_priority_thread;
static int64_t wake_time;
static struct semaphore wait_sema;

void
test_alarm_priority (void) 
{
  int i;
  
  /* 이 테스트는 MLFQS에서는 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  wake_time = timer_ticks () + 5 * TIMER_FREQ;
  sema_init (&wait_sema, 0);
  
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, alarm_priority_thread, NULL);
    }

  thread_set_priority (PRI_MIN);

  for (i = 0; i < 10; i++)
    sema_down (&wait_sema);
}

static void
alarm_priority_thread (void *aux UNUSED) 
{
  /* 현재 시각이 바뀔 때까지 바쁜 대기를 한다. */
  int64_t start_time = timer_ticks ();
  while (timer_elapsed (start_time) == 0)
    continue;

  /* 이제 타이머 틱이 막 시작된 시점임을 알기 때문에, 시각
     확인과 타이머 인터럽트 사이의 경쟁 상태를 걱정하지 않고
     timer_sleep()을 호출할 수 있다. */
  timer_sleep (wake_time - timer_ticks ());

  /* 깨어나면 메시지를 출력한다. */
  msg ("Thread %s woke up.", thread_name ());

  sema_up (&wait_sema);
}
