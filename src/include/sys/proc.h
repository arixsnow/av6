/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_PROC_H_
#define _AV6_PROC_H_

#include "sys/types.h"
#include "sys/param.h"
#include "sys/spinlock.h"
#include "arch/cpu.h"
#include "sys/list.h"
#include "arch/trapframe.h"

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
    struct spinlock lock;

    /* p->lock must be held when using these */
    enum procstate state;
    void *chan;             /* if non-zero, sleeping on chan */
    int killed;
    int xstate;             /* exit status for parent's wait */
    int pid;

    /* wait_lock must be held when using this: */
    struct proc *parent;

    /* private to the process (no lock needed) */
    uint64 kstack;          /* kernel stack address */
    uintptr onfault;        /* copyin/out fixup PC during a user access, 0 = none */
    struct trapframe *tf;   /* trapframe on kernel stack (EL0 traps) */
    pte_t *pagetable;       /* user page table (TTBR0), NULL for kthreads */
    void *isrc;             /* ithread: interrupt source served, else NULL */
    uint64 sz;              /* bytes of user VA mapped (from USER_BASE) */
    uint64 asid_ctx;        /* packed (generation | asid), 0 = unassigned */
    struct context context; /* swtch() here to run process */
    char name[16];

    int kslot;                      /* kstack-window slot id */
    LIST_ENTRY(proc) all_link;      /* allproc list (ptable_lock) */
    LIST_ENTRY(proc) pid_link;      /* pid -> proc hash (ptable_lock) */
    TAILQ_ENTRY(proc) runq_link;    /* scheduler run queue (runq_lock) */
};

extern struct proc *initproc;

void procinit(void);
struct proc *myproc(void);
void yield(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
void scheduler(void);
void swtch(struct context *old, struct context *new);
void forkret(void);
void usertrapret(void);
int kthread_add(void (*fn)(void), void *isrc, const char *name);
void userinit(void);
int fork(void);
void exit(int status);
int wait(uintptr xstatus);
int kill(int pid);
int killed(struct proc *p);
int growproc(int n);

#endif      /* _AV6_PROC_H_ */
