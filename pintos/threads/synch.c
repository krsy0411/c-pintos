/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생되었습니다.
  Nachos 저작권 고지가 아래에 전문 재현되어 있습니다. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
  All rights reserved.

  Permission to use, copy, modify, and distribute this software
  and its documentation for any purpose, without fee, and
  without written agreement is hereby granted, provided that the
  above copyright notice and the following two paragraphs appear
  in all copies of this software.

  IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
  ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
  CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
  AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
  HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
  BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
  PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
  MODIFICATIONS.
  */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 리스트 내의 하나의 세마포어. */
struct semaphore_elem {
   struct list_elem elem;              /* 리스트 요소. */
   struct semaphore semaphore;         /* 이 세마포어. */
};



/*
semaphore_elem으로부터 각 semaphore_elem의 쓰레드 디스크립터를 획득.
첫 번째 인자의 우선순위가 두 번째 인자의 우선순위보다 높으면 1을 반환 낮으면 0을 반환
*/
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct semaphore_elem *sema_a= list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b= list_entry(b, struct semaphore_elem, elem);

	if(sema_a==NULL || sema_b==NULL){
		return false;
	}

	struct list *list_a = &(sema_a->semaphore.waiters);
	struct list *list_b = &(sema_b->semaphore.waiters);

	if(list_a==NULL || list_b==NULL){
		return false;
	}

	struct thread *thread_a= list_entry(list_begin(list_a), struct thread, elem);
	struct thread *thread_b= list_entry(list_begin(list_b), struct thread, elem);

	if(thread_a ==NULL || thread_b == NULL){
		return false;
	}

	return thread_a -> priority > thread_b -> priority;
}


/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는 두 개의 원자적 연산자와 함께
  음이 아닌 정수입니다:

  - down 또는 "P": 값이 양수가 될 때까지 기다린 다음 감소시킵니다.
  - up 또는 "V": 값을 증가시킵니다 (그리고 대기 중인 스레드가 있다면 하나를 깨웁니다). */
void
sema_init (struct semaphore *sema, unsigned value) {
   ASSERT (sema != NULL);

   sema->value = value;
   list_init (&sema->waiters);
}

/* 세마포어에 대한 Down 또는 "P" 연산. SEMA의 값이 양수가 될 때까지 기다린 다음
  원자적으로 감소시킵니다.

  이 함수는 잠들 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다.
  이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 잠들면 다음으로
  스케줄된 스레드가 아마도 인터럽트를 다시 켤 것입니다. 이것이 sema_down 함수입니다. */
// void
// sema_down (struct semaphore *sema) {
//    enum intr_level old_level;

//    ASSERT (sema != NULL);
//    ASSERT (!intr_context ());

//    old_level = intr_disable ();
//    while (sema->value == 0) {
//    	list_push_back (&sema->waiters, &thread_current ()->elem);
//    	thread_block ();
//    }
//    sema->value--;
//    intr_set_level (old_level);
// }

void sema_down(struct semaphore *sema){
	enum intr_level old_level;
	ASSERT (sema!=NULL);
	ASSERT (!intr_context());

	old_level = intr_disable();

	//자원없으면 waiter 리스트에 넣고 블록
	while(sema->value ==0){
		list_insert_ordered(&sema -> waiters , &thread_current()->elem, cmp_priority,NULL);
		thread_block();
	}
	sema-> value--;
	intr_set_level(old_level);

}


