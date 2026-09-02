/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * exec.c - ELF loading and the exec() syscall
 *
 * loadelf() builds a fresh user address space from a static ELF64
 * image; exec() swaps it in for the caller's.
 *
 * Constraints:
 *      : ELF64, AArch64, ET_EXEC, little-endian
 *      : Static-linked, no dynamic linker, no PT_INTERP
 *      : PT_LOAD segments must be page-aligned (vaddr % PGSIZE == 0)
 *      : Every page mapped with PAGE_USER (RWX) for now ; per-segment
 *        permissions honoring PF_X / PF_W / PF_R land with the security
 *        pass.
 */

#include "sys/types.h"
#include "arch/arm64.h"
#include "arch/asid.h"
#include "sys/elf.h"
#include "sys/exec.h"
#include "vm/kalloc.h"
#include "sys/kio.h"
#include "arch/memlayout.h"
#include "arch/mmu.h"
#include "sys/proc.h"
#include "sys/string.h"
#include "arch/pmap.h"
#include "arch/copy.h"

/*
 * Embedded program table
 *
 * Maps user-visible paths to ELF blobs that the kernel was linked
 * with (via user/blobs.o). Later this will be replaced with
 * a CPIO-parsed initramfs supporting arbitrary paths. The table is
 * NULL-terminated so future entries just slot in above the sentinel.
 *
 * Externs come from user/blobs.s where each program's ELF is pulled
 * in with .incbin between a pair of labels:
 *      <name>_elf_start / <name>_elf_end
 */
extern uchar init_elf_start[];
extern uchar init_elf_end[];
extern uchar argvtest_elf_start[];
extern uchar argvtest_elf_end[];
extern uchar faulttest_elf_start[];
extern uchar faulttest_elf_end[];

static struct {
    const char *path;
    const uchar *start;
    const uchar *end;
} embedded_progs[] = {
    {"/init", init_elf_start, init_elf_end},
    {"/argv_test", argvtest_elf_start, argvtest_elf_end},
    {"/fault_test", faulttest_elf_start, faulttest_elf_end},
    {NULL, NULL, NULL}
};

/*
 * load_segment : place one PT_LOAD segment into the user pagetable.
 *
 * Iterates page by page over [vaddr, vaddr + memsz), allocating a fresh
 * zeroed page for each, copying on-file bytes (filesz) into it, and
 * mapping the page into the user pagetable. Pages past filesz stay zero
 * (BSS).
 *
 * Returns 0 on success, -1 out-of-memory or mappages failure. On failure,
 * pages already mapped remain mapped; the caller (loadelf) is expected
 * to release the whole pagetable via uvmfree.
 */
static int load_segment(pte_t *pagetable, const uchar *src, struct proghdr *ph)
{
    uintptr va, va_end;
    uint64 file_remaining, copy_now;
    char *page;

    if ((ph->vaddr & (PGSIZE - 1)) != 0) {
        return -1;      /* segments must be page-aligned */
    }
    if (ph->memsz < ph->filesz) {
        return -1;
    }

    file_remaining = ph->filesz;
    va_end = ph->vaddr + ph->memsz;

    for (va = ph->vaddr; va < va_end; va += PGSIZE) {
        page = kpage_alloc();
        if (page == NULL) {
            return -1;
        }
        memset(page, 0, PGSIZE);

        if (file_remaining > 0) {
            copy_now = (file_remaining < PGSIZE) ? file_remaining : PGSIZE;
            memcpy(page, src, copy_now);
            src += copy_now;
            file_remaining -= copy_now;
        }

        icache_sync_page(page);

        if (mappages(pagetable, USER_BASE + va, KVA_TO_PA((uintptr)page),
                PGSIZE, PAGE_USER) < 0) {
            kpage_free(page);
            return -1;
        }
    }

    return 0;
}

/*
 * loadelf
 *
 * High-level flow :
 *      1. Validate elfhdr (magic, class, data, machine, type, phentsize)
 *      2. Walk program headers; for each PT_LOAD :
 *          : sanity-check vaddr/memsz against MAXUVA
 *          : sanity-check off/filesz against the blob size
 *          : load_segment() into the pagetable
 *      3. Track the highest VA seen and report it via *sz_out
 *
 * Diagnostics on failure go to printk so userinit / exec() can panic
 * with a meaningful boot log rather than just "exec failed".
 */
