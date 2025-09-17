#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
void syscall_entry(void);
void syscall_handler(struct intr_frame*);
int write(int fd, const void* buffer, unsigned size);
int open(const char* file);
void seek(int fd, unsigned position);
unsigned tell(int fd);

bool create(const char* file, unsigned initial_size);
bool remove(const char* file);

void exit(int status);

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
void syscall_handler(struct intr_frame* f UNUSED) {
  /* 시스템 콜 번호에 따라 적절한 핸들러 호출 */
  int syscall_number =
      (int)f->R.rax;  // rax 레지스터에 시스템콜 번호가 저장되어 있음

  switch (syscall_number) {
    case SYS_HALT:
      power_off();
      break;
    case SYS_EXIT: {
      int status = (int)f->R.rdi;
      exit(status);
      break;
    }
    case SYS_WRITE:
      f->R.rax =
          write((int)f->R.rdi, (const void*)f->R.rsi, (unsigned)f->R.rdx);
      break;
    case SYS_SEEK: {
      // 인자들 저장하고 함수 호출(인자2개)
      int fd = (int)f->R.rdi;
      unsigned position = (unsigned)f->R.rsi;
      seek(fd, position);
      break;
    }
    /* 파일 생성 */
    case SYS_CREATE:
      f->R.rax = create((const char*)f->R.rdi, (unsigned)f->R.rsi);
      break;
    /* 파일 삭제 */
    case SYS_REMOVE:
    case SYS_EXEC:
    // todo: implement
    case SYS_TELL: {
      // 인자 저장하고 함수 호출(인자 1개)
      int fd = (int)f->R.rdi;
      f->R.rax = tell(fd);  // 반환값 rax에 저장
      break;
    }
    case SYS_OPEN:
      f->R.rax = open((const char*)f->R.rdi);
      break;
    default:
      printf("system call 오류 : 알 수 없는 시스템콜 번호 %d\n",
             syscall_number);
      thread_exit();
  }
}

/* 파일 생성 함수 */
bool create(const char* file, unsigned initial_size) {
  /* 파일이 없으면 프로세스 종료 */
  if (file == NULL) {
    exit(-1);  // false 리턴 금지
  }

  char fname[NAME_MAX + 1];
  size_t fname_len = 0;

  for (;;) {
    const char* u = file + fname_len;

    /* 유저 영역 검사 */
    if (!is_user_vaddr(u)) {
      exit(-1);  // false 리턴 금지
    }

    /* 안전하게 읽기 */
    uint8_t* k = pml4_get_page(thread_current()->pml4, u);
    if (k == NULL) {
      exit(-1);  // false 리턴 금지
    }

    uint8_t b = *k;
    if (b == '\0') break;

    if (fname_len >= NAME_MAX) {
      return false;  // create-long → false
    }

    fname[fname_len++] = (char)b;
  }

  fname[fname_len] = '\0';

  if (fname_len == 0) {
    return false;  // 빈 문자열은 실패
  }

  bool ok;

  ok = filesys_create(fname, initial_size);

  return ok;
}

bool remove(const char* file) {
  if (file == NULL) {
    exit(-1);
  }

  char fname[NAME_MAX + 1];
  size_t fname_len = 0;

  for (;;) {
    const char* u = file + fname_len;

    if (!is_user_vaddr(u)) {
      exit(-1);
    }

    uint8_t* k = pml4_get_page(thread_current()->pml4, u);
    if (k == NULL) {
      exit(-1);
    }

    uint8_t b = *k;
    if (b == '\0') break;

    if (fname_len >= NAME_MAX) {
      return false;
    }

    fname[fname_len++] = (char)b;
  }

  fname[fname_len] = '\0';

  if (fname_len == 0) {
    return false;
  }

  bool ok = filesys_remove(fname);
  return ok;
}

// process_get_file() 함수 구현하면 아래 함수들에서 사용 가능
void seek(int fd, unsigned position) {
  // 잘못된 fd인 경우 리턴
  if (!fd || fd < 2 || fd >= 128) return;

  // fdt에서 fd에 해당하는 파일 구조체 얻기
  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) return;

  // file_seek() 함수 호출
  file_seek(file, position);
}

unsigned tell(int fd) {
  if (!fd || fd < 2 || fd >= 128) return -1;

  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) return -1;

  return file_tell(file);
}

int write(int fd, const void* buffer, unsigned size) {
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
}

int open(const char* file) {
  struct thread* curr = thread_current();

  // 파일 유효성 검사
  // 1. 기본 포인터 검증
  if (!file) {
    exit(-1);
  }
  // 2. 사용자 영역 확인
  if (!is_user_vaddr(file)) {
    exit(-1);
  }

  // 사용자 문자열을 커널 공간으로 복사
  char kernel_file[256];

  int i = 0;
  while (i < 255) {
    // 각 바이트마다 주소 유효성 검사

    if (!is_user_vaddr((void*)(file + i))) {
      exit(-1);
    }
    // pml4_get_page로 매핑 확인
    if (!pml4_get_page(curr->pml4, (void*)(file + i))) {
      exit(-1);
    }
    if (!is_user_vaddr((void*)(file + i))) {
      exit(-1);
    }
    // pml4_get_page로 매핑 확인
    if (!pml4_get_page(curr->pml4, (void*)(file + i))) {
      exit(-1);
    }

    // 안전하게 복사
    kernel_file[i] = file[i];

    // 문자열 끝 확인
    if (file[i] == '\0') {
      break;
    }
    i++;
  }

  // 파일 열기
  struct file* f = filesys_open(kernel_file);
  if (!f) {
    return -1;  // 파일 열기 실패
  }

  // 파일 디스크립터 할당
  int fd = 2;
  // fdt의 끝까지 탐색하는 while
  while (fd < FDT_SIZE) {
    if (curr->fdt[fd] == NULL) {
      curr->fdt[fd] = f;
      return fd;
    }
    fd++;
  }

  file_close(f);
  return -1;
}

void exit(int status) {
  struct thread* curr = thread_current();
#ifdef USERPROG
  curr->exit_status = status;
#endif
  thread_exit();
}