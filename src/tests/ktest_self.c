/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Cases for the framework itself.
 */

#include "sys/types.h"
#include "tests/ktest.h"

static void ktest_expect_continues(struct ktest *t)
{
    struct ktest inner;

    ktest_init(&inner, "inner", 0);
    inner.quite = 1;

    KTEST_EXPECT(&inner, false);
    KTEST_EXPECT(&inner, false);

    KTEST_EXPECT_EQ(t, inner.checks, 2U);
    KTEST_EXPECT_EQ(t, inner.bad, 2U);
}
KTEST_CASE(ktest_expect_continues);

static void assert_then_mark(struct ktest *inner, int *reached)
{
    KTEST_ASSERT(inner, false);
    *reached = 1;
}

static void ktest_assert_aborts(struct ktest *t)
{
    struct ktest inner;
    int reached = 0;

    ktest_init(&inner, "inner", 0);
    inner.quite = 1;

    assert_then_mark(&inner, &reached);

    KTEST_EXPECT_EQ(t, reached, 0);
    KTEST_EXPECT_EQ(t, inner.bad, 1U);
}
KTEST_CASE(ktest_assert_aborts);

static void skip_then_mark(struct ktest *inner, int *reached)
{
    KTEST_SKIP(inner, "scratch");
    *reached = 1;
}

static void ktest_skip_is_not_failure(struct ktest *t)
{
    struct ktest inner;
    int reached = 0;

    ktest_init(&inner, "inner", 0);
    inner.quite = 1;

    skip_then_mark(&inner, &reached);

    KTEST_EXPECT_EQ(t, reached, 0);
    KTEST_EXPECT_EQ(t, inner.bad, 0U);
    KTEST_EXPECT(t, inner.skip != NULL);
}
KTEST_CASE(ktest_skip_is_not_failure);

static int teardown_seen;

static void mark_teardown(struct ktest *t)
{
    (void)t;
    teardown_seen = 1;
}

static void ktest_teardown_runs(struct ktest *t)
{
    KTEST_EXPECT_EQ(t, teardown_seen, 0);
}
KTEST_CASE_FIXTURE(ktest_teardown_runs, NULL, mark_teardown);

static void ktest_teardown_ran(struct ktest *t)
{
    KTEST_EXPECT_EQ(t, teardown_seen, 1);
}
KTEST_CASE(ktest_teardown_ran);
