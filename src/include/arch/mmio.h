/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * mmio.h - accessors for memory-mapped device registers.
 *
 * Every device-register access in AV6 goes through one of these four
 * helpers rather than a direct 'volatile' pointer dereference. The
 * payoff is a single point, an alignment check, access tracing, or
 * a Device-vs-Normal ordering barrier can be added here once and every
 * driver inherits it.
 *
 * AV6 maps device MMIO as Device-nGnRnE, so the architecture already
 * keeps accesses to a given device non-reordered and non-gathering
 * so two mmio_*() calls to the same block stay in program order with
 * no explicit barrier. What the memory type does not cover is ordering
 * a device access against Normal memory (a descriptor written to RAM
 * before a doorbell ring), that is where a future dsb/dmb belongs.
 *
 *
 * For now: every device AV6 drives (PL011, GICv3) uses 32-bit registers.
 * Wider or narrower variants get added when a device actually need one.
 */

#ifndef _AV6_MMIO_H_
#define _AV6_MMIO_H_

#include "sys/types.h"

static inline uint32 mmio_read32(uintptr addr)
{
    return *(volatile uint32 *)addr;
}

static inline void mmio_write32(uintptr addr, uint32 val)
{
    *(volatile uint32 *)addr = val;
}

/* read-modify-write: set the bits in 'mask' */
static inline void mmio_setbits32(uintptr addr, uint32 mask)
{
    mmio_write32(addr, mmio_read32(addr) | mask);
}

/* read-modify-write: clear the bits in 'mask', leave the rest */
static inline void mmio_clrbits32(uintptr addr, uint32 mask)
{
    mmio_write32(addr, mmio_read32(addr) & ~mask);
}

static inline void mmio_clrsetbits32(uintptr addr, uint32 mask, uint32 val)
{
    mmio_write32(addr, (mmio_read32(addr) & ~mask) | (val & mask));
}

static inline uint64 mmio_read64(uintptr addr)
{
    return *(volatile uint64 *)addr;
}

static inline void mmio_write64(uintptr addr, uint64 val)
{
    *(volatile uint64 *)addr = val;
}

#endif  /* _AV6_MMIO_H_ */
