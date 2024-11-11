/* Tests that cond_signal() wakes up the highest-priority thread
   waiting in cond_wait(). */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_condvar_thread;
static struct lock lock;
static struct condition condition;

void
test_priority_condvar (void) 
{
  int i;
  
  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  lock_init (&lock);
  cond_init (&condition);

  thread_set_priority (PRI_MIN);
  for (i = 0; i < 10; i++) 
  {
    // 23 22 21 30 29 28 27 26 25 24
    int priority = PRI_DEFAULT - (i + 7) % 10 - 1;
    char name[16];
    snprintf (name, sizeof name, "priority %d", priority);
    thread_create (name, priority, priority_condvar_thread, NULL);
  }


  for (i = 0; i < 10; i++) 
  {
    lock_acquire (&lock);
    msg ("Signaling...");
    cond_signal (&condition, &lock); // 3. 모든 스레드를 깨움
    lock_release (&lock);
  }
}

static void
priority_condvar_thread (void *aux UNUSED) 
{
  msg ("Thread %s starting.", thread_name ()); // 1. 각 스레드가 이 메세지를 출력하고
  lock_acquire (&lock);
  cond_wait (&condition, &lock); // 2. break point처럼 스레드를 대기 시키는 역할
  msg ("Thread %s woke up.", thread_name ()); // 4. 각 스레드가 우선순위 순서대로 이 메세지를 출력
  lock_release (&lock);
}

