/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Test suite for the invariant macros.
 */

#include "sys/kassert.h"
#include "sys/kmsg.h"
#include "sys/string.h"
#include "sys/types.h"
#include "tests/ktest.h"

static void kwarn_returns_condition(struct ktest *t)
{
    int taken;

    KTEST_EXPECT_EQ(t, KWARN(1), 1);
    KTEST_EXPECT_EQ(t, KWARN(0), 0);

    KTEST_EXPECT_EQ(t, KWARN(7), 1);

    taken = 0;
    if (KWARN(1 == 1)) {
        taken = 1;
    }
    KTEST_EXPECT_EQ(t, taken, 1);

    taken = 0;
    if (KWARN(1 == 0)) {
        taken = 1;
    }
    KTEST_EXPECT_EQ(t, taken , 0);
}
KTEST_CASE(kwarn_returns_condition);

static void kwarn_reports_when_armed(struct ktest *t)
{
#ifndef AV6_INVARIANTS
    KTEST_SKIP(t, "needs DEBUG=1");
#else
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 base;

    base = kmsg_next_seq();
    KWARN(1);
    KTEST_ASSERT_EQ(t, kmsg_next_seq(), base + 1);
    KTEST_ASSERT_GT(t, kmsg_read(base, &d, buf), 0);
    KTEST_EXPECT_EQ(t, strncmp(buf, "WARN: ", 6), 0);

    base = kmsg_next_seq();
    KWARN(0);
    KTEST_EXPECT_EQ(t, kmsg_next_seq(), base);
#endif
}
KTEST_CASE(kwarn_reports_when_armed);

static void kassert_is_gated(struct ktest *t)
{
#ifdef AV6_INVARIANTS
    KTEST_SKIP(t, "a live KASSERT would panic");
#else
    KASSERT(0, "must not fire");
    KTEST_EXPECT_EQ(t, 1, 1);
#endif
}
KTEST_CASE(kassert_is_gated);
