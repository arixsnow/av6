/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_MTE_H_
#define _AV6_MTE_H_

#include "sys/types.h"
#include "arch/sysreg.h"

/*
 * MTE - Memory Tagging Extension (ARMv8.5)
 *
 * Every 16-byte granule of "Tagged Normal" memory carries
 * a 4-bit allocation tag stored out-of-band. Every virtual
 * pointer to that memory carries a 4-bit logical tag in bits
 * [59:56] or the address. On a load/store, the hardware
 * compares the two; a mismatch is a tag check fault.
 *
 * AV6 Policy:
 *      TCF0 = sync : user tag mismatches fault immediately
 *      TCF = off : kernel runs unchecked (no KASAN yet: TODO)
 *      ATA = 1 : kernel can execute IRG/STG/LDG
 *      ATA0 = 1 : user can execute IRG/STG/LDG
 */

/*
 * SCTLR_EL1 MTE bit fields.
 *
 * Bit layout:
 *      [37] ITFSB : Instruction fetch tag check fault sync barrier
 *      [39:38] TCF0 Tag Check Fault (EL0)
 *      [41:40] TCF Tag Check Fault (EL1)
 *      [42] ATA0 EL0 allocation-tag access enable
 *      [43] ATA1 El1 allocation-tag access enable
 *
 * TCF / TCF0 encoding
 *      00 = no tag check
 *      01 = synchronous
 *      10 = asynchronous (TFSR_EL1 accumulator)
 *      11 = asymmetric (sync, read, async write)
 */
#define SCTLR_EL1_ITFSB         (1UL << 37)
#define SCTLR_EL1_TCF0_SHIFT    38
#define SCTLR_EL1_TCF0_SYNC     (1UL << SCTLR_EL1_TCF0_SHIFT)
#define SCTLR_EL1_TCF0_MASK     (3UL << SCTLR_EL1_TCF0_SHIFT)
#define SCTLR_EL1_TCF_SHIFT     40
#define SCTLR_EL1_TCF_MASK      (3UL << SCTLR_EL1_TCF_SHIFT)
#define SCTLR_EL1_ATA0          (1UL << 42)
#define SCTLR_EL1_ATA           (1UL << 43)

/*
 * GCR_EL1 (Generation of Random Tags)
 *
 * Exclude[15:0]:   bitmap of tags IRG must NOT produce.
 *                  bit N set = exclude tag N.
 *
 * RRND[16]: 0 = user RGSR_EL1 LFSR, 1 = TRNG (optional).
 */
#define GCR_EL1_EXCLUDE_0_F     0x8001UL
#define GCR_EL1_RRND            (1UL << 16)

/*
 * RGSR_EL1 (Random Allocation Tag Seed)
 *
 * SEED[23:8]: 16-bit LFSR state for pseudo-random tag stream
 * TAG[3:0] last-generated tag (persists across IRGs).
 *
 * Each CPU programs a distinct seed so tag streams on different
 * cores are independent.
 */
#define RGSR_EL1_SEED_SHIFT     8

/*
 * ESR_EL1.ISS.DFSC value for a synchronous tag check fault
 * on a Data Abort (EC == 0x24 or 0x25). Bits [5:0] or ISS.
 */
#define DFSC_TAG_CHECK          0x11

/*
 * Check for FEAT_MTE2 support.
 *
 * ID_AA64PFR1_EL1.MTE, bits [11:8]
 *      0 = none
 *      1 = MTE (allocation tags, no synchronous faults)
 *      2 = MTE2 (synchronous check faults at EL0)
 *      3 = MTE3 (asymmetric mode added)
 *
 * av6 requires MTE2 or newer : synchronous user fault give a precise
 * fault PC + FAR. Async mode instead accumulates tag faults in TFSR_EL1
 * and needs software to drain it periodically.
 */
static inline int has_mte2(void)
{
    uint64 pfr1 = read_sysreg(id_aa64pfr1_el1);

    return SYS_FIELD(pfr1, ID_AA64PFR1_MTE) >= ID_AA64PFR1_MTE_MTE2;
}

/*
 * irg(base): return a tagged pointer.
 *
 * The hardware picks a tag excluded by GCR_EL1.Exclude and
 * splices it into the top byte of base. Requires ATA = 1.
 */
static inline void *irg(void *base)
{
    void *tagged;

    asm volatile("irg %0, %1" : "=r"(tagged) : "r"(base));
    return tagged;
}

/*
 * stg(va): set the allocation tag of the 16-bytegranule at va
 * to the tag embedded in the top byte of va.
 *
 * va must 16-byte aligned. The target memory must be
 * MT_NORMAL_TAGGED : on untagged memory, STG is a NOP
 */
static inline void stg(void *va)
{
    asm volatile("stg %0, [%0]" :: "r"(va) : "memory");
}

/*
 * ldg(va): return va with its tag replaced by the granule's stored tag.
 *
 * Address bits are untouched. Only [59:56] change. Untagged memory
 * reads back tag 0, so the result means nothing unless the caller
 * already knows the granule is MT_NORMAL_TAGGED.
 */
static inline void *ldg(void *va)
{
    void *tagged = va;

    asm volatile("ldg %0, [%0]" : "+r"(tagged) :: "memory");
    return tagged;
}

/*
 * stzg(va): stg() plus zeroing of the granule's 16 bytes.
 *
 * Same rules are stg(). One instruction, so the tag store and the
 * zeroing cannot be separated.
 */
static inline void stzg(void *va)
{
    asm volatile("stzg %0, [%0]" :: "r"(va) : "memory");
}

/*
 * st2g(va), stz2g(va): stg()/stzg() across two consecutive granules.
 *
 * Head and tail of a run, not the middle: bulk tagging goes through
 * DC GZVA, which tags and zeroes a whole DCZID_EL0 block.
 */
static inline void st2g(void *va)
{
    asm volatile("st2g %0, [%0]" :: "r"(va) : "memory");
}

static inline void stz2g(void *va)
{
    asm volatile("stz2g %0, [%0]" :: "r"(va) : "memory");
}

void mte_init(void);

#endif      /* _AV6_MTE_H_ */
