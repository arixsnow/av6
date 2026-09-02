/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Test suite for the kernel message ring.
 */

#include "dev/console.h"
#include "sys/kio.h"
#include "sys/kmsg.h"
#include "sys/types.h"
#include "tests/ktest.h"

static void kmsg_seq_contiguous(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 base;
    int i;

    base = kmsg_next_seq();

    for (i = 0; i < 8; i++) {
        kmsg_add(KERN_INFO, "seq\n", 4);
    }

    KTEST_ASSERT_EQ(t, kmsg_next_seq(), base + 8);

    for (i = 0; i < 8; i++) {
        KTEST_ASSERT_EQ(t, kmsg_read(base + i, &d, buf), 4);
        KTEST_EXPECT_EQ(t, d.seq, base + i);
    }
}
KTEST_CASE(kmsg_seq_contiguous);

static void kmsg_cont_appends(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 base;

    base = kmsg_next_seq();

    kmsg_add(KERN_WARNING, "abc", 3);
    kmsg_add(KERN_CONT, "def\n", 4);

    KTEST_ASSERT_EQ(t, kmsg_next_seq(), base + 1);
    KTEST_ASSERT_EQ(t, kmsg_read(base, &d, buf), 7);

    KTEST_EXPECT_EQ(t, d.level, KERN_WARNING);
    KTEST_EXPECT_MEM_EQ(t, buf, "abcdef\n", 7);
}
KTEST_CASE(kmsg_cont_appends);

static void kmsg_open_is_hidden(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 base;

    base = kmsg_next_seq();

    kmsg_add(KERN_INFO, "open", 4);
    KTEST_EXPECT_LT(t, kmsg_read(base, &d, buf), 0);

    kmsg_finalize();

    KTEST_ASSERT_EQ(t, kmsg_read(base, &d, buf), 4);
    KTEST_EXPECT_MEM_EQ(t, buf, "open", 4);
}
KTEST_CASE(kmsg_open_is_hidden);

static void kmsg_next_record_closes(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 base;

    base = kmsg_next_seq();

    kmsg_add(KERN_INFO, "first", 5);
    KTEST_EXPECT_LT(t, kmsg_read(base, &d, buf), 0);

    kmsg_add(KERN_INFO, "second\n", 7);

    KTEST_ASSERT_EQ(t, kmsg_read(base, &d, buf), 5);
    KTEST_EXPECT_MEM_EQ(t, buf, "first", 5);
}
KTEST_CASE(kmsg_next_record_closes);

static void kmsg_record_cap(struct ktest *t)
{
    struct kmsg_desc d;
    char big[KMSG_RECORD_MAX + 64];
    char buf[KMSG_RECORD_MAX];
    uint64 base;
    int i;

    for (i = 0; i < (int)sizeof(big); i++) {
        big[i] = 'x';
    }

    base = kmsg_next_seq();
    kmsg_add(KERN_INFO, big, sizeof(big));
    kmsg_add(KERN_INFO, "cap\n", 4);

    KTEST_ASSERT_EQ(t, kmsg_read(base, &d, buf), KMSG_RECORD_MAX);
    KTEST_EXPECT_EQ(t, d.len, KMSG_RECORD_MAX);
}
KTEST_CASE(kmsg_record_cap);

static void kmsg_read_bounds(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 first, next;

    kmsg_add(KERN_INFO, "bounds\n", 7);

    first = kmsg_first_seq();
    next = kmsg_next_seq();

    KTEST_EXPECT_LT(t, kmsg_read(next, &d, buf), 0);
    KTEST_EXPECT_LT(t, kmsg_read(next + 100, &d, buf), 0);
    KTEST_EXPECT_GT(t, kmsg_read(first, &d, buf), 0);
    KTEST_EXPECT_GT(t, kmsg_read(next - 1, &d, buf), 0);
}
KTEST_CASE(kmsg_read_bounds);

static void kmsg_wrap(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    char rec[8];
    uint64 first, next, s;
    int i;

    for (i = 0; i < KMSG_NDESC + 200; i++) {
        rec[0] = '#';
        rec[1] = '0' + (i / 1000) % 10;
        rec[2] = '0' + (i / 100) % 10;
        rec[3] = '0' + (i / 10) % 10;
        rec[4] = '0' + i % 10;
        rec[5] = '\n';
        kmsg_add(KERN_INFO, rec, 6);
    }

    first = kmsg_first_seq();
    next = kmsg_next_seq();

    KTEST_ASSERT(t, next - first <= KMSG_NDESC);
    KTEST_ASSERT(t, next > first);

    for (s = first; s < next; s++) {
        t->checks++;
        if (kmsg_read(s, &d, buf) != 6) {
            ktest_bad(t, __FILE__, __LINE__, "seq %lu short or missing", s);
            break;
        }

        t->checks++;
        if (d.seq != s) {
            ktest_bad(t, __FILE__, __LINE__, "seq %lu read back as %lu", s, d.seq);
            break;
        }
    }

    console_flush();
}
KTEST_CASE(kmsg_wrap);

static void kmsg_cont_needs_same_caller(struct ktest *t)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 base;

    base = kmsg_next_seq();

    kmsg_add(KERN_INFO, "owner", 5);
    kmsg_add(KERN_CONT, " more\n", 6);

    KTEST_ASSERT_EQ(t, kmsg_next_seq(), base + 1);
    KTEST_EXPECT_EQ(t, kmsg_read(base, &d, buf), 11);
}
KTEST_CASE(kmsg_cont_needs_same_caller);
