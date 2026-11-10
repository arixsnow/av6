/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AV6_PLATFORM_H_
#define _AV6_PLATFORM_H_

#include <sys/types.h>

struct platform {
    uintptr uart_base; /* PL011 UART MMIO base (PA) */
    uint32 uart_clk;   /* UARTCLK in Hz. 0 if unspecified */
    uint32 uart_irq;   /* PL011 INTID */
    uintptr gicd_base; /* GICv3 distributor base (PA) */
    uintptr gicr_base; /* GICv3 redistributor base (PA) */
    uint32 timer_irq;  /* generic timer INTID */
};

extern struct platform platform;

#endif /* _AV6_PLATFORM_H_ */
