# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# src/user/user.mk - AV6 userland build, included by the project-root Makefile
#
# Output : $(BUILDDIR)/user/blobs.o - every program's ELF embedded via .incbin, exporting
# <name>_elf_start / <name>_elf_end, which the kernel references in exec.c.
# Syscall numbers come from include/sys/syscall.h via the generated syscall_nr.s.
#
# Adding a program: drop src/user/<name>.s in and add <name> to PROGS.

# -nostdlib : crt0/libc ; -ffreestanding : not hosted ; -static : no dynamic linker
UASFLAGS := -nostdlib -ffreestanding -static -c
ULDFLAGS := -static -nostdlib

# The only line to edit when adding a program
PROGS := init argvtest faulttest

USER_BUILD := $(BUILDDIR)/user
USER_SYSCALL_NR := $(USER_BUILD)/syscall_nr.s
UOBJS := $(PROGS:%=$(USER_BUILD)/%.o)
UELFS := $(PROGS:%=$(USER_BUILD)/%.elf)
USER_BLOBS_S := $(USER_BUILD)/blobs.s
USER_BLOB := $(USER_BUILD)/blobs.o

$(USER_SYSCALL_NR) : $(INCLDIR)/sys/syscall.h $(USERDIR)/user.mk
	@mkdir -p $(USER_BUILD)
	$(E) '  GEN       $@'
	@echo '/* generated from syscall.h - do not edit */' > $@
	@sed -n 's/^#define[[:space:]]\{1,\}\(SYS_[A-Za-z0-9_]*\)[[:space:]]\{1,\}\([0-9]\{1,\}\).*/.equ \1, \2/p' $< >> $@
	@echo '.equ AV6_KTEST, $(if $(KTEST),1,0)' >> $@

$(UOBJS) : $(USER_BUILD)/%.o : $(USERDIR)/%.s $(USER_SYSCALL_NR)
	@mkdir -p $(USER_BUILD)
	$(E) '  AS        $@'
	$(Q)$(CC) $(UASFLAGS) -I $(USER_BUILD) $< -o $@

$(UELFS) : $(USER_BUILD)/%.elf : $(USER_BUILD)/%.o $(USERDIR)/init.lds
	$(E) '  LD        $@'
	$(Q)$(LD) $(ULDFLAGS) -T $(USERDIR)/init.lds -o $@ $<

# Generate the embed stub form PROGS. .incbin reads the ELFs at assemble
# time, so blobs.o depends on every .elf
$(USER_BLOBS_S) : $(USERDIR)/user.mk
	@mkdir -p $(USER_BUILD)
	$(E) '  GEN       $@'
	@echo '/* generated form PROGS - do not edit */' > $@
	@echo '.section .rodata' >> $@
	@for p in $(PROGS); do \
	    echo ".balign 8" >> $@;	\
		echo ".global $${p}_elf_start, $${p}_elf_end" >> $@; \
		echo "$${p}_elf_start:" >> $@; \
		echo "		.incbin \"$${p}.elf\"" >> $@; \
		echo "$${p}_elf_end:" >> $@;  \
	done

# -I $(USER_BUILD) SO .incbin "<name>.elf" finds the linked ELFs
$(USER_BLOB) : $(USER_BLOBS_S) $(UELFS)
	$(E) '  AS        $@'
	$(Q)$(CC) $(UASFLAGS) -I $(USER_BUILD) $(USER_BLOBS_S) -o $@
