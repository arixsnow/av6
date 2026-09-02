/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/arm64.h"
#include "arch/cpu.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/spinlock.h"
#include "sys/types.h"

void push_off_at(const char *file, int line)
{
    int old;
    struct cpu *c;

    /*
     * Read DAIF before disabling
     * DAIF bit 7 (I) = 1 means IRQs masked (disabled)
     */
    old = irq_get();
    irq_disable();
    c = &cpus[cpuid()];
    if (c->noff == 0) {
        c->intena = old;        /* save original IRQ state */
        c->off_file = file;
        c->off_line = line;
    }
    c->noff++;
}

void pop_off_at(const char *file __unused, int line __unused)
{
    struct cpu *c;

    KASSERT(!irq_get(), "pop_off: interruptable at %s:%d", file, line);

    c = &cpus[cpuid()];

    KASSERT(c->noff >= 1, "pop_off: underflow at %s:%d", file, line);

    c->noff--;

    if (c->noff == 0 && c->intena) {
        irq_enable();
    }
}

/*
 * must be called with interrupts disabled (inside push_off~pop_off)
 */
int holding_spinlock(struct spinlock *lk)
{
    return lk->locked && lk->cpu == &cpus[cpuid()];
}

void init_spinlock(struct spinlock *lk, const char *name)
{
    lk->locked = 0;
    lk->name = name;
    lk->cpu = NULL;
}

void acquire_spinlock_at(struct spinlock *lk, const char *file, int line)
{
    push_off_at(file, line);

    KASSERT(!holding_spinlock(lk) || panicking,
        "acquire_spinlock: %s already held held at %s:%d", lk->name, file, line);

    while (atomic_swap(&lk->locked, 1) != 0) {
        if (panicking) {
            return;
        }
    }

    lk->cpu = &cpus[cpuid()];
}

void release_spinlock_at(struct spinlock *lk, const char *file, int line)
{
    KASSERT(holding_spinlock(lk) || panicking,
        "release_spinlock: %s not held at %s:%d", lk->name, file, line);

    lk->cpu = 0;
    /*
     * stlr = Store with Release semantics
     * Ensures all writes protected by this lock are visible
     * to other cores BEFORE the lock appears free.
     */
    asm volatile("stlr %w0, [%1]" :: "r" (0), "r" (&lk->locked) : "memory");

    pop_off_at(file, line);
}

/* try_acquire_spinlock - non-blocking acquire */
int try_acquire_spinlock_at(struct spinlock *lk, const char *file, int line)
{
    push_off_at(file, line);

    KASSERT(!holding_spinlock(lk) || panicking,
        "try_acquire_spinlock: %s already held at %s:%d", lk->name, file, line);

    if (atomic_swap(&lk->locked, 1) != 0) {
        if (panicking) {
            return 1;
        }

        pop_off_at(file, line);
        return 0;
    }

    lk->cpu = &cpus[cpuid()];
    return 1;
}
