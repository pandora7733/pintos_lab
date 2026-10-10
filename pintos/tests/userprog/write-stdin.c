/* fd 0(stdin)에 써 본다.
   이는 그냥 실패하거나 프로세스를 종료 코드 -1로
   종료할 수 있다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  char buf = 123;
  write (0, &buf, 1);
}
