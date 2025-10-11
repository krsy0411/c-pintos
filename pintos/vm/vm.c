/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include <string.h>

#include "filesys/file.h"
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

    // page 멤버 초기화
    page->writable = writable;
    page->is_stack = false;

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

/*
 * 페이지 폴트가 발생했을 때, 처리 가능한 정상적인 폴트인지 판단하고 처리할 함수
 * f(인터럽트 프레임) : 페이지 폴트가 발생했을 때의 CPU 레지스터 상태
 * addr : 페이지 폴트가 발생한 가상 주소
 * user : 유저 모드에서 발생한 페이지 폴트인지 여부(커널 모드/유저 모드)
 * write : 쓰기 접근인지 여부(읽기/쓰기)
 * not_present : 페이지가 존재하지 않아서 발생한 폴트인지 여부(페이지 존재/권한)
 */
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
                         bool write, bool not_present) {
  struct supplemental_page_table *spt = &thread_current()->spt;
  struct page *page = NULL;

  // 폴트가 발생한 주소가 유효하지 않거나 커널 영역 주소라면 복구 대상이 아님

  if ((addr == NULL) || !is_user_vaddr(addr)) return false;

  // 주소는 유효하나 메모리에 없어서 생긴 페이지 폴트라면 복구 대상이 맞음
  if (not_present) {
    // user access 인 경우
    void *rsp = f->rsp;
    // kernel access 인 경우
    if (!user) rsp = thread_current()->rsp;

    // 스택 확장으로 처리할 수 있는 폴트인 경우에 한해서만 vm_stack_growth를
    // 호출
    if ((USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr) &&
        (addr <= USER_STACK)) {
      vm_stack_growth(addr);
    } else if ((USER_STACK - (1 << 20) <= rsp) && (rsp <= addr) &&
               (addr <= USER_STACK)) {
      vm_stack_growth(addr);
    }

    page = spt_find_page(spt, addr);
    if (page == NULL) return false;
    // 읽기 관련 정보 확인
    if (write == 1 && page->writable == 0) return false;

    return vm_do_claim_page(page);
  }

  return false;
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
void supplemental_page_table_init(struct supplemental_page_table *spt) {
  hash_init(&spt->spt_hash, spt_hash_func, spt_less_func, NULL);
}

bool supplemental_page_table_copy(struct supplemental_page_table *dst,
                                  struct supplemental_page_table *src) {
  struct hash_iterator
      it;  // SPT는 해시테이블로 관리하니까 그 안을 돌기 위한 반복자 준비
  hash_first(&it, &src->spt_hash);  // 부모SPT 버킷 순회를 위해서 반복자 초기화
  while (hash_next(&it)) {  // 부모 SPT의 모든 페이지 엔트리를 순회하며 자식
                            // SPT에 대응 페이지를 만들기
    struct page *src_page = hash_entry(hash_cur(&it), struct page, hash_elem);
    enum vm_type type = page_get_type(src_page);
    void *upage = src_page->va;
    bool writable = src_page->writable;

    /* 아직 로드되지 않은(uninit) 페이지는 initializer/aux 정보를 복제
특히 파일 기반 lazy load라면 file_reopen()으로 자식 전용 파일 핸들을 만들기 */
    if (src_page->operations->type == VM_UNINIT) {
      struct uninit_page *uninit = &src_page->uninit;
      vm_initializer *init = uninit->init;
      enum vm_type init_type = uninit->type;

      void *aux = NULL;
      if (uninit->aux != NULL) {
        struct segment_info *src_info = uninit->aux;
        struct segment_info *dst_info = malloc(sizeof *dst_info);
        if (dst_info == NULL) return false;
        memcpy(dst_info, src_info, sizeof *dst_info);
        if (dst_info->file != NULL) {
          dst_info->file = file_reopen(dst_info->file);
          if (dst_info->file == NULL) {
            free(dst_info);
            return false;
          }
        }
        aux = dst_info;
      }

      if (!vm_alloc_page_with_initializer(init_type, upage, writable, init,
                                          aux)) {
        if (aux != NULL) free(aux);
        return false;
      }

      struct page *dst_page = spt_find_page(dst, upage);
      if (dst_page == NULL) return false;
      dst_page->is_stack = src_page->is_stack;
      continue;
    }
    /* 이미 로드된 anon/file 페이지라면 새 프레임을 할당·매핑하고 부모 프레임
     내용을 자식 프레임에 그대로 복사 */
    if (!vm_alloc_page(type, upage, writable)) return false;
    if (!vm_claim_page(upage)) {
      struct page *dst_page = spt_find_page(dst, upage);
      if (dst_page != NULL) {
        hash_delete(&dst->spt_hash, &dst_page->hash_elem);
        vm_dealloc_page(dst_page);
      }
      return false;
    }

    struct page *dst_page = spt_find_page(dst, upage);
    if (dst_page == NULL) return false;
    dst_page->is_stack = src_page->is_stack;

    if (src_page->frame != NULL && src_page->frame->kva != NULL &&
        dst_page->frame != NULL && dst_page->frame->kva != NULL) {
      memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }
  }
  return true;
}

/* 아래 spt_kill 함수에서 콜백 함수로 전달하기 위한 함수 */
static void spt_hash_destroy_func(struct hash_elem *e, void *aux) {
  struct page *page = hash_entry(e, struct page, hash_elem);
  vm_dealloc_page(page);  // destroy + free
}

void supplemental_page_table_kill(struct supplemental_page_table *spt) {
  hash_destroy(&spt->spt_hash, spt_hash_destroy_func);
}

uint64_t spt_hash_func(const struct hash_elem *e, void *aux) {
  struct page *page = hash_entry(e, struct page, hash_elem);
  return hash_bytes(&page->va, sizeof(page->va));
}

bool spt_less_func(const struct hash_elem *a, const struct hash_elem *b,
                   void *aux) {
  struct page *page_a = hash_entry(a, struct page, hash_elem);
  struct page *page_b = hash_entry(b, struct page, hash_elem);

  return page_a->va < page_b->va;
}