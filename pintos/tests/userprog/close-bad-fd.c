/* 유효하지 않은 fd를 닫으려 한다. 이는 조용히 실패하거나
   종료 코드 -1로 종료되어야 한다. */

#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  close (0x20101234);
}
