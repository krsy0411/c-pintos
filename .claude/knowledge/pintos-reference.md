# Pintos (KAIST) 레퍼런스 노트

출처: https://casys-kaist.github.io/pintos-kaist/ 의 공식 문서를 요약/정리한 것. 이 문서는 그 웹사이트와 `docs/Pintos_{2,3,4}.pdf`를 기반으로 하며, 이 둘에서 확인되지 않는 주장은 여기에 기록하지 않는다.

---

## 0. Overview

- Pintos는 x86-64 아키텍처용 교육용 OS 프레임워크. QEMU 시뮬레이터 위에서 실행된다.
- 학생은 커널의 thread, user program, virtual memory, file system 지원을 강화한다.
- KAIST 버전은 64-bit를 지원하며, 원본 Stanford Pintos와 구조 차이가 있다.

### 환경
- Tested: Ubuntu 16.04.6 LTS, gcc 7.4.0, QEMU 2.5.0 (공식 문서 기준)
- 본 저장소는 Docker + DevContainer(ubuntu 22.04)로 구성되어 있음 (`./README.md` 참조).

### 디렉터리 구조 (pintos/ 하위)
- `threads/` — 베이스 커널 코드, Project 1 구현
- `userprog/` — 유저 프로그램 로더/syscall, Project 2+
- `vm/` — 가상 메모리, Project 3
- `filesys/` — 파일시스템, Project 2+에서 사용, Project 4에서 수정
- `devices/` — I/O 장치 (타이머, 블록, 콘솔 등)
- `lib/`, `include/lib/kernel/`, `include/lib/user/` — C 표준 라이브러리 서브셋
- `tests/` — 프로젝트별 테스트
- `examples/` — 유저 프로그램 샘플 (Project 2+)

### 빌드 & 실행
```bash
source ./activate           # pintos 유틸리티 PATH 설정 (pintos 루트에서)
cd threads                  # 또는 userprog/, vm/, filesys/
make                        # build/ 디렉터리 생성
cd build
pintos run alarm-multiple               # 특정 테스트 실행
pintos -- run alarm-multiple > logfile  # 출력 기록
```

- `build/` 산출물: `Makefile`, `kernel.o` (디버그 심볼 포함), `kernel.bin` (스트립됨), `loader.bin` (512-byte 로더)
- `pintos` 옵션: `-v` (VGA 비활성), `-s` (stdin/stdout 억제)

### 테스트 & 채점
- `make check`를 각 프로젝트 `build/`에서 실행. 각 테스트는 `.result` 파일 생성.
- 개별 테스트: `make tests/<test-name>.result`
- `VERBOSE=1` 지원.
- **채점은 테스트 결과 기반.** 테스트 이름을 하드코딩하지 말 것. 일반 케이스에서 동작해야 함.
- 제출: `TEAM=[NUMBER] make archive` → `team<n>.tar.gz`.

---

## 1. Project 1 — Threads

작업 디렉터리: 주로 `threads/`, 일부 `devices/`.

### 수정 대상
- 주 파일: `threads/thread.{c,h}`, `devices/timer.{c,h}`
- 필요 시: `threads/init.{c,h}`, `threads/synch.c`

### 이미 제공됨
- 스레드 생성/종료, 기본 스케줄러, 동기화 primitive(semaphore/lock/cond var/barrier), page/block 할당자, 인터럽트 인프라.

### 동작 기본 규칙
- 임의 시점 정확히 한 스레드만 실행. 스케줄러가 다음 실행 대상을 결정.
- 스택은 작고 고정 크기(약 4 kB 미만).
- 바쁜 대기(busy-wait, tight loop + `thread_yield()`) 금지.
- 동기화는 primitive를 사용. 인터럽트 비활성화는 스레드↔인터럽트 핸들러 간 데이터 공유에만 허용.

### Task 1: Alarm Clock
- 재구현 대상: `devices/timer.c`의 `timer_sleep(int64_t ticks)`.
- 현재 구현은 busy-waiting. 이를 제거하고 슬립 큐 방식으로 전환.
- 시간 단위는 timer tick. `TIMER_FREQ` 기본 100 Hz.
- 정확한 웨이크업 시점 불필요. 해당 tick 이후 ready queue에 올려주면 됨.
- `timer_msleep/usleep/nsleep`은 `timer_sleep`을 자동 호출함.

### Task 2: Priority Scheduling
- 우선순위 범위: `PRI_MIN (0)` ~ `PRI_MAX (63)`, 기본 `PRI_DEFAULT (31)`.
- API:
  ```c
  void thread_set_priority(int new_priority);
  int  thread_get_priority(void);
  ```
