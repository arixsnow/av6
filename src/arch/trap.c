/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * trap.c - Exception handlers for AArch64
 *
 * Two paths:
 *      EL1 (kernel): trap_sync, trap_irq - called from el1_sync/el1_irq
 *      EL0 (user): usertrap_sync - called from el0_sync
 *                  trap_irq reused - called from el0_irq
 */

#include "arch/arm64.h"
#include "arch/gic.h"
#include "arch/mte.h"
#include "arch/sysreg.h"
#include "arch/trapframe.h"
#include "sys/bitops.h"
#include "sys/intr.h"
#include "sys/kassert.h"
#include "sys/kio.h"
#include "sys/proc.h"
#include "sys/syscall.h"
#include "sys/types.h"

/* Called from vectors.s */
void trap_sync(struct trapframe *tf, uint64 esr, uint64 far);
void trap_irq(void);
void usertrap_sync(struct trapframe *tf, uint64 esr, uint64 far);
void trap_unknown(uint64 esr, uint64 elr, uint64 far);
void handle_stack_overflow(uint64 sp);
void trap_serror(struct trapframe *tf, uint64 esr, uint64 far);
void trap_fiq(struct trapframe *tf, uint64 esr, uint64 far);

/*
 * Exception Class (EC) values from ESR_EL1[31:26]
 *
 * The EC encodes both the exception type and the source EL.
 * Aborts from EL0 and EL1 have adjacent EC values:
 *      IABT: EL0 = 0x20, EL1 = 0x21
 *      DABT: EL0 = 0x24, EL1 = 0x25
 */
#define EC_UNKNOWN          0x00        /* Unknown reason */
#define EC_SVC64            0x15        /* SVC (system call) from AArch64 */
#define EC_IABT_EL0         0x20        /* Instruction abort, lower EL */
#define EC_IABT_EL1         0x21        /* Instruction abort, same EL */
#define EC_DABT_EL0         0x24        /* Data abort, lower EL */
#define EC_DABT_EL1         0x25        /* Data abort, same EL */
#define EC_SERROR           0x2F        /* SError interrupt */

/* ESR_EL1 field extraction */
#define ESR_EC_SHIFT            26
#define ESR_EC_MASK             GENMASK(5, 0)   /* EC : ESR[31:26] */
#define ESR_ISS_MASK            GENMASK(24, 0)  /* ISS : ESR[24: 0] */
#define ESR_ISS_DFSC_MASK       GENMASK(5, 0)   /* DFSC : ISS[5:0] */

/* SError syndrome */
#define ESR_IDS                 BIT(24)
#define ESR_AET_SHIFT           10
#define ESR_AET_MASK            GENMASK(2, 0)   /* AET : ESR[12:10] */
#define ESR_AET_UC              0x0             /* uncontainable */
#define ESR_AET_UEU             0x1             /* uncorrected, unrecoverable */
#define ESR_AET_UEO             0x2             /* restartable, not yet consumed */
#define ESR_AET_UER             0x3             /* uncorrected, recoverable */
#define ESR_AET_CE              0x6             /* corrected */


/*
 * Per-CPU emergency stack for the kernel-stack-overflow handler
 *
 * the (overflowed) stack - that is the double-fault we must avoid. The EL1
 * vector instead switches SP to this CPU's slot here, a small known-good stack,
 * before calling handle_stack_overflow(). One page is ample for the report +
 * panic. The symbol is global so vector.s can address it.
 */
char overflow_stack[NCPU_MAX][OVERFLOW_STACK_SIZE] __attribute__((aligned(16)));

/*
 * Handle synchronous exception from EL1 (kernel mode)
 *
 * esr: Exception Syndrome Register: encodes the cause
 * elr: Exception Link Register: address of faulting instruction
 * far: Fault Address Register: the address that caused the fault
 */
