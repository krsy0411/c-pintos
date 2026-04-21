# CLAUDE.md — PintOS 구현 프로젝트

이 저장소는 **KAIST CS330 Pintos**를 사용자 본인이 처음부터 다시 구현하기 위한 작업공간이다. Claude Code는 이 파일과 아래에 명시된 문서만을 근거로 답변한다.

---

## 🔒 Ground Rules (반드시 준수)

1. **소스 오브 트루스는 아래 문서뿐**이다. 일반 OS 지식, 다른 Pintos 변종(Stanford 원본 등), 기억에만 의존한 구현 조언은 답변에 섞지 않는다.
   - 1차 자료: `.claude/knowledge/pintos-reference.md` (정리된 요약)
   - 1차 자료: https://casys-kaist.github.io/pintos-kaist/ (canonical web docs)
   - 1차 자료: `docs/Pintos_2.pdf`, `docs/Pintos_3.pdf`, `docs/Pintos_4.pdf` (강의 PDF)
2. 답변·코드 제안 전에 해당 태스크에 연관된 페이지/섹션을 먼저 확인한다.
   - `pintos-reference.md`에 요약이 있으면 그것을 우선 참조.
   - 세부 스펙이 필요하면 canonical URL을 `WebFetch`로 읽거나 해당 PDF를 `Read`로 연다.
3. 문서에서 **명시적으로 확인되지 않는** 내용은 답변에 포함하지 않는다. 필요하면 "문서에서 확인되지 않음"이라고 명시하고, 사용자에게 확인을 요청한다.
4. 코드는 KAIST 템플릿 규약을 따른다. Project 3에서 템플릿 위반은 **0점** 처리된다.
5. 테스트 이름을 하드코딩하거나 테스트에만 맞춘 특수 처리는 금지 (채점 정책).
6. 설명은 한국어로 한다. 코드 주석은 기존 코드 스타일(영어)을 유지한다.

---

## 📁 프로젝트 구조

```
pintos/                               # repo root
├── CLAUDE.md                         # 이 파일
├── .claude/knowledge/                # 로컬 지식 파일
│   └── pintos-reference.md           # KAIST 문서 통합 요약
├── docs/                             # 강의 PDF (1차 자료)
│   ├── Pintos_2.pdf                  # Project 2 관련
│   ├── Pintos_3.pdf                  # Project 3 관련
│   └── Pintos_4.pdf                  # Project 4 관련
├── .devcontainer/                    # Docker + VSCode 개발환경
├── README.md                         # Docker 기반 개발환경 가이드
└── pintos/                           # 커널 소스
    ├── threads/                      # Project 1 (alarm, priority, MLFQS)
    ├── userprog/                     # Project 2 (syscalls, process)
    ├── vm/                           # Project 3 (SPT, anon, file, swap)
    ├── filesys/                      # Project 2+에서 사용, Project 4에서 수정
    ├── devices/                      # timer, block, console 등
    ├── include/                      # 헤더
    ├── lib/                          # C stdlib 서브셋
    ├── tests/                        # 프로젝트별 테스트
    ├── examples/                     # 유저 프로그램 샘플 (P2+)
    └── utils/                        # pintos 유틸
```

---

## 🛠️ 빌드 & 테스트 (Quick Reference)

> 이 환경은 Docker/DevContainer가 권장됨 (`README.md` 참조). 아래 명령은 컨테이너 내부에서 실행.

```bash
# pintos 디렉터리에서
source ./activate                  # PATH에 utils/ 추가

# 각 프로젝트 디렉터리에서 빌드
cd threads && make                 # Project 1
cd userprog && make                # Project 2
cd vm && make                      # Project 3
cd filesys && make                 # Project 4

# 단일 테스트 실행
cd build
pintos run alarm-multiple          # 예: P1 alarm test
pintos -- run alarm-multiple > log # 로그 저장

# 전체 테스트 (채점 기준)
cd build && make check             # .result 파일 생성
make tests/<test-name>.result      # 개별 테스트
VERBOSE=1 make check               # 상세 출력

# 제출 아카이브
TEAM=<N> make archive              # team<N>.tar.gz 생성
```

**산출물 위치**: `<project>/build/kernel.o` (디버그 심볼 포함), `kernel.bin` (실제 부팅 이미지).

### pintos-util (테스트/디버깅 헬퍼, from github.com/jacti/pintos-util)

각 프로젝트 디렉터리(`pintos/{threads,userprog,vm,filesys}`)에 `select_test.sh` + `.test_config`가 배치되어 있음.

```bash
# (프로젝트 디렉터리에서) 테스트 리스트 출력 + 번호 선택 실행
./select_test.sh -q          # quick: 결과만
./select_test.sh -g          # gdb: vscode 디버거 연동
./select_test.sh -q -r       # 재빌드 후 테스트
# 입력 형식: "1-5 9 11-13" 등 공백 구분 (range 허용)
```

