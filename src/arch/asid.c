/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * asid.c - Rolling generation ASID allocator
 *          : lockless fast path and check_update_reserved_asid
 *
 * 1. LOCKLESS FAST PATH: The common case asid_switch (generation
 *    still current) reads p->asid_ctx, the per-CPU active slot, and
 *    the global generation atomically, then cmpxchgs the active slot
 *    to publish the in-use ASID. asid_lock is not touched.
 *
 * 2. check_update_reserved_asid. At rollover, each CPU's active ASID
 *    is carried into the new generation via cpus[c].asid_reserved.
 *    When the process that owned that ASID next runs new_context,
 *    its old number is preserved. TLB locality survives rollovers.
 *
 * Memory ordering: the fast path uses _relaxed atomics. Correctness
 * rests on cmpxchg linearizability on the per-CPU active slot plus a
 * slow-path re-check under asid_lock.
 *
 * Lock order: p->lock (scheduler) -> asid_lock. asid.c never takes
 * p->lock.
 *
 * exit() does NOT free a process's ASID. Dead ASIDs stay reserved in
 * the bitmap until the next rollover. They are not reused, so stale
 * TLB entries are harmless and cleared by the deferred local flush.
 */

#include "arch/arm64.h"
#include "arch/asid.h"
#include "sys/bitstring.h"
#include "arch/cpu.h"
#include "sys/cpumask.h"
#include "arch/dtb.h"
#include "sys/kio.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/spinlock.h"
#include "sys/string.h"
#include "sys/types.h"
#include "arch/pmap.h"
#include "sys/kassert.h"

#define ASID_MAX_BITS       16
#define ASID_MAX            (1u << ASID_MAX_BITS)

static struct spinlock asid_lock;
static uint64 asid_num_total;           /* 256 or 65536 */
static uint64 asid_mask;                /* asid_num_total - 1 */
static uint64 asid_generation;          /* counts in units of asid_num_total */
static uint64 asid_next;                /* round-robin hint */
static bit_decl(asid_bitmap, ASID_MAX); /* 1024 uint64 words */
static struct cpumask tlb_pending;      /* set of CPUs owing a local TLB flush */

/* Helpers (all called under asid_lock) */

/*
 * bitmap_find_zero : first zero bit in [start, total), wrapping to
 * [1, start) if needed. Returns asid_num_total if the bitmap is
 * full (i.e. rollover is required).
 */
static uint64 bitmap_find_zero(uint64 start)
{
    uint64 i, b;

    for (i = start; i < asid_num_total; i++) {
        if (!bit_test(asid_bitmap, i)) {
            return i;
        }
    }

    for (b = 1; b < start; b++) {
        if (!bit_test(asid_bitmap, b)) {
            return b;
        }
    }

    return asid_num_total;
}

/*
 * check_update_reserved_asid : if p's old ASID was carried across the
 * most recent rollover, some CPU's asid_reserved slot still holds the
 * old packed ctx. On hit, update the reservation to the new ctx and
 * return non-zero so new_context can reuse the number without disturbing
 * the bitmap.
 */
static int check_update_reserved_asid(uint64 asid, uint64 newasid)
{
    int c, hit = 0;

    for (c = 0; c < ncpus; c++) {
        if (cpus[c].asid_reserved == asid) {
            cpus[c].asid_reserved = newasid;
            hit = 1;
        }
    }

    return hit;
}

void asid_init(void)
{
    int i;

    init_spinlock(&asid_lock, "asid");

    asid_num_total = 1UL << asid_bits();
    asid_mask = asid_num_total - 1;

    /*
     * Start generation at asid_num_total (not 0): a fresh p->asid_ctx
     * of 0 has generation 0, which is strictly less than the current
     * generation -> always stale -> goes through new_context on first
     * switch.
     */
    atomic64_set(&asid_generation, asid_num_total);
    asid_next = 1;
    cpumask_zero(&tlb_pending);

    memset(asid_bitmap, 0, sizeof(asid_bitmap));
    bit_set(asid_bitmap, ASID_RESERVED);

    /*
     * cpus[] lives in .bss so the per-CPU fields are already zero;
     * defensive reset in case that ever changes.
     */
    for (i = 0; i < NCPU_MAX; i++) {
        atomic64_set(&cpus[i].asid_active, 0);
        cpus[i].asid_reserved = 0;
    }

    printk("asid: %lu ASIDs (%d-bit)\n", asid_num_total, asid_bits());
}

uint16 asid_num(const struct proc *p)
{
    return (uint16)(atomic64_read(&p->asid_ctx) & asid_mask);
}

/*
 * flush_context : begin a new generation
 *
 * : Clears the bitmap, re-reserve ASID 0.
 * : For each CPU: xchg its asid_active to 0 (synchronization point
 *   with concurrent fast-path cmpxchgs). On idle (active was 0)
 *   fall back to the CPU's existing asid_reserved (the only trace
 *   left of its last address space). Carry the asid into asid_reserved
 *   for check_update_reserved_asid, and re-set its bitmap bit so it
 *   cannot be handed out this generation.
 * : Mark every CPU as owing a local TLB flush
 * : Bump the global generation.
 *
 * Called only from new_context, under asid_lock
 */
