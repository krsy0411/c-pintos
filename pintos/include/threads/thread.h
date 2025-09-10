#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* 스레드 생애주기 상태. 
   - RUNNING: 현재 CPU에서 실행 중.
   - READY: 실행할 준비가 되어 큐에서 대기.
   - BLOCKED: 어떤 사건(락/세마포어/타이머 등)을 기다리는 중.
   - DYING: 종료 직전, 파괴 예정. */
enum thread_status {
	THREAD_RUNNING,
	THREAD_READY,
	THREAD_BLOCKED,
	THREAD_DYING
};

/* 스레드 식별자 타입. 실패 시 TID_ERROR 반환. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)

/* 우선순위 범위(라운드로빈 기본, 과제에서 우선순위 스케줄링 확장). */
#define PRI_MIN 0
#define PRI_DEFAULT 31
#define PRI_MAX 63



/* 커널 스레드/유저 프로세스를 표현하는 구조체.
   - 각 스레드는 “자신의 4KB 페이지” 최하단에 struct thread, 상단부터는 커널 스택이 아래로 자라납니다.
   - 스택 오버플로 시 magic 플래그가 깨지며 ASSERT로 감지됩니다.
   - elem은 run queue(ready_list) 또는 wait list(세마포어/조건변수) 어디에도 쓰일 수 있는 다목적 링크입니다(상호 배타). */
struct thread {
	/* thread.c가 관리 */
	tid_t tid;
	enum thread_status status;
	char name[16];
	int priority;
	int64_t wakeup_tick;
	/* thread.c와 synch.c가 공유(ready_list나 waiters 리스트에 들어갈 때 사용). */
	struct list_elem elem;
   
   // 우선순위 반납후 원 상태로 돌아갈 정보
   int init_priority;

   //얻고자 하는 락 종류(multiple donation만??) -> 포인터로서 nested에서도 가리키는데 쓰임(배열 ㄴ)
   struct lock *wait_on_lock;

   // 해당 스레드에 우선순위를 기부한 스레드들의 리스트
   struct list donations;

   //다른 elem과 동일하게 donation을 탐색하기 위한 요소
   struct list_elem donation_elem; 

#ifdef USERPROG
	/* userprog/process.c 소유 */
	uint64_t *pml4; /* x86-64 페이지 테이블 루트 */
#endif
#ifdef VM
	/* 스레드가 소유하는 전체 가상 메모리 테이블 */
	struct supplemental_page_table spt;
#endif

	/* thread.c 소유 */
	struct intr_frame tf; /* 문맥 전환용 레지스터 저장공간 */
	unsigned magic;       /* 스택 오염(오버플로) 탐지 */
};

/* 스케줄러 모드 스위치.
   - false: 라운드로빈(기본).
   - true : MLFQ(멀티레벨 피드백 큐), 커맨드라인 옵션 "-o mlfqs"로 제어. */
extern bool thread_mlfqs;

/* ───────────────────────────
   “시점/왜 필요한가/어디서 호출되는가” 요약
   ─────────────────────────── */

/* 스레딩 시스템 초기화.
   - 언제: 부팅 초기, 인터럽트 OFF 상태에서 한 번.
   - 어디서: threads/init.c 경로의 초기화 루틴.
   - 왜: 현재 실행 중인 코드를 ‘첫 스레드’로 변환하고, run queue, TID 락 등 준비. */
void thread_init (void);

/* 스레딩 시작(선점 스케줄링 on) 및 idle 스레드 생성.
   - 언제: thread_init() 직후.
   - 왜: 인터럽트를 켜고, idle 스레드가 준비되어야 스케줄러가 정상 동작. */
void thread_start (void);

/* 매 타이머 틱마다 타이머 인터럽트 핸들러에서 호출.
   - 왜: 틱 통계 갱신, 시간 슬라이스 소진 시 선점(intr_yield_on_return) 유발. */
void thread_tick (void);

/* 스레드 통계 출력(디버깅/통계). */
void thread_print_stats (void);

typedef void thread_func (void *aux);

// 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하고 스케줄링
void test_max_priority(void);

// 인자로 주어진 스레드들의 우선순위를 비교
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux  UNUSED);

/* 새 커널 스레드 생성 → ready queue에 등록.
   - 언제: 커널 스레드가 필요할 때(예: 디스크 I/O 서버 스레드, 테스트 스레드).
   - 주의: thread_start() 이후에는 생성 직후 바로 스케줄될 수 있음(동기화 필요). */

//**************************************************************************** */
//*********************새 스레드를 생성할때 우선순위도 지정하도록 수정******************** */
tid_t thread_create (const char *name, int priority, thread_func *, void *);

/* 현재 스레드를 BLOCKED로 전환하고 스케줄.
   - 언제: 세마포어 down, 락 획득 실패 등 “기다림” 상황.
   - 호출 전제: 인터럽트 OFF 상태여야 함. */


void thread_block (void);

/* BLOCKED 스레드 T를 READY로 전환(큐에 삽입).
   - 언제: 세마포어 up/조건변수 signal 등 “깨움” 상황.
   - 선점은 즉시 일어나지 않음(현재 호출자 문맥 유지). */

void thread_unblock (struct thread *t);

/* 현재 실행 중인 스레드/이름/TID 접근자. */
struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

/* 현재 스레드 종료(복귀하지 않음). 
   - 주의: 인터럽트 컨텍스트에서 호출 금지. */
void thread_exit (void) NO_RETURN;

/* CPU를 양보(READY로 넣고 스케줄).
   - 언제: 자발적 양보(타임슬라이스 중이라도), busy 대기 회피 등. */

void thread_yield (void);

void donate_priority(void);

/* 우선순위 getter/setter (과제 1-3에서 실제 정책 구현). */
int thread_get_priority (void);
void thread_set_priority (int);

/* MLFQS 관련 API(과제 확장). */
int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* 64-bit iret 진입용 헬퍼. 문맥전환 최종 단계에서 사용. */
void do_iret (struct intr_frame *tf);
void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);


#endif /* threads/thread.h */
