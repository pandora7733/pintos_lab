# 세마포어(Semaphore) 개념 완전 정리

> Pintos (KAIST x86-64) Project 1 — Threads
> 대상 코드: `include/threads/synch.h`, `threads/synch.c`
> 목표: Pintos의 세마포어가 **무엇이고, 왜 필요하고, 내부에서 정확히 어떻게 돌아가는지**를 한 문서로 이해한다.

---

## 목차

1. [한 문장 요약과 비유](#1-한-문장-요약과-비유)
2. [왜 세마포어가 필요한가](#2-왜-세마포어가-필요한가)
3. [세마포어의 정의 (P / V)](#3-세마포어의-정의-p--v)
4. [Pintos의 `struct semaphore`](#4-pintos의-struct-semaphore)
5. [`sema_init` — 초기화](#5-sema_init--초기화)
6. [`sema_down` — 한 줄씩 해부](#6-sema_down--한-줄씩-해부)
7. [`sema_up` — 한 줄씩 해부](#7-sema_up--한-줄씩-해부)
8. [`sema_try_down` — 기다리지 않는 down](#8-sema_try_down--기다리지-않는-down)
9. [원자성: 왜 "인터럽트 끄기"로 충분한가](#9-원자성-왜-인터럽트-끄기로-충분한가)
10. [스레드 상태 변화로 보는 세마포어](#10-스레드-상태-변화로-보는-세마포어)
11. [시나리오 추적: 스레드 3개가 하나의 세마포어를 두고 경쟁](#11-시나리오-추적-스레드-3개가-하나의-세마포어를-두고-경쟁)
12. [왜 `if`가 아니라 `while`인가](#12-왜-if가-아니라-while인가)
13. [세마포어의 3가지 사용 패턴](#13-세마포어의-3가지-사용-패턴)
14. [`sema_self_test` 핑퐁 추적](#14-sema_self_test-핑퐁-추적)
15. [락(Lock) = 세마포어 + 주인](#15-락lock--세마포어--주인)
16. [조건 변수(Condition)도 세마포어로 만들어진다](#16-조건-변수condition도-세마포어로-만들어진다)
17. [우선순위 스케줄링과 세마포어 (지금 내 코드)](#17-우선순위-스케줄링과-세마포어-지금-내-코드)
18. [인터럽트 핸들러에서 쓸 수 있는 함수 정리](#18-인터럽트-핸들러에서-쓸-수-있는-함수-정리)
19. [교과서 세마포어와 Pintos 세마포어의 차이](#19-교과서-세마포어와-pintos-세마포어의-차이)
20. [자주 하는 실수 / 헷갈리는 점](#20-자주-하는-실수--헷갈리는-점)
21. [Q&A 모음](#21-qa-모음)
22. [한 장 요약](#22-한-장-요약)

---

## 1. 한 문장 요약과 비유

**세마포어 = "남은 자리 수(value)" + "자리를 기다리며 자고 있는 스레드 줄(waiters)".**

### 비유: 열쇠 보관함이 있는 회의실

- 회의실 앞에 **열쇠 보관함**이 있고, 안에 열쇠가 `value`개 들어 있다.
- 들어가려는 사람(스레드)은 **열쇠를 하나 꺼낸다** → `sema_down` (P)
  - 열쇠가 없으면? 보관함 옆 **대기 의자(waiters)에 앉아서 잔다.** (CPU를 쓰지 않는다)
- 다 쓴 사람은 **열쇠를 다시 넣는다** → `sema_up` (V)
  - 넣으면서 의자에서 자고 있는 사람이 있으면 **한 명을 흔들어 깨운다.**
- 깨어난 사람은 다시 보관함을 확인하고 열쇠를 꺼내 간다.

| 비유 | Pintos |
|------|--------|
| 보관함 속 열쇠 개수 | `sema->value` |
| 대기 의자 | `sema->waiters` (리스트) |
| 열쇠 꺼내기 / 없으면 자기 | `sema_down()` |
| 열쇠 넣기 + 한 명 깨우기 | `sema_up()` |
| 열쇠 없으면 그냥 돌아가기 | `sema_try_down()` |

---

## 2. 왜 세마포어가 필요한가

### 2-1. 경쟁 상태(Race Condition)

두 스레드가 같은 전역 변수 `count`를 1씩 올린다고 하자.

```c
count++;   // C 코드로는 한 줄이지만...
```

CPU 입장에서는 세 단계다.

```
1. load  : 메모리의 count 값을 레지스터로 읽기
2. add   : 레지스터 값 + 1
3. store : 레지스터 값을 메모리에 쓰기
```

Pintos는 **타이머 인터럽트로 언제든 스레드를 바꿔치기(선점)** 한다. 그래서 이런 일이 생긴다.

| 시점 | 스레드 A | 스레드 B | 메모리 count |
|------|---------|---------|-------------|
| 1 | load (reg=0) | | 0 |
| 2 | ⚡ 타이머 인터럽트 → B로 전환 | | 0 |
| 3 | | load (reg=0) | 0 |
| 4 | | add (reg=1) | 0 |
| 5 | | store | **1** |
| 6 | ⚡ 다시 A로 | | 1 |
| 7 | add (reg=1) | | 1 |
| 8 | store | | **1** ← 2여야 하는데! |

두 번 올렸는데 결과는 1이다. 이렇게 **실행 순서에 따라 결과가 달라지는 상황**을 경쟁 상태라 하고, 공유 데이터를 건드리는 코드 구간을 **임계 구역(critical section)** 이라고 한다.

해결하려면 "임계 구역에는 한 번에 한 스레드만" 들어가도록 막아야 한다 → **상호 배제(mutual exclusion)**.

### 2-2. "기다림"도 필요하다

동기화는 상호 배제만이 아니다. "B는 A가 일을 끝낸 **다음에** 실행돼야 한다" 같은 **순서 보장**도 필요하다.
(예: `thread_start()`는 idle 스레드가 준비될 때까지 기다려야 한다 → 13장에서 실제 코드로 본다.)

### 2-3. 바쁜 대기(busy waiting)는 안 된다

기다리는 가장 단순한 방법은 이렇다.

```c
while (!준비됨)
  ;   // 계속 확인
```

하지만 이건 Alarm Clock 과제에서 본 문제와 똑같다. **기다리는 동안 CPU를 계속 태운다.**
세마포어는 기다릴 스레드를 **BLOCKED 상태로 재워서** CPU를 전혀 쓰지 않게 하고, 조건이 만족되면 **다른 스레드가 깨워 주는** 방식이다.

> 정리: 세마포어는 **(1) 상호 배제, (2) 순서 보장**을 **(3) 바쁜 대기 없이** 해 주는 도구다.

---

## 3. 세마포어의 정의 (P / V)

세마포어는 1965년 다익스트라(Dijkstra)가 고안했다. 정의는 딱 이것뿐이다.

> **음이 아닌 정수 하나** + 그것을 조작하는 **두 개의 원자적(atomic) 연산**

| 연산 | 다른 이름 | 의미 |
|------|----------|------|
| **P** | down, wait, `sema_down` | 값이 양수가 될 때까지 **기다렸다가**, 1 감소 |
| **V** | up, signal, `sema_up` | 1 증가, 기다리는 스레드가 있으면 **하나 깨움** |

(P와 V는 네덜란드어 *Proberen*(시도하다) / *Verhogen*(증가시키다)에서 왔다.)

핵심 단어는 **원자적**이다. "값 확인 → 감소"가 중간에 끊기지 않고 한 덩어리로 실행되어야 한다.
만약 끊기면 2장의 `count++` 문제가 세마포어 자체에서 다시 생긴다.

### 값의 의미

- `value` = **지금 당장 기다리지 않고 down 할 수 있는 횟수**
- `value == 0` = 지금 down 하면 **잠들어야 함**

---

## 4. Pintos의 `struct semaphore`

`include/threads/synch.h`

```c
/* 카운팅 세마포어. */
struct semaphore {
	unsigned value;             /* 현재 값. */
	struct list waiters;        /* 대기 중인 스레드 리스트. */
};
```

그림으로 보면:

```
struct semaphore
┌────────────────────────┐
│ value   : 0            │
│ waiters : ─────────────┼──► [ thread B ] ⇄ [ thread C ] ⇄ ...
└────────────────────────┘        (BLOCKED)      (BLOCKED)
```

- `value`가 `unsigned`인 점에 주목. **Pintos 세마포어는 절대 음수가 되지 않는다.** (19장 참고)
- `waiters`는 Pintos의 이중 연결 리스트(`lib/kernel/list.h`). 리스트에 들어가는 것은 스레드 자체가 아니라 **`struct thread` 안의 `elem` 멤버**다.

```c
struct thread {
	...
	struct list_elem elem;   /* ready_list 또는 세마포어 waiters에 들어갈 때 쓰는 고리 */
	...
};
```

> 하나의 `elem`을 `ready_list`와 `waiters`가 **같이 쓴다.** 이게 가능한 이유는 스레드가
> READY 상태(ready_list에 있음)와 BLOCKED 상태(waiters에 있음)에 **동시에 있을 수 없기 때문**이다.
> 그래서 `sema_up`에서 waiters에서 `pop` 한 뒤 바로 `thread_unblock`으로 ready_list에 넣을 수 있다.

리스트 원소(`elem`)에서 스레드 구조체를 다시 꺼낼 때는 `list_entry` 매크로를 쓴다.

```c
struct thread *t = list_entry (e, struct thread, elem);
//  "e는 struct thread 안의 elem 멤버를 가리키는 포인터다. 그 thread의 시작 주소를 돌려줘."
```

---

## 5. `sema_init` — 초기화

```c
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}
```

- 초기값이 **세마포어의 용도를 결정**한다. (13장)
  - `1` → 상호 배제 (락처럼)
  - `0` → 신호 / 순서 보장 ("누가 up 해줄 때까지 기다려")
  - `N` → 자원 N개 관리
- `waiters`는 빈 리스트로 시작.

---

## 6. `sema_down` — 한 줄씩 해부

현재 내 코드 (`threads/synch.c`):

```c
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());                       // ①

	old_level = intr_disable ();                     // ②
	while (sema->value == 0) {                       // ③
		list_insert_ordered (&sema->waiters,         // ④
		                     &thread_current ()->elem,
		                     thread_priority_greater, NULL);
		thread_block ();                             // ⑤
	}
	sema->value--;                                   // ⑥
	intr_set_level (old_level);                      // ⑦
}
```

### ① `ASSERT (!intr_context ())` — 인터럽트 핸들러 안에서는 금지

`sema_down`은 **잠들 수 있는** 함수다. 인터럽트 핸들러는 "지금 실행 중인 스레드를 잠깐 빌려서" 도는 코드라서,
핸들러가 잠들어 버리면 그 스레드는 영문도 모르고 멈추고, 인터럽트 처리도 끝나지 않는다.
그래서 **핸들러에서는 절대 잠들면 안 된다** → 아예 ASSERT로 막는다.

### ② `old_level = intr_disable ()` — 원자성 확보

인터럽트를 끄면 타이머 인터럽트가 안 들어오므로 **선점(스레드 전환)이 일어나지 않는다.**
→ ③~⑥이 다른 스레드에게 끼어들림 없이 한 덩어리로 실행된다. (자세한 건 9장)

`old_level`에 이전 상태를 저장해 두는 이유: 호출자가 **원래 인터럽트를 꺼 둔 상태**였을 수도 있다.
무조건 `intr_enable()`로 끝내면 호출자의 의도를 깨뜨리므로, 끝날 때 **원래 상태로 복원**(⑦)한다.

### ③ `while (sema->value == 0)` — 자리가 없으면 기다린다

`value`가 0이면 지금은 내려갈 수 없다. `if`가 아니라 `while`인 이유는 12장에서 자세히.

### ④ waiters에 나를 넣는다

원래 Pintos 코드는 `list_push_back`(줄 맨 뒤에 서기, FIFO)였다.
우선순위 스케줄링 때문에 지금은 `list_insert_ordered`로 **우선순위 내림차순** 위치에 끼워 넣는다. (17장)

### ⑤ `thread_block ()` — 잠든다

```c
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);   // 인터럽트가 꺼져 있어야 함!
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();                               // 다른 스레드로 전환
}
```

- 상태를 `THREAD_BLOCKED`로 바꾸고 `schedule()`로 다른 스레드에게 CPU를 넘긴다.
- BLOCKED 스레드는 `ready_list`에 없으므로 **스케줄러가 절대 고르지 않는다** → CPU 0% 사용.
- **이 함수는 여기서 "멈춘다".** 누군가 `sema_up` → `thread_unblock`을 해 주고, 스케줄러가 이 스레드를 다시 고르면,
  그때 `schedule()`에서 **리턴해서** 바로 다음 줄(`while` 조건 재검사)부터 이어서 실행한다.

> ❓ "인터럽트를 끈 채로 잠들면 시스템 전체가 인터럽트 꺼진 채로 멈추는 거 아냐?"
> → 아니다. 다음에 실행되는 스레드가 자기 코드에서 `intr_set_level(old_level)` 등으로 인터럽트를 다시 켠다.
> 인터럽트 상태는 사실상 **스레드마다 따로** 기억된다고 생각하면 된다. 나중에 내가 깨어나서 돌아오면
> 나는 여전히 "인터럽트 꺼진 상태"로 ③ 줄에 돌아오고, ⑦에서 내가 저장해 둔 상태로 복원한다.

### ⑥ `sema->value--` — 자리를 하나 가져간다

`while`을 빠져나왔다 = `value > 0`이 보장됨 → 안전하게 1 감소.

### ⑦ `intr_set_level (old_level)` — 원래 인터럽트 상태 복원

---

## 7. `sema_up` — 한 줄씩 해부

현재 내 코드:

```c
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();                                  // ①
	if (!list_empty (&sema->waiters)) {                           // ②
		list_sort (&sema->waiters, thread_priority_greater, NULL); // ③
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
		                            struct thread, elem));        // ④
	}
	sema->value++;                                                // ⑤

	thread_preempt ();                                            // ⑥
	intr_set_level (old_level);                                   // ⑦
}
```

### ① 인터럽트 끄기 — 마찬가지로 원자성

`sema_up`에는 `ASSERT (!intr_context ())`가 **없다.** 즉 **인터럽트 핸들러에서 호출해도 된다.**
`sema_up`은 절대 잠들지 않기 때문이다. (예: 디스크 I/O 완료 인터럽트가 "끝났어!" 하고 `sema_up` 하는 패턴)

### ② 기다리는 스레드가 있으면

### ③ 다시 정렬 (우선순위 기부 대비)

`sema_down`에서 정렬해서 넣었는데 왜 또 정렬할까?
**기다리는 동안 우선순위가 바뀔 수 있기 때문이다.** (우선순위 기부(donation)로 올라가거나, `thread_set_priority`로 바뀌거나)
리스트에 들어간 뒤 바뀐 값은 자동으로 재정렬되지 않으므로, **꺼내기 직전에** 정렬한다.

### ④ 맨 앞 스레드를 꺼내서 깨운다

```c
void
thread_unblock (struct thread *t) {
	...
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL);
	t->status = THREAD_READY;
	...
}
```

- `thread_unblock`은 스레드를 **BLOCKED → READY**로 바꾸고 `ready_list`에 넣을 뿐이다.
- **바로 실행시키는 게 아니다!** 언제 실행될지는 스케줄러가 정한다.
- 이 점이 12장 "`while`이어야 하는 이유"의 핵심이다.

### ⑤ `sema->value++`

열쇠를 보관함에 넣는다. 깨운 스레드는 나중에 실행되면서 `while`을 다시 검사하고, 이 값을 보고 `value--` 한다.

> ❓ 깨우기(④)를 먼저 하고 `value++`(⑤)를 나중에 해도 괜찮나?
> → 괜찮다. 인터럽트가 꺼져 있어서 ①~⑦ 사이에 아무도 끼어들지 못한다. 깨운 스레드도 READY일 뿐 아직 실행 전이다.
> 그 스레드가 실제로 실행될 때는 이미 `value`가 올라가 있다.

### ⑥ `thread_preempt ()` — 깨운 스레드가 더 높으면 양보

```c
void
thread_preempt (void) {
	enum intr_level old_level = intr_disable ();

	if (!list_empty (&ready_list)) {
		struct thread *front = list_entry (list_front (&ready_list), struct thread, elem);

		if (front->priority > thread_current ()->priority) {
			if (intr_context ())
				intr_yield_on_return ();   // 핸들러 안: 끝나고 양보하도록 예약
			else
				thread_yield ();           // 일반 상황: 지금 바로 양보
		}
	}
	intr_set_level (old_level);
}
```

우선순위 스케줄링의 규칙 "**항상 가장 높은 우선순위 스레드가 실행되어야 한다**"를 지키기 위해,
깨운 스레드가 나보다 높으면 즉시 CPU를 넘긴다.
`sema_up`은 핸들러에서도 불릴 수 있으므로, 핸들러 안에서는 `thread_yield()` 대신 `intr_yield_on_return()`을 쓴다.

---

## 8. `sema_try_down` — 기다리지 않는 down

```c
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0) {
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}
```

- 열쇠가 있으면 가져가고 `true`, 없으면 **기다리지 않고** 바로 `false`.
- 잠들지 않으므로 **인터럽트 핸들러에서도 호출 가능.**
- 주의: `while (!sema_try_down (&s)) ;` 처럼 반복 호출하면 그게 바로 **바쁜 대기**다. 하지 말자.

---

## 9. 원자성: 왜 "인터럽트 끄기"로 충분한가

### Pintos는 단일 CPU다

스레드 전환이 일어나는 경로는 딱 두 가지다.

1. 스레드가 **스스로** 양보/블록 (`thread_yield`, `thread_block`)
2. **타이머 인터럽트**가 와서 강제로 전환 (선점)

인터럽트를 끄면 2번이 막힌다. 1번은 내 코드가 직접 부르지 않는 한 일어나지 않는다.
→ CPU가 하나뿐이니 "다른 CPU가 동시에 접근"하는 경우도 없다.
→ **인터럽트를 끈 구간은 완벽하게 원자적이다.**

(멀티코어라면 다른 코어가 동시에 메모리를 건드릴 수 있어서 인터럽트 끄기만으로는 부족하고, 스핀락이나 원자적 CPU 명령이 필요하다. Pintos는 이걸 신경 쓰지 않아도 된다.)

### 그럼 아예 모든 동기화를 인터럽트 끄기로 하면 되지 않나?

안 된다. 인터럽트를 오래 끄면:
- 타이머 틱을 놓친다 (시간이 안 흐름, `timer_sleep` 망가짐)
- 키보드/디스크 등 장치 처리 지연
- 그 동안 다른 스레드가 전혀 못 돈다

그래서 원칙은 이렇다.

| 도구 | 언제 |
|------|------|
| **인터럽트 끄기** | 아주 짧은 구간. 세마포어/락 **자체를 구현**할 때, 또는 인터럽트 핸들러와 데이터를 공유할 때 |
| **세마포어 / 락** | 그 외 일반적인 임계 구역. 기다리는 동안 다른 스레드는 정상적으로 돈다 |

즉 **세마포어는 "짧은 인터럽트 끄기" 위에 쌓은 "긴 기다림도 가능한 도구"** 다.
세마포어 내부는 인터럽트를 아주 잠깐만 끄고, 진짜로 오래 기다려야 하면 **잠들어서** CPU를 넘긴다.

### 계층 구조

```
            ┌───────────────────────┐
            │  조건 변수 (condition) │
            └──────────┬────────────┘
                       │ 사용
            ┌──────────▼────────────┐
            │        락 (lock)       │
            └──────────┬────────────┘
                       │ 사용
            ┌──────────▼────────────┐
            │   세마포어 (semaphore)  │
            └──────────┬────────────┘
                       │ 사용
  ┌────────────────────▼───────────────────────────┐
  │ 인터럽트 끄기 + thread_block / thread_unblock    │
  └────────────────────────────────────────────────┘
```

Pintos의 모든 동기화 도구는 결국 **세마포어 하나** 위에 서 있다. 그래서 세마포어를 이해하면 나머지가 다 이해된다.

---

## 10. 스레드 상태 변화로 보는 세마포어

```
                    sema_down 했는데 value == 0
     ┌─────────┐   (waiters에 들어가고 thread_block)   ┌─────────┐
     │ RUNNING │ ──────────────────────────────────► │ BLOCKED │
     └─────────┘                                     └────┬────┘
          ▲                                               │
          │ 스케줄러가 선택                                 │ 다른 스레드의 sema_up
          │ (schedule)                                    │ (waiters에서 빠지고 thread_unblock)
     ┌────┴────┐                                          │
     │  READY  │ ◄────────────────────────────────────────┘
     └─────────┘
       (ready_list에 있음)
```

- BLOCKED 스레드가 있는 곳: **세마포어의 `waiters`** (또는 alarm clock의 `sleep_list`)
- READY 스레드가 있는 곳: **`ready_list`**
- `sema_up`은 BLOCKED → **READY**로 옮길 뿐, RUNNING으로 바로 보내지 않는다.

> Alarm Clock에서 했던 것과 구조가 똑같다!
> `sleep_list` + `thread_block` + 타이머에서 `thread_unblock` 하던 그 방식을, "시간" 대신 "value"를 조건으로 일반화한 것이 세마포어다.

---

## 11. 시나리오 추적: 스레드 3개가 하나의 세마포어를 두고 경쟁

`sema_init (&s, 1)` — 상호 배제용. 스레드 A, B, C 모두 같은 우선순위라고 하자.

| 단계 | 사건 | value | waiters | 상태 |
|------|------|-------|---------|------|
| 0 | 초기화 | 1 | [] | A,B,C 모두 READY/RUNNING |
| 1 | A: `sema_down` → value>0 이므로 바로 통과, value-- | **0** | [] | A는 임계 구역 안 |
| 2 | (타이머로 B에게 전환) B: `sema_down` → value==0 → waiters에 들어가 block | 0 | [B] | B: BLOCKED |
| 3 | C: `sema_down` → value==0 → block | 0 | [B, C] | C: BLOCKED |
| 4 | A 실행 재개, 임계 구역 끝, `sema_up` → B를 pop & unblock, value++ | **1** | [C] | B: READY |
| 5 | B가 스케줄되어 실행: `schedule()`에서 리턴 → `while(value==0)` 재검사 → 거짓 → value-- | **0** | [C] | B는 임계 구역 안 |
| 6 | B: `sema_up` → C를 unblock, value++ | 1 | [] | C: READY |
| 7 | C 실행: while 재검사 통과, value-- | 0 | [] | C 임계 구역 |
| 8 | C: `sema_up` → waiters 비어 있음, value++ | 1 | [] | 원래대로 |

포인트:
- 임계 구역 안에는 **언제나 최대 1개 스레드**만 있다.
- B, C는 기다리는 동안 **CPU를 전혀 쓰지 않았다.**
- 깨어난 스레드는 **`sema_down`의 while 루프 안에서 깨어난다.** 그래서 `value--`는 깨어난 스레드가 직접 한다.

---

## 12. 왜 `if`가 아니라 `while`인가

`if`로 바꾸면 이렇게 된다.

```c
if (sema->value == 0) {        // ✗ 위험
	list_push_back (&sema->waiters, &thread_current ()->elem);
	thread_block ();
}
sema->value--;                 // value가 0일 수도 있는데 감소!
```

문제 시나리오 (`value` 초기 0, 스레드 B가 기다리는 중):

| 단계 | 사건 | value |
|------|------|-------|
| 1 | A: `sema_up` → B를 unblock (B는 READY, 아직 실행 안 됨), value++ | 1 |
| 2 | 스케줄러가 B보다 **C를 먼저** 실행 | 1 |
| 3 | C: `sema_down` → value==1 이니 **기다리지 않고** 통과, value-- | 0 |
| 4 | 이제 B가 실행됨. `if`였다면 검사 없이 `value--` | **0 - 1 = unsigned 언더플로우 → 4294967295** 💥 |

`sema_up`이 "깨운다"는 건 **"다시 확인해 보라"는 뜻**이지, **"열쇠를 네 손에 쥐여 줬다"는 뜻이 아니다.**
깨어나서 실제로 실행되기까지 시간 차가 있고, 그 사이에 다른 스레드가 열쇠를 **가로챌** 수 있다.
그래서 깨어나면 **조건을 다시 확인**해야 하고, 여전히 0이면 **다시 waiters에 들어가서 잔다.**
(이미 `sema_up`에서 pop 되었으므로 루프 안에서 다시 insert 하는 것이 맞다.)

> 이 원리는 16장의 조건 변수(Mesa 스타일)에서도 똑같이 나온다:
> "`cond_wait`에서 깨어나면 조건을 `while`로 다시 확인하라."

---

## 13. 세마포어의 3가지 사용 패턴

### 패턴 1. 상호 배제 (초기값 1) — "한 명만 들어가"

```c
struct semaphore mutex;
sema_init (&mutex, 1);

sema_down (&mutex);
/* 임계 구역: 공유 데이터 수정 */
sema_up (&mutex);
```

값이 0과 1만 오가므로 **이진 세마포어(binary semaphore)** 라고 한다. 락이 바로 이것이다. (15장)

### 패턴 2. 신호 / 순서 보장 (초기값 0) — "너 끝나면 알려줘"

초기값이 0이면 `sema_down`은 **무조건 잠든다.** 누군가 `sema_up` 해 줄 때까지.
→ "어떤 일이 일어날 때까지 기다리기"에 쓴다.

**Pintos 실제 코드 — `thread_start()` (threads/thread.c)**

```c
void
thread_start (void) {
	/* idle 스레드를 생성한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);                          // 0으로 시작
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 선점형 스레드 스케줄링을 시작한다. */
	intr_enable ();

	/* idle 스레드가 idle_thread를 초기화할 때까지 기다린다. */
	sema_down (&idle_started);                             // idle이 up 해 줄 때까지 잔다
}

static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);                                // "나 준비됐어!"
	for (;;) { ... thread_block (); ... }
}
```

흐름:
1. main 스레드: `idle_started`를 0으로 만들고 idle 스레드 생성
2. main: `sema_down` → value가 0이니 잠듦
3. idle 스레드 실행: `idle_thread` 전역 변수 설정 후 `sema_up` → main을 깨움
4. main: 깨어나서 리턴 → 이제 `idle_thread`가 확실히 설정되어 있음

→ "**idle_thread가 설정된 뒤에야** thread_start()가 리턴한다"는 **순서**를 보장.

> 이 패턴에서는 down 하는 스레드와 up 하는 스레드가 **다르다.** 이것이 락과의 큰 차이다. (15장)

### 패턴 3. 카운팅 (초기값 N) — "자원이 N개"

```c
struct semaphore slots;
sema_init (&slots, 3);   // 동시에 3개까지 허용

sema_down (&slots);      // 자리 하나 차지 (3개 다 차면 잠듦)
/* 자원 사용 */
sema_up (&slots);        // 자리 반납
```

예: 버퍼 칸 수, 동시 접속 수 제한, 생산자-소비자 문제의 "빈 칸 수 / 찬 칸 수".

| 초기값 | 이름 | 용도 |
|-------|------|------|
| 0 | 신호(signaling) 세마포어 | 이벤트 대기, 실행 순서 보장 |
| 1 | 이진(binary) 세마포어 | 상호 배제 (= 락의 원형) |
| N | 카운팅(counting) 세마포어 | N개짜리 자원 관리 |

---

## 14. `sema_self_test` 핑퐁 추적

`threads/synch.c`에 있는 자체 테스트. 세마포어 **두 개**로 두 스레드가 번갈아 실행되게 만든다.

```c
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++) {
		sema_up (&sema[0]);     // "helper야, 네 차례야"
		sema_down (&sema[1]);   // helper가 끝낼 때까지 잠
	}
}

static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++) {
		sema_down (&sema[0]);   // main이 신호 줄 때까지 잠
		sema_up (&sema[1]);     // "main아, 네 차례야"
	}
}
```

```
main                         helper
 │ sema_up(s0)  ───────────►  │ (s0 down 통과)
 │ sema_down(s1) 💤            │
 │                ◄─────────  │ sema_up(s1)
 │ (깨어남)                    │ sema_down(s0) 💤
 │ sema_up(s0)  ───────────►  │ (깨어남)
 │ sema_down(s1) 💤            │
 │                ◄─────────  │ sema_up(s1)
 ...  10번 반복 ...
```

- `sema[0]`: main → helper 방향 신호
- `sema[1]`: helper → main 방향 신호
- 둘 다 초기값 0 → 패턴 2(신호)를 양방향으로 쓴 것.
- 두 스레드는 **절대 동시에 루프 본문을 진행하지 않고** 공처럼 주고받는다. 그래서 "핑퐁".

---

## 15. 락(Lock) = 세마포어 + 주인

```c
struct lock {
	struct thread *holder;      /* 락을 보유한 스레드 */
	struct semaphore semaphore; /* 초기값 1인 이진 세마포어 */
};

void lock_init (struct lock *lock) {
	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);       // 값 1로 시작
}

void lock_acquire (struct lock *lock) {
	ASSERT (!lock_held_by_current_thread (lock));   // 재귀 획득 금지
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();     // 주인 기록
}

void lock_release (struct lock *lock) {
	ASSERT (lock_held_by_current_thread (lock));    // 주인만 해제 가능
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}
```

락은 **초기값 1짜리 세마포어에 "주인(holder)" 개념을 붙인 것**이다.

| | 세마포어 | 락 |
|---|---------|----|
| 값의 범위 | 0 ~ 아무 수 | 0 또는 1 |
| 주인 | **없음** — A가 down, B가 up 해도 됨 | **있음** — 획득한 스레드만 해제 가능 |
| 같은 스레드가 두 번 획득 | (값에 따라) 가능 | 금지 (ASSERT) |
| 주 용도 | 신호, 순서, 카운팅, 상호 배제 | 상호 배제 |
| 인터럽트 핸들러에서 해제 | `sema_up` 가능 | 의미 없음 (핸들러는 락을 획득할 수 없으니까) |

> Pintos 주석의 조언: "이런 제약이 부담스럽게 느껴진다면, 락 대신 세마포어를 써야 한다는 좋은 신호이다."
> 예: "다른 스레드가 끝나면 알려줘"는 주인 개념이 맞지 않으므로 세마포어(초기값 0)를 쓴다.

### 왜 "주인"이 중요한가 → 우선순위 기부

세마포어에는 주인이 없으니 "누구 때문에 기다리는지"를 알 수 없다.
락에는 `holder`가 있으므로 "높은 우선순위 H가 **holder L** 때문에 기다린다"를 알 수 있고,
그래서 **L에게 우선순위를 빌려주는 것(donation)** 이 가능하다. → 앞으로 할 5~8단계가 바로 이것.
**우선순위 기부는 세마포어가 아니라 락에서만 구현한다.**

---

## 16. 조건 변수(Condition)도 세마포어로 만들어진다

조건 변수는 "**어떤 조건이 참이 될 때까지** 락을 잠시 놓고 기다리기" 위한 도구다.

```c
struct condition {
	struct list waiters;   /* semaphore_elem 들의 리스트 */
};

struct semaphore_elem {
	struct list_elem elem;
	struct semaphore semaphore;   /* 대기자 한 명 전용 세마포어 */
	struct thread *thread;        /* (내가 추가) 기다리는 스레드 */
};
```

핵심 아이디어: **대기자 한 명마다 초기값 0짜리 개인 세마포어를 하나씩** 만든다.

```c
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;            // 스택에 개인 세마포어 생성

	sema_init (&waiter.semaphore, 0);        // 0 → down 하면 무조건 잠듦
	waiter.thread = thread_current ();
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);                     // 락을 놓고
	sema_down (&waiter.semaphore);           // 내 개인 세마포어에서 잠
	lock_acquire (lock);                     // 깨어나면 락을 다시 잡음
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	if (!list_empty (&cond->waiters)) {
		list_sort (&cond->waiters, sema_elem_priority_greater, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
		                      struct semaphore_elem, elem)->semaphore);   // 한 명의 개인 세마포어를 up
	}
}
```

```
cond->waiters
   │
   ▼
[semaphore_elem A] ⇄ [semaphore_elem B] ⇄ ...
   │ semaphore(v=0)      │ semaphore(v=0)
   │  waiters: [A]       │  waiters: [B]
   │ thread: A           │ thread: B
```

- 각 개인 세마포어의 `waiters`에는 **딱 한 스레드**만 있다.
- 그래서 `cond->waiters`를 정렬할 때 `semaphore.waiters` 안을 뒤질 필요 없이, 내가 추가한 **`thread` 필드**로 바로 우선순위를 비교한다. (4단계에서 한 작업)
- **패턴 2(신호, 초기값 0)** 를 대기자마다 하나씩 쓴 것이다.

**Mesa 스타일**: `cond_signal`은 "깨울 뿐"이고, 깨어난 스레드가 락을 다시 잡기 전에 다른 스레드가 상태를 바꿀 수 있다.
그래서 사용하는 쪽은 반드시 `while`로 조건을 다시 확인해야 한다. (12장과 같은 이유)

```c
lock_acquire (&lock);
while (!조건)                 // if 가 아니라 while!
	cond_wait (&cond, &lock);
/* 조건이 참인 상태에서 작업 */
lock_release (&lock);
```

---

## 17. 우선순위 스케줄링과 세마포어 (지금 내 코드)

과제 요구사항:
> 락, 세마포어, 조건 변수를 기다리는 스레드가 여러 개일 때, **가장 높은 우선순위의 스레드가 먼저 깨어나야 한다.**

원래 Pintos는 waiters를 **FIFO**(먼저 온 순서)로 깨웠다. 지금까지 바꾼 것:

| 위치 | 원래 | 지금 | 이유 |
|------|------|------|------|
| `sema_down` | `list_push_back` | `list_insert_ordered (..., thread_priority_greater)` | 우선순위 내림차순으로 줄 서기 |
| `sema_up` | 바로 `list_pop_front` | `list_sort` 후 `pop_front` | 대기 중 우선순위가 바뀌었을 수 있으니(기부) 꺼내기 직전 재정렬 |
| `sema_up` 끝 | 없음 | `thread_preempt ()` | 깨운 스레드가 더 높으면 즉시 양보 |
| `semaphore_elem` | elem, semaphore | + `thread` | cond 대기자의 우선순위를 알기 위해 |
| `cond_signal` | 바로 `pop_front` | `list_sort (sema_elem_priority_greater)` 후 pop | 가장 높은 대기자에게 신호 |

### `thread_priority_greater`

```c
bool
thread_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);
	return ta->priority > tb->priority;
}
```

- `>`(엄격히 큼)를 쓰므로 **같은 우선순위끼리는 먼저 온 순서(FIFO)가 유지**된다.
  (`list_insert_ordered`는 "a가 b보다 앞서야 하면 true"인 첫 위치 앞에 넣으므로, 같은 값이면 기존 원소 뒤로 간다.)
- `ready_list`와 세마포어 `waiters`가 같은 `elem`을 쓰므로 **같은 비교 함수를 공유**할 수 있다.

### 관련 테스트

| 테스트 | 확인하는 것 |
|--------|-----------|
| `priority-sema` | 세마포어 waiters에서 높은 우선순위부터 깨어나는가 |
| `priority-condvar` | 조건 변수에서 높은 우선순위부터 신호를 받는가 |
| `priority-donate-*` | (앞으로) 락 holder에게 기부가 되는가 |

---

## 18. 인터럽트 핸들러에서 쓸 수 있는 함수 정리

규칙은 하나: **잠들 수 있는 함수는 핸들러에서 금지.**

| 함수 | 잠들 수 있나? | 핸들러에서 호출 |
|------|-------------|----------------|
| `sema_down` | O | ✗ (`ASSERT (!intr_context ())`) |
| `sema_try_down` | X | ✓ |
| `sema_up` | X | ✓ |
| `lock_acquire` | O | ✗ |
| `lock_try_acquire` | X | ✓ (그러나 핸들러가 락을 쥐는 건 보통 의미 없음) |
| `lock_release` | X | 의미 없음 |
| `cond_wait` | O | ✗ |
| `cond_signal` / `broadcast` | X | 의미 없음 (락을 쥐고 있어야 하므로) |
| `thread_yield` | — | ✗ → 대신 `intr_yield_on_return ()` |

그래서 `thread_preempt`에서 `intr_context ()`를 검사해 분기하는 것이다.
`sema_up`이 핸들러(예: 타이머)에서 불릴 수 있기 때문.

---

## 19. 교과서 세마포어와 Pintos 세마포어의 차이

운영체제 교과서(공룡책 등)에서는 이렇게 구현하기도 한다.

```c
wait(S) {
	S->value--;
	if (S->value < 0) {        // 음수면
		대기열에 추가;
		block();
	}
}
signal(S) {
	S->value++;
	if (S->value <= 0) {       // 기다리는 사람이 있으면
		대기열에서 하나 꺼내 wakeup;
	}
}
```

여기서는 **value가 음수**가 될 수 있고, `-value` = 기다리는 스레드 수다.

| | 교과서 방식 | Pintos |
|---|-----------|--------|
| value 타입 | `int` (음수 가능) | `unsigned` (항상 ≥ 0) |
| 대기자 수 | `-value`로 알 수 있음 | `list_size (&waiters)` |
| down 시 검사 | 먼저 감소, 음수면 잠 | 0이면 잠, 깨면 **다시 검사(while)** 후 감소 |
| 깨어난 스레드 | 이미 "자리를 받은" 상태 | "다시 확인해 봐" 상태 (가로채기 가능) |

둘 다 맞는 구현이다. 시험/면접에서 헷갈리지 않도록 **Pintos는 value가 절대 음수가 아니다**를 기억하자.

---

## 20. 자주 하는 실수 / 헷갈리는 점

1. **`sema_up` = 즉시 실행이라고 착각**
   → 아니다. BLOCKED → READY로 옮길 뿐. 실행은 스케줄러가 결정. (우선순위가 더 높으면 `thread_preempt`로 곧바로 넘어가긴 함)

2. **`while`을 `if`로 바꿈** → 12장. unsigned 언더플로우로 value가 거대한 수가 됨.

3. **인터럽트 핸들러에서 `sema_down` / `lock_acquire`** → ASSERT 실패로 커널 패닉.

4. **`intr_disable()` 후 `intr_enable()`로 끝냄**
   → 호출자가 인터럽트를 끈 상태였다면 깨뜨림. 항상 `old_level` 저장 → `intr_set_level (old_level)`.

5. **세마포어를 락처럼만 생각**
   → 초기값 0 신호 패턴이 오히려 더 많이 쓰인다 (thread_start, cond_wait, 나중의 process_wait 등).

6. **waiters에 넣을 때만 정렬하면 충분하다고 생각**
   → 기부로 우선순위가 바뀌면 순서가 틀어짐. `sema_up`에서 꺼내기 직전 재정렬이 필요.

7. **`elem`을 두 리스트에 동시에 넣음**
   → 하나의 `list_elem`은 한 번에 하나의 리스트에만 들어갈 수 있다. waiters에서 pop 한 뒤 ready_list에 넣는 순서를 지켜야 함.
   (우선순위 기부에서 "내가 기다리는 락 목록"을 만들 때는 **별도의 elem**(예: `donation_elem`)이 필요한 이유)

8. **스택 위의 세마포어 수명**
   → `cond_wait`의 `waiter`, `thread_start`의 `idle_started`는 지역 변수다. 함수가 리턴하기 전에 반드시 up/down이 끝나야 안전하다. (Pintos 코드는 그렇게 되어 있음)

---

## 21. Q&A 모음

**Q1. 세마포어와 바쁜 대기의 차이를 한 줄로?**
A. 바쁜 대기는 "계속 확인하며 CPU를 태움", 세마포어는 "잠들어 있다가 누가 깨워 줌".

**Q2. `sema_down`은 왜 인터럽트를 끄고 시작하나?**
A. "value 검사 → waiters에 넣고 잠들기" 또는 "value 검사 → 감소"가 중간에 끊기면, 다른 스레드가 같은 value를 보고 둘 다 통과하는 경쟁 상태가 생기기 때문.

**Q3. 인터럽트가 꺼진 채로 `thread_block`을 하면 영원히 인터럽트가 꺼지나?**
A. 아니다. 전환된 다음 스레드가 자기 저장 상태로 인터럽트를 다시 켠다. 내가 돌아오면 내 `old_level`로 복원한다.

**Q4. `sema_up`은 왜 핸들러에서 불러도 되나?**
A. 절대 잠들지 않기 때문. 인터럽트 끄고, 한 명 unblock, value++, 끝.

**Q5. 락과 초기값 1 세마포어의 차이?**
A. 락은 주인(holder)이 있어서 획득한 스레드만 해제 가능하고 재귀 획득이 금지된다. 그 덕분에 우선순위 기부가 가능하다.

**Q6. 깨어난 스레드가 value를 못 가져갈 수도 있나?**
A. 그렇다. 깨어나서 실제로 실행되기 전에 다른 스레드가 `sema_down`으로 가로챌 수 있다. 그래서 `while`로 재검사하고, 실패하면 다시 잠든다.

**Q7. 왜 우선순위 기부는 세마포어가 아니라 락에서만 하나?**
A. 세마포어는 주인이 없어서 "누구에게 기부할지"를 알 수 없다. 락은 `holder`가 있다.

**Q8. Alarm Clock을 세마포어로 구현할 수도 있었나?**
A. 가능하다. 스레드마다 초기값 0 세마포어를 두고 `timer_sleep`에서 `sema_down`, 타이머 인터럽트에서 시간이 된 스레드에 `sema_up`. (`sema_up`은 핸들러에서 호출 가능하니까.) 내가 한 `sleep_list + thread_block/unblock` 방식은 사실상 이걸 직접 풀어 쓴 것이다.

**Q9. `cond_wait`에서 `lock_release`와 `sema_down` 사이에 signal이 오면 놓치지 않나?**
A. 놓치지 않는다. `list_push_back`으로 cond->waiters에 **먼저** 등록한 뒤 락을 놓기 때문에, signal은 내 개인 세마포어를 up 해서 value를 1로 만들어 둔다. 그 뒤 `sema_down`은 value가 1이므로 잠들지 않고 바로 통과한다. **세마포어는 "신호를 기억"한다** — 이게 세마포어가 단순한 sleep/wakeup보다 강력한 이유다.

---

## 22. 한 장 요약

```
┌──────────────────────────────────────────────────────────────────┐
│ 세마포어 = value(음이 아닌 정수) + waiters(잠든 스레드 리스트)        │
├──────────────────────────────────────────────────────────────────┤
│ sema_down (P)                                                    │
│   인터럽트 OFF                                                    │
│   while (value == 0) { waiters에 나를 (우선순위순) 넣고 block }     │
│   value--                                                        │
│   인터럽트 복원                     ※ 잠들 수 있음 → 핸들러 금지      │
├──────────────────────────────────────────────────────────────────┤
│ sema_up (V)                                                      │
│   인터럽트 OFF                                                    │
│   waiters가 있으면 정렬 → 맨 앞을 unblock (BLOCKED → READY)        │
│   value++                                                        │
│   깨운 스레드가 더 높으면 양보 (thread_preempt)                      │
│   인터럽트 복원                     ※ 안 잠듦 → 핸들러 OK            │
├──────────────────────────────────────────────────────────────────┤
│ 초기값 0 : 신호 / 순서 보장  (thread_start, cond_wait, 핑퐁)        │
│ 초기값 1 : 상호 배제        (lock 의 내부)                          │
│ 초기값 N : 자원 N개 관리                                           │
├──────────────────────────────────────────────────────────────────┤
│ 계층: 인터럽트 끄기 → 세마포어 → 락(+holder) → 조건 변수(+개인 세마포어) │
│ 깨어남 ≠ 획득  →  반드시 while 로 재검사                             │
│ 단일 CPU 이므로 인터럽트 끄기 = 완벽한 원자성                          │
│ 우선순위 기부는 holder 가 있는 '락'에서만                             │
└──────────────────────────────────────────────────────────────────┘
```
