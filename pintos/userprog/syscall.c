#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "devices/input.h"
#include "filesys/directory.h"
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
#include "userprog/process.h"

#define FDT_SIZE 128
typedef int pid_t;

void syscall_entry(void);
void syscall_handler(struct intr_frame*);

/* System call function declarations */
void exit(int status);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int open(const char* file);
int filesize(int fd);
int read(int fd, void* buffer, unsigned size);
int write(int fd, const void* buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
pid_t fork(const char* thread_name);

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame* f UNUSED) {
  int syscall_number = (int)f->R.rax;

  switch (syscall_number) {
    case SYS_HALT: {
      power_off();
      break;
    }
    case SYS_EXIT: {
      int status = (int)f->R.rdi;
      exit(status);
      break;
    }
    case SYS_WRITE: {
      f->R.rax =
          write((int)f->R.rdi, (const void*)f->R.rsi, (unsigned)f->R.rdx);
      break;
    }
    case SYS_READ: {
      f->R.rax = read((int)f->R.rdi, (void*)f->R.rsi, (unsigned)f->R.rdx);
      break;
    }
    case SYS_SEEK: {
      int fd = (int)f->R.rdi;
      unsigned position = (unsigned)f->R.rsi;
      seek(fd, position);
      break;
    }
    case SYS_CREATE: {
      f->R.rax = create((const char*)f->R.rdi, (unsigned)f->R.rsi);
      break;
    }
    case SYS_REMOVE: {
      f->R.rax = remove((const char*)f->R.rdi);
      break;
    }
    case SYS_FILESIZE: {
      f->R.rax = filesize((int)f->R.rdi);
      break;
    }
    case SYS_TELL: {
      int fd = (int)f->R.rdi;
      f->R.rax = tell(fd);
      break;
    }
    case SYS_OPEN: {
      f->R.rax = open((const char*)f->R.rdi);
      break;
    }
    case SYS_CLOSE: {
      close((int)f->R.rdi);
      break;
    }
    case SYS_FORK: {
      f->R.rax = fork((const char*)f->R.rdi);
      break;
    }
    default: {
      printf("system call 오류 : 알 수 없는 시스템콜 번호 %d\n",
             syscall_number);
      thread_exit();
    }
  }
}

void exit(int status) {
  struct thread* curr = thread_current();
#ifdef USERPROG
  curr->exit_status = status;
  printf("%s: exit(%d)\n", curr->name, curr->exit_status);
#endif
  thread_exit();
}

bool create(const char* file, unsigned initial_size) {
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

  bool ok = filesys_create(fname, initial_size);
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

void seek(int fd, unsigned position) {
  if (!fd || fd < 2 || fd >= FDT_SIZE) return;

  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) return;

  file_seek(file, position);
}

unsigned tell(int fd) {
  if (!fd || fd < 2 || fd >= FDT_SIZE) return -1;

  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) return -1;

  return file_tell(file);
}

int write(int fd, const void* buffer, unsigned size) {
  // fd가 1이면 콘솔에 출력
  if (fd == 1) {
    if ((size == 0) || (buffer == NULL)) return 0;
    putbuf(buffer, size);
    return size;
  }

  // 버퍼가 NULL이거나 size가 0이면 0 반환
  if ((size == 0) || (buffer == NULL)) return 0;

  // 잘못된 fd인 경우 리턴
  if (!fd || fd < 2 || fd >= FDT_SIZE) return -1;

  // 버퍼가 유효한 사용자 주소인지 확인
  for (unsigned i = 0; i < size; i++) {
    if (!is_user_vaddr((uint8_t*)buffer + i)) {
      exit(-1);
    }
    if (!pml4_get_page(thread_current()->pml4, (uint8_t*)buffer + i)) {
      exit(-1);
    }
  }

  // fdt에서 fd에 해당하는 파일 구조체 얻기
  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) return -1;

  // 실제 쓰기 및 반환
  int bytes_written = file_write(file, buffer, size);
  return bytes_written;
}
/* 유저 포인터 `usrc`로부터 size 바이트를 커널 버퍼 `dst`로 복사한다.
   성공하면 true, 실패하면 false를 반환한다. */
