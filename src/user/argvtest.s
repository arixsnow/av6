/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
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
