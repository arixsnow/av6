/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/fmt.h>
#include <arch/stdarg.h>
#include <sys/types.h>

#define FMT_LEFT      0x01
#define FMT_PLUS      0x02
#define FMT_SPACE     0x04
#define FMT_ALT       0x08
#define FMT_ZERO      0x10
#define FMT_UPPER     0x20
#define FMT_PREFIX    0x40

#define FMT_MAXDIGITS 22 /* 2^64 - 1 in octal */
#define FMT_PTRDIGITS 16 /* a 64-bit pointer in hex */

enum {
    BASE_OCT = 8,
    BASE_DEC = 10,
    BASE_HEX = 16,
};

enum {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_J,
    LEN_Z,
    LEN_T,
};

struct fmt_spec {
    int flags;
    int width;
    int prec;
    int len;
};

struct fmt_out {
    char *buf;
    uint64 size;
    uint64 len;
};

static void emit_char(struct fmt_out *o, char c)
{
    if (o->len < o->size) {
        o->buf[o->len] = c;
    }
    o->len++;
}

static void emit_pad(struct fmt_out *o, char c, int n)
{
    while (n-- > 0) {
        emit_char(o, c);
    }
}

static void emit_num(struct fmt_out *o, uint64 v, int neg, unsigned int base,
    const struct fmt_spec *spec)
{
    const char *dig;
    char tmp[FMT_MAXDIGITS], pfx[2];
    int n, np, zeros, pad, nonzero, zeropad;

    dig = (spec->flags & FMT_UPPER) ? "0123456789ABCDEF" : "0123456789abcdef";
    nonzero = (v != 0);
    zeropad = (spec->flags & FMT_ZERO) && spec->prec < 0;

    n = 0;
    if (nonzero || spec->prec != 0) {
        do {
            tmp[n++] = dig[v % base];
            v /= base;
        } while (v != 0);
    }

    zeros = (spec->prec > n) ? spec->prec - n : 0;
    if ((spec->flags & FMT_ALT)
        && base == BASE_OCT
        && zeros == 0
        && (n == 0 || tmp[n - 1] != '0')) {
        zeros = 1;
    }

    np = 0;
    if (neg) {
        pfx[np++] = '-';
    } else if (spec->flags & FMT_PLUS) {
        pfx[np++] = '+';
    } else if (spec->flags & FMT_SPACE) {
        pfx[np++] = ' ';
    } else if ((spec->flags & FMT_PREFIX)
        || ((spec->flags & FMT_ALT) && base == BASE_HEX && nonzero)) {
        pfx[np++] = '0';
        pfx[np++] = (spec->flags & FMT_UPPER) ? 'X' : 'x';
    }

    pad = spec->width - np - zeros - n;
    if ((spec->flags & FMT_LEFT) == 0 && !zeropad) {
        emit_pad(o, ' ', pad);
    }
    for (int i = 0; i < np; i++) {
        emit_char(o, pfx[i]);
    }
    if (zeropad) {
        emit_pad(o, '0', pad);
    }
    emit_pad(o, '0', zeros);
    while (n > 0) {
        emit_char(o, tmp[--n]);
    }
    if (spec->flags & FMT_LEFT) {
        emit_pad(o, ' ', pad);
    }
}

static void emit_field(struct fmt_out *o, const char *s, int n,
    const struct fmt_spec *spec)
{
    int pad;

    pad = spec->width - n;
    if ((spec->flags & FMT_LEFT) == 0) {
        emit_pad(o, ' ', pad);
    }
    for (int i = 0; i < n; i++) {
        emit_char(o, s[i]);
    }
    if (spec->flags & FMT_LEFT) {
        emit_pad(o, ' ', pad);
    }
}

static void emit_str(struct fmt_out *o, const char *s,
    const struct fmt_spec *spec)
{
    int n;

    n = 0;
    while ((spec->prec < 0 || n < spec->prec) && s[n] != '\0') {
        n++;
    }
    emit_field(o, s, n, spec);
}

static const char *decode_flags(const char *fmt, struct fmt_spec *spec)
{
    for (;; fmt++) {
        switch (*fmt) {
            case '-':
                spec->flags |= FMT_LEFT;
                break;
            case '+':
                spec->flags |= FMT_PLUS;
                break;
            case ' ':
                spec->flags |= FMT_SPACE;
                break;
            case '#':
                spec->flags |= FMT_ALT;
                break;
            case '0':
                spec->flags |= FMT_ZERO;
                break;
            default:
                return fmt;
        }
    }
}

static const char *parse_int(const char *fmt, int *out)
{
    *out = 0;
    while (*fmt >= '0' && *fmt <= '9') {
        *out = *out * BASE_DEC + (*fmt++ - '0');
    }

    return fmt;
}

static const char *decode_len(const char *fmt, struct fmt_spec *spec)
{
    if (fmt[0] == 'h' && fmt[1] == 'h') {
        spec->len = LEN_HH;
        return fmt + 2;
    }
    if (fmt[0] == 'l' && fmt[1] == 'l') {
        spec->len = LEN_LL;
        return fmt + 2;
    }

    switch (*fmt) {
        case 'h':
            spec->len = LEN_H;
            break;
        case 'l':
            spec->len = LEN_L;
            break;
        case 'j':
            spec->len = LEN_J;
            break;
        case 'z':
            spec->len = LEN_Z;
            break;
        case 't':
            spec->len = LEN_T;
            break;
        default:
            spec->len = LEN_NONE;
            return fmt;
    }

    return fmt + 1;
}

