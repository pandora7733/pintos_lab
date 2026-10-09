/* 한 가상 페이지에서 다른 가상 페이지로 걸쳐 있는 데이터를
   넘겨서 시스템 콜을 망가뜨리려 하는 테스트들을 위한 유틸리티
   함수. */

#include <inttypes.h>
#include <round.h>
#include <string.h>
#include "tests/userprog/boundary.h"

static char dst[8192];

/* 페이지의 시작 주소를 반환한다. 반환된 포인터의 양쪽으로
   수정 가능한 바이트가 최소 2048개씩 있다. */
void *
get_boundary_area (void) 
{
  char *p = (char *) ROUND_UP ((uintptr_t) dst, 4096);
  if (p - dst < 2048)
    p += 4096;
  return p;
}

/* 두 페이지의 경계에 걸쳐 나뉜 SRC의 복사본을
   반환한다. */
char *
copy_string_across_boundary (const char *src) 
{
  char *p = get_boundary_area ();
  p -= strlen (src) < 4096 ? strlen (src) / 2 : 4096;
  strlcpy (p, src, 4096);
  return p;
}

