/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/arm64.h"
#include "arch/cpu.h"
#include "arch/dtb.h"
#include "arch/gic.h"
#include "arch/stdarg.h"
#include "dev/console.h"
#include "sys/fmt.h"
#include "sys/kio.h"
#include "sys/kmsg.h"
#include "sys/proc.h"
#include "sys/types.h"

#define PANIC_CPU_NONE      (~0UL)

/* One second for the panic SGI to be taken, and a spin count if CNTFRQ is unset. */
#define PANIC_STOP_SPINS    100000000UL

volatile int panicking;
static volatile uint64 panic_cpu = PANIC_CPU_NONE;

static char panic_msg[KMSG_RECORD_MAX];

/* Park this CPU. panic_stop_others() waits on the halted store. */
void panic_stop_self(void)
{
    struct cpu *c;

    irq_disable();

    c = &cpus[cpuid()];

    dsb_ishst();
    c->halted = 1;

    while (true) {
        wfi();
    }
}

int panic_in_progress(void)
{
    return panic_cpu != PANIC_CPU_NONE;
}

/*
 * Park the other CPUs before the log is touched. Returns how many never
 * answered. Those are still running and their output will interleave.
 */
static int panic_stop_others(void)
{
    uint64 deadline, freq, spins;
    int i, self, stuck;

    self = cpuid();
    freq = read_cntfrq();
    spins = PANIC_STOP_SPINS;

    dsb_ishst();
    gic_send_sgi_all(IRQ_PANIC);

    deadline = (freq != 0) ? read_cntpct() + freq : 0;

    while (true) {
        stuck = 0;
        for (i = 0; i < ncpus; i++) {
            if (i != self && cpus[i].online != 0 && cpus[i].halted == 0) {
                stuck++;
            }
        }

        if (stuck == 0) {
            break;
        }

        /* Re-arm */
        gic_send_sgi_all(IRQ_PANIC);

        if (freq != 0) {
            if (read_cntpct() >= deadline) {
                break;
            }
        } else if (--spins == 0) {
            break;
        }
    }

    dmb();      /* pairs with the dsb_ishst() in panic_stop_self() */

    return stuck;
}

static void panic_report_stuck(void)
{
    struct proc *p;
    int i, self;

    self = cpuid();

    for (i = 0; i < ncpus; i++) {
        if (i == self || cpus[i].online == 0 || cpus[i].halted != 0) {
            continue;
        }

        p = cpus[i].proc;
        printk_level(KERN_EMERG,
            "PANIC: cpu%d still running %s (state=%d noff=%d intena=%d lr=%p off=%s:%d)\n",
            i, (p != NULL) ? p->name : "scheduler",
            (p != NULL) ? (int)p->state : -1,
            cpus[i].noff, cpus[i].intena,
            (p != NULL) ? p->context.x30 : 0,
            (cpus[i].off_file != NULL) ? cpus[i].off_file : "?",
            cpus[i].off_line);
    }
}

void panic(const char *format, ...)
{
    va_list arglist;
    uint64 owner;
    int self, stuck, len;

    irq_disable();
    self = cpuid();

    owner = atomic64_cmpxchg_relaxed(&panic_cpu, PANIC_CPU_NONE, (uint64)self);
    if (owner != PANIC_CPU_NONE && owner != (uint64)self) {
        panic_stop_self();
    }

    /* Nested panic : falls through */
    stuck = (owner == PANIC_CPU_NONE) ? panic_stop_others() : 0;

    /* Locking is off from here: whatever the parked CPUs held stays held. */
    panicking = 1;

    kmsg_finalize();
    console_flush();

    if (stuck != 0) {
        printk_level(KERN_EMERG, "PANIC: %d CPUs did not stop\n", stuck);
        panic_report_stuck();
    }

    va_start(arglist, format);
    len = vsnprintf(panic_msg, sizeof(panic_msg), format, arglist);
    va_end(arglist);

    if (len >= (int)sizeof(panic_msg)) {
        len = (int)sizeof(panic_msg) - 1;
    }

    if (len > 0 && panic_msg[len - 1] == '\n') {
        panic_msg[len - 1] = '\0';
    }

    printk_level(KERN_EMERG, "PANIC (cpu%d): %s\n", self, panic_msg);

    panic_stop_self();
}
