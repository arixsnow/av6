/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_ELF_H_
#define _AV6_ELF_H_

#include "sys/types.h"

/*
 * elf.h - ELF64 binary format constants and structures (AArch64 subset)
 *
 * Subst of the System V ABI Generic ELF + AArch64 ABI specs needed
 * for loading static executables. Only the fields and constants, which are
 * actually inspected by the kernel, are defined to validate an image is
 * "ELF64 little-endian AArch64 ET_EXEC" and to walk its program-header table
 */

/*
 * struct elfhdr : ELF64 file header
 *
 * `ident` is the spec's 16-byte e_ident[]. Indexes are EI_*.
 * Magic bytes occupy ident[0..3]; check via ELFMAG0..ELFMAG3.
 */
struct elfhdr {
    uchar ident[16];
    uint16 type;        /* e_type : ET_EXEC etc. */
    uint16 machine;     /* e_machine : EM_AARCH64 */
    uint32 version;
    uint64 entry;       /* virtual address of entry point */
    uint64 phoff;       /* program header table file offset */
    uint64 shoff;       /* section header table file offset */
    uint32 flags;
    uint16 ehsize;
    uint16 phentsize;   /* per-entry size in PHT */
    uint16 phnum;       /* number of entries in PHT */
    uint16 shentsize;
    uint16 shnum;
    uint16 shstrndx;
};

/* e_ident[] indexes */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7

/* Magic bytes: ident[0..3] must be exactly these */
#define ELFMAG0         0x7F
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'

/* EI_CLASS values */
#define ELFCLASS32      1
#define ELFCLASS64      2

/* EI_DATA values */
#define ELFDATA2LSB     1       /* little-endian */
#define ELFDATA2MSB     2       /* big-endian */

/* e_type values */
#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2       /* executable file */
#define ET_DYN          3
#define ET_CORE         4

/* e_machine values */
#define EM_AARCH64      183     /* ARM 64-bit */

/*
 * struct proghdr : ELF64 program header
 *
 * One entry per loadable / informational segment. We act on PT_LOAD
 * entries; the rest (PT_GNU_STACK, PT_NOTE, PT_PHDR, ...) are skipped.
 */
struct proghdr {
    uint32 type;        /* p_type */
    uint32 flags;       /* p_flags : PF_X | PF_W | PF_R */
    uint64 off;         /* offset of segment in file */
    uint64 vaddr;       /* virtual address in memory */
    uint64 paddr;       /* physical address (irrelevant) */
    uint64 filesz;      /* bytes present in file */
    uint64 memsz;       /* bytes occupied in memory (>=filesz; BSS = memsz - filesz) */
    uint64 align;
};

/* p_type values */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         5
#define PT_GNU_STACK    0x6474E551U
#define PT_GNU_RELRO    0x6474E552U

/* p_flags : segment permissions (loadelf maps PAGE_USER for all) */
#define PF_X            (1U << 0)
#define PF_W            (1U << 1)
#define PF_R            (1U << 2)

#endif      /* _AV6_ELF_H_ */
