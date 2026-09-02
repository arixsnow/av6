/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * slab.c - slab / object allocator (kmem_cache)
 *
 * Three layers, fast to slow:
 *
 *      per-CPU MAGAZINES a CPU's loaded/previous magazine (a small stack of
 *                        free objects). Alloc pops, free pushes, no lock, just
 *                        push_off() to pin the CPU.
 *      DEPOT             full/empty magazine lists shared by all CPUs, under the
 *                        cache lock. Refills/drains a magazine at a time, so the
 *                        lock is touched once per MAG_DEPTH objects.
 *      SLABS             one page from kpage_alloc(), an in-slab header at the page
 *                        base (cache, inuse, a free bitmap, list link) then the
 *                        object slot. Found from any object by PGROUNDDW().
 *
 * Objects stay CONSTRUCTED (the ctor run once when a slab is grown) across
 * free -> magazine -> realloc, so free must leave an object idle-but-valid.
 *
 * The two internal caches (cache_cache, magazine_cache) run with the magazine
 * layer OFF (use_mag = 0) to break the bootstrap recursion, which is a normal
 * cache draws kmem_cache structs from cache_cache and magazines form magazine_cache,
 * and those two never need a magazine themselves.
 */

#include "arch/arm64.h"
#include "arch/cpu.h"
#include "arch/mmu.h"
#include "sys/bitstring.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/list.h"
#include "sys/param.h"
#include "sys/spinlock.h"
#include "sys/string.h"
#include "sys/types.h"
#include "vm/kalloc.h"
#include "vm/kmem.h"

/* objects per magazine */
#define MAG_DEPTH           15

/* minimum object size and default alignment (16 = MTE granule) */
#define KMEM_MIN_ALIGN      16

/*
 * upper bound on slots in a 1-page slab, sizing the per-slab free bitmap.
 * smallest object is KMEM_MIN_ALIGN bytes, so at most PGSIZE/KMEM_MIN_ALIGN.
 */
#define SLAB_MAX_OBJS       (PGSIZE / KMEM_MIN_ALIGN)

struct kmem_cache;

/* a magazine: a small LIFO stack of free-object pointers */
struct kmem_magazine {
    LIST_ENTRY(kmem_magazine) link;     /* depot full/empty list */
    int count;                          /* valid entries in objs[] */
    void *objs[MAG_DEPTH];
};
LIST_HEAD(kmem_maglist, kmem_magazine);

/* in-slab header: lives at the base of each slab page */
struct slab {
    LIST_ENTRY(slab) link;              /* cache partial/full/empty list */
    struct kmem_cache *cache;           /* owning cache (validation) */
    int inuse;                          /* slots handed out (incl. magazine-cached) */
    bit_decl(used, SLAB_MAX_OBJS);      /* 1 = used, 0 = free, bit_ffc finds a free slot */
};
LIST_HEAD(kmem_slablist, slab);

/* a CPU's private magazine pair (no lock, push_off pins the CPU) */
struct kmem_cpu_cache {
    struct kmem_magazine *loaded;
    struct kmem_magazine *previous;
};

struct kmem_cache {
    struct spinlock lock;               /* protects the slab lists + depot */
    const char *name;
    uint64 objsize;                     /* object size, rounded up to align */
    uint64 align;
    uint32 slots_per_slab;              /* objects in one slab page */
    uint32 obj_offset;                  /* page base -> object slot 0 */
    void (*ctor)(void *);               /* run once per object at slab grow */
    int use_mag;                        /* 0 for the internal caches */

    struct kmem_slablist partial;       /* slabs with some free slots */
    struct kmem_slablist full;          /* slabs with no free slots */
    struct kmem_slablist empty;         /* all-free slabs, reclaimable */

    struct kmem_maglist full_mags;      /* depot: full magazines */
    struct kmem_maglist empty_mags;     /* depot: empty magazines */

    struct kmem_cpu_cache cpu[NCPU_MAX];

    LIST_ENTRY(kmem_cache) link;        /* global cache list (for reclaim) */
};
LIST_HEAD(kmem_cachelist, kmem_cache);

