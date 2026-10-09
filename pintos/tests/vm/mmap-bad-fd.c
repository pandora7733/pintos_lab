/* 유효하지 않은 fd를 mmap하려 한다.
   이는 조용히 실패하거나 프로세스를 종료 코드 -1로
   종료해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (mmap ((void *) 0x10000000, 4096, 0, 0x5678, 0) == MAP_FAILED,
         "try to mmap invalid fd");
}

