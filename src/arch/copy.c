/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * copy.c - copyin / copyout / copyinstr
 *
 * The bounds check and the raw LDTR/STTR loop live in different
 * places deliberately: the check is written in C where it can be
 * audited at a glance, the loop is written in assembly because
 * LDTR/STTR have no inline intrinsic and because keeping the hot
 * path free of frame setup / register spills matters.
 *
 * Flow for every call:
 *      1. zero-length short-circuit (return 0)
 *      2. user VA bounds + overflow check (return -1 if bad)
 *      3. call the assembly primitive (return 0 ok, -1 on fault)
 */

#include "arch/mmu.h"
#include "sys/types.h"
#include "arch/copy.h"
#include "sys/proc.h"

extern char _copy_fault[];

extern int _copyin_bytes(void *dst_kern, uintptr src_user, uint64 len);
extern int _copyout_bytes(uintptr dst_user, const void *src_kern, uint64 len);
extern int _copyinstr_bytes(char *dst_kern, uintptr src_user, uint64 max);

/*
 * Validate that [user, user + len] lies entirely within the
 * user VA window [0, MAXUVA). Rejects:
 *      : any address at or above MAXUVA (kernel-half VA)
 *      : any range that wraps past MAXUVA (overflow)
 *
 * Caller guarantee len > 0 (zero-length ranges are handled
 * before this is called).
 */
static int user_range_ok(uintptr user, uint64 len)
{
    if (user >= MAXUVA) {
        return 0;
    }

    if (len > MAXUVA - user) {
        return 0;
    }

    return 1;
}

int copyin(void *dst, uintptr user_src, uint64 len)
{
    struct proc *p = myproc();
    int r;

    if (len == 0) {
        return 0;
    }

    if (!user_range_ok(user_src, len)) {
        return -1;
    }

    p->onfault = (uintptr)_copy_fault;
    r = _copyin_bytes(dst, user_src, len);
    p->onfault = (uintptr)NULL;

    return r;
}

int copyout(uintptr user_dst, const void *src, uint64 len)
{
    struct proc *p = myproc();
    int r;

    if (len == 0) {
        return 0;
    }

    if (!user_range_ok(user_dst, len)) {
        return -1;
    }

    p->onfault = (uintptr)_copy_fault;
    r = _copyout_bytes(user_dst, src, len);
    p->onfault = (uintptr)NULL;

    return r;
}

/*
 * copyinstr: copy a NULL terminated string from user space.
 *
 * max must be > 0 : a zero-byte buffer cannot hold even the
 * NULL terminator, so the call is rejected. On success, returns
 * the length of the string excluding the NULL (so a one-character
 * string "x\0" returns 1 and fills dst with "x\0"). On failure,
 * dst is left in an unspecified state and -1 is returned.
 *
 * "Failure" now means: bounds check rejected, or the user's
 * string was longer than max bytes with no NULL in-sight. The
 * assembly does the later check and returns -1 itself.
 */
int copyinstr(char *dst, uintptr user_src, uint64 max)
{
    struct proc *p = myproc();
    int r;

    if (max == 0) {
        return -1;
    }

    if (!user_range_ok(user_src, max)) {
        return -1;
    }

    p->onfault = (uintptr)_copy_fault;
    r = _copyinstr_bytes(dst, user_src, max);
    p->onfault = (uintptr)NULL;

    return r;
}
