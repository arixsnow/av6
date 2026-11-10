/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * fmt - kernel string formatting
 *
 * Returns the length the result would have had, so a caller can tell a
 * truncated line from a complete one. buf is always NUL terminated when size
 * is non-zero.
 *
 * C's integer conversions with every flag, width and precision, and the
 * hh h l ll j z t sizes, plus %c %s %%. %p prints 0x and 16 hex digits. No
 * floating point. %n stores nothing, and an unknown conversion ends
 * formatting: the rest of the format is copied as it is.
 */

#ifndef _AV6_FMT_H_
#define _AV6_FMT_H_

#include <arch/stdarg.h>
#include <sys/types.h>

int vsnprintf(char *buf, uint64 size, const char *fmt, va_list ap);
int snprintf(char *buf, uint64 size, const char *fmt, ...);

#endif /* _AV6_FMT_H_ */
