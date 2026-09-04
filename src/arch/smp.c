/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * smp.c - multi-core boot via PSCI
 *
 * PSCI (Power State Coordination Interface) is ARM's firmware API
 * for CPU power management.
 */

#include "arch/arm64.h"
#include "arch/cpu.h"
#include "arch/dtb.h"
#include "arch/gic.h"
#include "sys/kio.h"
#include "arch/memlayout.h"
#include "sys/param.h"
#include "arch/smp.h"
#include "sys/proc.h"
#include "arch/timer.h"
#include "sys/types.h"
#include "arch/pmap.h"

/* PSCI function IDs (SMC Calling Convention, 64-bit) */
#define PSCI_CPU_ON         0xC4000003UL

#define SMP_ONLINE_SECS     5
#define SMP_ONLINE_SPINS    500000000UL

/* Per-CPU state */
struct cpu cpus[NCPU_MAX];

extern void mpentry(void);

/* Called from entry.s */
void mpboot(void);

/*
 * Issue a PSCI call via HVC.
 * x0 = function ID, x1-x3 = arguments, return value in x0.
 */
static int64 psci_call(uint64 fn, uint64 arg0, uint64 arg1, uint64 arg2)
{
    register uint64 x0 asm("x0") = fn;
    register uint64 x1 asm("x1") = arg0;
    register uint64 x2 asm("x2") = arg1;
    register uint64 x3 asm("x3") = arg2;

    if (psci_conduit == PSCI_CONDUIT_SMC) {
        asm volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    } else {
        asm volatile("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    }

    return (int64)x0;
}

/*
 * Wake all secondary CPUs.
 * Called by BSP (CPU 0) after its own init is complete
 */
void smp_init(void)
{
    uint64 deadline, freq, spins;
    int i, online;
    int64 ret;

    online = 1;

    if (ncpus > 1) {
        printk("smp: bringing up %d secondary CPUs\n", ncpus - 1);
    }

    for (i = 1; i < ncpus; i++) {
        /*
         * PSCI CPU_ON:
         *      arg0 = target MPIDR (which core to wake)
         *      arg1 = entry point (where it starts executing)
         *      arg2 = context ID (passed in x0 to the entry point)
         *
         * We pass CPU ID as context so mpentry can set
         * tpidr_el1 without reading MPIDR.
         */
        ret = psci_call(PSCI_CPU_ON, cpu_mpidr[i], KVA_TO_PA((uint64)mpentry), i);
        if (ret != 0) {
            pr_warn("smp: CPU %d PSCI CPU_ON failed (error %d)\n", i, ret);
            continue;
        }

        /* wait for CPU i to come online before waking up the next */
        freq = read_cntfrq();
        spins = SMP_ONLINE_SPINS;
        deadline = (freq != 0) ? read_cntpct() + freq * SMP_ONLINE_SECS : 0;

        while (cpus[i].online == 0) {
            if (freq != 0) {
                if (read_cntpct() >= deadline) {
                    break;
                }
            } else if (--spins == 0) {
                break;
            }
        }

        if (cpus[i].online == 0) {
            pr_warn("smp: CPU %d did not come online\n", i);
            continue;
        }

        dmb();      /* pairs with the dsb_ishst() in mpmain() */
        online++;
    }
    printk("smp: %d of %d CPUs are online\n", online, ncpus);
}


void mpboot(void)
{
    dsb();
    kvminithart();
}

void mpmain(void)
{

    gic_inithart();
    timer_inithart();

    printk("cpu%d: online\n", cpuid());

    /* Publish the per-CPU setup above before smp_init() can observe it. */
    dsb_ishst();
    cpus[cpuid()].online = 1;
    daif_procctx();
    scheduler();
}
