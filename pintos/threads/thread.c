#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 magic 필드에 넣는 랜덤 값
   스택 오버플로 감지를 위해 사용됨 */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드용 랜덤 값
   이 값은 수정하지 말 것 */
#define THREAD_BASIC 0xd42df210

/* READY 상태인 스레드들의 리스트
   실행 준비된 스레드가 여기 들어가고 스케줄러가 뽑아감 */
static struct list ready_list;

/* idle 스레드
   실행할 스레드가 없을 때만 돌아가는 특수 스레드 */
static struct thread *idle_thread;

/* 초기(main) 스레드
   init.c:main()을 실행 중인 코드를 스레드로 변환한 것 */
static struct thread *initial_thread;

/* allocate_tid()에서 쓰는 락
   TID 배분을 원자적으로 보장 */
static struct lock tid_lock;

/* 종료된 스레드를 파괴하기 전까지 모아두는 리스트
   (스택이 아직 쓰이고 있을 수 있으므로 파괴를 지연) */
static struct list destruction_req;

/* 스케줄링 통계
   idle_ticks   : idle 스레드가 쓴 틱 수
   kernel_ticks : 커널 스레드가 쓴 틱 수
   user_ticks   : 유저 프로그램이 쓴 틱 수 */
static long long idle_ticks;
static long long kernel_ticks;
static long long user_ticks;




/* 스케줄링 파라미터 */
#define TIME_SLICE 4		  /* 스레드 하나가 연속으로 얻는 타임슬라이스 길이(틱 단위) */
static unsigned thread_ticks; /* 현재 스레드가 실행한 틱 수 */

/* 스케줄러 모드
   false = 라운드 로빈 (기본)
   true  = MLFQ (커맨드라인 -o mlfqs 옵션으로 켜짐) */
bool thread_mlfqs;

bool priority_less(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);
	return ta->priority > tb->priority;
}

static void kernel_thread(thread_func *, void *aux);
static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* 주어진 포인터 t가 유효한 스레드를 가리키면 true */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 실행 중인 스레드 반환
   CPU의 스택 포인터 rsp를 읽어서 페이지 경계로 내림
   struct thread는 항상 페이지의 맨 아래에 있으므로
   이걸로 현재 스레드를 찾을 수 있음 */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// thread_start에서 쓰는 GDT
// thread_init 이후에 gdt를 다시 세팅할 거라서 임시 gdt를 여기서 준비
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* 스레드 시스템 초기화
   지금 실행 중인 코드를 스레드로 변환함
   run queue, tid_lock 등 초기화
   초기화 끝나기 전까지 thread_current() 호출하면 안 됨 */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* 임시 gdt 로드 (유저 컨텍스트는 포함되지 않음)
	   gdt_init()에서 유저 컨텍스트 포함 gdt를 다시 세팅함 */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&destruction_req);

	/* 현재 실행 중인 코드를 스레드 구조체로 초기화 */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* 스레딩 시작
   idle 스레드를 만들고 인터럽트를 켜서 선점 스케줄링 활성화 */
void thread_start(void)
{
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	intr_enable();

	sema_down(&idle_started);
}

/* 매 타이머 틱마다 타이머 인터럽트 핸들러가 호출하는 함수
   실행 시간 통계 갱신, 타임슬라이스 소진 시 선점 유발 */
void thread_tick(void)
{
	struct thread *t = thread_current();

	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* 스레드 통계 출력 */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* 커널 스레드 생성
   새 struct thread를 초기화하고 ready_list에 넣음
   우선순위는 인자로 주어지지만 실제 우선순위 스케줄링은 과제에서 구현해야 함 */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* kernel_thread를 실행 entry로 세팅
	   rip = kernel_thread, 첫번째 인자(function), 두번째 인자(aux) */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	thread_unblock(t);


	// 우선 순위를 정해서 현재 스레드보다 생성된 스레드가 우선순위가 높다면 
	if(t->priority > thread_current()->priority)
		thread_yield();

	return tid;
}

/* 현재 스레드를 BLOCKED 상태로 전환
   schedule()을 호출해서 다른 스레드로 넘김
   반드시 인터럽트 off 상태에서 호출해야 함 */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* BLOCKED 스레드를 READY 상태로 전환
   run queue에 넣음
   실행 중인 스레드를 바로 빼앗지는 않음 */
