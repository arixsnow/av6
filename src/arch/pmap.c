/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * pmap.c - Virtual Memory setup for AArch64
 *
 * Two parts:
 *      Kernel page tables (boot): static 2MB block mappings via TTBR1
 *      User page tables (per-process): dynamic 4KB page mappings via TTBR0
 *
 * Kernel mappings user L0->L1->L2 with 2MB blocks (no L3)
 * User mappings use L0->L1->L2->L3 with 4KB pages.
 *
 * ASID (Address Space Identifier) tags TTBR0 so the TLB
 * can hold entries from multiple processes without flushing
 */

#include "arch/dtb.h"
#include "arch/arm64.h"
#include "arch/asid.h"
#include "arch/memlayout.h"
#include "arch/mmu.h"
#include "arch/mte.h"
#include "arch/pan.h"
#include "arch/pmap.h"
#include "sys/bitstring.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/param.h"
#include "sys/spinlock.h"
#include "sys/string.h"
#include "sys/types.h"
#include "vm/kalloc.h"
#include "vm/physmem.h"

/*
 * Translation tables: statically allocated, must be page-aligned
 *
 * 48-bit VA, 4KB granule -> 4-level walk: L0 -> L1 -> L2 -> (L3)
 */

static pte_t l0_table[TABLE_ENTRIES] __attribute__((aligned(PGSIZE)));
static pte_t l0_reserved[TABLE_ENTRIES] __attribute__((aligned(PGSIZE)));
static pte_t l1_table[TABLE_ENTRIES] __attribute__((aligned(PGSIZE)));
static pte_t l2_device[TABLE_ENTRIES] __attribute__((aligned(PGSIZE)));
static pte_t l2_kstack[TABLE_ENTRIES] __attribute__((aligned(PGSIZE)));

static pte_t *ptalloc(void);

static struct spinlock kstack_lock;
static bit_decl(kstack_slotmap, KSTACK_NSLOTS);
static const int pt_shift[4] = {L0_SHIFT, L1_SHIFT, L2_SHIFT, L3_SHIFT};

extern char stext[], end[];

/*
 * boot_alloc - hand out npages of physical RAM above the kernel image.
 *
 * Returns a PA and does NOT zero: kvminit runs MMU-off and writes at the PA
 * directly, kinit runs MMU-on and writes via PA_TO_KVA. A shared zeroing step
 * would be correct in only one of those regimes.
 */
static uintptr boot_freemem;

uintptr boot_alloc(uint64 npages)
{
    uintptr pa = boot_freemem;

    boot_freemem += npages * PGSIZE;
    return pa;
}

/* One zeroed page-table pag, as a PA-pointer usable while the MMU is off */
static pte_t *boot_pgtable(void)
{
    pte_t *pt = (pte_t *)boot_alloc(1);

    memset(pt, 0x00, PGSIZE);
    return pt;
}

/* L2 table under l1_table[L1X(pa)], created on first use */
static pte_t *dmap_l2(uint64 pa)
{
    int i = L1X(pa);
    pte_t *l2;

    if (i == 0 || i == KSTACK_WINDOW_L1) {
        panic("kvminit: RAM bank overlaps a reserved L1 slot (device/kstack");
    }
    if (!(l1_table[i] & PTE_VALID)) {
        l2 = boot_pgtable();
        l1_table[i] = (pte_t)l2 | PTE_VALID | PTE_TABLE;
    }

    return (pte_t *)PTE_ADDR(l1_table[i]);
}

static void dmap_page(uint64 pa)
{
    pte_t *l2 = dmap_l2(pa), *l3;

    if (!(l2[L2X(pa)] & PTE_VALID)) {
        l3 = boot_pgtable();
        l2[L2X(pa)] = (pte_t)l3 | PTE_VALID | PTE_TABLE;
    }
    l3 = (pte_t *)PTE_ADDR(l2[L2X(pa)]);
    l3[L3X(pa)] = pa | PAGE_KERNEL;         /* non-exec: head/tail RAM is never text */
}

