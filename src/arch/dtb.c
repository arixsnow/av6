/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * dtb.c
 *
 * Extracts only what is needed: CPU count and Memory size.
 * All integers in FDT are big-endian.
 *
 * DTB structure:
 *      Header (magic, offsets to struct/string blocks)
 *      Structure block (nested BEGIN_NODE/END_NODE/PROP tokens)
 *      Strings block (property name strings)
 */

#include "arch/arm64.h"
#include "sys/types.h"
#include "arch/dtb.h"
#include "arch/platform.h"
#include "arch/memlayout.h"
#include "sys/param.h"
#include "vm/physmem.h"
#include "sys/string.h"
#include "sys/kio.h"

/* FDT magic and tokens */
#define FDT_MAGIC           0xD00DFEED
#define FDT_BEGIN_NODE      0x00000001
#define FDT_END_NODE        0x00000002
#define FDT_PROP            0x00000003
#define FDT_NOP             0x00000004
#define FDT_END             0x00000009

#define FDT_FIRST_SUPPORTED_VERSION     0x02
#define FDT_LAST_SUPPORTED_VERSION      0x11

/* Runtime hardware parameters */
int ncpus;
int psci_conduit;
uint64 timer_freq_dtb;
struct platform platform;
uint64 cpu_mpidr[NCPU_MAX];

/* Big-endian to little-endian */
static uint32 fdt32(uint32 v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8)
        | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

static uint64 fdt64(uint32 *cells)
{
    return ((uint64)fdt32(cells[0]) << 32) | fdt32(cells[1]);
}

/* Read an n-cell (n=1 or 2) big-endian quantity. */
static uint64 fdt_cells(uint32 *cells, int n)
{
    if (n == 1) {
        return fdt32(*cells);
    }

    if (n == 2) {
        return fdt64(cells);
    }

    panic("dtb: unsupported cell count %d", n);
}

/* Round up to 4-byte boundary */
static uint32 align4(uint32 v)
{
    return (v + 3) & ~3;
}

static int startswith(const char *str, const char *prefix)
{
    while (*prefix) {
        if (*str++ != *prefix++)
            return 0;
    }
    return 1;
}

/*
 * Node name comes from the blob, so the terminator may not be there. Returns
 * the length including the NUL, or 0 if name runs past the block.
 */
static uint32 fdt_namelen(const char *name, const char *end)
{
    const char *s = name;

    while (s < end && *s != '\0') {
        s++;
    }

    return (s < end) ? (uint32)(s - name) + 1 : 0;
}

