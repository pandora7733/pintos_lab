#include "random.h"
#include <stdbool.h>
#include <stdint.h>
#include "debug.h"

/* RC4 기반 의사 난수 생성기(PRNG).

   RC4는 스트림 암호이다. 여기서는 암호학적 특성 때문이 아니라,
   구현하기 쉽고 비암호학적 용도로는 출력이 충분히 무작위적이기
   때문에 사용한다.

   RC4에 대한 정보는 http://en.wikipedia.org/wiki/RC4_(cipher)를
   참고하라.*/

/* RC4 상태. */
static uint8_t s[256];          /* S[]. */
static uint8_t s_i, s_j;        /* i, j. */

/* 이미 초기화되었는가? */
static bool inited;     

/* A와 B가 가리키는 바이트를 서로 바꾼다. */
static inline void
swap_byte (uint8_t *a, uint8_t *b) {
	uint8_t t = *a;
	*a = *b;
	*b = t;
}

/* 주어진 SEED로 PRNG를 초기화하거나 다시 초기화한다. */
void
random_init (unsigned seed) {
	uint8_t *seedp = (uint8_t *) &seed;
	int i;
	uint8_t j;

	for (i = 0; i < 256; i++) 
		s[i] = i;
	for (i = j = 0; i < 256; i++) {
		j += s[i] + seedp[i % sizeof seed];
		swap_byte (s + i, s + j);
	}

	s_i = s_j = 0;
	inited = true;
}

/* BUF에 SIZE 바이트의 난수를 쓴다. */
void
random_bytes (void *buf_, size_t size) {
	uint8_t *buf;

	if (!inited)
		random_init (0);

	for (buf = buf_; size-- > 0; buf++) {
		uint8_t s_k;

		s_i++;
		s_j += s[s_i];
		swap_byte (s + s_i, s + s_j);

		s_k = s[s_i] + s[s_j];
		*buf = s[s_k];
	}
}

/* 의사 난수 unsigned long 값을 반환한다.
   0...n (n 제외) 범위의 난수를 얻으려면 random_ulong() % n을
   사용하라. */
unsigned long
random_ulong (void) {
	unsigned long ul;
	random_bytes (&ul, sizeof ul);
	return ul;
}
