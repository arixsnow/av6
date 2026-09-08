/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Receive-fault classification
 */

#include "dev/console.h"
#include "dev/uart.h"
#include "sys/types.h"
#include "tests/ktest.h"

static void console_rx_error_reports_the_cause(struct ktest *t)
{
    KTEST_EXPECT_EQ(t, console_rx_error('A'), 0U);
    KTEST_EXPECT_EQ(t, console_rx_error(UART_DR_FE | 'A'), UART_DR_FE);
    KTEST_EXPECT_EQ(t, console_rx_error(UART_DR_PE | 'A'), UART_DR_PE);
    KTEST_EXPECT_EQ(t, console_rx_error(UART_DR_BE | 'A'), UART_DR_BE);
}
KTEST_CASE(console_rx_error_reports_the_cause);

static void console_rx_error_breaks_beat_framing(struct ktest *t)
{
    uint32 dr = UART_DR_BE | UART_DR_FE | UART_DR_PE;

    KTEST_EXPECT_EQ(t, console_rx_error(dr), UART_DR_BE);
    KTEST_EXPECT_EQ(t, console_rx_error(UART_DR_PE | UART_DR_FE), UART_DR_PE);
}
KTEST_CASE(console_rx_error_breaks_beat_framing);

static void console_rx_error_ignores_overrun(struct ktest *t)
{
    KTEST_EXPECT_EQ(t, console_rx_error(UART_DR_OE | 'A'), 0U);
    KTEST_EXPECT_EQ(t, console_rx_error(UART_DR_OE | UART_DR_FE), UART_DR_FE);
}
KTEST_CASE(console_rx_error_ignores_overrun);
