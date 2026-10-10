/* wait-killed 테스트가 실행하는 자식 프로세스.
   pintos를 실행하려 한다. Pintos에는 `pintos`가 없으므로
   프로세스는 종료 코드 -1로 종료되어야 한다. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  exec ("pintos");
  fail ("should have exited with -1");
}