static void dmap_block_2m(uint64 pa)
{
    pte_t *l2 = dmap_l2(pa);

    l2[L2X(pa)] = pa | BLOCK_NORMAL;
}

static void dmap_block_1g(uint64 pa)
{
    int i = L1X(pa);

    if (i == 0 || i == KSTACK_WINDOW_L1) {
        panic("kvminit: RAM bank overlaps a reserved L1 slot (device/kstack)");
    }
    l1_table[i] = pa | BLOCK_NORMAL;
}

/*
 * Map one RAM bank into the DMAP, largest block first.
 * (4KB -> 2MB -> 1GB -> 2MB -> 4KB)
 *
 * On QEMu virt every bank is 1GB-aligned, so only the 1GB phase
 * runs. The head/tail phases exist for real hardware and are exercised
 * by the boot self-test.
 */
static void dmap_bank(uint64 start_pa, uint64 end_pa)
{
    uint64 pa = start_pa;

    if (start_pa < BLOCK_SIZE_1G) {
        panic("kvminit: RAM bank below 1 GB collides with the device block");
    }

    if (end_pa > ((uint64)KSTACK_WINDOW_L1 << L1_SHIFT)) {
        panic("kvminit: RAM bank reaches the kstack window (>511 GB)");
    }

    while (pa < end_pa && (pa & (BLOCK_SIZE_2M - 1))) {
        dmap_page(pa);
        pa += PGSIZE;
    }

    while ((pa + BLOCK_SIZE_2M) <= end_pa && (pa & (BLOCK_SIZE_1G - 1))) {
        dmap_block_2m(pa);
        pa += BLOCK_SIZE_2M;
    }

    while ((pa + BLOCK_SIZE_1G) <= end_pa) {
        dmap_block_1g(pa);
        pa += BLOCK_SIZE_1G;
    }

    while ((pa + BLOCK_SIZE_2M) <= end_pa) {
        dmap_block_2m(pa);
        pa += BLOCK_SIZE_2M;
    }

    while (pa < end_pa) {
        dmap_page(pa);
        pa += PGSIZE;
    }
}

/*
 * kvminit - build the kernel translation tables.
 *
 * l1_table[0] -> l2_device (1GB device MMIO)
 * l1_table[1..] -> RAM banks, mapped by dmap_bank()
 * l1_table[511] -> l2_kstack (the per-proc kstack window)
 *
 * Runs with the MMU off. Symbol and table addresses are physical here.
 * Must be called after dtb_init() (which fills the physmem banks) and
 * before kvminithart().
 */
void kvminit(void)
{
    int i;

    init_spinlock(&kstack_lock, "kstack");

    memset(l0_table, 0x00, PGSIZE);
    memset(l1_table, 0x00, PGSIZE);
    memset(l2_device, 0x00, PGSIZE);
    memset(l2_kstack, 0x00, PGSIZE);

    boot_freemem = PGROUNDUP((uintptr)end);

    l0_table[0] = (pte_t)l1_table | PTE_VALID | PTE_TABLE;

    l1_table[0] = (pte_t)l2_device | PTE_VALID | PTE_TABLE;
    for (i = 0; i < TABLE_ENTRIES; i++) {
        l2_device[i] = ((pte_t)i << L2_SHIFT) | BLOCK_DEVICE;
    }

    l1_table[KSTACK_WINDOW_L1] = (pte_t)l2_kstack | PTE_VALID | PTE_TABLE;

    /* No exclusions yet, so avail == every hardware bank */
    physmem_avail();
    for (i = 0; i < vm_phys_nsegs; i++) {
        dmap_bank(vm_phys_segs[i].start, vm_phys_segs[i].end);
    }

    /* Withhold the kernel image and the boot page tables from kalloc. */
    physmem_exclude_region((uintptr)stext, boot_freemem - (uintptr)stext);

    printk("kvminit: DMAP mapped %lu MB RAM in %d bank(s)\n",
        physmem_size() >> 20, vm_phys_nsegs);
}

