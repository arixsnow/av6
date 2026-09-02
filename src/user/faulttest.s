/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * faulttest.s : copyin fault-recovery self-test
 *
 * Proves the bad-user-pointer
 *  1. write() a known string
 *  2. write() from an in-range but UNMAPPED user VA
 */

.include "syscall_nr.s"

.section .text
.global _start
.type _start, %function
_start:
    # write(1, msg_start, len) : copyin must succeed
    mov     x0, #1
    adr     x1, .Lmsg_start
    mov     x2, #(.Lmsg_start_end - .Lmsg_start)
    mov     x8, #SYS_write
    svc     #0

    # fault path
    mov     x0, #1
    movz    x1, #0x4000, lsl #32
    mov     x2, #16
    mov     x8, #SYS_write
    svc     #0

    # x0 must be -1 (EFAULT).
    cmn     x0, #1
    b.ne    .Lfail

.Lpass:
    mov     x0, #1
    adr     x1, .Lmsg_ok
    mov     x2, #(.Lmsg_ok_end - .Lmsg_ok)
    mov     x8, #SYS_write
    svc     #0
    mov     x0, #0
    mov     x8, #SYS_exit
    svc     #0

.Lfail:
    mov     x0, #1
    adr     x1, .Lmsg_fail
    mov     x2, #(.Lmsg_fail_end - .Lmsg_fail)
    mov     x8, #SYS_write
    svc     #0
    mov     x0, #1
    mov     x8, #SYS_exit
    svc     #0

.Lhang:
    b       .Lhang

.section .rodata
.Lmsg_start:
    .ascii  "fault_test: start\n"
.Lmsg_start_end:
.Lmsg_ok:
    .ascii  "fault_test: copyin EFAULT OK\n"
.Lmsg_ok_end:
.Lmsg_fail:
    .ascii  "fault_test: FAIL (no EFAULT)\n"
.Lmsg_fail_end:
