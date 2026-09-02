/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_KALLOC_H_
#define _AV6_KALLOC_H_

void kinit(void);

/*
 * The page allocator: one page. kmalloc() sits on top for variable-size
 * requests and takes the slab of a whole page from here.
 */

char *kpage_alloc(void);
void kpage_free(char *vaddr);

void kpage_set_slab(void *va);
int kpage_is_slab(void *va);

#endif  /* _AV6_KALLOC_H_ */
