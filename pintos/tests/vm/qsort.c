#include "tests/vm/qsort.h"
#include <stdbool.h>
#include <debug.h>
#include <random.h>

/* BUF의 SIZE 바이트 중에서 퀵 정렬의 피벗을 고른다. */
static unsigned char
pick_pivot (unsigned char *buf, size_t size) 
{
  ASSERT (size >= 1);
  return buf[random_ulong () % size];
}

/* ARRAY의 SIZE 바이트가, 모두 PIVOT보다 작은 앞쪽 LEFT_SIZE개의
   원소와 그 뒤의 모두 PIVOT보다 크거나 같은 SIZE - LEFT_SIZE개의
   원소로 나뉘어 있는지
   확인한다. */
static bool
is_partitioned (const unsigned char *array, size_t size,
                unsigned char pivot, size_t left_size) 
{
  size_t i;
  
  for (i = 0; i < left_size; i++)
    if (array[i] >= pivot)
      return false;

  for (; i < size; i++)
    if (array[i] < pivot)
      return false;

  return true;
}

/* *A와 *B의 바이트를 서로 바꾼다. */
static void
swap (unsigned char *a, unsigned char *b) 
{
  unsigned char t = *a;
  *a = *b;
  *b = t;
}

/* ARRAY를 제자리에서 분할하여, 모두 PIVOT보다 작은 바이트들의
   앞쪽 구간과 그 뒤의 모두 PIVOT보다 크거나 같은 바이트들의
   구간으로 만든다. 앞쪽 구간의 길이를 반환한다. */
static size_t
partition (unsigned char *array, size_t size, int pivot) 
{
  size_t left_size = size;
  unsigned char *first = array;
  unsigned char *last = first + left_size;

  for (;;)
    {
      /* FIRST를 앞으로 옮겨 PIVOT보다 큰 첫 번째 원소를
         가리키게 한다. */
      for (;;)
        {
          if (first == last)
            {
              ASSERT (is_partitioned (array, size, pivot, left_size));
              return left_size;
            }
          else if (*first >= pivot)
            break;

          first++;
        }
      left_size--;

      /* LAST를 뒤로 옮겨 PIVOT보다 크지 않은 마지막 원소를
         가리키게 한다. */
      for (;;)
        {
          last--;

          if (first == last)
            {
              ASSERT (is_partitioned (array, size, pivot, left_size));
              return left_size;
            }
          else if (*last < pivot)
            break;
          else
            left_size--;
        }

      /* FIRST와 LAST를 바꿈으로써, PREDICATE를 각각 만족하는
         앞쪽 구간과 만족하지 않는 뒤쪽 구간을
         확장한다. */
      swap (first, last);
      first++;
    }
}

/* BUF의 SIZE 바이트가 감소하지 않는 순서이면 true,
   아니면 false를 반환한다. */
static bool
is_sorted (const unsigned char *buf, size_t size) 
{
  size_t i;

  for (i = 1; i < size; i++)
    if (buf[i - 1] > buf[i])
      return false;

  return true;
}

/* 퀵 정렬 알고리즘을 사용해 BUF의 SIZE 바이트를 감소하지 않는
   순서로 정렬한다. */
void
qsort_bytes (unsigned char *buf, size_t size) 
{
  if (!is_sorted (buf, size)) 
    {
      int pivot = pick_pivot (buf, size);

      unsigned char *left_half = buf;
      size_t left_size = partition (buf, size, pivot);
      unsigned char *right_half = left_half + left_size;
      size_t right_size = size - left_size;
  
      if (left_size <= right_size) 
        {
          qsort_bytes (left_half, left_size);
          qsort_bytes (right_half, right_size); 
        }
      else
        {
          qsort_bytes (right_half, right_size); 
          qsort_bytes (left_half, left_size);
        }
    } 
}
