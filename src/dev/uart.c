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
 *      main (tree parsed): uart_config() programs the line
 *
 * After uart_remap(), TTBR0 can safely become user page tables!
 */

#include "arch/platform.h"
#include "arch/memlayout.h"
#include "arch/mmio.h"
#include "dev/uart.h"
#include "sys/kio.h"
#include "sys/types.h"

#define UART_BAUD           115200

/* FIFO depth by peripheral revision */
#define UART_FIFO_REV2      16
#define UART_FIFO_REV3      32

/*
 * Runtime UART base address.
 *
 * Starts at PA for early boot (MMU off).
 * uart_remap() switches to KVA after MMU is enabled.
 */
static volatile uintptr uart_base = UART0_BASE_PA;

static uint32 uart_fifo_depth = UART_FIFO_REV2;

#define UART_REG(off)       (uart_base + (off))

void uart_init(void)
{
    mmio_write32(UART_REG(UART_IMSC), 0);
    mmio_write32(UART_REG(UART_ICR), UART_INTR_ALL);
    mmio_write32(UART_REG(UART_CR), UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
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

/*
 * Program the line now that the clock is known.
 *
 * IBRD and FBRD do not reach the divider when written. The write to LCRH
 * latches them, so that order is fixed. Without a clock the firmware's divisor
 * statys: it is the one the log so far has proved works, and a guess replaces a
 * working console with silence.
 */
void uart_config(uint32 uartclk)
{
    uint64 quot;
    uint32 rev, ibrd, i;

    rev = (mmio_read32(UART_REG(UART_PIDR2)) >> 4) & 0xF;
    uart_fifo_depth = (rev >= 3) ? UART_FIFO_REV3 : UART_FIFO_REV2;

    quot = 0;

    if (uartclk != 0) {
        quot = (((uint64)uartclk << 2) + (UART_BAUD >> 1)) / UART_BAUD;
        ibrd = (uint32)(quot >> 6);

        if (ibrd == 0 || ibrd > 0xFFFF) {
            pr_warn("uart: %u Hz cannot reach %d baud, keeping firmware rate\n",
                uartclk, UART_BAUD);
            quot = 0;
        }
    }

    mmio_write32(UART_REG(UART_IMSC), 0);
    mmio_write32(UART_REG(UART_CR), 0);

    if (quot != 0) {
        mmio_write32(UART_REG(UART_IBRD), (uint32)(quot >> 6));
        mmio_write32(UART_REG(UART_FBRD), (uint32)(quot & 0x3F));
    }

    mmio_write32(UART_REG(UART_LCRH), UART_LCRH_WLEN8 | UART_LCRH_FEN);
    mmio_write32(UART_REG(UART_IFLS), UART_IFLS_RX_1_2 | UART_IFLS_TX_1_2);
    mmio_write32(UART_REG(UART_CR), UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);

    /* Anything the firmware left pending would fire the moment the mask opens. */
    mmio_write32(UART_REG(UART_ICR), UART_INTR_ALL);
    mmio_write32(UART_REG(UART_IMSC), UART_IMSC_RXIM | UART_IMSC_RTIM);

    /*
     * RXIM asserts on the crossing, not on the level, so a FIFO already at the
     * trigger leaves it asserted-never. Empty it once.
     */
    for (i = 0; i < (uart_fifo_depth << 1); i++) {
        if (mmio_read32(UART_REG(UART_FR)) & UART_FR_RXFE) {
            break;
        }

        mmio_read32(UART_REG(UART_DR));
    }

    if (quot != 0) {
        pr_info("uart: %d baud 8N1, ibrd=%d fbrd=%d, %d-byte FIFO (rev %d)\n",
            UART_BAUD, (uint32)(quot >> 6), (uint32)(quot & 0x3F),
            uart_fifo_depth, rev);
    } else {
        pr_warn("uart: no usable clock, keeping firmware rate. %d-byte FIFO (rev %d)\n",
            uart_fifo_depth, rev);
    }
}

void uart_putc(char c)
{
    while (mmio_read32(UART_REG(UART_FR)) & UART_FR_TXFF)
        continue;

    mmio_write32(UART_REG(UART_DR), (uint32)c);
}

int uart_getc(void)
{
    if (mmio_read32(UART_REG(UART_FR)) & UART_FR_RXFE) {
        return -1;
    }

    return (int)mmio_read32(UART_REG(UART_DR));
}
