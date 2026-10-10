/* 유효하지 않은 오프셋으로 mmap하려 한다.
   이는 조용히 실패하거나 프로세스를 종료 코드 -1로
   종료해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  CHECK ((handle = open ("large.txt")) > 1, "open \"large.txt\"");

  CHECK (mmap ((void *) 0x10000000, 4096, 0, handle, 0x1234) == MAP_FAILED,
         "try to mmap invalid offset");
}
