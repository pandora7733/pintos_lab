# Alarm Clock 구현 상세서

> Pintos (KAIST x86-64) Project 1 — Threads
> 과제: `devices/timer.c`의 `timer_sleep()`을 **바쁜 대기(busy waiting) 없이** 다시 구현한다.

---

## 목차

1. [과제 요약](#1-과제-요약)
2. [배경 지식](#2-배경-지식)
3. [문제 분석: 기존 코드의 바쁜 대기](#3-문제-분석-기존-코드의-바쁜-대기)
4. [1단계: 설계](#4-1단계-설계)
5. [2단계: `struct thread`에 `wakeup_tick` 추가](#5-2단계-struct-thread에-wakeup_tick-추가)
6. [3단계: `sleep_list`와 비교 함수 만들기](#6-3단계-sleep_list와-비교-함수-만들기)
7. [4단계: `timer_sleep()` 재작성 (재우기)](#7-4단계-timer_sleep-재작성-재우기)
8. [5단계: `timer_interrupt()` 수정 (깨우기)](#8-5단계-timer_interrupt-수정-깨우기)
9. [전체 동작 흐름](#9-전체-동작-흐름)
10. [최종 변경 사항 (diff)](#10-최종-변경-사항-diff)
11. [빌드와 테스트](#11-빌드와-테스트)
12. [테스트 결과와 해석](#12-테스트-결과와-해석)
13. [디버깅 가이드](#13-디버깅-가이드)
14. [자주 하는 실수](#14-자주-하는-실수)
15. [다른 설계 방법과 비교](#15-다른-설계-방법과-비교)
16. [C 문법 정리](#16-c-문법-정리)
17. [Q&A 모음](#17-qa-모음)
18. [남은 과제와의 연결](#18-남은-과제와의-연결)

---

## 1. 과제 요약

| 항목 | 내용 |
|------|------|
| 수정 대상 | `devices/timer.c`의 `timer_sleep()` |
| 문제 | 잠들어야 할 스레드가 `while` + `thread_yield()`로 계속 시간을 확인하며 CPU를 낭비함 |
| 목표 | 잠든 스레드는 **CPU를 전혀 쓰지 않고**, 정해진 틱이 지나면 **READY 상태로 돌아오게** 만듦 |
| 함수 사용법 | 바뀌지 않음 — `void timer_sleep (int64_t ticks);` |

### 지켜야 할 조건

1. `timer_sleep(ticks)`의 시그니처와 의미는 그대로 둔다.
2. 지정한 틱보다 **일찍 깨어나면 안 된다**. 조금 늦는 것은 괜찮다.
3. 깨울 때는 READY 상태로 돌려놓기만 하면 된다. 바로 실행될 필요는 없다.
4. `while` + `thread_yield()`로 계속 확인하는 방식은 쓰지 않는다.
5. `TIMER_FREQ`(100)는 바꾸지 않는다. 바꾸면 많은 테스트가 실패한다.
6. `timer_msleep()`, `timer_usleep()`, `timer_nsleep()`은 수정하지 않는다. 내부에서 `timer_sleep()`을 호출하므로 함께 개선된다.

### 수정한 파일

| 파일 | 변경 |
|------|------|
| `include/threads/thread.h` | `struct thread`에 `int64_t wakeup_tick;` 추가 |
| `devices/timer.c` | `sleep_list` 선언·초기화, `wakeup_tick_less()` 추가, `timer_sleep()` 재작성, `timer_interrupt()` 수정 |

---

## 2. 배경 지식

### 2-1. 틱(tick)과 커널의 시계

- `include/devices/timer.h`에 `#define TIMER_FREQ 100`이 정의되어 있다.
  → **1초에 100틱, 1틱 = 10ms**.
- 8254 타이머 칩이 1틱마다 하드웨어 인터럽트를 발생시키고, 그때마다 `timer_interrupt()`가 실행된다.
- `timer_interrupt()`는 전역 변수 `ticks`를 1씩 증가시킨다. 이 값이 **커널의 시계**다.

```c
/* OS 부팅 이후 지난 타이머 틱 수. */
static int64_t ticks;
```

- `timer_ticks()`는 이 값을 안전하게 읽어서 반환한다. 읽는 도중 인터럽트가 값을 바꾸지 못하도록 잠깐 인터럽트를 끈다.

```c
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}
```

- `timer_elapsed(then)`은 `timer_ticks() - then`, 즉 `then` 이후 지난 틱 수를 반환한다.

### 2-2. 스레드의 4가지 상태

`include/threads/thread.h`:

```c
enum thread_status {
	THREAD_RUNNING,     /* 실행 중인 스레드. */
	THREAD_READY,       /* 실행 중은 아니지만 실행할 준비가 됨. */
	THREAD_BLOCKED,     /* 어떤 이벤트가 발생하기를 기다리는 중. */
	THREAD_DYING        /* 곧 파괴될 예정. */
};
```

| 상태 | 의미 | `elem`이 들어가 있는 곳 | 스케줄러가 고를 수 있나 |
|------|------|--------------------------|--------------------------|
| RUNNING | 지금 CPU를 사용 중 (항상 1개) | 없음 | — |
| READY | 실행 가능, 차례를 기다림 | `ready_list` | **예** |
| BLOCKED | 어떤 사건을 기다리며 잠듦 | 대기 리스트 (세마포어, **`sleep_list`** 등) | **아니요** |
| DYING | 종료됨, 메모리 해제 대기 | `destruction_req` | 아니요 |

```
           thread_create
                │
                ▼
  ┌───────── READY ◀──────────────┐
  │   schedule()가 선택        thread_yield / 선점
  ▼                              │
RUNNING ─────────────────────────┘
  │  │
  │  └─ thread_block ──▶ BLOCKED ── thread_unblock ──▶ READY
  │
  └─ thread_exit ──▶ DYING
```

### 2-3. 이번 과제에서 쓰는 스레드 함수

`threads/thread.c`:

| 함수 | 하는 일 | 조건 |
|------|---------|------|
| `thread_current()` | 지금 실행 중인 스레드의 `struct thread *` 반환 | — |
| `thread_block()` | **현재 스레드**를 BLOCKED로 만들고 다른 스레드로 전환 | 인터럽트 **꺼진** 상태, 인터럽트 핸들러 **밖**에서만 |
| `thread_unblock(t)` | BLOCKED인 **스레드 `t`**를 READY로 바꾸고 `ready_list` 맨 뒤에 넣음. **선점하지 않음** | `t`가 BLOCKED여야 함. 인터럽트 핸들러 안에서도 호출 가능 |
| `thread_yield()` | 현재 스레드를 READY로 `ready_list`에 넣고 다른 스레드로 전환 | 인터럽트 핸들러 밖에서만 |

```c
void
thread_block (void) {
	ASSERT (!intr_context ());                   // 인터럽트 핸들러 안이면 안 됨
	ASSERT (intr_get_level () == INTR_OFF);      // 인터럽트가 꺼져 있어야 함
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}
```

### 2-4. 스케줄러가 다음 스레드를 고르는 방법

```c
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}
```

- `ready_list`가 비어 있으면 **idle 스레드**를 실행한다. idle 스레드는 `sti; hlt`로 CPU를 쉬게 한다.
- 아니면 `ready_list`의 **맨 앞**을 꺼낸다 (현재는 우선순위를 보지 않는 FIFO).

### 2-5. Pintos 리스트 (`lib/kernel/list.c`)

```c
struct list_elem {
	struct list_elem *prev;     /* 이전 리스트 원소. */
	struct list_elem *next;     /* 다음 리스트 원소. */
};

struct list {
	struct list_elem head;      /* 리스트 head. */
	struct list_elem tail;      /* 리스트 tail. */
};
```

- **이중 연결 리스트**다. `head`와 `tail`은 실제 데이터가 없는 표지판(sentinel)이다.
- 리스트에 넣는 것은 구조체 전체가 아니라 **구조체 안에 달린 고리(`struct list_elem`)**다.
- 고리에서 원래 구조체를 되찾을 때는 `list_entry(고리주소, 구조체타입, 고리필드이름)`을 쓴다.

```
head ⇄ [스레드 B의 elem] ⇄ [스레드 A의 elem] ⇄ tail
```

이번 과제에서 쓰는 리스트 함수:

| 함수 | 하는 일 |
|------|---------|
| `list_init(&list)` | 빈 리스트로 초기화 (`head ⇄ tail`) |
| `list_empty(&list)` | 비어 있으면 `true` |
| `list_front(&list)` | 맨 앞 원소의 주소 반환 (**제거하지 않음**, 빈 리스트면 패닉) |
| `list_pop_front(&list)` | 맨 앞 원소를 **제거**하고 주소 반환 |
| `list_push_back(&list, &elem)` | 맨 뒤에 추가 |
| `list_insert_ordered(&list, &elem, less, aux)` | `less` 기준으로 정렬된 위치에 삽입 |

### 2-6. 인터럽트와 동기화

- Pintos는 **단일 CPU**다. 일반 코드 실행 중 동시에 다른 코드가 끼어드는 유일한 경로는 **인터럽트**다.
- 그래서 일반 코드와 인터럽트 핸들러가 함께 쓰는 데이터는 **인터럽트를 꺼서** 보호한다.
- 인터럽트 핸들러 안에서는 잠들 수 있는 함수(`thread_block`, `thread_yield`, `sema_down`, `lock_acquire`)를 **쓸 수 없다**.
- 외부 인터럽트 핸들러는 **인터럽트가 꺼진 상태로 실행**된다. 핸들러 실행 중에 다른 인터럽트가 끼어들지 않는다.

`threads/interrupt.c`의 인터럽트 상태 함수:

```c
enum intr_level {
	INTR_OFF,             /* 인터럽트 비활성화됨. */
	INTR_ON               /* 인터럽트 활성화됨. */
};

intr_disable (void) {
	enum intr_level old_level = intr_get_level ();
	asm volatile ("cli" : : : "memory");   // CPU의 인터럽트 플래그를 끈다
	return old_level;                      // 끄기 "전" 상태를 돌려준다
}

intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}
```

**표준 패턴**: 끄기 전 상태를 저장해 두었다가 원래대로 되돌린다.

```c
enum intr_level old_level = intr_disable ();
/* ... 보호가 필요한 작업 ... */
intr_set_level (old_level);
```

---

## 3. 문제 분석: 기존 코드의 바쁜 대기

### 3-1. 기존 코드

```c
/* 약 TICKS 타이머 틱 동안 실행을 중단한다. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	while (timer_elapsed (start) < ticks)
		thread_yield ();
}
```

### 3-2. 실제로 일어나는 일

```
잠들려는 스레드 A
  ├─ 시간 됐나? → 아니오 → thread_yield() → READY로 ready_list에 들어감
  ├─ (스케줄러가 다시 A를 고름) → RUNNING
  ├─ 시간 됐나? → 아니오 → thread_yield() → READY
  ├─ ... 수십~수천 번 반복 ...
  └─ 시간 됐나? → 예 → 반환
```

### 3-3. 왜 문제인가

1. 잠들어 있어야 할 스레드가 **계속 스케줄되어** CPU를 쓴다.
2. 스레드가 바뀔 때마다 문맥 교환(`thread_launch` → `do_iret`) 비용이 든다.
3. 잠든 스레드가 계속 READY 상태라서 **`ready_list`가 비지 않는다**. 정말 할 일이 없어도 idle 스레드가 실행되지 못하고, CPU가 헛돈다(전력 소모, 발열).
4. 실제로 일이 있는 스레드도 `ready_list`에서 바쁜 대기 스레드들 뒤에 줄을 서야 해서 **반응이 늦어진다**.

### 3-4. 비유

| 방식 | 비유 |
|------|------|
| 바쁜 대기 (기존) | 전자레인지 앞에 서서 "다 됐나?" 확인하고, 아니면 뒷사람에게 잠깐 비켜 줬다가 곧바로 다시 줄을 서서 또 확인 |
| 잠들기 (구현) | 자리에 가서 쉬다가, 시간이 되면 **누군가 불러 줄 때** 다시 줄을 섬 |

### 3-5. 오해 바로잡기: 잠드는 것은 idle을 만드는 것이 아니다

- 스레드가 잠들면 **CPU를 양보**할 뿐이다. `ready_list`에 다른 스레드가 있으면 그 스레드가 즉시 CPU를 쓴다.
- idle 스레드는 **할 일이 하나도 없을 때만** 운영체제가 **의도적으로** 실행한다 (`next_thread_to_run`). 운영체제는 idle 상태를 정확히 알고 통계(`idle ticks`)도 센다.
- 평범한 컴퓨터는 대부분의 시간 동안 할 일이 없어 idle 상태다. 이건 나쁜 것이 아니다. **할 일이 없는데도 바쁜 대기로 CPU를 헛돌리는 것**이 나쁜 것이다.

> 목표는 "idle을 없애는 것"이 아니라 **"쓸모없는 일로 CPU를 채우지 않는 것"**이다.

### 3-6. 스레드는 왜 정해진 시간 동안 잠드는가

얼마나, 왜 잘지는 **`timer_sleep()`을 호출하는 프로그램이 자기 목적에 따라** 정한다. 운영체제는 "X틱 뒤에 깨워 달라"는 요청을 지켜 줄 뿐이다.

| 기다리는 대상 | 예 | 도구 |
|---------------|-----|------|
| **어떤 사건** (끝났다는 신호가 옴) | 디스크 읽기 완료, 키보드 입력, 락 해제 | 세마포어, 락, 조건 변수 — 신호가 오면 **즉시** 깨어남 |
| **시간 자체** (신호가 없거나 시간이 목적) | 장치 안정화 대기, 커서 깜빡임, 주기적 작업, 재시도 간격 | `timer_sleep()` |

실제 Pintos 예 — `devices/disk.c`:

- **디스크 읽기/쓰기**는 시간을 두지 않는다. 디스크가 작업을 끝내면 인터럽트로 알려 주고, `interrupt_handler`가 `sema_up(&c->completion_wait)`으로 기다리던 스레드를 바로 깨운다(이벤트 기반).
- **디스크 리셋**은 `timer_usleep(10)`, `timer_msleep(150)`으로 기다린다. 리셋 중에는 장치가 신호를 보낼 수 없고, ATA 규격이 "리셋 후 최소 이만큼 기다려라"라고 정해 두었기 때문이다(시간 기반).

테스트(`alarm-*`)에서 스레드들이 10, 20, 30틱씩 자는 것은 실용적 이유가 아니라 **`timer_sleep()`이 정확히 동작하는지 검사하기 위해서**다.

---

## 4. 1단계: 설계

### 4-1. 핵심 아이디어

기존 방식을 뒤집는다.

> 잠든 스레드는 아무것도 하지 않고, **다른 누군가가 시간을 확인해서 깨워 준다.**

### 4-2. 설계 질문과 답

| 질문 | 답 | 이유 |
|------|-----|------|
| Q1. CPU를 전혀 쓰지 않는 상태는? | **`THREAD_BLOCKED`** | READY는 `ready_list`에 있어 다시 실행된다. BLOCKED는 스케줄러 눈에 보이지 않는다 |
| Q2. 재우는/깨우는 함수는? | **`thread_block()` / `thread_unblock(t)`** | `thread_block()`은 자기 자신만 재울 수 있고, `thread_unblock(t)`는 다른 스레드를 깨운다. 잠든 스레드는 스스로 깨어날 수 없다 |
| Q3. 누가 깨워 주나? | **`timer_interrupt()`** | 매 틱마다 하드웨어에 의해 무조건 실행되고, `ticks`가 바뀌는 바로 그 자리다 |
| Q4. 무엇을 어디에 저장하나? | 각 스레드에 **깨어날 절대 시각 `wakeup_tick`**, 잠든 스레드들은 **`sleep_list`**에 시각 순 정렬 | 아래 4-3 참고 |
| Q5. 인터럽트 문맥에서 주의할 점은? | 핸들러 안에서는 `thread_unblock()`만 사용. `sleep_list`는 양쪽에서 쓰므로 `timer_sleep()` 쪽에서 **인터럽트를 꺼서** 보호 | 핸들러는 잠들 수 없고 락도 쓸 수 없다 |

### 4-3. 저장 방식 결정

**① 남은 시간 vs 깨어날 시각**

| 방식 | 저장 값 | 매 틱마다 할 일 |
|------|---------|----------------|
| 남은 시간(상대값) | `50` | 잠든 스레드 **모두**의 값을 1씩 줄여야 함 |
| **깨어날 시각(절대값)** | `시작 시각 + 50` | 현재 `ticks`와 **비교만** 하면 됨 |

→ 절대 시각을 쓴다. 값이 한 번 정해지면 바뀌지 않아 단순하다.

**② 정렬된 리스트**

`sleep_list`를 **깨어날 시각이 빠른 순**으로 정렬해 두면, 인터럽트 핸들러는 **맨 앞만** 확인하면 된다. 맨 앞 스레드가 아직이면 뒤쪽도 모두 아직이다. 인터럽트 핸들러는 짧고 빨라야 하므로 중요하다.

**③ 리스트 고리는 기존 `elem` 재사용**

`elem`은 READY일 때는 `ready_list`에, BLOCKED일 때는 대기 리스트에 쓰인다. 잠든 스레드는 BLOCKED이므로 `ready_list`에 없다. 따라서 `elem`을 `sleep_list`에 써도 충돌하지 않는다. 새 고리를 추가할 필요가 없다.

### 4-4. 인터럽트 문맥 규칙

| 위치 | 쓸 수 있는 것 | 쓸 수 없는 것 |
|------|---------------|---------------|
| `timer_interrupt()` (인터럽트 문맥) | `thread_unblock()` | `thread_block()`, `thread_yield()`, 락, `sema_down()` 등 잠들 수 있는 모든 것 |
| `timer_sleep()` (일반 스레드 문맥) | `thread_block()` (인터럽트를 끈 뒤) | — |

### 4-5. 설계 결과

```
[스레드 A] timer_sleep(100)
   ├─ wakeup_tick = 현재 ticks + 100 저장
   ├─ 인터럽트 끄기
   ├─ sleep_list에 깨어날 시각 순으로 삽입
   ├─ thread_block()  ──────▶ A는 BLOCKED. CPU 안 씀
   │                              ⋮
   │                    [timer_interrupt] 매 틱마다
   │                       ├─ ticks++
   │                       └─ sleep_list 맨 앞의 wakeup_tick <= ticks 이면
   │                            리스트에서 빼고 thread_unblock(A) → READY
   │                              ⋮
   ├─ (스케줄러가 A를 고르면 여기서 다시 실행 재개)
   └─ 인터럽트 상태 복원 후 반환
```

| 재료 | 역할 | 단계 |
|------|------|------|
| `wakeup_tick` | 각 스레드가 깨어날 시각을 적는 칸 | 2단계 |
| `sleep_list` + `wakeup_tick_less` | 잠든 스레드를 깨어날 시각 순으로 세우는 줄 | 3단계 |
| `timer_sleep()` 재작성 | 스레드를 줄에 세우고 재우기 | 4단계 |
| `timer_interrupt()` 수정 | 시간이 된 스레드를 깨우기 | 5단계 |

---

## 5. 2단계: `struct thread`에 `wakeup_tick` 추가

### 5-1. 코드

`include/threads/thread.h`

```c
struct thread {
	/* thread.c가 소유한다. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름 (디버깅 용도). */
	int priority;                       /* 우선순위. */

	int64_t wakeup_tick;                 /* timer_sleep()으로 잠들었을 때 깨어날 시각(틱) */

	/* thread.c와 synch.c가 공유한다. */
	struct list_elem elem;              /* 리스트 원소. */
	...
	/* thread.c가 소유한다. */
	struct intr_frame tf;               /* 스레드 전환을 위한 정보 */
	unsigned magic;                     /* 스택 오버플로를 감지한다. */
};
```

### 5-2. 역할

`wakeup_tick`은 **"이 스레드가 몇 번째 틱에 깨어나야 하는지"를 적어 두는 메모**다.

잠든 스레드는 BLOCKED라서 아무 코드도 실행하지 못한다. 기존 코드처럼 지역 변수 `start`를 들고 스스로 시간을 확인할 수 없다. 그래서 깨워 주는 쪽(타이머 인터럽트)이 볼 수 있는 곳, 즉 **스레드 구조체 안**에 깨어날 시각을 적어 둔다.

| 시점 | 누가 | `wakeup_tick`으로 하는 일 |
|------|------|---------------------------|
| 잠들 때 (`timer_sleep`) | 스레드 자신 | `timer_ticks() + ticks`를 **기록** |
| 리스트에 넣을 때 (`wakeup_tick_less`) | 정렬 비교 함수 | 다른 스레드와 **비교**해 위치 결정 |
| 매 틱마다 (`timer_interrupt`) | 타이머 인터럽트 | 현재 `ticks`와 **비교**해 깨울지 판단 |

**깨어날 시각 = 잠드는 순간의 현재 시각(`timer_ticks()`) + 잠들 시간(`timer_sleep`의 인자)**

```
ticks: 1000 ─── 1001 ─── ... ─── 1049 ─── 1050
        │                                  │
   A 잠듦 (wakeup_tick = 1050)          ticks >= 1050 → A 깨움
```

### 5-3. 설계 이유

| 결정 | 이유 |
|------|------|
| 타입 `int64_t` | 비교 대상인 `ticks`, `timer_ticks()`가 `int64_t`다. 타입을 맞춰야 크기·부호 변환 문제가 없다. `<stdint.h>`는 이미 포함되어 있다 |
| `magic` 앞에 위치 | `magic`은 스택 오버플로 경보선이라 **반드시 구조체 마지막**이어야 한다. 새 필드는 항상 `magic` 앞에 둔다 |
| 별도 초기화 없음 | `init_thread()`가 `memset(t, 0, sizeof *t)`로 구조체 전체를 0으로 채운다. 또 `timer_sleep()`이 호출될 때마다 새로 설정된다 |
| 크기 | 8바이트 증가. `struct thread`는 1KB보다 한참 작아야 하는데 문제없다 |
| 오버플로 | `int64_t` 최대값 ≈ 9.2×10¹⁸. 초당 100틱이면 약 29억 년 뒤에야 넘친다 |

### 5-4. 헤더에 넣은 이유

`.h` 파일은 여러 `.c` 파일이 공유하는 선언을 모아 두는 곳이다. `struct thread`는 `thread.c`, `timer.c`, `synch.c` 등 여러 파일에서 쓰인다. 각 `.c` 파일은 `#include "threads/thread.h"`로 헤더 내용을 복사해 온다. 그래서 헤더를 고치면 이 헤더를 포함하는 **모든 파일이 다시 컴파일**된다.

### 5-5. 체크포인트

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads
make
```

마지막 줄이 `cat loader.bin kernel.bin > os.dsk`면 성공. `init.c`, `mmu.c`, `disk.c`, `stdio.c`, `string.c`, `mlfqs-load-avg.c`의 경고는 **원래 제공된 코드에서 항상 나오는 것**이라 무시해도 된다.

---

## 6. 3단계: `sleep_list`와 비교 함수 만들기

모두 `devices/timer.c`에서 작업한다.

### 6-1. `sleep_list` 선언

```c
/* OS 부팅 이후 지난 타이머 틱 수. */
static int64_t ticks;

// timer_sleep()으로 잠든 스레드들의 리스트.
// wakeup_tick이 작은(먼저 깨어날) 스레드가 앞에 오도록 정렬되어 있음
static struct list sleep_list;
```

| 요소 | 의미 |
|------|------|
| `struct list` | Pintos가 제공하는 이중 연결 리스트 타입 |
| `static` (함수 밖) | **이 파일 안에서만 보이는 전역 변수**. 다른 파일이 실수로 건드릴 수 없다 |
| 함수 밖 선언 | 커널이 실행되는 동안 계속 살아 있다 |

**왜 `timer.c`에 두나?** 잠재우는 쪽(`timer_sleep`)과 깨우는 쪽(`timer_interrupt`)이 모두 이 파일에 있다. 리스트를 쓰는 코드가 한 파일에 모여 있으면 관리가 쉽다.

**`#include`가 더 필요 없는 이유**: `timer.c`가 포함하는 `"threads/thread.h"`가 `<list.h>`를 포함한다.

#### `sleep_list`는 FILO인가?

**아니다.** FILO(스택)도, 단순 FIFO(큐)도 아닌 **"깨어날 시각 순으로 정렬된 리스트"**다.

| 방식 | 넣는 곳 | 꺼내는 곳 | 먼저 나가는 것 |
|------|---------|-----------|----------------|
| FILO/LIFO (스택) | 맨 위 | 맨 위 | 가장 나중에 들어온 것 |
| FIFO (큐) | 맨 뒤 | 맨 앞 | 가장 먼저 들어온 것 |
| **`sleep_list`** | **시각 순 자리** | **맨 앞** | **깨어날 시각이 가장 빠른 것** |

`struct list` 자체는 어떤 방식도 강제하지 않는다. **어떻게 넣고 어디서 꺼내느냐**가 방식을 정한다.

```
A(1050) 잠듦:  head ⇄ A(1050) ⇄ tail
B(1020) 잠듦:  head ⇄ B(1020) ⇄ A(1050) ⇄ tail
C(1035) 잠듦:  head ⇄ B(1020) ⇄ C(1035) ⇄ A(1050) ⇄ tail
깨어나는 순서: B → C → A
```

### 6-2. `list_init()`으로 초기화

```c
void
timer_init (void) {
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: 카운터 0, LSB 다음 MSB, 모드 2, 이진수. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	list_init(&sleep_list); // sleep_list 초기화

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}
```

`list_init()`의 실제 동작 (`lib/kernel/list.c`):

```c
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;   // head 다음은 tail
	list->tail.prev = &list->head;   // tail 이전은 head
	list->tail.next = NULL;
}
```

```
초기화 후:   NULL ← head ⇄ tail → NULL      (빈 리스트)
```

초기화하지 않으면 포인터에 쓰레기 값이 있어서 리스트를 쓰는 순간 커널이 멈춘다.

**왜 `&sleep_list`(주소)를 넘기나?** C는 함수에 값을 넘기면 **복사본**을 전달한다. 복사본을 초기화해 봐야 원본은 그대로다. 원본을 바꾸려면 원본의 주소를 넘겨야 한다.

**왜 `timer_init()`인가?** `threads/init.c`의 부팅 순서:

```c
	thread_init ();      // 82행
	...
	intr_init ();
	timer_init ();       // 97행  ← sleep_list 초기화
	...
	thread_start ();     // 106행 ← 처음으로 인터럽트가 켜짐
	...
	timer_calibrate ();
```

`timer_init()`은 **인터럽트가 켜지기 전**, **누군가 `timer_sleep()`을 부르기 전**에 **딱 한 번** 실행된다.

**왜 `intr_register_ext()` 앞인가?** `intr_register_ext()`가 타이머 인터럽트 핸들러를 등록한다. 핸들러가 `sleep_list`를 읽으므로, 등록 전에 리스트가 준비되어 있는 것이 자연스럽다.

### 6-3. 비교 함수 원형 선언

```c
static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

static bool wakeup_tick_less (const struct list_elem *a, const struct list_elem *b, void *aux);
```

C 컴파일러는 파일을 위에서 아래로 읽는다. 원형(prototype)은 "이런 함수가 아래 어딘가에 있다"는 약속이다. 이 파일의 기존 스타일(`too_many_loops`, `busy_wait`)을 따른 것이고, Pintos는 `-Wmissing-prototypes` 같은 엄격한 경고 옵션으로 빌드한다.

### 6-4. 비교 함수 본문

`timer_sleep()` 바로 위에 둔다.

```c
// sleep_list 정렬용 비교 함수. A의 깨어날 시각이 B보다 빠르면 true를 반환한다.
static bool
wakeup_tick_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);

	return ta->wakeup_tick < tb->wakeup_tick;
}
```

#### 왜 비교 함수가 필요한가

4단계의 `list_insert_ordered()`는 원소가 무엇인지 모르는 **범용 함수**다. "A가 B보다 앞에 와야 하는가?"를 판단하는 함수를 **우리가 만들어 넘겨줘야** 한다.

```c
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
```

함수 모양은 `list_less_func` 타입으로 정해져 있다. **반환형과 매개변수가 정확히 같아야** 넘길 수 있다.

```c
/* A가 B보다 작으면 true를, A가 B보다 크거나 같으면 false를 반환한다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);
```

#### 왜 `bool`을 반환하나

이 함수가 답하는 질문이 **"예/아니요" 질문**이기 때문이다.

> "A가 B보다 앞에 와야 하나?" → `true` / `false`

`bool`은 `<stdbool.h>`가 제공하는 참/거짓 타입이다 (`list.h`가 포함). 반환값은 `list_insert_ordered()` 안의 `if`에서 쓰인다.

```c
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;
	...
	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))     // ← 우리 함수 호출
			break;
	return list_insert (e, elem);    // e 앞에 삽입
}
```

1. 리스트 맨 앞부터 원소 `e`를 하나씩 본다.
2. `less(새 원소, e)`가 처음으로 `true`가 되면 멈춘다.
3. 그 `e` **앞에** 새 원소를 끼워 넣는다. 끝까지 `true`가 없으면 맨 뒤에 넣는다.

#### `list_entry()`의 역할

비교 함수가 받는 것은 `struct thread` 전체가 아니라 **그 안의 `elem` 주소**뿐이다. `wakeup_tick`을 읽으려면 고리 주소에서 스레드 주소로 되돌아가야 한다.

```
             struct thread (시작 주소 = ta)
             ┌──────────────┐
  ta  ─────▶ │ tid          │
             │ status       │
             │ name[16]     │
             │ priority     │
             │ wakeup_tick  │
  a   ─────▶ │ elem         │   ← 비교 함수가 받은 주소
             │ ...          │
             └──────────────┘
```

```c
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))
```

원리: **고리의 주소 − 고리가 구조체 안에서 떨어진 거리(`offsetof`) = 구조체 시작 주소**. 사용법만 기억하면 된다.

```c
list_entry (고리 주소, 구조체 타입, 고리 필드 이름)
```

#### 왜 `<`이고 `<=`가 아닌가

같은 시각끼리는 **먼저 잠든 스레드가 앞**에 서도록(FIFO, 새치기 방지) 하기 위해서다.

```
현재:  head ⇄ B(1020) ⇄ C(1035) ⇄ A(1050) ⇄ tail
D(1035) 삽입:
  less(D, B)?  1035 < 1020 → false → 계속
  less(D, C)?  1035 < 1035 → false → 계속   ← '<'라서 같으면 false
  less(D, A)?  1035 < 1050 → true  → A 앞에 삽입
결과:  head ⇄ B(1020) ⇄ C(1035) ⇄ D(1035) ⇄ A(1050) ⇄ tail
```

`<=`를 쓰면 D가 C 앞에 끼어든다.

#### `aux UNUSED`

`list_less_func` 모양을 맞추려면 `aux`가 있어야 하지만 쓰지 않는다. `UNUSED`(`include/lib/debug.h`, `__attribute__((unused))`)로 "사용하지 않는 매개변수" 경고를 막는다.

### 6-5. 체크포인트

```bash
make
```

이 시점에는 `wakeup_tick_less`가 아직 호출되지 않아 `defined but not used` 경고가 날 수 있다. 4단계에서 사라진다.

---

## 7. 4단계: `timer_sleep()` 재작성 (재우기)

### 7-1. 코드

```c
/* 약 TICKS 타이머 틱 동안 실행을 중단한다. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();
	struct thread *cur;
	enum intr_level old_level;

	ASSERT (intr_get_level () == INTR_ON);

	/* 0 이하의 시간 만큼 잘 필요는 없기 때문에 바로 반환*/
	if (ticks <= 0)
		return;

	cur = thread_current ();

	/* sleep_list는 타이머 인터럽트 핸들러도 접근하기 때문에
	리스트를 수정하고 잠드는 동안 인터럽트를 죽인다. */
	old_level = intr_disable ();

	cur->wakeup_tick = start + ticks; // 깨어날 시각 설정
	list_insert_ordered (&sleep_list, &cur->elem, wakeup_tick_less, NULL); // sleep_list에 삽입
	thread_block (); // 스레드를 잠근다

	// 깨어나면 여기서부터 재실행
	intr_set_level (old_level);
}
```

### 7-2. 흐름

```
timer_sleep(50) 호출 (현재 ticks = 1000)
 ├─ ① start = 1000                      현재 시각 기록
 ├─ ② ASSERT: 인터럽트가 켜져 있는가
 ├─ ③ ticks <= 0 이면 바로 반환
 ├─ ④ cur = 나 자신
 ├─ ⑤ 인터럽트 끄기 (이전 상태 저장)
 ├─ ⑥ cur->wakeup_tick = 1050
 ├─ ⑦ sleep_list에 시각 순으로 삽입
 ├─ ⑧ thread_block()  ── BLOCKED, 다른 스레드로 전환 ──┐
 │                                                     │ (여기서 멈춤)
 │                 ... 5단계: ticks=1050에서 깨워 줌 ...│
 ├─ ⑨ 스케줄러가 다시 고르면 여기서 재개 ◀─────────────┘
 └─ ⑩ 인터럽트 상태 복원 후 반환
```

### 7-3. 줄별 해설

#### ① 지역 변수

```c
	int64_t start = timer_ticks ();
	struct thread *cur;
	enum intr_level old_level;
```

- `start`: 호출된 순간의 시각. "호출된 순간부터 `ticks`만큼" 자는 것이 함수의 의미라서 가장 먼저 확정한다.
- `cur`: 현재 스레드를 가리킬 포인터.
- `old_level`: 인터럽트를 끄기 전 상태(`INTR_ON` / `INTR_OFF`).

> ⚠️ **이름 가림(shadowing)**: 파일 위쪽 전역 변수 `ticks`(현재 시각)와 매개변수 `ticks`(잠들 시간)의 이름이 같다. C에서는 **안쪽 이름이 바깥쪽을 가린다**. `timer_sleep()` 안에서 `ticks`는 **항상 매개변수**다. 그래서 현재 시각은 `timer_ticks()` 함수로 읽는다.

#### ② 인터럽트 상태 검사

```c
	ASSERT (intr_get_level () == INTR_ON);
```

인터럽트가 꺼진 상태로 호출되면 타이머 인터럽트가 오지 않아 `ticks`가 늘지 않고, 영원히 깨어날 수 없다. 잘못된 호출을 미리 잡는다. 기존 코드에도 있던 줄이다.

#### ③ 0 이하 처리

```c
	if (ticks <= 0)
		return;
```

`timer_sleep(0)`(`alarm-zero`), `timer_sleep(-100)`(`alarm-negative`)는 "자지 않는다"는 뜻이다. 기존 코드도 `while` 조건이 처음부터 거짓이 되어 바로 반환했다. 이 동작을 유지한다. 이 처리가 없으면 깨어날 시각이 현재 이하로 정해져 쓸데없이 잠들었다가 다음 틱에 깨어나는 왕복이 생긴다.

#### ④ 현재 스레드

```c
	cur = thread_current ();
```

잠들 대상은 `timer_sleep()`을 부른 바로 그 스레드다.

#### ⑤ 인터럽트 끄기 — 이 과제에서 가장 중요한 동기화 포인트

```c
	old_level = intr_disable ();
```

**이유 1. `sleep_list` 보호**

`sleep_list`는 `timer_sleep()`(삽입)과 `timer_interrupt()`(제거) 양쪽에서 수정된다. `list_insert_ordered()`가 `prev`/`next`를 반쯤 고쳐 놓은 순간 인터럽트가 끼어들어 같은 리스트를 수정하면 **리스트가 끊어지거나 꼬인다**. 인터럽트 핸들러는 락을 쓸 수 없으므로 **인터럽트를 끄는 것이 유일한 보호 방법**이다.

**이유 2. "삽입"과 "잠들기" 사이의 틈 막기**

```
⑦ sleep_list에 A 삽입
      ← 이 순간 타이머 인터럽트! A가 깨어날 시각이라
        thread_unblock(A) 호출
        → ASSERT(t->status == THREAD_BLOCKED) 실패! (A는 아직 RUNNING)
⑧ thread_block()
```

두 동작은 **중간에 끊기면 안 되는 한 덩어리(원자적)**여야 한다.

**이유 3. `thread_block()`의 요구사항**

`thread_block()`은 `ASSERT (intr_get_level () == INTR_OFF);`로 인터럽트가 꺼져 있어야만 호출할 수 있다.

**`start`를 인터럽트를 끄기 전에 읽어도 되는 이유**: `start`와 인터럽트를 끄는 사이에 틱이 지나가도, 깨어날 시각은 `start + ticks`로 고정되어 있다. 깨어나는 조건은 `ticks(전역) >= start + ticks(인자)`, 즉 **호출 시점부터 최소 `ticks`틱이 지난 뒤**라서 과제 조건을 만족한다.

#### ⑥ 깨어날 시각 기록

```c
	cur->wakeup_tick = start + ticks;
```

`cur`가 포인터라서 `->`를 쓴다. 2단계에서 만든 칸에 처음으로 값이 들어간다.

#### ⑦ 리스트에 정렬 삽입

```c
	list_insert_ordered (&sleep_list, &cur->elem, wakeup_tick_less, NULL);
```

| 인자 | 의미 | 문법 |
|------|------|------|
| `&sleep_list` | 어느 리스트에 | 리스트의 주소 (원본 수정) |
| `&cur->elem` | 무엇을 | 내 스레드 고리의 주소. `->`가 `&`보다 먼저 계산되어 `&(cur->elem)` |
| `wakeup_tick_less` | 어떤 기준으로 | **함수 이름 = 함수의 주소(함수 포인터)** |
| `NULL` | 비교 함수에 줄 추가 정보 | 쓰지 않음 |

실행 중(RUNNING)인 스레드의 `elem`은 `ready_list`에 없고(스케줄러가 꺼냈음), 곧 BLOCKED가 되므로 `sleep_list`에 써도 안전하다.

#### ⑧ 잠들기 — 바쁜 대기가 사라지는 지점

```c
	thread_block ();
```

1. 내 상태가 **BLOCKED**가 된다. `ready_list`에 없으므로 스케줄러가 고르지 않는다.
2. `schedule()`이 다른 스레드(또는 idle)로 CPU를 넘긴다.
3. 내 실행은 **이 줄에서 멈춘다**. 함수가 반환되지 않은 채 얼어붙는다.
4. 5단계의 타이머 인터럽트가 `thread_unblock()`하면 READY가 된다.
5. 스케줄러가 나를 고르면 **다음 줄부터** 이어서 실행된다.

| | 기존 `thread_yield()` | 새 `thread_block()` |
|---|-------------------------|--------------------------|
| 상태 | READY | **BLOCKED** |
| `ready_list`에 들어가나 | 들어감 → 곧 다시 실행 | 안 들어감 → **실행 안 됨** |
| 다시 실행되려면 | 차례만 오면 됨 | **누군가 `thread_unblock()` 해야 함** |
| 반복 | `while`로 수없이 | **딱 한 번** |

#### ⑩ 인터럽트 복원

```c
	intr_set_level (old_level);
```

②의 `ASSERT`로 원래 켜져 있었음이 보장되므로 다시 켜진다.

> **잠든 동안 인터럽트가 계속 꺼져 있는가?** 아니다. 인터럽트 상태(`eflags`의 IF 비트)는 **스레드마다 저장·복원**된다(`thread_launch()`가 `eflags`를 `tf`에 저장). A가 잠들어 B로 전환되면 B의 인터럽트 상태가 복원되므로 타이머 인터럽트는 계속 들어온다.

### 7-4. 주의

4단계만 끝난 상태에서는 **깨우는 코드가 없어서** 잠든 스레드가 영원히 깨어나지 못한다. 빌드만 하고 테스트는 5단계 이후에 실행한다.

---

## 8. 5단계: `timer_interrupt()` 수정 (깨우기)

### 8-1. 코드

```c
/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;

	/* sleep_list는 깨어날 시각 순으로 정렬되어 있어서
	 맨 앞에서 부터 깨어날 시간이 된 스레드를 모두 깨움 */
	while (!list_empty (&sleep_list)) {
		struct thread *t = list_entry (list_front (&sleep_list), struct thread, elem);

		/* 맨 앞 스레드가 아직 깨어날 때가 아니면 뒤쪽도 모두 아님 */
		if (t->wakeup_tick > ticks)
			break;

		list_pop_front (&sleep_list); // sleep_list에서 제거
		thread_unblock (t); // 스레드 깨움
	}

	thread_tick ();
}
```

### 8-2. 흐름 예시

`ticks`가 1049 → 1050이 되는 순간:

```
sleep_list:  head ⇄ B(1050) ⇄ C(1050) ⇄ A(1080) ⇄ tail

timer_interrupt()
 ├─ ticks++  → 1050
 ├─ 1회차: 비었나? 아니오. 맨 앞 B(1050) > 1050? 아니오 → B 제거, 깨움
 ├─ 2회차: 비었나? 아니오. 맨 앞 C(1050) > 1050? 아니오 → C 제거, 깨움
 ├─ 3회차: 비었나? 아니오. 맨 앞 A(1080) > 1050? 예 → break
 └─ thread_tick()

결과 sleep_list:  head ⇄ A(1080) ⇄ tail
      ready_list:  ... ⇄ B ⇄ C
```

### 8-3. 줄별 해설

#### ① `ticks++;`

커널 시계를 한 칸 움직인다. 여기서 `ticks`는 **전역 변수**다 (`timer_interrupt`의 매개변수는 `args`뿐이라 가림이 없다).

#### ② `while (!list_empty (&sleep_list))`

- `!`는 논리 부정. "리스트가 비어 있지 않은 동안".
- **빈 리스트 검사가 필요한 이유**: `list_front()`는 `ASSERT (!list_empty (list));`로 빈 리스트면 패닉한다.
- **`if`가 아니라 `while`인 이유**: **같은 틱에 깨어나야 하는 스레드가 여러 개**일 수 있다. `if`면 한 틱에 하나만 깨우고 나머지는 늦게 깨어난다. `alarm-simultaneous`가 이 경우를 검사한다.

#### ③ 맨 앞 스레드 보기

```c
		struct thread *t = list_entry (list_front (&sleep_list), struct thread, elem);
```

1. `list_front()`: 첫 번째 고리의 주소 (**제거하지 않음**).
2. `list_entry()`: 고리 주소 → `struct thread` 주소.
3. 반복문 블록 `{ }` 안에서 선언해 필요한 범위에서만 존재하게 했다.

#### ④ 깨어날 시각 확인

```c
		if (t->wakeup_tick > ticks)
			break;
```

- `break`: 가장 가까운 반복문을 즉시 빠져나간다.
- **맨 앞만 보고 멈춰도 되는 이유**: 리스트가 시각 순으로 정렬되어 있다. 맨 앞조차 아직이면 뒤쪽은 더 늦다. 대부분의 틱에서 **비교 한 번으로 끝난다**.
- **`>`인 이유**: 깨우는 조건은 `ticks >= wakeup_tick`, 멈추는 조건은 그 반대인 `wakeup_tick > ticks`. `wakeup_tick == ticks`일 때 깨워야 정확히 요청한 틱 수만큼 잔 것이 되고, "최소 x틱 이후" 조건도 만족한다.

#### ⑤ 제거 후 깨우기

```c
		list_pop_front (&sleep_list);
		thread_unblock (t);
```

```c
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}
```

- 반환값은 이미 `t`로 구해 두었으므로 버린다.
- **순서가 중요하다: 반드시 빼고 나서 깨운다.** `t->elem` 고리 하나를 두 리스트가 공유한다. `thread_unblock()`은 이 고리를 `ready_list`에 연결하며 `prev`/`next`를 덮어쓴다. `sleep_list`에서 빼기 전에 깨우면 고리가 두 리스트에 걸쳐 **둘 다 망가진다**.
- **인터럽트 핸들러에서 `thread_unblock()`을 써도 되는 이유**: 잠들지 않고 리스트만 조작한다. 또 **선점하지 않으므로** 핸들러 도중 다른 스레드로 넘어가지 않는다.

#### 인터럽트를 따로 끄지 않는 이유

외부 인터럽트 핸들러는 **인터럽트가 꺼진 상태로 실행**된다. 그리고 `timer_sleep()` 쪽은 리스트를 수정하는 동안 인터럽트를 꺼 둔다. 따라서 **두 쪽이 동시에 `sleep_list`를 건드리는 일은 절대 없다**. 4단계와 5단계가 짝을 이뤄 리스트를 보호한다.

#### ⑥ `thread_tick ();`

통계를 갱신하고, 타임 슬라이스(4틱)를 다 쓴 스레드에 대해 인터럽트 종료 시 양보하도록 예약(`intr_yield_on_return()`)한다.

**깨우는 코드를 `thread_tick()` 앞에 둔 이유**: `ticks++`로 시각이 바뀐 직후 그 시각에 맞춰 깨우는 것이 논리적으로 자연스럽다. 알람 테스트 결과에는 순서가 영향을 주지 않는다.

---

## 9. 전체 동작 흐름

```
        [스레드 A: timer_sleep(50)]                  [타이머 인터럽트: 매 틱]
 ticks=1000
   인터럽트 OFF
   A.wakeup_tick = 1050
   sleep_list에 A 삽입 ──────────────┐
   thread_block() → A BLOCKED         │
   (다른 스레드가 CPU 사용 / 없으면 idle)
                                      │      ticks=1001: 맨 앞 A(1050) > 1001 → break
                                      │      ticks=1002: 맨 앞 A(1050) > 1002 → break
                                      │         ...   (매번 비교 한 번)
                                      └────▶ ticks=1050: A(1050) > 1050? 아니오
                                               → sleep_list에서 A 제거
                                               → thread_unblock(A) → A READY
   (스케줄러가 A를 고름)
   thread_block() 다음 줄에서 재개 ◀──────
   intr_set_level(old_level) → 인터럽트 ON
   timer_sleep() 반환
```

잠든 49틱 동안 A는 **한 번도 실행되지 않는다**.

### 데이터 구조 관점

```
            timer_sleep()                       timer_interrupt()
RUNNING ───────────────────▶ BLOCKED ─────────────────────────────▶ READY ──▶ RUNNING
 (CPU)    wakeup_tick 기록    (sleep_list,                         (ready_list)  (스케줄러가 선택)
          sleep_list 삽입      CPU 사용 0)      시각 도달 시 제거
          thread_block()                        thread_unblock()
```

---

## 10. 최종 변경 사항 (diff)

### `include/threads/thread.h`

```diff
 	char name[16];                      /* 이름 (디버깅 용도). */
 	int priority;                       /* 우선순위. */

+	int64_t wakeup_tick;                 /* timer_sleep()으로 잠들었을 때 깨어날 시각(틱) */
+
 	/* thread.c와 synch.c가 공유한다. */
 	struct list_elem elem;              /* 리스트 원소. */
```

### `devices/timer.c`

```diff
 /* OS 부팅 이후 지난 타이머 틱 수. */
 static int64_t ticks;

+// timer_sleep()으로 잠든 스레드들의 리스트.
+// wakeup_tick이 작은(먼저 깨어날) 스레드가 앞에 오도록 정렬되어 있음
+static struct list sleep_list;
+
 ...
 static void real_time_sleep (int64_t num, int32_t denom);
+
+static bool wakeup_tick_less (const struct list_elem *a, const struct list_elem *b, void *aux);

 ...
 	outb (0x40, count >> 8);

+	list_init(&sleep_list); // sleep_list 초기화
+
 	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
 }

+// sleep_list 정렬용 비교 함수. A의 깨어날 시각이 B보다 빠르면 true를 반환한다.
+static bool
+wakeup_tick_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
+	const struct thread *ta = list_entry (a, struct thread, elem);
+	const struct thread *tb = list_entry (b, struct thread, elem);
+
+	return ta->wakeup_tick < tb->wakeup_tick;
+}

 void
 timer_sleep (int64_t ticks) {
 	int64_t start = timer_ticks ();
+	struct thread *cur;
+	enum intr_level old_level;

 	ASSERT (intr_get_level () == INTR_ON);
-	while (timer_elapsed (start) < ticks)
-		thread_yield ();
+
+	if (ticks <= 0)
+		return;
+
+	cur = thread_current ();
+	old_level = intr_disable ();
+
+	cur->wakeup_tick = start + ticks;
+	list_insert_ordered (&sleep_list, &cur->elem, wakeup_tick_less, NULL);
+	thread_block ();
+
+	intr_set_level (old_level);
 }

 static void
 timer_interrupt (struct intr_frame *args UNUSED) {
 	ticks++;
+
+	while (!list_empty (&sleep_list)) {
+		struct thread *t = list_entry (list_front (&sleep_list), struct thread, elem);
+
+		if (t->wakeup_tick > ticks)
+			break;
+
+		list_pop_front (&sleep_list);
+		thread_unblock (t);
+	}
+
 	thread_tick ();
 }
```

### 정리하면 좋은 부분 (선택)

동작에는 영향이 없지만, 코드를 깔끔하게 유지하려면:

- 주석 처리해 둔 기존 `timer_sleep()`, `timer_interrupt()` 코드는 지워도 된다. 원본은 git 기록에서 언제든 볼 수 있다.
  ```bash
  git show 4bc6474:pintos/devices/timer.c
  ```
- 원형 선언 옆의 `// ... 근데 왜 bool?` 같은 질문 주석은 답을 알았으니 설명 주석으로 바꾸거나 지운다.
- `timer_interrupt()` 안의 `break;`는 `if`보다 한 단계 더 들여쓴다 (`if`에 속한 문장임이 보이도록).
- 함수 끝 `}` 앞의 불필요한 빈 줄 제거.

---

## 11. 빌드와 테스트

### 11-1. 실행 위치

`make`, `pintos`, `gdb`는 **반드시 Docker 컨테이너 안**에서 실행한다.

| 프롬프트 | 위치 | 할 수 있는 일 |
|----------|------|---------------|
| `sihoo@omen-arch ~/Projects/pintos_lab` | 호스트 | 코드 편집, git, `docker` 명령 |
| `jungle@<컨테이너ID>:/workspaces/...` | 컨테이너 | `make`, `pintos`, `gdb` |

호스트에서 `pintos`를 실행하면 `command not found: pintos`가 나온다.

```bash
# 호스트에서 컨테이너 접속
docker exec -it pintos bash
# (컨테이너가 꺼져 있으면) docker start -ai pintos
```

### 11-2. 빌드

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads
make
```

### 11-3. 테스트 하나 실행

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads/build
pintos -- -q run alarm-multiple
```

- `--` 앞은 pintos 스크립트 옵션, 뒤는 커널에 넘기는 인자다. `--`를 빼면 `usage:` 오류가 난다.
- `-q`는 테스트가 끝나면 자동으로 전원을 끈다. 빼면 QEMU가 대기 상태로 남는다 (`Ctrl+A` → `X`로 종료).
- 반드시 `build` 폴더에서 실행한다 (`os.dsk`가 거기 있다).

### 11-4. 테스트 하나 채점

```bash
make tests/threads/alarm-multiple.result
```

다시 채점하려면 결과 파일을 먼저 지운다.

```bash
rm -f tests/threads/alarm-multiple.result tests/threads/alarm-multiple.output
```

### 11-5. 알람 테스트 6개 채점

```bash
rm -f tests/threads/alarm-*.result tests/threads/alarm-*.output
for t in alarm-single alarm-multiple alarm-simultaneous alarm-priority alarm-zero alarm-negative; do make -s tests/threads/$t.result; done
```

### 11-6. 전체 테스트

```bash
make check
```

QEMU가 KVM 가속 없이 소프트웨어 에뮬레이션으로 동작하고 테스트마다 최대 60초가 걸려서 **CPU 부하와 발열이 크다**. 필요할 때만 실행하고, `make -j`(병렬)는 쓰지 않는다. 컨테이너 CPU 제한이 필요하면 호스트에서:

```bash
docker update --cpus 2 pintos
```

---

## 12. 테스트 결과와 해석

### 12-1. `alarm-multiple` 실행 결과

```
(alarm-multiple) thread 0: duration=10, iteration=1, product=10
(alarm-multiple) thread 1: duration=20, iteration=1, product=20
(alarm-multiple) thread 0: duration=10, iteration=2, product=20
...
(alarm-multiple) thread 4: duration=50, iteration=7, product=350
(alarm-multiple) end
Execution of 'alarm-multiple' complete.
Timer: 590 ticks
Thread: 550 idle ticks, 40 kernel ticks, 0 user ticks
```

**① 정확성**: `product`(반복 횟수 × 잠든 시간)가 한 번도 줄어들지 않는다.

```
10, 20, 20, 30, 30, 40, 40, 40, 50, 50, 60, 60, 60, 70, 80, 80, 90, 100, 100,
120, 120, 120, 140, 150, 150, 160, 180, 200, 200, 210, 240, 250, 280, 300, 350
```

→ 모든 스레드가 정해진 시각에 맞게 깨어났다.

**② 효율**:

| 항목 | 의미 |
|------|------|
| `Timer: 590 ticks` | 테스트 동안 흐른 전체 시간 (5.9초) |
| `550 idle ticks` | CPU가 할 일이 없어 쉰 시간 (약 93%) |
| `40 kernel ticks` | 실제로 스레드가 일한 시간 |

구현 전에는 잠든 스레드들이 계속 READY 상태로 `ready_list`에 있어서 idle 스레드가 실행될 틈이 거의 없었다. 이제 잠든 스레드는 CPU를 쓰지 않는다.

### 12-2. 알람 테스트 6개 결과

| 테스트 | 확인하는 것 | 결과 |
|--------|-------------|------|
| `alarm-single` | 스레드 5개가 각각 한 번씩 잠들고 깨어나기 | ✅ pass |
| `alarm-multiple` | 여러 스레드가 여러 번 반복 | ✅ pass |
| `alarm-simultaneous` | 여러 스레드가 **같은 틱에** 깨어나기 (`while`) | ✅ pass |
| `alarm-zero` | `timer_sleep(0)` (`ticks <= 0`) | ✅ pass |
| `alarm-negative` | `timer_sleep(-100)` (`ticks <= 0`) | ✅ pass |
| `alarm-priority` | 같은 시각에 깨어난 스레드들이 **우선순위 순서로** 실행되기 | ❌ FAIL (예상됨) |

### 12-3. `alarm-priority`가 실패하는 이유

**알람 시계가 틀린 것이 아니다.** 아직 구현하지 않은 **우선순위 스케줄링**이 필요한 테스트다.

`tests/threads/alarm-priority.c`:

```c
  wake_time = timer_ticks () + 5 * TIMER_FREQ;     // 모두 같은 시각에 깨어나도록
  for (i = 0; i < 10; i++)
    {
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1;
      ...
      thread_create (name, priority, alarm_priority_thread, NULL);
    }
```

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| 우선순위 | 25 | 24 | 23 | 22 | 21 | 30 | 29 | 28 | 27 | 26 |

1. 생성 순서(25, 24, 23, 22, 21, 30, 29, 28, 27, 26)대로 잠든다.
2. 깨어날 시각이 모두 같으므로 `sleep_list`에서는 **먼저 잠든 순서**가 유지된다 (`<` 비교).
3. `timer_interrupt()`가 같은 틱에 10개를 차례로 `thread_unblock()` → `ready_list` 맨 뒤에 차례로 들어간다.
4. `next_thread_to_run()`이 우선순위를 보지 않고 **맨 앞부터** 꺼낸다.
5. 출력 순서 = 25, 24, 23, 22, 21, 30, 29, 28, 27, 26 → **실제 출력과 정확히 일치**한다.

diff 읽는 법:

```
  (alarm-priority) begin                         ← 공백: 기대값과 같은 줄
- (alarm-priority) Thread priority 30 woke up.   ← '-': 기대값엔 여기 있는데 실제론 없음
  (alarm-priority) Thread priority 25 woke up.
+ (alarm-priority) Thread priority 30 woke up.   ← '+': 실제로는 여기에 나옴
```

모든 스레드가 깨어났고 빠진 것도 없다. **순서만** 우선순위대로가 아니다. 다음 과제인 우선순위 스케줄링을 구현하면 통과한다.

---

## 13. 디버깅 가이드

### 13-1. 오류 종류별 도구

| 오류 종류 | 언제 | 도구 |
|-----------|------|------|
| **컴파일 오류** | `make` 도중. `os.dsk`가 안 만들어짐 | **gcc 오류 메시지** |
| **실행 중 오류** | 빌드는 되지만 멈춤/패닉/결과 틀림 | **gdb**, `printf`, `backtrace` |

gdb는 **이미 만들어진 프로그램**을 실행하며 들여다보는 도구라서, `make`가 실패하면 쓸 수 없다.

### 13-2. 컴파일 오류 읽는 법

```
파일:행:열: error: 설명
```

1. **첫 번째 오류부터** 본다. 첫 오류가 뒤의 오류들을 만드는 경우가 많다.
2. `In function '...'` 줄로 **어느 함수 안**에서 발견됐는지 본다.
3. 오류가 가리킨 줄이 원인이 아닐 수 있다. 원인은 보통 **더 위쪽**에 있다 (특히 중괄호, 세미콜론).

오류만 걸러 보기:

```bash
make 2>&1 | grep -n "error"
```

#### 실제 사례: `{{` (중괄호 중복)

```c
timer_interrupt (struct intr_frame *args UNUSED) {{
```

```
devices/timer.c: In function 'timer_interrupt':
devices/timer.c:201:1: error: invalid storage class for function 'too_many_loops'
devices/timer.c:224:1: error: invalid storage class for function 'busy_wait'
devices/timer.c:231:1: error: invalid storage class for function 'real_time_sleep'
devices/timer.c:253:1: error: expected declaration or statement at end of input
```

- `{`가 하나 남아서 `timer_interrupt`가 끝나지 않은 것으로 해석되었다.
- 그 아래 함수들이 `timer_interrupt` **안에** 정의된 것처럼 보여서 `invalid storage class`(함수 안에 `static` 함수 정의 불가)가 났다.
- `expected declaration or statement at end of input`은 **중괄호 짝이 맞지 않을 때** 나오는 전형적인 메시지다.
- 오류는 201~253행에서 났지만 원인은 그보다 위의 `timer_interrupt` 첫 줄이었다.

### 13-3. gdb로 커널 디버깅

터미널 두 개를 쓴다. `utils/pintos`의 `--gdb` 옵션은 QEMU에 `-s -S`(1234 포트로 gdb 대기, 시작 즉시 정지)를 붙인다.

**터미널 1** (QEMU를 gdb 대기 상태로):

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads/build
pintos --gdb -- -q run alarm-multiple
```

**터미널 2** (호스트에서 `docker exec -it pintos bash`로 접속 후):

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads/build
gdb kernel.o
```

```
(gdb) target remote localhost:1234
(gdb) b timer_sleep
(gdb) c
(gdb) n
(gdb) p start
(gdb) p ticks                 # timer_sleep 안에서는 "매개변수" ticks가 출력됨 (가림)
(gdb) p cur->wakeup_tick
(gdb) p sleep_list
(gdb) bt
```

| 명령 | 줄임 | 하는 일 |
|------|------|---------|
| `break 함수` / `break 파일:행` | `b` | 중단점 설정 |
| `continue` | `c` | 다음 중단점까지 실행 |
| `next` | `n` | 한 줄 실행 (함수 안으로 안 들어감) |
| `step` | `s` | 한 줄 실행 (함수 안으로 들어감) |
| `print 식` | `p` | 값 출력 |
| `backtrace` | `bt` | 호출 경로 출력 |
| `info breakpoints` | `i b` | 중단점 목록 |
| `delete 번호` | `d` | 중단점 삭제 |
| `quit` | `q` | 종료 |

빈 리스트인지 확인: `p sleep_list`의 `head.next`가 `p &sleep_list.tail`과 같으면 비어 있다.

### 13-4. 커널 패닉 시 `backtrace`

패닉 메시지의 `Call stack: 0x... 0x...` 주소를 함수 이름과 줄 번호로 바꾼다 (`build` 폴더에서):

```bash
backtrace kernel.o 0x8004201234 0x8004205678
```

### 13-5. `printf` 디버깅 주의

- `timer_interrupt()` 같은 **인터럽트 핸들러 안**에서 `printf`를 많이 쓰면 출력이 느려 타이밍이 깨지고 테스트 결과가 달라질 수 있다.
- `schedule()`, `thread_launch()` 같은 문맥 교환 도중에는 `printf`가 안전하지 않다.

---

## 14. 자주 하는 실수

| 실수 | 증상 | 원인 / 해결 |
|------|------|-------------|
| `list_init(&sleep_list)` 누락 | 부팅 직후 또는 첫 `timer_sleep`에서 패닉/멈춤 | 초기화 안 된 리스트의 쓰레기 포인터 사용 |
| `timer_sleep()`에서 인터럽트를 끄지 않음 | `thread_block()`의 `ASSERT (intr_get_level () == INTR_OFF)` 실패 | `intr_disable()` 후 삽입·블록 |
| 인터럽트를 끄기 **전에** 리스트에 삽입 | 가끔 리스트가 깨지거나 `thread_unblock`의 `ASSERT(status == BLOCKED)` 실패 | 삽입과 블록을 인터럽트가 꺼진 한 덩어리로 |
| `thread_unblock()` 후 `list_pop_front()` | 리스트 손상, 무작위 패닉 | **먼저 제거, 그다음 깨우기** |
| `while` 대신 `if`로 깨우기 | `alarm-simultaneous` 실패 (같은 틱 스레드가 늦게 깨어남) | 같은 틱 스레드를 모두 깨우도록 `while` |
| `list_empty()` 검사 없이 `list_front()` | 잠든 스레드가 없을 때 패닉 | `while (!list_empty(...))` |
| 비교 함수에 `<=` 사용 | 같은 시각 스레드가 새치기 | `<` 사용 |
| `ticks <= 0` 처리 누락 | `alarm-negative` 등에서 불필요한 잠들기 | 바로 `return` |
| `timer_sleep()` 안에서 `ticks`를 현재 시각으로 착각 | 깨어날 시각 계산 오류 | 매개변수가 전역 변수를 가림. `timer_ticks()` 사용 |
| `timer_interrupt()`에서 `thread_block()`/`thread_yield()`/락 사용 | `ASSERT (!intr_context ())` 실패 | 인터럽트 핸들러에서는 `thread_unblock()`만 |
| `struct thread`의 `magic` 뒤에 필드 추가 | 스택 오버플로 감지 실패 | 새 필드는 `magic` **앞**에 |
| 중괄호 짝 불일치 (`{{`) | `invalid storage class`, `expected ... at end of input` | 중괄호 짝 확인 |
| 호스트에서 `pintos` 실행 | `command not found: pintos` | 컨테이너 안에서 실행 |
| `pintos run ...` (`--` 누락) | `usage: pintos ...` | `pintos -- -q run ...` |
| `threads/`에서 `pintos` 실행 | `os.dsk cannot be temporal.` | `threads/build/`에서 실행 |
| `pintos/utils` 실행 권한 없음 | `which pintos` 결과 없음 | `chmod +x pintos/utils/*` |

---

## 15. 다른 설계 방법과 비교

| 방법 | 재우기 | 깨우기 (매 틱) | 장점 | 단점 |
|------|--------|----------------|------|------|
| **정렬 리스트 (이번 구현)** | O(n) 정렬 삽입 | 대부분 O(1) — 맨 앞만 비교 | 인터럽트 핸들러가 매우 짧다 | 삽입 시 리스트를 훑는다 |
| 정렬 없는 리스트 | O(1) `list_push_back` | **O(n)** — 매 틱 전체 순회 | 삽입이 단순하다 | 잠든 스레드가 많으면 매 틱 비용이 크다 |
| 스레드별 세마포어 | `sema_down` | 시간 된 스레드에 `sema_up` | 동기화 도구를 재사용 | 결국 "누가 언제 깨어날지" 리스트가 또 필요하다 |
| 전역 "다음 깨울 시각" 변수 추가 | 정렬 삽입 + 최솟값 갱신 | `ticks < next_wakeup`이면 바로 반환 | 리스트 접근조차 생략 | 값 동기화 관리가 추가로 필요 |

인터럽트 핸들러는 **1초에 100번** 실행되고, 실행되는 동안 다른 모든 일이 멈춘다. 반면 `timer_sleep()`은 훨씬 드물게 호출된다. 그래서 **자주 실행되는 쪽(깨우기)을 가볍게, 드물게 실행되는 쪽(재우기)이 일을 더 하도록** 정렬 리스트를 택했다.

### `timer_msleep()` 등과의 관계

`real_time_sleep()`은 시간을 틱으로 변환한다.

```c
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 한 타이머 틱 전체를 기다려야 한다.
		   timer_sleep()을 사용하면 CPU를 다른 프로세스에게
		   양보하므로 이를 사용한다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면, 틱보다 짧은 시간을 더 정확하게 재기 위해
		   바쁜 대기 루프를 사용한다. 오버플로 가능성을 피하기 위해
		   분자와 분모를 1000으로 나누어 축소한다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
```

- 1틱(10ms) 이상이면 `timer_sleep()`을 호출하므로 **이번 구현으로 함께 개선**된다.
- 1틱보다 짧은 대기(`timer_usleep(10)`, `timer_nsleep(400)` 등)는 틱 단위로 잴 수 없어서 **의도적으로 바쁜 대기**를 유지한다. 과제 범위가 아니며 수정할 필요가 없다.

---

## 16. C 문법 정리

| 문법 | 예 | 의미 |
|------|-----|------|
| 변수 선언 | `int64_t start;` | `타입 이름;` |
| 선언과 초기화 | `int64_t start = timer_ticks ();` | 만들면서 값 넣기 |
| 구조체 | `struct thread { ... };` | 여러 변수를 묶은 사용자 정의 타입 |
| 포인터 선언 | `struct thread *cur;` | 주소를 담는 변수 |
| 주소 연산자 | `&sleep_list` | 변수의 메모리 주소 |
| `.` | `sleep_list.head` | **구조체 변수**의 멤버 |
| `->` | `cur->wakeup_tick` | **포인터가 가리키는 구조체**의 멤버. `(*cur).wakeup_tick`과 같다 |
| `const` | `const struct thread *ta` | 가리키는 내용을 바꾸지 않겠다는 약속 |
| `void *` | `void *aux` | 타입이 정해지지 않은 주소 |
| `NULL` | `NULL` | 아무것도 가리키지 않는 주소 |
| `static` (함수 밖 변수) | `static struct list sleep_list;` | 이 파일 안에서만 보이는 전역 변수 |
| `static` (함수) | `static bool wakeup_tick_less (...)` | 이 파일 안에서만 쓰는 함수 |
| 함수 원형 | `static bool f (int a);` | 함수가 존재한다는 약속 (본문 없음) |
| 함수 포인터 전달 | `list_insert_ordered (..., wakeup_tick_less, NULL)` | 함수 이름 = 함수 주소 |
| `bool` | `true` / `false` | 참/거짓 (`<stdbool.h>`) |
| `enum` | `enum intr_level { INTR_OFF, INTR_ON };` | 정해진 이름 중 하나만 갖는 타입 |
| `typedef` | `typedef bool list_less_func (...);` | 타입에 새 이름 붙이기 |
| `#define` 매크로 | `#define TIMER_FREQ 100` | 컴파일 전 글자 바꿔치기 |
| `#include` | `#include "threads/thread.h"` | 다른 파일 내용을 그대로 복사해 옴 |
| `if` | `if (ticks <= 0) return;` | 조건이 참일 때만 실행 |
| `while` | `while (!list_empty (...)) { ... }` | 조건이 참인 동안 반복 |
| `break` | `break;` | 가장 가까운 반복문 즉시 탈출 |
| `return` | `return;` / `return a < b;` | 함수 종료 (값 반환) |
| `!` | `!list_empty (...)` | 논리 부정 |
| `==` vs `=` | `a == b` / `a = b` | 비교 / 대입 |
| `++` | `ticks++` | 1 증가 |
| 삼항 연산자 | `c ? A : B` | 조건이 참이면 A, 아니면 B |
| 비교식의 값 | `a < b` | 그 자체가 참/거짓 값 |
| 이름 가림 | 매개변수 `ticks` vs 전역 `ticks` | 안쪽 이름이 바깥쪽을 가린다 |
| 주석 | `/* ... */`, `// ...` | 컴파일러가 무시 |

### 포인터 비유

포인터는 값 자체가 아니라 **값이 저장된 위치(주소)**를 담는다. "책 내용을 통째로 복사해서 건네는 것" 대신 "책이 꽂힌 책장 위치를 알려 주는 것"이다. 큰 구조체를 빠르게 넘길 수 있고, 원본을 직접 수정할 수 있다.

### Pintos 코드 스타일

```c
static bool                 ← 반환형을 한 줄에
wakeup_tick_less (...) {    ← 함수 이름을 다음 줄에
	...                     ← 들여쓰기는 탭
}
```

---

## 17. Q&A 모음

**Q. `wakeup_tick`은 정확히 어떤 역할인가?**
A. 스레드가 몇 번째 틱에 깨어나야 하는지 적어 두는 메모. 잠든 스레드는 스스로 시간을 확인할 수 없으므로, 깨워 주는 쪽(타이머 인터럽트)이 볼 수 있는 스레드 구조체 안에 기록한다.

**Q. 깨어날 시각은 무엇으로 정해지나?**
A. `잠드는 순간의 현재 시각(timer_ticks()) + 잠들 시간(timer_sleep의 인자)`. 잠들 시간은 `timer_sleep()`을 호출하는 프로그램이 정한다.

**Q. 스레드는 왜 정해진 시간 동안 잠드나?**
A. 운영체제가 아니라 호출하는 프로그램의 목적 때문이다. 장치가 준비될 때까지(디스크 리셋 150ms), 주기적 작업(커서 깜빡임), 재시도 간격 등 **"끝났다는 신호가 없거나, 시간 자체가 조건인"** 경우에 쓴다. 신호가 있는 작업(디스크 읽기 완료, 키 입력)은 세마포어 등으로 신호가 오는 즉시 깨어난다.

**Q. 처리량을 높이려면 idle을 없애야 하지 않나?**
A. 잠드는 것은 idle을 만드는 것이 아니라 CPU를 **양보**하는 것이다. 다른 일이 있으면 그 일이 실행되고, 정말 할 일이 없을 때만 운영체제가 알고서 idle 스레드를 실행한다. 바쁜 대기는 쓸모없는 확인 작업으로 CPU를 채워 오히려 처리량과 반응성을 떨어뜨린다.

**Q. `sleep_list`는 FILO인가?**
A. 아니다. 깨어날 시각 순으로 정렬된 리스트다. 같은 시각끼리는 FIFO.

**Q. 비교 함수는 왜 `bool`을 반환하나?**
A. "A가 B보다 앞에 와야 하나?"라는 예/아니요 질문에 답하는 함수이고, `list_insert_ordered()`가 그 값을 `if`에서 쓰기 때문이다. 모양은 `list_less_func`로 정해져 있다.

**Q. 인터럽트 핸들러에서는 왜 인터럽트를 끄지 않나?**
A. 외부 인터럽트 핸들러는 이미 인터럽트가 꺼진 상태로 실행된다.

**Q. 잠든 동안 인터럽트가 꺼져 있으면 시간이 안 흐르지 않나?**
A. 인터럽트 상태는 스레드마다 저장·복원된다. 잠든 스레드 대신 실행되는 스레드의 인터럽트 상태가 적용되므로 타이머 인터럽트는 계속 들어온다.

**Q. `make check` 때 CPU 온도가 오르는 이유는?**
A. QEMU가 하드웨어 가속(KVM) 없이 소프트웨어로 CPU를 에뮬레이션하고(`utils/pintos`에서 `-enable-kvm` 주석 처리), 테스트 수십 개를 연달아 각 최대 60초씩 실행하기 때문이다.

---

## 18. 남은 과제와의 연결

### 우선순위 스케줄링

`alarm-priority`를 통과하려면 스케줄러가 우선순위를 보고 스레드를 골라야 한다. 이때 알람 시계 코드와 관련해 고려할 점:

- 현재 `next_thread_to_run()`은 `ready_list` 맨 앞을 꺼낸다(FIFO). 우선순위 스케줄링에서 이 동작이 바뀌면, 같은 틱에 깨어난 스레드들도 우선순위 순서로 실행된다.
- `thread_unblock()`은 **선점하지 않는다**. 타이머 인터럽트가 현재 스레드보다 우선순위가 높은 스레드를 깨웠을 때 언제 CPU를 넘길지는 우선순위 스케줄링 과제에서 다룬다. 인터럽트 핸들러 안에서는 `thread_yield()`를 직접 부를 수 없고, `intr_yield_on_return()`으로 "인터럽트가 끝날 때 양보"를 예약하는 방식이 있다는 점을 기억해 두자.

### Advanced Scheduler (MLFQS)

MLFQS는 `load_avg`, `recent_cpu`를 **특정 틱마다**(매 틱, 4틱마다, 1초마다) 갱신해야 한다. 이 갱신도 `timer_interrupt()` → `thread_tick()` 경로에서 이루어지므로, 이번에 수정한 타이머 인터럽트 핸들러가 다시 등장한다.

---

## 부록: 관련 파일 위치

| 파일 | 내용 |
|------|------|
| `devices/timer.c` | `timer_sleep()`, `timer_interrupt()`, `sleep_list` |
| `include/devices/timer.h` | `TIMER_FREQ`, 타이머 함수 선언 |
| `include/threads/thread.h` | `struct thread`, `wakeup_tick`, 스레드 상태 |
| `threads/thread.c` | `thread_block()`, `thread_unblock()`, 스케줄러 |
| `threads/interrupt.c` | `intr_disable()`, `intr_set_level()`, `intr_yield_on_return()` |
| `include/lib/kernel/list.h`, `lib/kernel/list.c` | 리스트 API, `list_entry`, `list_insert_ordered` |
| `threads/init.c` | 부팅 순서 (`timer_init` → `thread_start`) |
| `tests/threads/alarm-*.c`, `*.ck` | 알람 테스트 코드와 기대 출력 |
| `tests/threads/Rubric.alarm` | 알람 테스트 채점표 |
