/* 기존 파일과 같은 이름의 디렉터리를 만들려고 시도한다.
   이는 반드시 실패해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (create ("abc", 0), "create \"abc\"");
  CHECK (!mkdir ("abc"), "mkdir \"abc\" (must return false)");
}
