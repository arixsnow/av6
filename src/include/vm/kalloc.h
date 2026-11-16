/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AV6_KALLOC_H_
#define _AV6_KALLOC_H_

void kinit(void);

/*
 * The page allocator: one page. kmalloc() sits on top for variable-size
 * requests and takes the slab of a whole page from here.
 */

char *kpage_alloc(void);
void kpage_free(const char *vaddr);

/*
 * A page the TLB may still reach waits on a pglist until the caller has
 * invalidated the TLB.
 */
struct page;

struct pglist {
    struct page *head;
};

void kpage_defer(struct pglist *list, const char *vaddr);
void kpage_free_list(struct pglist *list);

void kpage_set_slab(void *va);
int kpage_is_slab(void *va);

#endif /* _AV6_KALLOC_H_ */
