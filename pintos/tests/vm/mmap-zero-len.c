/* 길이 0으로 mmap을 시도한다. 길이가 0이면 mmap은 실패해야 한다.
 * 2.6.12 이전의 리눅스 커널에서는 길이가 0이어도 mmap이 성공했다.
 * 이 경우 매핑은 생성되지 않고 호출은 addr를 반환했다.
 * 커널 2.6.12부터는 길이 0의 매핑은 실패한다. Pintos의 mmap도
 * 길이가 0이면 실패(즉, MAP_FAILED 반환)할 것으로 기대한다. */

#include <string.h>
#include <syscall.h>
#include "tests/vm/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

#define ACTUAL ((void *) 0x10000000)

void
test_main (void)
{
  int handle;
  void *map;

  /* mmap을 통해 파일에 쓴다. */
  CHECK (create ("sample.txt", strlen (sample)), "create \"sample.txt\"");
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  CHECK ((map = mmap (ACTUAL, 0, 0, handle, 0)) == MAP_FAILED, 
			"try to mmap zero length");
 
}
