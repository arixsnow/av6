/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Named values of fields inside CPU feature / control registers. Centralising
 * them here keeps decoding readable and reusable.
 */

#ifndef _AV6_SYSREG_H_
#define _AV6_SYSREG_H_

#include "sys/types.h"

/*
 * read_sysreg(reg) : mrs 'reg' into a uint64 expression. 'reg' is the
 * assembler mnemonic of the system register (e.g. id_aa64mmfr0_el1)
 */

#define read_sysreg(reg)                            \
({                                                  \
    uint64 __val;                                   \
    asm volatile("mrs %0, " #reg : "=r"(__val));    \
    __val;                                          \
})

/*
 * SYS_FIELD(value, FIELD) : extract FIELD from 'value' using
 * FIELD##_SHIFT and FIELD##_WIDTH defined below.
 */
#define SYS_FIELD(_val, _field)     \
    (((_val) >> _field##_SHIFT) & ((1UL << _field##_WIDTH) - 1))

/* ID_AA64MMFR0_EL1 : Memory Model Feature Register 0 */
#define ID_AA64MMFR0_ASIDBITS_SHIFT         4
#define ID_AA64MMFR0_ASIDBITS_WIDTH         4
#define ID_AA64MMFR0_ASIDBITS_8             0x0
#define ID_AA64MMFR0_ASIDBITS_16            0x2

#define ID_AA64MMFR0_PARANGE_SHIFT          0
#define ID_AA64MMFR0_PARANGE_WIDTH          4
#define ID_AA64MMFR0_PARANGE_32             0x0
#define ID_AA64MMFR0_PARANGE_36             0x1
#define ID_AA64MMFR0_PARANGE_40             0x2
#define ID_AA64MMFR0_PARANGE_42             0x3
#define ID_AA64MMFR0_PARANGE_44             0x4
#define ID_AA64MMFR0_PARANGE_48             0x5
#define ID_AA64MMFR0_PARANGE_52             0x6
#define ID_AA64MMFR0_PARANGE_56             0x7

/* ID_AA64MMFR1_EL1 : Memory Model Feature Register 1 */
#define ID_AA64MMFR1_HAFDBS_SHIFT           0
#define ID_AA64MMFR1_HAFDBS_WIDTH           4
#define ID_AA64MMFR1_HAFDBS_NONE            0x0
#define ID_AA64MMFR1_HAFDBS_AF              0x1
#define ID_AA64MMFR1_HAFDBS_AF_DBS          0x2

/* ID_AA64PFR0_EL1 : Processor Feature Register 0 */
#define ID_AA64PFR0_RAS_SHIFT               28
#define ID_AA64PFR0_RAS_WIDTH               4
#define ID_AA64PFR0_RAS_NONE                0x0
#define ID_AA64PFR0_RAS_IMP                 0x1
#define ID_AA64PFR0_RAS_V1P1                0x2
#define ID_AA64PFR0_RAS_V2                  0x3

/*
 * ID_AA64PFR1_EL1 : Processor Feature Register 1
 *
 * MTE field encodings:
 *      0 = no MTE
 *      1 = MTE (allocation tags, no synchronous faults)
 *      2 = MTE2 (synchronous user tag-check faults at EL0)
 *      3 = MTE3 (asymmetric mode added)
 */
#define ID_AA64PFR1_MTE_SHIFT       8
#define ID_AA64PFR1_MTE_WIDTH       4
#define ID_AA64PFR1_MTE_NONE        0x0
#define ID_AA64PFR1_MTE_MTE         0x1
#define ID_AA64PFR1_MTE_MTE2        0x2
#define ID_AA64PFR1_MTE_MTE3        0x3

#define ID_AA64PFR1_NMI_SHIFT       36
#define ID_AA64PFR1_NMI_WIDTH       4
#define ID_AA64PFR1_NMI_NONE        0x0
#define ID_AA64PFR1_NMI_IMP         0x1

#endif  /* _AV6_SYSREG_H_ */
