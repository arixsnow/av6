/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * swtch.s - Context switch for AArch64
 *
 * void swtch(struct context *old, struct context *new)
 *
 * Save callee-saved registers in old, load from new.
 * Returns to the new context's saved x30 (lr).
 *
 * Arguments:
 *      x0 = pointer to old context (saved current regs here)
 *      x1 = pointer to new context (loads regs from here)
 *
 * struct context layout (from cpu.h):
 *      offset 0:       x19
 *      offset 8:       x20
 *        ...
 *      offset 88:      x30 (lr)
 *      offset 96:      sp
 */

.include "assym.s"

.global swtch
swtch:
    /*
     * Complete this CPU's outstanding memory work before the thread can be
     * resumed on another CPU. A DSB only orders the stores of the CPU that
     * executes it, so a thread preempted between a PTE store in mappages()
     * and its dsb ishst would otherwise carry the barrier to a CPU that has
     * nothing to publish, leaving the store stranded in this CPU's store
     * buffer and invisible to the table walker.
     *
     * 'ish' and not 'ishst' : this also completes any in-flight cache
     * maintenance the outgoing thread issued, which is loads as well as
     * stores.
     */
    dsb         ish
    /* Save callee-saved registers to old context */
    stp         x19, x20, [x0, #CTX_X19]
    stp         x21, x22, [x0, #CTX_X21]
    stp         x23, x24, [x0, #CTX_X23]
    stp         x25, x26, [x0, #CTX_X25]
    stp         x27, x28, [x0, #CTX_X27]
    stp         x29, x30, [x0, #CTX_X29]
    mov         x9, sp
    str         x9, [x0, #CTX_SP]

    /* Load callee-saved registers from new context */
    ldp         x19, x20, [x1, #CTX_X19]
    ldp         x21, x22, [x1, #CTX_X21]
    ldp         x23, x24, [x1, #CTX_X23]
    ldp         x25, x26, [x1, #CTX_X25]
    ldp         x27, x28, [x1, #CTX_X27]
    ldp         x29, x30, [x1, #CTX_X29]
    ldr         x9, [x1, #CTX_SP]
    mov         sp, x9

    ret

.size swtch, . - swtch
