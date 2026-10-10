#ifndef DEVICES_INTQ_H
#define DEVICES_INTQ_H

#include "threads/interrupt.h"
#include "threads/synch.h"

/* "인터럽트 큐". 커널 스레드와 외부 인터럽트 핸들러가
   공유하는 원형 버퍼이다.

   인터럽트 큐 함수는 커널 스레드나 외부 인터럽트
   핸들러에서 호출할 수 있다. intq_init()을 제외하면,
   어느 경우든 인터럽트가 꺼져 있어야 한다.

   인터럽트 큐는 "모니터" 구조를 가진다. threads/synch.h의
   락과 조건 변수는 커널 스레드끼리만 서로 보호할 수 있고
   인터럽트 핸들러로부터는 보호하지 못하기 때문에,
   보통과 달리 이 경우에는 사용할 수
   없다. */

/* 큐 버퍼 크기, 바이트 단위. */
#define INTQ_BUFSIZE 64

/* 바이트의 원형 큐. */
struct intq {
	/* 대기 중인 스레드. */
	struct lock lock;           /* 한 번에 하나의 스레드만 기다릴 수 있다. */
	struct thread *not_full;    /* not-full 조건을 기다리는 스레드. */
	struct thread *not_empty;   /* not-empty 조건을 기다리는 스레드. */

	/* 큐. */
	uint8_t buf[INTQ_BUFSIZE];  /* 버퍼. */
	int head;                   /* 새 데이터는 여기에 쓴다. */
	int tail;                   /* 오래된 데이터는 여기에서 읽는다. */
};

void intq_init (struct intq *);
bool intq_empty (const struct intq *);
bool intq_full (const struct intq *);
uint8_t intq_getc (struct intq *);
void intq_putc (struct intq *, uint8_t);

#endif /* devices/intq.h */
