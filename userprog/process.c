#define USERPROG

#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

#include "threads/synch.h"


void dump_page_table(uint64_t *table) {
    printf("Dumping page table at %p:\n", table);
    if (!is_user_vaddr(table)) {
        printf("  Invalid page table address.\n");
        return;
    }
    for (int i = 0; i < 512; i++) {
        if (table[i] != 0) {
            uint64_t physical_address = table[i] & 0x000FFFFFFFFFF000;
            uint64_t flags = table[i] & 0xFFF;
            void *logical_address = ptov(physical_address);
            if (!is_user_vaddr(logical_address)) {
                printf("  Entry %d: Invalid logical address for Physical Address = %p\n", i, (void *)physical_address);
                continue;
            }
            printf("  Entry %d: Physical Address = %p, Logical Address = %p, Flags = 0x%03lx\n", 
                   i, (void *)physical_address, logical_address, flags);
        }
    }
}


void dump_pml4(uint64_t *pml4) {
    printf("🛞 Dumping pml4 at %p:\n", pml4);
    for (int i = 0; i < 512; i++) {
        if (pml4[i] != 0) {
            uint64_t physical_address = pml4[i] & 0x000FFFFFFFFFF000; // 물리 주소 추출
            uint64_t flags = pml4[i] & 0xFFF; // 플래그 추출
            void *logical_address = ptov(physical_address); // 물리 주소 -> 논리 주소 변환
            printf("PML4 Entry %d: Physical Address = %p, Logical Address = %p, Flags = 0x%03lx\n", 
                   i, (void *)physical_address, logical_address, flags);
            
            // 하위 페이지 테이블 덤프
            dump_page_table((uint64_t *)logical_address);
        }
    }
	printf("\n\n");
}


static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	/* argument passing */
	char *save_ptr;
	strtok_r(file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

int process_add_file(struct file *f) {
	struct thread *cur = thread_current();
	struct file **fd_table = cur->fd_table;

	while (cur->next_fd < FDT_COUNT_LIMIT && fd_table[cur->next_fd])
		cur->next_fd ++;
	if (cur->next_fd >= FDT_COUNT_LIMIT)
		return -1;
	fd_table[cur->next_fd] = f;

	return cur->next_fd;
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* 현재 스레드를 복제하여 새로운 스레드를 생성
	 * [TODO] 부모의 intr_frame를 자식에게 전달하기 위해 thread_create에 parent_if를 전달할 수 있도록 수정 필요 */
	struct thread *cur = thread_current();
	// struct intr_frame *f = (pg_round_up(rrsp()) - sizeof(struct intr_frame));
	// memcpy(&cur->parent_if, f, sizeof(struct intr_frame));
	memcpy(&cur->parent_if, if_, sizeof(struct intr_frame));

	/* Clone current thread to new thread.*/
	// dump_pml4(thread_current()->pml4);
	tid_t pid = thread_create (name, PRI_DEFAULT, __do_fork, cur);

	if (pid == TID_ERROR)
		return TID_ERROR;

	struct thread *child = get_child_process(pid);
	sema_down(&child->load_sema);

	if (child->exit_status == TID_ERROR) {
		sema_up(&child->exit_sema);
		return TID_ERROR;
	}
	return pid;
}

struct thread *get_child_process(int pid) {
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid) 
			return t;
	}
	return NULL;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. [TODO] 부모의 페이지가 커널 영역에 있는 경우, 즉시 리턴 */
	if (is_kernel_vaddr(va))
		return true;
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL)
		return false;
	/* 3. [TODO] 자식 프로세스를 위해 새로운 유저페이지를 할당하고 newpage에 저장 */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (newpage == NULL)
		return false;
	/* 4. [TODO] 부모의 페이지 내용을 newpage에 복사하고, 페이지 쓰기 가능 여부 확인 */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. [TODO] 페이지 삽입에 실패할 경우 에러처리 수행 */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* [TODO] 부모의 intr_frame를 전달하기 위해 aux를 구조체로 만들어 parent와 parent_if를 함께 전달 */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; 

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;
       
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* [TODO] 부모 프로세스의 파일 디스크립터 테이블 복제 (file_duplicate() 사용)
			  부모는 자식의 리소스 복제가 완료될 때까지 반환되지 않아야함. */
	for (int i = 0; i < FDT_COUNT_LIMIT; i++) {
		struct file *file = parent->fd_table[i];
		if (file == NULL)
			continue;
		if (file > 2)
			current->fd_table[i] = file_duplicate(file);
		else
			current->fd_table[i] = file;
	}
	current->next_fd = parent->next_fd;
	sema_up (&current->load_sema);
	process_init ();
