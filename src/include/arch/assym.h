/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * assym.h - constants shared by C and assembly
 *
 * Root integer defines here become .equ for the .s files. Derived expressions
 * do not. assym.c checks them against the structs.
 */
#ifndef _AV6_ASSYM_H_
#define _AV6_ASSYM_H_

/* struct trapframe */
#define TF_X0                   0
#define TF_X30                  240
#define TF_SP                   248
#define TF_ELR                  256
#define TF_SPSR                 264
#define TF_TPIDR                272
#define TF_SIZE                 288

/* struct context */
#define CTX_X19                 0
#define CTX_X20                 8
#define CTX_X21                 16
#define CTX_X22                 24
#define CTX_X23                 32
#define CTX_X24                 40
#define CTX_X25                 48
#define CTX_X26                 56
#define CTX_X27                 64
#define CTX_X28                 72
#define CTX_X29                 80
#define CTX_X30                 88
#define CTX_SP                  96

#define KSTACK_SHIFT            14

/* Per-CPU emergency stack the overflow handler runs on */
#define OVERFLOW_STACK_SHIFT    12
#define OVERFLOW_STACK_SIZE     (1 << OVERFLOW_STACK_SHIFT)

#endif  /* _AV6_ASSYM_H_ */