- `thread_set_priority`: 현재 스레드가 더 이상 최고 우선순위가 아니면 yield.
- `thread_get_priority`: donation 중이면 donated(더 높은) 값을 반환.
- **Priority Donation 요구사항**
  1. Basic: 고우선순위 스레드가 저우선순위 스레드가 보유한 리소스를 기다리면, 보유 스레드에게 일시적으로 우선순위를 기부.
  2. Multiple: 여러 스레드가 한 스레드에게 동시에 기부하는 경우 처리.
  3. Nested: H → M → L 연쇄 기부 지원. 최대 깊이 8까지 허용.
  4. **Lock에서는 필수**, semaphore/cond var에서는 선택.
- lock/semaphore/cond var 대기 시 항상 최고 우선순위 스레드가 먼저 깨어나야 함.

### Task 3: Advanced Scheduler (4.4BSD MLFQS)
- 커널 옵션 `-mlfqs`로 활성화 (`thread_mlfqs` 플래그 true).
- 수식:
  - 매 4 tick마다: `priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)` (절단 후 [0,63]로 클램프)
  - 매 1초마다(`timer_ticks() % TIMER_FREQ == 0`):
    - `recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice`
    - `load_avg = (59/60) * load_avg + (1/60) * ready_threads` (ready_threads는 실행 중/준비 상태, idle 제외)
- 매 tick마다 현재 실행 중인 스레드(idle 제외)의 `recent_cpu` +1.
- Nice: -20 ~ +20, 부모로부터 상속.
- 초기값: `load_avg = 0`, `recent_cpu = 0` (또는 부모로부터 상속).
- MLFQS 활성 시 `thread_create`의 priority 인자 무시, priority setter 무효화.
- **고정소수점 17.14** (f = 2^14 = 16384):
  - int→fp: `n * f`
  - fp→int: `x / f` (round toward zero)
  - fp*fp: `((int64_t) x) * y / f`
  - fp/fp: `((int64_t) x) * f / y`

---

## 2. Project 2 — User Programs

작업 디렉터리: 주로 `userprog/`. Project 1 위에 이어서 빌드.

### 수정 대상
- `process.c/h` — ELF 로드 및 프로세스 시작
- `syscall.c/h` — 시스템콜 핸들러
- `exception.c/h` — 페이지 폴트/예외

### 건드리지 말 것
- `syscall-entry.S`, `gdt.{c,h}`, `tss.{c,h}`, `filesys/` 내부 (파일시스템 인터페이스 `filesys.h`, `file.h`만 사용).
- Project 3 전에는 `#ifdef VM` 블록에 코드를 넣지 말 것.

### 가상 주소 레이아웃
- 유저 공간: 0 ~ `KERN_BASE` (= 0x8004000000)
- 커널 공간: `KERN_BASE` 이상, 물리 메모리에 1:1 매핑
- 유저 코드 시작: 0x400000 (주소 공간 하단에서 약 128 MB)

### Task: Argument Passing
- x86-64 System V ABI 레지스터 인자 순서: `%rdi, %rsi, %rdx, %rcx, %r8, %r9` (커널은 `%rcx` 대신 `%r10` 사용 주의)
- 유저 프로그램 시작 전 커널이 스택 셋업:
  1. 명령줄 인자 문자열을 스택 최상단에 배치
  2. 각 문자열 주소 + null sentinel을 **right-to-left**로 푸시 (argv 배열)
  3. 스택 포인터를 8바이트 배수로 정렬 (round down)
  4. `%rsi = &argv[0]`, `%rdi = argc`
  5. 가짜 리턴 주소 0을 최하단에 푸시
- argv[0]이 최하위 가상 주소에 위치, `argv[argc]` = NULL sentinel.
- `process_exec`를 확장하여 입력 문자열을 공백으로 토크나이즈. `strtok_r()` 사용 권장.
- 인자 길이는 4 KB 한 페이지로 제한해도 무방.

### Task: System Calls
- 레지스터 규칙:
  - 번호: `%rax`
  - 인자: `%rdi, %rsi, %rdx, %r10, %r8, %r9`
  - 반환값: `struct intr_frame`의 `rax` 멤버에 저장
- FD 0 = STDIN, FD 1 = STDOUT (콘솔)
- 파일시스템은 내부 동기화가 없으므로 syscall 측에서 락 등으로 **상호배제 보장 필요**.
- 필수 syscall 목록:
  | Syscall | 요약 |
  |---|---|
  | `halt()` | `power_off()` 호출, Pintos 종료 |
  | `exit(int status)` | 현재 프로세스 종료, 커널에 status 반환 |
  | `fork(const char *thread_name)` | 자식 프로세스 복제. 부모=자식 pid, 자식=0. 실패 시 TID_ERROR |
  | `exec(const char *cmd_line)` | 현재 프로세스를 새 실행파일로 교체. 성공 시 리턴 없음, 실패 시 exit(-1) |
  | `wait(pid_t pid)` | 자식 종료 대기, exit status 회수. 잘못된/중복 wait 시 -1 |
  | `create(const char *file, unsigned initial_size)` | 파일 생성, bool 반환 |
  | `remove(const char *file)` | 파일 삭제, bool 반환 |
  | `open(const char *file)` | 파일 열기, fd(≥0) 또는 -1 |
  | `filesize(int fd)` | 파일 크기(byte) |
  | `read(int fd, void *buffer, unsigned size)` | 읽은 바이트 수 또는 -1 |
  | `write(int fd, const void *buffer, unsigned size)` | 쓴 바이트 수 |
  | `seek(int fd, unsigned position)` | 파일 위치 변경 |
  | `tell(int fd)` | 현재 위치 반환 |
  | `close(int fd)` | FD 닫기 |