// dump_pml4(current->pml4);
	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	sema_up(&current->load_sema);
	exit(-1);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name =  (char *)palloc_get_page(PAL_ZERO);
	if (file_name == NULL) {
		exit(-1);
	}

	strlcpy(file_name, (char *)f_name, strlen(f_name) + 1);
	bool success;
	/* [TODO] 프로그램의 인자를 파싱하여 스택에 적재해야함
			-> 스택 포인터 조정, 인자 저장, argv 포인터 설정 필요 */
	char *argv[128], *token, *save_ptr;
	int argc = 0;
	/* 받은 문자열 파싱 */
	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
		argv[argc++] = token;
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name, &_if);
	if (!success) {
		palloc_free_page (file_name);
		exit(-1);
	}

	/* 파싱하고 남은 문자열 스택에 저장 */
	argument_stack(argv, argc, &_if.rsp);
	_if.R.rdi = argc;			    	// argc -> RDI.
	_if.R.rsi = (char *)_if.rsp + 8;	// argv -> RSI. 유저 스택에 쌓은 argv의 주소를 가져와야 하므로 _if.rsp+8 
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true); // user stack을 16진수로 프린트

	/* If load failed, quit. */
	palloc_free_page (file_name);		/* 작업이 끝났으므로 동적할당한 file_name이 담긴 메모리 free */

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

void argument_stack(char **argv, int argc, void **rsp) {
	for (int i = argc-1; i > -1; i--) {
		for (int j = strlen(argv[i]); j > -1; j--) {
			(*rsp)--;							// 스택주소 지정
			**(char **)rsp = argv[i][j];		// 스택 주소에 문자 저장
		}
		argv[i] = *(char **)rsp;				// argv에 현재 rsp(스택주소)값 저장
	}

	int padding = (int)*rsp % 8;
	for (int i = 0; i < padding; i++) {
		(*rsp)--;
		**(uint8_t **)rsp = 0;					// rsp 직전까지 패딩으로 채우기
	}
	(*rsp) -= 8;
	**(char ***)rsp  = 0;						// char* 타입의 0 push (argument 문자열 종료 표현)

	for (int i = argc-1; i > -1; i--) {			// argument address push
		(*rsp) -= 8;
		**(char ***)rsp = argv[i];
	}

	(*rsp) -= 8;
	**(void ***)rsp = 0;						// void* 타입의 0 push (return address)

}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* [TODO] 자식 프로세스의 종료 상태를 저장할 자료 구조 필요
			  -> thread 구조체의 자식 프로세스의 리스트 추가하고 각 자식 프로세스의 종료 상태 관리하기 */
	
	/* [TODO] 주어진 child_tid가 현재 프로세스의 자식인지 확인 (아닐 경우 -1 반환) */
	struct thread *child = get_child_process(child_tid);
	if (child == NULL)
		return -1;
	/* [TODO] 이미 해당 자식에 대해 process_wait이 호출되었는지 확인 (이미 호출되었으면 -1 반환) */
	
	/* [TODO] 자식 프로세스가 종료될 때까지 대기하기
			  -> 세마포어 또는 컨디션배리어블 사용하여 동기화 필요 */
	sema_down(&child->wait_sema);
	/* [TODO] 자식 프로세스가 종료되면 그 상태 반환하고, 자식 프로세스의 자료구조를 정리하여 메모리 누수 방지 */
	list_remove(&child->child_elem);
	sema_up(&child->exit_sema);

	return child->exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *cur = thread_current ();
	/* [TODO] 프로세스 종료 메세지 구현, 프로세스의 리소스를 정리하는 코드 추가 필요 */
	for (int i = 0; i < FDT_COUNT_LIMIT; i++) {
		if (cur->fd_table[i] != NULL)
			close(i);
	}
	palloc_free_multiple(cur->fd_table, FDT_PAGES);		//multi-oom 실패 사유
	file_close(cur->running);
	process_cleanup();
	sema_up(&cur->wait_sema);
	sema_down(&cur->exit_sema);
}


/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL); 
		// printf("pml4 %p\n\n", pml4);
		// printf("is kernel valid %d\n\n", is_kernel_vaddr(pml4));
		// dump_pml4(pml4);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	t->running = file;
	file_deny_write(file);
	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* [TODO] 프로그램의 인자들을 파싱하여 스택에 적재하는 코드 작성
			  -> file_name에서 프로그램 이름과 인자들을 분리하고, setup_stack() 수정하여 스택에 인자들 올리기 */
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
			/* [TODO] 스택에 프로그램의 인자들을 적재하는 작업 수행
					  -> 인자들을 역순으로 스택에 저장, 스택 포인터 조정, argv 배열 구성, argc 설정 */
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */

	if(page == NULL)
		return false;

	struct container *container = aux;
	struct file *file = container->file;
	off_t offset = container->offset;
	size_t page_read_bytes = container->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	file_seek(file, offset);

	if(file_read(file, page->frame->kva, page_read_bytes) != (off_t)page_read_bytes) {
		palloc_free_page(page->frame->kva);
		return false;
	}

	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;

		struct container *container = (struct container*)malloc(sizeof(struct container));
		container->file = file;
		container->offset = ofs;
		container->page_read_bytes = page_read_bytes;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, container))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	if(vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) {
		success = vm_claim_page(stack_bottom);

		if(success) {
			if_->rsp = USER_STACK;
			thread_current()->stack_bottom = stack_bottom;
		}
	}

	return success;
}
#endif /* VM */
