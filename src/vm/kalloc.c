/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * kalloc.c - Physical page allocator
 *
 * One struct page per usable physical page. The descriptor live in one array
 * carved from RAM above the kernel image and sliced across the physmem segments,
 * each segment pointing at its own slice. Holes between DRAM banks, and excluded
 * rages inside them, get no descriptors at all.
 *
 * The freelist links descriptors, not data pages, and only pages physmem left
 * usable are on it.
 *
 * kinit() runs with MMU on: addresses here are KVAs, and physical memory is reached
 * through the DMAP.
 */


#include "arch/memlayout.h"
#include "arch/mmu.h"
#include "arch/pmap.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/spinlock.h"
#include "sys/types.h"
#include "vm/kalloc.h"
#include "vm/kmem.h"
#include "vm/physmem.h"

extern char end[];

/* Page flags */
#define PG_UNUSED       0       /* not on any list */
#define PG_FREE         1       /* on freelist, available */
#define PG_USED         2       /* allocated, in use */
#define PG_SLAB         3       /* allocated, carved into objects by the slab */

/*
 * Physical page descriptor. .next links free page, .flags and .refcnt are
 * always valid. The PA is not stored, page_to_pa() derives it from the owning
 * segment, which costs a short scan and saves 8 bytes per page.
 */
struct page {
    struct page *next;          /* freelist link (valid when PG_FREE) */
    uint32 flags;               /* PG_UNUSED / PG_FREE / PG_USED */
    int32 refcnt;               /* reference count (COW fork) */
};

static struct page *pages;      /* descriptor array, carved from RAM */
static uint64 npages;           /* descriptors allocated */

static struct {
    struct spinlock lock;
    struct page *freelist;
} kmem;

/* PA -> descriptor, or NULL if pa is not usable RAM */
static struct page *pa_to_page(uintptr pa)
{
    struct vm_phys_seg *seg = physmem_seg(pa);

    if (seg == NULL) {
        return NULL;
    }

    return &seg->first_page[(pa - seg->start) >> PGSHIFT];
}

/*
 * Descriptor -> PA. Scans the segments, for the same reason physmem_seg() does:
 * there are one to four of them.
 */
static uintptr page_to_pa(struct page *pg)
{
    uint64 n;
    int i;

    for (i = 0; i < vm_phys_nsegs; i++) {
        n = (vm_phys_segs[i].end - vm_phys_segs[i].start) >> PGSHIFT;
        if (pg >= vm_phys_segs[i].first_page
            && pg < vm_phys_segs[i].first_page + n) {
            return vm_phys_segs[i].start
                + ((uintptr)(pg - vm_phys_segs[i].first_page) << PGSHIFT);
        }
    }

    KASSERT_UNREACHABLE();
}

/* The slab marks its pages to kfree() can tell an object from a whole page */
void kpage_set_slab(void *va)
{
    struct page *pg = pa_to_page(KVA_TO_PA((uintptr)va));

    KASSERT(pg != NULL, "kpage_set_slab: %p is not managed RAM", va);
    pg->flags = PG_SLAB;
}

int kpage_is_slab(void *va)
{
    struct page *pg = pa_to_page(KVA_TO_PA(PGROUNDDW((uintptr)va)));

    return pg != NULL && pg->flags == PG_SLAB;
}

/*
 * kinit - carve the descriptor array and build the freelist.
 *
 * The array has to be excluded from the memory it describes. So the page count
 * is taken before that exclusion and the subtract is rerun afterwards, leaving
 * the array oversized by its own page count, which is 16KB per GB. Cheaper than
 * iterating to a fixpoint, and the reason physmem_avail() is idempotent.
 */
void kinit(void)
{
    struct vm_phys_seg *seg;
    uint64 array_bytes, n;
    uintptr array_pa, pa;
    struct page *pg;
    int i;

    init_spinlock(&kmem.lock, "kmem");

    /*
     * kvminit excluded the kernel image and the boot page tables, but left the
     * segments stale.
     */
    physmem_avail();
    npages = physmem_size() >> PGSHIFT;

    array_bytes = PGROUNDUP(npages * sizeof(struct page));
    array_pa = boot_alloc(array_bytes >> PGSHIFT);

    seg = physmem_seg(array_pa);
    if ((seg == NULL) || (array_pa + array_bytes > seg->end)) {
        /* the machine's RAM is too small, not a kernel contradiction */
        panic("kinit: no room for the %lu KB page-descriptor array",
            array_bytes >> 10);
    }

    physmem_exclude_region(array_pa, array_bytes);
    physmem_avail();

    pages = (struct page *)PA_TO_KVA(array_pa);

    /* Slice the array across the segments, in order */
    pg = pages;
    for (i = 0; i < vm_phys_nsegs; i++) {
        vm_phys_segs[i].first_page = pg;
        n = (vm_phys_segs[i].end - vm_phys_segs[i].start) >> PGSHIFT;
        pg += n;
    }

    /*
     * Every page of every segment is free by now. Each reachable descriptor is
     * written exactly once, so the array needs no prior zeroing. It is raw
     * RAM above the kernel image, never part of .bss.
     */
    for (i = 0; i < vm_phys_nsegs; i++) {
        for (pa = vm_phys_segs[i].start; pa < vm_phys_segs[i].end; pa += PGSIZE) {
            pg = pa_to_page(pa);
            pg->flags = PG_FREE;
            pg->refcnt = 0;
            pg->next = kmem.freelist;
            kmem.freelist = pg;
        }
    }

    physmem_print();
    printk("kalloc: %lu MB free in %d segment(s), %lu KB of descriptors\n",
        physmem_size() >> 20, vm_phys_nsegs, array_bytes >> 10);
}

void kpage_free(char *vaddr)
{
    struct page *pg;
    uintptr pa;

    pa = KVA_TO_PA((uintptr)vaddr);

    KASSERT(pa % PGSIZE == 0, "kfree: unaligned address %#lx", pa);

    pg = pa_to_page(pa);
    KASSERT(pg != NULL, "kfree: %#lx is not managed RAM", pa);

    acquire_spinlock(&kmem.lock);

    KASSERT(pg->flags == PG_USED || pg->flags == PG_SLAB,
        "kpage_free: %#lx not allocated (flags %d)", pa, pg->flags);
    KASSERT(pg->refcnt == 1, "kfree: %#lx bad refcnt %d", pg, pg->refcnt);

    pg->flags = PG_FREE;
    pg->refcnt = 0;
    pg->next = kmem.freelist;
    kmem.freelist = pg;

    release_spinlock(&kmem.lock);
}

static struct page *freelist_pop(void)
{
    struct page *pg;

    acquire_spinlock(&kmem.lock);

    pg = kmem.freelist;
    if (pg != NULL) {
        kmem.freelist = pg->next;
        pg->flags = PG_USED;
        pg->refcnt = 1;
    }

    release_spinlock(&kmem.lock);

    return pg;
}

char *kpage_alloc(void)
{
    struct page *pg;

    pg = freelist_pop();
    if (pg == NULL && kmem_reclaim() > 0) {
        pg = freelist_pop();
    }

    if (pg == NULL) {
        return NULL;
    }

    return (char *)PA_TO_KVA(page_to_pa(pg));
}
