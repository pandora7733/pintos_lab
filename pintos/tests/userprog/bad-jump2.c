/* 이 프로그램은 커널 가상 주소의 코드를 실행하려 한다.
   이 경우 프로세스는 종료 코드 -1로 종료되어야 한다. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("Congratulations - you have successfully called kernel code: %d", 
        ((int (*)(void))0x8004000000)());
  fail ("should have exited with -1");
}
