/* 파일을 연 뒤, 그 파일을 닫으려 하는 하위 프로세스를 실행한다.
   (Pintos에는 파일 핸들 상속이 없으므로 이는 실패해야
   한다.) 그런 다음 부모 프로세스가 파일 핸들을 사용하려 하며,
   이는 성공해야 한다. */

#include <stdio.h>
#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  char child_cmd[128];
  int handle;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  snprintf (child_cmd, sizeof child_cmd, "child-close %d", handle);
  
  pid_t pid;
  if (!(pid = fork("child-close"))){
    exec (child_cmd);
  }
  msg ("wait(exec()) = %d", wait (pid));

  check_file_handle (handle, "sample.txt", sample, sizeof sample - 1);
}
