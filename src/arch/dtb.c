/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * dtb.c
 *
 * Pulls out the handful of facts the kernel needs before it can do anything:
 * CPU count and MPIDRs, DRAM banks, the PSCI conduit, the timer frequency, and
 * the MMIO bases of the console and the interrupt controller.
 *
 * One consumer per node type. The walk and its bounds live in the fdt reader.
 */

#include "arch/arm64.h"
#include "arch/dtb.h"
#include "arch/gic.h"
#include "arch/memlayout.h"
#include "arch/platform.h"
#include "sys/fdt.h"
#include "sys/kio.h"
#include "sys/param.h"
#include "sys/string.h"
#include "sys/types.h"
#include "vm/physmem.h"

/* First cell of a GIC interrupt specifier */
#define DT_INTR_SPI         0
#define DT_INTR_PPI         1

#define DT_TIMER_PHYS_NS    1

/* Runtime hardware parameters */
int ncpus;
int psci_conduit;
uint64 timer_freq_dtb;
struct platform platform;
uint64 cpu_mpidr[NCPU_MAX];

/* Declared by the root node; every reg below is decoded with them. */
static int root_acells = 2;
static int root_scells = 1;

static uint64 boot_mpidr;
static int nbanks;
static int boot_found;
static int cpu_overflow;
static uint32 uart_clk_phandle;

static uint32 dtb_intid(const uint32 *intr)
{
    uint32 type;

    type = fdt32(intr[0]);

    if (type == DT_INTR_SPI) {
        return GIC_SPI_BASE + fdt32(intr[1]);
    }

    if (type == DT_INTR_PPI) {
        return GIC_PPI_BASE + fdt32(intr[1]);
    }

    panic("dtb: interrupt type %u is not SPI or PPI", type);
}

static void dtb_parse_root(const struct fdt *fdt, const struct fdt_node *node)
{
    uint32 v;

    if (fdt_prop_u32(fdt, node, "#address-cells", &v)) {
        root_acells = (int)v;
    }

    if (fdt_prop_u32(fdt, node, "#size-cells", &v)) {
        root_scells = (int)v;
    }
}

static void dtb_parse_cpus(const struct fdt *fdt, const struct fdt_node *cpus)
{
    struct fdt_node cpu;
    const uint32 *reg;
    uint64 mpidr;
    uint32 len;
    int ok, k;

    for (ok = fdt_first_subnode(fdt, cpus, &cpu); ok;
        ok = fdt_next_subnode(fdt, cpus, &cpu)) {

        if (!fdt_node_is(&cpu, "cpu")) {
            continue;
        }

        reg = fdt_getprop(fdt, &cpu, "reg", &len);

        if (reg == NULL || len < 4) {
            continue;
        }

        mpidr = (len == 4) ? fdt32(*reg) : fdt64(reg);

        if (mpidr == boot_mpidr) {
            if (boot_found) {
                panic("dtb: duplicate CPU MPIDR");
            }

            boot_found = 1;
            continue;
        }

        for (k = 1; k < ncpus; k++) {
            if (cpu_mpidr[k] == mpidr) {
                panic("dtb: duplicate CPU MPIDR");
            }
        }

        if (ncpus < NCPU_MAX) {
            cpu_mpidr[ncpus++] = mpidr;
        } else {
            cpu_overflow = 1;
        }
    }
}

/*
 * reg is (base, size) pairs. Several pairs, or several memory nodes, mean
 * several DRAM banks.
 */
static void dtb_parse_memory(const struct fdt *fdt, const struct fdt_node *node)
{
    const uint32 *reg;
    uint64 base, size;
    uint32 len, off, ec;

    reg = fdt_getprop(fdt, node, "reg", &len);

    if (reg == NULL) {
        return;
    }

    ec = (uint32)(root_acells + root_scells);

    for (off = 0; (off + ec) * 4 <= len; off += ec) {
        base = fdt_cells(reg + off, root_acells);
        size = fdt_cells(reg + off + root_acells, root_scells);

        if (size > 0) {
            physmem_hardware_region(base, size);
            nbanks++;
        }
    }
}

static void dtb_parse_psci(const struct fdt *fdt, const struct fdt_node *node)
{
    const char *method;
    uint32 len;

    method = fdt_getprop(fdt, node, "method", &len);

    if ((method == NULL) || (len == 0) || (method[len - 1] != '\0')) {
        return;
    }

    psci_conduit = (strcmp(method, "smc") == 0) ? PSCI_CONDUIT_SMC : PSCI_CONDUIT_HVC;
}

