/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * argvtest.s : exec(2) argv self-test program
 */

 /* Syscall numbers */
 .include "syscall_nr.s"

.section .text
.global _start
.type _start, %function
_start:
    ldr     x0, [sp]
    mov     x8, #SYS_exit
    svc     #0
.Lhang:
    b       .Lhang
