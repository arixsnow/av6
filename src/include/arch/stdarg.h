/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_STDARG_H_
#define _AV6_STDARG_H_

/*
 * AArch64 calling convention (AAPCS64)
 * > First 8 integer/pointer args go in registers x0-x7
 * > Remaining args are pushed onto the stack
 */

typedef __builtin_va_list va_list;

#define va_start(ap, last)      __builtin_va_start(ap, last)
#define va_arg(ap, type)        __builtin_va_arg(ap, type)
#define va_end(ap)              __builtin_va_end(ap)

#endif  /* _AV6_STDARG_H_ */
