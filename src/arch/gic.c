/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * gic.c - GICv3 driver for QEMU virt
 *
 * Three components to initialize:
 *      1. ICC (CPU interface) - system registers, fastest path
 *      2. GICR (Redistributor) - per-CPU, manages PPIs/SGIs
 *      3. GICD (Distributor) - global, routes SPIs
 *
 * Two init paths:
 *      gic_init()      - BSP only, sets up GICD (global) + UART SPI
 *      gic_inithart()  - all CPUs, sets up per-CPU GICR + ICC
 */

#include "arch/arm64.h"
#include "sys/types.h"
#include "arch/mmio.h"
#include "arch/memlayout.h"
#include "arch/platform.h"
#include "arch/dtb.h"
#include "arch/gic.h"
#include "sys/kio.h"

/* GICD (Distributor) - one per system */
#define GICD_CTLR               0x000
#define GICD_TYPER              0x004
#define GICD_ISENABLER(n)       (0x100 + 4 * (n))
#define GICD_ICENABLER(n)       (0x180 + 4 * (n))
#define GICD_ICACTIVER(n)       (0x380 + 4 * (n))
#define GICD_ICFGR(n)           (0xC00 + 4 * (n))
#define GICD_IPRIORITYR(n)      (0x400 + 4 * (n))
#define GICD_IGROUPR(n)         (0x080 + 4 * (n))
#define GICD_IROUTER(n)         (0x6000 + 8 * (n))
#define GICD_PIDR2              0xFFE8

#define GICD_CTLR_EN_GRP1NS     (1U << 1)
#define GICD_CTLR_EN_GRP1A      (1U << 2)
#define GICD_CTLR_ARE_NS        (1U << 4)
#define GICD_CTLR_RWP           (1U << 31)

#define GICD_TYPER_ITLINES_MASK     0x1fU
#define GICD_SPI_MAX                1020        /* arch ceiling on SPI count */

/* One second or a spin count if CNTFRQ_EL0 reads 0 */
#define GIC_RWP_SPINS               100000000UL

#define GICD_PIDR2_ARCHREV_SHIFT    4
#define GICD_PIDR2_ARCHREV_MASK     0xF

/*
 * GICR (Redistributor) - one per CPU
 * Frame sit at platform.gicr_base + cpu * gicr_stride
 * SGI_base is at offset 0x10000 within each frame. The
 * stride is one frame (0x20000) normally, or two (0x40000)
 * if the GIC adds a VLPI frame discovered from GICR_TYPER.VLPIS
 * in gic_init().
 */
#define GICR_FRAME_SIZE         0x20000
#define GICR_FRAME_SIZE_VLPI    0x40000

#define GICR_TYPER_OFF          0x008
#define GICR_TYPER_VLPIS        (1UL << 1)
#define GICR_TYPER_LAST         (1UL <<4)

#define GICR_WAKER_OFF          0x014
#define GICR_SGI_OFF            0x10000
#define GICR_IGROUPR0_OFF       (GICR_SGI_OFF + 0x080)
#define GICR_ISENABLER0_OFF     (GICR_SGI_OFF + 0x100)
#define GICR_ICENABLER0_OFF     (GICR_SGI_OFF + 0x180)
#define GICR_IPRIORITYR_OFF(N)  (GICR_SGI_OFF + 0x400 + 4 * (N))

#define GIC_SPI_BASE            32

#define GICR_WAKER_PSLEEP       (1U << 1)
#define GICR_WAKER_CASLEEP      (1U << 2)

#define ICC_CTLR_EOIMODE        (1U << 1)

static uintptr gicd;
static uint32 gic_nirq;

/* Stride between redistributor frames. Defaults to the no-VLPI size. */
static uint64 gicr_stride = GICR_FRAME_SIZE;

/* Per-CPU redistributor frame (KVA), found once by gicr_find() at init. */
static uintptr gicr_frame[NCPU_MAX];

