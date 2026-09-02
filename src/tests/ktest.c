/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * ktest.c - the case runner
 *
 * Output is KTRF version 1.
 * KTRF: Kernel Test Report Format
 *
 * Every line is "ktest: <verb> ..." with the verbs ktrf, count, run, pass,
 * fail, skip, bad and done. No verb is a substring of another, so a grep
 * for passes cannot match a failure. run comes out before the case rather
 * than after, so a kernel that dies mid-case still names it, and a count with
 * no matching done is how a truncated run is separated from a clean one.
 *
 * Output goes to the port rather than through printk. The log is the first
 * thing under test here, and a report carried by the subject proves nothing.
 */

#include "arch/arm64.h"
#include "arch/stdarg.h"
#include "dev/console.h"
#include "sys/fmt.h"
#include "sys/param.h"
#include "sys/string.h"
#include "sys/types.h"
#include "tests/ktest.h"

#define KTRF_VERSION        1
#define KTEST_LINE_MAX      256

extern const struct ktest_case *__ktest_start[];
extern const struct ktest_case *__ktest_end[];

static char line[KTEST_LINE_MAX];

static void flush_line(int n)
{
    int width = KTEST_LINE_MAX - 2;

    if (n > width) {
        n = width;
    }

    line[n] = '\n';
    console_write(line, (uint64)n + 1);
}

void ktest_init(struct ktest *t, const char *name, uint32 index)
{
    *t = (struct ktest){ .name = name, .index = index };
}

static void ktest_log(const char *fmt, ...)
{
    va_list ap;
    int n;

    n = snprintf(line, KTEST_LINE_MAX - 1, "ktest: ");

    va_start(ap, fmt);
    n += vsnprintf(line + n, KTEST_LINE_MAX - 1 - n, fmt, ap);
    va_end(ap);

    flush_line(n);
}

int64 ktest_memdiff(const void *a, const void *b, uint64 n)
{
    const uchar *x = a, *y = b;
    uint64 i;

    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return (int64)i;
        }
    }

    return -1;
}

/*
 * A case saying something that is not a failure. Own verb so the host parser
 * skips it, and quite for the same reason ktest_bad has it.
 */
void ktest_note(struct ktest *t, const char *fmt, ...)
{
    va_list ap;
    int n, width = KTEST_LINE_MAX - 2;

    if (t->quite) {
        return;
    }

    n = snprintf(line, KTEST_LINE_MAX - 1, "ktest: note %u ", t->index);
    if (n > width) {
        n = width;
    }

    va_start(ap, fmt);
    n += vsnprintf(line + n, KTEST_LINE_MAX - 1 - n, fmt, ap);
    va_end(ap);

    flush_line(n);
}

/* what the log must contain for this case to have passed. */
void ktest_expect_output(struct ktest *t, const char *text)
{
    if (t->quite) {
        return;
    }

    ktest_log("expect %u %s", t->index, text);
}

void ktest_bad(struct ktest *t, const char *file, int lineno, const char *fmt, ...)
{
    va_list ap;
    int n, width = KTEST_LINE_MAX - 2;

    t->bad++;

    if (t->quite) {
        return;
    }

    n = snprintf(line, KTEST_LINE_MAX - 1, "ktest: bad %u %s:%d ",
        t->index, file, lineno);
    if (n > width) {
        n = width;
    }

    va_start(ap, fmt);
    n += vsnprintf(line + n, KTEST_LINE_MAX - 1 - n, fmt, ap);
    va_end(ap);

    flush_line(n);
}

/* CNTFRQ_EL0 is ticks per second. Multiply first: the divisor truncates. */
static uint64 to_us(uint64 ticks, uint64 freq)
{
    if (freq == 0) {
        return 0;
    }

    return (ticks * USEC_PER_SEC) / freq;
}

#ifdef KTEST_SCENARIO_NAME
#define wanted(c) (((c)->flags & KTEST_NORETURN) &&                             \
                    strcmp((c)->name, KTEST_SCENARIO_NAME) == 0)
#elif defined(KTEST_ONLY)
#define wanted(c) (!((c)->flags & KTEST_NORETURN) &&                            \
                    strncmp((c)->name, KTEST_ONLY, strlen(KTEST_ONLY)) == 0)
#else
#define wanted(c) (!((c)->flags & KTEST_NORETURN))
#endif

void ktest_run_all(void)
{
    const struct ktest_case *const *v = __ktest_start;
    struct ktest t;
    uint64 freq, us, started;
    uint32 total, n, i, pass, fail, skip;

    total = (uint32)(__ktest_end - __ktest_start);
    freq = read_cntfrq();
    pass = 0;
    fail = 0;
    skip = 0;

    n = 0;
    for (i = 0; i < total; i++) {
        if (wanted(v[i])) {
            n++;
        }
    }

    ktest_log("ktrf %u", KTRF_VERSION);
    ktest_log("count %u", n);

    started = read_cntpct();

    for (i = 0; i < total; i++) {
        if (!wanted(v[i])) {
            continue;
        }

        ktest_init(&t, v[i]->name, pass + fail + skip + 1);

        ktest_log("run %u %s%s", t.index, t.name,
            (v[i]->flags & KTEST_NORETURN) ? " noreturn" : "");

        us = read_cntpct();

        if (v[i]->setup != NULL && v[i]->setup(&t) != 0) {
            ktest_bad(&t, __FILE__, __LINE__, "setup failed");
        } else {
            v[i]->run(&t);
        }

        if (v[i]->teardown != NULL) {
            v[i]->teardown(&t);
        }

        us = to_us(read_cntpct() - us, freq);

        if (t.bad != 0) {
            fail++;
            ktest_log("fail %u %s checks=%u bad=%u us=%lu", t.index, t.name,
                t.checks, t.bad, us);
        } else if (t.skip != NULL) {
            skip++;
            ktest_log("skip %u %s %s", t.index, t.name, t.skip);
        } else {
            pass++;
            ktest_log("pass %u %s checks=%u us=%lu", t.index, t.name,
                t.checks, us);
        }
    }

    ktest_log("done count=%u pass=%u fail=%u skip=%u us=%lu", n, pass, fail,
        skip, to_us(read_cntpct() - started, freq));
}
