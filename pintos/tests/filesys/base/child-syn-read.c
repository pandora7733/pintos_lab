/* syn-read 테스트의 자식 프로세스.
   테스트 파일의 내용을 한 번에 1바이트씩 읽는다. 이렇게 하면
   충분히 오래 걸려서 커널 파일 시스템 코드에서 상당한
   경합이 일어나기를 기대하기
   때문이다. */

#include <random.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/filesys/base/syn-read.h"

static char buf[BUF_SIZE];

int
main (int argc, const char *argv[]) 
{
  test_name = "child-syn-read";

  int child_idx;
  int fd;
  size_t i;

  quiet = true;
  
  CHECK (argc == 2, "argc must be 2, actually %d", argc);
  child_idx = atoi (argv[1]);

  random_init (0);
  random_bytes (buf, sizeof buf);

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  for (i = 0; i < sizeof buf; i++) 
    {
      char c;
      CHECK (read (fd, &c, 1) > 0, "read \"%s\"", file_name);
      compare_bytes (&c, buf + i, 1, i, file_name);
    }
  close (fd);

  return child_idx;
}

