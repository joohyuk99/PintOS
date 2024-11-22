#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void argument_stack(char **argv, int argc, void **rsp);
struct thread *get_child_process(int pid);

int process_add_file(struct file *f);

/* Project 3 */
#ifdef VM
bool lazy_load_segment(struct page *page, void *aux);
#endif

// structure for management file-backed page
struct container {
    struct file *file;
    off_t offset;
    size_t page_read_bytes;
};

#endif /* userprog/process.h */
