/* timer_sleep(-100)을 테스트한다. 유일한 요구 사항은 크래시가 나지 않는 것이다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void
test_alarm_negative (void) 
{
  timer_sleep (-100);
  pass ();
}
