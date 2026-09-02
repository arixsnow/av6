/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * AV6 AArch64 linker script for QEMU virt machine
 *
 * QEMU loads the binary at 0x40080000.
 *
 * The kernel is linked at high VA (0xFFFF000040080000) so it runs
 * through TTBR1 after MMU enable. The AT() directive keeps the
 * load address at the physical address.
 *
 * VMA : 0xFFFF000040080000
 * LMA : 0x40080000
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY (_start)

KERN_BASE = 0xFFFF000000000000;
KERN_LINK = KERN_BASE + 0x40080000;
PHYS_LINK = 0x40080000;

SECTIONS
{
    . = KERN_LINK;
    PROVIDE(stext = .);

    .text.boot : AT(PHYS_LINK) {
        KEEP(*(.text.boot))
    }

    .text : AT(PHYS_LINK + (ADDR(.text) - KERN_LINK)) {
        *(.text .text.*)
    }

    PROVIDE(etext = .);

    .rodata : AT(PHYS_LINK + (ADDR(.rodata) - KERN_LINK)) {
        *(.rodata .rodata.*)

        /* Test-case registry. Empty unless built with KTEST=1 */
        . = ALIGN(8);
        PROVIDE(__ktest_start = .);
        KEEP(*(.ktest))
        PROVIDE(__ktest_end = .);
    }

    .data : AT(PHYS_LINK + (ADDR(.data) - KERN_LINK)) {
        *(.data .data.*)
    }

    PROVIDE(edata = .);

    .bss (NOLOAD) : AT(PHYS_LINK + (ADDR(.bss) - KERN_LINK)) {
        PROVIDE(__bss_start = .);
        *(COMMON)
        *(.bss .bss.*)
        PROVIDE(__bss_end = .);
    }

    /*
     * Per-CPU kernel (boot/idle) stacks. Each CPU gets a 32 KB slot, 32 KB-aligned.
     * NCPU_MAX arrives via --defsym so this tracks the C-side array bound.
     */
    . = ALIGN(0x8000);
    .stack (NOLOAD) : {
        PROVIDE(_stack_base = .);
        . += 0x8000 * NCPU_MAX;
        PROVIDE(_stack_top = .);
    }

    /* end marks the start of free RAM for kalloc.*/

    PROVIDE(end = .);
}
