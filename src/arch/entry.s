/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * entry.s - AArch64 boot stub for QEMU virt machine
 *
 * When QEMU loads the kernel with (-kernel flag)
 *  : CPU is in EL1 (Exception Level 1 = kernel privilege)
 *  : MMU is OFF (virtual address = physical address)
 *  : Caches may be off
 *  : x0 = DTB (Device Tree Blob) pointer
 *  : PC = 0x40080000
 *
 * Two entry points:
 *      _start              - BSP (CPU 0), called by QEMU at boot
 *      mpentry     - secondary CPUs, called via PSCI CPU_ON
 */

.section .text.boot
.global _start
.global mpentry

_start:
    /* Save DTB pointer before anything clobbers x0 :) */
    mov     x19, x0

    /* Mask all exceptions */
    msr     daifset, #0xf

    /* Drop to EL1 if we entered at EL2 (real firmware / virtualization=on) */
    bl      el_setup

    /*
     * BSP is logical CPU 0 by definition, not derived from MPIDR, which is
     * not a dense 0..N index on real (clustered) hardware. x1= 0 is reused
     * as this CPU's stack-slot index
     */
    mov     x1, #0
    msr     tpidr_el1, x1

    add     x2, x1, #1
    adrp    x3, _stack_base
    add     x3, x3, :lo12:_stack_base
    mov     x4, #0x8000
    mul     x2, x2, x4
    add     x3, x3, x2
    mov     sp, x3

    /* Clear BSS - only BSP does this (uninitialized globals must be zero) */
    adrp    x1, __bss_start
    add     x1, x1, :lo12:__bss_start
    adrp    x2, __bss_end
    add     x2, x2, :lo12:__bss_end
1:
    cmp     x1, x2
    b.ge    2f          /* if x1 >= x2 -> done */
    str     xzr, [x1], #8   /* store zero, then x1 += 8 */
    b       1b
2:
    /*
     * VBAR_EL1 = Vector Base Address Register for EL1
     * isb = Instruction Synchronization Barrier
     * Ensures the CPU sees the new VBAR before executing
     * any further instructions that might cause an exception
     */
    adrp    x0, exception_vectors
    add     x0, x0, :lo12:exception_vectors
    msr     vbar_el1, x0
    isb

    /*
     * Early init at PA. bl is PC-relative, which works regardless
     * of linked address. Sets up console, DTB, allocator, page tables,
     * and enables MMU
     */

    mov     x0, x19
    bl      boot_init

    /*
     * MMU is on now. TTBR0 (identity) and TTBR1 (kernel) both
     * point to the same page table. Same physical memory is
     * accessible via low VA (TTBR0) or high VA (TTBR1).
     *
     * Transition to high VA:
     *      1. VBAR -> high VA (exception go through TTBR1)
     *      2. SP -> high VA (stack goes through TTBR1)
     *      3. Jump to main at high VA (code goes through TTBR1)
     *
     * After this point, the kernel run entirely through TTBR1.
     * TTBR0 identity map remains until replaced by user page tables.
     */

    /* VBAR -> high VA */
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    /* SP -> high VA */
    movz    x0, #0xFFFF, lsl #48
    add     sp, sp, x0

    /* Jump to main at high VA */
    ldr     x1, =main
    br      x1
halt:
    wfe
    b       halt

.size _start, . - _start

/*
 * Secondary CPU entry point
 *
 * PSCI CPU_ON jumps here. x0 = CPU ID
 * MMU is OFF, caches may be off - same state as BSP at boot
 */
mpentry:
    /* x0 = CPU ID (passed by PSCI as context_id) */
    mov     x19, x0

    msr     daifset, #0xf
    bl      el_setup

    msr     tpidr_el1, x19

    /* Setup per-CPU stack */
    add     x2, x19, #1
    adrp    x3, _stack_base
    add     x3, x3, :lo12:_stack_base
    mov     x4, #0x8000
    mul     x2, x2, x4
    add     x3, x3, x2
    mov     sp, x3

    /* VBAR at PA */
    adrp    x0, exception_vectors
    add     x0, x0, :lo12:exception_vectors
    msr     vbar_el1, x0
    isb

    bl      mpboot

    /* Transition to high VA */
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    movz    x0, #0xFFFF, lsl #48
    add     sp, sp, x0

    ldr     x0, =mpmain
    br      x0

    /* Should not return, but just in-case */
    b       halt

.size mpentry, . - mpentry

/*
 * el_setup: return to the caller (address in x30) executing at EL1 with a
 * known PSTATE, configuring the EL1 world on the way down if we entered at
 * EL2. Entry above EL2 does not return. Clobbers x0, preserves the rest.
 */
el_setup:
    mrs     x0, CurrentEL
    cmp     x0, #(3 << 2)
    b.hs    el_unsupported
    cmp     x0, #(2 << 2)
    b.ne    1f

    /* EL1 executes AArch64, no VHE, no EL2 traps (HCR_EL2.RW = bit 31) */
    movz    x0, #0x8000, lsl #16
    msr     hcr_el2, x0
    isb

    mrs     x0, cptr_el2
    bic     x0, x0, #(1 << 10)
    bic     x0, x0, #(1 << 8)
    msr     cptr_el2, x0

    mrs     x0, midr_el1
    msr     vpidr_el2, x0
    mrs     x0, mpidr_el1
    msr     vmpidr_el2, x0

    mrs     x0, cnthctl_el2
    orr     x0, x0, #0x03
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    mrs     x0, icc_sre_el2
    orr     x0, x0, #1
    orr     x0, x0, #(1 << 3)
    msr     icc_sre_el2, x0
    isb

    mov     x0, #0x3c5
    msr     spsr_el2, x0
    msr     elr_el2, x30
    eret

1:
    mov     x0, #0x3c5
    msr     spsr_el1, x0
    msr     elr_el1, x30
    eret

.size el_setup, . - el_setup

el_unsupported:
    wfe
    b       el_unsupported
