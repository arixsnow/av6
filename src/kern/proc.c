/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * proc.c - Process management and scheduling
 *
 * Procs come from a slab cache (proc_cache). Three tables track them:
 *      allproc : list of every live proc       (ptable_lock)
 *      pidhash : pid -> proc, for kill()       (ptable_lock)
 *      runq : FIFO of RUNNABLE procs           (runq_lock)
 * A recycling pid bitmap and the kstack-slot bitmap (pmap.c) supply the two
 * per-proc ids. Both are bounded by KSTACK_NSLOTS, the RAM-window ceiling.
 *
 * Lock order (outermost -> innermost):
 *      wait_lock -> ptable_lock -> p->lock -> runq_lock
 * pid_lock and kstack_lock are independent leaves.
 */

#include "arch/arm64.h"
#include "arch/asid.h"
#include "arch/cpu.h"
#include "arch/copy.h"
#include "arch/memlayout.h"
#include "arch/mmu.h"
#include "arch/pmap.h"
#include "sys/bitstring.h"
#include "sys/exec.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/list.h"
#include "sys/proc.h"
#include "sys/spinlock.h"
#include "sys/string.h"
#include "sys/types.h"
#include "vm/kalloc.h"
#include "vm/kmem.h"

#define PIDHASH_BUCKETS         64      /* power of two */

LIST_HEAD(proc_list, proc);
TAILQ_HEAD(proc_runq, proc);

static struct kmem_cache *proc_cache;

static struct proc_list allproc;                    /* every live proc */
static struct proc_list pidhash[PIDHASH_BUCKETS];   /* pid -> proc */
static struct proc_runq runq;                       /* RUNNABLE procs, FIFO */

static struct spinlock ptable_lock;     /* protects alloc + pidhash */
static struct spinlock runq_lock;       /* protects runq */
static struct spinlock pid_lock;        /* protects pidmap */
struct spinlock wait_lock;              /* protects proc->parent */

static bit_decl(pidmap, KSTACK_NSLOTS);     /* 1 = pid in use, pid 0 is reserved */

struct proc *initproc;

extern uchar init_elf_start[];
extern uchar init_elf_end[];

/* recycling pid allocator */

static int pid_alloc(void)
{
    int pid;

    acquire_spinlock(&pid_lock);
    bit_ffc(pidmap, (int)KSTACK_NSLOTS, &pid);
    if (pid >= 0) {
        bit_set(pidmap, pid);
    }
    release_spinlock(&pid_lock);

    return pid;
}

static void pid_free(int pid)
{
    acquire_spinlock(&pid_lock);
    bit_clear(pidmap, pid);
    release_spinlock(&pid_lock);
}

/* run queue */

static void runq_enqueue(struct proc *p)
{
    acquire_spinlock(&runq_lock);
    TAILQ_INSERT_TAIL(&runq, p, runq_link);
    release_spinlock(&runq_lock);
}

static struct proc *runq_dequeue(void)
{
    struct proc *p;

    acquire_spinlock(&runq_lock);
    p = TAILQ_FIRST(&runq);
    if (p != NULL) {
        TAILQ_REMOVE(&runq, p, runq_link);
    }
    release_spinlock(&runq_lock);

    return p;
}

/* pid -> proc lookup. Caller holds ptable_lock */
static struct proc *pid_lookup(int pid)
{
    struct proc *p;

    LIST_FOREACH(p, &pidhash[pid & (PIDHASH_BUCKETS - 1)], pid_link) {
        if (p->pid == pid) {
            return p;
        }
    }

    return NULL;
}

/* proc_cache ctor: runs once per object. init p->lock (survives free/realloc) */
static void proc_ctor(void *obj)
{
    struct proc *p = (struct proc *)obj;

    init_spinlock(&p->lock, "proc");
}

void procinit(void)
{
    int i;

    init_spinlock(&pid_lock, "pid");
    init_spinlock(&wait_lock, "wait_lock");
    init_spinlock(&ptable_lock, "ptable");
    init_spinlock(&runq_lock, "runq");

    LIST_INIT(&allproc);
    TAILQ_INIT(&runq);

    for (i = 0; i < PIDHASH_BUCKETS; i++) {
        LIST_INIT(&pidhash[i]);
    }

    bit_set(pidmap, 0);         /* reserve pid 0 */

    proc_cache = kmem_cache_create("proc", sizeof(struct proc), 0, proc_ctor);
    if (proc_cache == NULL) {
        panic("procinit: proc_cache create failed");
    }
}

