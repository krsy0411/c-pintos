#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);


/* console 출력 (putbuf) */
void putbuf (const char *s, size_t n);

/* 전원 끄기 (halt용) */
void power_off (void);

/* 프로세스/스레드 종료 */
void process_exit (void);
void thread_exit (void) NO_RETURN;

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

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

// exit, halt, write
static void sys_exit(int status) NO_RETURN;
static void sys_halt(void);
static int sys_write(int fd, const void *buf, unsigned len);

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax) {
		case SYS_EXIT: {
			int status = (int)f->R.rdi;
			sys_exit(status);
			break;
		}
		case SYS_HALT: {
			sys_halt();
			break;
		}
		case SYS_WRITE: {
			int fd = (int)f->R.rdi;
			const void *buf = (const void*)f->R.rsi;
			unsigned len = (unsigned)f->R.rdx;
			f->R.rax = (uint64_t)sys_write(fd, buf, len);
			break;
		}
		default:
		sys_exit(-1);
	}
}

static void sys_exit(int status) {
	struct thread *cur = thread_current();
	printf("%s: exit(%d)\n", cur->name, status);

	process_on_exit(status);   // ★ 부모에게 종료/코드 알리기
	process_exit();
	thread_exit();
}

static void sys_halt (void) {
	power_off();
}

static int sys_write(int fd, const void *buf, unsigned len) {
	if (fd != 1) return -1;
	if (len == 0) return 0;

	putbuf((const char*)buf, (size_t)len);
	return (int)len;
}