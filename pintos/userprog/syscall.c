#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void seek(int fd, unsigned position);
unsigned tell(int fd);
int write(int fd, const void *buffer, unsigned size);
int read(int fd, void *buffer, unsigned size);
int open(const char *file_name);

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
void syscall_handler(struct intr_frame *f UNUSED) {
  /* 시스템 콜 번호에 따라 적절한 핸들러 호출 */
  int syscall_number =
      f->R.rax;  // rax 레지스터에 시스템콜 번호가 저장되어 있음

  switch (syscall_number) {
    case SYS_HALT: {
      power_off();
      break;
    }
    case SYS_EXIT: {
      int status = (int)f->R.rdi;
      struct thread *curr = thread_current();
#ifdef USERPROG
      curr->exit_status = status;
#endif
      thread_exit();
      break;
    }
    case SYS_WRITE: {
      f->R.rax =
          write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned)f->R.rdx);
      break;
    }
    case SYS_READ: {
      f->R.rax = read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
      break;
    }
    case SYS_OPEN: {
      f->R.rax = open((const char *)f->R.rdi);
      break;
    }
    case SYS_SEEK: {
      // 인자들 저장하고 함수 호출(인자2개)
      int fd = (int)f->R.rdi;
      unsigned position = (unsigned)f->R.rsi;
      seek(fd, position);
      break;
    }
    case SYS_EXEC: {
      // todo: implement
      break;
    }
    case SYS_TELL: {
      // 인자 저장하고 함수 호출(인자 1개)
      int fd = (int)f->R.rdi;
      f->R.rax = tell(fd);  // 반환값 rax에 저장
      break;
    }
    default: {
      printf("system call 오류 : 알 수 없는 시스템콜 번호 %d\n",
             syscall_number);
      thread_exit();
      break;
    }
  }
}

// process_get_file() 함수 구현하면 아래 함수들에서 사용 가능
void seek(int fd, unsigned position) {
  // 잘못된 fd인 경우 리턴
  if (!fd || fd < 2 || fd >= 128) return;

  // fdt에서 fd에 해당하는 파일 구조체 얻기
  struct thread *curr = thread_current();
  struct file *file = curr->fdt[fd];

  if (file == NULL) return;

  // file_seek() 함수 호출
  file_seek(file, position);
}

unsigned tell(int fd) {
  if (!fd || fd < 2 || fd >= 128) return -1;

  struct thread *curr = thread_current();
  struct file *file = curr->fdt[fd];

  if (file == NULL) return -1;

  return file_tell(file);
}

int write(int fd, const void *buffer, unsigned size) {
  /* fd가 1이면 콘솔에 출력 : putbuf() 함수를 1번만 호출해서 전체 버퍼를 출력 */
  if (fd == 1) {
    if ((size == 0) || (buffer == NULL)) return 0;  // 잘못된 경우 0 반환

    putbuf(buffer, size);
    return size;  // 출력한 바이트 수 반환
  }

  /* ⭐️⭐️⭐️ 파일 쓰기 : 파일 크기 확장 불가 ⭐️⭐️⭐️ */
  // struct file *file =
  //     process_get_file(fd); /* 파일 디스크립터로부터 파일 구조체 얻기 */
  // if (file == NULL || buffer == NULL || size == 0) return 0;

  // // 파일 끝까지 최대한 많이 쓰기
  // off_t length = file_length(file);  // 파일 전체 크기
  // off_t file_pos = file_tell(file);  // 현재 파일 포인터 위치
  // unsigned max_write_size = 0;       // 실제로 쓸 수 있는 최대 바이트 수

  // // 파일 끝까지 쓸 수 있는 바이트 수 계산
  // if (file_pos < length) {
  //   // 파일 포인터가 파일 끝보다 앞에 있는 경우 : 남는 공간만큼 사용 가능
  //   max_write_size = length - file_pos;

  //   if (size < max_write_size)
  //     // 남는 공간보다 요청 크기가 더 작으면 : 요청 크기만큼만 사용
  //     max_write_size = size;
  // } else {
  //   max_write_size = 0;
  // }

  // // 실제 쓰기 및 반환 : max_write_size만큼만 사용
  // unsigned bytes_written = file_write(file, buffer, max_write_size);
  // return bytes_written;

  // 파일 쓰기 기능이 구현되지 않았으므로 임시로 -1 반환
  return -1;
}

int read(int fd, void *buffer, unsigned size) {
  int bytes_read = 0;

  // 버퍼 유효성 검사 (모든 fd에 대해 먼저 수행)
  if (buffer == NULL) return -1;
  if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size - 1)) {
    // 잘못된 포인터 접근 시 프로세스 종료
    struct thread *curr = thread_current();
#ifdef USERPROG
    curr->exit_status = -1;
#endif
    thread_exit();
  }

  // size가 0인 경우는 정상적인 요청으로 0 반환
  if (size == 0) return 0;

  if (fd == 0) {
    // stdin에서 읽기(표준 입력)
    for (unsigned i = 0; i < size; i++) {
      *((uint8_t *)buffer + i) = (uint8_t)input_getc();
    }
    bytes_read = size;
  } else {
    // fd 유효성 검사 강화
    if (fd < 0 || fd >= 128) return -1;  // 음수이거나 너무 큰 fd
    if (fd == 1 || fd == 2) return -1;   // stdout, stderr은 읽기 불가

    // TODO: 파일 디스크립터 찾기 및 파일 읽기
    // struct file_descriptor *curr_fd = find_file_descriptor(fd);
    // if (curr_fd == NULL) return -1;
    // bytes_read = file_read(curr_fd->file, buffer, size);

    // 임시로 -1 반환 (파일 시스템 구현 필요)
    bytes_read = -1;
  }

  return bytes_read;
}

int open(const char *file_name) {
  // 파일명 유효성 검사
  if (file_name == NULL) return -1;
  if (!is_user_vaddr(file_name)) return -1;

  // 파일시스템을 통해 파일 열기
  struct file *file = filesys_open(file_name);
  if (file == NULL) return -1;

  // 간단한 fd 할당 (2부터 시작, stdin=0, stdout=1)
  // TODO: 나중에 fdt 관리로 개선
  static int next_fd = 2;
  int fd = next_fd++;

  // 현재는 fdt가 없으므로 파일을 바로 닫음 (임시)
  // TODO: fdt 구현 후 파일 저장
  file_close(file);

  return fd;  // 임시로 fd만 반환
}