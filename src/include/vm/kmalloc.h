/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * kmalloc.h - variable-size kernel allocation
 *
 * Sits on the slab for small sizes and takes a whole page above them. Requests
 * larger than a page return NULL: the page allocator hands out one page and
 * cannot produce a run
 */
#ifndef _AV6_KMALLOC_H_
#define _AV6_KMALLOC_H_

#include "sys/types.h"

#define ZERO_SIZE_PTR           ((void *)16)
#define ZERO_OR_NULL_PTR(x)     ((uintptr)(x) <= (uintptr)ZERO_SIZE_PTR)
#define KMALLOC_MAX_SLAB        1024

void kmalloc_init(void);
void *kmalloc(uint64 size);
void kfree(void *ptr);

#endif  /* _AV6_KMALLOC_H_ */
