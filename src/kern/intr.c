/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * intr.c - interrupt source dispatch
 *
 * A small fixed table maps GIC INTID -> interrupt source.
 *
 * The table is populated once by the BSP (intr_register, before secondaries
 * start), then read-only on the dispatch path, so dispatch needs no lock.
 */

#include "arch/gic.h"
#include "sys/intr.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/proc.h"
#include "sys/spinlock.h"
#include "sys/string.h"
#include "sys/types.h"
#include "vm/kmalloc.h"

struct intr_src {
    const char *name;
    int (*filter)(void);        /* hardirq context */
    void (*handler)(void);      /* ithread context */
    uint64 count;               /* interrupts dispatched */
    uint64 stray;               /* dispatched but unhandled */
    uint32 irq;                 /* INTID, so the ithread can deactivates */
    volatile int pending;       /* hardirq -> ithread work handoff */
};

static struct intr_src **intr_srcs;
static uint32 intr_nirq;

static struct spinlock ithread_lock;

/*
 * ithread_loop - interrupt thread.
 *
 * One ithread per handler-bearing source. It sleeps until the hardirq
 * flags pending, runs the device handler in thread context (where it may
 * take locks and, in principle, block), then issues gic_deactivate() to
 * re-arm the source. Deactivating only after the handler returns means a
 * device that re-asserts while we work is held off until we are ready.
 */
static void ithread_loop(void)
{
    struct intr_src *src = myproc()->isrc;

    while (true) {
        acquire_spinlock(&ithread_lock);
        while (src->pending == 0) {
            sleep(src, &ithread_lock);
        }
        src->pending = 0;
        release_spinlock(&ithread_lock);

        src->handler();
        gic_deactivate(src->irq);
    }
}

void intr_init(void)
{
    uint64 bytes;

    init_spinlock(&ithread_lock, "ithread");

    /* The controller decides how many INTIDs exist, so it runs first. */
    intr_nirq = gic_nirqs();
    bytes = (uint64)intr_nirq * sizeof(struct intr_src *);

    intr_srcs = kmalloc(bytes);
    if (intr_srcs == NULL) {
        panic("intr_init: no room for %u sources (%lu bytes)", intr_nirq, bytes);
    }

    memset(intr_srcs, 0, bytes);

    printk("intr: %u sources\n", intr_nirq);
}

void intr_register(uint32 irq, const char *name, int (*filter)(void),
                    void (*handler)(void))
{
    int pid;
    struct intr_src *src;

    KASSERT(irq < intr_nirq, "intr_register: irq %d out of range (%u)", irq, intr_nirq);
    KASSERT(intr_srcs[irq] == NULL, "intr_register: irq %d already registered", irq);

    src = kmalloc(sizeof(*src));
    if (src == NULL) {
        panic("intr_register: no memory for irq %u", irq);
    }

    memset(src, 0, sizeof(*src));
    src->name = name;
    src->filter = filter;
    src->handler = handler;
    src->irq = irq;

    /* Published last: a dispatch must never see a half-built source. */
    intr_srcs[irq] = src;

    if (handler != NULL) {
        pid = kthread_add(ithread_loop, src, name);

        if (pid < 0) {
            panic("intr_register: ithread create failed");
        }

        printk("intr: %s ithread pid %d (irq %d)\n", name, pid, irq);
    }
}

int intr_dispatch(uint32 irq)
{
    struct intr_src *src;
    int res;

    if (irq >= intr_nirq || intr_srcs[irq] == NULL) {
        printk("intr: unexpected IRQ %d\n", irq);
        return FILTER_STRAY;
    }

    src = intr_srcs[irq];

    src->count++;
    res = src->filter();

    if (res == FILTER_SCHEDULE_THREAD) {
        acquire_spinlock(&ithread_lock);
        src->pending = 1;
        release_spinlock(&ithread_lock);
        wakeup(src);
    } else if (res == FILTER_STRAY) {
        src->stray++;
    }

    return res;
}
