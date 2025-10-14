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
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &file_ops;

  struct file_page *file_page = &page->file;
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

    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    addr += PGSIZE;
    offset += page_read_bytes;
  }

  return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr) {}