static const uint16 parange_bits[] = {32, 36, 40, 42, 44, 48, 52, 56};

static uint64 pa_range(void)
{
    uint64 parange = SYS_FIELD(read_sysreg(id_aa64mmfr0_el1), ID_AA64MMFR0_PARANGE);

    /* av6 walks 48-bit tables; a wider PARange would change PTE bits [51:48] */
    return (parange > ID_AA64MMFR0_PARANGE_48) ? ID_AA64MMFR0_PARANGE_48 : parange;
}

static int has_hafdbs(void)
{
    return SYS_FIELD(read_sysreg(id_aa64mmfr1_el1), ID_AA64MMFR1_HAFDBS)
        != ID_AA64MMFR1_HAFDBS_NONE;
}

/*
 * kvminithart - program MMU registers and enable translation
 *
 * Four registers, in order:
 *
 * 1. MAIR_EL1 - what memory types exist (Device, Tagged Normal WB)
 * 2. TCR_EL1 - how to walk tables (VA size, granule, caching)
 * 3. TTBR0_EL1 - where the L1 table is (physical address)
 * 4. SCTLR_EL1 - flip the MMU ON switch (bit 0)
 */
void kvminithart(void)
{
    uint64 mair, tcr, sctlr;

    /* MAIR: define memory attribute slots */
    mair = MAIR_VALUE;
    asm volatile("msr mair_el1, %0" :: "r"(mair));

    /*
     * TCR - translation control
     *
     * T0SZ = 16 -> 48-bit VA space (256TB)
     * TG0 = 4KB granule
     * IRGN0/ORGN0 = WB WA (cache page table walks)
     * SH0 = Inner Shareable
     * IPS = 36-bit PA (64GB physical)
     * TBI0 = top byte ignore (required for MTE tagged pointers)
     */
    tcr = TCR_T0SZ(48) | TCR_TG0_4KB | TCR_IRGN0_WB_WA | TCR_ORGN0_WB_WA
            | TCR_SH0_INNER | TCR_IPS(pa_range()) | TCR_TBI0 | TCR_TBI1
            | TCR_T1SZ(48) | TCR_TG1_4KB | TCR_IRGN1_WB_WA | TCR_ORGN1_WB_WA
            | TCR_SH1_INNER;

    if (asid_bits() == 16) {
        tcr |= TCR_AS;
    }

    if (has_hafdbs()) {
        tcr |= TCR_HA;
    }

    asm volatile("msr tcr_el1, %0" :: "r"(tcr));

    /* TTBR0 - pointe to the L0 table */
    asm volatile("msr ttbr0_el1, %0" :: "r"(l0_table));
    asm volatile("msr ttbr1_el1, %0" :: "r"(l0_table));

    /* barriers: ensure all register writes complete */
    dsb();
    isb();

    /* Invalidate stale stage-1 TLB and instruction cache before turning
     * translation on. On a cold boot these are already empty, but on real
     * hardware (warm reset, or firmware that ran with the MMU on) leftover
     * entries would otherwise be consulted the instant SCTLR_EL1.M flips.
     * 'nsh' scope is enough, as on boot path every CPU will run this.
     */
    asm volatile("tlbi vmalle1");
    asm volatile("ic iallu");
    asm volatile("dsb nsh");
    asm volatile("isb");

    /*
     * Build SCTLR: MMU, caches, PAN auto-arm. The MTE bits are set separately
     * by mte_init() once translation is live.
     */
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

    sctlr = SCTLR_EL1_BASE | SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I
            | SCTLR_EL1_SA | SCTLR_EL1_SA0;
    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr));

    /* Flush pipeline: CPU must re-fetch with MMU active */
    isb();

    /*
     * PAN - Privileged Access Never
     *
     * Required by av6. Panic on CPUs without FEAT_PAN rather than
     * silently running unprotected. Neoverse-N2 has it; this is
     * a safety net for anyone bringing the kernel up elsewhere.
     *
     * SCTLR.SPAN was cleared above, so every future exception
     * entry will arrive with PSTATE.PAN=1. Set it now as well
     * so the kernel runs PAN-on immediately, not just after the
     * first trap.
     */
    if (!has_pan()) {
        panic("kvminithart: CPU does not implement FEAT_PAN");
    }
    pan_on();
    isb();

    mte_init();

    if (cpuid() == 0) {
        printk("kvminithart: MMU enabled, PAN armed, MTE: user tags armed\n");
        printk("cpu: PA %d-bit, HAFDBS %s, FEAT_NMI %s\n",
            parange_bits[pa_range()], has_hafdbs() ? "yes" : "no",
            SYS_FIELD(read_sysreg(id_aa64pfr1_el1), ID_AA64PFR1_NMI)
                != ID_AA64PFR1_NMI_NONE ? "yes" : "no");
    }
}

