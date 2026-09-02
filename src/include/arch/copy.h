/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_COPY_H_
#define _AV6_COPY_H_

#include "sys/types.h"

/*
 * copy.h - safe access to user memory from the kernel
 *
 * The kernel never dereferences a user pointer with a plain
 * ldr/str. PAN (set in kvminithart) faults any such access.
 * All user memory traffic goes through the routines here,
 * which use LDTR/STTR, the "unprivileged" variants of load/store.
 *
 * LDTR/STTR perform the access with EL0 permission semantics
 * (the page must have AP[1]=1) and bypass PAN (the kernel is
 * explicitly opting into a user access). Translation goes
 * through the active TTBR0_EL1, which is the running process's
 * page table. There is no pagetable argument because the only
 * supported source is the current process: a syscall handler
 * already has myproc()->pagetable loaded.
 *
 * Bounds: every user address range must satisfy
 *      user_addr + len <= MAXUVA (no wrap, no kernel VA)
 * Out-of-range addresses are rejected before the LDTR/STTR
 * is issued, so a buggy syscall can't trip a fault on a
 * pointer it could have screened.
 *
 * Return value: 0 on success, -1 on failure.
 *      -1 means EITHER "bad bounds" (the routine refused to issue
 *      the access) OR a fault taken during the access. Each call
 *      arms proc->onfault = _copy_fault, and the EL1 data-abort
 *      handler (trap.c) redirects there on a fault, so a bad user
 *      pointer returns -1 rather than panicking the kernel.
 */

int copyin(void *dst, uintptr user_src, uint64 len);
int copyout(uintptr user_dst, const void *str, uint64 len);

/*
 * copyinstr - copy a NULL-terminated string from user space
 *
 * Reads at most max bytes from user_src into dst, stopping at
 * first NULL. Always NULL-terminated dst on success.
 *
 * Returns:
 *      length of the string in dst (excluding NULL) on success
 *      -1 on bad bounds or if the string is longer than max
 */
int copyinstr(char *dst, uintptr user_src, uint64 max);

#endif      /* _AV6_COPY_H_ */
