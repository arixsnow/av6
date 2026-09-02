/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "sys/string.h"

void * memset(void *dst, uchar c, uint64 n)
{
  for (uint64 i = 0; i < n; i++)
    ((uchar *)dst)[i] = c;

  return dst;
}

/*
 * memcpy - non-overlapping copy
 *
 * Word-at-a-time: align destination to 8 bytes, then
 * copy in uint64 chunks. The compiler generates LDR/STR
 * or LDP/STP pairs. Future maybe NEON !
 *
 * AArch64 handles unaligned source reads in hardware for
 * Normal memory, so only destination alignment matters.
 */
void *memcpy(void *dst, const void *src, uint64 n)
{
    uchar *d = dst;
    const uchar *s = src;
    uint64 *wd;
    const uint64 *ws;

    while (n > 0 && ((uintptr)d & 7)) {
        *d++ = *s++;
        n--;
    }

    wd = (uint64 *)d;
    ws = (const uint64 *)s;
    while (n >= 8) {
        *wd++ = *ws++;
        n -= 8;
    }

    /* Trailing bytes */
    d = (uchar *)wd;
    s = (const uchar *)ws;
    while (n-- > 0) {
        *d++ = *s++;
    }

    return dst;
}

/*
 * memmove - overlapping-safe copy
 *
 * If dst < src : forward copy (same as memcpy)
 * If dst > src : backward copy to avoid overwriting
 * source data before it's read.
 *
 * Both directions use word-at-a-time optimization.
 */
void *memmove(void *dst, const void *src, uint64 n)
{
    uchar *d = dst;
    const uchar *s = src;
    uint64 *wd;
    const uint64 *ws;

    if (d == s) {
        return dst;
    }

    if (d < s) {
        while (n > 0 && ((uintptr)d & 7)) {
            *d++ = *s++;
            n--;
        }

        wd = (uint64 *)d;
        ws = (const uint64 *)s;
        while (n >= 8) {
            *wd++ = *ws++;
            n -= 8;
        }

        d = (uchar *)wd;
        s = (const uchar *)ws;
        while (n-- > 0) {
            *d++ = *s++;
        }
     } else {
         d += n;
         s += n;

         while (n > 0 && ((uintptr)d & 7)) {
             *--d = *--s;
             n--;
         }

         wd = (uint64 *)d;
         ws = (const uint64 *)s;
         while (n >= 8) {
             *--wd = *--ws;
             n -= 8;
         }

         d = (uchar *)wd;
         s = (const uchar *)ws;
         while (n-- > 0) {
             *--d = *--s;
         }
     }

    return dst;
}

int memcmp(const void *s1, const void *s2, uint64 n)
{
    const uchar *a = s1, *b = s2;

    for (; 0 < n; n--, a++, b++) {
        if (*a != *b) {
            return (*a < *b) ? -1 : 1;
        }
    }

    return 0;
}

uint64 strlen(const char *str)
{
  uint len;

  len = 0;

  while (str[len] != '\0')
    len++;

  return len;
}

char *strncpy(char *dst, const char *src, uint64 n)
{
    char *d = dst;

    while (n > 0 && *src != '\0') {
        *d++ = *src++;
        n--;
    }

    while (n > 0) {
        *d++ = '\0';
        n--;
    }

    return dst;
}

int strcmp(const char *str1, const char *str2)
{
    for (; *str1 == *str2; str1++, str2++) {
        if (*str1 == '\0') {
            return 0;
        }
    }

    return (*(const uchar *) str1 < *(const uchar *) str2) ? -1 : 1;
}

int strncmp(const char *str1, const char *str2, uint64 n)
{
  for (; 0 < n; n--, str1++, str2++)
  {
    if (*str1 != *str2)
      return (*(const uchar *) str1 < *(const uchar *) str2) ? -1 : 1;
    else if (*str1 == '\0')
      break;
  }

  return 0;
}
