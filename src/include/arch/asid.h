/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_ASID_H_
#define _AV6_ASID_H_

#include "sys/types.h"
#include "arch/sysreg.h"

struct proc;

/*
 * ASID (Address Space Identifier) allocator
 *
 * Tags TTBR0 translations so the TLB can hold entries for many user
 * address spaces at once without flushing on every context switch.
 *
 * Rolling generation design:
 *  : A process's asid_ctx packs (generation << asid_bits) | asid.
 *  : asid_switch assigns an asid lazily on context switch, reusing
 *    the previous one if its generation is still current.
 *  : When the per-generation bitmap fills, an internal rollover
 *    bumps the generation, defers a local TLB flush to every CPU,
 *    and starts over.
 *  : ASIDs are not returned on process exit. They are reclaimed
 *    en masse at the next rollover.
 *
 * Width: 16-bit ASIDs if the hardware reports support
 * (ID_AA64MMFR0_EL1.ASIDBits = 0x2) and kvminithart sets TCR_EL1.AS;
 * otherwise 8-bit.
 */

#define ASID_RESERVED       0

/*
 * asid_bits : 8 or 16, per ID_AA64MMFR0_EL1.ASIDBits[7:4]. Used by
 * kvminithart to decide TCR_EL1.AS, and by asid_init for sizing.
 */
static inline int asid_bits(void)
{
    uint64 mmfr0 = read_sysreg(id_aa64mmfr0_el1);

    return SYS_FIELD(mmfr0, ID_AA64MMFR0_ASIDBITS) ==
           ID_AA64MMFR0_ASIDBITS_16 ? 16 : 8;
}

void asid_init(void);
void asid_switch(struct proc *p);
void asid_flush(struct proc *p);
uint16 asid_num(const struct proc *p);

#endif  /* _AV6_ASID_H_ */
