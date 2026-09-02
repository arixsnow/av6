/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * vectors.s - AArch64 exception vector table
 *
 * The vector table has 16 entries (4 exception types x 4 source contexts).
 * Each entry is 128 bytes (0x80), table must be 2KB (0x800) aligned.
 *
 * Exception types:
 *  : Synchronous   - data abort, instruction abort, SVC, undefined instruction
 *  : IRQ           - hardware interrupt (from GIC)
 *  : FIQ           - fast interrupt (not used)
 *  : SError        - system error (e.g. async abort)
 *
 * Source contexts:
 *  : Current EL, SP0   - kernel using SP_EL0 (not used)
 *  : Current EL, SPx   - kernel using SP_EL1
 *  : Lower EL, AArch64 - user mode trap
 *  : Lower EL, AArch32 - not used
 *
 * Two save/restore paths:
 *  : EL1 (kernel) - 256 bytes, x0-x30 on kernel stack
 *  : EL0 (user)   - 288 bytes (TF_SIZE), full trapframe including
 *                      SP_EL0, ELR_EL1, SPSR_EL1, TPIDR_EL0
 */

.include "assym.s"

/*
 * EL1 (kernel) save/restore
 *
 * Macro: save all general-purpose registers to the stack
 *
 * Save: x0-x30 (31 registers x 8 bytes = 248 bytes).
 * Used for kernel exception where SP_EL0/ELR/SPSR are not
 * needed (live in system regs for the current EL).
 *
 * Kernel-stack overflow guard. The frame is now carved. If SP
 * has crossed below the stack's upper 16 KB into the guard (bit
 * KSTACK_SHIFT clears), divert to __bad_stack before storing
 * anything. A store into the guard is the double-fault we must
 * avoid. No scratch register is free pre-save, so x0 is freed
 * by swapping it with SP via add/sub. On the normal path both
 * are restored, and the branch leaves x0 = the overflowing SP
 * for the handler.
 */
