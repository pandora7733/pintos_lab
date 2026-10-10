/* 스레드 N개를 만들고, 각 스레드는 서로 다른 고정된 시간만큼
   M번 잠든다. 깨어난 순서를 기록하고 그 순서가 올바른지
   검증한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

void
test_alarm_simultaneous (void) 
{
  test_sleep (3, 5);
}

/* 테스트에 대한 정보. */
struct sleep_test 
  {
    int64_t start;              /* 테스트 시작 시점의 현재 시각. */
    int iterations;             /* 스레드당 반복 횟수. */
    int *output_pos;            /* 출력 버퍼 안의 현재 위치. */
  };

static void sleeper (void *);

/* THREAD_CNT개의 스레드를 실행하며, 각 스레드는 ITERATIONS번 잠든다. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  int *output;
  int i;

  /* 이 테스트는 MLFQS에서는 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  msg ("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
  msg ("Each thread sleeps 10 ticks each time.");
  msg ("Within an iteration, all threads should wake up on the same tick.");

  /* 메모리 할당. */
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* 테스트를 초기화한다. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  test.output_pos = output;

  /* 스레드들을 시작한다. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      char name[16];
      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, &test);
    }
  
  /* 모든 스레드가 끝날 만큼 충분히 기다린다. */
  timer_sleep (100 + iterations * 10 + 100);

  /* 완료 순서를 출력한다. */
  msg ("iteration 0, thread 0: woke up after %d ticks", output[0]);
  for (i = 1; i < test.output_pos - output; i++) 
    msg ("iteration %d, thread %d: woke up %d ticks later",
         i / thread_cnt, i % thread_cnt, output[i] - output[i - 1]);
  
  free (output);
}

/* 잠드는 스레드. */
static void
sleeper (void *test_) 
{
  struct sleep_test *test = test_;
  int i;

  /* 타이머 틱이 막 시작된 시점인지 확실히 한다. */
  timer_sleep (1);

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * 10;
      timer_sleep (sleep_until - timer_ticks ());
      *test->output_pos++ = timer_ticks () - test->start;
      thread_yield ();
    }
}
