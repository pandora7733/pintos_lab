/* 이름이 두 페이지의 경계에 걸쳐 있는 파일을 연다.
   이는 유효하므로 반드시 성공해야 한다. */

#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("create(\"quux.dat\"): %d",
       create (copy_string_across_boundary ("quux.dat"), 0));
}
