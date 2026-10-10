/* vm.c: 가상 메모리 객체를 위한 범용 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을
 * 초기화한다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* project 4용 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* 위쪽 줄들은 수정하지 마시오. */
	/* TODO: 여기에 코드를 작성하세요. */
}

/* 페이지의 타입을 얻는다. 페이지가 초기화된 이후의 타입을 알고
 * 싶을 때 유용한 함수이다.
 * 이 함수는 이미 완전히 구현되어 있다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 도우미 함수 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* 초기화 함수(initializer)와 함께 대기 중인(pending) 페이지 객체를
 * 만든다. 페이지를 만들고 싶다면 직접 만들지 말고 이 함수나
 * `vm_alloc_page`를 통해 만들어라. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* upage가 이미 사용 중인지 확인한다. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 만들고, VM 타입에 따라 초기화 함수를 가져온 뒤,
		 * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 만든다.
		 * TODO: uninit_new를 호출한 뒤에 필드를 수정해야 한다. */

		/* TODO: 페이지를 spt에 삽입한다. */
	}
err:
	return false;
}

/* spt에서 VA를 찾아 페이지를 반환한다. 오류 시 NULL을 반환한다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채워라. */

	return page;
}

/* 검증을 거쳐 PAGE를 spt에 삽입한다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: 이 함수를 채워라. */

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* 내쫓길(evict) struct frame을 얻는다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: 내쫓기(eviction) 정책은 여러분이 정한다. */

	return victim;
}

/* 페이지 하나를 내쫓고 해당 프레임을 반환한다.
 * 오류 시 NULL을 반환한다.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: 희생 페이지(victim)를 스왑 아웃하고 내쫓은 프레임을 반환한다. */

	return NULL;
}

/* palloc()으로 프레임을 얻는다. 사용 가능한 페이지가 없으면 페이지를
 * 내쫓고 그것을 반환한다. 이 함수는 항상 유효한 주소를 반환한다.
 * 즉, 사용자 풀 메모리가 가득 차면 이 함수는 프레임을 내쫓아
 * 사용 가능한 메모리 공간을
 * 확보한다.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: 이 함수를 채워라. */

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* 스택을 키운다. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* 쓰기 보호된 페이지에서의 폴트를 처리한다 */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공하면 true를 반환한다 */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: 폴트를 검증한다 */
	/* TODO: 여기에 코드를 작성하세요 */

	return vm_do_claim_page (page);
}

/* 페이지를 해제한다.
 * 이 함수는 수정하지 마시오. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 확보(claim)한다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채워라 */

	return vm_do_claim_page (page);
}

/* PAGE를 확보(claim)하고 mmu를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 링크를 설정한다 */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 PA에 매핑하는 페이지 테이블 엔트리를 삽입한다. */

	return swap_in (page, frame->kva);
}

/* 새 보조 페이지 테이블(supplemental page table)을 초기화한다 */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
}

/* src의 보조 페이지 테이블을 dst로 복사한다 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* 보조 페이지 테이블이 보유한 자원을 해제한다 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 보유한 supplemental_page_table을 모두 파괴하고
	 * TODO: 수정된 내용을 모두 저장소에 다시 기록(writeback)한다. */
}
