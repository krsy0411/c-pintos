#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>

/* 초당 타이머 인터럽트 발생 횟수(틱 주파수). 
   - 하드웨어 PIT(Programmable Interval Timer)가 이 주기로 인터럽트를 발생시킵니다.
   - 스케줄러의 시간 슬라이스, sleep 구현, 통계 집계의 기준 시간이 됩니다. */
#define TIMER_FREQ 100

/* 타이머 장치 초기화.
   - 언제/어디서: 부팅 과정의 초기 단계(threads/init.c 경로의 init 루틴)에서 한 번 호출됩니다.
   - 왜 필요한가: PIT를 원하는 주기로 설정하고, 해당 외부 인터럽트(IRQ0)를 핸들러(timer_interrupt)에 연결합니다. */
void timer_init (void);

/* 짧은 지연(바쁜 대기 busy-wait)을 보정하기 위한 loops_per_tick 보정.
   - 언제/어디서: 부팅 직후, 인터럽트가 enable된 상태에서 한 번 호출됩니다.
   - 왜 필요한가: 매우 짧은(틱 미만) 수면/지연을 소프트웨어 루프로 정확히 맞추기 위해 CPU 속도에 따른 캘리브레이션이 필요합니다. */
void timer_calibrate (void);

/* OS 부팅 이후 누적된 타이머 틱 수를 반환.
   - 언제: 어디서든 시간 기준이 필요할 때(예: 경과 시간 계산).
   - 동시성: 내부적으로 인터럽트를 잠깐 비활성화하여 일관된 값을 읽습니다. */
int64_t timer_ticks (void);

/* 인자로 받은 시점(then) 이후 경과한 틱 수를 반환.
   - 사용 예: int64_t start = timer_ticks(); ... timer_elapsed(start) 으로 경과 시간 계산. */
int64_t timer_elapsed (int64_t then);

/* 현재 스레드를 약 TICKS 틱 동안 재우는(블록) 수면 함수.
   - 언제: 스레드가 특정 시간 동안 CPU를 양보하고 싶을 때.
   - 어디서: 스레드 컨텍스트(인터럽트 ON)에서 호출해야 합니다.
   - 기본 버전은 바쁜 대기 없이 단순 양보(yield) 루프라 비효율적 → 과제에서 “알람” 목록을 이용한 **차단형 수면**으로 개선합니다. */
void timer_sleep (int64_t ticks);

/* 밀리초/마이크로초/나노초 단위 수면. 내부적으로 실시간 변환(real_time_sleep)을 호출.
   - 틱 이상이면 블록킹 수면을, 틱 미만이면 보정된 바쁜 대기를 사용합니다. */
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* 타이머 통계(누적 틱)를 출력. 디버깅/통계용. */
void timer_print_stats (void);

#endif /* devices/timer.h */
