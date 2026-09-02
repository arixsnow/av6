/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_CPU_H_
#define _AV6_CPU_H_

#include "sys/param.h"
#include "sys/types.h"

struct proc;

/*
 * Saved registers for kernel context switches.
 *
 * swtch() saves the current callee-save registers here,
 * then loads from a different context to resume another thread.
 *
 * AArch64 AAPCS64 callee-saved registers:
 *          x19-x28         general purpose
 *          x29             frame pointer (fp)
 *          x30             link register (lr = return address)
 *          sp              stack pointer
 *
 * Layout must match the offsets in swtch.s exactly
 */
struct context {
    uint64 x19;
    uint64 x20;
    uint64 x21;
    uint64 x22;
    uint64 x23;
    uint64 x24;
    uint64 x25;
    uint64 x26;
    uint64 x27;
    uint64 x28;
    uint64 x29;     /* frame pointer */
    uint64 x30;     /* link register (return address) */
    uint64 sp;
};

/*
 * Per-CPU state
 *
 * Each core has its own struct cpu. Accessed via mycpu()
 * which uses cpuid() (reads tpidr_el1) as the index.
 *
 * noff/intena track interrupt disable nesting:
 *      push_off() - disables IRQs, increments noff
 *      pop_off() - decrements noff, re-enables if noff reaches 0
 *
 * This allows safe nesting:
 *      push_off;       noff=1, IRQs off
 *          push_off;   noff=2, IRQs still off
 *          pop_off;    noff=1, IRQs still off
 *      pop_off;        noff=0, IRQs restored
 */
struct cpu {
    struct proc *proc;          /* currently running process, or NULL */
    struct context context;     /* swtch() here to enter scheduler() */
    int noff;                   /* depth of push_off nesting */
    int intena;                 /* were IRQs enabled before first push_off? */
    const char *off_file;       /* caller of the outermost push_off, for panic */
    int off_line;
    int need_resched;           /* clock asked to preempt, consumed at trap return */
    uint64 asid_active;         /* atomic ctx in TTBR0. 0 when drained */
    volatile int online;        /* set by this CPU once bringup is done (SMP handshake) */
    volatile int halted;        /* parked by panic, and the panicking CPU waits on it */
    uint64 asid_reserved;       /* plain ctx carried across last rollover */
    int in_kmsg;                /* inside kmsg_add: re-entry takes the bypass */
};

extern struct cpu cpus[NCPU_MAX];

#endif      /* _AV6_CPU_H_ */
