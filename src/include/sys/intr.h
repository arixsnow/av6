/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * intr.h - interrupt source dispatch layer
 *
 * every hardware interrupt services  is described by one source, keyed
 * by its GIC_INTID. The hardirq path (trap_irq -> intr_dispatch) finds
 * the source by INTID and runs its filter.
 *
 * A filter runs in hardirq context, so it must not block or context-switch.
 * It either services the device completely and returns FILTER_HANDLED, or
 * asks for its thread to finish the work with FILTER_SCHEDULE_THREAD.
 */

#ifndef _AV6_INTR_H_
#define _AV6_INTR_H_

#include "sys/types.h"

/* Filter return codes */
#define FILTER_STRAY            0   /* spurious / nothing for this source */
#define FILTER_HANDLED          1   /* fully serviced in hardirq context */
#define FILTER_SCHEDULE_THREAD  2   /* hand off to the source's ithread */

void intr_init(void);
void intr_register(uint32 irq, const char *name, int (*filter)(void),
    void (*handler)(void));
int intr_dispatch(uint32 irq);

#endif  /* _AV6_INTR_H_ */