/* Per-process kernel stacks (the kstack window) */

/*
 * kstack_walk - find (optionally create) the L3 PTE for a kstack-window VA.
 *
 * The window is one 1 GB L1 slot covered by the static l2_kstack. The L3 tables
 * beneath it are allocated lazily here, the first time a stack in their 2 MB
 * span is mapped. With alloc == 0 or missing L3 returns NULL (used by free / lookup).
 */
static pte_t *kstack_walk(uint64 va, int alloc)
{
    uint64 l2i = L2X(va);
    pte_t *l3;

    if (!(l2_kstack[l2i] & PTE_VALID)) {
        if (!alloc) {
            return NULL;
        }
        l3 = ptalloc();
        if (l3 == NULL) {
            return NULL;
        }
        l2_kstack[l2i] = KVA_TO_PA((uintptr)l3) | PTE_VALID | PTE_TABLE;
    }

    l3 = (pte_t *)PA_TO_KVA(PTE_ADDR(l2_kstack[l2i]));
    return &l3[L3X(va)];
}

/* kstack_base_va - VA of the lowest stack page (just above the guard) for slot. */
static uint64 kstack_base_va(int idx)
{
    return KSTACK_WINDOW
        + (uint64)idx * KSTACK_SLOT_PAGES * PGSIZE
        + (uint64)KSTACK_GUARD_PAGES * PGSIZE;
}

/*
 * kstack_alloc - may slot 'idx's kernel stack in the kstack window.
 *
 * Each slot is a power-of-2 32 KB block. THe lower KSTACK_GUARD_PAGES are the
 * unmapped guard, the upper KSTACK_PAGES are the stack. Returns the top-of-stack
 * VA (where SP_EL1 starts) or 0 on out-of-memory.
 */
uint64 kstack_alloc(int idx)
{
    uint64 base_va, va;
    int i, j;
    char *mem;
    pte_t *pte;

    KASSERT(idx >= 0
        && (uint64)(idx + 1) * KSTACK_SLOT_PAGES * PGSIZE <= KSTACK_WINDOW_SIZE,
        "kstack_alloc: slot %d outside the kstack window", idx);

    base_va = kstack_base_va(idx);

    acquire_spinlock(&kstack_lock);

    for (i = 0; i < KSTACK_PAGES; i++) {
        va = base_va + (uint64)i * PGSIZE;

        pte = kstack_walk(va, 1);
        mem = (pte != NULL) ? kpage_alloc() : NULL;
        if (pte == NULL || mem == NULL) {
            /* out of memory (L3 page or stack page) : unwind, then fail */
            if (mem != NULL) {
                kpage_free(mem);
            }
            for (j = 0; j < i; j++) {
                pte = kstack_walk(base_va + (uint64)j * PGSIZE, 0);
                kpage_free((char *)PA_TO_KVA(PTE_ADDR(*pte)));
                *pte = 0;
            }
            release_spinlock(&kstack_lock);
            return 0;
        }
        *pte = KVA_TO_PA((uintptr)mem) | PAGE_KERNEL;
    }

    dsb_ishst();
    isb();

    release_spinlock(&kstack_lock);

    return KSTACK_WINDOW + (uint64)(idx + 1) * KSTACK_SLOT_PAGES * PGSIZE;
}

