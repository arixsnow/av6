/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * kmem.h - slab / object allocator (kmem_cache)
 *
 * A kmem_cache hands out fixed-size kernel objects, growing lazily from
 * the page allocator (kalloc) instead of a compile-time array. One cache
 * per object type: every object in a cache shares one size and alignment,
 * so allocation is O(1) and fragmentation-free.
 *
 * Layers:
 *      SLAB: one page from kpage_alloc(), an in-slab header at the page base
 *      (cache, inuse, a free-slot bitmap, list links) followed by the
 *      object slots. The header is found from any object by rounding the
 *      pointer down to its page, so there is no lookup table.
 *      per-CPU MAGAZINES + a shared depot: the SMP fast path. Alloc/free
 *      hit this CPU's magazine without a lock. the depot batches the
 *      cache lock to once per magazine.
 */

#ifndef _AV6_KMEM_H_
#define _AV6_KMEM_H_

#include "sys/types.h"

struct kmem_cache;

void kmem_init(void);

/*
 * align: required alignment, a power of two. 0 means the default
 *        (8-byte / pointer alignment). size is rounded up to it.
 * ctor: optional constructor run ONCE when an object is first carved
 *       from a fresh slab (NOT on every alloc). e.g. to init a lock.
 *       The object stays constructed across free -> magazine -> realloc,
 *       so free must leave it in its constructed-but-idle state. NULL
 *       for none.
 */
struct kmem_cache *kmem_cache_create(const char *name, uint64 size,
    uint64 align, void (*ctor)(void *));
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);
void kmem_free(void *obj);
void kmem_cache_destroy(struct kmem_cache *cache);
int kmem_reclaim(void);

#endif  /* _AV6_KMEM_H_ */
