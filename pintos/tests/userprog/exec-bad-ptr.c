/* exec 시스템 콜에 유효하지 않은 포인터를 넘긴다.
   프로세스는 종료 코드 -1로 종료되어야 한다. */

#include <syscall.h>
#include "tests/main.h"

void
test_main (void) 
{
  exec ((char *) 0x20101234);
}
