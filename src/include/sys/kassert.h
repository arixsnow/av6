/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * kassert.h - invariants
 *
 * KASSERT is for what cannot happen. panic() for what did and cannot be
 * survived. Armed by make DEBUG=1.
 */

#ifndef _AV6_KASSERT_H_
#define _AV6_KASSERT_H_

#include "sys/kio.h"
#include "sys/types.h"

#define CTASSERT(exp, msg)          _Static_assert(exp, msg)

#ifdef AV6_INVARIANTS

#define KASSERT(exp, ...)                                                       \
    do {                                                                        \
        if (__predict_false(!(exp))) {                                      \
            panic(__VA_ARGS__);                                                 \
        }                                                                       \
    } while (false)

#define KASSERT_UNREACHABLE()                                                   \
    panic("unreachable code at %s:%d (%s)", __FILE__, __LINE__, __func__)

#define KWARN(exp)                                                              \
    ({                                                                          \
        int _kw = !!(exp);                                                      \
        if (__predict_false(_kw)) {                                         \
            printk_level(KERN_WARNING, "WARN: %s at %s:%d (%s)\n",              \
                #exp, __FILE__, __LINE__, __func__);                            \
        }                                                                       \
        _kw;                                                                    \
    })

#else

#define KASSERT(exp, ...)           do {} while (false)
#define KASSERT_UNREACHABLE()       __builtin_unreachable()

/* Still evaluates: the caller's error path is not a debug feature. */
#define KWARN(exp)                  (!!(exp))

#endif      /* AV6_INVARIANTS */

#endif      /* _AV6_KASSERT_H_ */
