/* 이 프로그램은 매핑되지 않은 주소의 메모리에 쓰려 한다.
   이 경우 프로세스는 종료 코드 -1로 종료되어야 한다. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  *(int *)NULL = 42;
  fail ("should have exited with -1");
}
