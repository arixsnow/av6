/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_CONSOLE_H_
#define _AV6_CONSOLE_H_

#include "sys/types.h"

void console_init(void);
void console_config(void);
void console_intr(void);
int console_filter(void);

uint64 console_read(char *dst, uint64 n);
void console_write(const char *dst, uint64 n);
void console_flush(void);
void console_flush_upto(uint64 seq);
void console_replay(void);

#endif  /* _AV6_CONSOLE_H_ */
