#ifndef FILESYS_OFF_T_H
#define FILESYS_OFF_T_H

#include <stdint.h>

/* 파일 내부의 오프셋.
 * 여러 헤더가 다른 정의는 필요 없이 이 정의만 원하기 때문에
 * 별도의 헤더로 분리했다. */
typedef int32_t off_t;

/* printf()용 형식 지정자, 예:
 * printf ("offset=%"PROTd"\n", offset); */
#define PROTd PRId32

#endif /* filesys/off_t.h */