### Task: User Memory Access
- 두 가지 접근법이 허용됨:
  1. **사전 검증**: 포인터를 역참조하기 전 `thread/mmu.c` 유틸리티로 유효성 검사.
  2. **페이지 폴트 복구**: 포인터가 `KERN_BASE` 이하인지 먼저 확인, 실제 접근 시 페이지 폴트를 `page_fault()`에서 회수 처리.
- 커널 포인터, 부분 겹치는 영역, 미매핑 영역은 모두 거부 → 해당 프로세스 `exit(-1)`.
- 관련 인라인 어셈블리 스니펫(`get_user`, `put_user`)은 공식 문서의 "User Memory Access" 섹션 참조. (이번 스냅샷에서는 본문을 받아오지 못했으니 작업 시 재확인할 것.)

### Task: Process Termination Message
- `exit(status)` 또는 커널에 의해 종료 시, `printf ("%s: exit(%d)\n", process_name, exit_code);` 형식의 메시지를 출력.
  (구체 포맷은 공식 페이지와 테스트 확인 필요.)

### Task: Deny Write to Executables
- 실행 중인 프로세스의 executable에 쓰기 방지. `file_deny_write()` 사용 패턴 확인.

### Task: Extend File Descriptor (Optional, `dup2`)
- 추가 크레딧. `dup2(int oldfd, int newfd)` 지원 → FD 테이블 구조 확장 필요.

### File System 제약(작업 시 우회해야 할 요소)
- 내부 동기화 없음 (syscall에서 락 필요)
- 파일 크기 고정, sector 연속 할당
- 서브디렉터리 없음, 파일명 14자 제한

### 📄 KAIST 강의자료 보강 (`docs/Pintos_2.pdf`)

**유저 프로그램 실행 전체 코드 플로우**

커널 부팅 → 유저 프로그램 진입까지 따라야 할 함수 체인:
```
main()                         threads/init.c
 └─ run_actions()              threads/init.c
     └─ process_create_initd() userprog/process.c
         └─ thread_create()    threads/thread.c
             └─ kernel_thread() (스레드 시작 루틴)
                 └─ initd()    userprog/process.c
                     └─ process_exec()  userprog/process.c
                         └─ load()      userprog/process.c   /* ELF 로드, code/data/user stack 설정 */
                             └─ do_iret()                    /* 커널→유저 전환 */
```

**커널→유저 전환 (`do_iret`)의 마법**

`do_iret()`은 `struct intr_frame`을 구성해 `iret` 명령어를 실행한다. `iret` 시점의 스택 레이아웃:
```
low addr  RSP  →  RIP       (유저 진입 주소)
                  CS        (code segment selector)
                  EFLAGS    (플래그 레지스터 값)
                  RSP       (로드할 유저 스택 포인터)
                  SS        (stack segment selector)
high addr
```
`iret` 실행 이후 CPU는 RIP가 가리키는 유저 엔트리로 점프.

**Pintos 가상 주소 상수 (강의자료 확인)**
- `KERN_BASE = 0x8004000000`
- `USER_STACK = 0x47480000`
- 유저 코드 시작 주소: `0x0400000`

**Parameter Passing 실제 예시**

`/bin/ls -l foo bar` 입력 처리:
- `strtok_r` (`lib/string.c`)로 토크나이즈 → `"/bin/ls"`, `"-l"`, `"foo"`, `"bar"`
- 유저 스택에 인자 배치 후 x86-64 ABI 준수: `%rsi → &argv[0]`, `%rdi → argc`
- 배치 완료 후 user main(int argc, char *argv[])이 진입

**`fork` / `__do_fork` 구현 포인트**
- `__do_fork()`에서 `duplicate_pte()`를 사용해 부모 페이지를 자식 페이지 테이블에 복제.
- 부모/자식은 동일한 물리 메모리 내용으로 시작해야 함.
- **부모가 열어둔 파일 자원(FD 포함)을 자식에게 상속**해야 함.
- `duplicate_pte()` 구현 시 질문:
  - 어떤 API를 써서 페이지를 복제할 것인가?
  - `parent_page`의 내용을 새 child page로 어떻게 복사할 것인가?
  - (Intel x86 page table 구조 참고)