struct proc *myproc(void)
{
    struct proc *p;

    push_off();
    p = cpus[cpuid()].proc;
    pop_off();

    return p;
}

/*
 * allocproc : build a fresh proc, PRIVATE and UNLOCKED (not any list yet).
 *
 * Grabs a pid, a kstack slot + its mapped stack, and a proc object from the
 * slab. p->lock was initialized once by the ctor and survives free/realloc, so
 * it is NOT re-initialized. Every other field is stale slab memory and is set
 * explicitly. Returns NULL (releasing whatever it took) on exhaustion. The
 * caller does slow setup on the private proc, then proc_activate() publishes it.
 */
static struct proc *allocproc(void)
{
    struct proc *p;
    int pid, slot;
    uint64 kstack;

    pid = pid_alloc();
    if (pid < 0) {
        return NULL;
    }

    slot = kstack_slot_alloc();
    if (slot < 0) {
        pid_free(pid);
        return NULL;
    }

    kstack = kstack_alloc(slot);
    if (kstack == 0) {
        kstack_slot_free(slot);
        pid_free(pid);
        return NULL;
    }

    p = kmem_cache_alloc(proc_cache);
    if (p == NULL) {
        kstack_free(slot);
        kstack_slot_free(slot);
        pid_free(pid);
        return NULL;
    }

    p->pid = pid;
    p->kslot = slot;
    p->kstack = kstack;
    p->state = USED;
    p->chan = NULL;
    p->killed = 0;
    p->xstate = 0;
    p->parent = NULL;
    p->onfault = (uintptr)NULL;
    p->pagetable = NULL;
    p->isrc = NULL;
    p->sz = 0;
    p->asid_ctx = 0;
    p->name[0] = '\0';

    p->tf = (struct trapframe *)(p->kstack - TF_SIZE);
    memset(&p->context, 0, sizeof(p->context));
    p->context.x30 = (uint64)forkret;
    p->context.sp = (uint64)p->tf;

    return p;
}

/*
 * freeproc : release a proc that is already UNLINKED from allproc/pidhash and
 * UNLOCKED. Frees its pagetable, kstack + slot, pid, and the object itself.
 * Do not touch the proc after this returns.
 */
static void freeproc(struct proc *p)
{
    if (p->pagetable) {
        uvmfree(p->pagetable);
        p->pagetable = NULL;
    }

    if (p->kstack) {
        kstack_free(p->kslot);
        kstack_slot_free(p->kslot);
        p->kstack = 0;
    }

    pid_free(p->pid);
    kmem_cache_free(proc_cache, p);
}

/*
 * proc_activate : publish a freshly built proc, link it into allproc + the
 * pid hash, set it RUNNABLE, and enqueue it. If parent != NULL (fork) the caller
 * must already hold wait_lock. The parent link is set here.
 * Order: [wait_lock] -> ptable_lock -> p-> lock -> runq_lock
 */
static void proc_activate(struct proc *p, struct proc *parent)
{
    acquire_spinlock(&ptable_lock);
    acquire_spinlock(&p->lock);

    LIST_INSERT_HEAD(&allproc, p, all_link);
    LIST_INSERT_HEAD(&pidhash[p->pid & (PIDHASH_BUCKETS - 1)], p, pid_link);

    if (parent != NULL) {
        p->parent = parent;
    }
    p->state = RUNNABLE;
    runq_enqueue(p);

    release_spinlock(&p->lock);
    release_spinlock(&ptable_lock);
}

void forkret(void)
{
    struct proc *p = myproc();

    /* still holding p->lock, handed off by the scheduler */
    release_spinlock(&p->lock);

    void (*fn)(void) = (void (*)(void))p->context.x19;
    if (fn) {
        fn();
        printk("kthread %s fall through\n", p->name);
        panic("kthread fall through");
    }

    usertrapret();
}

