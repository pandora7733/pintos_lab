/* 꽤 큰 파일을 무작위 크기 블록 단위로 하나씩 순차적으로 쓴 뒤,
   다시 읽어서 올바르게 쓰였는지
   확인한다. */

#define TEST_SIZE 5678
#include "tests/filesys/base/seq-random.inc"
