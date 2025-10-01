/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "vm/vm.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
  /* TODO: Set up the swap_disk. */
  swap_disk = NULL;
}

/* Initialize the file mapping */
/* 익명 페이지(VM_ANON)의 초기화 인자로 사용되는 함수 */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  ASSERT(type == VM_ANON);
  ASSERT(page->frame != NULL);

  /* Set up the handler */
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;
  /*
   * TODO : Swap 구현할 때 필요한 초기화 코드 추가할 것
   * (anon_page 필드 추가도 포함해서)
   */

  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
  struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *anon_page = &page->anon;
}
