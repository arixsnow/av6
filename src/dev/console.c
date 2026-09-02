/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * console.c - console input layer
 *
 * Sits on top of uart.c. Handles line editing (backspace, ctrl+U),
 * echo, and input buffering. Output goes straight to uart_putc.
 */

#include "arch/arm64.h"
#include "dev/console.h"
#include "dev/uart.h"
#include "sys/fmt.h"
#include "sys/intr.h"
#include "sys/kio.h"
#include "sys/kmsg.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/spinlock.h"
#include "sys/types.h"

/*
 * Short History lesson:
 *
 * When you hold CTRL and press a key, the terminal doesn't set the letter,
 * it sends the letter minus 64 (which is '@'). This is a hardware convention
 * from the 1960s. The terminal literally clears bit 6, that's what the CTRL
 * key does electrically. 'A' is 0100 0001, clear bit 6 -> 0000 0001
 */
#define CTRL(x)         ((x) - '@')

#define INPUT_BUF_SIZE  128

static struct spinlock cons_lock;           /* input ring */
static struct spinlock cons_out_lock;       /* the port, and cons_seq */
static uint64 cons_seq;                     /* next record to render: cons_out_lock */

static struct {
    char buf[INPUT_BUF_SIZE];
    uint64 r;       /* read index */
    uint64 w;       /* write index (committed lines) */
    uint64 e;       /* edit index (current typing position) */
} cons;

void console_write(const char *buf, uint64 n)
{
    uint64 i;

    acquire_spinlock(&cons_out_lock);

    for (i = 0; i < n; i++) {
        uart_putc(buf[i]);
    }

    release_spinlock(&cons_out_lock);
}

/* Caller holds cons_out_lock */
static void console_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

static const char *level_tag(int level)
{
    switch (level) {
        case KERN_EMERG:
            return "EMERG:  ";
        case KERN_ALERT:
            return "ALERT:  ";
        case KERN_CRIT:
            return "CRIT:  ";
        case KERN_ERR:
            return "ERROR:  ";
        case KERN_WARNING:
            return "WARNING:  ";
        default:
            return "";
    }
}

/* Render one record as "[sec.usec] tag text". Caller holds cons_out_lock */
static void console_render(const struct kmsg_desc *d, const char *text)
{
    char hdr[32];
    uint64 f;
    uint16 i;

    f = read_cntfrq();
    if (f != 0) {
        snprintf(hdr, sizeof(hdr), "[%5lu.%06lu] ",
            d->ts / f, ((d->ts % f) * USEC_PER_SEC) / f);
        console_puts(hdr);
    }

    console_puts(level_tag(d->level));

    for (i = 0; i < d->len; i++) {
        uart_putc(text[i]);
    }
}

static void console_dropped(uint64 n)
{
    char msg[64];

    snprintf(msg, sizeof(msg), "** %lu messages dropped **\n", n);
    console_puts(msg);
}

static void console_drain(uint64 end)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 first;

    while (cons_seq < end) {
        if (kmsg_read(cons_seq, &d, buf) < 0) {
            first = kmsg_first_seq();
            if (cons_seq < first) {
                console_dropped(first - cons_seq);
                cons_seq = first;
                continue;
            }
            break;
        }

        console_render(&d, buf);
        cons_seq++;
    }
}

/* Lock order: cons_out_lock -> kmsg_lock */
void console_flush_upto(uint64 seq)
{
    if (seq == KMSG_NO_SEQ) {
        return;
    }

    acquire_spinlock(&cons_out_lock);
    console_drain(seq + 1);
    release_spinlock(&cons_out_lock);
}

/* Lock order: cons_out_lock -> kmsg_lock */
void console_flush(void)
{
    acquire_spinlock(&cons_out_lock);
    console_drain(kmsg_next_seq());
    release_spinlock(&cons_out_lock);
}

/*
 * Render everything still held, oldest first. cons_seq is left alone: a replay
 * is not a flush, and what has already gone out stays out.
 *
 * Lock order: cons_out_lock -> kmsg_lock
 */
void console_replay(void)
{
    struct kmsg_desc d;
    char buf[KMSG_RECORD_MAX];
    uint64 seq, first, last;

    acquire_spinlock(&cons_out_lock);

    seq = kmsg_first_seq();
    last = kmsg_next_seq();

    while (seq < last) {
        if (kmsg_read(seq, &d, buf) < 0) {
            first = kmsg_first_seq();
            if (seq < first) {
                seq = first;
                continue;
            }
            break;
        }
        console_render(&d, buf);
        seq++;
    }

    release_spinlock(&cons_out_lock);
}

static void console_echo(char c)
{
    if (c == '\b') {
        console_write("\b \b", 3);
    } else {
        console_write(&c, 1);
    }
}

void console_init(void)
{
    cons.r = 0;
    cons.w = 0;
    cons.e = 0;
    uart_init();
    init_spinlock(&cons_lock, "cons");
    init_spinlock(&cons_out_lock, "cons_out");
}

/*
 * Called from trap_irq() when UART interrupt fires.
 * Drains all available characters from the UART
 */

void console_intr(void)
{
    int c;

    acquire_spinlock(&cons_lock);

    while ((c = uart_getc()) >= 0) {
        switch (c) {
            case CTRL('U'):
                /* kill line, erase back to start of input */
                while (cons.e != cons.w
                    && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
                    cons.e--;
                    console_echo('\b');
                }
                break;
            case CTRL('H'):
            case '\x7f':
                /* backspace */
                if (cons.e != cons.w) {
                    cons.e--;
                    console_echo('\b');
                }
                break;
            default:
                if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
                    c = (c == '\r') ? '\n' : c;

                    console_echo(c);

                    cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

                    if (c == '\n' || c == CTRL('D')
                        || cons.e - cons.r == INPUT_BUF_SIZE) {
                        /*
                         * Complete line ready. Advance write index.
                         */
                        cons.w = cons.e;
                        wakeup(&cons.r);
                    }
                }
                break;
        }
    }

    release_spinlock(&cons_lock);
}

int console_filter(void)
{
    /*
     * Hardirq half: the GIC already identified the UART, so there is
     * nothing to check here. Defer the FIFO drain to the UART ithread,
     * which runs console_intr() in thread context.
     */
    return FILTER_SCHEDULE_THREAD;
}

uint64 console_read(char *dst, uint64 n)
{
    int c;
    uint64 target;

    target = n;

    acquire_spinlock(&cons_lock);

    while (n > 0) {
        /* Wait until a complete line is available */
        while (cons.r == cons.w) {
            if (killed(myproc())) {
                release_spinlock(&cons_lock);
                return -1;
            }
            sleep(&cons.r, &cons_lock);
        }

        c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

        if (c == CTRL('D')) {
            if (n < target) {
                /* Save ^D for next time, so caller gets 0-byte result */
                cons.r--;
            }
            break;
        }

        *dst++ = c;
        n--;

        if (c == '\n') {
            break;
        }
    }

    release_spinlock(&cons_lock);

    return target - n;
}