bool copyin(void* dst, const void* usrc, size_t size) {
  if (!is_user_vaddr(usrc) || !pml4_get_page(thread_current()->pml4, usrc)) {
    return false;
  }
}
int read(int fd, void* buffer, unsigned size) {
  int bytes_read = 0;

  if (fd == 0) {
    // stdin에서 읽기 전에 버퍼 유효성 검사
    for (unsigned i = 0; i < size; i++) {
      if (!is_user_vaddr((uint8_t*)buffer + i)) {
        exit(-1);
      }
      if (!pml4_get_page(thread_current()->pml4, (uint8_t*)buffer + i)) {
        exit(-1);
      }
    }

    // stdin에서 읽기
    for (unsigned i = 0; i < size; i++) {
      *((uint8_t*)buffer + i) = (uint8_t)input_getc();
    }
    bytes_read = size;
  } else {
    // 잘못된 fd인 경우 리턴
    if (!fd || fd < 2 || fd >= FDT_SIZE) return -1;

    // 버퍼가 유효한 사용자 주소인지 확인
    for (unsigned i = 0; i < size; i++) {
      if (!is_user_vaddr((uint8_t*)buffer + i)) {
        exit(-1);
      }
      if (!pml4_get_page(thread_current()->pml4, (uint8_t*)buffer + i)) {
        exit(-1);
      }
    }

    // fdt에서 fd에 해당하는 파일 구조체 얻기
    struct thread* curr = thread_current();
    struct file* file = curr->fdt[fd];

    if (file == NULL) return -1;

    // file_read() 함수 호출
    bytes_read = file_read(file, buffer, size);
  }

  return bytes_read;
}

int open(const char* file) {
  struct thread* curr = thread_current();

  if (!file) {
    exit(-1);
  }
  if (!is_user_vaddr(file)) {
    exit(-1);
  }

  // 사용자 문자열을 커널 공간으로 복사
  char kernel_file[256];
  int i = 0;
  while (i < 255) {
    if (!is_user_vaddr((void*)(file + i))) {
      exit(-1);
    }
    if (!pml4_get_page(curr->pml4, (void*)(file + i))) {
      exit(-1);
    }

    kernel_file[i] = file[i];

    if (file[i] == '\0') {
      break;
    }
    i++;
  }

  // 파일 열기
  struct file* f = filesys_open(kernel_file);
  if (!f) {
    return -1;
  }

  // 파일 디스크립터 할당
  int fd = 2;
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

int filesize(int fd) {
  if (!fd || fd < 2 || fd >= FDT_SIZE) return -1;

  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) return -1;

  return file_length(file);
}

void close(int fd) {
  // fd 유효성 검사 - stdin(0), stdout(1)은 닫으면 안됨
  if (fd < 2 || fd >= FDT_SIZE) {
    return;
  }

  // 파일 구조체 가져오기
  struct thread* curr = thread_current();
  struct file* file = curr->fdt[fd];

  if (file == NULL) {
    return;
  }

  // 실제 파일 닫기
  file_close(file);

  // fdt에서 제거
  curr->fdt[fd] = NULL;
}

pid_t fork(const char* thread_name) {
  // 1. 주소 유효성 검사
  if (thread_name == NULL || !is_user_vaddr(thread_name) ||
      !pml4_get_page(thread_current()->pml4, thread_name)) {
    exit(-1);
  }

  // 2. 전체 문자열 유효성 검사
  int len = 0;
  int MAX_LEN = 16;  // 최대 길이 제한(16자)
  while (len < MAX_LEN) {
    if (!is_user_vaddr(thread_name + len) ||
        !pml4_get_page(thread_current()->pml4, thread_name + len)) {
      exit(-1);
    }
    if (thread_name[len] == '\0') break;
    len++;
  }

  // 3. 부모의 인터럽트 프레임 주소
  struct intr_frame* parent_if = &thread_current()->tf;

  // 4. 자식 프로세스 생성
  pid_t child_pid = process_fork(thread_name, parent_if);

  return child_pid;
}