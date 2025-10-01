/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "kernel/hash.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
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

/*
 * 1. 페이지가 아직 물리적으로 할당되지 않은(uninit) 상태에서,
 * 타입에 맞는 초기화 정보와 함께 논리적으로 페이지를 등록
 * 2. 실제 물리 프레임 할당 및 초기화는 페이지 폴트 시,
 * vm_do_claim_page() => swap_in 핸들러를 통해 이뤄짐
 */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
  ASSERT(VM_TYPE(type) != VM_UNINIT);

  struct supplemental_page_table *spt = &thread_current()->spt;

  /* 해당 가상 주소에 페이지가 존재하지 않으면 */
  if (spt_find_page(spt, upage) == NULL) {
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) return false;

    page->writable = writable;

    /*
     * 초기화 함수 포인터 설정
     * type에 따라 anon_initializer 또는 file_backed_initializer 설정
     * page_initializer는 uninit_new()에서 사용
     * 페이지가 swap-in될 때 호출되어 페이지를 초기화하는 역할
     */
    bool (*initializer)(struct page *, enum vm_type, void *kva);
    switch (VM_TYPE(type)) {
      case VM_ANON:
        initializer = anon_initializer;
        break;
      case VM_FILE:
        initializer = file_backed_initializer;
        break;
      default:
        free(page);
        return false;
    }

    /* uninit 페이지 생성 */
    uninit_new(page, upage, init, type, aux, initializer);

    /* spt에 페이지 삽입 : 정보 저장 */
    if (!spt_insert_page(spt, page)) {
      free(page);
      return false;
    }

    return true;
  }

  /* 이미 해당 가상 주소에 페이지가 존재하면 실패 */
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
  // user pool에서 new physical page 할당 받아 커널 가상주소에 kva 저장됨
  void *kva = palloc_get_page(PAL_USER);

  // kva 가 NULL 이라면 사용자 풀에 더 이상 가용 페이지가 없다는 뜻
  if (kva == NULL) {
    // 나중에 스왑 아웃 로직 구현 필요
    PANIC("todo");
  }

  // physical page 관리할 frame 구조체를 위해 메모리 할당
  struct frame *frame = malloc(sizeof(struct frame));

  ASSERT(frame != NULL);  // 할당 성공 했는지 체크
  frame->kva = kva;       // frame member 초기화
  frame->page = NULL;

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
  page->frame = frame; /* 서로 연결 */

  struct thread *curr = thread_current();
  if (!pml4_set_page(curr->pml4, page->va, frame->kva, page->writable)) {
    frame->page = NULL;
    page->frame = NULL;
    palloc_free_page(frame->kva);
    free(frame);
    return false;
  }

  return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  /** Project 3-Memory Management */
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
