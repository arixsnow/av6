/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_ARM64_H_
#define _AV6_ARM64_H_

#include "sys/types.h"
#include "sys/bitops.h"

/* MPIDR_EL1 affinity bits: Aff3[39:32] | Aff2[23:16] | Aff1[15:8] | Aff0[7:0] */
#define MPIDR_AFF_MASK          0xFF00FFFFFFUL

#define DAIF_I                  BIT(7)
#define CTR_IMINLINE_SHIFT      0
#define CTR_DMINLINE_SHIFT      16
#define CTR_MINLINE_MASK        GENMASK(3, 0)

/*
 * Interrupt Control:
 *
 * AArch64 has a register called DAIF that masks 4 types of exceptions:
 *      D = Debug, A = SError, I = IRQ, F = FIQ
 *
 * bit 1 (I = IRQ) controls IRQs
 *      daifset: SET bits (1 = masked = disabled)
 *      daifclr: CLEAR bits (0 = unmasked = enabled)
 */

static inline void irq_disable(void)
{
    asm volatile("msr daifset, #3" ::: "memory");
}

static inline void irq_enable(void)
{
    asm volatile("msr daifclr, #3" ::: "memory");
}

/*
 * Leave boot's all-masked posture. SError stays unmasked from here on, including
 * under a spinlock -- masking it discards errors silently. Debug stays masked:
 * nothing handles it yet.
 */
static inline void daif_procctx(void)
{
    asm volatile("msr daifclr, #4" ::: "memory");
}

/*
 * Atomic swap
 *
 * Atomically replaces *addr with newval, returns the oldval.
 *
 * Uses the Load-Exclusive / Store-Exclusive pattern:
 *
 *      ldaxr   w0, [addr]      Load Acquire eXclusive Register
 *                              Reads value, marks cache line as exclusive
 *      stlxr   w1, w2, [addr]  Store-reLease eXclusive Register
 *                              Tries to write, w1 = 0 on success,
 *                              non-zero if another core touched that line
 *      cbnz    w1, 1b          Retry on failure
 *
 * "a" suffix = acquire (no loads/stores reordered before this)
 * "l" suffix = release (no loads/stores reordered after this)
 * ARM has a weak memory model - these suffixes enforce ordering
 */

static inline uint atomic_swap(volatile uint *addr, uint newval)
{
    uint oldval, status;

    asm volatile(
        "1: ldaxr   %w0, [%2]       \n"
        "   stlxr   %w1, %w3, [%2]  \n"
        "   cbnz    %w1, 1b         \n"
        : "=&r" (oldval), "=&r" (status)
        : "r" (addr), "r" (newval)
        : "memory"
    );

    return oldval;
}

/*
 * 64-bit relaxed atomics
 *
 * Aligned 64-bit ldr/str are single-copy atomic on AArch64, so a plain
 * read/write through a volatile pointer is the atomic primitive for
 * atomic64_read / atomic64_set. For atomic read-modify-write we drop
 * into the Load-Exclusive / Store-Exclusive loop with No acquire/
 * release suffix, the "_relaxed" family. Add explicit barriers at
 * the caller if you need ordering across these.
 */

static inline uint64 atomic64_read(const volatile uint64 *p)
{
    return *p;
}

static inline void atomic64_set(volatile uint64 *p, uint64 v)
{
    *p = v;
}

static inline uint64 atomic64_xchg_relaxed(volatile uint64 *p, uint64 newval)
{
    uint64 oldval;
    uint status;

    asm volatile(
        "1: ldxr    %0, [%2]\n"
        "   stxr    %w1, %3, [%2]\n"
        "   cbnz    %w1, 1b\n"
        : "=&r" (oldval), "=&r" (status)
        : "r" (p), "r" (newval)
        : "memory"
    );

    return oldval;
}

static inline uint64 atomic64_cmpxchg_relaxed(volatile uint64 *p,
        uint64 expect, uint64 newval)
{
    uint64 oldval;
    uint status;

    asm volatile(
        "1: ldxr    %0, [%2]\n"
        "   cmp     %0, %3\n"
        "   b.ne    2f\n"
        "   stxr    %w1, %4, [%2]\n"
        "   cbnz    %w1, 1b\n"
        "2:\n"
        : "=&r" (oldval), "=&r" (status)
        : "r" (p), "r" (expect), "r" (newval)
        : "memory", "cc"
    );

    return oldval;
}

/*
 * Memory barriers
 *
 * ARM cores can reorder memory accesses for performance.
 * Barriers force ordering when correctness requires it.
 *
 * The translation-table walker is architecturally a separate observer: an
 * ordering store to a PTE stays invisible to it until a DSB completes. That
 * makes dsb_ishst() and the publication barrier stores only, inner-shareable
 * and it is the one to reach for after writing page-table entries. dsb() is
 * full-system, loads and stores. It belongs in early boot, not on hot path.
 */