/* ICC (CPU interface) system register helpers */
static inline void icc_write_sre(uint64 val)
{
    asm volatile("msr s3_0_c12_c12_5, %0" :: "r"(val));
    isb();
}

static inline void icc_write_pmr(uint32 val)
{
    asm volatile("msr s3_0_c4_c6_0, %0" :: "r"((uint64)val));
}

static inline void icc_write_igrpen1(uint32 val)
{
    asm volatile("msr s3_0_c12_c12_7, %0" :: "r"((uint64)val));
}

static inline uint32 icc_read_iar1(void)
{
    uint64 val;

    asm volatile("mrs %0, s3_0_c12_c12_0" : "=r"(val));

    return (uint32) val;
}

static inline void icc_write_eoir1(uint32 val)
{
    asm volatile("msr s3_0_c12_c12_1, %0" :: "r"((uint64)val));
}

static inline uint64 icc_read_ctlr(void)
{
    uint64 val;

    asm volatile("mrs %0, s3_0_c12_c12_4" : "=r"(val));

    return val;
}

static inline void icc_write_ctlr(uint64 val)
{
    asm volatile("msr s3_0_c12_c12_4, %0" :: "r"(val));
    isb();
}

static inline void icc_write_dir(uint32 val)
{
    asm volatile("msr s3_0_c12_c11_1, %0" :: "r"((uint64)val));
}

static inline uintptr gicr_base(void)
{
    return gicr_frame[cpuid()];
}

static inline uint32 gicd_read32(uint64 off)
{
    return mmio_read32(gicd + off);
}

static inline void gicd_write32(uint64 off, uint32 val)
{
    mmio_write32(gicd + off, val);
}

static inline void gicd_write64(uint64 off, uint64 val)
{
    mmio_write64(gicd + off, val);
}

/*
 * ITLinesNumber encodes 32*(N+1); the arch caps SPIs 1019, so an
 * unclamped value walks past the register file.
 */
static uint32 gic_typer_nirq(void)
{
    uint32 lines = gicd_read32(GICD_TYPER) & GICD_TYPER_ITLINES_MASK;
    uint32 n = 32 * (lines + 1);

    return (n > GICD_SPI_MAX) ? GICD_SPI_MAX : n;
}

static void gic_wait_rwp(void)
{
    uint64 deadline, freq, spins;

    freq = read_cntfrq();
    spins = GIC_RWP_SPINS;
    deadline = (freq != 0) ? read_cntfrq() + freq : 0;

    while (gicd_read32(GICD_CTLR) & GICD_CTLR_RWP) {
        if (freq != 0) {
            if (read_cntpct() >= deadline) {
                break;
            }
        } else if (--spins == 0) {
            break;
        }
    }
}

/*
 * Pack an MPIDR affinity into the contiguous from GICR_TYPER[63:32]
 * uses (Aff3|Aff2|Aff1|Aff0).
 */
static uint64 mpidr_to_affinity(uint64 mpidr)
{
    return ((mpidr >> 32) & 0xff) << 24
        | ((mpidr >> 16) & 0xff) << 16
        | ((mpidr >> 8) & 0xff) << 8
        | (mpidr & 0xff);
}

/* Find this CPU's redistributor */
static uintptr gicr_find(void)
{
    uint64 target = mpidr_to_affinity(cpu_mpidr[cpuid()]);
    uintptr frame = PA_TO_KVA(platform.gicr_base);
    uint64 typer;

    while (true) {
        typer = mmio_read64(frame + GICR_TYPER_OFF);

        if (((typer >> 32) & 0xffffffffUL) == target) {
            return frame;
        }

        if (typer & GICR_TYPER_LAST) {
            break;
        }

        frame += gicr_stride;
    }

    panic("gic: no redistributor frame for this CPU");

    return 0;       /* unreachable */
}

uint32 gic_nirqs(void)
{
    return gic_nirq;
}