**Project 2에서 해야 할 일 (강의자료 체크리스트)**
1. Safe user-memory access
2. System calls
3. Process exit message
4. Deny writes to executable memory
5. 파일시스템 API에 익숙해지기

---

## 3. Project 3 — Virtual Memory

작업 디렉터리: 주로 `vm/`, 일부 `include/vm/`, `userprog/exception.c`, `devices/block.{c,h}`.

### 수정 대상
- `include/vm/vm.h`, `vm/vm.c` — 일반 VM 인터페이스, SPT
- `include/vm/uninit.h`, `vm/uninit.c` — 미초기화 페이지
- `include/vm/anon.h`, `vm/anon.c` — 익명 페이지
- `include/vm/file.h`, `vm/file.c` — 파일 백업 페이지
- `userprog/exception.c` — `vm_try_handle_fault()`

### 핵심 구조
```c
struct page {
    const struct page_operations *operations;
    void *va;              /* User-space virtual address */
    struct frame *frame;   /* Back reference to frame */
    union {
        struct uninit_page uninit;
        struct anon_page   anon;
        struct file_page   file;
#ifdef EFILESYS
        struct page_cache  page_cache;
#endif
    };
};
```

- `vm_type`: `VM_UNINIT`, `VM_ANON`, `VM_FILE`, `VM_PAGE_CACHE`(P4용 예약).
- **템플릿 준수 필수** — 어기면 0점.
- 프레임은 오직 `palloc_get_page(PAL_USER)`로만 획득.
- 유저 VA와 커널 VA의 alias 처리에 유의 (dirty/accessed 비트 동기화).

### Task: Supplemental Page Table (SPT)
- SPT는 각 VA별 메타데이터 보관 → 페이지 폴트 시 처리와 프로세스 종료 시 리소스 정리에 사용.
- 필수 함수(`vm/vm.c`):
  ```c
  void supplemental_page_table_init(struct supplemental_page_table *spt);
  struct page *spt_find_page(struct supplemental_page_table *spt, void *va);
  bool spt_insert_page(struct supplemental_page_table *spt, struct page *page);
  ```
- 공식 제안: 해시테이블(O(1) 조회). `lib/kernel/hash.h` 활용.
- `vm_get_frame()` — `palloc_get_page`로 프레임 획득 + 메타데이터 초기화.
- `vm_do_claim_page()` — VA↔PA MMU 매핑. `vm_claim_page()`는 페이지 찾은 뒤 위임.

### Task: Anonymous Pages
- 익명 매핑 = 백업 파일/장치가 없는 매핑 (스택/힙 등).
- 필수:
  ```c
  bool anon_initializer(struct page *page, enum vm_type type, void *kva);
  static void anon_destroy(struct page *page);
  ```
- `anon_initializer`: `page->operations`의 핸들러 설정 + `anon_page` 구조 갱신.
- `anon_destroy`: 해당 페이지가 쥐고 있던 리소스 해제. 페이지 구조체 자체 해제는 호출자 책임.

### Task: Stack Growth
- 페이지 폴트가 "합법적 스택 접근"으로 보이면 페이지 추가 할당.
- 크기 제한: **최대 1 MB**.
- 판별 휴리스틱:
  - 유저 프로그램의 `rsp`는 `struct intr_frame`의 `rsp`에서 얻음.
  - x86-64 PUSH는 스택 포인터 조정 전에 권한 검사 → **rsp에서 8바이트 아래까지**의 폴트는 허용 가능.
  - 커널 모드에서 폴트가 나면, 유저→커널 진입 시 `struct thread`에 저장해둔 `rsp`를 사용.
- 구현 지점:
  - `vm_try_handle_fault()` (`userprog/exception.c`)에서 스택 증가 여부 판정 후 `vm_stack_growth()` 호출.
  - `vm_stack_growth(void *addr)` — `PGSIZE`로 round down 후 anon 페이지 할당.

### Task: Memory-Mapped Files
- Syscall:
  ```c
  void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
  void  munmap(void *addr);
  ```
- `mmap` 실패 조건: 파일 길이 0, `addr` 페이지 비정렬, 기존 매핑과 겹침, `addr == 0`, `length == 0`, fd가 콘솔.
- 성공 시 VA 반환, 실패 시 NULL.
- 마지막 페이지의 남는 바이트는 폴트 시 0으로 초기화.
- `munmap` 시 dirty page만 파일로 기록, 깨끗한 페이지는 쓰지 않음.
- 헬퍼: `do_mmap/do_munmap` (`vm/file.c`), `vm_alloc_page_with_initializer`, `file_reopen()`, `vm_file_init()`, `file_backed_initializer()`, `file_backed_destroy()`.
- 파일 백업 페이지에 대해 **lazy load** 필수.

