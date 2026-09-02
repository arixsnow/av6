/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_PLATFORM_H_
#define _AV6_PLATFORM_H_

#include "sys/types.h"

struct platform {
    uintptr uart_base;      /* PL011 UART MMIO base (PA) */
    uintptr gicd_base;      /* GICv3 distributor base (PA) */
    uintptr gicr_base;      /* GICv3 redistributor base (PA) */
};

extern struct platform platform;

#endif  /* _AV6_PLATFORM_H_ */
