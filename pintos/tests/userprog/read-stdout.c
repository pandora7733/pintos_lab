/* fd 1(stdout)에서 읽어 본다.
   이는 그냥 실패하거나 프로세스를 종료 코드 -1로
   종료할 수 있다. */

#include <stdio.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  char buf;
  read (STDOUT_FILENO, &buf, 1);
}