### Task: Swap In/Out
- 물리 메모리 고갈 시 페이지를 swap 디스크로 축출.
- **Swap slot은 lazy 할당** — 실제 축출이 일어날 때만.
- 익명 페이지:
  - `vm_anon_init()` — swap 디스크 초기화, 4 KB 단위 free/used 관리 자료구조 준비.
  - `anon_initializer()` — `anon_page`에 swap 메타 추가.
  - `anon_swap_out()` — free slot으로 복사, 위치를 page 구조에 저장, 가용 슬롯 없으면 panic.
  - `anon_swap_in()` — 디스크에서 읽어 메모리로 복원, swap table 업데이트.
- 파일 백업 페이지:
  - `file_backed_swap_out()` — dirty면 원본 파일로 writeback 후 dirty bit clear, 깨끗하면 기록하지 않음.
  - `file_backed_swap_in()` — 파일에서 읽어옴. 파일시스템 동기화 필요.
- 두 타입 모두 `struct page_operations`의 함수 포인터로 다형성 처리.

### Task: Copy-on-Write (Optional)
- fork 시 페이지를 복제하지 않고 공유 + 쓰기 보호. 실제 write 시 복제.

### 📄 KAIST 강의자료 보강 (`docs/Pintos_3.pdf`)

**핵심 용어 정리**
- **Page**: 가상 메모리의 연속 영역
- **Frame**: 물리 메모리의 연속 영역
- **Page Table**: VA → PA 변환을 담당하는 자료구조
- **Eviction**: 프레임에서 페이지를 제거하고 필요 시 swap 파티션이나 파일시스템에 기록
- **Swap Table**: 축출된 페이지가 기록되는 swap 파티션 내 슬롯을 추적

**설계해야 할 4개 자료구조 (PDF 기준 명확화)**
1. **Supplemental Page Table** — 프로세스별. 각 페이지의 보조 메타 (데이터 위치: frame/disk/swap, 대응 커널 VA 포인터, active/inactive 상태 등)
2. **Frame Table** — 전역. 물리 프레임의 할당/비할당 상태 추적. **오직 `PAL_USER` 프레임만 관리**.
3. **Swap Table** — swap slot 사용/미사용 추적.
4. **File Mapping Table** — mmap된 파일이 어떤 페이지로 매핑되었는지 추적.

**자료구조 선택지 (PDF 제안)**
- Arrays — 간단하지만 sparsely populated 시 메모리 낭비
- Lists — 간단하나 탐색 비용
- Bitmaps — 동일 자원 집합 추적에 적합 (`lib/kernel/bitmap.[ch]`)
- Hash Tables — SPT에 권장 (`lib/kernel/hash.[ch]`)

**Lazy Loading 동작 모델**
- VA가 생성될 때(mmap 등) Pintos는 `struct page`를 VA에 결합한다. 이 시점에는 **물리 프레임은 할당되지 않음**.
- `struct page`는 VA 용도(익명/파일 백업)에 따라 다른 정보를 담음.
- 페이지는 `uninit_page`로 시작 → 폴트 시점에 타입별 initializer로 전환:
  - 익명 메모리: `anon_initializer`
  - 파일 백업 메모리: `file_map_initializer` (PDF 명명; 웹 문서의 `file_backed_initializer`와 동일 역할)
- "어떤 함수가 타입별 initializer를 결합시키는가?" — 설계 시 반드시 추적.

**Frame Table & Eviction 상세**
- 모든 프레임이 찼을 때 페이지 축출 필요 → page replacement 알고리즘.
- **요구 수준: LRU 근사, 최소 clock algorithm 수준의 성능**.
- `vm_get_victim` — 축출 피해자 선정.
- 축출 절차:
  1. page replacement 알고리즘으로 피해자 프레임 선정
  2. 해당 프레임을 참조하는 페이지 테이블 엔트리 제거
  3. 필요 시 파일시스템(파일 백업) 또는 swap(익명)에 기록
- 참고: CPU가 설정하는 **accessed/dirty 비트를 활용**.

**페이지 타입별 축출 대상**
- User stack 페이지 → **swap**으로 페이지 아웃
- 파일 백업(mmap) 페이지 → **파일시스템**으로 페이지 아웃
  - dirty → 원본 파일에 writeback
  - clean → 단순 deallocate (파일시스템에서 다시 읽어옴)

**Swap Table 책임**
- 사용/미사용 swap slot 추적
- 축출 시 미사용 슬롯 선택
- 페이지가 다시 읽혀오거나 프로세스 종료 시 슬롯 해제

**동기화 주의**
- 여러 스레드가 동시에 page in/out 시도 → 자료구조 보호 필수.

**Stack Growth 시점별 차이**
- Project 2: 유저 프로세스 스택을 **pre-allocate**.
- Project 3: 필요 시 **동적으로** 페이지를 더 할당.
- 이제 "유효한 스택 접근"이 페이지 폴트를 유발할 수 있음.
- 페이지 폴트 핸들러에서 유효 스택 접근이면 새 페이지 할당.
- 유저 `%rsp`는 `page_fault()`에 전달되는 `struct intr_frame`에서 획득.

