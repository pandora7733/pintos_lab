/* 기존 디렉터리와 같은 이름의 파일을 만들려고 시도한다.
   이는 반드시 실패해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (mkdir ("abc"), "mkdir \"abc\"");
  CHECK (!create ("abc", 0), "create \"abc\" (must return false)");
}
