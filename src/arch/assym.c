/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Check assym.h against the structs it describes
 */

#include "arch/assym.h"
#include "arch/cpu.h"
#include "arch/trapframe.h"
#include "sys/kassert.h"
#include "sys/types.h"

CTASSERT(offsetof(struct trapframe, x) == TF_X0, "TF_X0");
CTASSERT(offsetof(struct trapframe, x[30]) == TF_X30, "TF_X30");
CTASSERT(offsetof(struct trapframe, sp) == TF_SP, "TF_SP");
CTASSERT(offsetof(struct trapframe, elr) == TF_ELR, "TF_ELR");
CTASSERT(offsetof(struct trapframe, spsr) == TF_SPSR, "TF_SPSR");
CTASSERT(offsetof(struct trapframe, tpidr) == TF_TPIDR, "TF_TPIDR");
CTASSERT(sizeof(struct trapframe) == TF_SIZE, "TF_SIZE");
CTASSERT(TF_SIZE % 16 == TF_X0, "trapframe must keep SP 16-byte aligned");

CTASSERT(offsetof(struct context, x19) == CTX_X19, "CTX_X19");
CTASSERT(offsetof(struct context, x20) == CTX_X20, "CTX_X20");
CTASSERT(offsetof(struct context, x21) == CTX_X21, "CTX_X21");
CTASSERT(offsetof(struct context, x22) == CTX_X22, "CTX_X22");
CTASSERT(offsetof(struct context, x23) == CTX_X23, "CTX_X23");
CTASSERT(offsetof(struct context, x24) == CTX_X24, "CTX_X24");
CTASSERT(offsetof(struct context, x25) == CTX_X25, "CTX_X25");
CTASSERT(offsetof(struct context, x26) == CTX_X26, "CTX_X26");
CTASSERT(offsetof(struct context, x27) == CTX_X27, "CTX_X27");
CTASSERT(offsetof(struct context, x28) == CTX_X28, "CTX_X28");
CTASSERT(offsetof(struct context, x29) == CTX_X29, "CTX_X29");
CTASSERT(offsetof(struct context, x30) == CTX_X30, "CTX_X30");
CTASSERT(offsetof(struct context, sp) == CTX_SP, "CTX_SP");
CTASSERT(sizeof(struct context) == CTX_SP + 8, "struct context size");
