/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_PAN_H_
#define _AV6_PAN_H_

#include "sys/types.h"

/*
 * PAN - Privileged Access Never (ARMv8.1)
 *
 * When PSTATE.PAN = 1 any load/store issued to EL1 to a page
 * whose stage-1 descriptor carries AP[1]=1 (user-accessible)
 * takes a permission fault. This catches a whole class of bugs
 * where kernel code accidentally dereferences a user pointer
 * with a normal ldr/str instead of going through copyin/copyout.
 *
 * Legitimate access to user memory goes via LDTR/STTR. The "unprivileged"
 * variants, which are explicitly allowed to touch user pages from
 * EL1 and bypass the PAN check.
 *
 * SCTLR_EL1.SPAN (bit 23):
 *      0 = PAN is set to 1 automatically on exception entry to EL1
 *      1 = PAN is left unchanged on entry (pre-v8.1 compat default)
 * av6 clears SPAN so every trap into the kernel arrives with PAN
 * armed. The BSP/AP init path below also sets PSTATE.PAN=1 once
 * at bott so the kernel starts in the PAN-on state even before
 * the first exception.
 */

#define SCTLR_EL1_SPAN          (1UL << 23)

/*
 * Check whether the CPU implements FEAT_PAN.
 * ID_AA64MMFR1_EL1.PAN is bits [23:20]:
 *      0 = not implemented     (panic: av6 requires PAN)
 *      1 = PAN
 *      2 = PAN + AT S1E1R/W PAN-aware (PAN2)
 *      3 = PAN3
 */
static inline int has_pan(void)
{
    uint64 mmfr1;

    asm volatile("mrs %0, id_aa64mmfr1_el1" : "=r"(mmfr1));
    return ((mmfr1 >> 20) & 0xF) != 0;
}

/*
 * Set PSTATE.PAN = 1 (block kernel access to user pages).
 * Used once at boot; exception entry sets it automatically
 * thereafter because SCTLR_EL1_SPAN = 0.
 */

static inline void pan_on(void)
{
    asm volatile("msr pan, #1" ::: "memory");
}

/*
 * Clear PSTATE.PAN = 0 (allow kernel access to user pages).
 * Reserved for future use; the normal path is copyin/copyout
 * via LDTR/STTR, which bypasses PAN without toggling it.
 */
static inline void pan_off(void)
{
    asm volatile("msr pan, #0" ::: "memory");
}

#endif      /* _AV6_PAN_H_ */