static struct kmem_cache cache_cache;       /* hands out struct kmem_cache */
static struct kmem_cache magazine_cache;    /* hands out struct kmem_magazine */
static struct kmem_cachelist kmem_caches;
static struct spinlock kmem_caches_lock;

/*
 * cache_setup - fill in a cache's layout and init its lists/lock.
 *
 * Rounds the object up to align (>=KMEM_MIN_ALIGN), places the objects after
 * the in-slab header, and computes how many fit in one page. Does not allocate
 * anything. slabs grow lazily on the first alloc.
 */
static void cache_setup(struct kmem_cache *c, const char *name, uint64 size,
    uint64 align, void (*ctor)(void *), int use_mag)
{
    uint64 off;
    int i;

    if (align < KMEM_MIN_ALIGN) {
        align = KMEM_MIN_ALIGN;
    }

    size = (size + align - 1) & ~(align - 1);

    c->name = name;
    c->objsize = size;
    c->align = align;
    c->ctor = ctor;
    c->use_mag = use_mag;

    off = (sizeof(struct slab) + align - 1) & ~(align - 1);
    c->obj_offset = (uint32)off;
    c->slots_per_slab = (uint32)((PGSIZE - off) / size);

    init_spinlock(&c->lock, (char *)name);
    LIST_INIT(&c->partial);
    LIST_INIT(&c->full);
    LIST_INIT(&c->empty);
    LIST_INIT(&c->full_mags);
    LIST_INIT(&c->empty_mags);

    for (i = 0; i < NCPU_MAX; i++) {
        c->cpu[i].loaded = NULL;
        c->cpu[i].previous = NULL;
    }
}

/*
 * kmem_init - bring up the allocator. Called once from main() after kinit().
 * Sets up the two internal caches (magazine layer OFF) and the global list.
 */
void kmem_init(void)
{
    init_spinlock(&kmem_caches_lock, "kmem_caches");
    LIST_INIT(&kmem_caches);

    cache_setup(&cache_cache, "kmem_cache", sizeof(struct kmem_cache),
        0, NULL, 0);

    cache_setup(&magazine_cache, "kmem_magazine", sizeof(struct kmem_magazine),
        0, NULL, 0);

    LIST_INSERT_HEAD(&kmem_caches, &cache_cache, link);
    LIST_INSERT_HEAD(&kmem_caches, &magazine_cache, link);
}

static void *slab_obj(struct kmem_cache *c, struct slab *s, int slot)
{
    return (char *)s + c->obj_offset + (uint64)slot * c->objsize;
}

/* slab_grow - carve a new slab from one page */
static struct slab *slab_grow(struct kmem_cache *c)
{
    struct slab *s;
    char *page;
    uint32 i;

    page = kpage_alloc();
    if (page == NULL) {
        return NULL;
    }
    kpage_set_slab(page);

    s = (struct slab *)page;
    memset(s, 0, sizeof(*s));       /* used[] all clear => all slots free */
    s->cache = c;
    s->inuse = 0;

    if (c->ctor != NULL) {
        for (i = 0; i < c->slots_per_slab; i++) {
            c->ctor(slab_obj(c, s, (int)i));
        }
    }

    return s;
}

/* slab_alloc_locked - hand out one object from the slab layer */
static void *slab_alloc_locked(struct kmem_cache *c)
{
    struct slab *s;
    int slot;

    s = LIST_FIRST(&c->partial);
    if (s == NULL) {
        s = LIST_FIRST(&c->empty);
        if (s != NULL) {
            LIST_REMOVE(s, link);
        } else {
            s = slab_grow(c);
            if (s == NULL) {
                return NULL;
            }
        }
        LIST_INSERT_HEAD(&c->partial, s, link);
    }

    bit_ffc(s->used, (int)c->slots_per_slab, &slot);
    KASSERT(slot >= 0, "slab_alloc_locked: no free slot in a partial slab");
    bit_set(s->used, slot);
    s->inuse++;

    if ((uint32)s->inuse == c->slots_per_slab) {
        LIST_REMOVE(s, link);
        LIST_INSERT_HEAD(&c->full, s, link);
    }

    return slab_obj(c, s, slot);
}

