/* 코드 세그먼트에 쓰려고 시도한다.
   프로세스는 종료 코드 -1로 종료되어야 한다. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  *(int *) test_main = 0;
  fail ("writing the code segment succeeded");
}