void dtb_init(void *dtb)
{
    uint64 boot_mpidr, mpidr, base, size;
    uint32 *header, *p, token, len, nameoff, *struct_end;
    uint32 off_struct, off_strings, namelen, off, ec;
    uint32 totalsize, version, size_struct, size_strings;
    char *strings, *name, *propname, *strings_end;
    int depth, in_cpus, in_memory, in_psci, in_timer, in_uart, in_gic;
    int in_cpu, boot_found, cpu_overflow, k;
    int root_acells, root_scells, nbanks;

    header = (uint32 *)dtb;

    if (fdt32(header[0]) != FDT_MAGIC) {
        panic("dtb: no valid device tree (bad FDT magic");
    }

    version = fdt32(header[5]);
    if (version < FDT_FIRST_SUPPORTED_VERSION
        || fdt32(header[6]) > FDT_LAST_SUPPORTED_VERSION) {
        panic("dtb: unsupported format version %u", version);
    }

    totalsize = fdt32(header[1]);
    off_struct = fdt32(header[2]);
    off_strings = fdt32(header[3]);
    size_struct = fdt32(header[9]);
    size_strings = fdt32(header[8]);

    if (off_struct + size_struct < off_struct
        || off_struct + size_struct > totalsize
        || off_strings + size_strings < off_strings
        || off_strings + size_strings > totalsize) {
        panic("dtb: block outside the %u-byte blob", totalsize);
    }

    strings = (char *)dtb + off_strings;
    strings_end = strings + size_strings;
    p = (uint32 *)((char *)dtb + off_struct);
    struct_end = (uint32 *)((char *)dtb + off_struct + size_struct);

    boot_mpidr = read_mpidr();
    cpu_mpidr[0] = boot_mpidr;
    ncpus = 1;
    boot_found = 0;
    cpu_overflow = 0;

    root_acells = 2;
    root_scells = 1;
    nbanks = 0;
    depth = 0;
    in_cpus = 0;
    in_memory = 0;
    in_psci = 0;
    in_timer = 0;
    in_uart = 0;
    in_gic = 0;
    in_cpu = 0;

    while (true) {
        if (p >= struct_end) {
            panic("dtb: structure block ends with FDT_END");
        }

        token = fdt32(*p++);

        switch (token) {
            case FDT_BEGIN_NODE:
                name = (char *)p;
                namelen = fdt_namelen(name, (char *)struct_end);

                if (namelen == 0) {
                    panic("dtb: unterminated node name");
                }

                p += align4(namelen) >> 2;
                depth++;

                if (depth == 2 && startswith(name, "cpus")) {
                    in_cpus = 1;
                }

                if (depth == 3 && in_cpus && startswith(name, "cpu@")) {
                    in_cpu = 1;
                }

                if (depth == 2 && (strcmp(name, "memory") == 0
                    || startswith(name, "memory@"))) {
                    in_memory = 1;
                }

                if (depth == 2 && startswith(name, "psci")) {
                    in_psci = 1;
                }

                if (depth == 2 && startswith(name, "timer")) {
                    in_timer = 1;
                }

                if (depth == 2 && startswith(name, "pl011")) {
                    in_uart = 1;
                }

                if (depth == 2 && startswith(name, "intc")) {
                    in_gic = 1;
                }

                break;
            case FDT_END_NODE:
                if (depth == 3) {
                  in_cpu = 0;
                } else if (depth == 2) {
                    in_cpus = 0;
                    in_memory = 0;
                    in_psci = 0;
                    in_timer = 0;
                    in_uart = 0;
                    in_gic = 0;
                }
                depth--;
                break;
            case FDT_PROP:
                if (p + 2 > struct_end) {
                    panic("dtb: truncated property header");
                }

                len = fdt32(*p++);
                nameoff = fdt32(*p++);

                if (strings + nameoff >= strings_end) {
                    panic("dtb: property name outside the strings block");
                }

                propname = strings + nameoff;

                if ((char *)p + align4(len) > (char *)struct_end
                    || (char *)p + align4(len) < (char *)p) {
                    panic("dtb: property value outside the structure block");
                }

                if (depth == 1 && (strcmp(propname, "#address-cells") == 0
                    && len >= 4)) {
                    root_acells = (int)fdt32(*p);
                }

                if (depth == 1 && strcmp(propname, "#size-cells") == 0
                    && len >= 4) {
                    root_scells = (int)fdt32(*p);
                }

                /*
                 * Memory reg: (base, size) pairs, root #address-cells /
                 * #size-cells wide. Several pairs, or several /memory nodes,
                 * mean several DRAM banks.
                 */
                if (in_memory && strcmp(propname, "reg") == 0) {
                    ec = (uint32)(root_acells + root_scells);
                    for (off = 0; (off + ec) * 4 <= len; off += ec) {
                        base = fdt_cells(p + off, root_acells);
                        size = fdt_cells(p + off + root_acells, root_scells);
                        if (size > 0) {
                            physmem_hardware_region(base, size);
                            nbanks++;
                        }
                    }
                }

                if (in_cpu && strcmp(propname, "reg") == 0 && len >= 4) {
                    mpidr = (len == 4) ? fdt32(*p) : fdt64(p);

                    if (mpidr == boot_mpidr) {
                        if (boot_found) {
                            panic("dtb: duplicate CPU MPIDR");
                        }
                        boot_found = 1;
                    } else {
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

                if (in_psci && strcmp(propname, "method") == 0) {
                    if (strcmp((char *)p, "smc") == 0) {
                        psci_conduit = PSCI_CONDUIT_SMC;
                    } else {
                        psci_conduit = PSCI_CONDUIT_HVC;
                    }
                }

                if (in_timer && strcmp(propname, "clock-frequency") == 0 && len >= 4) {
                    timer_freq_dtb = fdt32(*p);
                }

                if (in_uart && strcmp(propname, "reg") == 0 && len >= 16) {
                    platform.uart_base = fdt64(p);
                }

                if (in_gic && strcmp(propname, "reg") == 0 && len >= 32) {
                    platform.gicd_base = fdt64(p);
                    platform.gicr_base = fdt64(p + 4);
                }

                p += align4(len) >> 2;
                break;
            case FDT_NOP:
                break;
            case FDT_END:
                goto done;
            default:
                printk("dtb: unknown token: %#x\n", token);
                goto done;
        }
    }
done:
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

    printk("dtb: %d CPUs\n", ncpus);
    physmem_print();

    printk("dtb: PSCI conduit = %s\n",
        psci_conduit == PSCI_CONDUIT_SMC ? "smc" : "hvc");

    printk("platform: uart=%p gicd=%p gicr=%p\n",
        (void *)platform.uart_base, (void *)platform.gicd_base,
        (void *)platform.gicr_base);

    if (platform.uart_base != UART0_BASE_PA
        || platform.gicd_base != GICD_BASE_PA
        || platform.gicr_base != GICR_BASE_PA) {
        printk("platform: WARNING parsed bases != QEMU virt constants\n");
    }
}
