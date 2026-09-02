/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_UART_H_
#define _AV6_UART_H_

#include "sys/bitops.h"

/*
 * PL011 URT on QEMU virt machine
 *
 * Base address defined in memlayout.h (UART0_BASE / UART_BASE_PA).
 * Register offsets from base:
 */

/* Register offsets from base */
#define UART_DR             0x00        /* Data Register: write a byte to transfer */
#define UART_FR             0x18        /* Flag Register: tells us TX/RX FIFO status */
#define UART_CR             0x30        /* Control Register: enable/disable UART */
#define UART_IMSC           0x38        /* Interrupt Mask Set/Clear */

/* Flag Register bits */
#define UART_FR_RXFE        BIT(4)      /* Receive FIFO empty */
#define UART_FR_TXFF        BIT(5)      /* Transmit FIFO full */

/* Control Register bits */
#define UART_CR_UARTEN      BIT(0)      /* UART enable */
#define UART_CR_TXE         BIT(8)      /* Transmit enable */
#define UART_CR_RXE         BIT(9)      /* Receive enable */

/* Interrupt Mask Set/Clear bits */
#define UART_IMSC_RXIM      BIT(4)      /* Receive interrupt */

void uart_init(void);
void uart_putc(char c);
int uart_getc(void);
void uart_remap(void);

#endif  /* _AV6_UART_H_ */
