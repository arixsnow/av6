/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * kmsg.c - kernel message ring
 *
 * Descriptors and text blocks are allocated in order and retired from the tail
 * in order, so desc[dtail] always owns the bytes at ttail. Nothing records
 * which text belongs to which record: the answer is always the front of both
 * queues, which is why reclaim is two assignments and a read needs no ownership
 * check. That only holds while a single writer holds kmsg_lock.
 *
 * seq is monotonic and never reused, so a reader that falls behind can tell
 * "overwritten" from "nothing new".
 */

#include "arch/arm64.h"
#include "arch/cpu.h"
#include "dev/uart.h"
#include "sys/kio.h"
#include "sys/kmsg.h"
#include "sys/proc.h"
#include "sys/spinlock.h"
#include "sys/types.h"

static struct spinlock kmsg_lock;

static struct kmsg_desc desc[KMSG_NDESC];
static char textbuf[KMSG_TEXTSZ];

static uint32 dhead;        /* next descriptor to fill */
static uint32 dtail;        /* oldest live descriptor */
static uint32 ndesc;        /* live descriptors, dhead == dtail is ambiguous */
static uint32 thead;        /* next free byte */
static uint32 ttail;        /* oldest live byte */
static uint32 tused;
static uint64 next_seq;

void kmsg_init(void)
{
    init_spinlock(&kmsg_lock, "kmsg");
}

/* Retire the oldest record. Caller holds kmsg_lock and has checked ndesc. */
static void kmsg_drop_oldest(void)
{
    ttail = (ttail + desc[dtail].len) % KMSG_TEXTSZ;
    tused -= desc[dtail].len;
    dtail = (dtail + 1) % KMSG_NDESC;
    ndesc--;
}

/* Copy into the text ring at off, wrapping. Caller holds kmsg_lock */
static void kmsg_write_text(uint32 off, const char *src, uint16 len)
{
    for (uint16 i = 0; i < len; i++) {
        textbuf[(off + i) % KMSG_TEXTSZ] = src[i];
    }
}

/* Newest record, or NULL if the ring is empty. Caller holds kmsg_lock */
static struct kmsg_desc *kmsg_last(void)
{
    if (ndesc == 0) {
        return NULL;
    }

    return &desc[(dhead + KMSG_NDESC - 1) % KMSG_NDESC];
}

static uint32 kmsg_caller(struct cpu *c)
{
    return (c->proc != NULL) ? (uint32)c->proc->pid
                             : (KMSG_CALLER_CPU | (uint32)cpuid());
}

/*
 * Extend the newest record instead of starting one. Returns NULL if it will not
 * fit, or is already closed, in which case the caller starts a fresh record
 * rather than truncating mid-line. Caller holds kmsg_lock.
 */
static struct kmsg_desc *kmsg_append_last(uint32 caller, const char *text, uint16 len)
{
    struct kmsg_desc *d;

    d = kmsg_last();
    if (d == NULL || (d->flags & KMSG_F_FINAL) != 0 || d->caller != caller) {
        return NULL;
    }

    if (d->len + len > KMSG_RECORD_MAX || tused + len > KMSG_TEXTSZ) {
        return NULL;
    }

    kmsg_write_text(thead, text, len);
    thead = (thead + len) % KMSG_TEXTSZ;
    tused += len;
    d->len += len;

    if (text[len - 1] == '\n') {
        d->flags |= KMSG_F_FINAL;
    }

    return d;
}

/*
 * Straight to the port, no ring and no lock. Reached only when this CPU is
 * already inside kmsg_add, where taking kmsg_lock again would spin forever on a
 * lock we hold ourselves. Output may interleave. The alternative is a hang
 */
static void kmsg_bypass(const char *text, uint16 len)
{
    for (uint16 i = 0; i < len; i++) {
        uart_putc(text[i]);
    }
}

uint64 kmsg_add(int level, const char *text, uint16 len)
{
    struct cpu *c;
    struct kmsg_desc *d;
    uint64 seq;
    uint32 caller;

    if (len > KMSG_RECORD_MAX) {
        len = KMSG_RECORD_MAX;
    }

    if (len == 0) {
        return KMSG_NO_SEQ;
    }

    push_off();
    c = &cpus[cpuid()];

    caller = kmsg_caller(c);

    if (c->in_kmsg) {
        kmsg_bypass(text, len);
        pop_off();
        return KMSG_NO_SEQ;
    }

    c->in_kmsg = 1;
    acquire_spinlock(&kmsg_lock);

    if (level == KERN_CONT && (d = kmsg_append_last(caller, text, len)) != NULL) {
        seq = d->seq;
        goto done;
    }

    d = kmsg_last();
    if (d != NULL) {
        d->flags |= KMSG_F_FINAL;
    }

    while (ndesc == KMSG_NDESC || tused + len > KMSG_TEXTSZ) {
        kmsg_drop_oldest();
    }

    d = &desc[dhead];
    d->seq = next_seq;
    d->ts = read_cntpct();
    d->off = thead;
    d->caller = caller;
    d->len = len;
    d->level = (level == KERN_CONT) ? KERN_INFO : level;
    d->flags = (text[len - 1] == '\n') ? KMSG_F_FINAL : 0;

    kmsg_write_text(thead, text, len);
    thead = (thead + len) % KMSG_TEXTSZ;
    tused += len;
    dhead = (dhead + 1) % KMSG_NDESC;
    ndesc++;
    next_seq++;

    seq = d->seq;
done:
    release_spinlock(&kmsg_lock);
    c->in_kmsg = 0;
    pop_off();

    return seq;
}

void kmsg_finalize(void)
{
    struct kmsg_desc *d;

    acquire_spinlock(&kmsg_lock);

    d = kmsg_last();
    if (d != NULL) {
        d->flags |= KMSG_F_FINAL;
    }

    release_spinlock(&kmsg_lock);
}

int kmsg_read(uint64 seq, struct kmsg_desc *out, char *buf)
{
    struct kmsg_desc *d;
    uint32 i, idx;
    int len;

    acquire_spinlock(&kmsg_lock);

    if (ndesc == 0 || seq < desc[dtail].seq || seq >= next_seq) {
        release_spinlock(&kmsg_lock);
        return -1;
    }

    idx = (dtail + (uint32)(seq - desc[dtail].seq)) % KMSG_NDESC;
    d = &desc[idx];

    if ((d->flags & KMSG_F_FINAL) == 0) {
        release_spinlock(&kmsg_lock);
        return -1;
    }

    for (i = 0; i < d->len; i++) {
        buf[i] = textbuf[(d->off + i) % KMSG_TEXTSZ];
    }

    *out = *d;
    len = d->len;

    release_spinlock(&kmsg_lock);

    return len;
}

uint64 kmsg_first_seq(void)
{
    uint64 seq;

    acquire_spinlock(&kmsg_lock);

    seq = (ndesc == 0) ? next_seq : desc[dtail].seq;

    release_spinlock(&kmsg_lock);

    return seq;
}

uint64 kmsg_next_seq(void)
{
    uint64 seq;

    acquire_spinlock(&kmsg_lock);

    seq = next_seq;

    release_spinlock(&kmsg_lock);

    return seq;
}
