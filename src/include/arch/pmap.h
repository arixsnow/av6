/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_PMAP_H_
#define _AV6_PMAP_H_

#include "sys/types.h"

/* Kernel page tables (boot) */
uintptr boot_alloc(uint64 npages);
void kvminit(void);
void kvminithart(void);

/* Per-process kernel stacks (kstack window, with a guard page) */
int kstack_slot_alloc(void);
void kstack_slot_free(int slot);
uint64 kstack_alloc(int idx);
void kstack_free(int idx);

/* User page tables */
pte_t *uvmcreate(void);
int mappages(pte_t *pagetable, uintptr va, uintptr pa, uint64 sz, uint64 perm);
int uvmcopy(pte_t *src, pte_t *dst, uint64 sz);
uint64 uvmalloc(pte_t *pagetable, uint64 oldsz, uint64 newsz);
uint64 uvmdealloc(pte_t *pagetable, uint64 oldsz, uint64 newsz);
void uvmfree(pte_t *pagetable);

/* Address space switch */
void switchuvm(uint16 asid, pte_t *pagetable);
void switchuvm_reserved(void);

/* Cache maintenance */
void icache_sync_page(void *page);

#endif  /* _AV6_PMAP_H_ */
