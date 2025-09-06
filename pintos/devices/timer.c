#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
/**
 *  컴파일 시점에 TIMER_FREQ 값이 유효한 범위 (19Hz ~ 1000Hz) 에 있는지 검사
 *  ='안전 장치'
 */
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
/* 8254 프로그래머블 인터벌 타이머(PIT)를 설정하여 초당 PIT_FREQ 횟수만큼
 * 인터럽트를 발생시키고, 해당 인터럽트 핸들러를 등록합니다.
 * 
 * 이 함수는 시스템 초기화 과정에서 한 번만 호출되며,
 * 타이머 하드웨어를 초기화하고 인터럽트 처리를 위한 기본 설정을 수행합니다.
 */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
/* loops_per_tick 값을 보정(캘리브레이션)합니다.
 * 
 * 짧은 시간 지연을 구현하는데 사용되는 loops_per_tick 값을
 * 현재 시스템에 맞게 계산하여 초기화합니다.
 * 
 * 이 함수는 시스템 초기화 과정에서 한 번만 호출되며,
 * 인터럽트가 활성화된 상태에서 실행되어야 합니다.
 * 
 * 캘리브레이션이 완료되면 초당 반복 횟수를 출력합니다.
 */
void
timer_calibrate(void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
/* OS가 부팅된 이후의 총 타이머 틱 수를 반환합니다.
 *
 * @return 부팅 이후 경과된 타이머 틱의 총 개수를 int64_t 타입으로 반환합니다.
 *         이 값은 인터럽트가 비활성화된 상태에서 안전하게 읽어옵니다.
 */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
/* 주어진 시점 이후 경과된 타이머 틱 수를 반환합니다.
 *
 * @param then 기준이 되는 시점의 타이머 틱 값입니다.
 *            이 값은 timer_ticks() 함수가 이전에 반환한 값이어야 합니다.
 * @return 주어진 시점으로부터 현재까지 경과된 타이머 틱의 수를 int64_t 타입으로 반환합니다.
 */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
/* 주어진 틱(tick) 수 만큼 실행을 일시 중단합니다.
 *
 * @param ticks 대기할 타이머 틱의 수입니다. 
 *              정확하지 않을 수 있으며 대략적인 값입니다.
 * 
 * 현재 스레드는 주어진 틱 수만큼 실행이 일시 중단되며,
 * 이 시간 동안 다른 스레드들이 실행될 수 있습니다.
 */
void
timer_sleep (int64_t ticks) {
	if (ticks <= 0) {
		return;
	}

	int64_t wake_up_at = timer_ticks() + ticks;
	thread_sleep(wake_up_at);
}

/* Suspends execution for approximately MS milliseconds. */

/* 실행을 대략적으로 MS 밀리초 동안 일시 중단합니다.
 *
 * @param ms 일시 중단할 시간(밀리초)입니다.
 *          정확하지 않을 수 있으며 근사값입니다.
 */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
/* 실행을 대략적으로 US 마이크로초 동안 일시 중단합니다.
 *
 * @param us 일시 중단할 시간(마이크로초)입니다.
 *          정확하지 않을 수 있으며 근사값입니다.
 */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
/* 실행을 대략적으로 NS 나노초 동안 일시 중단합니다.
 *
 * @param ns 일시 중단할 시간(나노초)입니다.
 *          정확하지 않을 수 있으며 근사값입니다.
 */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
/* 타이머 통계를 출력합니다.
 *
 * 시스템이 부팅된 이후 경과된 총 타이머 틱 수를 화면에 출력합니다.
 */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick();
	thread_wakeup(ticks);

	/* 현재 실행 중인 스레드보다 우선순위가 높은 스레드가 ready_list 에 있다면
	* 인터럽트가 끝날 때 CPU를 양보하도록 플래그를 설정한다. */
	if (should_preempt()) {
		intr_yield_on_return();
	}
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
/* 주어진 반복 횟수가 한 타이머 틱보다 더 오래 걸리는지 검사합니다.
 *
 * @param loops 검사할 반복 횟수입니다.
 * @return 주어진 반복 횟수가 한 타이머 틱보다 더 오래 걸리면 true,
 *         그렇지 않으면 false를 반환합니다.
 */

static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
/* 짧은 시간 지연을 구현하기 위해 간단한 루프를 LOOPS 횟수만큼 반복합니다.
 *
 * @param loops 반복할 횟수입니다.
 *
 * 이 함수는 NO_INLINE으로 표시되어 있습니다.
 * 코드 정렬이 타이밍에 큰 영향을 미칠 수 있기 때문에,
 * 다른 위치에서 인라인되면 결과를 예측하기 어려울 수 있습니다.
 */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
/* 대략적으로 NUM/DENOM 초 동안 실행을 일시 중단합니다.
 *
 * @param num 시간의 분자 값입니다.
 * @param denom 시간의 분모 값입니다.
 *
 * 실제 대기 시간은 NUM/DENOM 초에 근사합니다.
 * 타이머 틱 단위로 변환하여 timer_sleep()을 호출하거나,
 * 더 정확한 서브틱 타이밍을 위해 busy_wait를 사용합니다.
 */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
