#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "lib/user/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

void halt(void);
void exit(int status);
pid_t fork(const char *thread_name);
int exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

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
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!: %d %d\n", f->R.rax, f->R.rdi);
	switch(f->R.rax) {
		case SYS_HALT: 
			halt();
		break;
		case SYS_EXIT: 
			exit(f->R.rdi);
		break;
		case SYS_FORK: 
			thread_current()->parent_if = *f;
			f->R.rax = fork(f->R.rdi);
		break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
		break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
		break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
		break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
		break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
		break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
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
		default:
			thread_exit ();
	}
}

void halt(void) {
	power_off();
}

void exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, curr->exit_status);
	thread_exit();
}

pid_t fork(const char *thread_name) {
	return process_fork(thread_name, &thread_current()->parent_if);
}

int exec(const char *cmd_line) {

	if(cmd_line == NULL)
		exit(-1);
	else if(!is_user_vaddr(cmd_line))
		exit(-1);
	else if (pml4_get_page(thread_current()->pml4, cmd_line) == NULL)
		exit(-1);

	char *copy = palloc_get_page(PAL_ZERO);
	strlcpy(copy, cmd_line, strlen(cmd_line) + 1);
	return process_exec(copy);
}

int wait(pid_t pid) {
	return process_wait(pid);
}

bool create(const char *file, unsigned initial_size) {

	if(file == NULL)
		exit(-1);
	else if(!is_user_vaddr(file))
		exit(-1);
	else if (pml4_get_page(thread_current()->pml4, file) == NULL)
		exit(-1);

	return filesys_create(file, initial_size);
}

bool remove(const char *file) {

	if(file == NULL)
		exit(-1);
	else if(!is_user_vaddr(file))
		exit(-1);
	else if (pml4_get_page(thread_current()->pml4, file) == NULL)
		exit(-1);

	return filesys_remove(file);
}

int open(const char *file) {

	if(file == NULL)
		exit(-1);
	else if(!is_user_vaddr(file))
		exit(-1);
	else if (pml4_get_page(thread_current()->pml4, file) == NULL)
		exit(-1);

	struct file *open_file = filesys_open (file);
	if (open_file == NULL) {
		return -1;
	}
	
	struct thread *curr = thread_current();
	struct file **curr_fdt = curr->fd_table;
	int fd = curr->last_fd + 1;
	for(; curr_fdt[fd] != 0; fd++);
	curr_fdt[fd] = open_file;
	if(fd > curr->last_fd)
		curr->last_fd = fd;
	return fd;
}

int filesize(int fd) {
	struct thread *curr = thread_current();

	if(fd < 3 || curr->last_fd < fd || curr->fd_table[fd] == NULL)
		exit(-1);
		
	struct file *f = curr->fd_table[fd];

	return file_length(f);
}

int read(int fd, void *buffer, unsigned size) {
	struct thread *curr = thread_current();
	int ret;

	if(buffer == NULL)
		exit(-1);
	else if(!is_user_vaddr(buffer))
		exit(-1);
	else if (pml4_get_page(thread_current()->pml4, buffer) == NULL)
		exit(-1);

	if(fd == STDIN_FILENO) {
		char* ptr = buffer;
		for(int i = 0; i < size; i++)
			*ptr++ = input_getc();
		
		ret = size;
	}
	else {
		if(fd < 3 || curr->last_fd < fd || curr->fd_table[fd] == NULL)
			exit(-1);
		
		ret = file_read(curr->fd_table[fd], buffer, size);
	}

	return ret;
}

int write(int fd, const void *buffer, unsigned size) {
	struct thread *curr = thread_current();
	int ret;
	
	if(buffer == NULL)
		exit(-1);
	else if(!is_user_vaddr(buffer))
		exit(-1);
	else if (pml4_get_page(thread_current()->pml4, buffer) == NULL)
		exit(-1);

	if(fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		ret = size;
	}
	else {

		if(fd < 3 || curr->last_fd < fd || curr->fd_table[fd] == NULL)
			exit(-1);

		ret = file_write(curr->fd_table[fd], buffer, size);
	}

	return ret;
}

void seek(int fd, unsigned position) {
	file_seek(thread_current()->fd_table[fd], position);
}

unsigned tell(int fd) {
	return file_tell(thread_current()->fd_table[fd]);
}

void close(int fd) {
	struct thread *curr = thread_current();

	if(fd < 3 || curr->last_fd < fd || curr->fd_table[fd] == NULL)
		exit(-1);

	file_close(curr->fd_table[fd]);
	curr->fd_table[fd] = NULL;
}