/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_TYPES_H_
#define _AV6_TYPES_H_

#include "sys/cdefs.h"

typedef signed char         int8;
typedef unsigned char       uint8;

typedef signed short        int16;
typedef unsigned short      uint16;

typedef signed int          int32;
typedef unsigned int        uint32;

typedef signed long         int64;
typedef unsigned long       uint64;

/*
 * On AArch64 (LP64), pointers are 64-bit, so this is uint64
 */
typedef uint64              uintptr;

/*
 * Page table entry : 64-bit on AArch64
 */
typedef uint64              pte_t;

typedef unsigned char       uchar;
typedef unsigned short      ushort;
typedef unsigned int        uint;

#define true                1
#define false               0
#define NULL                ((void *) 0)

#endif  /* _AV6_TYPES_H_ */
