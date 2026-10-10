/* 빈 문자열을 이름으로 디렉터리를 만들려고 시도한다.
   이는 반드시 실패해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (!mkdir (""), "mkdir \"\" (must return false)");
}
