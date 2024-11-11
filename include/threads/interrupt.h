#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

/*  인터럽트 프레임 구조체 - TCB(Thread Control Block)의 일부?
	인터럽트가 발생했을 때 CPU의 상태를 저장하는 데 사용되는 데이터 구조체
	인터럽트 핸들러가 호출될 때, CPU의 레지스터와 관련 정보를 저장하여
	인터럽트 처리가 끝난 후 원래의 실행 상태로 복구하기 위함
*/
struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   이 코드는 인터럽트가 발생한 작업의 저장된 레지스터를 나타냅니다. */
	struct gp_registers R;
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;
	uint16_t __pad3;
	uint32_t __pad4;
	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* 인터럽트 벡터 번호 */
/* 가끔 프로세서에 의해 푸시되고,
   그렇지 않으면 intrNN_stub에 의해 0으로 푸시됩니다.
   프로세서는 그것을 `eip' 바로 아래에 두지만, 여기에 둡니다. */
	uint64_t error_code;
/* 프로세서에 의해 푸시됩니다.
   이것은 인터럽트된 작업의 저장된 레지스터입니다. */
	uintptr_t rip;
	uint16_t cs;
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags;
	uintptr_t rsp;
	uint16_t ss;
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