void gic_irq_enable(uint32 irq)
{
    if (irq >= GIC_SPI_BASE) {
        gicd_write32(GICD_ISENABLER(irq / 32), 1U << (irq % 32));
    } else {
        mmio_write32(gicr_base() + GICR_ISENABLER0_OFF, 1U << irq);
    }
}

void gic_irq_disable(uint32 irq)
{
    if (irq >= GIC_SPI_BASE) {
        gicd_write32(GICD_ICENABLER(irq / 32), 1U << (irq % 32));
    } else {
        mmio_write32(gicr_base() + GICR_ICENABLER0_OFF, 1U << irq);
    }
}

void gic_irq_set_group(uint32 irq, uint32 group)
{
    uintptr reg;
    uint32 bit;

    if (irq >= GIC_SPI_BASE) {
        reg = gicd + GICD_IGROUPR(irq / 32);
        bit = 1U << (irq % 32);
    } else {
        reg = gicr_base() + GICR_IGROUPR0_OFF;
        bit = 1U << irq;
    }

    if (group) {
        mmio_setbits32(reg, bit);
    } else {
        mmio_clrbits32(reg, bit);
    }
}

void gic_irq_set_priority(uint32 irq, uint8 prio)
{
    uint32 shift = (irq % 4) * 8;
    uintptr reg;

    if (irq >= GIC_SPI_BASE) {
        reg = gicd + GICD_IPRIORITYR(irq / 4);
    } else {
        reg = gicr_base() + GICR_IPRIORITYR_OFF(irq / 4);
    }

    mmio_clrsetbits32(reg, 0xFFU << shift, (uint32)prio << shift);
}

void gic_irq_set_affinity(uint32 irq, uint64 aff)
{
    /*
     * Only SPIs are routable. SGIs/PPIs are inherently per-CPU and have no
     * GICD_IROUTER. 'aff' is an MPIDR affinity (Aff3.Aff2.Aff1.Aff0). IRM is
     * left 0 so the SPI targets exactly the PE with that affinity. Requires
     * GICD_CTLR.ARE_NS to already be set.
     */

    if (irq < GIC_SPI_BASE) {
        return;
    }

    gicd_write64(GICD_IROUTER(irq), aff);
}



/*
 * gic_init - global distributor setup (BSP only, called once)
 *
 * Configured GICD for affinity routing and enables shared
 * interrupts (SPIs).
 */
void gic_init(void)
{
    uint32 archrev, i;

    gicd = PA_TO_KVA(platform.gicd_base);

    archrev = (gicd_read32(GICD_PIDR2) >> GICD_PIDR2_ARCHREV_SHIFT)
        & GICD_PIDR2_ARCHREV_MASK;

    if (archrev < 3) {
        panic("gic: not a GICv3 distributor (PIDR2 ArchRev < 3");
    }

    if (mmio_read64(PA_TO_KVA(platform.gicr_base) + GICR_TYPER_OFF)
        & GICR_TYPER_VLPIS) {
        gicr_stride = GICR_FRAME_SIZE_VLPI;
    }

    gic_nirq = gic_typer_nirq();

    /* Disable the distributor */
    gicd_write32(GICD_CTLR, 0);
    gic_wait_rwp();

    for (i = GIC_SPI_BASE; i < gic_nirq; i += 32) {
        gicd_write32(GICD_IGROUPR(i >> 5), 0xFFFFFFFFU);
        gicd_write32(GICD_ICENABLER(i >> 5), 0xFFFFFFFFU);
        gicd_write32(GICD_ICACTIVER(i >> 5), 0xFFFFFFFFU);
    }

    for (i = GIC_SPI_BASE; i < gic_nirq; i += 16) {
        gicd_write32(GICD_ICFGR(i >> 4), 0);
    }

    for (i = GIC_SPI_BASE; i < gic_nirq; i += 4) {
        gicd_write32(GICD_IPRIORITYR(i >> 2), 0x80808080U);
    }

    gic_wait_rwp();

    /* Enable then wait */
    gicd_write32(GICD_CTLR, GICD_CTLR_ARE_NS | GICD_CTLR_EN_GRP1NS
        | GICD_CTLR_EN_GRP1A);
    gic_wait_rwp();

    for (i = GIC_SPI_BASE; i < gic_nirq; i++) {
        gic_irq_set_affinity(i, read_mpidr());
    }

    /*
     * UART (SPI): non-secure group, flat default priority, routed to the
     * boot CPU, then enabled. Explicit IROUTER (IRM=0, this CPU's MPIDR
     * affinity) replaces relying on the reset value to pin IRQ 33 here.
     */
    gic_irq_set_group(IRQ_UART, 1);
    gic_irq_set_priority(IRQ_UART, GIC_PRIO_DEFAULT);
    gic_irq_set_affinity(IRQ_UART, read_mpidr());
    gic_irq_enable(IRQ_UART);

    printk("gic: distributor initialized, %u SPIs\n", gic_nirq - GIC_SPI_BASE);
}

