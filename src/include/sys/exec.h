/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_EXEC_H_
#define _AV6_EXEC_H_

#include "sys/types.h"

/*
 * Maximum size of a path string passed to exec() and other path-taking
 * syscalls. Includes the trailing NUL. Lives on the kernel stack of the
 * syscall handler, so keep modest.
 */
#define MAXPATH         128

/*
 * exec() argv limits. The argv strings are copied (packed, NUL-separated)
 * into a single scratch page, never onto kernel stack, so total argument
 * bytes are bounded to one page. MAXARG bounds the number
 * of argv entries (excludes the NULL terminator).
 */
#define MAXARG          32

/*
 * loadelf : parse an ELF blob and populate pagetable with its segments.
 *
 * pagetable : already-allocated user pagetable (from uvmcreate())
 * bytes : kernel pointer to the ELF image
 * size : length of the image in bytes
 * entry_out : on success, set to ELF e_entry (the user PC where execution begins)
 * sz_out : on success, set to PGROUNDUP(highest mapped VA + 1)
 *
 * Returns 0 on success or -1 on validation / load failure. On failure,
 * pagetable may be partially populated. The caller MUST tear it down
 * (uvmfree) before retrying or dropping it.
 */
int loadelf(pte_t *pagetable, const uchar *bytes, uint64 size,
    uintptr *entry_out, uint64 *sz_out);

/*
 * exec : replace the calling process's address space with the ELF
 * identified by path.
 *
 * path : NUL-terminated kernel-side string (the syscall stub copies
 *        it in from user space before calling).
 * uargv : user VA of a NULL-terminated array of user strings pointers
 *         (argv), or 0 for none. Copied from the caller's address space
 *         and laid out on the new user stack (standard SysV: SP -> argc),
 *         argv[], NULL, envp NULL, auxv AT_NULL, strings at the top). The
 *         C runtime reads argc/argv from the stack, not from registers.
 *
 * Returns 0 on success : control will eret into the new program's
 * _start rather thatn returning to user code at the SVC instruction.
 * Returns -1 on failure (no such path, OOM, malformed ELF, bad/too-large
 * argv). On any -1 path the caller's address space is left unchanged.
 */
int exec(const char *path, uintptr uargv);

#endif      /* _AV6_EXEC_H_ */
