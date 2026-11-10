/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AV6_DTB_H_
#define _AV6_DTB_H_

#include <sys/cpumask.h>
#include <sys/fdt.h>
#include <sys/param.h>
#include <sys/types.h>

extern int ncpus;

/*
 * Logical CPU id -> MPIDR affinity, from /cpus/cpu@N "reg". cpu_mpidr[0] is
 * the boot CPU. It is the PSCI_CPU_ON target for each logical id.
 */
extern uint64 cpu_mpidr[NCPU_MAX];

enum {
    PSCI_CONDUIT_NONE = 0,
    PSCI_CONDUIT_HVC,
    PSCI_CONDUIT_SMC
};

extern int psci_conduit;

/* Logical CPUs whose enable-method is "psci". */
extern struct cpumask psci_cpus;

extern uint64 timer_freq_dtb;

void dtb_init(void *dtb);

int dtb_is_memory(const struct fdt *fdt, const struct fdt_node *node);
int dtb_is_psci(const struct fdt *fdt, const struct fdt_node *node);
int dtb_is_timer(const struct fdt *fdt, const struct fdt_node *node);
int dtb_is_gic(const struct fdt *fdt, const struct fdt_node *node);
int dtb_is_console(const struct fdt *fdt, const struct fdt_node *node);
uint32 dtb_intid(const struct fdt *fdt, const struct fdt_irq *irq);
int dtb_psci_conduit(const struct fdt *fdt, const struct fdt_node *node);
int dtb_cpu_uses_psci(const struct fdt *fdt, const struct fdt_node *cpu);
int dtb_cpu_mpidr(const struct fdt *fdt, const struct fdt_node *cpu, uint32 ac,
    uint64 *mpidr);

#endif /* _AV6_DTB_H_ */