int loadelf(pte_t *pagetable, const uchar *bytes, uint64 size,
            uintptr *entry_out, uint64 *sz_out)
{
    struct elfhdr *eh;
    struct proghdr *ph;
    uint64 ph_off, max_end = 0, end;
    int i;

    if (size < sizeof(struct elfhdr)) {
        printk("loadelf: image too small (%lu bytes)\n", size);
        return -1;
    }

    eh = (struct elfhdr *)bytes;

    if (eh->ident[EI_MAG0] != ELFMAG0 || eh->ident[EI_MAG1] != ELFMAG1
        || eh->ident[EI_MAG2] != ELFMAG2 || eh->ident[EI_MAG3] != ELFMAG3) {
        printk("loadelf: bad magic\n");
        return -1;
    }

    if (eh->ident[EI_CLASS] != ELFCLASS64) {
        printk("loadelf: not ELF64 (class=%u)\n", eh->ident[EI_CLASS]);
        return -1;
    }

    if (eh->ident[EI_DATA] != ELFDATA2LSB) {
        printk("loadelf: not little-endian (data=%u)\n", eh->ident[EI_DATA]);
        return -1;
    }

    if (eh->machine != EM_AARCH64) {
        printk("loadelf: not AArch64 (machine=%u)\n", eh->machine);
        return -1;
    }

    if (eh->type != ET_EXEC) {
        printk("loadelf: not ET_EXEC (type=%u)\n", eh->type);
        return -1;
    }

    if (eh->phentsize != sizeof(struct proghdr)) {
        printk("loadelf: unexpected phentsize %u\n", eh->phentsize);
        return -1;
    }

    for (i = 0; i < eh->phnum; i++) {
        ph_off = eh->phoff + (uint64)i * eh->phentsize;

        if (ph_off + sizeof(*ph) > size) {
            printk("loadelf: phdr %d past EOF\n", i);
            return -1;
        }

        ph = (struct proghdr *)(bytes + ph_off);

        if (ph->type != PT_LOAD) {
            continue;
        }

        if (ph->memsz < ph->filesz) {
            printk("loadelf: phdr %d : memsz < filesz\n", i);
            return -1;
        }

        if (ph->vaddr + ph->memsz < ph->vaddr) {
            printk("loadelf: phdr %d : vaddr+memsz overflow\n", i);
            return -1;
        }

        if (ph->vaddr + ph->memsz > MAXUVA) {
            printk("loadelf: phdr %d : runs past MAXUVA\n", i);
            return -1;
        }

        if (ph->off + ph->filesz > size) {
            printk("loadelf: phdr %d : file range past EOF\n", i);
            return -1;
        }

        if (load_segment(pagetable, bytes + ph->off, ph) < 0) {
            printk("loadelf: phdr %d : load_segment failed\n", i);
            return -1;
        }

        end = ph->vaddr + ph->memsz;
        if (end > max_end) {
            max_end = end;
        }
    }

    if (max_end == 0) {
        printk("loadelf: no PT_LOAD segments\n");
        return -1;
    }

    *entry_out = eh->entry;
    *sz_out = PGROUNDUP(max_end);

    return 0;
}

/*
 * exec : replace the calling proc's user address space with the ELF
 * identified by path.
 *
 * Two-phase commit :
 *      Phase 1 (revocable) : resolve path -> blob, build a fresh
 *      pagetable, laodelf, allocate stack. ANY failure here releases
 *      the new pagetable (and stack page if allocated) and returns -1
 *      with p unchanged.
 *
 *      Phase 2 (irrevocable, "swap-and-flush") : install the new
 *      pagetable on p, rest the trapframe so eret lands at the new
 *      _start, switchuvm() writes TTBR0, tlbi features flushes stale
 *      entries for the ASID, then we uvmfree the old pagetable.
 *
 * The TLB flush is mandatory between the TTBR0 write and the uvmfree
 * : exec keeps the same ASID, so old translations cached against
 * this ASID would otherwise point at pages uvmfree is about to release
 * back to kalloc.
 *
 * No locks needed : p->pagetable, p->sz, p->tf, p->name, p->asid
 * are private to the running proc, and no other CPU runs this proc
 * concurrently.
 *
 * uargv is the user VA of a NULL-terminated array of user string pointers
 * (argv), or 0 for none. The new program enters on the standard SysV/AArch64
 * initial stack: SP -> argc, then argv[] pointers, a NULL, an empty envp NULL,
 * and an AT_NULL auxv terminator. The argv strings sit at the top of the stack
 * page. The C runtime reads argc/argv from the stack (NOT from registers).
 * Fuller auxv (AT_PAGESZ, AT_RANDOM, ...) arrives with the userland libc phase.
 *
 * Argument bytes are bounded to one page. They are first copied from
 * the caller's address space into a single kalloc'd scratch page, never onto
 * the kernel stack, so deep argv cannot overflow the kernel stack.
 */
