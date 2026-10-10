/* 꽤 작은 파일의 내용을 무작위 순서로 쓴 뒤, 다시 무작위 순서로
   읽어서 올바르게 쓰였는지
   확인한다. */

#define BLOCK_SIZE 13
#define TEST_SIZE (13 * 123)
#include "tests/filesys/base/random.inc"
