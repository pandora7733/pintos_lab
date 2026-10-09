#include <stdint.h>

/* x86에서는 64비트 정수를 다른 64비트 정수로 나누는 연산을
   명령어 하나나 짧은 명령어 열로 수행할 수 없다. 그래서 GCC는
   64비트 나눗셈과 나머지 연산을 함수 호출로 구현한다.
   이 함수들은 보통 libgcc에서 가져오며, libgcc는 GCC가
   수행하는 모든 링크에 자동으로 포함된다.

   그러나 일부 x86-64 머신에는 libgcc를 비롯한 필요한
   라이브러리 없이 32비트 x86 코드를 생성할 수 있는 컴파일러와
   유틸리티가 있다. 따라서 Pintos가 libgcc에서 필요로 하는
   유일한 루틴인 64비트 나눗셈 루틴을 직접 구현하기만 하면
   이런 머신에서도 Pintos가 동작하도록 할 수 있다.

   완결성도 이 루틴들을 포함하는 또 다른 이유이다.
   Pintos가 완전히 자기 완결적이라면 그만큼 덜
   신비롭게 느껴질 것이다. */

/* x86 DIVL 명령어를 사용해 64비트 N을 32비트 D로 나누어
   32비트 몫을 얻는다. 몫을 반환한다.
   몫이 32비트에 들어가지 않으면 나눗셈 오류(#DE)로
   트랩이 발생한다. */
static inline uint32_t
divl (uint64_t n, uint32_t d) {
	uint32_t n1 = n >> 32;
	uint32_t n0 = n;
	uint32_t q, r;

	asm ("divl %4"
			: "=d" (r), "=a" (q)
			: "0" (n1), "1" (n0), "rm" (d));

	return q;
}

/* X의 앞쪽(leading) 0 비트 개수를 반환한다.
   X는 0이 아니어야 한다. */
static int
nlz (uint32_t x) {
	/* 이 기법은 이식성이 있지만, 특정 시스템에서는 더 나은
	   방법이 있다. 충분히 최신 GCC라면 __builtin_clz()를
	   사용해 GCC가 알고 있는 방법을 활용할 수 있다.
	   또는 x86 BSR 명령어를 직접 사용할 수도
	   있다. */
	int n = 0;
	if (x <= 0x0000FFFF) {
		n += 16;
		x <<= 16;
	}
	if (x <= 0x00FFFFFF) {
		n += 8;
		x <<= 8;
	}
	if (x <= 0x0FFFFFFF) {
		n += 4;
		x <<= 4;
	}
	if (x <= 0x3FFFFFFF) {
		n += 2;
		x <<= 2;
	}
	if (x <= 0x7FFFFFFF)
		n++;
	return n;
}

/* 부호 없는 64비트 N을 부호 없는 64비트 D로 나누어
   몫을 반환한다. */
static uint64_t
udiv64 (uint64_t n, uint64_t d) {
	if ((d >> 32) == 0) {
		/* 정확성 증명:

		   n, d, b, n1, n0를 이 함수에서와 같이 정의하자.
		   [x]를 x의 "내림(floor)"이라 하자. T = b[n1/d]라 하자. d가
		   0이 아니라고 가정하면:
		   [n/d] = [n/d] - T + T
		   = [n/d - T] + T                         아래 (1)에 의해
		   = [(b*n1 + n0)/d - T] + T               n의 정의에 의해
		   = [(b*n1 + n0)/d - dT/d] + T
		   = [(b(n1 - d[n1/d]) + n0)/d] + T
		   = [(b[n1 % d] + n0)/d] + T,             %의 정의에 의해
		   이것이 바로 아래에서 계산하는 식이다.

		   (1) 임의의 실수 x와 정수 i에 대해 [x] + i = [x + i]임에 유의하라.

		   divl()이 트랩을 일으키지 않으려면 [(b[n1 % d] + n0)/d]가
		   b보다 작아야 한다. [n1 % d]와 n0가 각각의 최댓값인
		   d - 1과 b - 1을 가진다고 가정하면:
		   [(b(d - 1) + (b - 1))/d] < b
		   <=> [(bd - 1)/d] < b
		   <=> [b - 1/d] < b
		   이는 항진명제(tautology)이다.

		   따라서 이 코드는 올바르며 트랩을 일으키지 않는다. */
		uint64_t b = 1ULL << 32;
		uint32_t n1 = n >> 32;
		uint32_t n0 = n;
		uint32_t d0 = d;

		return divl (b * (n1 % d0) + n0, d0) + b * (n1 / d0);
	} else {
		/* 다음에서 제공하는 알고리즘과 증명을 바탕으로 한다:
		 * http://www.hackersdelight.org/revisions.pdf. */
		if (n < d)
			return 0;
		else {
			uint32_t d1 = d >> 32;
			int s = nlz (d1);
			uint64_t q = divl (n >> 1, (d << s) >> 32) >> (31 - s);
			return n - (q - 1) * d < d ? q - 1 : q;
		}
	}
}

/* 부호 없는 64비트 N을 부호 없는 64비트 D로 나누어
   나머지를 반환한다. */
static uint32_t
umod64 (uint64_t n, uint64_t d) {
	return n - d * udiv64 (n, d);
}

/* 부호 있는 64비트 N을 부호 있는 64비트 D로 나누어
   몫을 반환한다. */
static int64_t
sdiv64 (int64_t n, int64_t d) {
	uint64_t n_abs = n >= 0 ? (uint64_t) n : -(uint64_t) n;
	uint64_t d_abs = d >= 0 ? (uint64_t) d : -(uint64_t) d;
	uint64_t q_abs = udiv64 (n_abs, d_abs);
	return (n < 0) == (d < 0) ? (int64_t) q_abs : -(int64_t) q_abs;
}

/* 부호 있는 64비트 N을 부호 있는 64비트 D로 나누어
   나머지를 반환한다. */
static int32_t
smod64 (int64_t n, int64_t d) {
	return n - d * sdiv64 (n, d);
}

/* GCC가 호출하는 루틴들이다. */

long long __divdi3 (long long n, long long d);
long long __moddi3 (long long n, long long d);
unsigned long long __udivdi3 (unsigned long long n, unsigned long long d);
unsigned long long __umoddi3 (unsigned long long n, unsigned long long d);

/* 부호 있는 64비트 나눗셈. */
long long
__divdi3 (long long n, long long d) {
	return sdiv64 (n, d);
}

/* 부호 있는 64비트 나머지. */
long long
__moddi3 (long long n, long long d) {
	return smod64 (n, d);
}

/* 부호 없는 64비트 나눗셈. */
unsigned long long
__udivdi3 (unsigned long long n, unsigned long long d) {
	return udiv64 (n, d);
}

/* 부호 없는 64비트 나머지. */
unsigned long long
__umoddi3 (unsigned long long n, unsigned long long d) {
	return umod64 (n, d);
}
