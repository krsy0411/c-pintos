#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "intrinsic.h"
#include "kernel/stdio.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void sys_write(int fd, const void *buffer, size_t size);
void sys_exit(int status);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f) {
  // TODO: Your implementation goes here.
  if (f->R.rax == SYS_HALT) {
    power_off();
  } else if (f->R.rax == SYS_EXIT) {
    sys_exit(f->R.rdi);
  } else if (f->R.rax == SYS_WRITE) {
    sys_write(f->R.rdi, (const void *)f->R.rsi, f->R.rdx);
  }
}

void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->exit_status = status;
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit();
}
void sys_write(int fd, const void *buffer, size_t size) {
  if (buffer == NULL || is_user_vaddr(buffer) == false ||
      pml4_get_page(thread_current()->pml4, buffer) == NULL) {
    sys_exit(-1);  // 잘못된 주소 접근 시 강제 종료
  }

  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  } else {
    sys_exit(-1);  // 지원하지 않는 fd
  }

  // struct thread *cur = thread_current();
  // struct file *fd_tb = cur->fd_table[fd];
}