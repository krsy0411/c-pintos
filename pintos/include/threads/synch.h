#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어.
   - value: 현재 카운트(>=0)
   - waiters: 이 세마포어를 기다리는 스레드 큐
   - 사용 목적: 공유 자원 접근 제한(P/V), 조건 대기 등 
   - 시점: sema_down()으로 0이면 BLOCKED, sema_up() 때 READY로 깨움. */
struct semaphore {
	unsigned value;
	struct list waiters;
};
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux);

void sema_init (struct semaphore *, unsigned value);
/* P 연산: value>0 될 때까지 기다렸다가 원자적으로 1 감소(필요 시 BLOCKED). */
void sema_down (struct semaphore *);
/* try P: value>0이면 1 감소하고 true, 아니면 대기 없이 false. */
bool sema_try_down (struct semaphore *);
/* V 연산: value 1 증가, 대기 스레드가 있으면 하나 깨움(READY). */
void sema_up (struct semaphore *);
/* 동작 검증용 자가 테스트(핑퐁). */
void sema_self_test (void);

/* 락(뮤텍스).
   - holder: 현재 락 보유 스레드(디버깅용).
   - semaphore: 내부적으로 이진 세마포어(value=1)로 구현.
   - 목적: 상호 배제를 엄격히 보장(같은 스레드가 acquire/release). */
struct lock {
	struct thread *holder;
	struct semaphore semaphore;
};

void lock_init (struct lock *);
/* 필요 시 수면하며 락을 획득(P). 인터럽트 핸들러에서는 호출 금지. */
void lock_acquire (struct lock *);
/* 수면 없이 즉시 시도. 성공 시 true. */
bool lock_try_acquire (struct lock *);
/* 락 해제(V). holder가 반드시 현재 스레드여야 함. */
void lock_release (struct lock *);
/* 현재 스레드가 이 락을 들고 있는지. */
bool lock_held_by_current_thread (const struct lock *);

/* 조건 변수.
   - waiters: 조건을 기다리는 세마포어 목록.
   - 목적: “조건 충족/신호” 패턴을 락과 함께 구현(Mesa 스타일). */
struct condition {
	struct list waiters;
};

void cond_init (struct condition *);
/* (원자적으로) LOCK을 풀고 조건 대기 → 신호 받으면 깨어나 LOCK을 다시 획득. */
void cond_wait (struct condition *, struct lock *);
/* 대기 중인 스레드가 있으면 하나 신호(깨움). */
void cond_signal (struct condition *, struct lock *);
/* 대기 중 모두에게 신호. */
void cond_broadcast (struct condition *, struct lock *);

/* 최적화 장벽: 컴파일러의 재정렬을 막음. 
   - 메모리 가시성/순서를 보장해야 하는 저수준 코드에서 사용. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
