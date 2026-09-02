/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_GIC_H_
#define _AV6_GIC_H_

#include "sys/types.h"

/*
 * GICv3 - Generic Interrupt Controller v3
 *
 * This helper eposes only the interface. All register definitions,
 * MMIO helpers, and ICC system register encodings are internal to gic.c
 *
 * Interrupt ID ranges:
 *      0-15    SGI (Software Generated) - inter-core IPI
 *      16-31   PPI (Private Peripheral) - per-core (timer=30)
 *      32+     SPI (Shared Peripheral) - devices (UART=33)
 */

/* Interrupt IDs */
#define IRQ_PANIC       0           /* SGI 0: panic halt IPI */
#define IRQ_TIMER       30          /* EL1 Physical Timer (PPI) */
#define IRQ_UART        33          /* PL011 UART (SPI) */

/* Returned by gic_acknwlg() when no real IRQ is pending */
#define GIC_SPURIOUS    1023

void gic_init(void);
void gic_inithart(void);
uint32 gic_acknwlg(void);
void gic_eoi(uint32 irq);
void gic_deactivate(uint32 irq);
uint32 gic_nirqs(void);

/*
 * Interrupt priority scheme
 *
 * AV6 runs every normal interrupt at one priority and does no GIC-level
 * nesting. A hardirq runs with PSTATE.I masked, and urgency between devices
 * is a scheduler concern (ithread priority), not a hardware one. This is the
 * high-reliability choice, no reentrant hardirq state, no unbounded nesting.
 * GIC_PRIO_NMI is a reserved higher-priority band (lower value = higher priority)
 * kept for a future pseudo-NMI.
 */
#define GIC_PRIO_NMI            0x80
#define GIC_PRIO_DEFAULT        0xA0

/* Per-IRQ irqchip operations */
void gic_irq_enable(uint32 irq);
void gic_irq_disable(uint32 irq);
void gic_irq_set_group(uint32 irq, uint32 group);
void gic_irq_set_priority(uint32 irq, uint8 prio);
void gic_irq_set_affinity(uint32 irq, uint64 aff);

/* Send SGI to all CPUs except self */
void gic_send_sgi_all(uint32 intid);

#endif      /* _AV6_GIC_H_ */
