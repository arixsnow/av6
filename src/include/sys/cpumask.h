/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * cpumask.h - a set of CPUs, as a bitmap over NCPU_MAX
 *
 * Ops are non-atomic: the only user today (the ASID deferred-flush mask) is
 * always manipulated under asid_lock. A future lockless consumer would add
 * atomic variants.
 */

#ifndef _AV6_CPUMASK_H_
#define _AV6_CPUMASK_H_

#include "sys/types.h"
#include "sys/param.h"
#include "sys/bitstring.h"

struct cpumask {
    bitstr_t bits[BITSTR_NWORDS(NCPU_MAX)];
};

static inline void cpumask_zero(struct cpumask *m)
{
    for (int i = 0; i < BITSTR_NWORDS(NCPU_MAX); i++) {
        m->bits[i] = 0;
    }
}

static inline void cpumask_set(struct cpumask *m, int cpu)
{
    bit_set(m->bits, cpu);
}

static inline void cpumask_clear(struct cpumask *m, int cpu)
{
    bit_clear(m->bits, cpu);
}

static inline int cpumask_test(const struct cpumask *m, int cpu)
{
    return bit_test(m->bits, cpu);
}

#endif  /* _AV6_CPUMASK_H_ */
