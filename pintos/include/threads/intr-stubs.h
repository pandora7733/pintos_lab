#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/* 인터럽트 스텁.
 *
 * intr-stubs.S에 있는 작은 코드 조각들로, x86에서 가능한
 * 256개의 인터럽트마다 하나씩 있다. 각 스텁은 스택을 약간
 * 조작한 뒤 intr_entry()로 점프한다.
 * 자세한 내용은 intr-stubs.S를 참고하라.
 *
 * 이 배열은 intr_init()이 쉽게 찾을 수 있도록 각 인터럽트
 * 스텁의 진입점을 가리킨다. */
typedef void intr_stub_func (void);
extern intr_stub_func *intr_stubs[256];

#endif /* threads/intr-stubs.h */
