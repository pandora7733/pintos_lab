/* 빈 문자열을 이름으로 파일을 열려고 시도한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle = open ("");
  if (handle != -1)
    fail ("open() returned %d instead of -1", handle);
}
