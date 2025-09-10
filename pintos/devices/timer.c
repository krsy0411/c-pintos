// #define DEBUG_TIMER 1  // 디버깅 시 1로 변경

// #if DEBUG_TIMER
// #define TIMER_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
// #else
// #define TIMER_DEBUG(fmt, ...) do {} while(0)
// #endif



#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부사항은 [8254]를 참고하십시오. */

#if TIMER_FREQ < 19
#error 8254 타이머는 TIMER_FREQ >= 19 가 필요합니다
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ 는 1000 이하를 권장합니다
#endif

/* OS가 부팅된 이후 누적된 타이머 틱 수. */
static int64_t ticks;

//wakeup_tick 함수를 오름차순으로 유지
static struct list sleep_list;
//정렬 비교
static bool
wakeup_less(const struct list_elem *a,
			const struct list_elem *b,
			void *aux UNUSED)
{
	const struct thread *ta= list_entry(a,struct thread, elem);
	const struct thread *tb= list_entry(b,struct thread, elem);

	return ta -> wakeup_tick < tb-> wakeup_tick;
}

/* 틱당 루프 횟수(바쁜 대기 정확도 보정을 위해 사용).
   timer_calibrate()에서 초기화됩니다. */
static unsigned loops_per_tick;

/* 외부 인터럽트 핸들러 시그니처 */
static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 PIT(Programmable Interval Timer)를 설정하여
   초당 PIT_FREQ(=TIMER_FREQ)번 인터럽트를 발생시키고,
   해당 외부 인터럽트를 등록합니다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값(반올림). */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* 제어워드: 카운터0, LSB→MSB 순서, 모드2, 이진 모드. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");

	list_init(&sleep_list);
}

/* 짧은 지연(틱 미만)의 정확도를 위해 loops_per_tick 값을 보정합니다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick을 1틱보다 작은 최대 2의 거듭제곱으로 근사. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* 이후 8비트를 정밀 보정. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후의 누적 틱 수를 반환. (원자적으로 읽기) */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* then(과거의 timer_ticks() 결과) 이후 경과된 틱 수를 반환. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 현재 스레드를 약 TICKS 틱 동안 대기시킵니다.
   기본 구현: 바쁜 대기 대신 단순한 양보(yield) 루프 → 비효율적.
   과제 목표: 알람/슬립 리스트를 도입하여 BLOCKED로 전환하고,
   타이머 인터럽트에서 깨우는 방식으로 개선(정확/효율). */

// sleep 리스트에 넣어야 함
// 언제 깰지를 담은 스레드가 들어감
// 근데 공유 자료 구조임
// 두 군데에서 이 리스트에 접근함
// 일반 스레드 문맥: timer_sleep()안에서 현재 스레드를 리스트에 넣고
// timer_block()으로 잠재움

// 인터럽트 문맥: timer_interrupt()안에서 시간이 된 스레드를 리스트에서 꺼내
// thread_unblock()을 함

// 만약 일반 문맥에서 리스트에 삽입 중인데, 그 순간 타이머 인터럽트가 끼어들어
// 리스트를 꺼낸다면 -> 커널 패닉 -> 리스트 조작 구간을 원자적으로 보호 해야 함
// 근데 왜 락이 아니라 인터럽트 제어 인가..
// 원래 세마포어/뮤텍스는 스레드들 사이의 경쟁을 조정
// 누군가 락을 잡고 있으면, 다른 스레드는 블록되거나 스핀해서 양보를 해야함
// 근데 sleep_list는 스레드 문맥 + 인터럽트 문맥에서 동시 접근

/*
	인터럽트 핸들러에서 thread_block()을 호출하면,
	현재 실행 중인 스레드(idle 포함)를 블록시키지만 핸들러 자체는 스케줄링될 수 없어서 시스템이 멈춘다
*/
void
timer_sleep (int64_t ticks) {

	if (ticks <= 0) return;  
    
    ASSERT (intr_get_level () == INTR_ON);  
	
	int64_t start = timer_ticks ();

	struct thread *cur= thread_current();
	int64_t wake= start+ ticks;

	enum intr_level old= intr_disable();

	cur-> wakeup_tick = wake;
	list_insert_ordered(&sleep_list, &cur->elem, wakeup_less, NULL);
	//TIMER_DEBUG("[sleep] %s: now=%lld wake=%lld\n", cur->name, start, cur->wakeup_tick);

	// printf("[sleep] %s: now=%lld wake=%lld\n",
    //    cur->name, start, cur->wakeup_tick);

	thread_block(); 
	intr_set_level(old);
}

/* 약 MS 밀리초 동안 수면. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 약 US 마이크로초 동안 수면. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 약 NS 나노초 동안 수면. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러.
   - 매 틱마다 ticks++ 하고, 스케줄러용 thread_tick()을 호출합니다.
   - “선점(preemption)”과 시간 슬라이스 관리의 실제 트리거가 됩니다. */

// 얘는 무조건 커널에서 실행, 막을 수 없음
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();

	int64_t now= ticks;

	while(!list_empty(&sleep_list)){

		// 맨 앞에서 now이하의 스레드만 깨움
		struct thread *t = list_entry(list_front(&sleep_list), struct thread, elem);

		if(t->wakeup_tick<= now){
			list_pop_front(&sleep_list);

	// 		printf("[wake ] %s: now=%lld wake=%lld\n",
    //    t->name, now, t->wakeup_tick);

			thread_unblock(t);
		}
		else{
			break;
		}

	}
}

/* 루프를 LOOPS 만큼 돌리는 것이 1틱을 초과하는지 여부(초과하면 true). */
static bool
too_many_loops (unsigned loops) {
	/* 다음 틱이 올 때까지 대기. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS 만큼 단순 루프 수행. */
	start = ticks;
	busy_wait (loops);

	/* 루프 도중 틱이 바뀌었다면 1틱을 초과한 것. */
	barrier ();
	return start != ticks;
}

/* 아주 짧은 지연을 위해 단순 루프를 LOOPS 번 반복.
   NO_INLINE: 인라이닝/정렬에 따라 타이밍이 흔들릴 수 있어서 금지. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* (NUM / DENOM)초 동안 수면.
   - 변환: ticks = NUM * TIMER_FREQ / DENOM (내림).
   - ticks >= 1 이면 thread_sleep 기반(과제에서 구현)으로 CPU 양보.
   - ticks == 0 이면 바쁜 대기로 더 정밀한 지연 수행. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* 초 → 틱 변환(내림). 
	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks
	   1 s / TIMER_FREQ ticks */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 1틱 이상이면, 블록킹 수면 사용(다른 스레드가 CPU 사용). */
		timer_sleep (ticks);
	} else {
		/* 1틱 미만이면 바쁜 대기 사용(정밀).
		   오버플로 방지를 위해 분자/분모를 1000으로 스케일 다운. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
