/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * entry.h - C entry points and data the assembly uses
 */

#ifndef _AV6_ENTRY_H_
#define _AV6_ENTRY_H_

#include <arch/assym.h>
#include <arch/trapframe.h>
#include <sys/types.h>

/* entry.s */
void boot_init(void *dtb);
void main(void);
void mpboot(void);

/* vectors.s */
void trap_sync(struct trapframe *tf, uint64 esr, uint64 far);
void trap_irq(void);
void usertrap_sync(struct trapframe *tf, uint64 esr, uint64 far);
void trap_serror(struct trapframe *tf, uint64 esr, uint64 far);
void trap_fiq(struct trapframe *tf, uint64 esr, uint64 far);
void trap_unknown(uint64 esr, uint64 elr, uint64 far);
void handle_stack_overflow(uint64 sp);

extern char overflow_stack[NCPU_MAX][OVERFLOW_STACK_SIZE];

#endif /* _AV6_ENTRY_H_ */