void trap_sync(struct trapframe *tf, uint64 esr, uint64 far)
{
    uint32 ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;        /* Exception Class */
    uint32 iss = esr & ESR_ISS_MASK;                        /* Instruction Specific Syndrome */
    struct proc *p;

    switch (ec) {
        case EC_DABT_EL1:
            p = myproc();
            if (p != NULL && p->onfault != 0) {
                tf->elr = p->onfault;
                p->onfault = (uintptr)NULL;
                return;
            }

            printk("Data Abort at ELR=%p, FAR=%p, ISS=%#x\n", tf->elr, far, iss);
            panic("data abort");
            break;

        case EC_IABT_EL1:
            printk("Instruction Abort at ELR=%p, FAR=%p, ISS=%#x\n", tf->elr, far, iss);
            panic("instruction abort");
            break;

        case EC_SVC64:
            printk("SVC from Kernel (unexpected), ESR=%p, ELR=%p\n", esr, tf->elr);
            panic("unexpected svc");
            break;

        default:
            printk("Sync Exception: EC=%#x, ISS=%#x\n", ec, iss);
            printk("    ELR=%p (faulting instruction)\n", tf->elr);
            printk("    FAR=%p (fault address)\n", far);
            panic("unhandled sync exception");
            break;
    }
}

/*
 * Handle IRQ (shared by EL1 and EL0 paths).
 *
 * GICv3 flow:
 *      1. Acknowledge - read IRQ ID (claims it exclusively)
 *      2. Handle - dispatch to the right driver
 *      3. End - signal completion so GIC can deliver the next one
 * Timer preemption calls yield(), which works correctly
 * regardless or whether the interrupted code was EL0 or EL1
 */
void trap_irq(void)
{
    struct cpu *c;
    int res;
    uint32 irq = gic_acknwlg();

    if (irq == GIC_SPURIOUS)
        return;

    if (irq == IRQ_PANIC) {
        gic_eoi(irq);
        gic_deactivate(irq);
        panic_stop_self();
    }

    res = intr_dispatch(irq);

    /*
     * EOImode=1 end-of-interrupt, in two steps:
     *
     * gic_eoi() : drops this CPU's running priority (ICC_EOIR1)
     *             the interrupt is still active.
     * gic_deactivate : clears the active state (ICC_DIR), letting the
     *                  source assert again.
     */
    gic_eoi(irq);
    if (res != FILTER_SCHEDULE_THREAD) {
        gic_deactivate(irq);
    }

    /*
     * preemption point.
     *
     * The whole GIC cycle (ack -> handle -> EOI) has now
     * completed on this CPU, so the ack/EOI pair is balanced here
     * and a subsequent migration is harmless. Only now do we honour
     * a reschedule request raised by the clock filter.
     *
     * myproc() != NULL gates out the idle/scheduler context (c->proc
     * is NULL there, nothing to preempt). A held spinlock can't be the
     * interrupted context. Acquiring one mask IRQs, so taking a maskable
     * IRQ at all implies noff == 0. The yield is always safe here.
     */
    c = &cpus[cpuid()];
    if (c->need_resched && myproc() != NULL) {
        c->need_resched = 0;
        yield();
    }
}

extern void userret(struct trapframe *tf);

/*
 * Handle synchronous exception from EL0 (user mode)
 *
 * tf: pointer to trapframe on the kernel stack
 * esr: Exception Syndrome Register (grabbed in assembly)
 * far: Fault Address Register (grabbed in assembly)
 */
void usertrap_sync(struct trapframe *tf, uint64 esr, uint64 far)
{
    uint32 ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    uint32 iss = esr & ESR_ISS_MASK;

    switch (ec) {
        case EC_SVC64:
            syscall();
            break;
        case EC_DABT_EL0:
            if ((iss & ESR_ISS_DFSC_MASK) == DFSC_TAG_CHECK) {
                printk("kill: pid %d MTE tag-check fault, ELR=%p, FAR=%p\n",
                    myproc()->pid, tf->elr, far);
            } else {
                printk("kill: pid %d data abort ELR=%p, FAR=%p, ISS=%#x\n",
                    myproc()->pid, tf->elr, far, iss);
            }
            exit(-1);
            break;
        case EC_IABT_EL0:
            printk("kill: pid %d instruction abort ELR=%p, FAR=%p\n",
                myproc()->pid, tf->elr, far);
            exit(-1);
            break;
        default:
            /*
             * Illegal/undefined instruction or any other EL0 sync trap: the
             * process did something it can't, so terminate it.
             */
            printk("kill: pid %d illegal sync exception, EC=%#x, ELR=%p, FAR=%p\n",
                myproc()->pid, ec, tf->elr, far);
            exit(-1);
            break;
    }

    /*
     * Killed check : if SYS_kill flagged us while we were in the kernel
     * for this syscall (or some other proc raced in via SMP), exit now
     * rather than returning to user space. exit(-1) signals "killed by
     * signal" to the eventual wait().
     */
    if (killed(myproc())) {
        exit(-1);
    }
}

