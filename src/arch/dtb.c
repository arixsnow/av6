/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
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

#include "arch/dtb.h"
#include "arch/arm64.h"
#include "arch/gic.h"
#include "arch/memlayout.h"
#include "arch/platform.h"
#include "sys/fdt.h"
#include "sys/kio.h"
#include "sys/string.h"
#include "sys/types.h"
#include "vm/physmem.h"

/* First cell of a GIC interrupt specifier */
#define DT_INTR_SPI      0
#define DT_INTR_PPI      1

#define DT_TIMER_PHYS_NS 1

/* Runtime hardware parameters */
int ncpus;
int psci_conduit;
struct cpumask psci_cpus;
uint64 timer_freq_dtb;
struct platform platform;
uint64 cpu_mpidr[NCPU_MAX];

static uint64 boot_mpidr;
static int nbanks;
static int boot_found;
static int cpu_overflow;
static uint32 uart_clk_phandle;

/* A non-zero fourth cell names a PPI partition, which AV6 does not support. */
uint32 dtb_intid(const struct fdt *fdt, const struct fdt_irq *irq)
{
    uint32 n;

    if (!dtb_is_gic(fdt, &irq->ctrl)
        || (irq->ncells != 3 && (irq->ncells != 4 || irq->cells[3] != 0))) {
        return 0;
    }

    n = irq->cells[1];

    if (irq->cells[0] == DT_INTR_SPI && n < GIC_SPI_END - GIC_SPI_BASE) {
        return GIC_SPI_BASE + n;
    }

    if (irq->cells[0] == DT_INTR_PPI && n < GIC_SPI_BASE - GIC_PPI_BASE) {
        return GIC_PPI_BASE + n;
    }

    return 0;
}

int dtb_cpu_mpidr(const struct fdt *fdt, const struct fdt_node *cpu, uint32 ac,
    uint64 *mpidr)
{
    const uint32 *reg;
    uint32 len;

    reg = fdt_getprop(fdt, cpu, "reg", &len);

    return (reg != NULL
        && len == ac * 4
        && fdt_cells(reg, (int)ac, mpidr)
        && (*mpidr & ~MPIDR_AFF_MASK) == 0);
}

static void dtb_parse_cpus(const struct fdt *fdt, const struct fdt_node *cpus)
{
    struct fdt_node cpu;
    uint64 mpidr;
    uint32 ac;
    int ok, k;

    if (!fdt_prop_u32(fdt, cpus, "#address-cells", &ac)
        || (ac != 1 && ac != 2)) {
        panic("dtb: /cpus #address-cells must be 1 or 2");
    }

    for (ok = fdt_first_subnode(fdt, cpus, &cpu); ok;
        ok = fdt_next_subnode(fdt, cpus, &cpu)) {

        if (!fdt_node_is(&cpu, "cpu")) {
            continue;
        }

        if (!dtb_cpu_mpidr(fdt, &cpu, ac, &mpidr)) {
            pr_warn("dtb: %s: reg is not an MPIDR, skipped\n", cpu.name);
            continue;
        }

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
            if (dtb_cpu_uses_psci(fdt, &cpu)) {
                cpumask_set(&psci_cpus, ncpus);
            }

            cpu_mpidr[ncpus++] = mpidr;
        } else {
            cpu_overflow = 1;
        }
    }
}

int dtb_is_memory(const struct fdt *fdt, const struct fdt_node *node)
{
    const char *type;
    uint32 len;

    type = fdt_getprop(fdt, node, "device_type", &len);

    return (type != NULL
        && len > 0
        && type[len - 1] == '\0'
        && strcmp(type, "memory") == 0);
}

/*
 * reg is (base, size) pairs. Several pairs, or several memory nodes, mean
 * several DRAM banks.
 */
static void dtb_parse_memory(const struct fdt *fdt, const struct fdt_node *node)
{
    uint64 base, size;
    uint32 i;

    for (i = 0; fdt_translate_reg(fdt, node, i, &base, &size); i++) {
        if (size > 0) {
            physmem_hardware_region(base, size);
            nbanks++;
        }
    }
}

int dtb_is_psci(const struct fdt *fdt, const struct fdt_node *node)
{
    return (fdt_node_is_compatible(fdt, node, "arm,psci-1.0")
        || fdt_node_is_compatible(fdt, node, "arm,psci-0.2"));
}

int dtb_psci_conduit(const struct fdt *fdt, const struct fdt_node *node)
{
    const char *method;
    uint32 len;

    method = fdt_getprop(fdt, node, "method", &len);

    if (method == NULL || len == 0 || method[len - 1] != '\0') {
        return PSCI_CONDUIT_NONE;
    }

    if (strcmp(method, "hvc") == 0) {
        return PSCI_CONDUIT_HVC;
    }

    if (strcmp(method, "smc") == 0) {
        return PSCI_CONDUIT_SMC;
    }

    return PSCI_CONDUIT_NONE;
}

static void dtb_parse_psci(const struct fdt *fdt, const struct fdt_node *node)
{
    psci_conduit = dtb_psci_conduit(fdt, node);

    if (psci_conduit == PSCI_CONDUIT_NONE) {
        pr_warn("dtb: PSCI method is neither hvc nor smc\n");
    }
}

int dtb_cpu_uses_psci(const struct fdt *fdt, const struct fdt_node *cpu)
{
    const char *method;
    uint32 len;

    method = fdt_getprop(fdt, cpu, "enable-method", &len);

    return (method != NULL
        && len > 0
        && method[len - 1] == '\0'
        && strcmp(method, "psci") == 0);
}

