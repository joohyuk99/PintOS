/* The main thread acquires locks A and B, then it creates two
   higher-priority threads.  Each of these threads blocks
   acquiring one of the locks and thus donate their priority to
   the main thread.  The main thread releases the locks in turn
   and relinquishes its donated priorities.
   
   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;

void
test_priority_donate_multiple (void) 
{
  struct lock a, b;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&a);
  lock_init (&b);

//   printf("0️⃣[%s] lock_acquire(&a) 호출\n", thread_current()->name);
  lock_acquire (&a);
//   printf("0️⃣[%s] lock_acquire(&b) 호출\n", thread_current()->name);
  lock_acquire (&b);

  thread_create ("a", PRI_DEFAULT + 1, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  thread_create ("b", PRI_DEFAULT + 2, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());

//   printf("3️⃣[%s] lock_release(&b) 호출\n", thread_current()->name);
  lock_release (&b);
  msg ("Thread b should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

//   printf("5️⃣[%s] lock_release(&a) 호출\n", thread_current()->name);
  lock_release (&a);
  msg ("Thread a should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
}

static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;
//   printf("1️⃣[%s] lock_acquire(lock) 호출\n", thread_current()->name);
  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
//   printf("6️⃣[%s] lock_release(lock) 호출\n", thread_current()->name);
  lock_release (lock);
  msg ("Thread a finished.");
}

static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;
//   printf("2️⃣[%s] lock_acquire(lock) 호출\n", thread_current()->name);
  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
//   printf("4️⃣[%s] lock_release(lock) 호출\n", thread_current()->name);
  lock_release (lock);
  msg ("Thread b finished.");
}
