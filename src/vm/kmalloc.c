/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/mmu.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/types.h"
#include "vm/kalloc.h"
#include "vm/kmalloc.h"
#include "vm/kmem.h"

#define KMALLOC_ALIGN       16
#define KMALLOC_NCLASS      10

/*
 * A 4096-byte slab with a 64-byte header: 192 wastes 1% where
 * 256 wastes 6%, and 96/384 stop a request just over a class
 * rounding up to double. 2048 would hold one object per page
 * the same 50% a whole page wastes, plus a header, a bitmap
 * and a lock, so the slab range stops here.
 */
static const uint16 kmalloc_size[KMALLOC_NCLASS] = {
    16, 32, 64, 96, 128, 192, 256, 384, 512, 1024
};

static const char *const kmalloc_name[KMALLOC_NCLASS] = {
    "kmalloc-16", "kmalloc-32", "kmalloc-64", "kmalloc-96", "kmalloc-128",
    "kmalloc-192", "kmalloc-256", "kmalloc-384", "kmalloc-512", "kmalloc-1024"
};

static struct kmem_cache *kmalloc_cache[KMALLOC_NCLASS];

/* size >> 4 -> class, so the lookup is a load rather than a loop. */
static uint8 kmalloc_idx[(KMALLOC_MAX_SLAB / KMALLOC_ALIGN) + 1];

void kmalloc_init(void)
{
    uint64 sz;
    int i, k;

    k = 0;
    for (sz = KMALLOC_ALIGN; sz <= KMALLOC_MAX_SLAB; sz += KMALLOC_ALIGN) {
        while (kmalloc_size[k] < sz) {
            k++;
        }
        kmalloc_idx[sz / KMALLOC_ALIGN] = (uint8)k;
    }

    for (i = 0; i < KMALLOC_NCLASS; i++) {
        kmalloc_cache[i] = kmem_cache_create(kmalloc_name[i],
            kmalloc_size[i], KMALLOC_ALIGN, NULL);

        if (kmalloc_cache[i] == NULL) {
            panic("kmalloc_init: no cache for %u bytes", kmalloc_size[i]);
        }
    }
}

void *kmalloc(uint64 size)
{
    if (size == 0) {
        return ZERO_SIZE_PTR;
    }

    if (size <= KMALLOC_MAX_SLAB) {
        size = (size + KMALLOC_ALIGN - 1) & ~(uint64)(KMALLOC_ALIGN - 1);
        return kmem_cache_alloc(kmalloc_cache[kmalloc_idx[size / KMALLOC_ALIGN]]);
    }

    if (size <= PGSIZE) {
        return kpage_alloc();
    }

    return NULL;
}

void kfree(void *ptr)
{
    if (ZERO_OR_NULL_PTR(ptr)) {
        return;
    }

    if (kpage_is_slab(ptr)) {
        kmem_free(ptr);
        return;
    }

    kpage_free((char *)ptr);
}