int dtb_is_timer(const struct fdt *fdt, const struct fdt_node *node)
{
    return (fdt_node_is_compatible(fdt, node, "arm,armv8-timer")
        || fdt_node_is_compatible(fdt, node, "arm,armv7-timer"));
}

static void dtb_parse_timer(const struct fdt *fdt, const struct fdt_node *node)
{
    struct fdt_irq irq;
    uint32 v;

    if (fdt_prop_u32(fdt, node, "clock-frequency", &v)) {
        timer_freq_dtb = v;
    }

    if (fdt_irq(fdt, node, DT_TIMER_PHYS_NS, &irq)) {
        platform.timer_irq = dtb_intid(fdt, &irq);
    }
}

static void dtb_parse_uart(const struct fdt *fdt, const struct fdt_node *node)
{
    struct fdt_irq irq;
    uint64 base;
    uint32 v;

    if (fdt_translate_reg(fdt, node, 0, &base, NULL)) {
        platform.uart_base = (uintptr)base;
    }

    if (fdt_prop_u32(fdt, node, "clock-frequency", &v)) {
        platform.uart_clk = v;
    } else if (fdt_prop_u32(fdt, node, "clocks", &v)) {
        uart_clk_phandle = v;
    }

    if (fdt_irq(fdt, node, 0, &irq)) {
        platform.uart_irq = dtb_intid(fdt, &irq);
    }
}

int dtb_is_gic(const struct fdt *fdt, const struct fdt_node *node)
{
    return fdt_node_is_compatible(fdt, node, "arm,gic-v3");
}

static void dtb_parse_gic(const struct fdt *fdt, const struct fdt_node *node)
{
    uint64 base;

    if (fdt_translate_reg(fdt, node, 0, &base, NULL)) {
        platform.gicd_base = (uintptr)base;
    }

    if (fdt_translate_reg(fdt, node, 1, &base, NULL)) {
        platform.gicr_base = (uintptr)base;
    }
}

int dtb_is_console(const struct fdt *fdt, const struct fdt_node *node)
{
    return fdt_node_is_available(fdt, node)
        && fdt_node_is_compatible(fdt, node, "arm,pl011");
}

/* serial0 stands in only when stdout-path names nothing */
static int dtb_console(const struct fdt *fdt, struct fdt_node *node)
{
    struct fdt_node chosen;
    const char *path = NULL;
    uint32 len, n;

    if (fdt_find_path(fdt, "/chosen", (uint32)strlen("/chosen"), &chosen)) {
        path = fdt_getprop(fdt, &chosen, "stdout-path", &len);
    }

    if (path != NULL && len > 0 && path[len - 1] == '\0') {
        for (n = 0; n < len - 1 && path[n] != ':'; n++) {
            continue;
        }

        if (fdt_find_path(fdt, path, n, node)) {
            return dtb_is_console(fdt, node);
        }
    }

    return fdt_find_path(fdt, "serial0", (uint32)strlen("serial0"), node)
        && dtb_is_console(fdt, node);
}

void dtb_init(void *dtb)
{
    struct fdt fdt;
    struct fdt_node node, clk, console;
    int ok;

    fdt_open(&fdt, dtb);

    boot_mpidr = read_mpidr();
    cpu_mpidr[0] = boot_mpidr;
    ncpus = 1;

    for (ok = fdt_first_node(&fdt, &node); ok;
        ok = fdt_next_node(&fdt, &node)) {

        if (node.depth == 1 && fdt_node_is(&node, "cpus")) {
            dtb_parse_cpus(&fdt, &node);
        } else if (!fdt_node_is_available(&fdt, &node)) {
            continue;
        } else if (node.depth == 1 && dtb_is_memory(&fdt, &node)) {
            dtb_parse_memory(&fdt, &node);
        } else if (dtb_is_psci(&fdt, &node)) {
            dtb_parse_psci(&fdt, &node);
        } else if (dtb_is_timer(&fdt, &node)) {
            dtb_parse_timer(&fdt, &node);
        } else if (dtb_is_gic(&fdt, &node)) {
            dtb_parse_gic(&fdt, &node);
        }
    }

    if (!dtb_console(&fdt, &console)) {
        panic("dtb: no usable PL011 named by stdout-path or the serial0 alias");
    }

    dtb_parse_uart(&fdt, &console);

    if (platform.uart_clk == 0
        && uart_clk_phandle != 0
        && fdt_node_by_phandle(&fdt, uart_clk_phandle, &clk)) {
        fdt_prop_u32(&fdt, &clk, "clock-frequency", &platform.uart_clk);
    }

    if (!boot_found) {
        panic("dtb: boot CPU MPIDR not described in device tree");
    }

    if (cpu_overflow) {
        printk("dtb: more than %d CPUs present, using %d\n", NCPU_MAX,
            NCPU_MAX);
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
        psci_conduit == PSCI_CONDUIT_SMC       ? "smc"
            : psci_conduit == PSCI_CONDUIT_HVC ? "hvc"
                                               : "none");

    printk("platform: uart=%p irq=%u clk=%u Hz\n", (void *)platform.uart_base,
        platform.uart_irq, platform.uart_clk);

    printk("platform: gicd=%p gicr=%p timer irq=%u\n",
        (void *)platform.gicd_base, (void *)platform.gicr_base,
        platform.timer_irq);

    if (platform.uart_base != UART0_BASE_PA
        || platform.gicd_base != GICD_BASE_PA
        || platform.gicr_base != GICR_BASE_PA) {
        printk("platform: WARNING parsed bases != QEMU virt constants\n");
    }
}
