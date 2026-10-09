/* 하위 프로세스가 끝나기를 두 번 기다린다.
   첫 번째 호출은 보통 방식대로 기다리고 종료 코드를 반환해야 한다.
   두 번째 wait 호출은 즉시 -1을 반환해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t child;
  if ((child = fork ("child-simple"))){
    msg ("wait(exec()) = %d", wait (child));
    msg ("wait(exec()) = %d", wait (child));
  } else {
    exec ("child-simple");
  }
}
