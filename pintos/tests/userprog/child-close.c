/* multi-child-fd 테스트가 실행하는 자식 프로세스.

   첫 번째 커맨드 라인 인자로 전달된 파일 디스크립터를 닫으려
   한다. KAIST의 새 Pintos는 fork() 시스템 콜에서 exec()을
   거쳐도 열린 파일 디스크립터를 상속하므로,
   이는 잘 동작해야 한다 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"

int
main (int argc UNUSED, char *argv[]) 
{
  test_name = "child-close";

  msg ("begin");
  
  if (!isdigit (*argv[1]))
    fail ("bad command-line arguments");
  
  int handle = atoi (argv[1]);
  check_file_handle (handle, "sample.txt", sample, sizeof sample - 1);

  close (handle);
  msg ("end");

  return 0;
}
