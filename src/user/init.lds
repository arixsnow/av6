/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * init.lds : linker script for user programs.
 * Layout:
 *      VA 0x00000000 - 0x0000FFFF: unmapped (null guard)
 *      VA 0x00010000 -     ~     : text + rodata + data + bss
 *
 * The 64 KB null guard means a NULL deref takes a page fault
 * instead of a silent read.
 *
 * Single PT_LOAD segment for the whole program : .text + .rodata
 * + .data + .bss collapsed. That keep the ELF small and the
 * loader straight. Page-aligned phys/virt so the loader can map
 * a clean PGSIZE region.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(0x1000) {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(8) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(8) {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(8) {
        *(.bss)
        *(.bss.*)
    }

    /DISCARD/ : {
        *(.note.*)
        *(.comment*)
        *(.eh_frame*)
        *(.ARM.attributes)
    }
}