static void dtb_parse_timer(const struct fdt *fdt, const struct fdt_node *node)
{
    const uint32 *intr;
    uint32 v, len;

    if (fdt_prop_u32(fdt, node, "clock-frequency", &v)) {
        timer_freq_dtb = v;
    }

    intr = fdt_getprop(fdt, node, "interrupts", &len);

    if (intr != NULL && len >= (DT_TIMER_PHYS_NS + 1) * 12) {
        platform.timer_irq = dtb_intid(intr + DT_TIMER_PHYS_NS * 3);
    }
}

static void dtb_parse_uart(const struct fdt *fdt, const struct fdt_node *node)
{
    const uint32 *reg, *intr;
    uint32 len, v;

    reg = fdt_getprop(fdt, node, "reg", &len);

    if (reg != NULL && len >= 16) {
        platform.uart_base = fdt64(reg);
    }

    if (fdt_prop_u32(fdt, node, "clock-frequency", &v)) {
        platform.uart_clk = v;
    } else if (fdt_prop_u32(fdt, node, "clocks", &v)) {
        uart_clk_phandle = v;
    }

    intr = fdt_getprop(fdt, node, "interrupts", &len);

    if (intr != NULL && len >= 12) {
        platform.uart_irq = dtb_intid(intr);
    }
}

static void dtb_parse_gic(const struct fdt *fdt, const struct fdt_node *node)
{
    const uint32 *reg;
    uint32 len;

    reg = fdt_getprop(fdt, node, "reg", &len);

    if (reg != NULL && len >= 32) {
        platform.gicd_base = fdt64(reg);
        platform.gicr_base = fdt64(reg + 4);
    }
}

void dtb_init(void *dtb)
{
    struct fdt fdt;
    struct fdt_node node, clk;
    int ok;

    fdt_open(&fdt, dtb);

    boot_mpidr = read_mpidr();
    cpu_mpidr[0] = boot_mpidr;
    ncpus = 1;

    for (ok = fdt_first_node(&fdt, &node); ok;
        ok = fdt_next_node(&fdt, &node)) {

        if (node.depth == 0) {
            dtb_parse_root(&fdt, &node);
        } else if (node.depth != 1) {
            continue;
        } else if (fdt_node_is(&node, "cpus")) {
            dtb_parse_cpus(&fdt, &node);
        } else if (fdt_node_is(&node, "memory")) {
            dtb_parse_memory(&fdt, &node);
        } else if (fdt_node_is(&node, "psci")) {
            dtb_parse_psci(&fdt, &node);
        } else if (fdt_node_is(&node, "timer")) {
            dtb_parse_timer(&fdt, &node);
        } else if (fdt_node_is(&node, "pl011")) {
            dtb_parse_uart(&fdt, &node);
        } else if (fdt_node_is(&node, "intc")) {
            dtb_parse_gic(&fdt, &node);
        }
    }

    if (platform.uart_clk == 0 && uart_clk_phandle != 0
        && fdt_node_by_phandle(&fdt, uart_clk_phandle, &clk)) {
        fdt_prop_u32(&fdt, &clk, "clock-frequency", &platform.uart_clk);
    }

    if (!boot_found) {
        panic("dtb: boot CPU MPIDR not described in device tree");
    }

    if (cpu_overflow) {
        printk("dtb: more than %d CPUs present, using %d\n",
            NCPU_MAX, NCPU_MAX);
    }

    if (nbanks == 0) {
        panic("dtb: no usable /memory node in device tree");
    }

    if (platform.uart_irq == 0) {
        panic("dtb: uart interrupt not described in device tree");
    }

    if (platform.timer_irq == 0) {
        panic("dtb: timer interrupt not described in device tree");
    }

    printk("dtb: %d CPUs\n", ncpus);
    physmem_print();

    printk("dtb: PSCI conduit = %s\n",
        psci_conduit == PSCI_CONDUIT_SMC ? "smc" : "hvc");

    printk("platform: uart=%p irq=%u clk=%u Hz\n",
        (void *)platform.uart_base, platform.uart_irq, platform.uart_clk);

    printk("platform: gicd=%p gicr=%p timer irq=%u\n",
        (void *)platform.gicd_base, (void *)platform.gicr_base,
        platform.timer_irq);

    if (platform.uart_base != UART0_BASE_PA
        || platform.gicd_base != GICD_BASE_PA
        || platform.gicr_base != GICR_BASE_PA) {
        printk("platform: WARNING parsed bases != QEMU virt constants\n");
    }
}
