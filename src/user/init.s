/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * init.s : first user-mode program (pid 1)
 *
 * The kernel sets up the trapframe with elr = _start
 * (from the ELF entry point), sp = top of the mapped
 * user stack, x0..x7 = 0.
 *      x8 = syscall number
 *      x0..x5 = arguments
 *      x0 = return value
 */

/* Syscall numbers */
.include "syscall_nr.s"

.section .text
.global _start
.type _start, %function
_start:
    /* fork() : x0 = childpid in parent, 0 in child */
    mov     x8, #SYS_fork
    svc     #0
    cbz     x0, .Lexec_argv

    /* parent : reap the child, then idle forever */
    mov     x0, #0          /* xstatus = NULL : discard status */
    mov     x8, #SYS_wait
    svc     #0

.if AV6_KTEST
    mov     x8, #SYS_fork
    svc     #0
    cbz     x0, .Lexec_fault

    mov     x0, #0
    mov     x8, #SYS_wait
    svc     #0
.endif
.Lparent_idle:
    b       .Lparent_idle

.Lexec_argv:
    adr     x0, .Lpath_argv
    adr     x1, .Largv
    mov     x8, #SYS_exec
    svc     #0

    mov     x0, #1
    mov     x8, #SYS_exit
    svc     #0

.Lexec_fault:
    adr     x0, .Lpath_fault
    mov     x1, #0
    mov     x8, #SYS_exec
    svc     #0

    mov     x0, #1
    mov     x8, #SYS_exit
    svc     #0
.Ldefensive:
    b       .Ldefensive       /* exit() never returns; defensive only */

.section .rodata
.Lpath_argv:
    .asciz  "/argv_test"
.Lpath_fault:
    .asciz  "/fault_test"
.Larg0:
    .asciz  "a"
.Larg1:
    .asciz  "bb"
.Larg2:
    .asciz  "ccc"
.balign 8
.Largv:
    .quad   .Larg0
    .quad   .Larg1
    .quad   .Larg2
    .quad   0
