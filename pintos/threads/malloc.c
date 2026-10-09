#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* malloc()의 간단한 구현.

   각 요청의 크기(바이트 단위)는 2의 거듭제곱으로 올림되고,
   그 크기의 블록들을 관리하는 "디스크립터"에 배정된다.
   디스크립터는 빈 블록 리스트를 유지한다. 빈 리스트가
   비어 있지 않으면, 그 블록 중 하나로 요청을
   충족한다.

   그렇지 않으면 페이지 할당자로부터 "아레나(arena)"라고 부르는
   새 메모리 페이지를 얻는다(남은 페이지가 없으면 malloc()은
   널 포인터를 반환한다). 새 아레나는 블록들로 나뉘고, 그 블록들은
   모두 디스크립터의 빈 리스트에 추가된다. 그런 다음 새 블록 중
   하나를 반환한다.

   블록을 해제하면 그 블록을 디스크립터의 빈 리스트에 추가한다.
   하지만 그 블록이 속한 아레나에 사용 중인 블록이 더 이상 없으면,
   아레나의 모든 블록을 빈 리스트에서 제거하고 아레나를
   페이지 할당자에게 돌려준다.

   이 방식으로는 2kB보다 큰 블록을 처리할 수 없다. 디스크립터와
   함께 페이지 하나에 들어가기에는 너무 크기 때문이다. 그런
   블록은 페이지 할당자로 연속된 페이지들을 할당하고, 할당된
   블록의 아레나 헤더 앞부분에 할당 크기를 기록하여
   처리한다. */

/* 디스크립터. */
struct desc {
	size_t block_size;          /* 각 원소의 바이트 크기. */
	size_t blocks_per_arena;    /* 아레나 하나에 들어 있는 블록 수. */
	struct list free_list;      /* 빈 블록 리스트. */
	struct lock lock;           /* 락. */
};

/* 아레나 손상을 감지하기 위한 매직 넘버. */
#define ARENA_MAGIC 0x9a548eed

/* 아레나. */
struct arena {
	unsigned magic;             /* 항상 ARENA_MAGIC으로 설정된다. */
	struct desc *desc;          /* 소유 디스크립터. 큰 블록이면 널. */
	size_t free_cnt;            /* 빈 블록 수. 큰 블록이면 페이지 수. */
};

/* 빈 블록. */
struct block {
	struct list_elem free_elem; /* 빈 리스트 원소. */
};

/* 디스크립터 집합. */
static struct desc descs[10];   /* 디스크립터들. */
static size_t desc_cnt;         /* 디스크립터 개수. */

static struct arena *block_to_arena (struct block *);
static struct block *arena_to_block (struct arena *, size_t idx);

/* malloc() 디스크립터들을 초기화한다. */
void
malloc_init (void) {
	size_t block_size;

	for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2) {
		struct desc *d = &descs[desc_cnt++];
		ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
		d->block_size = block_size;
		d->blocks_per_arena = (PGSIZE - sizeof (struct arena)) / block_size;
		list_init (&d->free_list);
		lock_init (&d->lock);
	}
}

/* 최소 SIZE 바이트인 새 블록을 얻어 반환한다.
   사용할 수 있는 메모리가 없으면 널 포인터를 반환한다. */
void *
malloc (size_t size) {
	struct desc *d;
	struct block *b;
	struct arena *a;

	/* 0바이트 요청은 널 포인터로 충족된다. */
	if (size == 0)
		return NULL;

	/* SIZE 바이트 요청을 충족하는 가장 작은 디스크립터를
	   찾는다. */
	for (d = descs; d < descs + desc_cnt; d++)
		if (d->block_size >= size)
			break;
	if (d == descs + desc_cnt) {
		/* SIZE가 어떤 디스크립터에도 맞지 않을 만큼 크다.
		   SIZE와 아레나 하나를 담을 수 있을 만큼 페이지를 할당한다. */
		size_t page_cnt = DIV_ROUND_UP (size + sizeof *a, PGSIZE);
		a = palloc_get_multiple (0, page_cnt);
		if (a == NULL)
			return NULL;

		/* PAGE_CNT 페이지짜리 큰 블록임을 나타내도록 아레나를
		   초기화하고 반환한다. */
		a->magic = ARENA_MAGIC;
		a->desc = NULL;
		a->free_cnt = page_cnt;
		return a + 1;
	}

	lock_acquire (&d->lock);

	/* 빈 리스트가 비어 있으면 새 아레나를 만든다. */
	if (list_empty (&d->free_list)) {
		size_t i;

		/* 페이지를 할당한다. */
		a = palloc_get_page (0);
		if (a == NULL) {
			lock_release (&d->lock);
			return NULL;
		}

		/* 아레나를 초기화하고 그 블록들을 빈 리스트에 추가한다. */
		a->magic = ARENA_MAGIC;
		a->desc = d;
		a->free_cnt = d->blocks_per_arena;
		for (i = 0; i < d->blocks_per_arena; i++) {
			struct block *b = arena_to_block (a, i);
			list_push_back (&d->free_list, &b->free_elem);
		}
	}

	/* 빈 리스트에서 블록을 하나 꺼내 반환한다. */
	b = list_entry (list_pop_front (&d->free_list), struct block, free_elem);
	a = block_to_arena (b);
	a->free_cnt--;
	lock_release (&d->lock);
	return b;
}

