/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * cdefs.h - compiler plumbing
 */

#ifndef _AV6_CDEFS_H_
#define _AV6_CDEFS_H_

#define __unused                __attribute__((__unused__))

/* Hints only: both return the expression unchanged */
#define __predict_true(exp)     __builtin_expect((exp), 1)
#define __predict_false(exp)    __builtin_expect((exp), 0)

#define offsetof(type, member)  __builtin_offsetof(type, member)

/*
 * NELEM(a) : number of elements in the array 'a'.
 *
 * __must_be_array(a) is a compile-time guard, 0 for a real
 * array, a build error for a pointer.
 */
#define __must_be_array(a) \
    (sizeof(struct { int : (-!!__builtin_types_compatible_p(typeof(a), typeof(&(a)[0]))); }))
#define NELEM(a)    (sizeof(a) / sizeof((a)[0]) + __must_be_array(a))

#endif      /* _AV6_CDEFS_H_ */
