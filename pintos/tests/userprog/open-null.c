/* 널 포인터를 이름으로 파일을 열려고 시도한다.
   프로세스는 종료 코드 -1로 종료되어야 한다. */

#include <stddef.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  open (NULL);
}
