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
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
  void vm_init(void) {
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
  }

  /* Get the type of the page. This function is useful if you want to know the
   * type of the page after it will be initialized.
   * This function is fully implemented now. */
  enum vm_type page_get_type(struct page * page) {
    int ty = VM_TYPE(page->operations->type);
    switch (ty) {
      case VM_UNINIT:
        return VM_TYPE(page->uninit.type);
      default:
        return ty;
    }
    enum vm_type page_get_type(struct page * page) {
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
    static bool vm_do_claim_page(struct page * page);
    static struct frame *vm_evict_frame(void);

    /* Create the pending page object with initializer. If you want to create a
     * page, do not create it directly and make it through this function or
     * `vm_alloc_page`. */
    bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                        bool writable, vm_initializer *init,
                                        void *aux) {
      ASSERT(VM_TYPE(type) != VM_UNINIT)

      struct supplemental_page_table *spt = &thread_current()->spt;

      /* Check wheter the upage is already occupied or not. */
      if (spt_find_page(spt, upage) == NULL) {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */

        /* TODO: Insert the page into the spt. */
      }
    err:
      return false;
    }

    /*
     * supplemental_page_table(spt)에서 주어진 가상 주소(va)에 해당하는 페이지
     * 정보를 찾아 반환합니다.
     * - spt_hash 해시 테이블을 이용해 va를 키로 검색합니다.
     * - 내부적으로 임시 page 구조체를 만들어 va만 채운 뒤, hash_find로
     * 검색합니다.
     * - 검색 결과가 있으면 해당 struct page 포인터를 반환하고, 없으면 NULL을
     * 반환합니다.
     * - 인자(spt, va)가 NULL이면 바로 NULL 반환.
     * - 페이지 폴트 처리, 페이지 할당 여부 확인 등에서 사용됩니다.
     */
    struct page *spt_find_page(struct supplemental_page_table * spt UNUSED,
                               void *va UNUSED) {
      /* 인자 검증 : 에러시 NULL 반환 */
      if (spt == NULL) return NULL;
      if (va == NULL) return NULL;

      struct page temp_page;  // hash_find용 임시 페이지
      temp_page.va = va;

      /* spt에서 va에 해당하는 페이지 찾기 */
      struct hash_elem *e = hash_find(&spt->spt_hash, &temp_page.hash_elem);
      if (e == NULL) return NULL;

      /* hash_find 결과가 있으면 해당 struct page 포인터를 반환
        hash_elem에서 struct page 구조체를 얻기 위해 hash_entry 매크로 사용

        struct page의 메모리 구조:
          0x1000┌─────────────────┐ ← page 구조체 시작
                │ operations      │
          0x1008├─────────────────┤
                │ va              │
          0x1010├─────────────────┤
                │ frame           │
          0x1018├─────────────────┤ ← hash_elem 위치
                │ hash_elem       │
          0x1020├─────────────────┤
                │ union { ... }   │
                └─────────────────┘

        해당하는 page가 없으면 NULL 반환
       */
      return hash_entry(e, struct page, hash_elem);
    }

    /* Insert PAGE into spt with validation. */
    /* 가상 주소에 해당하는 페이지의 메타데이터를 SPT에 등록
     * 이를 통해 나중에 해당 주소에서 페이지 폴트가 발생했을 때
     * 이 페이지를 어디서 찾아야 하는지(ex 파일 시스템, 스왑 디스크) 알 수 있게
     * 됨*/

    bool spt_insert_page(struct supplemental_page_table * spt UNUSED,
                         struct page * page UNUSED) {
      // page의 가상 주소가 spt에 이미 존재하는지 확인
      // spt_find_page로 해당 주소의 page를 찾으면 그 page의 포인터를, 찾지
      // 못하면 NULL을 반환

      if (spt_find_page(spt, page->va) != NULL) {
        // 해당 가상 주소에 매핑된 페이지가 존재하므로
        // 중복 삽입을 방지하고 false반환
        return false;
      }

      // 존재 하지 않는 경우만 해시테이블에 insert
      hash_insert(&spt->spt_hash, &page->hash_elem);

      // insert 성공 했으므로 true 반환
      return true;
    }

    void spt_remove_page(struct supplemental_page_table * spt,
                         struct page * page) {
      vm_dealloc_page(page);
      return true;
      void spt_remove_page(struct supplemental_page_table * spt,
                           struct page * page) {
        vm_dealloc_page(page);
        return true;
      }

      /* Get the struct frame, that will be evicted. */
      static struct frame *vm_get_victim(void) {
        struct frame *victim = NULL;
        /* TODO: The policy for eviction is up to you. */
        static struct frame *vm_get_victim(void) {
          struct frame *victim = NULL;
          /* TODO: The policy for eviction is up to you. */

          return victim;
        }

        /* Evict one page and return the corresponding frame.
         * Return NULL on error.*/
        static struct frame *vm_evict_frame(void) {
          struct frame *victim UNUSED = vm_get_victim();
          /* TODO: swap out the victim and return the evicted frame. */

          return NULL;
        }

        /* palloc() and get frame. If there is no available page, evict the page
         * and return it. This always return valid address. That is, if the user
         * pool memory is full, this function evicts the frame to get the
         * available memory space.*/
        static struct frame *vm_get_frame(void) {
          struct frame *frame = NULL;
          /* TODO: Fill this function. */

          ASSERT(frame != NULL);
          ASSERT(frame->page == NULL);
          return frame;
        }

        /* Growing the stack. */
        static void vm_stack_growth(void *addr UNUSED) {}

        /* Handle the fault on write_protected page */
        static bool vm_handle_wp(struct page * page UNUSED) {}

        /* Return true on success */
        /* 페이지 폴트 처리 함수 : exception.c 파일에서 호출
         * not_present가 true인 경우에만 호출
         */
        bool vm_try_handle_fault(struct intr_frame * f UNUSED,
                                 void *addr UNUSED, bool user UNUSED,
                                 bool write UNUSED, bool not_present UNUSED) {
          struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
          struct page *page = NULL;
          /* TODO: Validate the fault */
          /* TODO: Your code goes here */

          return vm_do_claim_page(page);
        }

        /* Free the page.
         * DO NOT MODIFY THIS FUNCTION. */
        void vm_dealloc_page(struct page * page) {
          destroy(page);
          free(page);
        }

        /* Claim the page that allocate on VA. */
        bool vm_claim_page(void *va UNUSED) {
          struct page *page = NULL;
          /* TODO: Fill this function */

          return vm_do_claim_page(page);
        }

        /* Claim the PAGE and set up the mmu. */
        /* 실제 페이지를 할당하고 mmu를 설정하는 함수 */
        static bool vm_do_claim_page(struct page * page) {
          struct frame *frame = vm_get_frame();

          /* Set links */
          frame->page = page;
          page->frame = frame;
          /* Set links */
          frame->page = page;
          page->frame = frame;

          /* TODO: Insert page table entry to map page's VA to frame's PA. */
          /* TODO: Insert page table entry to map page's VA to frame's PA. */

          return swap_in(page, frame->kva);
          return swap_in(page, frame->kva);
        }

        /* Initialize new supplemental page table */
        void supplemental_page_table_init(struct supplemental_page_table *
                                          spt UNUSED) {}
        void supplemental_page_table_init(struct supplemental_page_table *
                                          spt UNUSED) {}

        /* Copy supplemental page table from src to dst */
        bool supplemental_page_table_copy(
            struct supplemental_page_table * dst UNUSED,
            struct supplemental_page_table * src UNUSED) {}
        bool supplemental_page_table_copy(
            struct supplemental_page_table * dst UNUSED,
            struct supplemental_page_table * src UNUSED) {}

        /* Free the resource hold by the supplemental page table */
        void supplemental_page_table_kill(struct supplemental_page_table *
                                          spt UNUSED) {
          /* TODO: Destroy all the supplemental_page_table hold by thread and
           * TODO: writeback all the modified contents to the storage. */
          void supplemental_page_table_kill(struct supplemental_page_table *
                                            spt UNUSED) {
            /* TODO: Destroy all the supplemental_page_table hold by thread and
             * TODO: writeback all the modified contents to the storage. */
          }