**Memory-Mapped Files 재확인**
- `mmap()`은 다음 조건에서 에러:
  - 파일 크기 0
  - 기존 매핑 페이지와 겹침
  - `addr`이 페이지 정렬이 아님
- 축출 시 mmap된 페이지의 변경분은 원본 파일에 써야 함.
- 프로세스 종료 시 모든 매핑은 **암묵적으로 unmap**.

**🗺️ PDF가 제안하는 구현 순서**
1. **Frame table만** 구현 (swapping 없이). 이 시점에 **Project 2 테스트는 모두 통과**해야 함.
2. Supplemental page table + page fault handler (lazy load — 코드/데이터 세그먼트를 페이지 폴트로 로드). Project 2 기능 테스트 전부 + 일부 견고성 테스트 통과.
3. Stack growth, memory-mapped files, page reclamation.
4. Eviction. **동기화 주의!**

---

## 4. Project 4 — File System

Project 2 또는 Project 3 위에 빌드. **VM 비활성화 시 10% 감점.**

### 수정 대상
- `filesys/inode.{c,h}` — 디스크상의 파일 데이터 레이아웃
- `filesys/directory.{c,h}` — 이름→inode 변환
- `filesys/fat.{c,h}` — FAT 관리
- `filesys/file.{c,h}` — 읽기/쓰기
- `filesys/page_cache.{c,h}` — 선택적 캐싱 계층

### Task: Indexed and Extensible Files
- KAIST Pintos는 **FAT 기반 인덱싱**. 멀티레벨 인덱싱(강의의 FFS) 코드 **금지(0점)**.
- Cluster 크기 = 1 sector. FAT은 DRAM에 상주. inode는 첫 블록 섹터 번호 보관, FAT이 체인 유지.
- 필수 구현:
  - `fat_fs_init()` — `fat_length`, `data_start` 설정
  - `fat_create_chain()` — 체인에 클러스터 추가
  - `fat_remove_chain()` — 체인 일부 제거
  - `fat_put()/fat_get()` — FAT 엔트리 조작
- 확장성:
  - 파일은 크기 0으로 생성. EOF 너머 쓰기 시마다 확장.
  - seek를 EOF 너머로 하는 것만으로는 확장 안 됨. 쓰기가 있어야 확장.
  - 기존 EOF와 쓰기 위치 사이 gap은 0으로 채움.
  - root directory도 16파일 한계를 넘어 확장 가능해야 함.
- 크기 한계: 파티션 크기(기본 8 MB)에서 메타데이터를 뺀 값.

### Task: Subdirectories and Soft Links
- 필수 syscall:
  - `chdir(const char *dir)` — cwd 변경, 상대/절대 모두.
  - `mkdir(const char *dir)` — 디렉터리 생성. 이미 존재하거나 부모가 없으면 실패.
  - `readdir(int fd, char *name)` — 엔트리 읽기. 성공 시 true, 더 없으면 false. `.`, `..`은 제외.
  - `isdir(int fd)` — 디렉터리 여부.
  - `inumber(int fd)` — inode 섹터 번호 반환.
  - `symlink(const char *target, const char *linkpath)` — 심볼릭 링크 생성. 성공 0, 실패 -1.
- 계층 디렉터리 + 확장 가능한 디렉터리.
- 프로세스마다 별도 cwd 유지. 자식은 부모의 cwd 상속.
- 절대/상대 경로, `/`, `.`, `..` 지원.
- 디렉터리 open 가능. `close`만이 디렉터리 fd를 받을 수 있음.
- 빈 디렉터리(루트 제외) 삭제 가능.
- 심볼릭 링크: target 경로(절대/상대) 저장, 접근 시 리다이렉트.

### Task: Buffer Cache (Extra Credit, VM 기능 활용)
- `page_cache.{c,h}` 구현. VM이 활성화된 상태여야 함.

### Task: Synchronization
- 멀티스레드 환경에서 파일시스템 연산의 데이터 무결성 보장.

### 테스트
- 영속성 테스트: Pintos를 두 번 실행, 두 번째에서 `tar`로 파일시스템 내용 추출.
- `tar`는 extensible file + subdirectory가 모두 필요.

### 📄 KAIST 강의자료 보강 (`docs/Pintos_4.pdf`)

**구현 범위 (PDF 목차 기준)**
1. **파일 인덱싱 API (FAT) 구현** — `filesys/fat.c`
2. **계층적 네임스페이스 구축** — 계층 디렉터리
3. **블록 접근을 FAT 사용으로 수정** — `inode_create`, `inode_open` 등
4. **Softlink** — 심볼릭 링크

**FAT 구조**
- FAT 테이블 크기: `fat_fs->fat_length * sizeof(cluster_t)`
- FAT는 파일시스템 포맷 시 초기화됨.
- `filesys/fat.c`의 API를 프로젝트 문서 기술대로 구현.

