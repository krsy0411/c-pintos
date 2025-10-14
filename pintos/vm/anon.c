/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */
#include <string.h>  // ← memset

#include "devices/disk.h"
#include "lib/kernel/bitmap.h"  // ← Pintos 경로
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

#define SECTOR_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

static struct bitmap *swap_bitmap;
static struct lock swap_lock;

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
  swap_disk = disk_get(1, 1);
  ASSERT(swap_disk != NULL);

  size_t slots = disk_size(swap_disk) / SECTOR_PER_PAGE;
  swap_bitmap = bitmap_create(slots);
  ASSERT(swap_bitmap != NULL);

  lock_init(&swap_lock);
}

/* 익명 페이지 초기화 */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  ASSERT(VM_TYPE(type) == VM_ANON);
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;
  anon_page->slot_no = BITMAP_ERROR;

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
  if (page == NULL) return;

  // 가상 페이지가 물리 프레임도 갖고 있다면 해제해줘야 한다
  if (page->frame != NULL) {
    struct frame *f = page->frame;

    struct thread *t = thread_current();
    if (t != NULL && t->pml4 != NULL) {
      pml4_clear_page(t->pml4, page->va);
    }

    if (f->kva != NULL) {
      palloc_free_page(f->kva);
      f->kva = NULL;
    }

    f->page = NULL;
    page->frame = NULL;
  }
}
