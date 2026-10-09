/* 가장 일반적인 방법으로 파일을 읽어 본다. */

#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  check_file ("sample.txt", sample, sizeof sample - 1);
}
