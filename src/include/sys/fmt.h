/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * fmt - kernel string formatting
 *
 * Returns the length the result would have had, so a caller can tell a
 * truncated line from a complete one. buf is always NUL terminated when size
 * is non-zero.
 *
 * %d %u %x %o %c %s %p, the l modifier for 64-bit arguments, # for a 0x or 0
 * prefix, a minimum field width and 0 to pad it with zeros.
 */

#ifndef _AV6_FMT_H_
#define _AV6_FMT_H_

#include "arch/stdarg.h"
#include "sys/types.h"

int vsnprintf(char *buf, uint64 size, const char *fmt, va_list ap);
int snprintf(char *buf, uint64 size, const char *fmt, ...);

#endif      /* _AV6_FMT_H_ */
