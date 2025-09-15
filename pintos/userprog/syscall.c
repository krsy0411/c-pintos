#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>

#include "init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

void syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void halt(void);
void exit(int);
/* The main system call interface */
void syscall_handler (struct intr_frame *f UNUSED) {
  uint64_t syscall_num = f->R.rax;  // 주석 해제!

  switch (syscall_num) {
    case SYS_EXIT:
      exit((int)f->R.rdi);
      break;
    case SYS_WRITE:
      // TODO: write 구현
      break;
    case SYS_HALT:
      halt();
      break;
    default:
      printf("Unknown system call: %llu\n", syscall_num);
      thread_exit();
  }
}

void halt () {
  power_off(); // 시스템 종료
}

void exit (const int status) {
  struct thread *curr = thread_current();
  curr->exit_status = status;

  // TODO: 나중에 파일 descriptor 정리 등 추가

  thread_exit();
}