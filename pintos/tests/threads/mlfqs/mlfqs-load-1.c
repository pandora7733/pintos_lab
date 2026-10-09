/* 바쁜 스레드 하나가 38~45초 안에 load average를 0.5로
   올리는지 검증한다. 예상 시간은 42초이며, 다음으로
   확인할 수 있다:
   perl -e '$i++,$a=(59*$a+1)/60while$a<=.5;print "$i\n"'

   그런 다음, 10초 동안 아무 활동이 없으면 load average가
   다시 0.5 아래로 떨어지는지 검증한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void
test_mlfqs_load_1 (void) 
{
  int64_t start_time;
  int elapsed;
  int load_avg;
  
  ASSERT (thread_mlfqs);

  msg ("spinning for up to 45 seconds, please wait...");

  start_time = timer_ticks ();
  for (;;) 
    {
      load_avg = thread_get_load_avg ();
      ASSERT (load_avg >= 0);
      elapsed = timer_elapsed (start_time) / TIMER_FREQ;
      if (load_avg > 100)
        fail ("load average is %d.%02d "
              "but should be between 0 and 1 (after %d seconds)",
              load_avg / 100, load_avg % 100, elapsed);
      else if (load_avg > 50)
        break;
      else if (elapsed > 45)
        fail ("load average stayed below 0.5 for more than 45 seconds");
    }

  if (elapsed < 38)
    fail ("load average took only %d seconds to rise above 0.5", elapsed);
  msg ("load average rose to 0.5 after %d seconds", elapsed);

  msg ("sleeping for another 10 seconds, please wait...");
  timer_sleep (TIMER_FREQ * 10);

  load_avg = thread_get_load_avg ();
  if (load_avg < 0)
    fail ("load average fell below 0");
  if (load_avg > 50)
    fail ("load average stayed above 0.5 for more than 10 seconds");
  msg ("load average fell back below 0.5 (to %d.%02d)",
       load_avg / 100, load_avg % 100);

  pass ();
}