/*
 * Enter user mode (or return to it)
 *
 * Called from forkret() for initial entry, and later from
 * exec() to start a new program. Never returns.
 *
 * The trapframe at p->tf must be fully populated:
 *      tf->elr = user entry point
 *      tf->spsr = EL0 PSTATE (0x0 = EL0t, IRQs enabled)
 *      tf->sp = user stack pointer
 *      tf->x[] = initial register values
 */
void usertrapret(void)
{
    struct proc *p = myproc();

    /*
     * Diable interrupts until eret. About to restore
     * user state, a nested IRQ here would see half-restored
     * registers and corrupt the trapframe
     */
    irq_disable();

    userret(p->tf);
}

/*
 * Handle unrecognized exception
 */
void trap_unknown(uint64 esr, uint64 elr, uint64 far)
{
    printk("Unhandled Exception!\n");
    printk("    ESR=%p\n", esr);
    printk("    ELR=%p\n", elr);
    printk("    FAR=%p\n", far);
    panic("unhandled exception");
}

/*
 * handle_stack_overflow - last-resort report for a kernel-stack overflow.
 *
 * Reached from the EL1 vector (el1_sync / el1_irq) when the incoming exception
 * frame would spill past the bottom of the running proc's kstack into the guard.
 * The vector has already switched SP to this CPU's overflow_stack, so this runs
 * on a known-good stack. 'sp' is the kernel SP at the failed entry. ELR/ESR/FAR
 * still hold the triggering exception. No return.
 */
void handle_stack_overflow(uint64 sp)
{
    struct proc *p = cpus[cpuid()].proc;
    uint64 elr, esr, far;

    asm volatile("mrs %0, elr_el1" : "=r"(elr));
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, far_el1" : "=r"(far));

    printk("\n=== KERNEL STACK OVERFLOW ===\n");
    printk("cpu %d, pid %d (%s)\n",
        (int)cpuid(), p ? p->pid : -1, p ? p->name : "<none>");
    printk("    kernel SP = %p (overflowed into the guard)\n", sp);
    printk("    ELR = %p ESR = %p FAR = %p\n", elr, esr, far);
    panic("kernel stack overflow");
}

/*
 * A RAS SError the hardware already contained is not an event: CE and UEO
 * return silently, on purpose, and logging them would be the bug. Anything
 * whose syndrome cannot be decoded is uncontainable by definition.
 */
static int serror_is_fatal(uint64 esr)
{
    uint64 aet;

    if ((esr & ESR_IDS) != 0
        || SYS_FIELD(read_sysreg(id_aa64pfr0_el1), ID_AA64PFR0_RAS)
            == ID_AA64PFR0_RAS_NONE) {
        return 1;
    }

    aet = (esr >> ESR_AET_SHIFT) & ESR_AET_MASK;

    return (aet != ESR_AET_CE && aet != ESR_AET_UEO);
}

void trap_serror(struct trapframe *tf, uint64 esr, uint64 far)
{
    KASSERT(((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) == EC_SERROR,
        "trap_serror: EC %#lx is not SError", (esr >> ESR_EC_SHIFT) & ESR_EC_MASK);

    if (!serror_is_fatal(esr)) {
        return;
    }

    printk("SError at ELR=%p, FAR=%p, ESR=%p\n", tf->elr, far, esr);
    panic("uncontainable system error");
}

void trap_fiq(struct trapframe *tf, uint64 esr, uint64 far)
{
    printk("FIQ at ELR=%p, FAR=%p, ESR=%p\n", tf->elr, far, esr);
    panic("unexpected FIQ");
}