int exec(const char *path, uintptr uargv)
{
    struct proc *p = myproc();
    pte_t *new_pagetable, *old_pagetable;
    const uchar *elf_start = NULL, *elf_end = NULL;
    uintptr entry, uarg, strbase, sp;
    uint64 elf_size, new_sz, pos, off, infosz, *w;
    char *stack, *argbuf;
    const char *base, *q;
    int i, argc, slen;

    /* Resolve path : tiny linear scan; table is NULL-terminated */
    for (i = 0; embedded_progs[i].path != NULL; i++) {
        if (strcmp(path, embedded_progs[i].path) == 0) {
            elf_start = embedded_progs[i].start;
            elf_end = embedded_progs[i].end;
            break;
        }
    }
    if (elf_start == NULL) {
        return -1;      /* no such binary */
    }

    /* Phase 1 */
    new_pagetable = uvmcreate();
    if (new_pagetable == NULL) {
        return -1;
    }

    elf_size = (uint64)(elf_end - elf_start);
    if (loadelf(new_pagetable, elf_start, elf_size, &entry, &new_sz) < 0) {
        uvmfree(new_pagetable);
        return -1;
    }

    stack = kpage_alloc();
    if (stack == NULL) {
        uvmfree(new_pagetable);
        return -1;
    }
    memset(stack, 0, PGSIZE);

    if (mappages(new_pagetable, USER_BASE + new_sz, KVA_TO_PA((uintptr)stack),
        PGSIZE, PAGE_USER) < 0) {
        kpage_free(stack);
        uvmfree(new_pagetable);
        return -1;
    }

    argbuf = NULL;
    argc = 0;
    pos = 0;
    if (uargv != 0) {
        argbuf = kpage_alloc();
        if (argbuf == NULL) {
            goto bad;
        }
        for (argc = 0; argc < MAXARG; argc++) {
            if (copyin(&uarg, uargv + (uint64)argc * sizeof(uintptr),
                sizeof(uarg)) < 0) {
                goto bad;
            }
            if (uarg == 0) {
                break;
            }
            slen = copyinstr(argbuf + pos, uarg, PGSIZE - pos);
            if (slen < 0) {
                goto bad;
            }
            pos += (uint64)slen + 1;
        }
        if (argc == MAXARG) {
            goto bad;
        }
    }

    /*
     * Lay out the new user stack, writing through the stack pages's KVA.
     * It maps to user VA. argv strings at the top, then the info block.
     * (argc, argv[], NULL, envp NULL, auxv AT_NULL) with SP pointing at argc.
     * argv pointer values are re-derived by walking the packed string.
     */

    strbase = (new_sz + PGSIZE - pos) & ~0xfUL;
    if (pos != 0) {
        memcpy(stack + (strbase - new_sz), argbuf, pos);
    }

    /*
     * Info-block words: the argc argv pointers, plus 5 fixed words:
     * argc + the argv NULL, the envp NULL, and the auxv AT_NULL pair.
     */
    infosz = ((uint64)argc + 5) * sizeof(uint64);           /* argc + argv + NULL + envp + auxv */
    sp = (strbase - infosz) & ~0xfUL;
    if (sp < new_sz) {
        goto bad;
    }

    w = (uint64 *)(stack + (sp - new_sz));
    w[0] = (uint64)argc;
    off = 0;
    for (i = 0; i < argc; i++) {
        w[1 + i] = (uint64)(strbase + off);
        off += (uint64)strlen(argbuf + off) + 1;
    }
    w[1 + argc] = 0;            /* argv[argc] = NULL */
    w[2 + argc] = 0;            /* envp[0] = NULL */
    w[3 + argc] = 0;            /* auxv: AT_NULL type */
    w[4 + argc] = 0;            /* auxv: value 0 */

    if (argbuf != NULL) {
        kpage_free(argbuf);
    }

    /* Phase 2 */
    old_pagetable = p->pagetable;
    p->pagetable = new_pagetable;
    p->sz = new_sz + PGSIZE;

    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->sp = sp;
    p->tf->elr = entry;
    p->tf->spsr = 0x0;

    /* p->name = basename of path (last component after the final '/') */
    base = path;
    for (q = path; *q != '\0'; q++) {
        if (*q == '/') {
            base = q + 1;
        }
    }
    strncpy(p->name, base, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';

    /*
     * Switch TTBR0 to the new pagetable, then invalidate this ASID's
     * cached TLB entries before freeing the old pagetable's pages
     * (exec keeps the same ASID, so old translations would otherwise
     * alias the freed pages).
     */

    switchuvm(asid_num(p), p->pagetable);
    asid_flush(p);
    uvmfree(old_pagetable);

    return 0;

bad:
    /* The stack page is already mapped into new_pagetable, so uvmfree frees it. */
    if (argbuf != NULL) {
        kpage_free(argbuf);
    }
    uvmfree(new_pagetable);
    return -1;
}