/*
 * kstack_free : unmap and free slot 'idx's kernel-stack pages.
 *
 * The kstack window is a GLOBAL kernel mapping (TTBR1, shared by every CPU),
 * and a dying proc may have run on several CPUs, so the stale entries must be
 * dropped across the inner-shareable domain before the slot is reused. Order: clear
 * PTEs, make the clears visible, broadcast-invalidation each VA, wait. The L3
 * table is kept (the slot will be reused by a future proc).
 */
void kstack_free(int idx)
{
    uint64 base_va;
    int i;
    pte_t *pte;

    base_va = kstack_base_va(idx);

    acquire_spinlock(&kstack_lock);

    for (i = 0; i < KSTACK_PAGES; i++) {
        pte = kstack_walk(base_va + (uint64)i * PGSIZE, 0);
        if (pte != NULL && (*pte & PTE_VALID)) {
            kpage_free((char *)PA_TO_KVA(PTE_ADDR(*pte)));
            *pte = 0;
        }
    }

    /* Drop the stale translations */
    tlbi_vaae1is_range(base_va, KSTACK_PAGES);

    release_spinlock(&kstack_lock);
}

/*
 * kstack_slot_alloc - reserve a free kstack-window slot id, or -1 if the window is
 * full (the graceful proc-exhaustion path: allocproc then fails, fork returns -1).
 * The slot id indexes kstack_alloc/kstack_free. It replaces the old proctab index.
 */
int kstack_slot_alloc(void)
{
    int slot;

    acquire_spinlock(&kstack_lock);
    bit_ffc(kstack_slotmap, (int)KSTACK_NSLOTS, &slot);
    if (slot >= 0) {
        bit_set(kstack_slotmap, slot);
    }
    release_spinlock(&kstack_lock);

    return slot;
}

/* kstack_slot_free - release a slot back to the window */
void kstack_slot_free(int slot)
{
    acquire_spinlock(&kstack_lock);
    bit_clear(kstack_slotmap, slot);
    release_spinlock(&kstack_lock);
}

/* User page tables */

/*
 * Allocate a page-table page: kalloc + zero + strip MTE tags
 *
 * Page table entries store physical addresses. The MTE tag from
 * kalloc must be stripped before converting KVA->PA for the entry.
 * Returns the clean (untagged) KVA.
 */
static pte_t *ptalloc(void)
{
    char *mem;

    mem = kpage_alloc();
    if (mem == NULL) {
        return NULL;
    }

    memset(mem, 0, PGSIZE);

    return (pte_t *)mem;
}

/*
 * Walk the 4-level page table to find the L3 PTE for a user VA.
 *
 * If alloc is set, missing intermediate tables are allocated.
 * Returns pointer to the L3 PTE, or NULL on failure.
 *
 * L0[VA[47:39]] -> L1[VA[38:30]] -> L2[VA[29:21]] -> L3[VA[20:12]]
 */
static pte_t *walk(pte_t *pagetable, uintptr va, int alloc)
{
    pte_t *pte, *next;
    int level;

    KASSERT(va < MAXUVA, "walk: va %#lx out of range", va);

    for (level = 0; level < 3; level++) {
        pte = &pagetable[(va >> pt_shift[level]) & (TABLE_ENTRIES - 1)];

        if (*pte & PTE_VALID) {
            /* follow table desc to next level */
            pagetable = (pte_t *)PA_TO_KVA(PTE_ADDR(*pte));
        } else {
            if (!alloc) {
                return NULL;
            }

            next = ptalloc();
            if (next == NULL) {
                return NULL;
            }

            *pte = KVA_TO_PA((uintptr)next) | PTE_VALID | PTE_TABLE;
            pagetable = next;
        }
    }

    /* At L3: return pointer to the page entry */
    return &pagetable[L3X(va)];
}