/* 세마포어에 대한 Down 또는 "P" 연산이지만, 세마포어가 이미 0이 아닌 경우에만
  수행됩니다. 세마포어가 감소되면 true를, 그렇지 않으면 false를 반환합니다.

  이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
   enum intr_level old_level;
   bool success;

   ASSERT (sema != NULL);

   old_level = intr_disable ();
   if (sema->value > 0)
   {
   	sema->value--;
   	success = true;
   }
   else
   	success = false;
   intr_set_level (old_level);

   return success;
}

/* 세마포어에 대한 Up 또는 "V" 연산. SEMA의 값을 증가시키고 SEMA를 기다리고 있는
  스레드 중 하나를 깨웁니다(있다면).

  이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
// void
// sema_up (struct semaphore *sema) {
//    enum intr_level old_level;

//    ASSERT (sema != NULL);

//    old_level = intr_disable ();
//    if (!list_empty (&sema->waiters))
//    	thread_unblock (list_entry (list_pop_front (&sema->waiters),
//    				struct thread, elem));
//    sema->value++;
//    intr_set_level (old_level);
// }

void sema_up(struct semaphore *sema){
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if(!list_empty(&sema->waiters)){
		list_sort(&sema->waiters, cmp_priority,NULL);
		thread_unblock(list_entry (list_pop_front(&sema->waiters), struct thread, elem));
	}

	sema->value++;

	// 다시 test_max_priority()로 동기화
	// 현재 스레드의 우선순위와 레디 리스트에서 가장 높은 순위 비교후 우선순위가 아니라면 양보해야함
	test_max_priority();

	intr_set_level(old_level);
}


static void sema_test_helper (void *sema_);

/* 한 쌍의 스레드 사이에서 제어가 "핑퐁"하도록 하는 세마포어 자체 테스트.
  무슨 일이 일어나는지 보려면 printf() 호출을 삽입하세요. */
void
sema_self_test (void) {
   struct semaphore sema[2];
   int i;

   printf ("Testing semaphores...");
   sema_init (&sema[0], 0);
   sema_init (&sema[1], 0);
   thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
   for (i = 0; i < 10; i++)
   {
   	sema_up (&sema[0]);
   	sema_down (&sema[1]);
   }
   printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수. */
static void
sema_test_helper (void *sema_) {
   struct semaphore *sema = sema_;
   int i;

   for (i = 0; i < 10; i++)
   {
   	sema_down (&sema[0]);
   	sema_up (&sema[1]);
   }
}

/* LOCK을 초기화합니다. 락은 주어진 시간에 최대 하나의 스레드만 보유할 수 있습니다.
  우리의 락은 "재귀적"이지 않습니다. 즉, 현재 락을 보유하고 있는 스레드가 그 락을
  다시 획득하려고 시도하는 것은 오류입니다.

  락은 초기값이 1인 세마포어의 특수화입니다. 락과 그러한 세마포어의 차이점은
  두 가지입니다. 첫째, 세마포어는 1보다 큰 값을 가질 수 있지만, 락은 한 번에
  하나의 스레드만 소유할 수 있습니다. 둘째, 세마포어는 소유자가 없습니다.
  즉, 한 스레드가 세마포어를 "down"하고 다른 스레드가 "up"할 수 있지만,
  락의 경우 같은 스레드가 획득과 해제를 모두 해야 합니다. 이러한 제한이
  번거로울 때는 락 대신 세마포어를 사용해야 한다는 좋은 신호입니다. */
void
lock_init (struct lock *lock) {
   ASSERT (lock != NULL);

   lock->holder = NULL;
   sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득하며, 필요한 경우 사용 가능해질 때까지 잠듭니다. 락은 이미
  현재 스레드에 의해 보유되고 있어서는 안 됩니다.

  이 함수는 잠들 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다.
  이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 잠들어야 하는
  경우 인터럽트가 다시 켜집니다. */



// void
// lock_acquire (struct lock *lock) {

//    //락 요청하고 낮은 우선순위 스레드에게 자신의 우선순위를 기부
//    ASSERT (lock != NULL);
//    ASSERT (!intr_context ());
//    ASSERT (!lock_held_by_current_thread (lock));

//    sema_down (&lock->semaphore);
//    lock->holder = thread_current ();
// }

void lock_acquire(struct lock *lock){
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));
	
	struct thread *t= thread_current();
    
	if(lock->holder != NULL){
    	//현재 락을 기다림
		t->wait_on_lock =lock;
        
        //lock의 현재 홀더의 donations에 추가
		list_push_back(&lock->holder->donations, &t->donation_elem);
        
        //우선순위 기부
		donate_priority();
	}

	sema_down(&lock->semaphore);

	// 락을 획득했으니 기다리는 락이 없음
	t-> wait_on_lock= NULL;
    
    // 홀더를 이 스레드로 갱신
	lock->holder = t;
}