/* 0으로 초기화된 A 곱하기 B 바이트를 할당하여 반환한다.
   사용할 수 있는 메모리가 없으면 널 포인터를 반환한다. */
void *
calloc (size_t a, size_t b) {
	void *p;
	size_t size;

	/* 블록 크기를 계산하고 size_t에 들어가는지 확인한다. */
	size = a * b;
	if (size < a || size < b)
		return NULL;

	/* 메모리를 할당하고 0으로 채운다. */
	p = malloc (size);
	if (p != NULL)
		memset (p, 0, size);

	return p;
}

/* BLOCK에 할당된 바이트 수를 반환한다. */
static size_t
block_size (void *block) {
	struct block *b = block;
	struct arena *a = block_to_arena (b);
	struct desc *d = a->desc;

	return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs (block);
}

/* OLD_BLOCK의 크기를 NEW_SIZE 바이트로 바꾸려 하며, 그 과정에서
   블록이 이동할 수도 있다.
   성공하면 새 블록을, 실패하면 널 포인터를
   반환한다.
   OLD_BLOCK이 널인 호출은 malloc(NEW_SIZE)와 같다.
   NEW_SIZE가 0인 호출은 free(OLD_BLOCK)과 같다. */
void *
realloc (void *old_block, size_t new_size) {
	if (new_size == 0) {
		free (old_block);
		return NULL;
	} else {
		void *new_block = malloc (new_size);
		if (old_block != NULL && new_block != NULL) {
			size_t old_size = block_size (old_block);
			size_t min_size = new_size < old_size ? new_size : old_size;
			memcpy (new_block, old_block, min_size);
			free (old_block);
		}
		return new_block;
	}
}

/* 블록 P를 해제한다. P는 이전에 malloc(), calloc(), realloc()으로
   할당된 것이어야 한다. */
void
free (void *p) {
	if (p != NULL) {
		struct block *b = p;
		struct arena *a = block_to_arena (b);
		struct desc *d = a->desc;

		if (d != NULL) {
			/* 일반 블록이다. 여기서 처리한다. */

#ifndef NDEBUG
			/* 해제 후 사용(use-after-free) 버그를 찾기 쉽도록 블록을 지운다. */
			memset (b, 0xcc, d->block_size);
#endif

			lock_acquire (&d->lock);

			/* 블록을 빈 리스트에 추가한다. */
			list_push_front (&d->free_list, &b->free_elem);

			/* 아레나가 이제 전혀 사용되지 않으면 해제한다. */
			if (++a->free_cnt >= d->blocks_per_arena) {
				size_t i;

				ASSERT (a->free_cnt == d->blocks_per_arena);
				for (i = 0; i < d->blocks_per_arena; i++) {
					struct block *b = arena_to_block (a, i);
					list_remove (&b->free_elem);
				}
				palloc_free_page (a);
			}

			lock_release (&d->lock);
		} else {
			/* 큰 블록이다. 그 페이지들을 해제한다. */
			palloc_free_multiple (a, a->free_cnt);
			return;
		}
	}
}

/* 블록 B가 들어 있는 아레나를 반환한다. */
static struct arena *
block_to_arena (struct block *b) {
	struct arena *a = pg_round_down (b);

	/* 아레나가 유효한지 확인한다. */
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);

	/* 블록이 아레나에 맞게 올바르게 정렬되어 있는지 확인한다. */
	ASSERT (a->desc == NULL
			|| (pg_ofs (b) - sizeof *a) % a->desc->block_size == 0);
	ASSERT (a->desc != NULL || pg_ofs (b) == sizeof *a);

	return a;
}

/* 아레나 A 안의 (IDX - 1)번째 블록을 반환한다. */
static struct block *
arena_to_block (struct arena *a, size_t idx) {
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);
	ASSERT (idx < a->desc->blocks_per_arena);
	return (struct block *) ((uint8_t *) a
			+ sizeof *a
			+ idx * a->desc->block_size);
}
