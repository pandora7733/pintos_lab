/* 유효하지 않은 fd에 쓰려 한다.
   이는 조용히 실패하거나 프로세스를 종료 코드 -1로
   종료해야 한다. */

#include <limits.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  char buf = 123;
  write (0x01012342, &buf, 1);
  write (7, &buf, 1);
  write (2546, &buf, 1);
  write (-5, &buf, 1);
  write (-8192, &buf, 1);
  write (INT_MIN + 1, &buf, 1);
  write (INT_MAX - 1, &buf, 1);
}
