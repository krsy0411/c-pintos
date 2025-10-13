/* file.c: Implementation of memory backed file object (mmaped object). */

#include "filesys/file.h"

#include <string.h>

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) {}

/* Initialize the file backed page */

/* 최소 구현: 스왑은 나중에. 지금은 메타데이터만 넣어두기 */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  page->operations = &file_ops;

  struct segment_info *aux = (struct segment_info *)page->uninit.aux;
  ASSERT(aux != NULL);

  page->file.file = aux->file;  // file_reopen된 핸들 (공유)
  page->file.ofs = aux->ofs;
  page->file.read_bytes =
      aux->page_read_bytes;  // 마지막 페이지 write-back에 필요
  page->file.zero_bytes = aux->page_zero_bytes;

  /* aux를 여기서 free 할지, lazy 로더에서 free 할지는 네 구현 한 군데로만! */
  // free(aux);

  return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {
  struct file *f = file_reopen(file);
  if (f == NULL) {
    return NULL;
  }

  void *start_addr = addr;
  size_t total_page_count = (length + PGSIZE - 1) / PGSIZE;

  off_t file_len = file_length(f);
  size_t read_bytes = (file_len < length) ? file_len : length;
  size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);

  if (zero_bytes == PGSIZE) {
    zero_bytes = 0;
  }

  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(addr) == 0);     // upage가 페이지 정렬되어 있는지 확인
  ASSERT(offset % PGSIZE == 0);  // ofs가 페이지 정렬되어 있는지 확인

  while (read_bytes > 0 || zero_bytes > 0) {
    /* 이 페이지를 채우는 방법을 계산
    파일에서 PAGE_READ_BYTES 바이트를 읽고
    최종 PAGE_ZERO_BYTES 바이트를 0으로 채우기 */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct segment_info *aux =
        (struct segment_info *)malloc(sizeof(struct segment_info));
    if (aux == NULL) {
      file_close(f);
      return NULL;
    }

    aux->file = f;
    aux->ofs = offset;
    aux->page_read_bytes = page_read_bytes;
    aux->page_zero_bytes = page_zero_bytes;

    // vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성
    if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable,
                                        lazy_load_segment, aux)) {
      free(aux);
      file_close(f);
      return NULL;
    }

    struct page *p = spt_find_page(&thread_current()->spt, start_addr);
    p->mapped_page_count = total_page_count;

    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    addr += PGSIZE;
    offset += page_read_bytes;
  }

  return start_addr;
}
/* Do the munmap */ /* Do the munmap */
void do_munmap(void *addr) {
  if (addr == NULL) return;

  struct thread *t = thread_current();
  void *base = pg_round_down(addr);

  struct page *first = spt_find_page(&t->spt, base);
  if (first == NULL) return;

  int total = first->mapped_page_count;
  if (total <= 0) return;

  /* file 포인터는 페이지 free 전에 백업해 둔다 */
  struct file *file_to_close = first->file.file;

  for (int i = 0; i < total; i++) {
    void *va = base + (size_t)i * PGSIZE;
    struct page *p = spt_find_page(&t->spt, va);
    if (p == NULL) continue;

    /* 파일 백드 + 더티면 파일에 반영 (read_bytes 만큼) */
    if (VM_TYPE(p->operations->type) == VM_FILE) {
      if (p->frame != NULL && pml4_is_dirty(t->pml4, p->va)) {
        (void)file_write_at(p->file.file, p->frame->kva, p->file.read_bytes,
                            p->file.ofs);
        pml4_set_dirty(t->pml4, p->va, false);
      }
    }

    /* 해시에서 제거 + 해제 : vm_dealloc_page() 직접 호출 말고 이걸로! */
    spt_remove_page(&t->spt, p);
    /* spt_remove_page 내부에서 pml4_clear_page, frame 반환, free 까지 처리 */
  }
}