/* slab_free_locked - return one object to the slab layer */
static void slab_free_locked(struct kmem_cache *c, void *obj)
{
    struct slab *s;
    int slot, was_full;

    s = (struct slab *)PGROUNDDW((uintptr)obj);
    KASSERT(s->cache == c, "kmem_cache_free: object does not belong to this cache");

    slot = (int)(((char *)obj - ((char *)s + c->obj_offset)) / c->objsize);
    KASSERT(slot >= 0 && (uint32)slot < c->slots_per_slab && bit_test(s->used, slot),
        "kmem_cache_free: bad address or double free");

    was_full = ((uint32)s->inuse == c->slots_per_slab);
    bit_clear(s->used, slot);
    s->inuse--;

    if (was_full) {
        LIST_REMOVE(s, link);
        LIST_INSERT_HEAD(&c->partial, s, link);
    }
    if (s->inuse == 0) {
        LIST_REMOVE(s, link);
        LIST_INSERT_HEAD(&c->empty, s, link);
    }
}

/*
 * The per-CPU layer keeps two magazines, 'loaded' (the working magazine,
 * and fill level) and 'previous' (a spare kept strictly full or empty).
 * Crossing a magazine boundary swaps them instead of touching the depot, so
 * the cache lock is taken only once per MAG_DEPTH operations.
 */
void *kmem_cache_alloc(struct kmem_cache *cache)
{
    struct kmem_cpu_cache *cc;
    struct kmem_magazine *full, *t;
    void *obj;

    if (!cache->use_mag) {
        acquire_spinlock(&cache->lock);
        obj = slab_alloc_locked(cache);
        release_spinlock(&cache->lock);
        return obj;
    }

    push_off();
    cc = &cache->cpu[cpuid()];

    while (true) {
        if (cc->loaded != NULL && cc->loaded->count > 0) {
            obj = cc->loaded->objs[--cc->loaded->count];
            pop_off();
            return obj;
        }

        if (cc->previous != NULL && cc->previous->count == MAG_DEPTH) {
            t = cc->loaded;
            cc->loaded = cc->previous;
            cc->previous = t;
            continue;
        }

        acquire_spinlock(&cache->lock);
        full = LIST_FIRST(&cache->full_mags);
        if (full != NULL) {
            LIST_REMOVE(full, link);
            if (cc->previous != NULL) {
                LIST_INSERT_HEAD(&cache->empty_mags, cc->previous, link);
            }
            cc->previous = cc->loaded;
            cc->loaded = full;
            release_spinlock(&cache->lock);
            continue;
        }
        obj = slab_alloc_locked(cache);
        release_spinlock(&cache->lock);
        pop_off();
        return obj;
    }
}

void kmem_cache_free(struct kmem_cache *cache, void *obj)
{
    struct kmem_cpu_cache *cc;
    struct kmem_magazine *empty, *t;

    if (!cache->use_mag) {
        acquire_spinlock(&cache->lock);
        slab_free_locked(cache, obj);
        release_spinlock(&cache->lock);
        return;
    }

    push_off();
    cc = &cache->cpu[cpuid()];

    while (true) {
        if (cc->loaded != NULL && cc->loaded->count < MAG_DEPTH) {
            cc->loaded->objs[cc->loaded->count++] = obj;
            pop_off();
            return;
        }

        if (cc->previous != NULL && cc->previous->count == 0) {
            t = cc->loaded;
            cc->loaded = cc->previous;
            cc->previous = t;
            continue;
        }

        acquire_spinlock(&cache->lock);
        empty = LIST_FIRST(&cache->empty_mags);
        if (empty != NULL) {
            LIST_REMOVE(empty, link);
        } else {
            empty = kmem_cache_alloc(&magazine_cache);
            if (empty == NULL) {
                slab_free_locked(cache, obj);
                release_spinlock(&cache->lock);
                pop_off();
                return;
            }
            empty->count = 0;
        }
        if (cc->previous != NULL) {
            LIST_INSERT_HEAD(&cache->full_mags, cc->previous, link);
        }
        cc->previous = cc->loaded;
        cc->loaded = empty;
        release_spinlock(&cache->lock);
        continue;
    }
}

