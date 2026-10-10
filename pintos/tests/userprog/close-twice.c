/* 파일을 연 뒤 두 번 닫으려 한다. 두 번째 close는
   조용히 실패하거나 종료 코드 -1로 종료되어야
   한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  msg ("close \"sample.txt\"");
  close (handle);
  msg ("close \"sample.txt\" again");
  close (handle);
}
