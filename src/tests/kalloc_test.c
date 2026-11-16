/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Test suite for the page allocator
 */

#include <sys/types.h>
#include <tests/ktest.h>
#include <vm/kalloc.h>

#define DEFER_PROBES 64

static void kpage_defer_keeps_pages_allocated(struct ktest *t)
{
    static char *got[DEFER_PROBES];
    struct pglist free = {NULL};
    char *a, *b;
    int i;

    a = kpage_alloc();
    b = kpage_alloc();
    KTEST_ASSERT(t, a != NULL && b != NULL);

    kpage_defer(&free, a);
    kpage_defer(&free, b);

    for (i = 0; i < DEFER_PROBES; i++) {
        got[i] = kpage_alloc();
        KTEST_ASSERT(t, got[i] != NULL);
        KTEST_EXPECT(t, got[i] != a && got[i] != b);
    }

    for (i = 0; i < DEFER_PROBES; i++) {
        kpage_free(got[i]);
    }

    kpage_free_list(&free);
    KTEST_EXPECT(t, free.head == NULL);
}
KTEST_CASE(kpage_defer_keeps_pages_allocated);
