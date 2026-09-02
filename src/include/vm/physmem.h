/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * physmem.h - physical memory map
 *
 * DRAM does not start at 0 and need not be contiguous: it occupies one or more
 * banks at addresses the board chose, and the device tree reports them. Two sets
 * are kept:
 *      hardware regions    all physical RAM            -> kvminit maps these
 *      avail segments      hardware minus exclusions   -> kalloc frees these
 *
 * Exclusions are the kernel image, the boot page tables, the page descriptor
 * array, and later /reserved-memory. Filled by dtb_init() with the MMU off,
 * so the arrays are static. Memory cannot be allocated to describe where memory is.
 * Overflow panics. A bank is never silently dropped.
 */

#ifndef _AV6_PHYSMEM_H_
#define _AV6_PHYSMEM_H_

#include "sys/types.h"

#define MAX_HWCNT           16      /* DTB-reported banks */
#define MAX_EXCNT           16      /* excluded ranges */
#define VM_PHYSSEG_MAX      64      /* avail segments (banks split by exclusions) */

struct page;

struct region {
    uint64 addr;
    uint64 size;
};

/*
 * [start, end) of usable RAM, page-aligned. first_page addresses this segment's
 * slice of the descriptor array, so PA -> struct page is
 * &first_page[(pa - start) >> PGSHIFT]. Per-segment bases are what let holes
 * between banks cost no descriptors. NULL until kinit().
 */
struct vm_phys_seg {
    uint64 start;
    uint64 end;
    struct page *first_page;
};

extern struct vm_phys_seg vm_phys_segs[VM_PHYSSEG_MAX];
extern int vm_phys_nsegs;

/* Record a bank of RAM. dtb_init() only. Sorted insert, coalescing neighbours. */
void physmem_hardware_region(uint64 pa, uint64 size);

/* Withhold a range from kalloc. Must precede physmem_avail(). */
void physmem_exclude_region(uint64 pa, uint64 size);

/* True if any part of [pa, pa + size) is excluded */
bool physmem_excluded(uint64 pa, uint64 size);

/*
 * Subtract the exclusions from the hardware regions and publish the result in
 * vm_phys_segs[]. Idempotent: kinit() runs it twice, once to size the descriptor
 * array and again after excluding the array itself.
 */
void physmem_avail(void);

/*
 * Segment containing pa, or NULL. Linear scan, the runtime count 1 (QEMU virt)
 * to 4 (a real board).
 */
struct vm_phys_seg *physmem_seg(uint64 pa);

uint64 physmem_size(void);          /* usable bytes. holes and exclusions omitted */
uint64 physmem_start(void);         /* lowest hardware base */
uint64 physmem_end(void);           /* highest hardware limit. holes included */

void physmem_print(void);

#endif  /* _AV6_PHYSMEM_H_ */
