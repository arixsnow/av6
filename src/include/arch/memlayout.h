/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_MEMLAYOUT_H_
#define _AV6_MEMLAYOUT_H_

#include "arch/assym.h"

/*
 * AArch64 virtual address space split (48-bit VA, TBI enabled)
 *
 *      TTBR0 (user, per-process)
 *      0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF
 *
 *      TTBR1 (kernel, shared)
 *      0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF
 *
 * The CPU selects TTBR0 or TTBR1 based on VA[63]:
 *      bit 63 = 0 -> TTBR0 (user)
 *      bit 63 = 1 -> TTBR1 (kernel)
 *
 * Kernel maps all physical memory at KERN_BASE + PA:
 *      PA 0x00000000 (devices) -> KVA 0xFFFF_0000_0000_0000
 *      PA 0x08000000 (GIC) -> KVA 0xFFFF_0000_0800_0000
 *      PA 0x09000000 (UART) -> KVA 0xFFFF_0000_0900_0000
 *      PA 0x40000000 (RAM) -> KVA 0xFFFF_0000_4000_0000
 *      PA 0x40080000 (kernel) -> KVA 0xFFFF_0000_4008_0000
 */

#define KERN_BASE       0xFFFF000000000000UL

/*
 * QEMU virt machine physical memory map
 *
 *      0x00000000 - 0x08000000     Flash and other devices
 *      0x08000000 - 0x08010000     GIC distributor
 *      0x080A0000 - 0x09000000     GIC redistributors (0x20000 per CPU)
 *      0x09000000 - 0x09001000     PL011 UART
 *      0x40000000 -      ~         RAM
 */

/* Device Physical addresses (for early boot, DTB) */
#define GICD_BASE_PA    0x08000000UL        /* Distributor: global, routes SPIs */
#define GICR_BASE_PA    0x080A0000UL        /* Redistributor: per-CPU, SGIs/PPIs */
#define UART0_BASE_PA   0x09000000UL

/*
 * Kernel Virtual Address <-> Physical Address
 *
 * The kernel identity-maps all physical memory at offset KERN_BASE
 * These macros convert between the two.
 */
#define KVA_TO_PA(addr)     ((uintptr)(addr) - KERN_BASE)
#define PA_TO_KVA(addr)     ((uintptr)(addr) + KERN_BASE)

/* Legacy names used throughout the codebase */
#define VTP(addr)           KVA_TO_PA(addr)
#define PTV(addr)           ((char *)PA_TO_KVA(addr))

/*
 * Device kernel virtual addresses (DMAP)
 *
 * After MMU is on, all device MMIO must go through TTBR1
 * so that TTBR0 can be per-process user page tables.
 */
#define GICD_BASE           (KERN_BASE + GICD_BASE_PA)
#define GICR_BASE           (KERN_BASE + GICR_BASE_PA)
#define UART0_BASE          (KERN_BASE + UART0_BASE_PA)

/*
 * Kernel-stack window: a 4-KB granule region for per-proc kernel stacks at a
 * high, RAM-free L1 slot. Each kstack carries an unmapped guard the 2 MB-block
 * DMAP cannot host.
 *
 * The window spans one whole 1GB L1 slot. A single static L2 table covers it.
 * The L3 tables beneath are allocated on DEMAND as stacks are created (see
 * kstack_walk). So the window's page-table footprint tracks the live proc count
 * rather than any configured ceiling. Region capacity is 1 GB / 32 KB = 32768 stacks.
 *
 * Each proc owns a power-of-2 32 KB slot, 32-KB aligned (KSTACK_WINDOW is GB-aligned).
 * Upper 16 KB = stack, lower 16 KB = guard (unmapped). The alignment lets the vectors
 * detect overflow with a one-bit test on SP. A valid SP (upper half) has bit KSTACK_SHIFT
 * set. A push into the guard clears it, caught before the guard is ever touched.
 */
#define KSTACK_WINDOW_L1    511                             /* l1_table slot (511 GB) */
#define KSTACK_WINDOW       (KERN_BASE + (511UL << 30))     /* = KERN_BASE + 511 GB */
#define KSTACK_WINDOW_SIZE  (1UL << 30)                     /* one L1 slot = 1 GB */
#define KSTACK_PAGES        4                               /* 16 KB usable stack */
#define KSTACK_GUARD_PAGES  4                               /* 16 KB guard (lower half), unmapped */
#define KSTACK_SLOT_PAGES   (KSTACK_GUARD_PAGES + KSTACK_PAGES) /* 8 pages = 32 KB */
#define KSTACK_NSLOTS       (KSTACK_WINDOW_SIZE / (KSTACK_SLOT_PAGES * PGSIZE))     /* the proc ceiling */

/*
 * MTE tag stripping for kernel virtual addresses.
 *
 * TBI (Top Byte Ignore): bits [63:56] are the top byte
 * MTE uses bits [59:56] as the 4-bit allocation tag.
 * For kernel VAs (TTBR1), the untagged top byte is 0xFF.
 * IRG replaces [59:56] with a random tag: 0xFF -> 0xFX.
 * OR-ing bits [59:56] back to 1 restores the clean KVA.
 */
#define KVA_STRIP_TAG(addr)     ((uintptr)(addr) | (0xFUL << 56))

/*
 * User virtual address layout
 *
 * initcode is mapped at VA 0. User stack pointer starts at PGSIZE
 * (grows down into the same page) for now!
 * Later should add text/data/stack/heap separation.
 */
#define USER_BASE               0x0UL

#endif  /* _AV6_MEMLAYOUT_H_ */
