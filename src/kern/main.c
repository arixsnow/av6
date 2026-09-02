/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/arm64.h"
#include "arch/asid.h"
#include "dev/console.h"
#include "arch/dtb.h"
#include "arch/gic.h"
#include "sys/intr.h"
#include "vm/kalloc.h"
#include "sys/kio.h"
#include "vm/kmem.h"
#include "arch/smp.h"
#include "sys/proc.h"
#include "arch/timer.h"
#include "dev/uart.h"
#include "arch/pmap.h"
#include "vm/kmalloc.h"

#ifdef AV6_KTEST
#include "tests/ktest.h"
#endif

/* Called from entry.s */
void boot_init(void *dtb);

void boot_init(void *dtb)
{
    console_init();
    printk_init();
    printk("AV6 [AArch64] booting...\n");
    dtb_init(dtb);
    kvminit();
    kvminithart();
}

void main(void)
{
    uart_remap();
    kinit();
    kmem_init();
    kmalloc_init();
    procinit();
    asid_init();
    gic_init();
    intr_init();
    intr_register(IRQ_TIMER, "timer", timer_tick, NULL);
    intr_register(IRQ_UART, "uart", console_filter, console_intr);
    gic_inithart();
    timer_init();
    smp_init();
    userinit();

#ifdef AV6_KTEST
    ktest_run_all();
#endif

    switchuvm_reserved();
    daif_procctx();
    printk("BSP: enabling scheduler...\n");
    scheduler();

    /* Halt until interrupted - timer will wake us every 100ms */
    while (true)
        wfi();
}
