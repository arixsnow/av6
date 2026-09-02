/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_MMU_H_
#define _AV6_MMU_H_

/* Page / Block Sizes */

#define PGSIZE          4096
#define PGSHIFT         12

#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & (~(PGSIZE - 1)))
#define PGROUNDDW(sz)   ((sz) & (~(PGSIZE - 1)))

/* Translation table levels */

#define L0_SHIFT        39      /* Each L0 entry covers 2^39 = 512GB */
#define L1_SHIFT        30      /* Each L1 entry covers 2^30 = 1GB */
#define L2_SHIFT        21      /* Each L2 entry covers 2^21 = 2MB */
#define L3_SHIFT        12      /* Each L3 entry covers 2^12 = 4KB */
#define TABLE_ENTRIES   512     /* 2^9 entries per table */

#define BLOCK_SIZE_2M   (1UL << L2_SHIFT)
#define BLOCK_SIZE_1G   (1UL << L1_SHIFT)

/*
 * Maximum user virtual address (48-bit VA space)
 */
#define MAXUVA          (1UL << 48)

/*
 * Page table index extraction
 *
 * 48-bit VA, 4KB granule, 4-level walk:
 *      L0: VA[47:39]   (512GB per entry)
 *      L1: VA[38:30]   (1GB per entry)
 *      L2: VA[29:21]   (2MB per entry)
 *      L3: VA[20:12]   (4KB per entry)
 */
#define L0X(va)         (((uintptr)(va) >> L0_SHIFT) & (TABLE_ENTRIES - 1))
#define L1X(va)         (((uintptr)(va) >> L1_SHIFT) & (TABLE_ENTRIES - 1))
#define L2X(va)         (((uintptr)(va) >> L2_SHIFT) & (TABLE_ENTRIES - 1))
#define L3X(va)         (((uintptr)(va) >> L3_SHIFT) & (TABLE_ENTRIES - 1))

/*
 * Extract physical address from a page table entry.
 * Output address is in bits [47:12] for 4KB granule.
 */
#define PTE_ADDR(pte)   ((pte) & 0x0000FFFFFFFFF000UL)

/*
 * Descriptor type bits [1:0]
 *      V=0 : Invalid (any access faults)
 *      V=1 TBL=0 : Block descriptor (L1: 1GB, L2: 2MB)
 *      V=1 TBL=1 : Table descriptor (points to next-level table)
 *                  or page descriptor (L3 only: maps 4KB)
 */
#define PTE_VALID       (1UL << 0)
#define PTE_TABLE       (1UL << 1)
#define PTE_BLOCK       (0UL << 1)

/*
 * Lower attributes [11:2]
 *
 * AttrIndx[4:2]    - Indexes into MAIR_EL1 (which memory type)
 * AP[7:6]          - access permission
 * SH[9:8]          - shareability domain
 * AF[10]           - access flag (must be 1)
 */
#define PTE_ATTRINDX(n) ((uint64)(n) << 2)

#define PTE_AP_RW_EL1   (0UL << 6)      /* Kernel RW, User none */
#define PTE_AP_RW_ALL   (1UL << 6)      /* Kernel RW, User RW */
#define PTE_AP_RO_EL1   (2UL << 6)      /* Kernel RO, User none */
#define PTE_AP_RO_ALL   (3UL << 6)      /* Kernel RO, User RO */

#define PTE_SH_NONE     (0UL << 8)      /* Non-shareable */
#define PTE_SH_OUTER    (2UL << 8)      /* Outer shareable */
#define PTE_SH_INNER    (3UL << 8)      /* Inner shareable */

#define PTE_AF          (1UL << 10)     /* Access Flag */

/* Upper attributes */
#define PTE_PXN         (1UL << 53)     /* Privileged eXecute Never */
#define PTE_UXN         (1UL << 54)     /* Unprivileged eXecute Never */