.macro save_regs
    sub     sp, sp, #TF_SIZE
    add     sp, sp, x0
    sub     x0, sp, x0
    tbz     x0, #KSTACK_SHIFT, __bad_stack
    sub     x0, sp, x0
    sub     sp, sp, x0
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    add     x0, sp, #TF_SIZE
    str     x0, [sp, #TF_SP]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #TF_ELR]
.endm

/*
 * Macro: restore all general-purpose registers to the stack
 */
.macro restore_regs
    ldp     x0, x1, [sp, #TF_ELR]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #TF_SIZE
.endm

/*
 * EL0 (user) save/restore
 *
 * Full trapframe: x0-x30 + SP_EL0 + ELR_EL1 + SPSR_EL1 + TPIDR_EL0
 * Total: TF_SIZE (288) bytes, 16-byte aligned.
 *
 * Save: x0-x29 first (15 stp pairs), then use x0/x1 as scatch
 *          for system registers. Safe because x0/x1 are already saved.
 *
 * Restore: system registers first (using x0/x1 as scratch),
 *          then x0/x1 last so their user values are preserved.
 *
 * MTE: SP carries the kernel stack's allocation tag. All trapframe
 * stores/loads are hardware-checked against that tag.
 */

.macro save_user_regs
    sub     sp, sp, #TF_SIZE

    /* x0-x29: 15 stp pairs */
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]

    /*  x30 + SP_EL0 as a pair */
    mrs     x0, sp_el0
    stp     x30, x0, [sp, #TF_X30]

    /* ELR_EL1 + SPSR_EL1 as a pair */
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #TF_ELR]

    /* TPIDR_EL0 */
    mrs     x0, tpidr_el0
    str     x0, [sp, #TF_TPIDR]
.endm

.macro restore_user_regs
    ldr     x0, [sp, #TF_TPIDR]
    msr     tpidr_el0, x0

    ldp     x0, x1, [sp, #TF_ELR]
    msr     elr_el1, x0
    msr     spsr_el1, x1

    ldp     x30, x0, [sp, #TF_X30]
    msr     sp_el0, x0

    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #TF_SIZE
.endm

/*
 * Macro: create a vector entry that branches to a handler
 *
 * Each entry must be exactly 128 bytes (0x80)
 * .balign 0x80 pads with zeros to the next 128-byte boundary
 */
.macro vector_entry handler
    .balign 0x80
    b       \handler
.endm

/*
 * The actual vector table
 * Must be 2KB (0x800) aligned
 */
.section .text
.balign 0x800
.global exception_vectors
exception_vectors:
    /* Current EL, SP0 (not used - use always SP_EL1) */
    vector_entry        unhandled_exception
    vector_entry        unhandled_exception
    vector_entry        unhandled_exception
    vector_entry        unhandled_exception

    /* Current EL, SPx : Kernel at EL1 with SP_EL1 */
    vector_entry        el1_sync        /* Synchronous: data abort, SVC, undef */
    vector_entry        el1_irq         /* IRQ: hardware interrupt */
    vector_entry        el1_fiq         /* FIQ: no source on this platform */
    vector_entry        el1_error       /* SError: RAS-classified */

    /* Lower EL, AArch64 (user mode) */
    vector_entry        el0_sync
    vector_entry        el0_irq
    vector_entry        el0_fiq
    vector_entry        el0_error

    /* Lower EL, AArch32 */
    vector_entry        unhandled_exception
    vector_entry        unhandled_exception
    vector_entry        unhandled_exception
    vector_entry        unhandled_exception

/*
 * Handler: synchronous exceptions from EL1
 *
 * Save all registers, read exception info from system registers
 * pass them as arguments to the C handler, restore registers, return
 *
 * AArch64 system registers for exceptions:
 *      ESR_EL1  -  Exception Syndrome Register: Cause
 *                  Bits [31:26] = EC (Exception Class): type of exception
 *                  Bits [24:0] = ISS: additional info
 *      ELR_EL1  -  Exception Link Register: place of event
 *      FAR_EL1  -  Fault Address Register: the address that caused the fault
 *                  (only valid for data/instruction aborts)
 */
el1_sync:
    save_regs

    mov     x0, sp              /* arg0: trapframe */
    mrs     x1, esr_el1         /* arg1: exception syndrome */
    mrs     x2, far_el1         /* arg2: fault address */
    bl      trap_sync

    restore_regs
    eret                        /* Exception return - back to the interrupted code */

/*
 * Handler: anything nothing is being handle yet
 * Read exception info and panic
 */
el1_irq:
    save_regs

    bl      trap_irq

    restore_regs
    eret

/*
 * SError from EL1. A RAS error that hardware contained returns silently. An
 * uncontainable one does not return at all.
 */
el1_error:
    save_regs

    mov     x0, sp
    mrs     x1, esr_el1
    mrs     x2, far_el1
    bl      trap_serror

    restore_regs
    eret

el1_fiq:
    save_regs

    mov     x0, sp
    mrs     x1, esr_el1
    mrs     x2, far_el1
    bl      trap_fiq

    restore_regs
    eret

/*
 * EL0 handler (user mode exceptions)
 *
 * ON entry, the CPU has already:
 *  : Switched SP to SP_EL1 (kernel stack)
 *  : Saved return address in ELR_EL1
 *  : Saved PSTATE in SPSR_EL1
 *
 * Push full trapframe, call the C handler, then restore
 * and eret back to EL0.
 */

/*
 * Synchronous exception from EL0 (syscall, fault, etc.)
 *
 * PAN: hardware sets PSTATE.PAN=1 automatically on entry
 * because SCTLR_EL1.SPAN=0 (set in kvminithart). Any stray
 * kernel-side ldr/str through a user VA will now fault.
 * Legitimate user-memory access goes through copyin/copyout
 * user LDTR/STTR, which bypass PAN.
 *
 * Pass trapframe pointer, ESR, and FAR to C handler.
 * ESR/FAR are grabbed here because a nested IRQ in the
 * C handler would overwrite them (ESR becomes UNKNOWN
 * for async exceptions).
 */
el0_sync:
    save_user_regs

    mov     x0, sp          /* arg0: trapframe pointer */
    mrs     x1, esr_el1     /* arg1: exception syndrome */
    mrs     x2, far_el1     /* arg2: fault address */
    bl      usertrap_sync

    restore_user_regs
    eret

/*
 * IRQ from EL0
 *
 * Same GIC dispatch as EL1, but saves/restores the full
 * user trapframe. Timer preemption (yield) works correctly:
 * the trapframe stays on the kernel stack across swtch.
 */
el0_irq:
    save_user_regs

    bl      trap_irq

    restore_user_regs
    eret

el0_error:
    save_user_regs

    mov     x0, sp
    mrs     x1, esr_el1
    mrs     x2, far_el1
    bl      trap_serror

    restore_user_regs
    eret

el0_fiq:
    save_user_regs

    mov     x0, sp
    mrs     x1, esr_el1
    mrs     x2, far_el1
    bl      trap_fiq

    restore_user_regs
    eret

/* userret - enter (or return to) user mode from C code
 *
 * void userret(struct trapframe *tf)
 *
 * Called from usertrapret() for:
 *      : Initial entry to user mode (forkret -> usertrapret)
 *      : Return to user after exec
 *
 * Sets SP to the trapframe, restores all user state, and erets.
 * After eret, SP_EL1 = top of this proc's kstack-window slot.
 */
.global userret
userret:
    mov     sp, x0
    restore_user_regs
    eret

.size userret, . - userret

/*
 * __bad_stack - kernel-stack overflow landing pad.
 */
.global __bad_stack
__bad_stack:
    mov     x1, x0              /* x1 = overflowing SP (handler arg) */
    adrp    x0, overflow_stack
    add     x0, x0, :lo12:overflow_stack
    mrs     x2, tpidr_el1       /* x2 = logical CPU id */
    add     x2, x2, #1
    add     x0, x0, x2, lsl #OVERFLOW_STACK_SHIFT   /* x0 = top of this CPU's slot */
    mov     sp, x0
    mov     x0, x1              /* arg0 = overflowing SP */
    bl      handle_stack_overflow
    b       .

.size __bad_stack, . - __bad_stack

/*
 * Handler: anything nothing is being handle yet
 * Read exception info and panic
 */
unhandled_exception:
    save_regs

    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    bl      trap_unknown

    restore_regs
    eret
