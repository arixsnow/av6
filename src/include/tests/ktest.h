/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * ktest.h - Kernel Test System : in-kernel test cases
 *
 * A case is a plain function. KTEST_CASE() files a pointer to it in the .ktest
 * linker section, so a test file registers itself and no central llist has to be
 * edited. Cases run in link order. A case whose result depends on what ran
 * before it is a coupled test, not an ordering problem.
 *
 * EXPECT records a failure and carries on. ASSERT records and abandons the case,
 * use it only where continuing would fault, since one run should report every
 * broken expectation rather than the first.
 *
 * teardown always runs: after a failed setup, and after an ASSERT abandons the case.
 * That is why it belongs to the runner and not to the case, and why ASSERT can
 * be a plain return.
 */

#ifndef _AV6_KTEST_H_
#define _AV6_KTEST_H_

#include "sys/string.h"
#include "sys/types.h"

/* Case halts the kernel, so no result comes back. Selected by name, run one per boot */
#define KTEST_NORETURN          0x01

struct ktest {
    const char *name;
    uint32 index;               /* 1-based, ties bad lines to run and pass */
    uint32 checks;              /* expectations evaluated */
    uint32 bad;                 /* or those, failed */
    const char *skip;           /* reason, or NULL */
    int quite;                  /* record failures, do not print them */
    void *ctx;                  /* setup leaves state here, teardown frees it */
};

struct ktest_case {
    const char *name;
    int (*setup)(struct ktest *t);      /* NULL, or non-zero to fail the case */
    void (*run)(struct ktest *t);
    void (*teardown)(struct ktest *t);  /* NULL, or runs however run() ended */
    uint32 flags;
};

void ktest_init(struct ktest *t, const char *name, uint32 index);
int64 ktest_memdiff(const void *a, const void *b, uint64 n);
void ktest_bad(struct ktest *t, const char *file, int lineno, const char *fmt, ...);
void ktest_note(struct ktest *t, const char *fmt, ...);
void ktest_expect_output(struct ktest *t, const char *text);
void ktest_run_all(void);

#define KTEST_REGISTER(fn, setupfn, downfn, fl)                             \
    static const struct ktest_case __ktest_c_##fn = {                       \
        .name=#fn, .setup=setupfn, .run=fn, .teardown=downfn, .flags=fl     \
    };                                                                      \
    static const struct ktest_case *const __ktest_p_##fn                    \
        __attribute__((used, section(".ktest"))) = &__ktest_c_##fn          \

#define KTEST_CASE(fn)                      KTEST_REGISTER(fn, NULL, NULL, 0)
#define KTEST_CASE_FIXTURE(fn, s, d)        KTEST_REGISTER(fn, s, d, 0)
#define KTEST_SCENARIO(fn)                  KTEST_REGISTER(fn, NULL, NULL, KTEST_NORETURN)

/*
 * Bases. Each takes an abort argument, empty to carry on, return to abandon
 * the case, and the aliases below are the interfaces. Do not call these.
 */
#define KTEST_BASE_CHECK(t, cond, abort, ...)                               \
    do {                                                                    \
        (t)->checks++;                                                      \
        if (!(cond)) {                                                      \
            ktest_bad(t, __FILE__, __LINE__, __VA_ARGS__);                  \
            abort;                                                          \
        }                                                                   \
    } while (false)

#define KTEST_BASE_CMP(t, a, op, b, abort)                                  \
    do {                                                                    \
        typeof(a) _a = (a);                                                 \
        typeof(b) _b = (b);                                                 \
        KTEST_BASE_CHECK((t), _a op _b, abort,                              \
            "%s %s %s, got %ld and %ld", #a, #op, #b, (int64)_a, (int64)_b);\
    } while (false)

#define KTEST_BASE_MEM(t, a, b, n, abort)                                   \
    do {                                                                    \
        int64 _d = ktest_memdiff(a, b, n);                                  \
        KTEST_BASE_CHECK((t), _d < 0, abort,                                \
        "%s != %s at byte %ld, got %#x want %#x", #a, #b, _d,               \
        ((const uchar *)(a))[_d < 0 ? 0 : _d],                              \
        ((const uchar *)(b))[_d < 0 ? 0 : _d]);                             \
    } while (false)

#define KTEST_SKIP(t, why)                                                  \
    do {                                                                    \
        (t)->skip = (why);                                                  \
        return;                                                             \
    } while (false)

#define KTEST_EXPECT(t, c)                  KTEST_BASE_CHECK((t), (c), , "expected %s", #c)
#define KTEST_ASSERT(t, c)                  KTEST_BASE_CHECK((t), (c), return, "expected %s", #c)

#define KTEST_EXPECT_EQ(t, a, b)            KTEST_BASE_CMP((t), a, ==, b, )
#define KTEST_EXPECT_NE(t, a, b)            KTEST_BASE_CMP((t), a, !=, b, )
#define KTEST_EXPECT_LT(t, a, b)            KTEST_BASE_CMP((t), a, <, b, )
#define KTEST_EXPECT_LE(t, a, b)            KTEST_BASE_CMP((t), a, <=, b, )
#define KTEST_EXPECT_GT(t, a, b)            KTEST_BASE_CMP((t), a, >, b, )
#define KTEST_EXPECT_GE(t, a, b)            KTEST_BASE_CMP((t), a, >=, b, )

#define KTEST_ASSERT_EQ(t, a, b)            KTEST_BASE_CMP((t), a, ==, b, return)
#define KTEST_ASSERT_NE(t, a, b)            KTEST_BASE_CMP((t), a, !=, b, return)
#define KTEST_ASSERT_LT(t, a, b)            KTEST_BASE_CMP((t), a, <, b, return)
#define KTEST_ASSERT_LE(t, a, b)            KTEST_BASE_CMP((t), a, <=, b, return)
#define KTEST_ASSERT_GT(t, a, b)            KTEST_BASE_CMP((t), a, >, b, return)
#define KTEST_ASSERT_GE(t, a, b)            KTEST_BASE_CMP((t), a, >=, b, return)

#define KTEST_EXPECT_MEM_EQ(t, a, b, n)     KTEST_BASE_MEM((t), a, b, n, )
#define KTEST_ASSERT_MEM_EQ(t, a, b, n)     KTEST_BASE_MEM((t), a, b, n, return)

#endif  /* _AV6_KTEST_H_ */