/*
 * MAIR_EL1 - Memory Attribute Indirection Register
 *
 * 8 slots x 8 bits = 64-bit register. Block/page descriptors reference
 * a slot via AttrIndx. Two definitions:
 *
 * Slot 0 (0x00): Device-nGnRnE
 *      - No Gathering: each MMIO access hits the device separately
 *      - No Reordering: accesses arrive in program order
 *      - No Early write ack: write isn't "done" until the device says so
 *      : Perfect for(?) UART, GIC, and other device registers
 *
 * Slot 1 (0xff): Normal, Write-Back, Read-Allocate, Write-Allocate
 *      - Outer[7:4] = 0xf: WB cacheable, read/write allocate
 *      - Inner[3:0] = 0xf: WB cacheable, read/write allocate
 *      : Maximum performance for RAM
 *
 * Slot 2 (0xf0): Normal Tagged, Write-Back, Read-Allocate, Write-Allocate
 *      - Outer[7:4] = 0xf: WB cacheable, read/write allocate
 *      - Inner[3:0] = 0x0: Tagged Normal encoding (FEAT_MTE2)
 *      : RAM with MTE allocation tag support
 */
#define MT_DEVICE_nGnRnE        0
#define MT_NORMAL               1
#define MT_NORMAL_TAGGED        2

#define MAIR_DEVICE_nGnRnE      0x00UL
#define MAIR_NORMAL_WB          0xffUL
#define MAIR_NORMAL_TAGGED      0xf0UL
#define MAIR_VALUE              ((MAIR_DEVICE_nGnRnE << (MT_DEVICE_nGnRnE << 3)) \
                                | (MAIR_NORMAL_WB << (MT_NORMAL << 3)) \
                                | (MAIR_NORMAL_TAGGED << (MT_NORMAL_TAGGED << 3)))

/*
 * TCR_EL1 - Translation Control Register
 *
 * T0SZ[5:0]:       VA size = 2^(64-T0SZ). For 48-bit VA: T0SZ = 16
 * TG0[15:14]:      Granule size. 00 = 4KB
 * IRGN0[9:8]:      Inner cacheability for page walks. 01 = WB WA
 * ORGN0[11:10]:    Outer cacheability for page walks. 01 = WB WA
 * SH0[13:12]:      Shareability for page walks. 11 = Inner Shareable
 * IPS[34:32]:      Output PA size, from ID_AA64MMFR0_EL1_PARange
 */
#define TCR_T0SZ(va_bits)       ((64UL - (va_bits)) & 0x3fUL)
#define TCR_TG0_4KB             (0UL << 14)
#define TCR_IRGN0_WB_WA         (1UL << 8)
#define TCR_ORGN0_WB_WA         (1UL << 10)
#define TCR_SH0_INNER           (3UL << 12)
#define TCR_IPS_SHIFT           32
#define TCR_IPS(parange)        (((uint64)(parange) & 0x7UL) << TCR_IPS_SHIFT)
#define TCR_HA                  (1UL << 39)     /* hardware Access-flag update */
#define TCR_TBI0                (1UL << 37)     /* Top Byte Ignore for TTBR0 */
#define TCR_TBI1                (1UL << 38)
#define TCR_AS                  (1UL << 36)     /* 16-bit ASID when set */

/*
 * TCR TTBR1 fields (kernel address space)
 *
 * Note: TG1 4KB = 0b10, NOT 0b00 like TG0!
 * ARM uses different encodings for TG0 and TG1
 */
#define TCR_T1SZ(va_bits)       (((64UL - (va_bits)) & 0x3fUL) << 16)
#define TCR_TG1_4KB             (2UL << 30)
#define TCR_IRGN1_WB_WA         (1UL << 24)
#define TCR_ORGN1_WB_WA         (1UL << 26)
#define TCR_SH1_INNER           (3UL << 28)

/* SCTLR_EL1 - System Control Register (core control bits) */
#define SCTLR_EL1_M             (1UL << 0)      /* MMU enable */
#define SCTLR_EL1_A             (1UL << 1)      /* Alignment fault check */
#define SCTLR_EL1_C             (1UL << 2)      /* Data cache enable */
#define SCTLR_EL1_SA            (1UL << 3)      /* SP alignment check, EL1*/
#define SCTLR_EL1_SA0           (1UL << 4)      /* SP alignment check, EL0 */

