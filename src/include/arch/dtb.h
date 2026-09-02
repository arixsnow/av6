/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_DTB_H_
#define _AV6_DTB_H_

#include "sys/types.h"
#include "sys/param.h"

extern int ncpus;

/*
 * Logical CPU id -> MPIDR affinity, from /cpus/cpu@N "reg". cpu_mpidr[0] is
 * the boot CPU. It is the PSCI_CPU_ON target for each logical id.
 */
extern uint64 cpu_mpidr[NCPU_MAX];

enum {
    PSCI_CONDUIT_HVC = 0,
    PSCI_CONDUIT_SMC = 1
};

extern int psci_conduit;
extern uint64 timer_freq_dtb;

void dtb_init(void *dtb);

#endif  /* _AV6_DTB_H_ */
