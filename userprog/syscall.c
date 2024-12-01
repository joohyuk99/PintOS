#define USERPROG

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/palloc.h"
#include "threads/init.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"

#include "vm/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct file *process_get_file (int fd);
void process_close_file(int fd);

void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int exec(const char *cmd_line);
int fork(const char *thread_name, struct intr_frame *_if);
int wait(int pid);
int open(const char *file);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
void check_valid_buffer(void *buffer, size_t size, void *rsp, bool writable);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

#ifndef VM
void addr_validation(const void *addr) {
    struct thread *cur = thread_current ();
    if (addr == NULL || !is_user_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL) 
        exit(-1);        
}
#else
struct page* addr_validation(const void *addr) {
	struct thread *cur = thread_current();
	if(addr == NULL || is_kernel_vaddr(addr))
		exit(-1);
	return spt_find_page(&cur->spt, addr);
}
#endif

void process_close_file(int fd)
{
    struct thread *cur = thread_current();
    struct file **fd_table = cur->fd_table;
    if (fd < 2 || fd >= FDT_COUNT_LIMIT)
        return NULL;
    fd_table[fd] = NULL;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

#ifdef VM
	thread_current()->stack_pointer = f->rsp;
#endif

	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
        	break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		case SYS_READ:
			check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, true);
	        f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, false);
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_MMAP:
			f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;
		case SYS_MUNMAP:
			munmap(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	// printf ("system call!\n");
	// thread_exit ();
	}
}

int exec(const char *cmd_line) {
	addr_validation(cmd_line);
	
	if (process_exec((void *)cmd_line) < 0)
        exit(-1);
	NOT_REACHED();
}

void halt(void) {
	power_off();
}

void exit(int status) { printf("%d exec\n", thread_current()->tid);
	struct thread *cur = thread_current();
	cur->exit_status = status;

	printf("%s: exit(%d)\n", thread_name(), cur->exit_status);
	thread_exit();
}

bool create(const char *file, unsigned initial_size) { printf("%d create\n", thread_current()->tid);
	addr_validation(file);
	lock_acquire(&filesys_lock);
	bool ret = filesys_create(file, initial_size);		// 파일 이름과 파일 사이즈를 인자 값으로 받아 파일을 생성하는 함수
	// printf("create %d\n", thread_current()->tid);
	lock_release(&filesys_lock);
	return ret;
}

bool remove(const char *file) {
	addr_validation(file);
	lock_acquire(&filesys_lock);
	bool ret = filesys_remove(file);					// 파일 제거 성공 시 true, 실패 시 false
	lock_release(&filesys_lock);
	return ret;
}

int fork(const char *thread_name, struct intr_frame *_if) { printf("%d fork\n", thread_current()->tid);
	// printf("parent %d\n\n", thread_current()->tid);
	addr_validation(thread_name);
	int t = process_fork(thread_name, _if);
	// printf("current %d %d\n\n", thread_current()->tid, t);
	return t;
}

int wait(int pid) {
	return process_wait(pid);
}

int open(const char *file) {
	addr_validation(file);
	lock_acquire(&filesys_lock);
	struct file *file_open = filesys_open(file);
	if (file_open == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}
	int fd = process_add_file(file_open);
	if (fd == -1) 
		file_close(file_open);
	lock_release(&filesys_lock);
	return fd;
	// return -1;
}

int filesize(int fd) {
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return -1;
	return file_length(file);
}

struct file *process_get_file (int fd) {
	struct thread *cur = thread_current();
	if (fd < 0 || fd >= FDT_COUNT_LIMIT)
		return NULL;
	return cur->fd_table[fd];
}

void seek(int fd, unsigned position) {
	struct file *file = process_get_file(fd);
	if (file <= 2)
		return;
	file->pos = position;
}

unsigned tell(int fd) {
	struct file *file = process_get_file(fd);
	if (file <= 2)
		return;
	return file_tell(file);
}

void close(int fd) {
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	file_close(file);
	process_close_file(fd);
}

int read(int fd, void *buffer, unsigned size) {
    if (fd < 0 || fd == 1 || fd >= FDT_COUNT_LIMIT)		// fd값 검증
        exit(-1);

	addr_validation(buffer);					// buffer 주소 검증

    if (fd == 0) {
        unsigned i;
        char *buf = (char *)buffer;
        for (i = 0; i < size; i++) 
            buf[i] = input_getc();
        return size;
    }

	lock_acquire(&filesys_lock);
    struct file *file = process_get_file(fd);
    if (file == NULL) {
		lock_release(&filesys_lock);
        return -1;
	}

    int bytes_read = file_read(file, buffer, size);
	lock_release(&filesys_lock);

    return bytes_read;
}


int write(int fd, void *buffer, unsigned size) {
    if (fd < 0 || fd >= FDT_COUNT_LIMIT)		// fd값 검증
        exit(-1);

	addr_validation(buffer);					// buffer 주소 검증
    
    if (fd == 0) 
        return -1;

    if (fd == 1) {
        putbuf(buffer, size);
        return size;
    }

    struct file *file = process_get_file(fd);
    if (file == NULL)
        return -1;
    
    int bytes_write = file_write(file, buffer, size);
    return bytes_write;
}

/* Project 3 */
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset) {
#ifdef VM
	if(!addr || pg_round_down(addr) != addr)
		return NULL;
	
	if(is_kernel_vaddr(addr) || is_kernel_vaddr(addr + length))
		return NULL;
	
	if(offset != pg_round_down(offset) || offset % PGSIZE != 0)
		return NULL;
	
	if(spt_find_page(&thread_current()->spt, addr))
		return NULL;

	if(fd == STDIN_FILENO || fd == STDOUT_FILENO)
		return NULL;

	struct file *file = process_get_file(fd);

	if(file == NULL)
		return NULL;

	if(file_length(file) == 0 || (long)length <= 0)
		return NULL;

	void* t = do_mmap(addr, length, writable, file, offset);
	// printf("%p\n", t);
	return t;
#endif
}

void munmap(void *addr) {
#ifdef VM
	do_munmap(addr);
#endif
}

void check_valid_buffer(void *buffer, size_t size, void *rsp, bool writable) {

#ifdef VM
    for (size_t i = 0; i < size; i++) {
        /* buffer가 spt에 존재하는지 검사 */
        struct page *page = addr_validation(buffer + i);
        if (page == NULL)
            exit(-1);
        if (writable == true && page->writable == false)
            exit(-1);
    }
#else
	return NULL;
#endif
}