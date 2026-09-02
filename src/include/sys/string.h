/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_STRING_H_
#define _AV6_STRING_H_

#include "sys/types.h"

void * memset(void *dst, uchar c, uint64 n);
void *memcpy(void *dst, const void *src, uint64 n);
void *memmove(void *dst, const void *src, uint64 n);
int memcmp(const void *s1, const void *s2, uint64 n);
uint64 strlen(const char *str);
char *strncpy(char *dst, const char *src, uint64 n);
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, uint64 n);

#endif  /* _AV6_STRING_H_ */