**Current Directory (cwd) 관리**
- `struct thread`에 현재 디렉터리를 저장 (`struct dir *` 사용 패턴).
- `cd` 류 시스템콜로 현재 디렉터리 변경 지원.
- `dir_create()`를 확장하여 `.` (현재) 와 `..` (부모) 엔트리를 각 디렉터리에 추가.
- `dir_add()` / `dir_remove()`를 면밀히 검토하여 디렉터리 엔트리 포맷 파악.

**Directory 내부 구조**
- 디렉터리는 `struct dir_entry`의 배열.
- 읽기/수정/쓰기 흐름: read → modify → write, 디스크에는 FAT를 경유해 데이터 기록.

**디렉터리/파일 확장 시**
- inode를 다룰 때 FAT를 사용하도록 `inode_create()`, `inode_open()`을 수정.
- `inode_create()`는 새 inode sector를 만들고, `inode_open()`은 inode sector를 읽음.

**절대 경로 open/remove 지원**
- `open("/a/b/c/d")` 또는 `remove("/a/b/c/d")` 호출 시:
  - 파일시스템이 루트 `"/"`부터 `"/a/b/c/"`까지 워크해서 `d`를 찾아야 함.
  - `filesys_open()`, `filesys_remove()`를 확장하여 이름 문자열이 주어진 디렉터리 계층을 순회하도록 변경.

**Softlink 동작 규칙 (PDF 중요 사항)**
- **심볼릭 링크는 새로운 inode를 생성**한다. 이 inode의 내용은 타겟 inode에서 복제된다.
- 심볼릭 링크 inode도 일반 inode처럼 취급한다:
  - 심볼릭 링크의 이름은 디렉터리에 저장되고, inode 번호는 심볼릭 링크 inode를 가리킴.
  - **심볼릭 링크 삭제는 원본 파일을 삭제하지 않음.**
  - 심볼릭 링크는 디렉터리도 가리킬 수 있음.
  - **타겟 파일이 삭제되어도 심볼릭 링크는 남는다 (dangling symlink).**

---

## 5. Appendix — 자주 쓰는 커널 API

### Synchronization (`include/threads/synch.h`, `include/threads/interrupt.h`)
```c
/* 인터럽트 */
enum intr_level intr_get_level(void);
enum intr_level intr_set_level(enum intr_level level);
enum intr_level intr_enable(void);
enum intr_level intr_disable(void);

/* 세마포어 */
void sema_init(struct semaphore *sema, unsigned value);
void sema_down(struct semaphore *sema);
bool sema_try_down(struct semaphore *sema);
void sema_up(struct semaphore *sema);

/* 락 (initial value 1 세마포어와 유사, 소유자만 해제 가능) */
void lock_init(struct lock *lock);
void lock_acquire(struct lock *lock);
bool lock_try_acquire(struct lock *lock);
void lock_release(struct lock *lock);
bool lock_held_by_current_thread(const struct lock *lock);

/* 조건 변수 (monitor 패턴: 락과 함께 사용) */
void cond_init(struct condition *cond);
void cond_wait(struct condition *cond, struct lock *lock);
void cond_signal(struct condition *cond, struct lock *lock);
void cond_broadcast(struct condition *cond, struct lock *lock);
```
- `barrier()` 매크로: 컴파일러가 메모리 연산 순서를 바꾸지 못하게 함.
- 인터럽트 비활성화는 "최소한의 동기화" — 스레드↔인터럽트 핸들러 간 데이터 공유 외에는 primitive 사용 권장.

### Page Table (`include/threads/mmu.h`)
```c
uint64_t *pml4_create(void);
void      pml4_destroy(uint64_t *pml4);
void      pml4_activate(uint64_t *pml4);

bool      pml4_set_page(uint64_t *pml4, void *upage, void *kpage, bool rw);
void     *pml4_get_page(uint64_t *pml4, const void *uaddr);
void      pml4_clear_page(uint64_t *pml4, void *upage);

bool      pml4_is_dirty(uint64_t *pml4, const void *vpage);
bool      pml4_is_accessed(uint64_t *pml4, const void *vpage);
void      pml4_set_dirty(uint64_t *pml4, const void *vpage, bool dirty);
void      pml4_set_accessed(uint64_t *pml4, const void *vpage, bool accessed);
```

### Hash Table (`include/lib/kernel/hash.h`) — SPT 구현에 유용
```c
bool hash_init(struct hash *hash,
               hash_hash_func *hash_func,
               hash_less_func *less_func,
               void *aux);
void hash_clear(struct hash *hash, hash_action_func *action);
void hash_destroy(struct hash *hash, hash_action_func *action);

struct hash_elem *hash_insert(struct hash *hash, struct hash_elem *elem);  /* NULL이면 신규 삽입 */
struct hash_elem *hash_find(struct hash *hash, struct hash_elem *elem);
struct hash_elem *hash_delete(struct hash *hash, struct hash_elem *elem);

void hash_first(struct hash_iterator *it, struct hash *hash);
struct hash_elem *hash_next(struct hash_iterator *it);
struct hash_elem *hash_cur(struct hash_iterator *it);

#define hash_entry(HASH_ELEM, STRUCT, MEMBER) /* containerof 스타일 매크로 */
```