static void flush_context(void)
{
    int c;
    uint64 asid;

    memset(asid_bitmap, 0, sizeof(asid_bitmap));
    bit_set(asid_bitmap, ASID_RESERVED);

    for (c = 0; c < ncpus; c++) {
        asid = atomic64_xchg_relaxed(&cpus[c].asid_active, 0);
        if (asid == 0) {
            asid = cpus[c].asid_reserved;
        }
        cpus[c].asid_reserved = asid;
        if (asid != 0) {
            bit_set(asid_bitmap, asid & asid_mask);
        }
    }

    /*
     * Every cpu owes a local TLB flush before it reuses an ASID from
     * the new generation.
     */
    for (c = 0; c < ncpus; c++) {
        cpumask_set(&tlb_pending, c);
    }

    atomic64_set(&asid_generation, atomic64_read(&asid_generation) + asid_num_total);
    asid_next = 1;
}

/*
 * new_context : assign p a fresh asid in the current generation.
 *
 * Called only from asid_switch slow path, under asid_lock.
 */
static uint64 new_context(struct proc *p)
{
    uint64 old = atomic64_read(&p->asid_ctx);
    uint64 gen = atomic64_read(&asid_generation);
    uint64 asid, newasid;

    if (old != 0) {
        newasid = gen | (old & asid_mask);

        /*
         * Path 1: live process across rollover
         * Old asid was carried across the most recent rollover
         * (still in some CPU's reserved slot) : update that
         * reservation to the new generation and reuse the number.
         */
        if (check_update_reserved_asid(old, newasid)) {
            return newasid;
        }

        /*
         * Path 2: sleeping process
         * Otherwise try to keep the number if its bit is free
         */
        asid = old & asid_mask;
        if (!bit_test(asid_bitmap, asid)) {
            bit_set(asid_bitmap, asid);
            return newasid;
        }
    }

    asid = bitmap_find_zero(asid_next);
    if (asid == asid_num_total) {
        flush_context();
        gen = atomic64_read(&asid_generation);
        asid = bitmap_find_zero(1);

        KASSERT(asid != asid_num_total, "asid: pool exhausted after rollover");
    }

    bit_set(asid_bitmap, asid);
    asid_next = asid + 1;
    return gen | asid;
}

/*
 * asid_switch : load p's address space into TTBR0
 *
 * Fast path (lockless): if the per-CPU active slot is non-zero, the
 * process's generation matches the current generation, AND we can
 * cmpxchg the active slot to our ctx, and jump straight to switchuvm.
 *
 * Slow path: take asid_lock, re-check, call new_context if stale,
 * drain any deferred local TLB flush, publish the ctx into the per-CPU
 * active slot.
 *
 * Called from the scheduler with p->lock held and IRQs off.
 */
void asid_switch(struct proc *p)
{
    uint64 ctx, old_active, gen;
    int cpu;

    cpu = cpuid();
    ctx = atomic64_read(&p->asid_ctx);
    gen = atomic64_read(&asid_generation);
    old_active = atomic64_read(&cpus[cpu].asid_active);

    /*
     * Fast path: The cmpxchg synchronizes with flush_context()'s
     * xchg of the same slot. If a rollover races and zeros the slot
     * before our cmpxchg returns 0 (!= old_active)
     * and we fall to the slow path. 'old_active != 0' rejects the idle
     * case (CPU has never published an ASID since boot or since the last
     * rollover drained it). (ctx ^ gen) & ~asid_mask is zero iff the generation
     * bits match.
     */
    if (old_active != 0
        && ((ctx ^ gen) & ~asid_mask) == 0
        && atomic64_cmpxchg_relaxed(&cpus[cpu].asid_active,
            old_active, ctx) == old_active) {
        goto fast_path;
    }

    /* Slow path */
    acquire_spinlock(&asid_lock);

    ctx = atomic64_read(&p->asid_ctx);
    if ((ctx & ~asid_mask) != atomic64_read(&asid_generation)) {
        ctx = new_context(p);
        atomic64_set(&p->asid_ctx, ctx);
    }

    if (cpumask_test(&tlb_pending, cpu)) {
        cpumask_clear(&tlb_pending, cpu);

        /*
         * tlbi vmalle1 : LOCAL "all stage-1 EL1/EL0". Drops both
         * TTBR0 (ASID-tagged) and TTBR1 (global) entries on this
         * CPU. It invalidates only the TLB *cache*.
         */
        tlbi_vmalle1();
    }

    atomic64_set(&cpus[cpu].asid_active, ctx);

    release_spinlock(&asid_lock);

fast_path:
    switchuvm((uint16)(ctx & asid_mask), p->pagetable);
}

/*
 * asid_flush : invalidate p's ASID inner-sharable. Used by exec
 * (after the pagetable swap, before freeing the old one) and by
 * growproc shrink (after the unmap). Lockless : p is the running
 * proc, so p->asid_ctx is stable.
 */
void asid_flush(struct proc *p)
{
    uint64 asid = atomic64_read(&p->asid_ctx) & asid_mask;

    tlbi_aside1is(asid);
}
