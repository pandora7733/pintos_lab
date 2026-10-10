/* mmap-inherit 테스트의 자식 프로세스.
   부모에 존재하는 매핑에 쓰려 한다.
   프로세스는 종료 코드 -1로 종료되어야 한다. */

#include <string.h>
#include "tests/vm/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  memset ((char *) 0x54321000, 0, 4096);
  fail ("child can modify parent's memory mappings");
}

