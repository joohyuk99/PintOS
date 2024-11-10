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

/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는
   두 개의 원자적 연산을 통해 조작할 수 있는 비음수 정수입니다:

   - down 또는 "P": 값이 양수가 될 때까지 기다린 후, 값을 감소시킵니다.

   - up 또는 "V": 값을 증가시키고, 대기 중인 스레드가 있으면 하나를 깨웁니다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산입니다. SEMA의 값이
   양수가 될 때까지 기다린 후, 값을 원자적으로 감소시킵니다.

   이 함수는 대기 상태가 될 수 있으므로 인터럽트 핸들러 내에서
   호출되어서는 안 됩니다. 이 함수는 인터럽트가 비활성화된 상태에서
   호출될 수 있지만, 대기 상태가 되면 다음에 스케줄된 스레드가
   아마도 인터럽트를 다시 활성화할 것입니다. 이것이 sema_down 함수입니다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) { // 세마포어의 값이 양수가 될 때까지 대기
		// printf("⏸️ sema_down 실행: %lld\n", sema->value);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_priority_higher, NULL); // 현재 스레드를 세마포어의 대기자 목록에 추가
		// printf("🔍 sema_down 실행: 현재 스레드: %s(%lld), 대기자 목록: %s(%lld)\n", thread_current()->name, thread_current()->priority, list_entry(list_back(&sema->waiters), struct thread, elem)->name, list_entry(list_back(&sema->waiters), struct thread, elem)->priority);
		thread_block (); // 현재 스레드를 블록 상태로 전환
	}
	sema->value--; // 세마포어의 값을 감소시킴
	intr_set_level (old_level); // 이전 인터럽트 상태로 복원
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

/* 세마포어에 대한 up 또는 "V" 연산입니다. SEMA의 값을
   증가시키고, SEMA를 기다리고 있는 스레드 중 하나를 깨웁니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		// 대기자 목록 우선순위 순으로 정렬
		list_sort(&sema->waiters, thread_priority_higher, NULL);

		// 우선순위가 가장 높은 스레드를 깨움
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	thread_test_preemption();
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

	if (thread_mlfqs) {
		sema_down(&lock->semaphore);
		lock->holder = thread_current();
		return;
	}

	struct thread *curr = thread_current();

	if (lock->holder != NULL) {
		curr->wait_on_lock = lock; // 현재 스레드가 기다릴 lock을 저장
		list_insert_ordered(&lock->holder->donations_list, &curr->donation_elem, donation_priority_higher, NULL); // lock 소유자 스레드의 donations_list에 현재 스레드를 추가
		
		/* 우선 순위 기부 */
		// int donations_size = list_size(&lock->holder->donations_list);
		for (int i = 0; i <= 8; i++) {
			if (curr->wait_on_lock == NULL) break;
			curr->wait_on_lock->holder->priority = curr->priority; // 우선순위를 기부
			curr = curr->wait_on_lock->holder; // 기부한 스레드로 이동
		}
	}
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
	thread_current()->wait_on_lock = NULL;
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

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
    ASSERT (lock != NULL);
    ASSERT (lock_held_by_current_thread (lock));

	if (thread_mlfqs) {
		lock->holder = NULL;
		sema_up(&lock->semaphore);
		return;
	}

    // donations_list에서 현재 lock과 관련된 기부된 우선순위 제거
    struct list_elem *e;
	struct thread *holder = thread_current();

    for (e = list_begin(&holder->donations_list); e != list_end(&holder->donations_list); e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, donation_elem);
        if (t->wait_on_lock == lock)
            list_remove(&t->donation_elem);
    }

	refresh_priority();

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
	// 우선순위대로 waiter 리스트에 정렬
	list_insert_ordered(&cond->waiters, &waiter.elem, sema_priority_higher, NULL);
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
	{
		list_sort(&cond->waiters, sema_priority_higher, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
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

/* priority-condvar 구현 */
bool sema_priority_higher(const struct list_elem *a, const struct list_elem *b, void *aux) {
	// sema의 waiters 리스트에서 맨 앞의 스레드의 우선순위 비교
	struct semaphore_elem *sema1 = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema2 = list_entry(b, struct semaphore_elem, elem);

	struct list *sema1_waiters = &(sema1->semaphore.waiters);
	struct list *sema2_waiters = &(sema2->semaphore.waiters);

	struct thread *thread1 = list_entry(list_begin(sema1_waiters), struct thread, elem);
	struct thread *thread2 = list_entry(list_begin(sema2_waiters), struct thread, elem);

	return thread1->priority > thread2->priority;
}

/* donation 구현 */
void refresh_priority(void) {
	// 현재 실행 중인 스레드가 lock의 소유자
	struct thread *holder = thread_current();
	// 일단 원래 우선 순위로 복구
	holder->priority = holder->original_priority;

    // donations_list에 남아있는 기부된 우선순위가 있으면 가장 높은 우선순위로 설정
    if (!list_empty(&holder->donations_list)) {
		list_sort(&holder->donations_list, donation_priority_higher, NULL);

		struct thread *front = list_entry(list_front(&holder->donations_list), struct thread, donation_elem);
		if (holder->priority < front->priority) {
			holder->priority = front->priority;
		}
	}
}