static inline void dmb(void)        /* Data Memory Barrier: ordering only */
{
    asm volatile("dmb sy" ::: "memory");
}

static inline void dsb(void)        /* Data Synchronization Barrier: ordering + completion */
{
    asm volatile("dsb sy" ::: "memory");
}

/* Prior STORES complete, inner-sharable. Publishes PTEs */
static inline void dsb_ishst(void)
{
    asm volatile("dsb ishst" ::: "memory");
}

/* Prior loads AND stores complete, inner-sharable. */
static inline void dsb_ish(void)
{
    asm volatile("dsb ish" ::: "memory");
}

static inline void isb(void)        /* Instruction Synchronization Barrier: flush pipeline */
{
    asm volatile("isb" ::: "memory");
}

static inline void wfi(void)        /* Wait for Interrupt: halt this CPU until an IRQ arrives */
{
    asm volatile("wfi" ::: "memory");
}

/*
 * TLB maintenance. Each helper publishes first, issues the TLBI, waits with a
 * DSB of the correct shareability scope, then an ISB. Call sites name the
 * operation and never hand-roll (or drift on) the barrier scope.
 *
 * The LEADING dsb ishst is not optional. A TLBI cannot invalidate a table
 * change the walker has not see. It would drop the entry and refill it from
 * the same stale memory.
 *
 * tlbi_vmalle1() : local, drop ALL stage-1 EL1/EL0 entries on this CPU.
 * tlbi_aside1is() : broadcast, drop one ASID's entries across the inner-
 *                   sharable domain (every CPU).
 * tlbi_vaae1is_range() : broadcast, drop npages VAs for ALL ASIDs, a global
 *                        kernel mapping, which carries no ASID to name it by.
 */
static inline void tlbi_vmalle1(void)
{
    dsb_ishst();                                /* publish PTE writes first */
    asm volatile("tlbi vmalle1" ::: "memory");
    asm volatile("dsb nsh" ::: "memory");       /* local scope: non-sharable */
    isb();
}

static inline void tlbi_aside1is(uint64 asid)
{
    dsb_ishst();                                /* publish PTE writes first */
    asm volatile("tlbi aside1is, %0" :: "r"(asid << 48) : "memory");
    dsb_ish();                                  /* wait for the broadcast */
    isb();
}

static inline void tlbi_vaae1is_range(uint64 va, uint64 npages)
{
    uint64 page, i;

    /* The TLBI operand is not an address, it is VA[55:12] */
    page = va >> 12;

    dsb_ishst();
    for (i = 0; i < npages; i++) {
        asm volatile("tlbi vaae1is, %0" :: "r"(page + i) : "memory");
    }
    dsb_ish();                                  /* wait for the broadcast */
    isb();
}

static inline uint64 read_mpidr(void)
{
    uint64 val;
    asm volatile("mrs %0, mpidr_el1" : "=r"(val));
    return val & MPIDR_AFF_MASK;      /* affinity only, strips RES1/U/MT flags */
}

/* Generic timer : free-running physical counter and its frequency (Hz). */
static inline uint64 read_cntpct(void)
{
    uint64 val;
    asm volatile("isb; mrs %0, cntpct_el0" : "=r"(val) :: "memory");
    return val;
}

static inline uint64 read_cntfrq(void)
{
    uint64 val;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline int cpuid(void)
{
    uint64 val;
    asm volatile("mrs %0, tpidr_el1" : "=r"(val));
    return (int)val;
}

static inline void set_cpuid(uint64 id)
{
    asm volatile("msr tpidr_el1, %0" :: "r"(id));
}

static inline int irq_get(void)
{
    uint64 daif;

    asm volatile("mrs %0, daif" : "=r"(daif));
    return (daif & DAIF_I) == 0;      /* I clear = unmasked = enable */
}

/*
 * cacheline_size : smallest cache-line size (in bytes) that is safe
 * to stride by for dc/ic on this CPU.
 *
 * CTR_EL0 reports cache geometry:
 *      DminLine, bits [19:16] : log2 of the smallest D-cache line
 *      IminLine, bits [3:0] : log2 of the smallest I-cache line
 * Both are counts of 4-byte words, so line bytes = 4 << field.
 * Striding a maintenance loop by the smaller of the two guarantees
 * no cache line is ever skipped.
 */
static inline uint64 cacheline_size(void)
{
    uint64 ctr, dline, iline;

    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
    dline = 4UL << ((ctr >> CTR_DMINLINE_SHIFT) & CTR_MINLINE_MASK);
    iline = 4UL << ((ctr >> CTR_IMINLINE_SHIFT) & CTR_MINLINE_MASK);

    return (dline < iline) ? dline : iline;
}

#endif      /* _AV6_ARM64_H_ */
