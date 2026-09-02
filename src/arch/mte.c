/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * mte.c - Memory Tagging Extension setup
 *
 * Runs once per CPU, after the MMU is up. The per-thread tag lifecycle joins
 * it here when userland can opt in.
 */

#include "arch/arm64.h"
#include "arch/mte.h"
#include "sys/kio.h"
#include "sys/types.h"

/*
 * Arm EL0 tag checking. EL1 is left unchecked: the DMAP is MT_NORMAL and TCF
 * stays 00.
 *
 * GCR excludes 0x0 and 0xF so IRG never returns a tag that looks untagged.
 * RGSR is seeded from MPIDR to keep each core's tag stream distinct.
 *
 * FEAT_MTE2 is required: without it the synchronous EL0 fault doesn't exist.
 */
void mte_init(void)
{
    uint64 sctlr, mpidr, seed;

    if (!has_mte2()) {
        panic("mte_init: CPU does not implement FEAT_MTE2");
    }

    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

    sctlr &= ~SCTLR_EL1_TCF0_MASK;
    sctlr |= SCTLR_EL1_TCF0_SYNC;
    sctlr &= ~SCTLR_EL1_TCF_MASK;
    sctlr |= SCTLR_EL1_ATA;
    sctlr |= SCTLR_EL1_ATA0;

    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    isb();

    asm volatile("msr gcr_el1, %0" :: "r"((uint64)GCR_EL1_EXCLUDE_0_F));

    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

    /* SEED[23:8] non-zero. The low Aff0 byte gives per-CPU divergence */
    seed = (((mpidr & 0xff) + 1) & 0xffff) << RGSR_EL1_SEED_SHIFT;
    seed |= 0x1;        /* TAG[3:0] non-zero starting tag */
    asm volatile("msr rgsr_el1, %0" :: "r"(seed));
    isb();
}
