/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * syscall.c - System call dispatcher
 *
 * AArch64 syscall ABI:
 *      x8 - syscall number
 *      x0..x5 - arguments
 *      x0 - return value (written back into tf->x[0])
 *      svc #0 trap to kernel
 *
 * Handler fetch arguments via argint / argaddr / arglong,
 * which read the saved user registers out of the trapframe.
 * The trapframe's x8 is left as-in on return : the user sees
 * the syscall number it put in.
 *
 * User-pointer arguments arrive as plain 64-bit values. The
 * kernel nevers dereferences them directly: any access goes
 * through copyin / copyout, which LDTR / STTR. PAN (kvminithart)
 * turns plain ldr/str against a user VA into a fault, so a
 * forgotten copyin would be caught immediately rather than
 * silently succeeding.
 */

#include "dev/console.h"
#include "sys/exec.h"
#include "sys/kio.h"
#include "sys/proc.h"
#include "sys/syscall.h"
#include "arch/trapframe.h"
#include "sys/types.h"
#include "arch/copy.h"
#include "sys/kassert.h"

/* Max bytes copied per copyin round-trip inside sys_write. */
#define WRITE_BOUNCE_SZ     128
#define READ_BOUNCE_SZ      128

/*
 * Fetch the nth syscall argument as a raw 64-bit value.
 *
 * av6 follows the AArch64 PCS for syscall args: x0..x5. Register
 * x8 carries the syscall number itself, so it is not an argument
 * slot. Anything beyond arg 5 is unsupported (no syscall needs it
 * yet, and stack-passed args require copyin).
 */
static uint64 argraw(int n)
{
    struct trapframe *tf = myproc()->tf;

    KASSERT(n >= 0 && n <=5, "argraw: bad arg index");

    return tf->x[n];
}

static void argint(int n, int *ip)
{
    *ip = (int)argraw(n);
}

static void argaddr(int n, uintptr *ap)
{
    *ap = (uintptr)argraw(n);
}

static void arglong(int n, uint64 *lp)
{
    *lp = argraw(n);
}

/*
 * sys_getpid : returns the caller's pid
 */
static uint64 sys_getpid(void)
{
    return myproc()->pid;
}

/*
 * sys_fork : duplicate the calling process.
 *
 * fork() returns the child's pid in the parent and 0 in the child;
 * -1 on failure. The child wakes up inside scheduler, runs through
 * forkret -> usertrapret, and re-enters userspace at the instruction
 * after the SVC with x0 = 0 (set by fork() in the child's trapframe).
 */
static uint64 sys_fork(void)
{
    return (uint64)fork();
}

/*
 * sys_exit(status) : terminates the calling process.
 *
 * exit() does not return, so this never reaches its return statement.
 */
static uint64 sys_exit(void)
{
    int status;

    argint(0, &status);
    exit(status);

    return 0;
}

/*
 * sys_wait(xstatus) : reap a ZOMBIE child
 *
 * xstatus is a user VA (or 0). On success returns the reaped
 * child's pid; -1 ir the caller has no children or copyout fails.
 */
static uint64 sys_wait(void)
{
    uintptr xstatus;

    argaddr(0, &xstatus);
    return (uint64)wait(xstatus);
}

/*
 * sys_kill(pid) : flag a process for termination.
 *
 * Returns 0 on succes, -1 if no such process. The victim does not
 * die synchronously : kill() only sets a flag and may wake a sleeper.
 * The flagged process self-exits when it next returns to user mode.
 */
static uint64 sys_kill(void)
{
    int pid;

    argint(0, &pid);
    return (uint64)kill(pid);
}

/*
 * sys_sbrk(n) : grow / shrink the user heap by n bytes.
 *
 * Returns the OLD program break (= old p->sz) on success,
 * so a follow-up alloc can use the just-mapped range starting
 * there. Returns (uint64)-1 on failure (out of memory, or
 * shrink would underflow USER_BASE).
 *
 * POSIX semantics : sbrk(0) returns the current break without
 * modifying it.
 */
static uint64 sys_sbrk(void)
{
    int n;
    uint64 old;

    argint(0, &n);

    old = myproc()->sz;
    if (growproc(n) < 0) {
        return (uint64)-1;
    }
    return old;
}

