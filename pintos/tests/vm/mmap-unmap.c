/* 파일을 매핑했다가 해제한 뒤, 매핑되었던 영역에 더 이상
   접근할 수 없는지 검증한다. */

#include <syscall.h>
#include "tests/vm/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

#define ACTUAL ((void *) 0x10000000)


void
test_main (void)
{
  int handle;
  void *map;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  CHECK ((map = mmap (ACTUAL, 0x2000, 0, handle, 0)) != MAP_FAILED, "mmap \"sample.txt\"");
  msg ("memory is readable %d", *(int *) ACTUAL);
  msg ("memory is readable %d", *(int *) ACTUAL + 0x1000);

  munmap (map);

  fail ("unmapped memory is readable (%d)", *(int *) (ACTUAL + 0x1000));
  fail ("unmapped memory is readable (%d)", *(int *) (ACTUAL));
}
