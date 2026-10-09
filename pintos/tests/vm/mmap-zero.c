/* 길이 0인 파일을 매핑하려 한다. 이는 동작할 수도, 동작하지 않을
   수도 있지만 프로세스를 종료하거나 크래시를 일으키면 안 된다.
   그런 다음 매핑하려 했던 주소를 역참조하면,
   프로세스는 종료 코드 -1로 종료되어야 한다. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char *data = (char *) 0x7f000000;
  int handle;

  CHECK (create ("empty", 0), "create empty file \"empty\"");
  CHECK ((handle = open ("empty")) > 1, "open \"empty\"");

  /* mmap() 호출은 성공할 수도 실패할 수도 있다. 상관없다. */
  msg ("mmap \"empty\"");
  mmap (data, 0, 0, handle, 0);

  /* 호출이 성공했는지와 관계없이, *data는 프로세스를
     종료시켜야 한다. */
  fail ("unmapped memory is readable (%d)", *data);
}

