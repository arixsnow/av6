/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_SPINLOCK_H_
#define _AV6_SPINLOCK_H_

#include "sys/types.h"

struct cpu;

struct spinlock {
    uint locked;
    const char * name;
    struct cpu *cpu;    /* CPU that hold this lock, NULL if free */
};

void push_off_at(const char *file, int line);
void pop_off_at(const char *file __unused, int line __unused);
void acquire_spinlock_at(struct spinlock *lk, const char *file, int line);
int try_acquire_spinlock_at(struct spinlock *lk, const char *file, int line);
void release_spinlock_at(struct spinlock *lk, const char *file, int line);

#define push_off()                      push_off_at(__FILE__, __LINE__)
#define pop_off()                       pop_off_at(__FILE__, __LINE__)
#define acquire_spinlock(lk)            acquire_spinlock_at(lk, __FILE__, __LINE__)
#define try_acquire_spinlock(lk)        try_acquire_spinlock_at(lk, __FILE__, __LINE__)
#define release_spinlock(lk)            release_spinlock_at(lk, __FILE__, __LINE__)

int holding_spinlock(struct spinlock *lk);
void init_spinlock(struct spinlock *lk, const char *name);

#endif  /* _AV6_SPINLOCK_H_ */
