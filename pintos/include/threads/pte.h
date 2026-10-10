#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* x86 하드웨어 페이지 테이블을 다루기 위한 함수와 매크로.
 * 가상 주소를 위한 보다 일반적인 함수와 매크로는 vaddr.h를 참고하라.
 *
 * 가상 주소는 다음과 같은 구조로 되어 있다:
 *  63          48 47            39 38            30 29            21 20         12 11         0
 * +-------------+----------------+----------------+----------------+-------------+------------+
 * | Sign Extend |    Page-Map    | Page-Directory | Page-directory |  Page-Table |  Physical  |
 * |             | Level-4 Offset |    Pointer     |     Offset     |   Offset    |   Offset   |
 * +-------------+----------------+----------------+----------------+-------------+------------+
 *               |                |                |                |             |            |
 *               +------- 9 ------+------- 9 ------+------- 9 ------+----- 9 -----+---- 12 ----+
 *                                         Virtual Address
 */

#define PML4SHIFT 39UL
#define PDPESHIFT 30UL
#define PDXSHIFT  21UL
#define PTXSHIFT  12UL

#define PML4(la)  ((((uint64_t) (la)) >> PML4SHIFT) & 0x1FF)
#define PDPE(la) ((((uint64_t) (la)) >> PDPESHIFT) & 0x1FF)
#define PDX(la)  ((((uint64_t) (la)) >> PDXSHIFT) & 0x1FF)
#define PTX(la)  ((((uint64_t) (la)) >> PTXSHIFT) & 0x1FF)
#define PTE_ADDR(pte) ((uint64_t) (pte) & ~0xFFF)

/* 중요한 플래그들은 아래와 같다.
   PDE나 PTE가 "present" 상태가 아니면 다른 플래그들은
   무시된다.
   0으로 초기화된 PDE나 PTE는 "not present"로 해석되며,
   이는 아무 문제 없다. */
#define PTE_FLAGS 0x00000000000000fffUL    /* 플래그 비트. */
#define PTE_ADDR_MASK  0xffffffffffffff000UL /* 주소 비트. */
#define PTE_AVL   0x00000e00             /* OS가 사용할 수 있는 비트. */
#define PTE_P 0x1                        /* 1=존재함, 0=존재하지 않음. */
#define PTE_W 0x2                        /* 1=읽기/쓰기, 0=읽기 전용. */
#define PTE_U 0x4                        /* 1=사용자/커널, 0=커널 전용. */
#define PTE_A 0x20                       /* 1=접근됨, 0=접근되지 않음. */
#define PTE_D 0x40                       /* 1=dirty(수정됨), 0=dirty 아님 (PTE 전용). */

#endif /* threads/pte.h */
