/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * bitops.h - build single-bit values and contiguous-field masks for
 * register and flag definitions.
 */

#ifndef _AV6_BITOPS_H_
#define _AV6_BITOPS_H_

#include "sys/types.h"

/* AArch64 is LP64. a long is 64 bits */
#define BITS_PER_LONG       64

/* BIT(n): a single bit set, n < 64 */
#define BIT(n)              (1UL << (n))

/*
 * GENMASK(h, l): bits l..h inclusive set, h >= l.
 *      (~0UL << l) : set every bit from l upward
 *      (~0UL >> (BITS_PER_LONG - 1 - h)) : set every bit 0..h
 *      AND : leaves exactly  l..h
 */
#define GENMASK(h, l)       ((~0UL << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#endif      /* _AV6_BITOPS_H_ */