/*
 * sys_exec(path) : replace the caller's address space with the ELF
 * at the given path.
 *
 * path is a user VA pointing at a NUL-terminated string. It is copied
 * into a kernel buffer before calling exec(). It does its own pagetable
 * swap, and copyinstr after that would be reading from the new (empty)
 * address space.
 *
 * On success exec() doesn't return to the user's svc. It rewrites tf->elr
 * to the new entry point and tf->sp to the new stack top. But 0 is returned
 * here so the dispatcher writes 0 to tf->x[0], which becomes the new program's
 * initial x0. Entry argc comes off the stack, not x0.
 *
 * Returns -1 if the path is unresolvable, too long for MAXPATH, or if the ELF
 * fails to load. On any -1 the caller is unchanged.
 */
static uint64 sys_exec(void)
{
    char path[MAXPATH];
    uintptr user_path, uargv;

    argaddr(0, &user_path);
    argaddr(1, &uargv);

    if (copyinstr(path, user_path, sizeof(path)) < 0) {
        return (uint64)-1;
    }

    return (uint64)exec(path, uargv);
}

/*
 * sys_write(fd, buf, n) : write n bytes from user buf to fd
 *
 * Only fd 1 and 2 are honoured, and both go to the console. Read fd tables
 * arrive with the file-table work (TODO). Bytes come in via copyin to a small
 * bounce buffer, so the user pointer is never dereferenced on the kernel side.
 * The text does not enter the log ring: app output is not dmesg.
 *
 * Return: number of bytes written, or (uint64)-1 on error.
 */
static uint64 sys_write(void)
{
    int fd;
    uintptr user_buf;
    uint64 n, chunk, done = 0;
    char bounce[WRITE_BOUNCE_SZ];

    argint(0, &fd);
    argaddr(1, &user_buf);
    arglong(2, &n);

    if (fd != 1 && fd != 2) {
        return (uint64)-1;
    }

    while (done < n) {
        chunk = n - done;
        if (chunk > sizeof(bounce)) {
            chunk = sizeof(bounce);
        }

        if (copyin(bounce, user_buf + done, chunk) < 0) {
            /*
             * Bad user pointer. Return -1 if nothing was
             * written yet, or the partial count if some
             * bytes already made it to the UART. Matches
             * POSIX write() semantics for EFAULT on partial
             * writes.
             */
            return done == 0 ? (uint64)-1 : done;
        }

        console_write(bounce, chunk);
        done += chunk;
    }

    return done;
}

/*
 * sys_read(fd, buf, n) : read up to n bytes from fd into user buf.
 *
 * Only fd 0 (stdin / console) for now. console_read blocks until a full
 * line is available (or Ctrl-D), reading into a kernel bounce buffer.
 * copyout then delivers it to userspace, so the user pointer is never
 * dereferenced directly. Returns bytes read (0 on EOF) or -1 on a bad fd
 * or bad user buffer.
 */
static uint64 sys_read(void)
{
    int fd;
    uintptr user_buf;
    uint64 n, chunk, got;
    char bounce[READ_BOUNCE_SZ];

    argint(0, &fd);
    argaddr(1, &user_buf);
    arglong(2, &n);

    if (fd != 0) {
        return (uint64)-1;
    }

    if (n == 0) {
        return 0;
    }

    chunk = (n < sizeof(bounce)) ? n : sizeof(bounce);
    got = console_read(bounce, chunk);
    if (got <= 0) {
        return (uint64)got;     /* 0 = EOF (Ctrl-D) */
    }

    if (copyout(user_buf, bounce, got) < 0) {
        return (uint64)-1;
    }

    return got;
}


static uint64 (*syscalls[])(void) = {
    [SYS_getpid] = sys_getpid,
    [SYS_write] = sys_write,
    [SYS_fork] = sys_fork,
    [SYS_exit] = sys_exit,
    [SYS_wait] = sys_wait,
    [SYS_kill] = sys_kill,
    [SYS_sbrk] = sys_sbrk,
    [SYS_exec] = sys_exec,
    [SYS_read] = sys_read,
};

/*
 * syscall - dispatch a system call from EL0
 *
 * Called from usertrap_sync() with EC = EC_SVC64. Reads the
 * syscall number from x8, looks up the handler, and writes the
 * return value back into tf->x[0]. Unknown numbers return -1.
 *
 * Note: ELR_EL1 is left untouched. The CPU points it at the
 * instruction *after* the SVC, so eret resumes correctly.
 */
void syscall(void)
{
    struct proc *p = myproc();
    uint64 num = p->tf->x[8];

    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        p->tf->x[0] = syscalls[num]();
    } else {
        printk("pid %d %s: unknown syscall %lu\n", p->pid, p->name, num);
        p->tf->x[0] = (uint64)-1;
    }
}