void userinit(void)
{
    struct proc *p;
    uintptr entry, sp;
    uint64 sz, elf_size, *w;
    char *stack;
    int pid;

    p = allocproc();
    if (p == NULL) {
        panic("userinit: allocproc failed");
    }

    p->pagetable = uvmcreate();
    if (p->pagetable == NULL) {
        panic("userinit: uvmcreate failed");
    }

    elf_size = (uint64)(init_elf_end - init_elf_start);
    if (loadelf(p->pagetable, init_elf_start, elf_size, &entry, &sz) < 0) {
        panic("userinit: loadelf failed");
    }

    stack = kpage_alloc();
    if (stack == NULL) {
        panic("userinit: kalloc failed");
    }
    memset(stack, 0, PGSIZE);

    if (mappages(p->pagetable, USER_BASE + sz, KVA_TO_PA((uintptr)stack),
        PGSIZE, PAGE_USER) < 0) {
        panic("userinit: stack mappages failed");
    }

    sp = (sz + PGSIZE - 5 * sizeof(uint64)) & ~0xfUL;
    w = (uint64 *)(stack + (sp - sz));
    w[0] = 0;       /* argc */
    w[1] = 0;       /* argv NULL */
    w[2] = 0;       /* envp NULL */
    w[3] = 0;       /* auxv AT_NULL type */
    w[4] = 0;       /* auxv value */

    p->sz = sz + PGSIZE;

    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->elr = entry;
    p->tf->spsr = 0x0;
    p->tf->sp = sp;

    strncpy(p->name, "init", sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';

    pid = p->pid;
    initproc = p;

    printk("userinit: pid %d created (entry=%p, sz=%lu)\n",
        pid, (void *)entry, p->sz);

    proc_activate(p, NULL);
}

void scheduler(void)
{
    struct proc *p;
    struct cpu *c = &cpus[cpuid()];

    c->proc = NULL;

    while (true) {
        if (panic_in_progress()) {
            panic_stop_self();
        }

        irq_enable();

        p = runq_dequeue();
        if (p == NULL) {
            wfi();
            continue;
        }

        KASSERT(irq_get(), "scheduler: dispatch with IRQs masked");

        acquire_spinlock(&p->lock);
        if (p->state != RUNNABLE) {
            /* only RUNNABLE procs are enqueued, be defensive */
            release_spinlock(&p->lock);
            continue;
        }

        p->state = RUNNING;
        c->proc = p;

        if (p->pagetable) {
            asid_switch(p);
        }

        swtch(&c->context, &p->context);

        /* proc switched back to us, still holding p->lock */
        c->proc = NULL;
        switchuvm_reserved();
        if (p->state == RUNNABLE) {
            runq_enqueue(p);
        }
        release_spinlock(&p->lock);
    }
}

static void sched(void)
{
    int intena;
    struct proc *p = myproc();
    struct cpu *c = &cpus[cpuid()];

    KASSERT(holding_spinlock(&p->lock), "sched: p->lock not held");
    KASSERT(c->noff == 1, "sched: %d locks held", c->noff);
    KASSERT(p->state != RUNNING, "sched: still RUNNING");
    KASSERT(!irq_get(), "sched: interrupts enabled");

    intena = c->intena;
    swtch(&p->context, &c->context);
    cpus[cpuid()].intena = intena;
}

void yield(void)
{
    struct proc *p = myproc();

    acquire_spinlock(&p->lock);
    p->state = RUNNABLE;
    sched();
    release_spinlock(&p->lock);
}

void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    acquire_spinlock(&p->lock);
    release_spinlock(lk);

    p->chan = chan;
    p->state = SLEEPING;

    sched();

    p->chan = 0;

    release_spinlock(&p->lock);
    acquire_spinlock(lk);
}

void wakeup(void *chan)
{
    struct proc *p;
    struct proc *self = myproc();

    acquire_spinlock(&ptable_lock);
    LIST_FOREACH(p, &allproc, all_link) {
        if (p != self) {
            acquire_spinlock(&p->lock);
            if (p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
                runq_enqueue(p);
            }
            release_spinlock(&p->lock);
        }
    }
    release_spinlock(&ptable_lock);
}

