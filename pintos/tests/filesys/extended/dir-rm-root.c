/* 루트 디렉터리를 제거하려고 시도한다.
   이는 반드시 실패해야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (!remove ("/"), "remove \"/\" (must fail)");
  CHECK (create ("/a", 243), "create \"/a\"");
}
