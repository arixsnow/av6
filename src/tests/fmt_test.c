/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Test suite for the kernel formatter. The conversions C defines are
 * checked against C's rules; %p, %n, a NULL %s and an unknown conversion
 * are AV6's own.
 */

#include <sys/fmt.h>
#include <sys/string.h>
#include <sys/types.h>
#include <tests/ktest.h>

#define FMT_EXPECT(t, want, ...)                              \
    do {                                                      \
        char _buf[96];                                        \
        int _n = snprintf(_buf, sizeof(_buf), __VA_ARGS__);   \
        KTEST_EXPECT_EQ(t, _n, (int)strlen(want));            \
        KTEST_EXPECT_MEM_EQ(t, _buf, want, strlen(want) + 1); \
    } while (false)

/* Flag pairs C defines but compilers warn about, so they come from here. */
static const char *const f_left_zero = "%-05d|";
static const char *const f_plus_space = "%+ d";
static const char *const f_zero_prec = "%05.3d";
static const char *const f_zero_prec_x = "%08.3x";

static void fmt_signed_conversions(struct ktest *t)
{
    FMT_EXPECT(t, "0", "%d", 0);
    FMT_EXPECT(t, "42", "%d", 42);
    FMT_EXPECT(t, "-42", "%d", -42);
    FMT_EXPECT(t, "-42", "%i", -42);
    FMT_EXPECT(t, "2147483647", "%d", 2147483647);
    FMT_EXPECT(t, "-2147483648", "%d", -2147483647 - 1);
    FMT_EXPECT(t, "-9223372036854775808", "%ld", -9223372036854775807L - 1);
    FMT_EXPECT(t, "9223372036854775807", "%ld", 9223372036854775807L);
}
KTEST_CASE(fmt_signed_conversions);

static void fmt_unsigned_conversions(struct ktest *t)
{
    FMT_EXPECT(t, "4294967295", "%u", 4294967295U);
    FMT_EXPECT(t, "18446744073709551615", "%lu", 18446744073709551615UL);
    FMT_EXPECT(t, "10", "%o", 8U);
    FMT_EXPECT(t, "1777777777777777777777", "%lo", 18446744073709551615UL);
    FMT_EXPECT(t, "ff", "%x", 255U);
    FMT_EXPECT(t, "FF", "%X", 255U);
    FMT_EXPECT(t, "deadbeef", "%x", 0xdeadbeefU);
    FMT_EXPECT(t, "ffffffffffffffff", "%lx", 18446744073709551615UL);
}
KTEST_CASE(fmt_unsigned_conversions);

static void fmt_sign_and_space_flags(struct ktest *t)
{
    FMT_EXPECT(t, "+42", "%+d", 42);
    FMT_EXPECT(t, "-42", "%+d", -42);
    FMT_EXPECT(t, "+0", "%+d", 0);
    FMT_EXPECT(t, " 42", "% d", 42);
    FMT_EXPECT(t, "-42", "% d", -42);
    FMT_EXPECT(t, "+42", f_plus_space, 42);
}
KTEST_CASE(fmt_sign_and_space_flags);

static void fmt_width_and_alignment(struct ktest *t)
{
    FMT_EXPECT(t, "   42", "%5d", 42);
    FMT_EXPECT(t, "42   |", "%-5d|", 42);
    FMT_EXPECT(t, "00042", "%05d", 42);
    FMT_EXPECT(t, "-0042", "%05d", -42);
    FMT_EXPECT(t, "+0042", "%+05d", 42);
    FMT_EXPECT(t, "-42  |", f_left_zero, -42);
    FMT_EXPECT(t, "12345", "%3d", 12345);
    FMT_EXPECT(t, "  -42", "%5d", -42);
}
KTEST_CASE(fmt_width_and_alignment);

static void fmt_precision_on_integers(struct ktest *t)
{
    FMT_EXPECT(t, "007", "%.3d", 7);
    FMT_EXPECT(t, "-007", "%.3d", -7);
    FMT_EXPECT(t, "", "%.0d", 0);
    FMT_EXPECT(t, "   ", "%3.0d", 0);
    FMT_EXPECT(t, "  007", "%5.3d", 7);
    FMT_EXPECT(t, "  007", f_zero_prec, 7);
    FMT_EXPECT(t, "     0ff", f_zero_prec_x, 255U);
    FMT_EXPECT(t, "", "%.0x", 0U);
    FMT_EXPECT(t, "12345", "%.2d", 12345);
}
KTEST_CASE(fmt_precision_on_integers);

static void fmt_alternate_form(struct ktest *t)
{
    FMT_EXPECT(t, "0xff", "%#x", 255U);
    FMT_EXPECT(t, "0XFF", "%#X", 255U);
    FMT_EXPECT(t, "0", "%#x", 0U);
    FMT_EXPECT(t, "010", "%#o", 8U);
    FMT_EXPECT(t, "0", "%#o", 0U);
    FMT_EXPECT(t, "0", "%#.0o", 0U);
    FMT_EXPECT(t, "", "%#.0x", 0U);
    FMT_EXPECT(t, "0x0000ff", "%#08x", 255U);
    FMT_EXPECT(t, "  010", "%#5o", 8U);
    FMT_EXPECT(t, "00010", "%#.5o", 8U);
    FMT_EXPECT(t, "0x0000000040080000", "0x%016lx", 0x40080000UL);
    FMT_EXPECT(t, "0x0000000000000000", "0x%016lx", 0UL);
}
KTEST_CASE(fmt_alternate_form);

