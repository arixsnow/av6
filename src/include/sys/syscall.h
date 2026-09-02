/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_SYSCALL_H_
#define _AV6_SYSCALL_H_

/*
 * System call numbers.
 *
 * av6 is not binary-stable yet: numbers can be reshuffled
 * freely until we commit to an ABI. Current policy is simply
 * "order by implementation data," so new calls land at the
 * next free slot. SYS_putc (old slot 2) has been removed and
 * slot 2 is reused for SYS_write.
 *
 * Handler signatures all take void and return uint64. Arguments
 * are fetched from the trapframe by argint / argaddr / arglong
 * in syscall.c. Return value lands in tf->x[0].
 */
#define SYS_getpid      1
#define SYS_write       2
#define SYS_fork        3
#define SYS_exit        4
#define SYS_wait        5
#define SYS_kill        6
#define SYS_sbrk        7
#define SYS_exec        8
#define SYS_read        9

#ifndef __ASSEMBLER__

void syscall(void);

#endif      /* __ASSEMBLER__ */

#endif      /* _AV6_SYSCALL_H_ */
