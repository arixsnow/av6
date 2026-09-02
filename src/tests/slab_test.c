/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Test suite for the slab allocator
 */

#include "sys/string.h"
#include "sys/types.h"
#include "tests/ktest.h"
#include "vm/kmem.h"

static void kmem_cache_reclaim_cycle(struct ktest *t)
{
    static void *objs[256];
    struct kmem_cache *tc;
    int i, n;

    n = 256;

    tc = kmem_cache_create("kmem_test", 64, 0, NULL);
    KTEST_ASSERT(t, tc != NULL);

    for (i = 0; i < n; i++) {
        objs[i] = kmem_cache_alloc(tc);
        KTEST_ASSERT(t, objs[i] != NULL);
        memset(objs[i], 0xab, 64);
    }

    for (i = 0; i < n; i++) {
        kmem_cache_free(tc, objs[i]);
    }

    kmem_reclaim();

    for (i = 0; i < n; i++) {
        objs[i] = kmem_cache_alloc(tc);
        KTEST_ASSERT(t, objs[i] != NULL);
    }

    for (i = 0; i < n; i++) {
        kmem_cache_free(tc, objs[i]);
    }

    kmem_cache_destroy(tc);
}
KTEST_CASE(kmem_cache_reclaim_cycle);