static void fmt_star_width_and_precision(struct ktest *t)
{
    FMT_EXPECT(t, "   42", "%*d", 5, 42);
    FMT_EXPECT(t, "42   |", "%*d|", -5, 42);
    FMT_EXPECT(t, "007", "%.*d", 3, 7);
    FMT_EXPECT(t, "7", "%.*d", -1, 7);
    FMT_EXPECT(t, "  007", "%*.*d", 5, 3, 7);
    FMT_EXPECT(t, "ab", "%.*s", 2, "abcdef");
}
KTEST_CASE(fmt_star_width_and_precision);

static void fmt_length_modifiers(struct ktest *t)
{
    FMT_EXPECT(t, "44", "%hhd", 300);
    FMT_EXPECT(t, "-56", "%hhd", 200);
    FMT_EXPECT(t, "255", "%hhu", 511);
    FMT_EXPECT(t, "4464", "%hd", 70000);
    FMT_EXPECT(t, "-1", "%hd", 65535);
    FMT_EXPECT(t, "4464", "%hu", 70000);
    FMT_EXPECT(t, "-1", "%lld", -1LL);
    FMT_EXPECT(t, "18446744073709551615", "%llu", 18446744073709551615ULL);
    FMT_EXPECT(t, "-1", "%jd", (int64)-1);
    FMT_EXPECT(t, "4096", "%zu", (uint64)4096);
    FMT_EXPECT(t, "-1", "%zd", (int64)-1);
    FMT_EXPECT(t, "-8", "%td", (int64)-8);
    FMT_EXPECT(t, "ffff", "%hx", 0x1ffffU);
}
KTEST_CASE(fmt_length_modifiers);

static void fmt_chars_and_strings(struct ktest *t)
{
    static const char unterminated[3] = {'x', 'y', 'z'};
    char buf[8];

    FMT_EXPECT(t, "A", "%c", 'A');
    FMT_EXPECT(t, "  A", "%3c", 'A');
    FMT_EXPECT(t, "A  |", "%-3c|", 'A');
    FMT_EXPECT(t, "abc", "%s", "abc");
    FMT_EXPECT(t, "  abc", "%5s", "abc");
    FMT_EXPECT(t, "abc  |", "%-5s|", "abc");
    FMT_EXPECT(t, "ab", "%.2s", "abc");
    FMT_EXPECT(t, "", "%.0s", "abc");
    FMT_EXPECT(t, "    a", "%5.1s", "abc");
    FMT_EXPECT(t, "xyz", "%.3s", unterminated);
    FMT_EXPECT(t, "100%", "%d%%", 100);
    FMT_EXPECT(t, "a 1 b", "%c %d %s", 'a', 1, "b");

    KTEST_EXPECT_EQ(t, snprintf(buf, sizeof(buf), "a%cb", 0), 3);
    KTEST_EXPECT_MEM_EQ(t, buf, "a\0b", 4);
}
KTEST_CASE(fmt_chars_and_strings);

static void fmt_truncation_and_return_value(struct ktest *t)
{
    char buf[8];

    memset(buf, 'Z', sizeof(buf));
    KTEST_EXPECT_EQ(t, snprintf(buf, 4, "%d", 12345), 5);
    KTEST_EXPECT_MEM_EQ(t, buf, "123\0ZZZZ", 8);

    memset(buf, 'Z', sizeof(buf));
    KTEST_EXPECT_EQ(t, snprintf(buf, 0, "%s", "abc"), 3);
    KTEST_EXPECT_MEM_EQ(t, buf, "ZZZZZZZZ", 8);

    memset(buf, 'Z', sizeof(buf));
    KTEST_EXPECT_EQ(t, snprintf(buf, 1, "%s", "abc"), 3);
    KTEST_EXPECT_MEM_EQ(t, buf, "\0ZZZZZZZ", 8);

    KTEST_EXPECT_EQ(t, snprintf(NULL, 0, "%08x", 1U), 8);
}
KTEST_CASE(fmt_truncation_and_return_value);

static void fmt_pointer_is_0x_and_16_digits(struct ktest *t)
{
    FMT_EXPECT(t, "0x0000000000001234", "%p", (void *)0x1234);
    FMT_EXPECT(t, "0x0000000000000000", "%p", (void *)0);
    FMT_EXPECT(t, "0xffff000040080000", "%p", (void *)0xffff000040080000UL);
    FMT_EXPECT(t, "  0x0000000000001234", "%20p", (void *)0x1234);
    FMT_EXPECT(t, "0x0000000000001234  |", "%-20p|", (void *)0x1234);
}
KTEST_CASE(fmt_pointer_is_0x_and_16_digits);

static void fmt_null_string_and_percent_n(struct ktest *t)
{
    static const char *const f_n = "a%nb%d";
    const char *null = NULL;
    int count = -7;

    FMT_EXPECT(t, "(null)", "%s", null);
    FMT_EXPECT(t, "ab5", f_n, &count, 5);
    KTEST_EXPECT_EQ(t, count, -7);
}
KTEST_CASE(fmt_null_string_and_percent_n);

static void fmt_unknown_conversion_stops_formatting(struct ktest *t)
{
    static const char *const f_unknown = "x%yz %d";
    static const char *const f_wide = "%ls %d";
    static const char *const f_trailing = "50%";

    FMT_EXPECT(t, "x%yz %d", f_unknown, 1);
    FMT_EXPECT(t, "%ls %d", f_wide, "s", 1);
    FMT_EXPECT(t, "50%", f_trailing);
}
KTEST_CASE(fmt_unknown_conversion_stops_formatting);
