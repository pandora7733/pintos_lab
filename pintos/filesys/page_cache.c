/* page_cache.c: 페이지 캐시(버퍼 캐시) 구현. */

#include "vm/vm.h"
static bool page_cache_readahead (struct page *page, void *kva);
static bool page_cache_writeback (struct page *page);
static void page_cache_destroy (struct page *page);

/* 이 구조체는 수정하지 마시오 */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE,
};

tid_t page_cache_workerd;

/* 파일 vm 초기화 함수 */
void
pagecache_init (void) {
	/* TODO: page_cache_kworkerd로 페이지 캐시용 워커 데몬을 생성하라 */
}

/* 페이지 캐시를 초기화한다 */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 핸들러 설정 */
	page->operations = &page_cache_op;

}

/* 스왑 인 메커니즘을 활용해 미리 읽기(readahead)를 구현한다 */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* 스왑 아웃 메커니즘을 활용해 write-back을 구현한다 */
static bool
page_cache_writeback (struct page *page) {
}

/* page_cache를 파괴한다. */
static void
page_cache_destroy (struct page *page) {
}

/* 페이지 캐시용 워커 스레드 */
static void
page_cache_kworkerd (void *aux) {
}