static const char *decode(const char *fmt, struct fmt_spec *spec, va_list *ap)
{
    spec->flags = 0;
    spec->prec = -1;
    fmt = decode_flags(fmt, spec);

    if (*fmt == '*') {
        spec->width = va_arg(*ap, int);
        if (spec->width < 0) {
            spec->flags |= FMT_LEFT;
            spec->width = -spec->width;
        }
        fmt++;
    } else {
        fmt = parse_int(fmt, &spec->width);
    }

    if (*fmt == '.' && fmt[1] == '*') {
        spec->prec = va_arg(*ap, int);
        fmt += 2;
    } else if (*fmt == '.') {
        fmt = parse_int(fmt + 1, &spec->prec);
    }

    if (spec->flags & FMT_LEFT) {
        spec->flags &= ~FMT_ZERO;
    }
    if (spec->flags & FMT_PLUS) {
        spec->flags &= ~FMT_SPACE;
    }

    return decode_len(fmt, spec);
}

static int64 arg_signed(va_list *ap, int len)
{
    switch (len) {
        case LEN_HH:
            return (signed char)va_arg(*ap, int);
        case LEN_H:
            return (short)va_arg(*ap, int);
        case LEN_L:
            return va_arg(*ap, long);
        case LEN_LL:
            return va_arg(*ap, long long);
        case LEN_J:
        case LEN_Z:
        case LEN_T:
            return va_arg(*ap, int64);
        default:
            return va_arg(*ap, int);
    }
}

static uint64 arg_unsigned(va_list *ap, int len)
{
    switch (len) {
        case LEN_HH:
            return (unsigned char)va_arg(*ap, unsigned int);
        case LEN_H:
            return (unsigned short)va_arg(*ap, unsigned int);
        case LEN_L:
            return va_arg(*ap, unsigned long);
        case LEN_LL:
            return va_arg(*ap, unsigned long long);
        case LEN_J:
        case LEN_Z:
        case LEN_T:
            return va_arg(*ap, uint64);
        default:
            return va_arg(*ap, unsigned int);
    }
}

static void emit_unsigned(struct fmt_out *o, unsigned int base,
    struct fmt_spec *spec, va_list *ap)
{
    spec->flags &= ~(FMT_PLUS | FMT_SPACE);
    emit_num(o, arg_unsigned(ap, spec->len), 0, base, spec);
}

/* Returns false for a conversion it does not know. */
static bool convert(struct fmt_out *o, char c, struct fmt_spec *spec,
    va_list *ap)
{
    const char *s;
    int64 sv;
    char ch;

    if (spec->len != LEN_NONE && (c == 'c' || c == 's' || c == 'p')) {
        return false;
    }

    switch (c) {
        case 'd':
        case 'i':
            sv = arg_signed(ap, spec->len);
            emit_num(o, (sv < 0) ? -(uint64)sv : (uint64)sv, sv < 0, BASE_DEC,
                spec);
            return true;
        case 'o':
            emit_unsigned(o, BASE_OCT, spec, ap);
            return true;
        case 'u':
            emit_unsigned(o, BASE_DEC, spec, ap);
            return true;
        case 'X':
            spec->flags |= FMT_UPPER;
            emit_unsigned(o, BASE_HEX, spec, ap);
            return true;
        case 'x':
            emit_unsigned(o, BASE_HEX, spec, ap);
            return true;
        case 'c':
            ch = (char)va_arg(*ap, int);
            emit_field(o, &ch, 1, spec);
            return true;
        case 's':
            s = va_arg(*ap, const char *);
            emit_str(o, (s == NULL) ? "(null)" : s, spec);
            return true;
        case 'p':
            spec->flags = (spec->flags & FMT_LEFT) | FMT_PREFIX;
            spec->prec = FMT_PTRDIGITS;
            emit_num(o, (uintptr)va_arg(*ap, void *), 0, BASE_HEX, spec);
            return true;
        case 'n':
            (void)va_arg(*ap, void *);
            return true;
        case '%':
            emit_char(o, '%');
            return true;
        default:
            return false;
    }
}

int vsnprintf(char *buf, uint64 size, const char *fmt, va_list ap)
{
    struct fmt_out o = {buf, size, 0};
    struct fmt_spec spec;
    const char *pct;
    char c;

    while (*fmt != '\0') {
        if (*fmt != '%') {
            emit_char(&o, *fmt++);
            continue;
        }

        pct = fmt;
        fmt = decode(fmt + 1, &spec, &ap);
        c = *fmt;
        if (c != '\0') {
            fmt++;
        }

        if (!convert(&o, c, &spec, &ap)) {
            /* The arguments no longer line up: copy the rest unformatted. */
            while (pct < fmt) {
                emit_char(&o, *pct++);
            }
            while (*fmt != '\0') {
                emit_char(&o, *fmt++);
            }
        }
    }

    if (size > 0) {
        buf[(o.len < size) ? o.len : size - 1] = '\0';
    }

    return (int)o.len;
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
