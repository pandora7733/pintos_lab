/* 여러 자식 프로세스를 생성하여 파일 내용의 서로 다른 부분을
   쓰게 하고, 그들이 끝나기를 기다린다. 그런 다음
   파일을 다시 읽어 내용을 검증한다. */

#include <random.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "tests/filesys/base/syn-write.h"
#include "tests/lib.h"
#include "tests/main.h"

char buf1[BUF_SIZE];
char buf2[BUF_SIZE];

void
test_main (void) 
{
  pid_t children[CHILD_CNT];
  int fd;

  CHECK (create (file_name, sizeof buf1), "create \"%s\"", file_name);

  exec_children ("child-syn-wrt", children, CHILD_CNT);
  wait_children (children, CHILD_CNT);

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  CHECK (read (fd, buf1, sizeof buf1) > 0, "read \"%s\"", file_name);
  random_bytes (buf2, sizeof buf2);
  compare_bytes (buf1, buf2, sizeof buf1, 0, file_name);
}