/*
 * Map a range of user virtual addresses to physical addresses.
 *
 * va and pa must be page-aligned. Creates L3 page entries
 * with the given permission bits.
 *
 * Returns 0 on success, -1 on failure (out of memory)
 */
int mappages(pte_t *pagetable, uintptr va, uintptr pa, uint64 sz, uint64 perm)
{
    uintptr a, last;
    pte_t *pte;

    KASSERT(sz != 0, "mappages: sz == 0");

    a = PGROUNDDW(va);
    last = PGROUNDDW(va + sz - 1);

    while (true) {
        pte = walk(pagetable, a, 1);
        if (pte == NULL) {
            return -1;
        }

        KASSERT(!(*pte & PTE_VALID), "mappages: remap at va %#lx", a);

        *pte = pa | perm;
        if (a == last) {
            break;
        }

        a += PGSIZE;
        pa += PGSIZE;
    }

    /* Publish every PTE written above */
    dsb_ishst();

    return 0;
}

/*
 * icache_sync_page : make one freshly-written page coherent for
 * instruction fetch.
 *
 * After the kernel writes bytes a user will execute, the D-cache
 * holds them but the I-cache cannot see them. Clean every D-cache
 * line in the page to the Point of Unification (dc cvau), then
 * invalidate the matching I-cache lines (ic ivau). The loop strides
 * by cacheline_size() so no line is skipped on any implementation.
 *
 * dsb ish orders the maintenance to completion. isb re-syncs the
 * fetch pipeline. Mandatory after kernel-side writes user code.
 */
void icache_sync_page(void *page)
{
    uintptr a;
    uintptr base = (uintptr)page;
    uint64 step = cacheline_size();

    for (a = base; a < base + PGSIZE; a += step) {
        asm volatile("dc cvau, %0" :: "r"(a) : "memory");
    }
    dsb_ish();

    for (a = base; a < base + PGSIZE; a += step) {
        asm volatile("ic ivau, %0" :: "r"(a) : "memory");
    }
    dsb_ish();
    isb();
    // asm volatile("isb");
}

/*
 * uvmcopy: duplicate a user address space for fork()
 *
 * Walks src's mappings in [USER_BASE, USER_BASE + sz) and creates
 * a parallel mapping in dst with freshly allocated, byte-for-byte
 * copies of each page. Page permissions/attrs are inherited from
 * source PTE (so RX code stays RX, RW data stays RW once we have
 * those distinctions).
 *
 * Returns 0 on success, -1 on out-of-memory. On failure the caller
 * is expected to release dst via uvmfree() : freewalk() copes with
 * partially-populated pagetables.
 *
 * MTE note: page contents are copied via the kernel DMAP (untagged),
 * but per-granule allocation tags are NOT propagated. Userland that
 * carries IRG-tagged pointers across fork() will tag-fault.
 */
int uvmcopy(pte_t *src, pte_t *dst, uint64 sz)
{
    pte_t *pte;
    uintptr pa, va;
    uint64 flags;
    char *mem;

    for (va = USER_BASE; va < USER_BASE + sz; va += PGSIZE) {
        pte = walk(src, va, 0x0);
        if (pte == NULL || (*pte & PTE_VALID) == 0) {
            continue;
        }

        pa = PTE_ADDR(*pte);
        flags = *pte ^ pa;

        mem = kpage_alloc();
        if (mem == NULL) {
            return -1;
        }

        memcpy(mem, (void *)PA_TO_KVA(pa), PGSIZE);

        /*
         * Make the copied page coherent for instruction fetch:
         * a forked code page lands via D-cache writes the I-cache
         * cannot see. icache_sync_page covers the whole page.
         */
        icache_sync_page(mem);

        if (mappages(dst, va, KVA_TO_PA((uintptr)mem), PGSIZE, flags) < 0) {
            kpage_free(mem);
            return -1;
        }
    }

    return 0;
}

