/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "kernel/bitmap.h"  //bitmap을 사용을 위해
#include "threads/vaddr.h"
#include "vm/vm.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
struct bitmap *swap_table;  // swap slot 사용여부 관리하기 위한 비트맵 포인터
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;
// 페이지 하나당 몇개의 sector가 들어가는가를 계산

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
  /* 익명 페이지 실행 전 스왑이 원활하게 일어날 수 있도록
    스왑 파티션 디스크를 찾아 연결하고 초기화 후 비트맵을 생성하는 함수
  */
  swap_disk = disk_get(1, 1);
  size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
  swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;
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