// void thread_unblock(struct thread *t)
// {
// 	enum intr_level old_level;

// 	ASSERT(is_thread(t));

// 	old_level = intr_disable();
// 	ASSERT(t->status == THREAD_BLOCKED);
// 	//list_push_back (&ready_list, &t->elem);
// 	list_insert_ordered(&ready_list, &t->elem, priority_less, NULL); 
// 	t->status = THREAD_READY;

// 	if (!intr_context() && !list_empty(&ready_list))
// 	{
// 		struct thread *front = list_entry(list_front(&ready_list), struct thread, elem);
// 		if (front->priority > thread_current()->priority)
// 		{
// 			thread_yield();
// 		}
// 	}

// 	intr_set_level(old_level);
// }
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);

	
	//list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);

	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 실행 중인 스레드 이름 반환 */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* 실행 중인 스레드 구조체 포인터 반환
   magic 값 체크로 유효성 검사 */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* 현재 스레드의 tid 반환 */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* 현재 스레드 종료
   status를 DYING으로 바꾸고 schedule() 호출
   실제 메모리 해제는 안전한 시점에 destruction_req 리스트에서 처리됨 */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level; 

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		//list_push_back (&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위 설정 */
void thread_set_priority(int new_priority)
{
	thread_current()->priority = new_priority;

	// 우선순위 역전 문제 방지
	thread_current()->init_priority= new_priority;
	
	refresh_priority();

	// 현재 스레드의 우선순위와 ready_list에서 가장 높은 우선순위를 비교하여 스케줄링하는 함수 호출
	test_max_priority();
}

/*
ready_list에서 우선 순위가 가장 높은 쓰레드와 현재 쓰레드의 우선 순위를 비교
→ 현재 쓰레드의 우선수위가 더 작다면 thread_yield()
*/
void test_max_priority(void){
	if (list_empty(&ready_list))
		return;
	
	struct thread *t= list_entry(list_front(&ready_list),struct thread, elem);

	if(thread_get_priority()< t-> priority){
		thread_yield();
	}
}

/*
첫 번째 인자의 우선순위가 높으면 1을 반환, 두 번째 인자의 우선순위가 높으면 0을 반환
list_insert_ordered() 함수에서 사용
*/
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct thread *ta= list_entry(a,struct thread, elem);
	struct thread *tb= list_entry(b,struct thread, elem);

	if(ta==NULL || tb== NULL){
		return false;
	}

	return ta->priority > tb->priority;
}

/* 현재 스레드의 우선순위 반환 */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* MLFQ nice, recent_cpu, load_avg 관련 함수는 TODO */
void thread_set_nice(int nice UNUSED) {}
int thread_get_nice(void) { return 0; }
int thread_get_load_avg(void) { return 0; }
int thread_get_recent_cpu(void) { return 0; }

/* idle 스레드
   실행 가능한 스레드가 없을 때만 스케줄러가 이걸 선택 */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		intr_disable();
		thread_block();
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드 함수 진입점
   실제 함수(function)를 실행하고 끝나면 thread_exit() 호출 */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable();
	function(aux);
	thread_exit();
}

/* 새 스레드 구조체 초기화
   status=BLOCKED, 이름/우선순위 설정, 스택 포인터 설정 */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);

	
	//t->priority = priority;
	t->priority= t->init_priority =priority;

	//처음에는 원하는 락이 없음
	list_init(&t->donations);
	t->wait_on_lock = NULL;

	t->magic = THREAD_MAGIC;
}

/* 다음에 실행할 스레드 선택
   ready_list에서 하나 꺼내거나, 비어있으면 idle_thread 반환 */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* iretq로 새 스레드를 실행시키는 함수 */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* thread_launch: 현재 문맥을 저장하고 새로운 스레드의 문맥을 로드해 전환 */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	__asm __volatile(
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n"
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n"
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n"
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n"
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n"
		"movw %%cs, 8(%%rax)\n"
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n"
		"mov %%rsp, 24(%%rax)\n"
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* 스케줄링 핵심
   현재 스레드 상태 갱신 → 다음 스레드 선택 → 전환 */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));

	next->status = THREAD_RUNNING;
	thread_ticks = 0;

#ifdef USERPROG
	process_activate(next);
#endif

	if (curr != next)
	{
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}
		thread_launch(next);
	}
}

/* 새 스레드에 쓸 tid를 하나 배정 */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}