/*
 * uvmalloc : grow the user mapping from oldsz to newsz.
 *
 * Allocates and zeroes pages for [PGROUNDUP(oldsz), newsz) and
 * maps them with PAGE_USER permissions. On out-of-memory, rolls
 * back any pages already mapped via uvmdealloc and returns 0.
 *
 * Returns the new size (newsz) on success, 0 on failure.
 *
 * Both oldsz and newsz are byte counts measured from USER_BASE
 * (matching uvmcopy's convention). The caller (growproc) passes
 * p->sz directly.
 */
uint64 uvmalloc(pte_t *pagetable, uint64 oldsz, uint64 newsz)
{
    char *mem;
    uint64 a;

    if (newsz < oldsz) {
        return oldsz;
    }

    oldsz = PGROUNDUP(oldsz);
    for (a = oldsz; a < newsz; a += PGSIZE) {
        mem = kpage_alloc();
        if (mem == NULL) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }

        memset(mem, 0, PGSIZE);

        if (mappages(pagetable, USER_BASE + a, KVA_TO_PA((uintptr)mem),
            PGSIZE, PAGE_USER) < 0) {
            kpage_free(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }

    return newsz;
}

/*
 * uvmunmap_range : free leaves and collapse empty tables in
 * [va_start, va_end) of a user pagetable.
 *
 * Walks the affected subtree top-down. On the way back up,
 * any intermediate L3/L2/L1 table whose entries are all
 * zero is freed and the parent's pointer to it is cleared.
 * The L0 root is never freed here : it is the pagetable handle
 * itself, owned by uvmfree.
 *
 * Single-pass recursion : data-page free + table-collapse share
 * one traversal, so the cost is proportional to the affected
 * subtree, not to the entire address space.
 *
 * level: depth of table. 0 = L0 (root), 1 = L1, 2 = L2, 3 = L3.
 *          L3 entries are leaves (point to user data pages).
 *
 * Preconditions:
 *      va_start, va_end aligned to PGSIZE
 *      table is a valid pagetable page at the given level
 *      PTE_VALID set on every internal-pointer entry
 */
static void uvmunmap_range(pte_t *table, int level, uintptr va_start, uintptr va_end)
{
    int shift, idx, j;
    uintptr va, next_va, sub_start, sub_end;
    uint64 entry_size;
    pte_t *pte, *child;
    char *page;

    KASSERT(level >= 0 && level <= 3, "uvmunmap_range: bad level %d", level);

    shift = pt_shift[level];
    entry_size = 1UL << shift;

    /*
     * Round va down to this level's entry boundary so the loop
     * always lands on entry-aligned addresses.
     */
    va = va_start & ~(entry_size - 1);

    while (va < va_end) {
        idx = (va >> shift) & (TABLE_ENTRIES - 1);
        pte = &table[idx];
        next_va = va + entry_size;

        if (*pte & PTE_VALID) {
            if (level == 3) {
                /*
                 * Leaf entry : free the user data page if this VA
                 * is genuinely inside the unmap range. The boundary
                 * check covers the case where va_start/va_end fall
                 * mid-L3-table.
                 */
                if (va >= va_start && va < va_end) {
                    page = (char *)PA_TO_KVA(PTE_ADDR(*pte));
                    kpage_free(page);
                    *pte = 0;
                }
            } else {
                /*
                 * Internal entry : descend, clamp to the sub-range to
                 * this entry's coverage.
                 */
                child = (pte_t *)PA_TO_KVA(PTE_ADDR(*pte));
                sub_start = (va_start > va) ? va_start : va;
                sub_end = (va_end < next_va) ? va_end : next_va;

                uvmunmap_range(child, level + 1, sub_start, sub_end);

                /*
                 * Collapse : if the child is now fully empty, free
                 * it and clear our entry. The parent (one frame up)
                 * will do the same for us if we end up empty.
                 */
                for (j = 0; j < TABLE_ENTRIES; j++) {
                    if (child[j] != 0) {
                        break;
                    }
                }
                if (j == TABLE_ENTRIES) {
                    kpage_free((char *)child);
                    *pte = 0;
                }
            }
        }

        va = next_va;
    }
}

/*
 * uvmdealloc : shrink the user mapping from oldsz to newsz.
 *
 * Walks each leaf PTE in [PGROUNDUP(newsz), PGROUNDUP(oldsz)),
 * frees the underlying page, and clears the PTE.
 *
 * Returns newsz unconditionally (the operation cannot fail; pages
 * outside the active range are simply not freed).
 *
 * Note : we do not collapse empty L3/L2/L1 tables back to UNUSED.
 * Their pages stay allocated until the entire user pagetable is
 * torn down by uvmfree.
 * TODO: revisit if sbrk-heavy workloads need it.
 *
 * TLB invalidation is the caller's responsibility (growproc does
 * a per-ASID flush so this function stays pagetable-pure).
 */
uint64 uvmdealloc(pte_t *pagetable, uint64 oldsz, uint64 newsz)
{
    uintptr va_start, va_end;

    if (newsz >= oldsz) {
        return oldsz;
    }

    va_start = USER_BASE + PGROUNDUP(newsz);
    va_end = USER_BASE + PGROUNDUP(oldsz);

    if (va_start < va_end) {
        uvmunmap_range(pagetable, 0, va_start, va_end);
    }

    return newsz;
}

/*
 * Create an empty user page table (just the L0 page)
 *
 * Returns pointer to the L0 table, or NULL on failure.
 * The caller must map pages into it with mappages().
 */
pte_t *uvmcreate(void)
{
    return ptalloc();
}

/*
 * Free a user page table and all its mapped physical pages.
 *
 * Walks the 4-level table recursively.
 * Frees:
 *      L3 entries: the mapped physical page
 *      L2/L1/L0 tables: the page-table pages themselves
 *
 * sz is the size of the user mapping (how far to walk L3).
 * For simplicity, we walk the entire table structure.
 */
static void freewalk(pte_t *pagetable, int level)
{
    pte_t entry, *child;
    char *page;

    for (int i = 0; i < TABLE_ENTRIES; i++) {
        entry = pagetable[i];

        if ((entry & PTE_VALID) == 0) {
            continue;
        }

        if (level < 3) {
            child = (pte_t *)PA_TO_KVA(PTE_ADDR(entry));
            freewalk(child, level + 1);
            pagetable[i] = 0;
        } else {
            page = (char *)PA_TO_KVA(PTE_ADDR(entry));
            kpage_free(page);
            pagetable[i] = 0;
        }
    }

    /* Free this table page itself */
    kpage_free((char *)pagetable);
}

void uvmfree(pte_t *pagetable)
{
    freewalk(pagetable, 0);
}

/*
 * Switch to a user address space.
 *
 * Loads TTBR0 with the process's page table physical address
 * and ASID. The TLB retains entries from other ASIDs so, no flush
 * needed.
 *
 * TTBR0 format:
 *      [63:48] = ASID (8-bit in low config, 16-bit if TCR.AS=1)
 *      [47:1] = BADDR (physical address of L0 table)
 *      [0] = CnP (ignored, set to 0)
 */
void switchuvm(uint16 asid, pte_t *pagetable)
{
    uintptr pa;
    uint64 ttbr;

    KASSERT(pagetable != NULL, "switchuvm: no page table");

    pa = KVA_TO_PA((uintptr)pagetable);
    ttbr = ((uint64)asid << 48) | pa;

    asm volatile("msr ttbr0_el1, %0" :: "r"(ttbr));
    isb();
}

/*
 * Part TTBR0 on the empty table. The walker may fil the TLB speculatively from
 * any reachable table, so a freed one must never stay installed.
 */
void switchuvm_reserved(void)
{
    switchuvm(ASID_RESERVED, l0_reserved);
}
