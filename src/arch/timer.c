/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * timer.c - ARM Generic Timer Driver
 *
 * Every ARM core has a built-in countdown timer. Unlike the GIC (which
 * is a separate peripheral), the timer is part of the core itself.
 *
 * Three registers:
 *      CNTFRQ_EL0 - timer frequency is Hz (read-only, set by firmware)
 *      CNTP_TVAL_EL0 - countdown value (fires IRQ 30 when it hits 0)
 *      CNTP_CTL_EL0 - control: bit 0 = enable, bit 1 = mask output
 */

#include "arch/arm64.h"
#include "arch/dtb.h"
#include "arch/platform.h"
#include "arch/timer.h"
#include "sys/intr.h"
#include "sys/kio.h"
#include "sys/proc.h"
#include "sys/types.h"

#define TIMER_HZ    10      /* 10 ticks per second (100ms interval) */

static uint64 timer_interval;
static uint64 timer_ticks;

void timer_init(void)
{
    uint64 freq;

    /* Read the timer frequency, on QEMU virt which is typically 63.5 MHz */
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));

    if (freq == 0) {
        freq = timer_freq_dtb;
        if (freq == 0) {
            panic("timer: CNTFRQ_EL0 unset and no DTB clock-frequency");
        }
        pr_warn("timer: CNTFRQ_EL0 unset, using DTB clock-frequency\n");
    }

    timer_interval = freq / TIMER_HZ;
    timer_ticks = 0;

    intr_register(platform.timer_irq, "timer", timer_tick, NULL);

    if (cpuid() == 0) {
        printk("timer: freq=%d Hz, interval=%d ticks (%d Hz)\n",
            freq, timer_interval, TIMER_HZ);
    }
}

/* CNTP is banked per CPU, so every hart arms its own */
void timer_inithart(void)
{
    /* Load the countdown */
    asm volatile("msr cntp_tval_el0, %0" :: "r"(timer_interval));

    /* Enable timer, unmask IRQ (bit 0 = enable, bit 1 = 0 (unmask)) */
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(1UL));
}

int timer_tick(void)
{
    /*
     * Clock filter: runs in hardirq context, so it must not block or
     * context-switch.
     *
     * Reload the countdown first. The generic timer interrupt is level
     * sensitive, while CNTP_CTL.ISTATUS is set the PPI stays asserted,
     * so the source must be de-asserted (TVAL reloaded) before trap_irq's
     * EOI deactivates it, or it re-pends immediately.
     */
    asm volatile("msr cntp_tval_el0, %0" :: "r"(timer_interval));

    if (cpuid() == 0)
        timer_ticks++;

    /*
     * Flag a reschedule rather than yielding here. Yielding mid-hardirq
     * would context-switch before the GIC cycle is closed, so the timer's
     * EOI could land on a different CPU and orphan this CPU's timer PPI.
     * trap_irq consumes the flag after gic_eoi(), at the one safe point.
     */
    cpus[cpuid()].need_resched = 1;

    return FILTER_HANDLED;
}