/*
	우선순위 기부를 수행
	nested donation을 고려하여 구현
	- 현재 스레드가 기다리고 있는 락과 연결된 모든 스레드를 순회하면서,
	- 현재 스레드의  우선순위를 "락을 보유하고 있는" 스레드에게 기부
	- nested depth는 8로 제한 (과도한 연산, 혹은 재귀적으로 기부를 전파할때 커널 스택 오버플로 방지)
*/

void donate_priority(){
	struct thread *t= thread_current();
	int priority= t->priority;

	for (int depth =0; depth<8; depth++){
		if(t->wait_on_lock==NULL){
			break;
		}
		t=t->wait_on_lock->holder;
		t->priority=priority;
	}
}

/*
	lock을 해제한 후, 현재 스레드의 대기 리스트 갱신 (remove_with_lock() 사용)
	priority를 기부받았을 수 있으므로 원래의 priority로 초기화
*/
void
lock_release (struct lock *lock) {
   ASSERT (lock != NULL);
   ASSERT (lock_held_by_current_thread (lock));

   lock->holder = NULL;

   remove_with_lock(lock);
   refresh_priority();

   sema_up (&lock->semaphore);
}

// 락을 획득 했을 때, waiters 리스트에서 해당 엔트리를 삭제하기 위한 함수를 구현
// 현재 스레드의 waiters 리스트를 확인하여 해지할 lock을 보유하고 있는 엔트리를 삭제

void remove_with_lock(struct lock *lock){
	struct thread *t= thread_current();
	struct list_elem *curr= list_begin(&t->donations);
	struct thread *curr_thread= NULL;

	while (curr != list_end(&t->donations))
	{
		curr_thread= list_entry(curr, struct thread, donation_elem);
		if(curr_thread->wait_on_lock==lock){
			list_remove(&curr_thread -> donation_elem);
		}
		curr= list_next(curr);
	}
}

/*
스레드의 우선순위가 변경되었을때, 도네이션을 고려하여 우선순위를 다시 결정하는 함수
현재 스레드의 우선순위를 기부받기 전의 우선순위로 변경
현재 스레드의 waiters 리스트에서 가장 높은 우선순위를 현재 쓰레드의 우선순위와 비교 후 우선순위 설정
*/
void refresh_priority(void){
	struct thread *t= thread_current();
	t->priority = t->init_priority;

	if(list_empty(&t->donations))
		return;
	
	list_sort(&t->donations, cmp_priority,NULL);

	struct list_elem *max_elem= list_front(&t->donations);
	struct thread *max_thread= list_entry(max_elem, struct thread, donation_elem);	

	if(t->priority < max_thread->priority){
		t->priority= max_thread->priority;
	}
}


/* LOCK 획득을 시도하고 성공하면 true를, 실패하면 false를 반환합니다.
  락은 이미 현재 스레드에 의해 보유되고 있어서는 안 됩니다.

  이 함수는 잠들지 않으므로 인터럽트 핸들러 내에서 호출될 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
   bool success;

   ASSERT (lock != NULL);
   ASSERT (!lock_held_by_current_thread (lock));

   success = sema_try_down (&lock->semaphore);
   if (success)
   	lock->holder = thread_current ();
   return success;
}

/* 현재 스레드가 소유해야 하는 LOCK을 해제합니다.
  이것이 lock_release 함수입니다.

  인터럽트 핸들러는 락을 획득할 수 없으므로 인터럽트 핸들러 내에서
  락을 해제하려고 시도하는 것은 의미가 없습니다. */
// void
// lock_release (struct lock *lock) {
//    ASSERT (lock != NULL);
//    ASSERT (lock_held_by_current_thread (lock));

//    lock->holder = NULL;
//    sema_up (&lock->semaphore);
// }

