/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/stdarg.h"
#include "sys/fmt.h"
#include "sys/types.h"

#define FMT_ALT         0x01
#define FMT_ZERO        0x02
#define FMT_LONG        0x04
#define FMT_SIGN        0x08

struct fmt_spec {
    uint8 base;
    uint8 flags;
    int16 width;
};

static const char digits[] = "0123456789abcdef";

static char *emit_char(char *str, char *end, char c)
{
    if (str < end) {
        *str = c;
    }

    return str + 1;
}

static char *emit_str(char *str, char *end, const char *s)
{
    while (*s != '\0') {
        str = emit_char(str, end, *s++);
    }

    return str;
}

/* Sign and prefix count against the field width, as printf(3) has them. */
static char *emit_num(char *str, char *end, uint64 v, int neg, struct fmt_spec spec)
{
    char tmp[32];
    char pfx[2];
    int n, np, pad, i;

    n = 0;
    do {
        tmp[n++] = digits[v % spec.base];
    } while ((n < (int)sizeof(tmp)) && (v /= spec.base) != 0);

    np = 0;
    if (neg) {
        pfx[np++] ='-';
    } else if (spec.flags & FMT_ALT) {
        pfx[np++] = '0';
        if (spec.base == 16) {
            pfx[np++] = 'x';
        }
    }

    pad = spec.width - n - np;

    if ((spec.flags & FMT_ZERO) == 0) {
        while (pad-- > 0) {
            str = emit_char(str, end, ' ');
        }
    }

    for (i = 0; i < np; i++) {
        str = emit_char(str, end, pfx[i]);
    }

    if (spec.flags & FMT_ZERO) {
        while (pad-- > 0) {
            str = emit_char(str, end, '0');
        }
    }

    while (--n >= 0) {
        str = emit_char(str, end, tmp[n]);
    }

    return str;
}

static const char *decode(const char *fmt, struct fmt_spec *spec)
{
    int c;

    spec->flags = 0;
    spec->width = 0;
    spec->base = 10;

    while (true) {
        c = *fmt & 0xff;
        if (c == '#') {
            spec->flags |= FMT_ALT;
        } else if (c == '0') {
            spec->flags |= FMT_ZERO;
        } else {
            break;
        }
        fmt++;
    }

    while (*fmt >= '1' && *fmt <= '9') {
        spec->width = spec->width * 10 + (*fmt++ - '0');
    }

    if (*fmt == 'l') {
        spec->flags |= FMT_LONG;
        fmt++;
    }

    return fmt;
}

int vsnprintf(char *buf, uint64 size, const char *fmt, va_list ap)
{
    struct fmt_spec spec;
    char *str, *end;
    const char *s;
    uint64 v;
    int64 sv;
    int c, neg;

    str = buf;
    end = buf + size;

    while ((c = *fmt++ & 0xff) != '\0') {
        if (c != '%') {
            str = emit_char(str, end, c);
            continue;
        }

        fmt = decode(fmt, &spec);
        c = *fmt++ & 0xff;
        neg = 0;

        switch (c) {
            case 'd':
                sv = (spec.flags & FMT_LONG) ? va_arg(ap, int64) : (int64)va_arg(ap, int);
                neg = (sv < 0);
                v = neg ? (uint64)-sv : (uint64)sv;
                str = emit_num(str, end, v, neg, spec);
                break;
            case 'x':
                spec.base = 16;
                /* FALLTHROUGH */
            case 'o':
                if (c == 'o') {
                    spec.base = 8;
                }
                /* FALLTHROUGH */
            case 'u':
                v = (spec.flags & FMT_LONG) ? va_arg(ap, uint64) : (uint64)va_arg(ap, uint);
                str = emit_num(str, end, v, 0, spec);
                break;
            case 'p':
                spec.base = 16;
                spec.flags |= FMT_ALT | FMT_ZERO;
                spec.width = 18;
                str = emit_num(str, end, va_arg(ap, uint64), 0, spec);
                break;
            case 's':
                s = va_arg(ap, const char *);
                str = emit_str(str, end, (s == NULL) ? "(null)" : s);
                break;
            case 'c':
                str = emit_char(str, end, (char)va_arg(ap, int));
                break;
            case '%':
                str = emit_char(str, end, '%');
                break;
            default:
                str = emit_char(str, end, '%');
                str = emit_char(str, end, (char)c);
                break;
        }
    }

    if (size > 0) {
        *((str < end) ? str : end - 1) = '\0';
    }

    return (int)(str - buf);
}

int snprintf(char *buf, uint64 size, const char *fmt, ...)
{
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    return len;
}
