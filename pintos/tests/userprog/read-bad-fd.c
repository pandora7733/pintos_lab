/* 유효하지 않은 fd에서 읽으려 한다.
   이는 조용히 실패하거나 프로세스를 종료 코드 -1로
   종료해야 한다. */

#include <limits.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  char buf;
  read (0x20101234, &buf, 1);
  read (5, &buf, 1);
  read (1234, &buf, 1);
  read (-1, &buf, 1);
  read (-1024, &buf, 1);
  read (INT_MIN, &buf, 1);
  read (INT_MAX, &buf, 1);
}
