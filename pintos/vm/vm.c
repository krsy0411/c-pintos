/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "kernel/hash.h"
#include "threads/malloc.h"
#include "vm/inspect.h"
#include "vm/vm.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
#ifdef EFILESYS
  pagecache_init();
#endif
  register_inspect_intr();
  /* TODO: Your code goes here. */
}

enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
  ASSERT(VM_TYPE(type) != VM_UNINIT);

  struct supplemental_page_table *spt = &thread_current()->spt;

  if (spt_find_page(spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initializer according to the VM type,
     * and then create "uninit" page struct by calling uninit_new. */
    /* TODO: Insert the page into the spt. */
  }
  return false;
}

struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
  ASSERT(spt != NULL);
  if (va == NULL) return NULL;

  struct page temp_page;
  temp_page.va = va;
  struct hash_elem *e = hash_find(&spt->spt_hash, &temp_page.hash_elem);
  if (e == NULL) return NULL;
  return hash_entry(e, struct page, hash_elem);
}

bool spt_insert_page(struct supplemental_page_table *spt, struct page *page) {
  ASSERT(spt != NULL);
  ASSERT(page != NULL);
  ASSERT(page->va != NULL);

  if (spt_find_page(spt, page->va) != NULL) return false;

  hash_insert(&spt->spt_hash, &page->hash_elem);

  return true;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  vm_dealloc_page(page);
}

static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  return victim;
}

static struct frame *vm_evict_frame(void) {
  struct frame *victim = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */
  return NULL;
}

/* 물리 프레임 할당 */
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */
  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

static void vm_stack_growth(void *addr UNUSED) {}

static bool vm_handle_wp(struct page *page UNUSED) {}

bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
                         bool write, bool not_present) {
  struct supplemental_page_table *spt = &thread_current()->spt;
  struct page *page = NULL;
  /* TODO: Validate the fault */
  /* TODO: Your code goes here */
  return vm_do_claim_page(page);
}

void vm_dealloc_page(struct page *page) {
  destroy(page);
  free(page);
}

/*
 * va(가상 주소)로 해당 페이지 찾아서 vm_do_claim_page() 호출
 * claim : va(가상주소)를 물리 프레임을 실제로 할당하고 페이지 테이블에 매핑
 */
bool vm_claim_page(void *va) {
  ASSERT(va != NULL);

  // va로 페이지 찾기
  struct supplemental_page_table *spt = &thread_current()->spt;
  struct page *page = spt_find_page(spt, va);

  // 해당하는 페이지가 없으면 실패(false)
  if (page == NULL) return false;

  return vm_do_claim_page(page);
}

static bool vm_do_claim_page(struct page *page) {
  struct frame *frame = vm_get_frame();
  frame->page = page;
  page->frame = frame;
  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  return swap_in(page, frame->kva);
}

void supplemental_page_table_init(struct supplemental_page_table *spt) {
  ASSERT(spt != NULL);

  hash_init(&spt->spt_hash, hash_hash_func, hash_less_func, NULL);
}

bool supplemental_page_table_copy(struct supplemental_page_table *dst,
                                  struct supplemental_page_table *src) {
  /* TODO: Implement copy logic */
  return false;
}

void supplemental_page_table_kill(struct supplemental_page_table *spt) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * writeback all the modified contents to the storage. */
}

uint64_t hash_hash_func(const struct hash_elem *e, void *aux) {
  struct page *page = hash_entry(e, struct page, hash_elem);
  return hash_bytes(page->va, sizeof *page->va);
}

bool hash_less_func(const struct hash_elem *a, const struct hash_elem *b,
                    void *aux) {
  struct page *page_a = hash_entry(a, struct page, hash_elem);
  struct page *page_b = hash_entry(b, struct page, hash_elem);

  return page_a->va < page_b->va;
}