- 통과 이력은 `.test_status`에 기록됨(빨강/초록 표시). 리셋: 해당 파일 삭제.
- **VSCode 디버깅**: `.vscode/launch.json`에 [Threads / User Program / VM] 3종 구성 등록됨. `miDebuggerPath: /usr/bin/gdb` (devcontainer 내부 기준). `-g` 옵션으로 QEMU를 gdbserver 모드로 띄운 뒤 F5.

---

## 🗺️ 프로젝트 태스크 맵

자세한 스펙은 `.claude/knowledge/pintos-reference.md`에 있다. 아래는 빠른 탐색용.

### Project 1 — Threads (`pintos/threads/`, `pintos/devices/`)
- **Alarm Clock**: `devices/timer.c`의 `timer_sleep` busy-wait 제거 → sleep queue
- **Priority Scheduling**: 우선순위 (0~63) 스케줄링 + Donation (basic/multiple/nested, lock 필수, 최대 depth 8)
- **Advanced Scheduler (MLFQS)**: `-mlfqs` 옵션, 17.14 고정소수점, priority/recent_cpu/load_avg 공식

### Project 2 — User Programs (`pintos/userprog/`)
- **Argument Passing**: x86-64 System V ABI, stack에 argv 구성 (right-to-left, 8바이트 정렬)
- **System Calls**: halt/exit/fork/exec/wait/create/remove/open/filesize/read/write/seek/tell/close
- **User Memory Access**: 사전 검증 또는 page-fault 복구
- **Process Termination Msg**, **Deny Write to Exec**, **dup2 (optional)**
- 제약: `#ifdef VM` 블록 금지, `syscall-entry.S`/`gdt.c`/`tss.c` 수정 금지, filesys는 외부에서 락으로 보호

### Project 3 — Virtual Memory (`pintos/vm/`, `include/vm/`, `userprog/exception.c`)
- **SPT**: `struct page`, `supplemental_page_table_init/find/insert`. 해시테이블 권장.
- **Anonymous Pages**: `anon_initializer`, `anon_destroy`
- **Stack Growth**: rsp-8 휴리스틱, 최대 1 MB, `vm_stack_growth`
- **mmap/munmap**: lazy load, dirty-only writeback
- **Swap**: `anon_swap_in/out`, `file_backed_swap_in/out`. Swap slot은 lazy.
- **Copy-on-Write (optional)**
- 제약: 템플릿 엄수(위반 시 0점), `palloc_get_page(PAL_USER)`만 사용

### Project 4 — File System (`pintos/filesys/`)
- **Indexed/Extensible Files**: **FAT 기반만 허용** (multi-level indexing 사용 시 0점). `fat_fs_init`, `fat_create_chain` 등.
- **Subdirectories & Soft Links**: chdir/mkdir/readdir/isdir/inumber/symlink
- **Buffer Cache (optional, VM 활용)**
- **Synchronization**
- 제약: VM 비활성화 시 10% 감점. 영속성 테스트는 `tar` 사용.

---

## 🧭 작업 플로우 (권장)

1. **스펙 확인**: 태스크 시작 전 `.claude/knowledge/pintos-reference.md`의 해당 섹션을 먼저 읽는다. 세부가 필요하면 canonical URL을 WebFetch.
2. **기존 코드 파악**: 수정 대상 파일(`thread.c`, `process.c`, `vm.c` 등)을 Read로 검토 후 수정.
3. **증분 구현 & 테스트**: 작은 단위로 빌드 → `make check` 또는 개별 테스트.
4. **테스트 로그 확인**: 실패 시 `VERBOSE=1` 또는 `pintos -- run <test> > log`로 분석.
5. **설계 결정 기록**: 중요한 자료구조 선택/락 전략 등은 커밋 메시지에 남긴다(이 저장소는 개인 연습용).

---

## 📎 문서 확인되지 않을 때의 폴백

문서(로컬 요약 + 웹 + PDF)에서 해답이 안 나올 때:
1. 그 사실을 사용자에게 먼저 알린다.
2. 어떤 경로(웹 페이지/파일/테스트)에서 더 확인할 수 있을지 제안한다.
3. **추측으로 답하지 않는다.**

---

## 🔧 알려진 환경 이슈

- **PDF 읽기**: `poppler`는 이미 설치됨 (`brew install poppler` 완료). `docs/Pintos_*.pdf`의 핵심 내용은 이미 `.claude/knowledge/pintos-reference.md`의 각 프로젝트 섹션 "📄 KAIST 강의자료 보강"에 반영됨.
  - 원문 재확인이 필요하면: `pdftotext -layout docs/Pintos_<N>.pdf -` (Bash에서 직접 실행)
  - Claude Code의 `Read` 툴이 PDF를 인식하지 못하면 PATH 캐시 문제 — Bash 경유 pdftotext를 쓸 것.
- **Docker 환경**: `.devcontainer/` 사용 권장. 호스트 macOS에서 직접 빌드는 공식 지원 외.
