/* 디스크가 가득 찰 때까지 아주 깊은 "덩굴" 모양의 디렉터리
   /dir0/dir1/dir2/...와 각 디렉터리 안의 일반 파일을
   만든다.

   그런 다음 두 가지 이유로 대부분을 삭제한다. 첫째, "tar"는
   파일 이름을 100자로 제한한다(큰 어려움 없이 256자로 늘릴
   수는 있다). 둘째, 꽉 찬 디스크에는 tar 아카이브를 위한
   공간이 없다. */

#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int i;

  msg ("creating many levels of files and directories...");
  quiet = true;
  CHECK (mkdir ("start"), "mkdir \"start\"");
  CHECK (chdir ("start"), "chdir \"start\"");
  for (i = 0; ; i++) 
    {
      char name[3][READDIR_MAX_LEN + 1];
      char file_name[16], dir_name[16];
      char contents[128];
      int fd;

      /* 파일을 만든다. */
      snprintf (file_name, sizeof file_name, "file%d", i);
      if (!create (file_name, 0))
        break;
      CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
      snprintf (contents, sizeof contents, "contents %d\n", i);
      if (write (fd, contents, strlen (contents)) != (int) strlen (contents)) 
        {
          CHECK (remove (file_name), "remove \"%s\"", file_name);
          close (fd);
          break;
        }
      close (fd);
      
      /* 디렉터리를 만든다. */
      snprintf (dir_name, sizeof dir_name, "dir%d", i);
      if (!mkdir (dir_name)) 
        {
          CHECK (remove (file_name), "remove \"%s\"", file_name);
          break; 
        }

      /* 파일과 디렉터리를 확인한다. */
      CHECK ((fd = open (".")) > 1, "open \".\"");
      CHECK (readdir (fd, name[0]), "readdir \".\"");
      CHECK (readdir (fd, name[1]), "readdir \".\"");
      CHECK (!readdir (fd, name[2]), "readdir \".\" (should fail)");
      CHECK ((!strcmp (name[0], dir_name) && !strcmp (name[1], file_name))
             || (!strcmp (name[1], dir_name) && !strcmp (name[0], file_name)),
             "names should be \"%s\" and \"%s\", "
             "actually \"%s\" and \"%s\"",
             file_name, dir_name, name[0], name[1]);
      close (fd);

      /* 디렉터리 안으로 내려간다. */
      CHECK (chdir (dir_name), "chdir \"%s\"", dir_name);
    }
  CHECK (i > 200, "created files and directories only to level %d", i);
  quiet = false;

  msg ("removing all but top 10 levels of files and directories...");
  quiet = true;
  while (i-- > 10) 
    {
      char file_name[16], dir_name[16];

      snprintf (file_name, sizeof file_name, "file%d", i);
      snprintf (dir_name, sizeof dir_name, "dir%d", i);
      CHECK (chdir (".."), "chdir \"..\"");
      CHECK (remove (dir_name), "remove \"%s\"", dir_name);
      CHECK (remove (file_name), "remove \"%s\"", file_name);
    }
  quiet = false;
}