void exit(int status)
{
    struct proc *p = myproc();
    struct proc *child;

    if (p == initproc) {
        panic("init exiting");
    }

    if (p->pagetable) {
        uvmfree(p->pagetable);
        p->pagetable = NULL;
        p->sz = 0;
    }

    acquire_spinlock(&wait_lock);

    /* reparent this proc's children to init */
    acquire_spinlock(&ptable_lock);
    LIST_FOREACH(child, &allproc, all_link) {
        if (child->parent == p) {
            child->parent = initproc;
        }
    }

    release_spinlock(&ptable_lock);
    wakeup(initproc);
    if (p->parent != NULL && p->parent != initproc) {
        wakeup(p->parent);
    }

    acquire_spinlock(&p->lock);
    p->xstate = status;
    p->state = ZOMBIE;

    release_spinlock(&wait_lock);

    printk("pid %d (%s) exited with status %d\n", p->pid, p->name, status);

    sched();
    KASSERT_UNREACHABLE();
}

int wait(uintptr xstatus)
{
    struct proc *np;
    struct proc *p = myproc();
    int havekids, pid;

    acquire_spinlock(&wait_lock);

    while (true) {
        havekids = 0;

        acquire_spinlock(&ptable_lock);
        LIST_FOREACH(np, &allproc, all_link) {
            if (np->parent != p) {
                continue;
            }

            acquire_spinlock(&np->lock);
            havekids = 1;

            if (np->state == ZOMBIE) {
                pid = np->pid;

                if (xstatus != 0
                    && copyout(xstatus, &np->xstate, sizeof(np->xstate)) < 0) {
                    release_spinlock(&np->lock);
                    release_spinlock(&ptable_lock);
                    release_spinlock(&wait_lock);
                    return -1;
                }

                /* unlink from the tables under ptable_lock, then free last */
                LIST_REMOVE(np, all_link);
                LIST_REMOVE(np, pid_link);
                release_spinlock(&np->lock);
                release_spinlock(&ptable_lock);

                freeproc(np);
                release_spinlock(&wait_lock);
                return pid;
            }
            release_spinlock(&np->lock);
        }
        release_spinlock(&ptable_lock);

        if (!havekids || killed(p)) {
            release_spinlock(&wait_lock);
            return -1;
        }
        sleep(p, &wait_lock);
    }
}

int killed(struct proc *p)
{
    int k;

    acquire_spinlock(&p->lock);
    k = p->killed;
    release_spinlock(&p->lock);

    return k;
}

int kill(int pid)
{
    struct proc *p;

    acquire_spinlock(&ptable_lock);
    p = pid_lookup(pid);
    if (p == NULL) {
        release_spinlock(&ptable_lock);
        return -1;
    }

    acquire_spinlock(&p->lock);
    p->killed = 1;
    if (p->state == SLEEPING) {
        p->state = RUNNABLE;
        runq_enqueue(p);
    }
    release_spinlock(&p->lock);
    release_spinlock(&ptable_lock);

    return 0;
}

int kthread_add(void (*fn)(void), void *isrc, const char *name)
{
    struct proc *p;
    int pid;

    p = allocproc();
    if (p == NULL) {
        return -1;
    }

    p->context.x19 = (uint64)fn;
    p->isrc = isrc;

    strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';

    pid = p->pid;
    proc_activate(p, NULL);

    return pid;
}

int fork(void)
{
    struct proc *p = myproc();
    struct proc *np;
    int pid;

    np = allocproc();
    if (np == NULL) {
        return -1;
    }

    np->pagetable = uvmcreate();
    if (np->pagetable == NULL) {
        freeproc(np);
        return -1;
    }

    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        return -1;
    }
    np->sz = p->sz;

    *np->tf = *p->tf;
    np->tf->x[0] = 0;

    strncpy(np->name, p->name, sizeof(np->name) - 1);
    np->name[sizeof(np->name) - 1] = '\0';

    pid = np->pid;

    /* publish: parent link under wait_lock, then allproc + pidhash + RUNNABLE + enqueue */
    acquire_spinlock(&wait_lock);
    proc_activate(np, p);
    release_spinlock(&wait_lock);

    return pid;
}

int growproc(int n)
{
    struct proc *p = myproc();
    uint64 sz = p->sz;

    if (n > 0) {
        sz = uvmalloc(p->pagetable, sz, sz + n);
        if (sz == 0) {
            asid_flush(p);
            return -1;
        }
    } else if (n < 0) {
        if ((uint64)(-n) > sz) {
            return -1;
        }
        sz = uvmdealloc(p->pagetable, sz, sz + n);
        asid_flush(p);
    }

    p->sz = sz;
    return 0;
}