#define SCTLR_EL1_EOS           (1UL << 11)     /* eret is context-synchronizing */
#define SCTLR_EL1_I             (1UL << 12)     /* Instruction cache enable */
#define SCTLR_EL1_WXN           (1UL << 19)     /* Writable implied execute-never */
#define SCTLR_EL1_TSCXT         (1UL << 20)     /* EL0 access to SCXTNUM_EL0 traps */
#define SCTLR_EL1_IESB          (1UL << 21)     /* Implicit error barrier at eret/entry */
#define SCTLR_EL1_EIS           (1UL << 22)     /* exception entry is context-synchronizing */
#define SCTLR_EL1_E0E           (1UL << 24)     /* EL0 data endianness (0 = LE) */
#define SCTLR_EL1_EE            (1UL << 25)     /* EL1 data endianness (0 = LE) */
#define SCTLR_EL1_nTLSMD        (1UL << 28)     /* no trap on LD/ST-multiple to device */
#define SCTLR_EL1_LSMAOE        (1UL << 29)     /* LD/ST-multiple atomicity+ordering */
#define SCTLR_EL1_EPAN          (1UL << 57)     /* PAN also blocks EL0-executable pages */

/*
 * The state SCTLR_EL1 is written from, never read back into. SPAN and EPAN are
 * deliberately absent: PAN is armed on every exception entry and never toggled.
 */
#define SCTLR_EL1_BASE          (SCTLR_EL1_LSMAOE | SCTLR_EL1_nTLSMD    \
                                | SCTLR_EL1_EIS | SCTLR_EL1_IESB        \
                                | SCTLR_EL1_TSCXT | SCTLR_EL1_EOS)



/*
 * Block descriptor templates
 *
 * BLOCK_DEVICE: for MMIO regions (UART, GIC)
 *      - Device memory (uncacheable, strictly ordered)
 *      - Kernel RW, no user access
 *      - No execute (PXN + UXN) > never run code from device memory
 *
 * BLOCK_NORMAL: for RAM
 *      - Normal cacheable memory (MT_NORMAL)
 *      - Kernel RW, no user access
 *      - Inner shareable (cache coherent access cores)
 *      - UXN set (user can not execute, kernel can execute)
 */
#define BLOCK_DEVICE            (PTE_VALID | PTE_BLOCK | PTE_ATTRINDX(MT_DEVICE_nGnRnE) \
                                | PTE_AP_RW_EL1 | PTE_SH_NONE | PTE_AF \
                                | PTE_PXN | PTE_UXN)

#define BLOCK_NORMAL            (PTE_VALID | PTE_BLOCK | PTE_ATTRINDX(MT_NORMAL) \
                                | PTE_AP_RW_EL1 | PTE_SH_INNER | PTE_AF | PTE_UXN)

/*
 * Page descriptor template (L3 user mappings)
 *
 * At L3, PTE_TABLE (bit 1) = 1 means "page descriptor" (not table).
 * The architecture reuses bit 1 with different meaning at L3.
 *
 * PAGE_USER: user-accessible 4KB page
 *      Tagged Normal Memory (MT_NORMAL_TAGGED) : every 16-byte
 *          granule carries an allocation tag, checked on EL0
 *          loads/stores when SCTLR.TCF0 = sync
 *      Kernel + User RW
 *      Inner shareable
 *      PXN: Kernel cannot execute user memory
 *      UXN clear: user CAN execute (for code pages)
 */
#define PAGE_USER               (PTE_VALID | PTE_TABLE | PTE_ATTRINDX(MT_NORMAL_TAGGED) \
                                | PTE_AP_RW_ALL | PTE_SH_INNER | PTE_AF | PTE_PXN)

/*
 * PAGE_KERNEL: a 4KB L3 kernel data page (kstack). Kernel RW, Normal cacheable,
 * inner-shareable, never executable (PXN + UXN). MT_NORMAL (untagged)
 */
#define PAGE_KERNEL             (PTE_VALID | PTE_TABLE | PTE_ATTRINDX(MT_NORMAL) \
                                | PTE_AP_RW_EL1 | PTE_SH_INNER | PTE_AF \
                                | PTE_PXN | PTE_UXN)

#endif  /* _AV6_MMU_H_ */
