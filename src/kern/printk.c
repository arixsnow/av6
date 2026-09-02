/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/arm64.h"
#include "arch/cpu.h"
#include "arch/stdarg.h"
#include "dev/console.h"
#include "sys/fmt.h"
#include "sys/kio.h"
#include "sys/kmsg.h"
#include "sys/param.h"
#include "sys/spinlock.h"

/* vprintk formats here and hands the finished line to kmsg_add(). */
static struct kstage {
    char buf[KMSG_RECORD_MAX];
} stages[NCPU_MAX];

void vprintk(int level, const char *format, va_list arglist)
{
    int cpu_id, len;
    uint64 seq;

    push_off();
    cpu_id = cpuid();

    len = vsnprintf(stages[cpu_id].buf, KMSG_RECORD_MAX, format, arglist);
    if (len > KMSG_RECORD_MAX) {
        len = KMSG_RECORD_MAX;
    }

    seq = kmsg_add(level, stages[cpu_id].buf, (uint16)len);
    pop_off();

    console_flush_upto(seq);
}

void printk_init(void)
{
   kmsg_init();
}

void printk_level(int level, const char *format, ...)
{
    va_list arglist;

    va_start(arglist, format);
    vprintk(level, format, arglist);
    va_end(arglist);
}

void printk(const char * format, ...)
{
    va_list arglist;

    va_start(arglist, format);
    vprintk(KERN_INFO, format, arglist);
    va_end(arglist);
}
