/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * kmsg.h - kernel message ring
 *
 * Two rings: descriptors carry the metadata, a byte ring carries the text, so
 * a record costs its own length and nothing is truncated or padded. Level and
 * timestamp are metadata, not text, so a reader can filter and re-render.
 *
 * Both rings are static. Logging runs before any allocator exists, which is
 * what justifies the compile-time size.
 */

#ifndef _AV6_KMSG_H_
#define _AV6_KMSG_H_

#include "sys/types.h"

/*
 * TEXTSZ is the number to tune. NDESC follows from it
 */
#define KMSG_TEXTSZ         (64 * 1024)
#define KMSG_NDESC          1024
#define KMSG_RECORD_MAX     256

#define KMSG_NO_SEQ         (~0UL)      /* kmsg_add() stored nothing */
#define KMSG_F_FINAL        0x01        /* no KERN_CONT can extend it */

#define KMSG_CALLER_CPU     0x80000000U /* no proc: the id is a CPU number */

/*
 * off/len address the text ring, which wraps, so a record's text is not
 * necessarily contiguous. kmsg_read() reassembles it.
 */
struct kmsg_desc {
    uint64 seq;             /* monotonic, never reused */
    uint64 ts;              /* CNTPCT ticks at record time */
    uint32 off;             /* start of text in the ring */
    uint32 caller;          /* only this caller may continue the record */
    uint16 len;             /* <= KMSG_RECORD_MAX */
    uint8 level;            /* KERN_* */
    uint8 flags;            /* KMSG_F_* */
};

void kmsg_init(void);

/*
 * Append a record, dropping the oldest until both rings have room. Text past
 * KMSG_RECORD_MAX is truncated. Returns the seq now holding the text, or
 * KMSG_NO_SEQ if nothing was stored.
 *
 * KERN_CONT appends to the previous record instead of starting a new one, so a
 * continuation is not a second lock acquisition another CPU can interleave with.
 * A record becomes final when its text ends in a newline or when a later record
 * starts, and no reader sees it before then, so a line reaches the port whole.
 */
uint64 kmsg_add(int level, const char *text, uint16 len);

/* Close the open record, if any. Nothing is left to continue it. */
void kmsg_finalize(void);

/*
 * Copy record seq into out/buf. Returns the text length, or -1 if that record
 * is unavailable, overwritten, or not yet final. buf must hold KMSG_RECORD_MAX
 * bytes.
 */
int kmsg_read(uint64 seq, struct kmsg_desc *out, char *buf);

/* Sequence range currently held: [first, next) */
uint64 kmsg_first_seq(void);
uint64 kmsg_next_seq(void);

#endif      /* _AV6_KMSG_H_ */
