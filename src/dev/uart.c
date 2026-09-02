/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * uart.c - PL011 UART hardware driver
 *
 * Raw register access only. No buffering, no line editing.
 *
 * Two phases:
 *      boot_init (MMU off): uart_base = PA, accesses go through flat memory
 *      main (MMU on): uart_remap() switches to KVA via TTBR1 DMAP
 *
 * After uart_remap(), TTBR0 can safely become user page tables!
 */

#include "sys/types.h"
#include "arch/mmio.h"
#include "dev/uart.h"
#include "arch/memlayout.h"
#include "arch/platform.h"

/*
 * Runtime UART base address.
 *
 * Starts at PA for early boot (MMU off).
 * uart_remap() switches to KVA after MMU is enabled.
 */
static volatile uintptr uart_base = UART0_BASE_PA;

#define UART_REG(off)       (uart_base + (off))

void uart_init(void)
{
    /*
     * QEMU's PL011 is already initialized and ready.
     * On real hardware: baud rate, word length, FIFO etc.
     * Here we just enable: UART, TX, RX
     */
    mmio_write32(UART_REG(UART_CR), 0);
    mmio_write32(UART_REG(UART_CR), UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);

    /*
     * Enable receive interrupt.
     * IMSC bit 4 = RXIM: fires IRQ when a character arrives.
     */
    mmio_write32(UART_REG(UART_IMSC), UART_IMSC_RXIM);
}

/*
 * Switch UART access from PA to KVA (DMAP).
 *
 * Must be called from main() after MMU is on, before
 * TTBR0 is replaced with user page tables.
 */
void uart_remap(void)
{
    uart_base = PA_TO_KVA(platform.uart_base);
}

void uart_putc(char c)
{
    while (mmio_read32(UART_REG(UART_FR)) & UART_FR_TXFF)
        continue;

    mmio_write32(UART_REG(UART_DR), (uint32)c);
}

int uart_getc(void)
{
    if (mmio_read32(UART_REG(UART_FR)) & UART_FR_RXFE)
        return -1;

    return mmio_read32(UART_REG(UART_DR)) & 0xFF;
}
