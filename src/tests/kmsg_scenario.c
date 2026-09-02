/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * Scenarios for the panic path
 *
 * These halt the kernel on purpose: make KTEST=1 SCENARIO=<name> check.
 * Each declares what the log must contain, since that is the only verdict
 * available to it.
 */

#include "arch/arm64.h"
#include "sys/kio.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/types.h"
#include "tests/ktest.h"

#define HELPER_WAIT_MS          100

static volatile int helper_up[NCPU_MAX];
static volatile int release;

static int wait_for_helper(int i)
{
    uint64 deadline;

    deadline = read_cntpct() + (read_cntfrq() * HELPER_WAIT_MS) / MSEC_PER_SEC;

    while (read_cntpct() < deadline) {
        if (helper_up[i]) {
            return 1;
        }
    }

    return 0;
}

static int spawn(void (*fn)(void), int i, const char *name)
{
    return kthread_add(fn, (void *)(uintptr)i, name);
}

static void load_thread(void)
{
    char line[128];
    int i, id;

    id = (int)(uintptr)myproc()->isrc;

    for (i = 0; i < (int)sizeof(line) - 1; i++) {
        line[i] = 'a' + (id % 26);
    }

    line[sizeof(line) - 1] = '\0';

    helper_up[id] = 1;

    while (true) {
        printk("load%d %s\n", id, line);
    }
}

static void panic_load(struct ktest *t)
{
    int i, up = 0;

    ktest_expect_output(t, "PANIC (cpu");

    for (i = 0; i < 3; i++) {
        if (spawn(load_thread, i, "ktload") >= 0 && wait_for_helper(i)) {
            up++;
        }
    }

    ktest_note(t, "%d load threads running", up);

    panic("panic under load");
}
KTEST_SCENARIO(panic_load);

static void racer_thread(void)
{
    helper_up[0] = 1;

    while (release == 0) {
        continue;
    }

    panic("concurrent panic from cpu%d", cpuid());
}

static void panic_concurrent(struct ktest *t)
{
    ktest_expect_output(t, "PANIC (cpu");

    if (spawn(racer_thread, 0, "ktrace") < 0 || !wait_for_helper(0)) {
        ktest_note(t, "racer never started, only one cpu will panic");
    }

    release = 1;
    panic("concurrent panic from cpu%d", cpuid());
}
KTEST_SCENARIO(panic_concurrent);

static void wedge_thread(void)
{
    helper_up[0] = 1;
    irq_disable();

    while (true) {
        continue;
    }
}

static void panic_wedge(struct ktest *t)
{
    ktest_expect_output(t, "PANIC: 1 CPUs did not stop");

    if (spawn(wedge_thread, 0, "ktwedge") < 0 || !wait_for_helper(0)) {
        ktest_note(t, "wedge never started, every cpu will stop");
    }

    panic("one CPU is wedged");
}
KTEST_SCENARIO(panic_wedge);
