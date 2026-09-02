# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# copy_user.s - LDTR/STTR primitives for user memory access
#
# The kernel runs with PSTATE.PAN=1, so plain ldr/str against
# a user VA faults. LDTR/STTR are the "unprivileged" load/store
# variants. They translate via the active TTBR0_EL1, apply EL0
# access permissions, and bypass PAN. They are the only legitimate
# way the kernel touches user memory.
#
# Limitations of LDTR/STTR vs LDR/STR:
#       : No register offset addressing ([Xn, Xm] not allowed)
#       : No pre/post-index             ([Xn], #imm not allowed)
#       : No paired form                (no ldtp/sttp)
#       : Only [Xn, #imm9] with singed 9-bit immediate
# The user-side pointer is therefore advanced with explicit
# add instructions; the kernel-side pointer can still use
# normal post-index since it goes through plain str/ldr.
#
# Byte-at-a-time by design. The byte loop is right, and the only
# callers today (sys_write / sys_read) copy <= 128 bytes per call,
# so there is no hot consumer to justify a wider loop yet. Widen to
# 8-byte chunks with a byte tail when the filesystem brings page-sized
# copies.

# int _copyin_bytes(void *dst_kern, uintptr src_user, uint64 len)
#
# x0=kernel destination (plain strb target)
# x1=user source
# x2=byte count
# Returns: 0 in x0 on success

.global _copyin_bytes
_copyin_bytes:
    cbz         x2, 2f
1:
    ldtrb       w3, [x1]
    strb        w3, [x0], #1
    add         x1, x1, #1
    subs        x2, x2, #1
    b.ne        1b
2:
    mov         x0, #0
    ret
.size _copyin_bytes, . - _copyin_bytes

# int _copyout_bytes(uintptr dst_user, const void *src_kern, uint64 len)
#
# x0=user destination       (sttrb target)
# x1=kernel source          (plain ldrb target)
# x2=byte count
#
# Returns: 0 in x0 on success

.global _copyout_bytes
_copyout_bytes:
    cbz         x2, 2f
1:
    ldrb        w3, [x1], #1
    sttrb       w3, [x0]
    add         x0, x0, #1
    subs        x2, x2, #1
    b.ne        1b
2:
    mov         x0, #0
    ret
.size _copyout_bytes, . - _copyout_bytes

# int _copyinstr_bytes(char *dst_kern, uintptr src_user, uint64 max)
#
# x0 = kernel destination (strb target)
# x1 = user source
# x2 = max bytes (must be > 0 by caller; 0 returns -1)
#
# Returns:
#   x0 = string length (excluding NULL) on success
#   x0 = -1 if max bytes consumed without seeing NULL

.global _copyinstr_bytes
_copyinstr_bytes:
    cbz         x2, 3f
    mov         x4, x0          /* save original dst for length calc */
1:
    ldtrb       w3, [x1]
    strb        w3, [x0], #1
    add         x1, x1, #1
    cbz         w3, 2f          /* NULL: success */
    subs        x2, x2, #1
    b.ne        1b
3:
    mov         x0, #-1
    ret
2:
    sub         x0, x0, x4      /* dst_not - dst_orig = bytes copied (incl NULL) */
    sub         x0, x0, #1      /* exclude NULL from length */
    ret
.size _copyinstr_bytes, . - _copyinstr_bytes


# _copy_fault - fixup target for copyin/copyout/copyinstr.
#
# The EL1 data-abort handler redirects ELR_EL1 here when an LDTR/STTR faults
# while proc->onfault is set. The user registers have been restored, and x30
# still holds the C wrapper's return address (the copy loops never touch it).
.global _copy_fault
_copy_fault:
    mov         x0, #-1
    ret
.size _copy_fault, . - _copy_fault
