#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* mlfqs 구현 */
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
/* 커널 스레드 또는 사용자 프로세스.
 *
 * 각 스레드 구조체는 자체 4 kB 페이지에 저장됩니다.
 * 스레드 구조체 자체는 페이지의 가장 아래쪽(오프셋 0)에 위치합니다.
 * 페이지의 나머지 부분은 스레드의 커널 스택에 예약되어 있으며,
 * 이는 페이지의 맨 위(오프셋 4 kB)에서 아래로 성장합니다. 다음은 그 예시입니다:
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택              |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         아래로 성장              |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 이로 인해 두 가지 중요한 점이 있습니다:
 *
 *    1. 첫째, `struct thread'는 너무 커지지 않도록 해야 합니다.
 *       그렇지 않으면 커널 스택에 충분한 공간이 없게 됩니다.
 *       기본 `struct thread'는 몇 바이트에 불과합니다.
 *       이는 1 kB 이하로 유지되어야 합니다.
 *
 *    2. 둘째, 커널 스택이 너무 커지지 않도록 해야 합니다.
 *       스택이 오버플로우되면 스레드 상태가 손상됩니다.
 *       따라서 커널 함수는 큰 구조체나 배열을 비정적 지역 변수로 할당하지 않아야 합니다.
 *       대신 malloc() 또는 palloc_get_page()를 사용하여 동적 할당을 해야 합니다.
 *
 * 이러한 문제의 첫 번째 증상은 아마도 thread_current()에서의
 * 어설션 실패일 것입니다. 이는 실행 중인 스레드의 `struct thread'의
 * `magic' 멤버가 THREAD_MAGIC으로 설정되어 있는지 확인합니다.
 * 스택 오버플로우는 일반적으로 이 값을 변경하여 어설션을 트리거합니다. */
/* `elem` 멤버는 두 가지 용도로 사용됩니다. 
 * 이는 실행 대기열(thread.c)의 요소가 될 수 있으며,
 * 세마포어 대기 목록(synch.c)의 요소가 될 수도 있습니다.
 * 이 두 가지 방식으로 사용될 수 있는 이유는 상호 배타적이기 때문입니다:
 * 준비 상태에 있는 스레드만 실행 대기열에 있으며,
 * 차단 상태에 있는 스레드만 세마포어 대기 목록에 있습니다. */
struct thread {
	int64_t wakeup_time;
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. - 리스트 요소 구조체를 만듦으로써 리스트를 사용하기 위함 */
	
	/* donation 구현 */
	int original_priority; /* 원래 우선순위 */

	struct lock *wait_on_lock; /* 현재 스레드가 얻기 위해 기다리고 있는 lock */
	struct list donations_list; /* 우선순위를 기부해준 스레드들의 리스트 */
	struct list_elem donation_elem; /* donaton 리스트를 사용하기 위함 */

	/* mlfqs */
	int nice;
	int recent_cpu;
	struct list_elem all_elem;
	

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux); // 함수 포인터 타입 정의 - 쓰레드가 실행할 함수의 형태
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

void do_iret (struct intr_frame *tf);

void thread_sleep (int64_t ticks);
void thread_wakeup (int64_t ticks);
bool wakeup_time_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool thread_priority_higher(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void thread_test_preemption(void);
bool donation_priority_higher(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

/* mlfqs 구현 */
void mlfqs_calculate_priority (struct thread *t);
void mlfqs_calculate_recent_cpu (struct thread *t);
void mlfqs_calculate_load_avg();
void mlfqs_increment_recent_cpu(void);
void mlfqs_recalculate_recent_cpu(void);
void mlfqs_recalculate_priority(void);

void thread_set_nice(int nice UNUSED);
int thread_get_nice (void);
int thread_get_load_avg (void);
int thread_get_recent_cpu (void);



#endif /* threads/thread.h */