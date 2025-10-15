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

static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;

  lock_acquire(&swap_lock);
  size_t slot = anon_page->slot_no;
  if (slot == BITMAP_ERROR || !bitmap_test(swap_bitmap, slot)) {
    lock_release(&swap_lock);
    return false;
  }

  for (size_t i = 0; i < SECTOR_PER_PAGE; i++) {
    disk_read(swap_disk, slot * SECTOR_PER_PAGE + i,
              (uint8_t *)kva + i * DISK_SECTOR_SIZE);
  }
  bitmap_reset(swap_bitmap, slot);    // 슬롯 반환
  anon_page->slot_no = BITMAP_ERROR;  // 더 이상 스왑 자리 없음
  lock_release(&swap_lock);
  return true;
}

static bool anon_swap_out(struct page *page) {
  if (page == NULL || page->frame == NULL) return false;

  struct anon_page *anon_page = &page->anon;

  lock_acquire(&swap_lock);
  size_t page_no = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
  if (page_no == BITMAP_ERROR) {
    lock_release(&swap_lock);
    return false;  // 또는 PANIC("insufficient swap space");
  }

  // 반드시 프레임의 KVA에서 디스크로!
  for (size_t i = 0; i < SECTOR_PER_PAGE; i++) {
    disk_write(swap_disk, page_no * SECTOR_PER_PAGE + i,
               (uint8_t *)page->frame->kva + i * DISK_SECTOR_SIZE);
  }
  anon_page->slot_no = page_no;

  // 매핑 해제 (프레임은 교체기에서 재사용)
  pml4_clear_page(thread_current()->pml4, page->va);
  page->frame->page = NULL;
  page->frame = NULL;

  lock_release(&swap_lock);
  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  if (page == NULL) return;

  struct anon_page *anon_page = &page->anon;

  // 스왑 슬롯 차지 중이면 반납
  if (anon_page->slot_no != BITMAP_ERROR) {
    lock_acquire(&swap_lock);
    bitmap_reset(swap_bitmap, anon_page->slot_no);
    lock_release(&swap_lock);
    anon_page->slot_no = BITMAP_ERROR;
  }

  // 매핑만 정리 (프레임/테이블 해제는 프레임 관리자가 처리)
  if (page->frame) {
    pml4_clear_page(thread_current()->pml4, page->va);
    // 여기서 palloc_free_page(page->frame->kva)나 free(frame)를 직접 하지 말고,
    // 프레임 관리 경로에서 일관되게 정리하도록 두는 게 안전.
    page->frame->page = NULL;
    page->frame = NULL;
  }
}