/* 현재 스레드가 LOCK을 보유하고 있으면 true를, 그렇지 않으면 false를 반환합니다.
  (다른 스레드가 락을 보유하고 있는지 테스트하는 것은 경쟁 조건이 될 수 있습니다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
   ASSERT (lock != NULL);

   return lock->holder == thread_current ();
}


/* 조건 변수 COND를 초기화합니다. 조건 변수는 한 코드 조각이 조건을 신호하고
  협력하는 코드가 신호를 받아 그에 따라 행동할 수 있게 합니다. */
void
cond_init (struct condition *cond) {
   ASSERT (cond != NULL);

   list_init (&cond->waiters);
}

/* 원자적으로 LOCK을 해제하고 다른 코드 조각에 의해 COND가 신호될 때까지 기다립니다.
  COND가 신호된 후, 반환하기 전에 LOCK이 다시 획득됩니다. 이 함수를 호출하기 전에
  LOCK을 보유해야 합니다.

  이 함수에 의해 구현된 모니터는 "Hoare" 스타일이 아닌 "Mesa" 스타일입니다.
  즉, 신호 보내기와 받기가 원자적 연산이 아닙니다. 따라서 일반적으로 호출자는
  대기가 완료된 후 조건을 다시 확인하고, 필요한 경우 다시 기다려야 합니다.

  주어진 조건 변수는 단 하나의 락과만 연관되지만, 하나의 락은 임의의 수의
  조건 변수와 연관될 수 있습니다. 즉, 락에서 조건 변수로의 일대다 매핑이 있습니다.

  이 함수는 잠들 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다.
  이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 잠들어야 하는
  경우 인터럽트가 다시 켜집니다. */


  /*
	Hoare: signal 보내기와 받기가 원자적
	Mesa: signal과 wake-up 사이에 다른 스레드가 개입 가능
	깨어난 후 락을 다시 획득하는 동안 다른 스레드가 조건을 바꿀 수 있는 Mesa 스타일
  */
//condition variable의 waiters list에 우선순위 순서로 삽입되도록 수정
void
cond_wait (struct condition *cond, struct lock *lock) {
   struct semaphore_elem waiter;

   ASSERT (cond != NULL);
   ASSERT (lock != NULL);
   ASSERT (!intr_context ());
   ASSERT (lock_held_by_current_thread (lock));

   sema_init (&waiter.semaphore, 0);
   //list_push_back (&cond->waiters, &waiter.elem);
   list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority,NULL);
   lock_release (lock);
   sema_down (&waiter.semaphore);
   lock_acquire (lock);
}

/* COND에서 기다리고 있는 스레드가 있다면 (LOCK에 의해 보호됨), 이 함수는
  그 중 하나에게 대기에서 깨어나라고 신호합니다. 이 함수를 호출하기 전에
  LOCK을 보유해야 합니다.

  인터럽트 핸들러는 락을 획득할 수 없으므로 인터럽트 핸들러 내에서
  조건 변수에 신호를 보내려고 시도하는 것은 의미가 없습니다. */

  /*
   condition variable의 waiters list를 우선순위로 재 정렬
   대기 중에 우선순위가 변경되었을 가능성이 있음
  */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
   ASSERT (cond != NULL);
   ASSERT (lock != NULL);
   ASSERT (!intr_context ());
   ASSERT (lock_held_by_current_thread (lock));

   if (!list_empty (&cond->waiters))
   	list_sort(&cond->waiters, cmp_sem_priority, NULL);
   	sema_up (&list_entry (list_pop_front (&cond->waiters),
   				struct semaphore_elem, elem)->semaphore);
}

/* COND에서 기다리고 있는 모든 스레드를 깨웁니다 (LOCK에 의해 보호됨).
  이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

  인터럽트 핸들러는 락을 획득할 수 없으므로 인터럽트 핸들러 내에서
  조건 변수에 신호를 보내려고 시도하는 것은 의미가 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
   ASSERT (cond != NULL);
   ASSERT (lock != NULL);

   while (!list_empty (&cond->waiters))
   	cond_signal (cond, lock);
}