---

## 6. 공식 문서 URL 인덱스 (상세 확인용 canonical source)

### Introduction
- https://casys-kaist.github.io/pintos-kaist/introduction/getting_started.html
- https://casys-kaist.github.io/pintos-kaist/introduction/grading.html
- https://casys-kaist.github.io/pintos-kaist/introduction/legal_and_ethical_issues.html

### Project 1 (Threads)
- https://casys-kaist.github.io/pintos-kaist/project1/introduction.html
- https://casys-kaist.github.io/pintos-kaist/project1/alarm_clock.html
- https://casys-kaist.github.io/pintos-kaist/project1/priority_scheduling.html
- https://casys-kaist.github.io/pintos-kaist/project1/advanced_scheduler.html
- https://casys-kaist.github.io/pintos-kaist/project1/FAQ.html

### Project 2 (User Programs)
- https://casys-kaist.github.io/pintos-kaist/project2/introduction.html
- https://casys-kaist.github.io/pintos-kaist/project2/argument_passing.html
- https://casys-kaist.github.io/pintos-kaist/project2/user_memory.html
- https://casys-kaist.github.io/pintos-kaist/project2/system_call.html
- https://casys-kaist.github.io/pintos-kaist/project2/process_termination.html
- https://casys-kaist.github.io/pintos-kaist/project2/deny_write.html
- https://casys-kaist.github.io/pintos-kaist/project2/dup.html
- https://casys-kaist.github.io/pintos-kaist/project2/FAQ.html

### Project 3 (Virtual Memory)
- https://casys-kaist.github.io/pintos-kaist/project3/introduction.html
- https://casys-kaist.github.io/pintos-kaist/project3/vm_management.html
- https://casys-kaist.github.io/pintos-kaist/project3/anon.html
- https://casys-kaist.github.io/pintos-kaist/project3/stack_growth.html
- https://casys-kaist.github.io/pintos-kaist/project3/memory_mapped_files.html
- https://casys-kaist.github.io/pintos-kaist/project3/swapping.html
- https://casys-kaist.github.io/pintos-kaist/project3/cow.html
- https://casys-kaist.github.io/pintos-kaist/project3/FAQ.html

### Project 4 (File System)
- https://casys-kaist.github.io/pintos-kaist/project4/introduction.html
- https://casys-kaist.github.io/pintos-kaist/project4/indexed_and_extensible_files.html
- https://casys-kaist.github.io/pintos-kaist/project4/subdirectories.html
- https://casys-kaist.github.io/pintos-kaist/project4/buffer_cache.html
- https://casys-kaist.github.io/pintos-kaist/project4/synchronization.html
- https://casys-kaist.github.io/pintos-kaist/project4/FAQ.html

### Appendix
- https://casys-kaist.github.io/pintos-kaist/appendix/threads.html
- https://casys-kaist.github.io/pintos-kaist/appendix/synchronization.html
- https://casys-kaist.github.io/pintos-kaist/appendix/memory_allocation.html
- https://casys-kaist.github.io/pintos-kaist/appendix/virtual_address.html
- https://casys-kaist.github.io/pintos-kaist/appendix/page_table.html
- https://casys-kaist.github.io/pintos-kaist/appendix/debugging_tools.html
- https://casys-kaist.github.io/pintos-kaist/appendix/development_tools.html
- https://casys-kaist.github.io/pintos-kaist/appendix/hash_table.html

---

## 7. 로컬 PDF 참조

`docs/` 디렉터리에 다음 PDF가 존재하며, 이 프로젝트의 1차 학습 자료에 포함된다. 각 PDF의 핵심 내용은 해당 프로젝트 섹션의 "📄 KAIST 강의자료 보강" 서브섹션에 반영되어 있다.

- `docs/Pintos_2.pdf` — Project 2 (User Programs), 17p, Instructor: Youngjin Kwon
- `docs/Pintos_3.pdf` — Project 3 (Virtual Memory), 18p, Instructor: Youngjin Kwon
- `docs/Pintos_4.pdf` — Project 4 (File System), 10p, Instructor: Youngjin Kwon

**PDF 읽기 도구**
- `poppler`가 설치되어 있다 (`brew install poppler`). `pdftotext -layout <file>`으로 텍스트 추출 가능.
- Claude Code의 Read 툴로 PDF를 직접 열 때 `pdftoppm`이 필요. 현재 세션의 Read 툴은 캐시된 PATH 때문에 인식 못 할 수 있음 — 이 경우 `pdftotext`로 Bash 경유 추출을 사용.
