/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

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

/* 우선순위 비교 함수 (semaphore waiters용) */
static bool
priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *thread_a = list_entry (a, struct thread, elem);
	struct thread *thread_b = list_entry (b, struct thread, elem);
	
	return thread_a->priority > thread_b->priority;
}

/* 우선순위 비교 함수 (donation list용) */
static bool
donation_priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *thread_a = list_entry (a, struct thread, donation_elem);
	struct thread *thread_b = list_entry (b, struct thread, donation_elem);
	
	return thread_a->priority > thread_b->priority;
}

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back(&sema->waiters, &(thread_current()->elem));
		thread_block();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
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

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	// if (!list_empty (&sema->waiters))
	// 	thread_unblock (list_entry (list_pop_front (&sema->waiters),
	// 				struct thread, elem));
	// sema->value++;
	// intr_set_level (old_level);

	sema->value++;
	if(!list_empty (&sema->waiters))
	{
		list_sort(&sema->waiters, priority_compare, NULL); // 정렬(우선순위 기준 내림차순)

		struct thread* unblocked_thread = list_entry(list_pop_front (&sema->waiters), struct thread, elem);
		thread_unblock(unblocked_thread);
		
		// 깨운 스레드의 우선순위가 현재 스레드보다 높으면 양보(선점)
		if(unblocked_thread->priority > thread_current()->priority)
		{
			thread_yield();
		}
	}

	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
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

/* Thread function used by sema_self_test(). */
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

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

void donate_priority(struct thread* giver, struct thread* receiver)
{
	if(receiver == NULL || giver == NULL) 
		return;

	int depth = 0; // 8단계 이상 기부 방지
	struct thread* current_giver = giver;
	struct thread* current_receiver = receiver;
	
	// 체인을 따라가면서 기부 수행
	while(current_receiver != NULL && depth < 8)
	{
		// 기부할 필요가 없으면 중단
		if(current_giver->priority <= current_receiver->priority)
		{
			break;
		}
			
		// 우선순위 기부
		current_receiver->priority = current_giver->priority;
		
		// 중복 방지 : receiver의 donation_list에 이미 giver가 있으면 제거
		if(!list_empty(&current_receiver->donation_list))
		{
			struct list_elem* element = list_begin(&current_receiver->donation_list);
			while(element != list_end(&current_receiver->donation_list))
			{
				struct thread* td = list_entry(element, struct thread, donation_elem);
				if(td == current_giver)
				{
					// 이미 기부한 기록이 있으면 제거
					list_remove(element);
					break;
				}
				element = list_next(element);
			}
		}
		// receiver의 donation_list에 giver 추가 (receiver가 giver로부터 기부받았다는 기록)
		list_insert_ordered(&current_receiver->donation_list, &current_giver->donation_elem, donation_priority_compare, NULL);
		
		// 다음 단계로 진행 (nested donation)
		if(current_receiver->waiting_lock != NULL && current_receiver->waiting_lock->holder != NULL)
		{
			current_giver = current_receiver;  // 현재 receiver가 다음 giver
			current_receiver = current_receiver->waiting_lock->holder;
			depth++;
		}
		else
		{
			break; // 더 이상 진행할 곳이 없음
		}
	}
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	// lock 소유 스레드가 있으면 우선순위 기부
	struct thread* current_thread = thread_current();
	if(lock->holder != NULL)
	{
		current_thread->waiting_lock = lock;
		donate_priority(current_thread, lock->holder);
	}

	sema_down (&lock->semaphore);
	
	// lock 획득 성공 후 정리
	current_thread->waiting_lock = NULL;
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
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

void remove_donations(struct lock *lock)
{
	struct thread* current_thread = thread_current();
	struct list_elem* elem = list_begin(&current_thread->donation_list);

	while(elem != list_end(&current_thread->donation_list))
	{
		struct thread* td = list_entry(elem, struct thread, donation_elem);

		// td가 기다리는 lock이 인자로 받은 lock과 같다면, donation_list에서 제거
		if(td->waiting_lock == lock)
		{
			elem = list_remove(elem);
		} 
		else
		{
			// 기다리던 lock이 아니면, 다음 원소로 이동
			elem = list_next(elem);
		}
	}
}

void update_priority_of_thread(struct thread* t)
{
	// donation_list가 우선순위 순으로 정렬되어 있으므로, 첫 번째 요소가 가장 높은 우선순위를 가짐
	if(!list_empty(&(t->donation_list)))
	{
		struct thread* highest_donor = list_entry(list_front(&t->donation_list), struct thread, donation_elem);
		t->priority = (highest_donor->priority > t->base_priority) ? highest_donor->priority : t->base_priority;
	}
	else
	{
		// donation이 없으면 기본 우선순위로 복원
		t->priority = t->base_priority;
	}
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	// donation 정리 및 우선순위 업데이트
	remove_donations(lock);
	update_priority_of_thread(thread_current());
	
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
