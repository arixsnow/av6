/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * physmem.c - physical memory map
 *
 * Hardware regions come from the device tree. Exclusions are the ranges the
 * kernel has already spent: its image, the boot page tables. the page descriptor
 * array. physmem_avail() subtracts the second list from the first and publishes
 * what survives as vm_phys_segs[], the only memory kalloc may ever put on its
 * free list.
 *
 * Both lists are kept sorted and coalesced, so the subtract is one ordered walk.
 * All of this runs before an allocator exists, so the arrays are static and overflow
 * panics rather than dropping a bank.
 *
 * Rounding is deliberately asymmetric: hardware regions shrink to whole pages, exclusions
 * grow to whole pages. Both err towards handing out less.
 */

#include "sys/kio.h"
#include "arch/mmu.h"
#include "vm/physmem.h"
#include "sys/types.h"

static struct region hwregions[MAX_HWCNT];
static struct region exregions[MAX_EXCNT];
static int hwcnt;
static int excnt;

struct vm_phys_seg vm_phys_segs[VM_PHYSSEG_MAX];
int vm_phys_nsegs;

/*
 * Entry i has grown. Absorb any following entries it now overlaps or abuts, and
 * close the gap they leave behind.
 */
static int merge_upper(struct region *regions, int cnt, int i)
{
    uint64 end;
    int j, n;

    end = regions[i].addr + regions[i].size;

    for (j = i + 1; j < cnt; j++) {
        if (regions[j].addr > end) {
            break;
        }
        if (regions[j].addr + regions[j].size > end) {
            end = regions[j].addr + regions[j].size;
        }
    }

    regions[i].size = end - regions[i].addr;

    n = j - (i + 1);
    if (n == 0) {
        return cnt;
    }

    for (; j < cnt; j++) {
        regions[j - n] = regions[j];
    }

    return cnt - n;
}

/*
 * Insertion-sort [addr, addr + size) into a region list, merging into any entry
 * its overlaps or touches. Returns the new count, or -1 if the list is full.
 */
static int insert_region(struct region *regions, int cnt, int max,
    uint64 addr, uint64 size)
{
    uint64 end, rend;
    int i, j;

    end = addr + size;

    for (i = 0; i < cnt; i++) {
        rend = regions[i].addr + regions[i].size;

        if (addr <= rend && end >= regions[i].addr) {
            if (addr < regions[i].addr) {
                regions[i].addr = addr;
            }
            if (end < rend) {
                end = rend;
            }

            regions[i].size = end - regions[i].addr;

            return merge_upper(regions, cnt, i);
        }

        if (addr < regions[i].addr) {
            break;
        }
    }

    if (cnt >= max) {
        return -1;
    }

    for (j = cnt; j > i; j--) {
        regions[j] = regions[j - 1];
    }

    regions[i].addr = addr;
    regions[i].size = size;

    return cnt + 1;
}

/* Record a bank of RAM. Shrinks to whole pages. */
void physmem_hardware_region(uint64 pa, uint64 size)
{
    uint64 start, end;
    int n;

    start = PGROUNDUP(pa);
    end = PGROUNDDW(pa + size);

    if (start >= end) {
        return;
    }

    n = insert_region(hwregions, hwcnt, MAX_HWCNT, start, end - start);
    if (n < 0) {
        panic("physmem: too many memory banks (max %d)", MAX_HWCNT);
    }

    hwcnt = n;
}

/* Withhold a range from kalloc. Grows to whole pages. */
void physmem_exclude_region(uint64 pa, uint64 size)
{
    uint64 start, end;
    int n;

    start = PGROUNDDW(pa);
    end = PGROUNDUP(pa + size);

    if (start >= end) {
        return;
    }

    n = insert_region(exregions, excnt, MAX_EXCNT, start, end - start);
    if (n < 0) {
        panic("physmem: too many excluded regions (max %d)", MAX_EXCNT);
    }

    excnt = n;
}

bool physmem_excluded(uint64 pa, uint64 size)
{
    uint64 end;
    int i;

    end = pa + size;

    for (i = 0; i < excnt; i++) {
        if (pa < exregions[i].addr + exregions[i].size
            && end > exregions[i].addr) {
            return true;
        }
    }

    return false;
}

static void add_seg(uint64 start, uint64 end)
{
    struct vm_phys_seg *seg;

    if (start >= end) {
        return;
    }

    if (vm_phys_nsegs >= VM_PHYSSEG_MAX) {
        panic("physmem: too many segments (max %d)", VM_PHYSSEG_MAX);
    }

    seg = &vm_phys_segs[vm_phys_nsegs++];
    seg->start = start;
    seg->end = end;
    seg->first_page = NULL;
}

/*
 * Subtract the exclusions from the hardware regions. Both lists are sorted and
 * non-overlapping, so each bank is walked once against the exclusions that can
 * reach it.
 * Idempotent: kinit() runs this twice, once to size the descriptor array, and
 * again after excluding the array itself.
 */
void physmem_avail(void)
{
    uint64 start, end, xstart, xend;
    int hwi, exi;

    vm_phys_nsegs = 0;

    for (hwi = 0; hwi < hwcnt; hwi++) {
        start = hwregions[hwi].addr;
        end = start + hwregions[hwi].size;

        for (exi = 0; exi < excnt; exi++) {
            xstart = exregions[exi].addr;
            xend = xstart + exregions[exi].size;

            if (xend <= start) {
                continue;
            }

            if (xstart >= end) {
                break;          /* sorted: nothing later reaches this bank */
            }

            if (xstart <= start) {
                start = xend;   /* clips the front, or cover the bank */
                if (start >= end) {
                    break;
                }

                continue;
            }

            if (xend >= end) {
                end = xstart;       /* clips the tail */
                break;
            }

            add_seg(start, xstart);     /* strictly interior: split the bank */
            start = xend;
        }

        add_seg(start, end);
    }
}

struct vm_phys_seg *physmem_seg(uint64 pa)
{
    int i;

    for (i = 0; i < vm_phys_nsegs; i++) {
        if (pa >= vm_phys_segs[i].start && pa < vm_phys_segs[i].end) {
            return &vm_phys_segs[i];
        }
    }

    return NULL;
}

uint64 physmem_size(void)
{
    uint64 size;
    int i;

    size = 0;
    for (i = 0; i < vm_phys_nsegs; i++) {
        size += vm_phys_segs[i].end - vm_phys_segs[i].start;
    }

    return size;
}

uint64 physmem_start(void)
{
    if (hwcnt == 0) {
        panic("physmem: no memory banks");
    }

    return hwregions[0].addr;
}

uint64 physmem_end(void)
{
    if (hwcnt == 0) {
        panic("physmem: no memory banks");
    }

    return hwregions[hwcnt - 1].addr + hwregions[hwcnt - 1].size;
}

void physmem_print(void)
{
    int i;

    for (i = 0; i < hwcnt; i++) {
        printk("physmem: bank %d: %#lx-%#lx (%lu MB)\n", i, hwregions[i].addr,
            hwregions[i].addr + hwregions[i].size, hwregions[i].size >> 20);
    }

    for (i = 0; i < excnt; i++) {
        printk("physmem: reserved: %#lx-%#lx (%lu KB)\n", exregions[i].addr,
            exregions[i].addr + exregions[i].size, exregions[i].size >> 10);
    }

    for (i = 0; i < vm_phys_nsegs; i++) {
        printk("physmem: seg %d: %#lx-%#lx (%lu MB)\n", i, vm_phys_segs[i].start,
            vm_phys_segs[i].end, (vm_phys_segs[i].end - vm_phys_segs[i].start) >> 20);
    }
}