void gic_inithart(void)
{
    uintptr gicr;

    gicr_frame[cpuid()] = gicr_find();
    gicr = gicr_base();

    /*
     * CPU interface - enable system register access
     *
     * GICv3 moved the CPU interface from MMIO (GICv2) to system
     * registers. ICC_SRE_EL1 bit 0 = 1 enables this.
     */
    icc_write_sre(1);

    /*
     * EOImode = 1
     * A threaded interrupt can then drop its running priority
     * immediately yet stay active. The active state masks the
     * source in hardware until the serving ithread issues ICC_DIR.
     */
    icc_write_ctlr(icc_read_ctlr() | ICC_CTLR_EOIMODE);

    /* accept all priority levels */
    icc_write_pmr(0xFF);

    /* enable group 1 (non-secure) interrupts */
    icc_write_igrpen1(1);

    /*
     * Redistributor
     *
     * On reset the redistributor may be asleep. Clear the
     * processor-sleep bit, the spin until the children-asleep
     * bit confirms it's awake
     */
    mmio_clrbits32(gicr + GICR_WAKER_OFF, GICR_WAKER_PSLEEP);

    while (mmio_read32(gicr + GICR_WAKER_OFF) & GICR_WAKER_CASLEEP) {
        continue;
    }

    /*
     * Set all SGIs (0-15) and PPIs (16-31) to group 1 (non-secure).
     * A non-secure OS has no business leaving interrupts in Group 0
     * (secure/FIQ). Individual interrupts are still gated by ISENABLER.
     */
    mmio_write32(gicr + GICR_IGROUPR0_OFF, ~0U);

    /*
     * Timer (PPI 30) and the panic IPI (SGI 0): flat default priority, then
     * enable. Both target this CPU's redistributor, so this runs on
     * every hart.
     */
    gic_irq_set_priority(IRQ_TIMER, GIC_PRIO_DEFAULT);
    gic_irq_set_priority(IRQ_PANIC, GIC_PRIO_DEFAULT);
    gic_irq_enable(IRQ_TIMER);
    gic_irq_enable(IRQ_PANIC);

    printk("gic: cpu%d redistributor initialized\n", cpuid());

}

uint32 gic_acknwlg(void)
{
    return icc_read_iar1() & 0x3FF;
}

void gic_eoi(uint32 irq)
{
    icc_write_eoir1(irq);
}

void gic_deactivate(uint32 irq)
{
    icc_write_dir(irq);
}

/*
 * Send an SGI to all CPUs except self
 *
 * ICC_SGI1_EL1 register format:
 *      Bit 40 (IRM) = 1: route to all PEs except the caller
 *      Bit [27:24] (INTID): which SGI (0-15)
 *
 * Used by panic() to halt all other cores immediately
 */
void gic_send_sgi_all(uint32 intid)
{
    uint64 val = (1UL << 40) | ((uint64)(intid & 0xF) << 24);

    asm volatile("msr s3_0_c12_c11_5, %0" :: "r"(val) : "memory");
    isb();
}
