/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_UART_H_
#define _AV6_UART_H_

#include "sys/bitops.h"
#include "sys/types.h"

/*
 * PL011 URT on QEMU virt machine
 *
 * Base address and INTID come from the device tree. (struct platform)
 */

/* Register offsets from base */
#define UART_DR             0x00        /* Data Register */
#define UART_RSR            0x04        /* Receive status (any write clears it) */
#define UART_FR             0x18        /* Flag Register: tells us TX/RX FIFO status */
#define UART_IBRD           0x24        /* Integer baud rate divisor */
#define UART_FBRD           0x28        /* Fractional baud rate divisor */
#define UART_LCRH           0x2C        /* Line control register */
#define UART_CR             0x30        /* Control Register: enable/disable UART */
#define UART_IFLS           0x34        /* Interrupt FIFO level select */
#define UART_IMSC           0x38        /* Interrupt Mask Set/Clear */
#define UART_RIS            0x3C        /* Raw interrupt status */
#define UART_MIS            0x40        /* Masked interrupt status */
#define UART_ICR            0x44        /* Interrupt clear register */
#define UART_PIDR2          0xFE8       /* Peripheral ID 2 */

/* Data Register */
#define UART_DR_DATA        0xFF
#define UART_DR_OE          BIT(11)     /* Overrun: an earlier byte was lost */
#define UART_DR_BE          BIT(10)     /* Break */
#define UART_DR_PE          BIT(9)      /* Parity error */
#define UART_DR_FE          BIT(8)      /* Framing error */

/* Flag Register bits */
#define UART_FR_RXFE        BIT(4)      /* Receive FIFO empty */
#define UART_FR_TXFF        BIT(5)      /* Transmit FIFO full */

/* Line Control bits */
#define UART_LCRH_FEN       BIT(4)      /* FIFO enable */
#define UART_LCRH_WLEN8     (3 << 5)    /* 8 data bits */

/* Control Register bits */
#define UART_CR_UARTEN      BIT(0)      /* UART enable */
#define UART_CR_TXE         BIT(8)      /* Transmit enable */
#define UART_CR_RXE         BIT(9)      /* Receive enable */

/* FIFO level select: receive in [5:3], transmit in [2:0]. */
#define UART_IFLS_RX_1_2    (2 << 3)
#define UART_IFLS_TX_1_2    2

/* Interrupt Mask Set/Clear bits */
#define UART_IMSC_RXIM      BIT(4)      /* Receive interrupt */
#define UART_IMSC_RTIM      BIT(6)      /* Receive timeout: below it, and idle */

/* Every interrupt the part can raise */
#define UART_INTR_ALL       0x7FF

void uart_init(void);
void uart_config(uint32 uartclk);
void uart_putc(char c);
int uart_getc(void);
void uart_remap(void);

#endif  /* _AV6_UART_H_ */
