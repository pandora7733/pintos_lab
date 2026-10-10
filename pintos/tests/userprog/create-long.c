/* 너무 긴 이름으로 파일을 만들려고 시도한다.
   이는 반드시 실패해야 한다. */

#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  static char name[512];
  memset (name, 'x', sizeof name);
  name[sizeof name - 1] = '\0';
  
  msg ("create(\"x...\"): %d", create (name, 0));
}
