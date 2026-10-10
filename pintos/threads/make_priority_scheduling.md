# Priority Scheduling 구현 상세서 (1부: 기본 스케줄링 + 동기화 도구)

> Pintos (KAIST x86-64) Project 1 — Threads
> 과제: 스케줄러가 **스레드의 우선순위를 보고** 실행할 스레드를 고르게 만든다.
> 이 문서는 전체 8단계 중 **1~4단계(Part A·B)**를 다룬다. 우선순위 기부(5~8단계, Part C)는 구현을 마친 뒤 이어서 추가한다.
> 커밋: `a1e5699 feat: priority scheduling in progress (steps 1-4)`

---

## 목차

0. [진행 현황](#0-진행-현황)
1. [과제 요약](#1-과제-요약)
2. [단계별 구현 한눈에 보기](#2-단계별-구현-한눈에-보기)
3. [배경 지식](#3-배경-지식)
4. [문제 분석: 기존 코드는 우선순위를 무시한다](#4-문제-분석-기존-코드는-우선순위를-무시한다)
5. [설계 원칙 세 가지](#5-설계-원칙-세-가지)
6. [1단계: ready_list를 우선순위 순으로 정렬해서 넣기](#6-1단계-ready_list를-우선순위-순으로-정렬해서-넣기)
7. [2단계: 즉시 선점 `thread_preempt()`](#7-2단계-즉시-선점-thread_preempt)
8. [3단계: 세마포어 대기자를 우선순위 순으로 깨우기](#8-3단계-세마포어-대기자를-우선순위-순으로-깨우기)
9. [4단계: 조건 변수 대기자를 우선순위 순으로 깨우기](#9-4단계-조건-변수-대기자를-우선순위-순으로-깨우기)
10. [전체 동작 흐름](#10-전체-동작-흐름)
11. [최종 변경 사항 (diff)](#11-최종-변경-사항-diff)
12. [빌드와 테스트](#12-빌드와-테스트)
13. [테스트별 동작 추적과 기대 출력](#13-테스트별-동작-추적과-기대-출력)
14. [디버깅 가이드](#14-디버깅-가이드)
15. [자주 하는 실수](#15-자주-하는-실수)
16. [다른 설계 방법과 비교](#16-다른-설계-방법과-비교)
17. [C 문법 정리](#17-c-문법-정리)
18. [Q&A 모음](#18-qa-모음)
19. [남은 단계: 우선순위 기부 (5~8단계)](#19-남은-단계-우선순위-기부-58단계)
- [부록: 관련 파일 위치](#부록-관련-파일-위치)

---

## 0. 진행 현황

| 단계 | 내용 | 상태 |
|------|------|------|
| 1단계 | 비교 함수 `thread_priority_greater()` + ready_list 정렬 삽입 | ✅ 완료 |
| 2단계 | 즉시 선점 `thread_preempt()` | ✅ 완료 |
| 3단계 | 세마포어 대기자 우선순위 순 깨우기 + 선점 | ✅ 완료 |
| 4단계 | 조건 변수 대기자 우선순위 순 깨우기 | ✅ 완료 |
| 5단계 | 기부용 필드 추가 (`init_priority`, `wait_on_lock`, `donations`, `donation_elem`) | ⬜ 예정 |
| 6단계 | `lock_acquire()`에서 기부 (중첩 기부 포함) | ⬜ 예정 |
| 7단계 | `lock_release()`에서 기부 회수 + 우선순위 재계산 | ⬜ 예정 |
| 8단계 | 기부 중 `thread_set_priority()` | ⬜ 예정 |

현재 통과해야 하는 테스트: alarm 6개 전부 + `priority-change`, `priority-preempt`, `priority-fifo`, `priority-sema`, `priority-condvar`.
`priority-donate-*` 7개는 Part C 이후에 통과한다.

---

## 1. 과제 요약

| 항목 | 내용 |
|------|------|
| 수정 대상 | `threads/thread.c`의 스케줄링 경로, `threads/synch.c`의 세마포어·조건 변수 |
| 문제 | 스레드마다 `priority`(0~63)가 있지만 스케줄러와 동기화 도구가 이를 **전혀 보지 않고** 선착순(FIFO)으로 동작함 |
| 목표 | ① 항상 우선순위가 가장 높은 READY 스레드가 실행된다 ② 더 높은 스레드가 READY가 되면 **즉시** CPU를 넘긴다 ③ 세마포어·락·조건 변수도 가장 높은 대기자부터 깨운다 ④ (Part C) 우선순위 역전을 기부로 해결한다 |
| 함수 사용법 | 바뀌지 않음 — `thread_create()`, `thread_set_priority()`, `sema_up()` 등 시그니처 그대로 |

### 지켜야 할 조건

1. 우선순위 범위는 `PRI_MIN`(0) ~ `PRI_MAX`(63), 기본값 `PRI_DEFAULT`(31). 숫자가 **클수록** 높다.
2. 실행 중인 스레드보다 높은 스레드가 READY가 되면 **즉시** 양보한다. 타임 슬라이스가 끝날 때까지 기다리면 안 된다.
3. 우선순위가 **같은** 스레드끼리는 기존처럼 라운드 로빈(먼저 온 순서)으로 돈다.
4. 세마포어·락·조건 변수에서 기다리는 스레드가 여럿이면 **가장 높은 스레드**를 먼저 깨운다.
5. 인터럽트 핸들러 안에서는 `thread_yield()`를 호출할 수 없다.
6. `thread_mlfqs`가 true인 경우(Advanced Scheduler)는 다음 과제다. 이번에는 신경 쓰지 않는다.

### 수정한 파일 (1~4단계)

| 파일 | 변경 |
|------|------|
| `include/threads/thread.h` | `thread_priority_greater()`, `thread_preempt()` 원형 추가 |
| `threads/thread.c` | 두 함수 정의, `thread_unblock()`·`thread_yield()` 정렬 삽입, `thread_create()`·`thread_set_priority()`에 선점 검사 |
| `threads/synch.c` | `sema_down()` 정렬 삽입, `sema_up()` 재정렬 + 선점, `semaphore_elem`에 `thread` 필드, `sema_elem_priority_greater()`, `cond_wait()`·`cond_signal()` 수정 |

---

## 2. 단계별 구현 한눈에 보기

Priority Scheduling의 가장 큰 특징은 **작은 기능을 하나씩 쌓아 올리고, 단계마다 특정 테스트로 검증**한다는 점이다. 각 단계는 앞 단계가 만든 도구(비교 함수, 선점 함수)를 재사용한다.

### 2-1. 구현 기능 ↔ 테스트 ↔ 검증 내용 ↔ PASS 조건

| 구현 기능 | 관련 테스트 | 검증 내용 | 예상 PASS 조건 |
|-----------|-------------|-----------|----------------|
| Alarm Clock + Priority | `alarm-priority` | 같은 시각에 깨어난 스레드들이 우선순위 순으로 실행되는가 | Alarm Clock + **ready_list 우선순위 정렬** (1단계) |
| 동일 우선순위 FIFO | `priority-fifo` | 같은 우선순위 스레드 16개의 실행 순서가 매 반복마다 똑같은가 | 비교 함수가 **엄격한 `>`** 라서 FIFO 유지 (1단계) + `set_priority` 선점 (2단계) |
| 기본 우선순위 선점 | `priority-preempt` | 더 높은 스레드를 **생성**하면 즉시 그 스레드가 실행되는가 | ready_list 정렬 (1단계) + `thread_create()` 선점 (2단계) |
| 우선순위 변경 | `priority-change` | 실행 중 자기 우선순위를 **낮추면** 즉시 CPU를 양보하는가 | `thread_set_priority()` 선점 (2단계) |
| Semaphore | `priority-sema` | `sema_up()`이 가장 높은 대기자를 깨우고, 깨운 스레드가 즉시 실행되는가 | 세마포어 대기자 우선순위 선택 + `sema_up` 선점 (3단계) |
| Condition Variable | `priority-condvar` | `cond_signal()`이 가장 높은 대기자를 깨우는가 | `cond->waiters` 우선순위 정렬 (4단계) + 락 해제 시 선점 (3단계) |
| 단일 기부 | `priority-donate-one` | 락을 기다리는 높은 스레드가 락 주인에게 우선순위를 빌려주는가 | 6·7단계 |
| 다중 기부 | `priority-donate-multiple`, `-multiple2` | 락 여러 개로 받은 기부 중 최댓값을 쓰고, 락 하나를 풀면 그 락의 기부만 사라지는가 | 6·7단계 |
| 중첩 기부 | `priority-donate-nest`, `-chain` | H→M→L 사슬을 따라 기부가 전달되는가 (깊이 8) | 6단계 |
| 기부 + 세마포어 | `priority-donate-sema` | 락과 세마포어가 섞여도 기부·깨움 순서가 맞는가 | 3·6·7단계 |
| 기부 중 우선순위 변경 | `priority-donate-lower` | 기부 받는 중 `set_priority`로 낮춰도 실제 우선순위는 유지되는가 | 8단계 |

### 2-2. 단계 로드맵

```
Part A ─ 기본 우선순위 스케줄링
  1단계  thread_priority_greater() + ready_list 정렬 삽입 ──▶ alarm-priority
  2단계  thread_preempt() (생성 / set_priority) ───────────▶ priority-change, -preempt, -fifo
                     │
                     │ 비교 함수와 선점 함수를 그대로 재사용
                     ▼
Part B ─ 동기화 도구의 대기 순서
  3단계  sema_down 정렬 삽입, sema_up 재정렬 + 선점 ────────▶ priority-sema
         (락은 세마포어로 만들어져 있어서 자동으로 함께 해결)
  4단계  semaphore_elem->thread + cond_signal 정렬 ────────▶ priority-condvar
                     │
                     │ "꺼낼 때 다시 정렬"이 기부를 대비한 장치
                     ▼
Part C ─ 우선순위 기부 (예정)
  5단계  struct thread에 기부용 필드
  6단계  lock_acquire: 기부 (중첩)                ─────────▶ donate-one, -nest, -chain
  7단계  lock_release: 기부 회수 + 재계산         ─────────▶ donate-multiple, -multiple2, -sema
  8단계  기부 중 set_priority                     ─────────▶ donate-lower
```

### 2-3. 단계별 수정 위치 요약

| 단계 | 파일 | 함수/위치 | 바뀐 것 |
|------|------|-----------|---------|
| 1 | `thread.c` | `thread_priority_greater()` (신규) | 우선순위 비교 함수 |
| 1 | `thread.h` | 원형 | 다른 파일(synch.c)에서도 쓰도록 공개 |
| 1 | `thread.c` | `thread_unblock()` | `list_push_back` → `list_insert_ordered` |
| 1 | `thread.c` | `thread_yield()` | `list_push_back` → `list_insert_ordered` |
| 2 | `thread.c` | `thread_preempt()` (신규) | ready_list 맨 앞이 더 높으면 양보 |
| 2 | `thread.c` | `thread_create()` | `thread_unblock()` 뒤에 `thread_preempt()` |
| 2 | `thread.c` | `thread_set_priority()` | 값 변경 뒤에 `thread_preempt()` |
| 3 | `synch.c` | `sema_down()` | `list_push_back` → `list_insert_ordered` |
| 3 | `synch.c` | `sema_up()` | `list_sort` 후 깨우기, `value++` 뒤 `thread_preempt()` |
| 4 | `synch.c` | `struct semaphore_elem` | `struct thread *thread` 필드 |
| 4 | `synch.c` | `sema_elem_priority_greater()` (신규) | semaphore_elem 비교 함수 |
| 4 | `synch.c` | `cond_wait()` | `waiter.thread = thread_current ()` |
| 4 | `synch.c` | `cond_signal()` | `list_sort` 후 깨우기 |

---

## 3. 배경 지식

### 3-1. 우선순위 상수

`include/threads/thread.h:27-29`:

```c
#define PRI_MIN 0                       /* 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. */
#define PRI_MAX 63                      /* 가장 높은 우선순위. */
```

- `main` 스레드는 `PRI_DEFAULT`(31)로 시작한다 (`thread_init()` → `init_thread (initial_thread, "main", PRI_DEFAULT)`).
- `idle` 스레드는 `PRI_MIN`(0)이다 (`thread_start()` → `thread_create ("idle", PRI_MIN, ...)`).
- `init_thread()`는 범위를 `ASSERT (PRI_MIN <= priority && priority <= PRI_MAX)`로 검사한다.

### 3-2. 스케줄러의 동작 경로

```
thread_yield / thread_block / thread_exit
        │
        ▼
  do_schedule(status)  ── 현재 스레드 상태를 바꿈
        │
        ▼
    schedule()
        │
        ▼
 next_thread_to_run() ── ready_list 맨 앞을 꺼냄 (비어 있으면 idle)
        │
        ▼
 thread_launch(next) ── 문맥 전환
```

`threads/thread.c:455` `next_thread_to_run()`:

```c
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}
```

**맨 앞을 꺼낸다**는 점이 핵심이다. 그래서 "맨 앞에 무엇이 있느냐"만 바꾸면 스케줄링 정책이 바뀐다. 이번 구현은 이 함수를 **건드리지 않는다**.

### 3-3. ready_list에 스레드가 들어가는 곳 (딱 두 곳)

| 함수 | 상태 변화 | 언제 |
|------|-----------|------|
| `thread_unblock(t)` | BLOCKED → READY | 새 스레드 생성(`thread_create`), 알람 깨우기(`timer_interrupt`), 세마포어 깨우기(`sema_up`) |
| `thread_yield()` | RUNNING → READY | 스스로 양보, 타임 슬라이스 만료(`intr_yield_on_return` → 인터럽트 끝에서 `thread_yield`) |

이 두 곳에서 정렬 삽입을 하면 ready_list는 **항상 정렬된 상태**가 된다.

### 3-4. 선점(preemption)이란

**선점** = 실행 중인 스레드가 스스로 양보하지 않았는데도 CPU를 빼앗기는 것.

Pintos에는 원래 선점이 한 가지 있다. **타임 슬라이스 선점**이다.

```c
/* threads/thread.c:152 — thread_tick() */
	if (++thread_ticks >= TIME_SLICE)       // TIME_SLICE = 4틱
		intr_yield_on_return ();
```

이번 과제에서 **우선순위 선점**을 추가한다. "나보다 높은 스레드가 READY가 되는 순간" 양보한다.

| 선점 종류 | 언제 | 누가 처리 |
|-----------|------|-----------|
| 타임 슬라이스 | 4틱마다 | `thread_tick()` (원래 있음) |
| 우선순위 | 더 높은 스레드가 READY가 될 때 | `thread_preempt()` (이번에 추가) |

### 3-5. 인터럽트 문맥과 `intr_yield_on_return()`

인터럽트 핸들러(예: `timer_interrupt()`) 안에서는 **잠들거나 문맥 전환을 직접 할 수 없다**. 핸들러는 "지금 실행 중이던 스레드"를 잠깐 멈추고 끼어든 코드이기 때문이다.

```c
/* threads/thread.c — thread_yield() */
	ASSERT (!intr_context ());     // 핸들러 안이면 커널 패닉
```

대신 `threads/interrupt.c:265` `intr_yield_on_return()`으로 **"핸들러가 끝나면 양보해 줘"**라고 예약한다. 핸들러가 끝나는 지점(`intr_handler`의 끝)에서 이 플래그를 보고 `thread_yield()`를 호출한다.

| 함수 | 위치 | 의미 |
|------|------|------|
| `intr_context()` | `threads/interrupt.c:256` | 지금 외부 인터럽트 핸들러 안이면 true |
| `intr_yield_on_return()` | `threads/interrupt.c:265` | 핸들러 종료 시 양보 예약 |

### 3-6. 리스트 API (이번에 쓰는 것)

| 함수 | 위치 | 하는 일 | 비용 |
|------|------|---------|------|
| `list_insert_ordered(list, elem, less, aux)` | `lib/kernel/list.c:416` | `less(elem, e)`가 처음 true인 `e` **앞에** 삽입 | O(n) |
| `list_sort(list, less, aux)` | `lib/kernel/list.c:378` | 전체 정렬 (자연 병합 정렬, **안정 정렬**) | O(n log n) |
| `list_front(list)` | `lib/kernel/list.c:266` | 맨 앞을 **보기만** 함 (빈 리스트면 ASSERT) | O(1) |
| `list_pop_front(list)` | `lib/kernel/list.c:248` | 맨 앞을 **떼어내고** 반환 (빈 리스트면 ASSERT) | O(1) |
| `list_empty(list)` | `lib/kernel/list.c:293` | 비었으면 true | O(1) |
| `list_entry(elem, STRUCT, MEMBER)` | `include/lib/kernel/list.h:103` | elem 주소 → 바깥 구조체 주소 | O(1) |
| `list_less_func` | `include/lib/kernel/list.h:147` | 비교 함수의 타입: `bool (const elem*, const elem*, void*)` | — |

### 3-7. `elem` 하나를 여러 리스트가 공유하는 이유

`include/threads/thread.h:80-84` 주석:

> `elem` 멤버는 실행 큐(thread.c)의 원소도 되고, 세마포어 대기 리스트(synch.c)의 원소도 된다. 두 경우가 서로 배타적이기 때문이다.

| 스레드 상태 | `elem`이 들어 있는 리스트 |
|-------------|---------------------------|
| READY | `ready_list` |
| BLOCKED (세마포어/락) | `sema->waiters` |
| BLOCKED (알람) | `sleep_list` |
| RUNNING | 어디에도 없음 |

한 스레드는 동시에 한 상태만 가지므로 `elem` 하나로 충분하다. **5단계에서 추가할 `donation_elem`은 예외**다. 기부 목록에 들어 있는 스레드는 동시에 락의 `sema->waiters`에도 들어 있으므로 별도 elem이 필요하다.

### 3-8. 우선순위 역전 (Part C의 동기)

```
L(31): lock A 보유, 실행 중
H(33): lock A 필요 → BLOCKED (L이 풀 때까지)
M(32): 락과 무관, CPU 원함

스케줄러: M(32) > L(31) → M 실행
→ L이 못 돌아서 lock A가 안 풀림
→ 가장 높은 H가 M 때문에 계속 기다림   ← 우선순위 역전
```

해결: H가 L에게 33을 **기부**한다. L이 33으로 돌아 락을 빨리 풀고, 풀면 31로 돌아간다. 19장에서 자세히 다룬다.

---

## 4. 문제 분석: 기존 코드는 우선순위를 무시한다

| 위치 | 기존 코드 | 결과 |
|------|-----------|------|
| `thread.c` `thread_unblock()` | `list_push_back (&ready_list, &t->elem);` | READY 스레드가 맨 뒤에 줄 섬 |
| `thread.c` `thread_yield()` | `list_push_back (&ready_list, &curr->elem);` | 양보한 스레드도 맨 뒤 |
| `thread.c` `next_thread_to_run()` | `list_pop_front (&ready_list)` | 결국 **도착 순서(FIFO)** |
| `thread.c` `thread_create()` | `thread_unblock (t); return tid;` | 더 높은 스레드를 만들어도 계속 실행 |
| `thread.c` `thread_set_priority()` | `thread_current ()->priority = new_priority;` | 낮춰도 양보 안 함 |
| `synch.c` `sema_down()` | `list_push_back (&sema->waiters, ...)` | 대기자도 FIFO |
| `synch.c` `sema_up()` | `list_pop_front (&sema->waiters)` | 가장 오래 기다린 스레드를 깨움 |
| `synch.c` `cond_signal()` | `list_pop_front (&cond->waiters)` | 가장 오래 기다린 스레드를 깨움 |

`include/threads/thread.h`의 주석도 이 사실을 밝히고 있다:

> 실제 우선순위 스케줄링은 구현되어 있지 않다. 우선순위 스케줄링은 Problem 1-3의 목표이다.

---

## 5. 설계 원칙 세 가지

1~4단계 전체를 관통하는 원칙은 세 가지다.

### 원칙 ① 넣을 때 정렬, 꺼낼 때 맨 앞

```
삽입: list_insert_ordered(..., thread_priority_greater, NULL)   ← O(n)
선택: list_pop_front / list_front                               ← O(1)
```

- Alarm Clock의 `sleep_list`와 같은 패턴이다.
- `next_thread_to_run()`을 고치지 않아도 된다.
- 선점 검사(`thread_preempt`)가 맨 앞 하나만 보면 된다.

### 원칙 ② 대기 리스트는 "꺼내기 직전에 한 번 더 정렬"

```
sema_up:     list_sort(&sema->waiters, ...) → list_pop_front
cond_signal: list_sort(&cond->waiters, ...) → list_pop_front
```

- 정렬 삽입은 **넣는 순간의** 우선순위 기준이다.
- 기다리는 동안 기부(Part C)로 우선순위가 바뀌면 정렬이 깨진다.
- 깨울 때 다시 정렬하면 항상 **지금** 가장 높은 스레드를 깨운다.

### 원칙 ③ "누군가 READY가 되거나 내 우선순위가 바뀐 직후" 선점 검사

| 사건 | 선점 검사 위치 | 단계 |
|------|----------------|------|
| 새 스레드 생성 | `thread_create()` 끝 | 2 |
| 내 우선순위 변경 | `thread_set_priority()` 끝 | 2 |
| 세마포어/락으로 깨움 | `sema_up()` 끝 | 3 |
| 기부 회수로 내 우선순위 하락 | `lock_release()` (sema_up 경유) | 7 |

모든 검사는 **하나의 함수 `thread_preempt()`**로 한다. 인터럽트 문맥 처리도 이 함수 안에 한 번만 구현한다.

### 비교 함수 하나로 세 리스트를 정렬

```
thread_priority_greater ──┬── ready_list       (thread.c, 1단계)
                          └── sema->waiters    (synch.c, 3단계)
sema_elem_priority_greater ── cond->waiters    (synch.c, 4단계)
```

`ready_list`와 `sema->waiters`는 둘 다 `struct thread`의 `elem`을 담으므로 같은 비교 함수를 쓴다. `cond->waiters`는 `struct semaphore_elem`을 담으므로 별도 비교 함수가 필요하다.

---

## 6. 1단계: ready_list를 우선순위 순으로 정렬해서 넣기

### 6-1. 목표

`next_thread_to_run()`이 맨 앞을 꺼낼 때 그 스레드가 **항상 우선순위가 가장 높은 스레드**가 되게 한다.

### 6-2. 설계 선택

| 방법 | 넣을 때 | 꺼낼 때 | 채택 |
|------|---------|---------|------|
| A. 정렬 삽입 | `list_insert_ordered` O(n) | 맨 앞 O(1), 기존 코드 그대로 | ✅ |
| B. 꺼낼 때 탐색 | `list_push_back` O(1) | `list_max` + `list_remove` O(n) | |

A를 고른 이유:
1. Alarm Clock에서 써 본 패턴이다.
2. `next_thread_to_run()`을 고치지 않는다.
3. 같은 비교 함수를 3단계에서 재사용한다.
4. 2단계의 선점 검사가 맨 앞 하나만 보면 된다.

### 6-3. 코드

#### ① `threads/thread.c` — 비교 함수 (`thread_get_priority()` 아래, 현재 331행)

```c
/* A 스레드의 우선순위가 B보다 높으면 true를 반환한다.
   list_insert_ordered()에 넘기면 리스트가 우선순위 내림차순으로 정렬되고,
   우선순위가 같은 스레드끼리는 먼저 들어온 순서(FIFO)가 유지된다. */
bool
thread_priority_greater (const struct list_elem *a, const struct list_elem *b,
		void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);

	return ta->priority > tb->priority;
}
```

#### ② `include/threads/thread.h` — 원형 (137행)

```c
int thread_get_priority (void);
void thread_set_priority (int);
bool thread_priority_greater (const struct list_elem *a,
                              const struct list_elem *b, void *aux);
```

#### ③ `threads/thread.c` — `thread_unblock()` (243행)

```c
/* 변경 전 */
	list_push_back (&ready_list, &t->elem);

/* 변경 후 */
	list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL);
```

#### ④ `threads/thread.c` — `thread_yield()` (306행)

```c
/* 변경 전 */
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);

/* 변경 후 */
	if (curr != idle_thread)
		list_insert_ordered (&ready_list, &curr->elem, thread_priority_greater, NULL);
```

### 6-4. 핵심: 왜 `>`이면 "내림차순 + 같은 값은 FIFO"가 될까

`lib/kernel/list.c:424-427`:

```c
for (e = list_begin (list); e != list_end (list); e = list_next (e))
    if (less (elem, e, aux))   // "새 원소가 e보다 앞에 와야 하나?"
        break;
return list_insert (e, elem);  // e 바로 앞에 삽입
```

- 매개변수 이름은 `less`지만 실제 뜻은 **"앞에 와야 하면 true"**다.
- `>`를 넘기면 "우선순위가 더 높으면 앞으로" → 내림차순.

예: ready_list = `[33, 31a, 31b, 20]`에 `31c` 삽입

| e | `31c > e`? | 결과 |
|---|------------|------|
| 33 | 31 > 33 → false | 계속 |
| 31a | 31 > 31 → **false** | 계속 |
| 31b | 31 > 31 → **false** | 계속 |
| 20 | 31 > 20 → true | break, 20 앞에 삽입 |

→ `[33, 31a, 31b, 31c, 20]`. 같은 31끼리 **들어온 순서대로 뒤에 붙는다**.

`>=`를 쓰면 `31c`가 `31a` 앞에 새치기한다 → 같은 우선순위 라운드 로빈이 깨지고 `priority-fifo`가 실패한다.

### 6-5. 줄별 해설

| 코드 | 설명 |
|------|------|
| `bool` | `list_less_func` 모양에 맞춘 반환형. Pintos 스타일은 반환형을 윗줄에 쓴다 |
| `const struct list_elem *a` | a가 가리키는 내용을 **읽기만** 하겠다는 약속 |
| `void *aux UNUSED` | 추가 정보 통로. 안 쓰므로 `UNUSED`(`include/lib/debug.h:7`)로 경고를 끈다 |
| `static`이 없음 | `wakeup_tick_less`는 timer.c 전용이라 `static`이었다. 이 함수는 synch.c(3단계)도 써야 하므로 공개하고 원형을 thread.h에 둔다 |
| `list_entry (a, struct thread, elem)` | elem 주소 − (elem의 구조체 내 offset) = 스레드 구조체 주소 |
| `const struct thread *ta` | a가 const이므로 결과도 const로 받아 약속을 유지한다 |
| `return ta->priority > tb->priority;` | 비교식 자체가 참/거짓 값이므로 바로 반환한다 |
| `&ready_list` | 리스트를 수정해야 하므로 주소를 넘긴다 |
| `&t->elem` | `&(t->elem)` — 스레드 안 elem의 주소 |
| `thread_priority_greater` (괄호 없음) | **함수 포인터**. 괄호를 붙이면 그 자리에서 호출해 버린다 |
| `NULL` | aux 자리, 사용 안 함 |

### 6-6. 이 단계로 해결되는 것

- **`alarm-priority`**: 우선순위 21~30 스레드 10개가 같은 틱에 깨어난다. `timer_interrupt()`는 그대로 `thread_unblock (t)`을 호출하지만, `thread_unblock()` **내부**가 정렬 삽입으로 바뀌었으므로 30 → 21 순으로 실행된다. **Alarm Clock 코드를 한 줄도 고치지 않고** 통과한다.
- 아직 안 되는 것: 선점. 더 높은 스레드가 생겨도 ready_list 맨 앞에 서 있을 뿐, 타임 슬라이스(4틱)가 끝나야 실행된다.

### 6-7. 체크포인트

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads && make
cd build
for t in alarm-single alarm-multiple alarm-simultaneous alarm-priority alarm-zero alarm-negative; do make -s tests/threads/$t.result; done
```

결과: **alarm 6/6 PASS** (Alarm Clock만 했을 때는 `alarm-priority`가 FAIL이라 5/6이었다).

---

## 7. 2단계: 즉시 선점 `thread_preempt()`

### 7-1. 목표

실행 중인 스레드보다 높은 스레드가 READY가 되면 **즉시** CPU를 넘긴다.

### 7-2. 설계

선점 검사가 필요한 곳이 여러 곳(생성, 우선순위 변경, sema_up, lock_release)이므로 **공통 함수 하나**로 만든다.

```
thread_preempt():
    인터럽트 끄기
    ready_list가 비어 있지 않고
    맨 앞의 우선순위 > 현재 스레드 우선순위 이면
        인터럽트 핸들러 안이면  intr_yield_on_return()
        아니면                  thread_yield()
    인터럽트 상태 복원
```

- ready_list가 정렬되어 있으므로 **맨 앞 하나만** 보면 된다 (O(1)).
- `thread_unblock()` 안에는 선점을 넣지 않는다. `thread.c`의 `thread_unblock()` 주석대로, 호출자가 인터럽트를 끄고 "깨우기 + 다른 데이터 갱신"을 원자적으로 하려는 경우가 있기 때문이다. 예를 들어 `timer_interrupt()`의 while문은 여러 스레드를 연달아 깨운다.

### 7-3. 코드

#### ① `threads/thread.c` — `thread_preempt()` (현재 344행)

```c
/* ready_list 맨 앞 스레드의 우선순위가 현재 스레드보다 높으면 CPU를 양보한다.
   ready_list는 우선순위 내림차순으로 정렬되어 있으므로 맨 앞만 보면 된다.
   인터럽트 핸들러 안에서는 thread_yield()를 쓸 수 없으므로
   핸들러가 끝날 때 양보하도록 예약한다. */
void
thread_preempt (void) {
	enum intr_level old_level = intr_disable ();

	if (!list_empty (&ready_list)) {
		struct thread *front = list_entry (list_front (&ready_list), struct thread, elem);

		if (front->priority > thread_current ()->priority) {
			if (intr_context ()) {
				intr_yield_on_return ();
			} else {
				thread_yield ();
			}
		}
	}

	intr_set_level (old_level);
}
```

#### ② `include/threads/thread.h` — 원형 (138행)

```c
void thread_preempt (void);
```

#### ③ `threads/thread.c` — `thread_create()` 끝 (208행)

```c
	/* 실행 큐에 추가한다. */
	thread_unblock (t);

	/* 새 스레드가 현재 스레드보다 우선순위가 높으면 즉시 양보한다. */
	thread_preempt ();

	return tid;
```

#### ④ `threads/thread.c` — `thread_set_priority()` (313행)

```c
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;

	/* 우선순위를 낮춘 결과 더 높은 READY 스레드가 생겼다면 즉시 양보한다. */
	thread_preempt ();
}
```

### 7-4. 줄별 해설

```c
	enum intr_level old_level = intr_disable ();
```
- ready_list는 타이머 인터럽트(`timer_interrupt` → `thread_unblock`)도 수정한다. "비었는지 확인 → 맨 앞 읽기 → 결정"을 한 덩어리로 묶기 위해 인터럽트를 끈다.
- `intr_disable()`(`threads/interrupt.c:150`)은 **이전 상태**를 반환한다.
- `enum intr_level`은 `INTR_OFF` / `INTR_ON` 두 값만 갖는 열거형이다.

```c
	if (!list_empty (&ready_list)) {
```
- `list_front()`는 빈 리스트에서 ASSERT 패닉이 난다. READY가 하나도 없을 때도 `thread_set_priority()`는 호출될 수 있으므로 꼭 검사한다.

```c
		struct thread *front = list_entry (list_front (&ready_list), struct thread, elem);
```
- `list_front`는 **보기만** 한다. 실제로 꺼내는 일은 스케줄러(`next_thread_to_run`)가 한다.

```c
		if (front->priority > thread_current ()->priority) {
```
- **`>`이지 `>=`가 아니다.** 같은 우선순위에 양보하면 불필요한 문맥 전환이 생긴다. 같은 우선순위끼리의 순환은 타임 슬라이스가 맡는다.

```c
			if (intr_context ()) {
				intr_yield_on_return ();
			} else {
				thread_yield ();
			}
```
- `sema_up()`(3단계)은 인터럽트 핸들러에서도 호출될 수 있으므로 처음부터 두 경우를 나눠 둔다.
- **인터럽트가 꺼진 상태에서 `thread_yield()`를 불러도 될까?** 된다. `thread_yield()`가 내부에서 다시 `intr_disable()`로 이전 상태(OFF)를 기억한다. 다른 스레드가 돌다가 내 차례가 오면 `thread_yield()`가 반환되고, 그 뒤 우리의 `intr_set_level (old_level)`이 원래 상태(ON)로 복구한다. 인터럽트 상태는 스레드마다 따로 저장·복원된다.

```c
	intr_set_level (old_level);
```
- 무조건 켜지 않고 **원래 상태로 복구**한다. 호출자가 이미 인터럽트를 꺼 두었다면(예: `sema_up` 안) 꺼진 채로 돌려줘야 한다.

```c
	thread_unblock (t);
	thread_preempt ();
```
- **순서가 중요하다.** 먼저 ready_list에 넣어야 `thread_preempt()`가 새 스레드를 맨 앞에서 볼 수 있다.

### 7-5. 체크포인트

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads && make
cd build
for t in priority-change priority-preempt priority-fifo alarm-priority alarm-multiple; do make -s tests/threads/$t.result; done
```

결과: 5개 모두 PASS.

---

## 8. 3단계: 세마포어 대기자를 우선순위 순으로 깨우기

> 세마포어 자체의 개념(P/V, `while` 재확인, 사용 패턴 등)은 `threads/semaphore_concept.md`에 따로 정리되어 있다. 여기서는 우선순위와 관련된 변경만 다룬다.

### 8-1. 목표

`sema_up()`이 **가장 우선순위가 높은 대기자**를 깨우고, 깨운 스레드가 나보다 높으면 즉시 양보한다.

**락도 함께 해결된다.** `lock_acquire()`는 내부적으로 `sema_down (&lock->semaphore)`, `lock_release()`는 `sema_up (&lock->semaphore)`이기 때문이다.

### 8-2. 설계

| 위치 | 할 일 | 이유 |
|------|-------|------|
| `sema_down()` | `list_insert_ordered`로 정렬 삽입 | 1단계와 같은 방식, 같은 비교 함수 재사용 |
| `sema_up()` | 꺼내기 직전에 `list_sort`로 **다시 정렬** | 대기 중 기부로 우선순위가 바뀔 수 있음 (원칙 ②) |
| `sema_up()` | `value++` 뒤 `thread_preempt()` | 깨운 스레드가 더 높으면 즉시 양보 (원칙 ③) |

**왜 `sema_up`에서 또 정렬하나?**

```
lock X의 waiters = [A(35), B(32)]   ← 넣을 때는 정렬되어 있었음
    ↓ B가 가진 다른 락 Y를 우선순위 40인 C가 기다리며 B에게 기부
lock X의 waiters = [A(35), B(40)]   ← 정렬이 깨짐! 맨 앞을 꺼내면 틀림
```

지금(1~4단계)은 기부가 없어서 정렬 삽입만으로도 테스트가 통과한다. 재정렬은 `priority-donate-*`를 위한 사전 준비다.

`sema_down`의 정렬 삽입은 생략해도 결과가 같다. 그래도 해 두면 리스트가 거의 정렬된 상태라 `list_sort`(자연 병합 정렬)가 빨리 끝난다.

### 8-3. 코드 (`threads/synch.c`)

#### ① `sema_down()` (69행)

```c
	old_level = intr_disable ();
	while (sema->value == 0) {
		/* 대기자 리스트를 우선순위 내림차순으로 유지한다. */
		list_insert_ordered (&sema->waiters, &thread_current ()->elem,
				thread_priority_greater, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
```

#### ② `sema_up()` (106행)

```c
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		/* 기다리는 동안 기부로 우선순위가 바뀌었을 수 있으므로 다시 정렬한 뒤
		   가장 높은 우선순위의 스레드를 깨운다. */
		list_sort (&sema->waiters, thread_priority_greater, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;

	/* 깨운 스레드가 현재 스레드보다 우선순위가 높으면 양보한다. */
	thread_preempt ();
	intr_set_level (old_level);
}
```

### 8-4. `list_sort`가 FIFO를 지키는 이유 (안정 정렬)

`lib/kernel/list.c:355` `inplace_merge()`:

```c
	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))       // 뒤 원소가 앞 원소보다 "엄격히 앞서야" 할 때만
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);   // 뒤 원소를 앞으로 옮김
		}
```

뒤 구간 원소를 앞으로 옮기는 조건은 `less(뒤, 앞)` = `뒤.priority > 앞.priority`가 **엄격하게** 참일 때뿐이다. 같으면 순서를 바꾸지 않는다 → **안정 정렬**. 같은 우선순위 대기자끼리는 먼저 온 스레드가 먼저 깨어난다. 여기서도 `>`가 중요하다.

### 8-5. 줄별 해설

| 코드 | 설명 |
|------|------|
| `&sema->waiters` | `&(sema->waiters)`. sema는 포인터이므로 `->` |
| `&thread_current ()->elem` | `&((thread_current ())->elem)`. 현재 스레드의 elem 주소 |
| `while (sema->value == 0)` | 깨어나도 value가 남아 있다는 보장이 없다(다른 스레드가 먼저 가져갈 수 있음). 그래서 다시 확인 |
| `if (...) { ... }` 중괄호 | `list_sort`와 `thread_unblock` **두 문장**이 if에 속해야 하므로 필수 |
| `list_entry (list_pop_front (...), struct thread, elem)` | 안쪽부터: 떼어냄 → 스레드 포인터로 변환 → `thread_unblock` |
| `sema->value++;` 다음 `thread_preempt ();` | **순서 중요**. 양보를 먼저 하면 깨어난 스레드가 value 0을 보고 다시 잠든다 |
| 인터럽트 핸들러에서 호출 | `sema_up`은 핸들러에서 호출 가능. `thread_preempt()`가 `intr_context()`를 보고 `intr_yield_on_return()`으로 바꾸므로 안전 |

### 8-6. 실제로 있었던 실수: 중괄호 누락

처음 입력한 코드:

```c
	if (!list_empty (&sema->waiters))
	/* 주석 */
		list_sort (&sema->waiters, thread_priority_greater, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters), ...));
```

컴파일러가 읽는 방식:

```c
	if (!list_empty (&sema->waiters))
		list_sort (...);                 // 이것만 if에 속함

	thread_unblock (list_entry (list_pop_front (&sema->waiters), ...));   // 항상 실행!
```

- C에서 중괄호 없는 `if`는 **바로 다음 한 문장**만 포함한다. 들여쓰기는 컴파일러가 보지 않는다. 주석도 문장이 아니다.
- 대기자가 없는 세마포어에 `sema_up`(예: 아무도 기다리지 않는 락의 `lock_release`) → 빈 리스트에서 `list_pop_front` → `list_front`의 `ASSERT (!list_empty (list))` → **커널 패닉**.
- 해결: `if (...) { ... }`로 감싼다.

### 8-7. 체크포인트

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads && make
cd build
for t in priority-sema priority-change priority-preempt priority-fifo alarm-priority alarm-multiple; do make -s tests/threads/$t.result; done
```

결과: 6개 모두 PASS.

---

## 9. 4단계: 조건 변수 대기자를 우선순위 순으로 깨우기

### 9-1. 조건 변수의 구조

`threads/synch.c:289` `cond_wait()`:

```c
	struct semaphore_elem waiter;                       // 스택에 "나만의 세마포어"
	sema_init (&waiter.semaphore, 0);                   // 값 0 → down하면 바로 잠듦
	list_push_back (&cond->waiters, &waiter.elem);      // cond 대기 리스트에 등록
	lock_release (lock);
	sema_down (&waiter.semaphore);                      // 나만의 세마포어에서 잠듦
	lock_acquire (lock);
```

```
cond->waiters:  [semaphore_elem A] → [semaphore_elem B] → [semaphore_elem C]
                        │                    │                    │
                  semaphore.waiters    semaphore.waiters    semaphore.waiters
                    [스레드 25]          [스레드 30]          [스레드 22]
```

- `cond->waiters`의 원소는 **스레드가 아니라 `semaphore_elem`**이다.
- 각 semaphore_elem의 세마포어에서 **스레드 딱 1개**가 잠들어 있다.
- 3단계의 `sema_up` 정렬은 **세마포어 하나의 대기자들 사이**에서만 효과가 있다. 여기서는 세마포어마다 1명뿐이라 소용없다.
- → **`cond->waiters` 자체**를 우선순위로 골라야 한다.

### 9-2. 설계: `semaphore_elem`에 스레드 포인터 추가

| 방법 | 비교 방식 | 문제 |
|------|-----------|------|
| (a) 세마포어 대기 리스트의 맨 앞 스레드를 본다 | `list_front (&sa->semaphore.waiters)` | **비어 있을 수 있음** → 패닉 위험 |
| (b) `struct thread *thread` 필드 추가 | `sa->thread->priority` | 없음 ✅ |

**(a)가 위험한 이유**: `cond_wait()`는 `cond->waiters`에 **먼저 등록**한 뒤 `lock_release` → `sema_down` 순서로 잠든다. `lock_release` 안의 `sema_up`에서 선점(3단계)이 일어나면, 더 높은 스레드가 끼어들어 `cond_signal`을 부를 수 있다. 그 순간 내 semaphore_elem은 등록되어 있지만 세마포어 대기 리스트는 **아직 비어 있다**.

**(b)의 장점**:
- 등록 **전에** 포인터를 저장하므로 항상 안전하다.
- 비교할 때 스레드의 **현재** 우선순위를 읽으므로 기부로 바뀐 값도 반영된다.

**정렬 시점은 `cond_signal()`**이다 (원칙 ②). `cond_wait`는 `list_push_back` 그대로 둔다. `list_sort`가 안정 정렬이라 같은 우선순위끼리 FIFO가 유지된다.

### 9-3. 코드 (`threads/synch.c`)

#### ① `struct semaphore_elem` + 비교 함수 (242행)

```c
/* 리스트 안의 세마포어 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 원소. */
	struct semaphore semaphore;         /* 이 세마포어. */
	struct thread *thread;              /* 이 세마포어에서 기다리는 스레드. */
};

/* 조건 변수 대기자 A의 스레드 우선순위가 B보다 높으면 true를 반환한다.
   cond->waiters를 우선순위 내림차순으로 정렬할 때 쓴다. */
static bool
sema_elem_priority_greater (const struct list_elem *a,
		const struct list_elem *b, void *aux UNUSED) {
	const struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
	const struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);

	return sa->thread->priority > sb->thread->priority;
}
```

#### ② `cond_wait()` (298행)

```c
	sema_init (&waiter.semaphore, 0);
	waiter.thread = thread_current ();
	list_push_back (&cond->waiters, &waiter.elem);
```

#### ③ `cond_signal()` (313행)

```c
	if (!list_empty (&cond->waiters)) {
		/* 우선순위가 가장 높은 스레드가 기다리는 세마포어를 깨운다. */
		list_sort (&cond->waiters, sema_elem_priority_greater, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
```

`cond_broadcast()`는 수정하지 않는다. 내부에서 `cond_signal()`을 반복하므로 자동으로 우선순위 순으로 모두 깨운다.

### 9-4. 줄별 해설

| 코드 | 설명 |
|------|------|
| `struct thread *thread;` | 멤버 이름과 struct 태그가 같아도 된다. **struct 태그와 멤버 이름은 다른 이름 공간**. 바로 윗줄 `struct semaphore semaphore;`도 같은 방식 |
| `static bool sema_elem_priority_greater` | `struct semaphore_elem`은 **synch.c 안에서만** 정의된 구조체다. 다른 파일은 내부를 모르므로 비교 함수도 synch.c 전용(`static`) |
| 비교 함수 위치 | 반드시 구조체 정의 **아래**. 위에 있으면 `sa->thread`에서 "incomplete type" 에러 |
| `list_entry (a, struct semaphore_elem, elem)` | 1단계와 같은 패턴. 바깥 구조체만 다르다 |
| `sa->thread->priority` | `->` 연쇄: semaphore_elem → thread 포인터 → 스레드의 priority |
| `waiter.thread = thread_current ();` | `waiter`는 포인터가 아닌 **구조체 변수**라서 `.`을 쓴다 |
| 대입 위치 | 반드시 `list_push_back` **전**. 등록 순간부터 다른 스레드가 비교 함수로 읽을 수 있다 |
| 스택 변수 주소를 리스트에 넣기 | 안전하다. `cond_signal`이 리스트에서 꺼낸 **뒤에야** `sema_down`에서 깨어나 함수가 반환되므로, 사라진 메모리를 가리키는 일이 없다 |
| `&list_entry (...)->semaphore` | `->`가 `&`보다 먼저 계산 → `&(…->semaphore)`. `sema_up`은 포인터를 받으므로 `&` 필요 |

### 9-5. 체크포인트

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads && make
cd build
for t in priority-condvar priority-sema priority-change priority-preempt priority-fifo alarm-priority alarm-multiple; do make -s tests/threads/$t.result; done
```

결과: 7개 모두 PASS. **Part A·B 완료.**

---

## 10. 전체 동작 흐름

### 10-1. 선점이 일어나는 지점 지도

```
                       ┌─────────────────────────────┐
                       │  ready_list (내림차순 정렬)   │
                       │  [40] → [33] → [31] → [20]   │
                       └──────────────▲──────────────┘
                                      │ list_insert_ordered (1단계)
               ┌──────────────────────┴───────────────────────┐
         thread_unblock()                                thread_yield()
               ▲                                               ▲
     ┌─────────┼──────────────┐                                │
thread_create  sema_up     timer_interrupt              타임 슬라이스 만료
     │         │  (3단계)     (Alarm Clock)               / thread_preempt
     │         │                    │
     ▼         ▼                    ▼
thread_preempt()  thread_preempt()  (선점 없음: 타임 슬라이스·다음 스케줄 때 반영)
  (2단계)         (3단계)

thread_set_priority() ──▶ thread_preempt() (2단계)
```

### 10-2. 데이터 구조 관점

| 리스트 | 담는 것 | 정렬 방식 | 비교 함수 | 꺼내는 곳 |
|--------|---------|-----------|-----------|-----------|
| `ready_list` | `struct thread` (`elem`) | 삽입 시 정렬 | `thread_priority_greater` | `next_thread_to_run()` |
| `sema->waiters` | `struct thread` (`elem`) | 삽입 시 정렬 + 꺼낼 때 재정렬 | `thread_priority_greater` | `sema_up()` |
| `cond->waiters` | `struct semaphore_elem` (`elem`) | 꺼낼 때 정렬 | `sema_elem_priority_greater` | `cond_signal()` |
| `sleep_list` | `struct thread` (`elem`) | 삽입 시 정렬 (깨어날 시각) | `wakeup_tick_less` | `timer_interrupt()` |

### 10-3. 락 대기 흐름 (락 = 세마포어 값 1)

```
H(33)  lock_acquire(A) → sema_down → value 0 → waiters에 정렬 삽입 → BLOCKED
L(31)  lock_release(A) → sema_up → list_sort → H를 unblock → value 1
                                  → thread_preempt: 33 > 31 → L 양보
H(33)  sema_down의 while 재확인 → value 1 → value-- → 락 획득
```

---

## 11. 최종 변경 사항 (diff)

### `include/threads/thread.h`

```diff
 int thread_get_priority (void);
 void thread_set_priority (int);
+bool thread_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
+void thread_preempt (void);
```

### `threads/thread.c`

```diff
@@ thread_create
 	/* 실행 큐에 추가한다. */
 	thread_unblock (t);
 
+	thread_preempt(); // 새로 생성된 스레드의 우선순위가 현재 스레드보다 높으면 CPU를 양보
+
 	return tid;

@@ thread_unblock
 	ASSERT (t->status == THREAD_BLOCKED);
-	list_push_back (&ready_list, &t->elem);
+	list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL);
 	t->status = THREAD_READY;

@@ thread_yield
 	if (curr != idle_thread)
-		list_push_back (&ready_list, &curr->elem);
+		list_insert_ordered (&ready_list, &curr->elem, thread_priority_greater, NULL);
 	do_schedule (THREAD_READY);

@@ thread_set_priority
 	thread_current ()->priority = new_priority;
+
+	/* 우선순위를 낮춘 결과 더 높은 READY 스레드가 생겼다면 즉시 양보한다. */
+	thread_preempt();
 }

+bool
+thread_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
+	const struct thread *ta = list_entry (a, struct thread, elem);
+	const struct thread *tb = list_entry (b, struct thread, elem);
+
+	return ta->priority > tb->priority;
+}
+
+void
+thread_preempt (void) {
+	enum intr_level old_level = intr_disable ();
+
+	if (!list_empty (&ready_list)) {
+		struct thread *front = list_entry (list_front (&ready_list), struct thread, elem);
+
+		if (front->priority > thread_current ()->priority) {
+			if (intr_context ()) {
+				intr_yield_on_return ();
+			} else {
+				thread_yield ();
+			}
+		}
+	}
+
+	intr_set_level (old_level);
+}
```

### `threads/synch.c`

```diff
@@ sema_down
 	while (sema->value == 0) {
-		list_push_back (&sema->waiters, &thread_current ()->elem);
+		list_insert_ordered (&sema->waiters, &thread_current ()->elem, thread_priority_greater, NULL);
 		thread_block ();
 	}

@@ sema_up
 	old_level = intr_disable ();
-	if (!list_empty (&sema->waiters))
-		thread_unblock (list_entry (list_pop_front (&sema->waiters),
-					struct thread, elem));
+	if (!list_empty (&sema->waiters)) {
+		list_sort (&sema->waiters, thread_priority_greater, NULL);
+		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
+	}
 	sema->value++;
+
+	thread_preempt ();
 	intr_set_level (old_level);

@@ struct semaphore_elem
 	struct list_elem elem;              /* 리스트 원소. */
 	struct semaphore semaphore;         /* 이 세마포어. */
+	struct thread *thread;              /* 이 세마포어에서 기다리는 스레드 */
 };
 
+static bool sema_elem_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
+	const struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
+	const struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
+
+	return sa->thread->priority > sb->thread->priority;
+}

@@ cond_wait
 	sema_init (&waiter.semaphore, 0);
+	waiter.thread = thread_current();
 	list_push_back (&cond->waiters, &waiter.elem);

@@ cond_signal
-	if (!list_empty (&cond->waiters))
-		sema_up (&list_entry (list_pop_front (&cond->waiters),
-					struct semaphore_elem, elem)->semaphore);
+	if (!list_empty (&cond->waiters)) {
+		list_sort (&cond->waiters, sema_elem_priority_greater, NULL);
+		sema_up (&list_entry (list_pop_front (&cond->waiters),
+		struct semaphore_elem, elem)->semaphore);
+	}
```

### 정리하면 좋은 부분 (선택)

- `thread.c`의 주석 오타: `listen_insert_ordered()` → `list_insert_ordered()`, `내리마순` → `내림차순`.
- `thread.h` 원형의 `UNUSED`: 문법상 문제는 없지만 "본문에서 안 쓴다"는 표시라 보통 원형에는 붙이지 않는다.

---

## 12. 빌드와 테스트

### 12-1. 실행 위치

| 작업 | 위치 (컨테이너 안) |
|------|--------------------|
| 컨테이너 접속 | 호스트에서 `docker exec -it pintos bash` |
| 빌드 | `/workspaces/pintos_22.04_lab_docker/pintos/threads` |
| 실행·채점 | `/workspaces/pintos_22.04_lab_docker/pintos/threads/build` |

### 12-2. 빌드

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads && make
```

### 12-3. 테스트 하나 실행 (출력 직접 보기)

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads/build && pintos -- -q run priority-sema
```

### 12-4. 테스트 하나 채점

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads/build && make tests/threads/priority-sema.result
```

### 12-5. 1~4단계 관련 테스트 한 번에 채점

```bash
cd /workspaces/pintos_22.04_lab_docker/pintos/threads/build && for t in alarm-single alarm-multiple alarm-simultaneous alarm-priority alarm-zero alarm-negative priority-change priority-preempt priority-fifo priority-sema priority-condvar; do make -s tests/threads/$t.result; done
```

### 12-6. 채점표

`tests/threads/Rubric.priority` (가중치):

| 테스트 | 점수 | 현재 |
|--------|------|------|
| priority-change | 1 | ✅ |
| priority-preempt | 1 | ✅ |
| priority-fifo | 1 | ✅ |
| priority-sema | 2 | ✅ |
| priority-condvar | 2 | ✅ |
| priority-donate-one | 2 | ⬜ |
| priority-donate-multiple | 3 | ⬜ |
| priority-donate-multiple2 | 3 | ⬜ |
| priority-donate-nest | 3 | ⬜ |
| priority-donate-chain | 3 | ⬜ |
| priority-donate-sema | 2 | ⬜ |
| priority-donate-lower | 2 | ⬜ |

---

## 13. 테스트별 동작 추적과 기대 출력

### 13-1. `alarm-priority` (1단계)

`tests/threads/alarm-priority.c`: 우선순위 `PRI_DEFAULT - (i+5)%10 - 1` (= 25, 24, 23, 22, 21, 30, 29, 28, 27, 26 순으로 생성)인 스레드 10개가 모두 같은 시각(`wake_time`)까지 잠든다. main은 `PRI_MIN`으로 낮추고 `sema_down`으로 10번 기다린다.

```
timer_interrupt: 10개 모두 wakeup_tick 도달
  → thread_unblock × 10 → ready_list에 정렬 삽입
  → ready_list = [30, 29, 28, …, 21]
스케줄러: 30부터 차례로 실행 → 메시지 → sema_up(main) → 종료
```

기대 출력:
```
(alarm-priority) begin
(alarm-priority) Thread priority 30 woke up.
(alarm-priority) Thread priority 29 woke up.
...
(alarm-priority) Thread priority 21 woke up.
(alarm-priority) end
```

이전(FIFO)에는 `sleep_list`에서 꺼낸 순서대로 실행되어 우선순위 순이 아니었다 → FAIL.

### 13-2. `priority-preempt` (2단계)

```
main(31): thread_create("high-priority", 32)
          → unblock → preempt: 32 > 31 → 양보
high(32): iteration 0 → thread_yield
          → ready_list = [high(32), main(31)] 정렬 삽입 → 다시 high가 선택됨
          … iteration 4 → done! → 종료
main(31): "The high-priority thread should have already completed."
```

기대 출력:
```
(priority-preempt) begin
(priority-preempt) Thread high-priority iteration 0
(priority-preempt) Thread high-priority iteration 1
(priority-preempt) Thread high-priority iteration 2
(priority-preempt) Thread high-priority iteration 3
(priority-preempt) Thread high-priority iteration 4
(priority-preempt) Thread high-priority done!
(priority-preempt) The high-priority thread should have already completed.
(priority-preempt) end
```

`thread_yield` 후에도 high가 계속 실행되는 것은 1단계의 정렬 삽입 덕분이다.

### 13-3. `priority-change` (2단계)

```
main(31): thread_create("thread 2", 32)
          → preempt: 32 > 31 → 양보
thread2(32): "Thread 2 now lowering priority."
             thread_set_priority(30) → preempt: main(31) > 30 → 양보
main(31):  "Thread 2 should have just lowered its priority."
           thread_set_priority(29) → preempt: thread2(30) > 29 → 양보
thread2(30): "Thread 2 exiting." → 종료
main(29):  "Thread 2 should have just exited."
```

기대 출력:
```
(priority-change) begin
(priority-change) Creating a high-priority thread 2.
(priority-change) Thread 2 now lowering priority.
(priority-change) Thread 2 should have just lowered its priority.
(priority-change) Thread 2 exiting.
(priority-change) Thread 2 should have just exited.
(priority-change) end
```

`thread_preempt()` 호출 하나하나가 출력 순서를 만든다.

### 13-4. `priority-fifo` (1단계 + 2단계)

`tests/threads/priority-fifo.c`:

```
main: thread_set_priority(33)
main: 우선순위 32인 스레드 16개 생성 (0~15) → 33 > 32라 선점 없음
      ready_list = [0, 1, 2, …, 15]  (같은 32, 생성 순서 유지)
main: thread_set_priority(31) → preempt: 32 > 31 → 양보
각 스레드 (16번 반복):
      lock_acquire → 자기 id 기록 → lock_release → thread_yield
      → thread_yield가 같은 32들의 "맨 뒤"에 다시 삽입 (엄격한 > 덕분)
      → 0, 1, 2, …, 15, 0, 1, 2, … 순서가 매번 같음
```

검사 방식(`priority-fifo.ck`): 16번의 반복 각각이 **첫 번째 반복과 똑같은 순서**여야 PASS.

`>=`를 쓰면 `thread_yield`한 스레드가 같은 우선순위들 **맨 앞**에 다시 들어가 자기 자신이 계속 선택된다 → 순서가 깨져 FAIL.

### 13-5. `priority-sema` (3단계)

```
main: thread_set_priority(PRI_MIN=0)
main: 스레드 10개 생성 (우선순위 27, 26, 25, 24, 23, 22, 21, 30, 29, 28 순)
      → 각자 생성 즉시 선점(2단계) → sema_down → waiters에 정렬 삽입 → 잠듦
      → waiters = [30, 29, …, 21]
main: sema_up ① → 30 깨움 → preempt: 30 > 0 → 양보
  30: "Thread priority 30 woke up." → 종료
main: "Back in main thread."
… 반복
```

기대 출력:
```
(priority-sema) begin
(priority-sema) Thread priority 30 woke up.
(priority-sema) Back in main thread.
(priority-sema) Thread priority 29 woke up.
(priority-sema) Back in main thread.
...
(priority-sema) Thread priority 21 woke up.
(priority-sema) Back in main thread.
(priority-sema) end
```

| 빠뜨린 것 | 증상 |
|-----------|------|
| 정렬 (sema_down/sema_up) | 생성 순서(27, 26, …)대로 깨어남 |
| `sema_up`의 `thread_preempt()` | "Back in main thread."가 연달아 찍힘 |

### 13-6. `priority-condvar` (4단계 + 3단계)

```
main(0): 스레드 10개 생성 (23, 22, 21, 30, 29, 28, 27, 26, 25, 24 순)
  각 스레드: 즉시 선점 → "starting" → lock_acquire → cond_wait
             (cond->waiters에 생성 순서대로 push_back, 락 해제 후 잠듦)

main: lock_acquire → "Signaling..." → cond_signal
      → list_sort로 [30, 29, …, 21] → 30의 세마포어 sema_up
      → 30이 READY, preempt로 양보
      → 30은 cond_wait 안의 lock_acquire에서 main이 가진 락을 기다리며 다시 BLOCKED
main: lock_release → sema_up → 30을 깨우고 선점
  30: "Thread priority 30 woke up." → lock_release → 종료
main: 다음 반복 → 29 … 21
```

기대 출력:
```
(priority-condvar) begin
(priority-condvar) Thread priority 23 starting.
(priority-condvar) Thread priority 22 starting.
(priority-condvar) Thread priority 21 starting.
(priority-condvar) Thread priority 30 starting.
(priority-condvar) Thread priority 29 starting.
(priority-condvar) Thread priority 28 starting.
(priority-condvar) Thread priority 27 starting.
(priority-condvar) Thread priority 26 starting.
(priority-condvar) Thread priority 25 starting.
(priority-condvar) Thread priority 24 starting.
(priority-condvar) Signaling...
(priority-condvar) Thread priority 30 woke up.
(priority-condvar) Signaling...
(priority-condvar) Thread priority 29 woke up.
...
(priority-condvar) Signaling...
(priority-condvar) Thread priority 21 woke up.
(priority-condvar) end
```

"starting"은 **생성 순서**, "woke up"은 **우선순위 순서**다. 이 대비가 4단계가 하는 일을 그대로 보여 준다.

---

## 14. 디버깅 가이드

### 14-1. 오류 종류별 도구

| 증상 | 먼저 볼 것 |
|------|------------|
| 컴파일 에러 | `make` 출력의 **첫 번째** 에러 (뒤 에러는 연쇄인 경우가 많음) |
| 커널 패닉 (`assertion ... failed`) | 패닉 메시지의 파일:행 + `backtrace` |
| 출력 순서가 틀림 | `pintos -- -q run <테스트>`로 실제 출력을 보고 `tests/threads/<테스트>.ck`의 기대값과 비교 |
| 멈춤 (타임아웃) | 모든 스레드가 BLOCKED인지, 선점 무한 반복인지 gdb로 확인 |

### 14-2. 자주 보는 컴파일 에러

| 에러 | 원인 |
|------|------|
| `implicit declaration of function 'thread_priority_greater'` | thread.h에 원형이 없음 |
| `dereferencing pointer to incomplete type 'struct semaphore_elem'` | 비교 함수가 구조체 정의보다 **위**에 있음 |
| `'struct semaphore_elem' has no member named 'thread'` | 구조체에 필드를 추가하지 않음 |
| `passing argument 3 of 'list_insert_ordered' from incompatible pointer type` | 비교 함수의 매개변수 모양이 `list_less_func`와 다름 (`const` 누락 등) |

### 14-3. 자주 보는 패닉

| 패닉 메시지 | 원인 |
|-------------|------|
| `assertion '!list_empty (list)' failed` (list.c) | `sema_up`/`cond_signal`의 `if`에 중괄호 누락, 또는 `thread_preempt`에서 빈 리스트 검사 누락 |
| `assertion '!intr_context ()' failed` (thread.c) | 인터럽트 핸들러 안에서 `thread_yield()` 호출 → `intr_yield_on_return()` 분기 확인 |
| `assertion 'intr_get_level () == INTR_OFF' failed` | `schedule()`에 인터럽트가 켜진 채로 들어감 |

### 14-4. gdb

```bash
# 터미널 1 (threads/build)
pintos --gdb -- -q run priority-sema
# 터미널 2 (threads/build)
gdb kernel.o
(gdb) target remote localhost:1234
(gdb) break sema_up
(gdb) continue
(gdb) p sema->value
(gdb) p list_size(&sema->waiters)
```

`list_entry`는 매크로라 gdb에서 바로 쓸 수 없다. 리스트 안의 스레드 우선순위를 보고 싶다면 `thread_current ()->priority`처럼 포인터가 있는 곳에서 확인하거나, 잠깐 `printf`를 넣는 편이 간단하다. 단, `schedule()` 안이나 인터럽트가 꺼진 구간의 출력은 타이밍을 바꿀 수 있으니 확인 후 반드시 지운다.

---

## 15. 자주 하는 실수

| 실수 | 증상 | 원인 / 해결 |
|------|------|-------------|
| 비교 함수에 `>=` | `priority-fifo` FAIL | 같은 우선순위가 새치기. **엄격한 `>`** |
| `thread_preempt`의 비교에 `>=` | 불필요한 문맥 전환, 같은 우선순위 순환이 꼬임 | `>` 사용 |
| ready_list 삽입을 한 곳만 수정 | `thread_yield` 후 순서가 틀림 | `thread_unblock`·`thread_yield` **둘 다** |
| `thread_unblock()` 안에 선점을 넣음 | `timer_interrupt`에서 깨우는 도중 문맥 전환, 패닉 | 선점은 호출자 쪽(`thread_create`, `sema_up`)에서 |
| `thread_create`에서 `thread_preempt`를 `thread_unblock` **전에** 호출 | `priority-preempt` FAIL | 넣은 다음 검사 |
| `thread_preempt`에서 `list_empty` 검사 누락 | READY가 없을 때 `set_priority` → 패닉 | `if (!list_empty (...))` |
| 인터럽트 문맥 분기 누락 | 핸들러에서 `sema_up` 시 `!intr_context ()` 패닉 | `intr_context()` → `intr_yield_on_return()` |
| `sema_up`의 `if`에 중괄호 누락 (**실제로 겪음**) | 거의 모든 테스트에서 `!list_empty` 패닉 | 두 문장 이상이면 반드시 `{ }` |
| `sema_up`에서 `thread_preempt`를 `value++` **전**에 호출 | 깨어난 스레드가 value 0을 보고 다시 잠듦 | `value++` 다음에 |
| `cond_wait`에서 `waiter.thread` 대입을 `list_push_back` 뒤에 | 드물게 쓰레기 포인터 비교 → 패닉 | 등록 **전**에 대입 |
| 비교 함수를 `semaphore_elem` 정의 위에 작성 | incomplete type 에러 | 구조체 정의 아래로 |
| `cond_signal`에서 `sema_elem_...` 대신 `thread_priority_greater` 사용 | 엉뚱한 메모리 비교 → 순서 틀림/패닉 | `cond->waiters`는 semaphore_elem을 담는다 |
| 수정 후 `make`를 안 함 | 고쳤는데 결과가 그대로 | `threads`에서 `make` 먼저 |
| 호스트에서 `pintos` 실행 | `command not found` | 컨테이너 안에서 |

---

## 16. 다른 설계 방법과 비교

### 16-1. ready_list

| 방법 | 삽입 | 선택 | 선점 검사 | 비고 |
|------|------|------|-----------|------|
| **정렬 삽입 (이번 구현)** | O(n) | O(1) | O(1) 맨 앞만 | 기부로 ready 스레드 우선순위가 바뀌면 재정렬 필요 |
| 정렬 없음 + `list_max` | O(1) | O(n) | O(n) | 항상 현재 값 기준이라 기부에 강함 |
| 우선순위별 큐 64개 | O(1) | O(64) | O(64) | 실제 OS 방식(비트맵). MLFQS에서도 유용 |

스레드 수가 적은 Pintos에서는 어느 쪽이든 성능 차이가 거의 없다. 기존 코드(`next_thread_to_run`)를 덜 고치고, Alarm Clock과 같은 패턴인 정렬 삽입을 택했다.

### 16-2. 세마포어 대기자

| 방법 | 장점 | 단점 |
|------|------|------|
| `sema_down` 정렬 삽입만 | 간단 | 기부 후 순서가 틀릴 수 있음 |
| `sema_up`에서 `list_sort` (+ 정렬 삽입) **(이번 구현)** | 기부에도 안전, 안정 정렬로 FIFO 유지 | 매번 정렬 |
| `sema_up`에서 `list_max` + `list_remove` | O(n), 기부에도 안전, 같은 값은 앞쪽 선택 | 코드가 조금 길어짐 |

### 16-3. 조건 변수 대기자

| 방법 | 장점 | 단점 |
|------|------|------|
| `semaphore_elem->thread` 저장 **(이번 구현)** | 항상 안전, 현재 우선순위 반영 | 필드 하나 추가 |
| 세마포어 waiters의 맨 앞 스레드 비교 | 필드 추가 없음 | 등록~잠들기 사이에 비어 있으면 패닉 |
| `semaphore_elem`에 `int priority` 복사 | 단순 | 기부로 바뀐 값을 반영 못 함 |

---

## 17. C 문법 정리

Alarm Clock 상세서(`make_alarm_clock.md` 16장)의 표에 이어, 이번에 새로 나온 것 위주로 정리한다.

| 문법 | 예 | 의미 |
|------|-----|------|
| 중괄호 없는 `if` | `if (c) a(); b();` | `a()`만 if에 속한다. `b()`는 항상 실행 |
| 중괄호 있는 `if` | `if (c) { a(); b(); }` | 둘 다 if에 속한다. 두 문장 이상이면 필수 |
| `if` / `else` | `if (intr_context ()) A; else B;` | 조건에 따라 둘 중 하나 |
| 함수 포인터 전달 | `list_sort (..., thread_priority_greater, NULL)` | 괄호 없는 함수 이름 = 함수 주소 |
| `typedef` 함수 타입 | `typedef bool list_less_func (...)` | 비교 함수의 "모양" 정의 |
| 공개 함수 vs `static` 함수 | `bool thread_priority_greater` / `static bool sema_elem_...` | 다른 파일에서 쓰면 공개 + 헤더 원형, 한 파일에서만 쓰면 `static` |
| 함수 원형 | `void thread_preempt (void);` | 다른 .c 파일이 호출할 수 있도록 헤더에 선언 |
| `.` vs `->` | `waiter.thread` / `sa->thread` | 구조체 **변수**는 `.`, 구조체 **포인터**는 `->` |
| `->` 연쇄 | `sa->thread->priority` | 포인터 → 포인터 → 멤버 |
| `&` + `->` 우선순위 | `&sema->waiters` | `->`가 먼저: `&(sema->waiters)` |
| 함수 결과에 `->` | `thread_current ()->priority` | 반환된 포인터에 바로 접근 |
| 중첩 호출 | `thread_unblock (list_entry (list_pop_front (...), ...))` | 안쪽부터 실행 |
| `const` 포인터 | `const struct thread *ta` | 가리키는 대상을 읽기만 |
| `enum` 변수 | `enum intr_level old_level` | `INTR_ON`/`INTR_OFF` 중 하나 |
| 블록 중간 변수 선언 | `struct thread *front = ...;` (if 안) | C99부터 허용. 그 블록 안에서만 유효 |
| 이름 공간 | `struct thread *thread;` | struct 태그와 멤버 이름은 겹쳐도 된다 |
| 선언 순서 | 구조체 정의 → 그 구조체를 쓰는 함수 | 컴파일러는 위에서 아래로 읽는다 |
| 비교식의 값 | `return a > b;` | 비교식 자체가 `bool` 값 |
| 매크로 | `UNUSED`, `list_entry`, `PRI_MAX` | 컴파일 전에 글자가 바뀐다 |

### 들여쓰기는 컴파일러에게 아무 의미가 없다

```c
if (x)
	a ();
	b ();      // 들여쓰기는 if 안처럼 보이지만 항상 실행된다
```

이번 3단계에서 실제로 겪은 버그다. GCC의 `-Wmisleading-indentation` 경고가 잡아 주기도 하지만, 사이에 주석이 있으면 놓칠 수 있다. **두 줄 이상이면 무조건 중괄호**를 습관으로 하자.

---

## 18. Q&A 모음

**Q. Alarm Clock만 했을 때는 `alarm-priority`가 FAIL이었는데 왜 1단계만으로 PASS가 됐나?**
A. `timer_interrupt()`는 깨울 때 `thread_unblock()`을 호출한다. 1단계에서 `thread_unblock()` **내부**를 정렬 삽입으로 바꿨기 때문에, 같은 틱에 깨어난 스레드들이 자동으로 우선순위 순으로 ready_list에 들어간다. Alarm Clock 코드는 그대로다.

**Q. 비교 함수 이름이 `less`인데 왜 `>`를 넘기나?**
A. `list_insert_ordered`/`list_sort`가 원하는 것은 "a가 b보다 **앞에** 와야 하나?"라는 질문의 답이다. 작은 값을 앞에 두려면 `<`, 큰 값을 앞에 두려면 `>`를 넘기면 된다. 이름은 오름차순을 기본으로 가정했을 뿐이다.

**Q. 왜 `next_thread_to_run()`은 고치지 않았나?**
A. 넣을 때 정렬해 두면 맨 앞이 항상 최댓값이므로, 기존의 "맨 앞을 꺼낸다"가 그대로 정답이 된다.

**Q. 왜 선점을 `thread_unblock()` 안에 넣지 않았나?**
A. `thread_unblock()`의 원래 계약이 "선점하지 않는다"이다. 호출자가 인터럽트를 끄고 여러 스레드를 연달아 깨우거나 다른 데이터를 함께 갱신할 때, 중간에 CPU를 빼앗기면 안 된다. 그래서 선점은 호출자(`thread_create`, `sema_up`) 쪽에서 모든 작업을 마친 뒤 한다.

**Q. `timer_interrupt()`에서 높은 스레드를 깨웠을 때도 즉시 선점해야 하나?**
A. 테스트 통과에는 필요 없다. `alarm-priority`에서는 깨어날 때 실행 중인 스레드가 idle이고, 인터럽트가 끝나면 idle이 곧 `thread_block()`으로 스케줄러를 부른다. 또 타임 슬라이스 선점이 최대 4틱 안에 반영한다. 원한다면 `timer_interrupt()` 끝에서 `thread_preempt()`를 부를 수 있고, 인터럽트 문맥이므로 자동으로 `intr_yield_on_return()`이 선택된다.

**Q. 인터럽트를 끈 상태에서 `thread_yield()`를 불러도 되나?**
A. 된다. 인터럽트 상태는 스레드마다 저장·복원된다. `thread_yield()`가 내부에서 이전 상태를 기억했다가 돌아올 때 복구하고, 이어서 `thread_preempt()`의 `intr_set_level (old_level)`이 원래 상태로 되돌린다.

**Q. 세마포어에서 `sema_down` 정렬 삽입과 `sema_up` 정렬을 둘 다 하는 이유는?**
A. `sema_up`의 정렬만으로도 정답은 나온다. 정렬 삽입은 리스트를 거의 정렬된 상태로 유지해 `list_sort`(자연 병합 정렬)를 빠르게 만드는 보조 역할이다. `sema_up`의 재정렬은 기다리는 동안 기부로 우선순위가 바뀐 경우를 바로잡는 **필수** 역할이다.

**Q. 락은 따로 고치지 않았는데 왜 우선순위 순으로 깨어나나?**
A. 락은 값이 1인 세마포어 + 주인(`holder`) 정보다. `lock_acquire` = `sema_down`, `lock_release` = `sema_up`이므로 3단계 수정이 그대로 적용된다.

**Q. 조건 변수에서 왜 `cond_wait`에서 정렬 삽입하지 않고 `cond_signal`에서 정렬하나?**
A. 정렬 삽입하려면 비교 함수가 우선순위를 읽어야 한다. `thread` 필드를 쓰면 가능하지만, 기다리는 동안 기부로 우선순위가 바뀔 수 있으므로 어차피 꺼낼 때 다시 정렬해야 한다. 그래서 꺼낼 때 한 번만 정렬한다.

**Q. `cond_wait`의 `waiter`는 지역 변수인데, 그 주소를 리스트에 넣어도 되나?**
A. 된다. `waiter`는 `cond_wait()`가 끝나야 사라진다. `cond_signal`이 먼저 리스트에서 꺼내 `sema_up`을 해야 `sema_down`에서 깨어나 함수가 반환되므로, 리스트가 사라진 메모리를 가리키는 순간이 없다.

**Q. 세마포어와 락에는 기부를 안 하나?**
A. 기부는 **락에만** 한다. 락에는 "누가 가지고 있다"(`holder`)는 개념이 있어서 기부할 대상이 분명하다. 세마포어·조건 변수에는 주인이 없어서 누구에게 기부해야 할지 알 수 없다.

---

## 19. 남은 단계: 우선순위 기부 (5~8단계)

구현 후 이 장을 상세 내용으로 교체한다. 지금은 설계 방향만 적어 둔다.

### 19-1. 기부의 세 가지 경우

| 경우 | 상황 | 기대 동작 | 테스트 |
|------|------|-----------|--------|
| 단일 | H가 L의 락을 기다림 | L이 H의 우선순위로 실행, 락 해제 시 원래대로 | `donate-one` |
| 다중 | L이 락 A, B를 갖고 H1, H2가 각각 기다림 | L = max(기부값들). A를 풀면 A 쪽 기부만 사라짐 | `donate-multiple`, `-multiple2` |
| 중첩 | H → M의 락, M → L의 락 | H의 우선순위가 M을 거쳐 L까지 전달 (깊이 8) | `donate-nest`, `-chain` |
| 기부 중 변경 | 기부받는 중 `set_priority`로 낮춤 | 실제 우선순위는 기부값 유지, 끝나면 새 값 | `donate-lower` |
| 세마포어 혼합 | 락 + 세마포어 | 깨움 순서도 기부 반영 | `donate-sema` |

### 19-2. 추가할 필드 (5단계)

| 필드 | 타입 | 의미 |
|------|------|------|
| `init_priority` | `int` | 기부와 무관한 **원래** 우선순위 (`set_priority`가 바꾸는 값) |
| `wait_on_lock` | `struct lock *` | 지금 기다리는 락 (중첩 기부 때 사슬을 따라가는 데 사용) |
| `donations` | `struct list` | 나에게 기부한 스레드들의 목록 |
| `donation_elem` | `struct list_elem` | 다른 스레드의 `donations`에 들어가기 위한 elem (`elem`과 별도) |

`priority`는 "기부까지 반영된 **실제** 우선순위"로 의미가 바뀐다. 1~4단계의 비교 함수들은 모두 `priority`를 보므로 수정할 필요가 없다.

### 19-3. 흐름 미리보기

```
lock_acquire(lock):
    holder가 있으면
        cur->wait_on_lock = lock
        holder->donations에 cur 추가
        donate: cur → holder → holder가 기다리는 락의 holder → … (최대 8단계)
    sema_down
    cur->wait_on_lock = NULL
    lock->holder = cur

lock_release(lock):
    donations에서 "wait_on_lock == lock"인 스레드 제거
    priority = max(init_priority, 남은 donations의 priority)
    lock->holder = NULL
    sema_up  (→ thread_preempt로 즉시 양보)

thread_set_priority(new):
    init_priority = new
    priority 재계산 (기부가 남아 있으면 기부값 유지)
    thread_preempt
```

### 19-4. 1~4단계 설계가 Part C를 어떻게 돕는가

| 1~4단계 결정 | Part C에서의 역할 |
|--------------|-------------------|
| `sema_up`에서 `list_sort` | 기다리는 동안 기부받은 스레드를 올바르게 먼저 깨움 |
| `cond_signal`에서 `list_sort` + `semaphore_elem->thread` | 기부로 바뀐 현재 우선순위로 비교 |
| `thread_preempt()` 공통 함수 | `lock_release`·`set_priority` 후 우선순위가 내려갔을 때 그대로 재사용 |
| `thread_priority_greater()` 공개 | `donations` 리스트 정렬/최댓값 계산에 재사용 가능 |

---

## 부록: 관련 파일 위치

| 파일 | 내용 |
|------|------|
| `include/threads/thread.h` | `PRI_MIN/DEFAULT/MAX`, `struct thread`, `thread_priority_greater`·`thread_preempt` 원형 |
| `threads/thread.c` | `thread_create` (178), `thread_unblock` (236), `thread_yield` (298), `thread_set_priority` (313), `thread_priority_greater` (331), `thread_preempt` (344), `next_thread_to_run` (455) |
| `threads/synch.c` | `sema_down` (60), `sema_up` (106), `lock_acquire` (188), `lock_release` (223), `struct semaphore_elem` (242), `sema_elem_priority_greater` (252), `cond_wait` (289), `cond_signal` (313), `cond_broadcast` (333) |
| `include/threads/synch.h` | `struct semaphore`, `struct lock` (`holder`), `struct condition` |
| `threads/interrupt.c` | `intr_disable` (150), `intr_set_level` (129), `intr_context` (256), `intr_yield_on_return` (265) |
| `include/lib/kernel/list.h`, `lib/kernel/list.c` | `list_entry` (h:103), `list_less_func` (h:147), `list_insert_ordered` (c:416), `list_sort` (c:378), `inplace_merge` (c:355), `list_front` (c:266), `list_pop_front` (c:248) |
| `devices/timer.c` | `timer_interrupt` — Alarm Clock 깨우기 (`thread_unblock` 경유로 1단계 혜택) |
| `tests/threads/priority-*.c`, `*.ck` | 테스트 코드와 기대 출력 |
| `tests/threads/Rubric.priority` | 우선순위 테스트 채점표 |
| `threads/make_alarm_clock.md` | Alarm Clock 상세서 |
| `threads/semaphore_concept.md` | 세마포어 개념 정리 |
