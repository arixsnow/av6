/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Test suite for interrupt source registration.
 */

#include "arch/gic.h"
#include "sys/intr.h"
#include "sys/types.h"
#include "tests/ktest.h"

static int stub_filter(void)
{
    return FILTER_HANDLED;
}

/* Nothing enables this INTID, so the registration stays inert. */
static void intr_register_above_128(struct ktest *t)
{
    uint32 irq = gic_nirqs() - 1;

    KTEST_ASSERT(t, irq >= 128);
    intr_register(irq, "ktest-high", stub_filter, NULL);
    KTEST_EXPECT_EQ(t, intr_dispatch(irq), FILTER_HANDLED);
}
KTEST_CASE(intr_register_above_128);