void kmem_free(void *obj)
{
    struct slab *s = (struct slab *)PGROUNDDW((uintptr)obj);

    kmem_cache_free(s->cache, obj);
}

static void drain_magazine(struct kmem_cache *cache, struct kmem_magazine *m)
{
    int i;

    for (i = 0; i < m->count; i++) {
        slab_free_locked(cache, m->objs[i]);
    }
    m->count = 0;
    kmem_cache_free(&magazine_cache, m);
}

struct kmem_cache *kmem_cache_create(const char *name, uint64 size,
    uint64 align, void (*ctor)(void *))
{
    struct kmem_cache *c;

    if (size == 0) {
        return NULL;
    }

    c = kmem_cache_alloc(&cache_cache);
    if (c == NULL) {
        return NULL;
    }

    cache_setup(c, name, size, align, ctor, 1);

    if (c->slots_per_slab == 0) {
        kmem_cache_free(&cache_cache, c);
        return NULL;
    }

    acquire_spinlock(&kmem_caches_lock);
    LIST_INSERT_HEAD(&kmem_caches, c, link);
    release_spinlock(&kmem_caches_lock);

    return c;
}

void kmem_cache_destroy(struct kmem_cache *cache)
{
    struct kmem_magazine *m, *mtmp;
    struct slab *s, *stmp;
    int i;

    acquire_spinlock(&kmem_caches_lock);
    LIST_REMOVE(cache, link);
    release_spinlock(&kmem_caches_lock);

    acquire_spinlock(&cache->lock);

    for (i = 0; i < NCPU_MAX; i++) {
        if (cache->cpu[i].loaded != NULL) {
            drain_magazine(cache, cache->cpu[i].loaded);
            cache->cpu[i].loaded = NULL;
        }
        if (cache->cpu[i].previous != NULL) {
            drain_magazine(cache, cache->cpu[i].previous);
            cache->cpu[i].previous = NULL;
        }
    }

    LIST_FOREACH_SAFE(m, &cache->full_mags, link, mtmp) {
        LIST_REMOVE(m, link);
        drain_magazine(cache, m);
    }

    LIST_FOREACH_SAFE(m, &cache->empty_mags, link, mtmp) {
        LIST_REMOVE(m, link);
        drain_magazine(cache, m);
    }

    KASSERT(LIST_EMPTY(&cache->partial) && LIST_EMPTY(&cache->full),
        "kmem_cache_destroy: cache still has live objects");

    LIST_FOREACH_SAFE(s, &cache->empty, link, stmp) {
        LIST_REMOVE(s, link);
        kpage_free((char *)s);
    }

    release_spinlock(&cache->lock);

    kmem_cache_free(&cache_cache, cache);
}

int kmem_reclaim(void)
{
    struct kmem_cache *c;
    struct kmem_magazine *m, *mtmp;
    struct slab *s, *stmp;
    int freed = 0;

    push_off();

    if (!try_acquire_spinlock(&kmem_caches_lock)) {
        pop_off();
        return 0;                       /* another CPU is reclaiming or editing the list */
    }

    LIST_FOREACH(c, &kmem_caches, link) {
        if (holding_spinlock(&c->lock)) {
            continue;
        }
        if (!try_acquire_spinlock(&c->lock)) {
            continue;
        }

        LIST_FOREACH_SAFE(m, &c->full_mags, link, mtmp) {
            LIST_REMOVE(m, link);
            drain_magazine(c, m);
        }

        LIST_FOREACH_SAFE(m, &c->empty_mags, link, mtmp) {
            LIST_REMOVE(m, link);
            drain_magazine(c, m);
        }

        LIST_FOREACH_SAFE(s, &c->empty, link, stmp) {
            LIST_REMOVE(s, link);
            kpage_free((char *)s);
            freed++;
        }

        release_spinlock(&c->lock);
    }

    release_spinlock(&kmem_caches_lock);
    pop_off();

    return freed;
}
