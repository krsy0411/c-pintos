#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);
// 부모자식프로세스 관리
void process_register_child (struct thread *parent, tid_t child_tid);
void process_on_exit (int status);

#endif /* userprog/process.h */
