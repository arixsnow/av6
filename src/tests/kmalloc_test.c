/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Test suite for the variable kernel allocator
 */

#include "arch/mmu.h"
#include "sys/string.h"
#include "sys/types.h"
#include "tests/ktest.h"
#include "vm/kmalloc.h"

#define NBLOCK          4

/* Either side of every class edge, so a mis-indexed size shows up as overlap. */
static const uint16 edge[] = {
    1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 95, 96, 97,
    127, 128, 129, 191, 192, 193, 255, 256, 257,
    383, 384, 385, 511, 512, 513, 1023, 1024 // Why not 1025 and what is the logic behind edge ?
};

static void *blk[NBLOCK];

/*
 * Fill every block to its full requested length, then re-read. A class too
 * small for the request writes into its neighbour, and the neighbour notices.
 */
static void kmalloc_class_boundaries(struct ktest *t)
{
    uint64 i, j, n;
    int b, bad;

    for (i = 0; i < NELEM(edge); i++) {
        n = edge[i];

        for (b = 0; b < NBLOCK; b++) {
            blk[b] = kmalloc(n);
            KTEST_ASSERT(t, blk[b] != NULL);
            memset(blk[b], 0xA0 + b, n);
        }

        bad = -1;
        for (b = 0; b < NBLOCK && bad < 0; b++) {
            for (j = 0; j < n; j++) {
                if (((uchar *)blk[b])[j] != (uchar)(0xA0 + b)) {
                    bad = b;
                    break;
                }
            }
        }
        KTEST_EXPECT_EQ(t, bad, -1);

        for (b = 0; b < NBLOCK; b++) {
            kfree(blk[b]);
        }
    }
}
KTEST_CASE(kmalloc_class_boundaries);

static void kmalloc_is_aligned(struct ktest *t)
{
    uint64 i;
    void *p;

    for (i = 0; i < NELEM(edge); i++) {
        p = kmalloc(edge[i]);
        KTEST_ASSERT(t, p != NULL);
        KTEST_EXPECT_EQ(t, (uint64)p & 0xF, 0UL);
        kfree(p);
    }
}
KTEST_CASE(kmalloc_is_aligned);

/* 1025..PGSIZE take a whole page; above that there is no run allocator. */
static void kmalloc_page_path(struct ktest *t)
{
    void *p;

    p = kmalloc(KMALLOC_MAX_SLAB + 1);
    KTEST_ASSERT(t, p != NULL);
    memset(p, 0x5a, KMALLOC_MAX_SLAB + 1);
    kfree(p);

    p = kmalloc(PGSIZE);
    KTEST_ASSERT(t, p != NULL);
    memset(p, 0x5a, PGSIZE);
    kfree(p);

    KTEST_EXPECT_EQ(t, kmalloc(PGSIZE + 1), NULL);
}
KTEST_CASE(kmalloc_page_path);

static void kmalloc_zero_size(struct ktest *t)
{
    void *p = kmalloc(0);

    KTEST_EXPECT_NE(t, p, NULL);
    KTEST_EXPECT_EQ(t, ZERO_OR_NULL_PTR(p), 1);

    kfree(p);
    kfree(NULL);
}
KTEST_CASE(kmalloc_zero_size);

/* A kfree that failed to route back would exhaust the class long before this. */
static void kmalloc_free_returns_memory(struct ktest *t)
{
    void *p;
    int i;

    for (i = 0; i < 4096; i++) {
        p = kmalloc(96);
        KTEST_ASSERT(t, p != NULL);
        kfree(p);
    }

    for (i = 0; i < 512; i++) {
        p = kmalloc(PGSIZE);
        KTEST_ASSERT(t, p != NULL);
        kfree(p);
    }
}
KTEST_CASE(kmalloc_free_returns_memory);
