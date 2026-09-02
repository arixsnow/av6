/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_TRAPFRAME_H_
#define _AV6_TRAPFRAME_H_

#include "arch/assym.h"
#include "sys/types.h"

/*
 * Trapframe: saved user state on the process's kernel stack
 *
 * When an exception arrives from EL0 (user mode), the CPU
 * switches to SP_EL1 (kernel stack) and jumps to the EL1
 * exception vector. The vector pushes this frame, then
 * calls the C trap handler with a pointer to it.
 *
 * On return to user (eret), the vector pops this frame
 * to restore all user-visible registers.
 *
 * AArch64 TTBR0/TTBR1 slipt means the kernel (TTBR1) is
 * always reachable. The trapframe lives directly on the kernel
 * stack instead of requiring a separately mapped page.
 *
 * MTE: kernel stack is kalloc'd with a random tag. SP carries
 * this tag, so all trapframe stores/loads are hardware-checked
 * against the allocation tag. Use-after-free of a dead process's
 * kstack triggers a synchronous tag-check fault.
 *
 * Offsets live in assym.h, which vectors.s and assym.c share.
 * sizeof(struct trapframe) == 288 (0x120), 16-byte aligned.
 */
struct trapframe {
    uint64 x[31];           /*   0: x0-x30 (general-purpose registers) */
    uint64 sp;              /* 248: SP_EL0 (user stack pointer) */
    uint64 elr;             /* 256: ELR_EL1 (user return address) */
    uint64 spsr;            /* 264: SPSR_EL1 (user saved PSTATE) */
    uint64 tpidr;           /* 272: TPIDR_EL0 (user thread pointer) */
    uint64 _pad;            /* 280: padding for 16-byte alignment */
};

#endif      /* _AV6_TRAPFRAME_H_ */
