/* syn-rw의 자식 프로세스.
   부모 프로세스가 만들어 계속 늘리고 있는 파일에서 읽는다.
   파일 전체를 성공적으로 읽을 때까지 반복한다. 그동안 파일이
   늘어나지 않았기 때문에 루프의 많은 반복에서 0바이트가
   반환된다. 즉, 파일이 늘어나기를 "바쁜 대기"하는 것이다.
   (이 테스트는 "yield" 시스템 콜을 추가하고 0바이트를 읽을
   때마다 yield를 호출하도록 하면 개선할 수 있다.) */

#include <random.h>
#include <stdlib.h>
#include <syscall.h>
#include "tests/filesys/extended/syn-rw.h"
#include "tests/lib.h"

static char buf1[BUF_SIZE];
static char buf2[BUF_SIZE];

int
main (int argc, const char *argv[]) 
{
  test_name = "child-syn-rw";

  int child_idx;
  int fd;
  size_t ofs;

  quiet = true;
  
  CHECK (argc == 2, "argc must be 2, actually %d", argc);
  child_idx = atoi (argv[1]);

  random_init (0);
  random_bytes (buf1, sizeof buf1);

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  ofs = 0;
  while (ofs < sizeof buf2)
    {
      int bytes_read = read (fd, buf2 + ofs, sizeof buf2 - ofs);
      CHECK (bytes_read >= -1 && bytes_read <= (int) (sizeof buf2 - ofs),
             "%zu-byte read on \"%s\" returned invalid value of %d",
             sizeof buf2 - ofs, file_name, bytes_read);
      if (bytes_read > 0) 
        {
          compare_bytes (buf2 + ofs, buf1 + ofs, bytes_read, ofs, file_name);
          ofs += bytes_read;
        }
    }
  close (fd);

  return child_idx;
}
