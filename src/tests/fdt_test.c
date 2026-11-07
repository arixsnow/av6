/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#include "arch/dtb.h"
#include "sys/fdt.h"
#include "sys/string.h"
#include "sys/types.h"
#include "tests/ktest.h"

/* Define here rather than shared, so a wrong value in the reader shows up. */
#define FDT_MAGIC           0xD00DFEED
#define FDT_BEGIN_NODE      1
#define FDT_END_NODE        2
#define FDT_PROP            3
#define FDT_END             9

#define SW_HDR_WORDS        14
#define SW_WORDS            1024
#define SW_STRINGS          256

static uint32 sw_buf[SW_WORDS];
static char sw_str[SW_STRINGS];
static uint32 sw_n, sw_slen;
static int sw_err;

static void sw_data(const void *p, uint32 len)
{
    uint32 words = (len + 3) >> 2;

    if (sw_n + words > SW_WORDS) {
        sw_err = 1;
        return;
    }

    memset(&sw_buf[sw_n], 0, words << 2);
    memcpy(&sw_buf[sw_n], p, len);
    sw_n += words;
}

static void sw_cell(uint32 v)
{
    v = fdt32(v);
    sw_data(&v, 4);
}

static void sw_begin(const char *name)
{
    sw_cell(FDT_BEGIN_NODE);
    sw_data(name, (uint32)strlen(name) + 1);
}

static void sw_end(void)
{
    sw_cell(FDT_END_NODE);
}

static uint32 sw_string(const char *name)
{
    uint32 nlen, i;

    nlen = (uint32)strlen(name) + 1;

    for (i = 0; i + nlen <= sw_slen; i++) {
        if (memcmp(&sw_str[i], name, nlen) == 0) {
            return i;
        }
    }

    if (sw_slen + nlen > SW_STRINGS) {
        sw_err = 1;
        return 0;
    }

    memcpy(&sw_str[sw_slen], name, nlen);
    sw_slen += nlen;

    return sw_slen - nlen;
}

static void sw_prop(const char *name, const void *val, uint32 len)
{
    sw_cell(FDT_PROP);
    sw_cell(len);
    sw_cell(sw_string(name));
    sw_data(val, len);
}

static void sw_prop_cells(const char *name, const uint32 *v, uint32 n)
{
    uint32 be[32], i;

    if (n > 32) {
        sw_err = 1;
        return;
    }

    for (i = 0; i < n; i++) {
        be[i] = fdt32(v[i]);
    }

    sw_prop(name, be, n << 2);
}

#define SW_STR(name, lit)       sw_prop((name), (lit), sizeof(lit))
#define SW_CELLS(name, ...)     sw_prop_cells((name), (const uint32[]){ __VA_ARGS__ },  \
    sizeof((const uint32[]){ __VA_ARGS__ }) / 4)
#define SW_EMPTY(name)          sw_prop((name), "", 0)

static const void *sw_finish(void)
{
    uint32 off_str;

    sw_cell(FDT_END);

    if (sw_err || (sw_n << 2) + sw_slen > sizeof(sw_buf)) {
        return NULL;
    }

    off_str = sw_n << 2;
    memcpy((char *)sw_buf + off_str, sw_str, sw_slen);

    sw_buf[0] = fdt32(FDT_MAGIC);
    sw_buf[1] = fdt32(off_str + sw_slen);
    sw_buf[2] = fdt32(SW_HDR_WORDS << 2);
    sw_buf[3] = fdt32(off_str);
    sw_buf[4] = fdt32(40);
    sw_buf[5] = fdt32(17);
    sw_buf[6] = fdt32(16);
    sw_buf[7] = 0;
    sw_buf[8] = fdt32(sw_slen);
    sw_buf[9] = fdt32(off_str - (SW_HDR_WORDS << 2));

    return sw_buf;
}

static const void *fdt_fixture(void)
{
    memset(sw_buf, 0, sizeof(sw_buf));
    sw_n = SW_HDR_WORDS;
    sw_slen = 0;
    sw_err = 0;

    sw_begin("");
        SW_CELLS("#address-cells", 2);
        SW_CELLS("#size-cells", 2);
        SW_CELLS("interrupt-parent", 1);
        sw_begin("chosen");
            SW_STR("stdout-path", "serial0:115200n8");
        sw_end();
        sw_begin("aliases");
            SW_STR("serial0", "/bus@8000000/motherboard-bus@8000000"
                "/iofpga-bus@300000000/serial@90000");
        sw_end();
        sw_begin("psci");
            SW_STR("compatible", "arm,psci-0.2");
            SW_STR("method", "smc");
        sw_end();
        sw_begin("psci-v1");
            SW_STR("compatible", "arm,psci-1.0");
            SW_STR("method", "hvc");
        sw_end();
        sw_begin("psci-v0");
            SW_STR("compatible", "arm,psci");
            SW_STR("method", "svc");
        sw_end();
        sw_begin("cpus");
            SW_CELLS("#address-cells", 1);
            SW_CELLS("#size-cells", 0);
            sw_begin("cpu@0");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 0);
                SW_STR("enable-method", "psci");
            sw_end();
            sw_begin("cpu@1");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 1);
                SW_STR("enable-method", "spin-table");
            sw_end();
            sw_begin("cpu@2");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 2);
            sw_end();
            sw_begin("cpu@3");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 0, 3);
            sw_end();
            sw_begin("cpu@1000000");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 0x1000000);
            sw_end();
        sw_end();
        sw_begin("cpus2");
            SW_CELLS("#address-cells", 2);
            SW_CELLS("#size-cells", 0);
            sw_begin("cpu@100000000");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 1, 0);
            sw_end();
            sw_begin("cpu@10000000000");
                SW_STR("device_type", "cpu");
                SW_CELLS("reg", 0x100, 0);
            sw_end();
        sw_end();
        sw_begin("timer");
            SW_STR("compatible", "arm,armv8-timer");
            SW_CELLS("interrupts", 1, 13, 8, 1, 14, 8, 1, 11, 8, 1, 10, 8);
        sw_end();
        sw_begin("timer-v7");
            SW_STR("compatible", "arm,armv7-timer");
        sw_end();
        sw_begin("interrupt-controller@2f000000");
            SW_STR("compatible", "arm,gic-v3");
            SW_CELLS("#interrupt-cells", 3);
            SW_CELLS("#address-cells", 2);
            SW_CELLS("#size-cells", 2);
            SW_EMPTY("interrupt-controller");
            SW_CELLS("reg", 0, 0x2f000000, 0, 0x10000, 0, 0x2f100000, 0, 0x200000);
            SW_CELLS("phandle", 1);
        sw_end();
        sw_begin("intc-off");
            SW_CELLS("#interrupt-cells", 3);
            SW_EMPTY("interrupt-controller");
            SW_STR("status", "disabled");
            SW_CELLS("phandle", 2);
        sw_end();
        sw_begin("memory@80000000");
            SW_STR("device_type", "memory");
            SW_CELLS("reg", 0, 0x80000000, 0, 0x7c000000,
                8, 0x80000000, 0, 0x80000000);
        sw_end();
        sw_begin("bus@8000000");
            SW_STR("compatible", "simple-bus");
            SW_CELLS("#address-cells", 2);
            SW_CELLS("#size-cells", 1);
            SW_CELLS("ranges", 0, 0x8000000, 0, 0x8000000, 0x18000000);
            SW_CELLS("#interrupt-cells", 1);
            SW_CELLS("interrupt-map-mask", 0, 0, 63);
            SW_CELLS("interrupt-map",
                0, 0, 5, 1, 0, 0, 0, 5, 4,
                0, 0, 7, 2, 0, 7, 4,
                0, 0, 15, 1, 0, 0, 0, 15, 4);
            sw_begin("motherboard-bus@8000000");
                SW_CELLS("#address-cells", 2);
                SW_CELLS("#size-cells", 1);
                SW_CELLS("ranges",
                    0, 0, 0, 0x08000000, 0x04000000,
                    1, 0, 0, 0x14000000, 0x04000000,
                    2, 0, 0, 0x18000000, 0x04000000,
                    3, 0, 0, 0x1c000000, 0x04000000,
                    4, 0, 0, 0x0c000000, 0x04000000,
                    5, 0, 0, 0x10000000, 0x04000000);
                sw_begin("flash@0");
                    SW_CELLS("reg", 0, 0, 0x04000000, 4, 0, 0x04000000);
                sw_end();
                sw_begin("iofpga-bus@300000000");
                    SW_STR("compatible", "simple-bus");
                    SW_CELLS("#address-cells", 1);
                    SW_CELLS("#size-cells", 1);
                    SW_CELLS("ranges", 0, 3, 0, 0x210000);
                    sw_begin("serial@90000");
                        SW_STR("compatible", "arm,pl011\0arm,primecell");
                        SW_CELLS("reg", 0x90000, 0x1000);
                        SW_CELLS("interrupts", 5);
                    sw_end();
                    sw_begin("serial@a0000");
                        SW_STR("compatible", "arm,pl011\0arm,primecell");
                        SW_CELLS("reg", 0xa0000, 0x1000);
                        SW_STR("status", "disabled");
                    sw_end();
                    sw_begin("nomap@b0000");
                        SW_CELLS("reg", 0xb0000, 0x1000);
                        SW_CELLS("interrupts", 9);
                    sw_end();
                    sw_begin("offmap@c0000");
                        SW_CELLS("reg", 0xc0000, 0x1000);
                        SW_CELLS("interrupts", 7);
                    sw_end();
                    sw_begin("ext@d0000");
                        SW_CELLS("reg", 0xd0000, 0x1000);
                        SW_CELLS("interrupts", 5);
                        SW_CELLS("interrupts-extended", 1, 0, 7, 4);
                    sw_end();
                sw_end();
                sw_begin("sram@700000000");
                    SW_CELLS("reg", 7, 0, 0x1000);
                sw_end();
            sw_end();
        sw_end();
        sw_begin("noranges");
            SW_CELLS("#address-cells", 1);
            SW_CELLS("#size-cells", 1);
            sw_begin("dev@0");
                SW_CELLS("reg", 0, 0x1000);
            sw_end();
        sw_end();
        sw_begin("oversize");
            SW_CELLS("#address-cells", 1);
            SW_CELLS("#size-cells", 5);
            sw_begin("dev@0");
                SW_CELLS("reg", 0, 0, 0, 0, 0, 0x1000);
            sw_end();
        sw_end();
        sw_begin("loop-a");
            SW_CELLS("#interrupt-cells", 1);
            SW_CELLS("#address-cells", 0);
            SW_CELLS("interrupt-map", 1, 4, 1);
            SW_CELLS("phandle", 3);
        sw_end();
        sw_begin("loop-b");
            SW_CELLS("#interrupt-cells", 1);
            SW_CELLS("#address-cells", 0);
            SW_CELLS("interrupt-map", 1, 3, 1);
            SW_CELLS("phandle", 4);
        sw_end();
        sw_begin("looped");
            SW_CELLS("interrupt-parent", 3);
            SW_CELLS("interrupts", 1);
        sw_end();
        sw_begin("badmask");
            SW_CELLS("#interrupt-cells", 1);
            SW_CELLS("#address-cells", 0);
            SW_CELLS("interrupt-map-mask", 0, 63);
            SW_CELLS("interrupt-map", 5, 1, 0, 0, 0, 5, 4);
            SW_CELLS("phandle", 5);
        sw_end();
        sw_begin("masked");
            SW_CELLS("interrupt-parent", 5);
            SW_CELLS("interrupts", 5);
        sw_end();
    sw_end();

    return sw_finish();
}

static const char fdt_console[] = "/bus@8000000/motherboard-bus@8000000"
    "/iofpga-bus@300000000/serial@90000";

static int fdt_fixture_path(struct fdt *fdt, struct fdt_node *node,
    const char *path)
{
    const void *blob;

    blob = fdt_fixture();

    if (blob == NULL) {
        return 0;
    }

    fdt_open(fdt, blob);

    return fdt_find_path(fdt, path, (uint32)strlen(path), node);
}

static int fdt_fixture_reg(const char *path, uint32 index, uint64 *addr,
    uint64 *size)
{
    struct fdt fdt;
    struct fdt_node node;

    return (fdt_fixture_path(&fdt, &node, path)
        && fdt_translate_reg(&fdt, &node, index, addr, size));
}

static void fdt_cells_refuses_to_truncate(struct ktest *t)
{
    uint32 c[4];
    uint64 v;

    c[0] = 0;
    c[1] = fdt32(0xdead);
    c[2] = fdt32(0xbeef);
    c[3] = fdt32(1);

    KTEST_EXPECT(t, fdt_cells(c, 3, &v) && v == 0xdead0000beefUL);
    KTEST_EXPECT(t, fdt_cells(c + 2, 2, &v) && v == 0xbeef00000001UL);
    KTEST_EXPECT(t, !fdt_cells(c + 1, 3, &v));
    KTEST_EXPECT(t, !fdt_cells(c, 0, &v));
    KTEST_EXPECT(t, !fdt_cells(c, FDT_MAX_CELLS + 1, &v));
}
KTEST_CASE(fdt_cells_refuses_to_truncate);

static void fdt_compatible_searches_the_whole_list(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node node;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, fdt_console));

    KTEST_EXPECT(t, fdt_node_is_compatible(&fdt, &node, "arm,pl011"));
    KTEST_EXPECT(t, fdt_node_is_compatible(&fdt, &node, "arm,primecell"));
    KTEST_EXPECT(t, !fdt_node_is_compatible(&fdt, &node, "arm,pl0"));
    KTEST_EXPECT(t, !fdt_node_is_compatible(&fdt, &node, "arm,pl011x"));
}
KTEST_CASE(fdt_compatible_searches_the_whole_list);

static void fdt_translates_through_nested_buses(struct ktest *t)
{
    uint64 addr, size;

    KTEST_ASSERT(t, fdt_fixture_reg(fdt_console, 0, &addr, &size));
    KTEST_EXPECT_EQ(t, addr, 0x1c090000UL);
    KTEST_EXPECT_EQ(t, size, 0x1000UL);

    KTEST_EXPECT(t, !fdt_fixture_reg(fdt_console, 1, &addr, &size));
}
KTEST_CASE(fdt_translates_through_nested_buses);

static void fdt_translate_picks_the_window(struct ktest *t)
{
    static const char flash[] = "/bus@8000000/motherboard-bus@8000000/flash@0";
    static const char gic[] = "/interrupt-controller@2f000000";
    static const char mem[] = "/memory@80000000";
    uint64 addr, size;

    KTEST_EXPECT(t, fdt_fixture_reg(flash, 0, &addr, &size)
        && addr == 0x08000000UL && size == 0x04000000UL);
    KTEST_EXPECT(t, fdt_fixture_reg(flash, 1, &addr, &size)
        && addr == 0x0c000000UL && size == 0x04000000UL);

    KTEST_EXPECT(t, fdt_fixture_reg(gic, 0, &addr, NULL)
        && addr == 0x2f000000UL);
    KTEST_EXPECT(t, fdt_fixture_reg(gic, 1, &addr, NULL)
        && addr == 0x2f100000UL);

    KTEST_EXPECT(t, fdt_fixture_reg(mem, 0, &addr, &size)
        && addr == 0x80000000UL && size == 0x7c000000UL);
    KTEST_EXPECT(t, fdt_fixture_reg(mem, 1, &addr, &size)
        && addr == 0x880000000UL && size == 0x80000000UL);
}
KTEST_CASE(fdt_translate_picks_the_window);

static void fdt_translate_fails_without_a_window(struct ktest *t)
{
    uint64 addr, size;

    KTEST_EXPECT(t, !fdt_fixture_reg(
        "/bus@8000000/motherboard-bus@8000000/sram@700000000", 0, &addr, &size));
    KTEST_EXPECT(t, !fdt_fixture_reg("/noranges/dev@0", 0, &addr, &size));
    KTEST_EXPECT(t, !fdt_fixture_reg("/oversize/dev@0", 0, &addr, &size));
}
KTEST_CASE(fdt_translate_fails_without_a_window);

static void fdt_parent_walks_up(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node node, up;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, fdt_console));

    KTEST_ASSERT(t, fdt_parent(&fdt, &node, &up));
    KTEST_EXPECT(t, fdt_node_is(&up, "iofpga-bus"));

    KTEST_ASSERT(t, fdt_parent(&fdt, &up, &node));
    KTEST_EXPECT(t, fdt_node_is(&node, "motherboard-bus"));

    KTEST_ASSERT(t, fdt_parent(&fdt, &node, &up));
    KTEST_EXPECT(t, fdt_node_is(&up, "bus"));

    KTEST_ASSERT(t, fdt_parent(&fdt, &up, &node));
    KTEST_EXPECT_EQ(t, node.depth, 0);
    KTEST_EXPECT(t, !fdt_parent(&fdt, &node, &up));
}
KTEST_CASE(fdt_parent_walks_up);

static void fdt_status_separates_described_from_usable(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node node;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/bus@8000000"
        "/motherboard-bus@8000000/iofpga-bus@300000000/serial@a0000"));
    KTEST_EXPECT(t, !fdt_node_is_available(&fdt, &node));

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, fdt_console));
    KTEST_EXPECT(t, fdt_node_is_available(&fdt, &node));
}
KTEST_CASE(fdt_status_separates_described_from_usable);

static void fdt_find_path_resolves_aliases(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node want, node;
    static const char chosen[] = "serial0:115200n8";

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &want, fdt_console));

    KTEST_EXPECT(t, fdt_find_path(&fdt, chosen, 7, &node)
        && node.props == want.props);

    KTEST_EXPECT(t, fdt_fixture_path(&fdt, &node, "serial0")
        && node.props == want.props);

    KTEST_EXPECT(t, fdt_fixture_path(&fdt, &node,
        "/bus@8000000/motherboard-bus/iofpga-bus/serial@90000")
        && node.props == want.props);

    KTEST_EXPECT(t, !fdt_find_path(&fdt, chosen, 0, &node));
    KTEST_EXPECT(t, !fdt_fixture_path(&fdt, &node, "serial9"));
    KTEST_EXPECT(t, !fdt_fixture_path(&fdt, &node, "/bus@8000000/nosuch"));
}
KTEST_CASE(fdt_find_path_resolves_aliases);

static void dtb_matches_on_compatible(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node node;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/psci"));
    KTEST_EXPECT(t, dtb_is_psci(&fdt, &node));
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/psci-v1"));
    KTEST_EXPECT(t, dtb_is_psci(&fdt, &node));
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/psci-v0"));
    KTEST_EXPECT(t, !dtb_is_psci(&fdt, &node));

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/timer"));
    KTEST_EXPECT(t, dtb_is_timer(&fdt, &node));
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/timer-v7"));
    KTEST_EXPECT(t, dtb_is_timer(&fdt, &node));

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/interrupt-controller"));
    KTEST_EXPECT(t, dtb_is_gic(&fdt, &node));
    KTEST_EXPECT(t, !dtb_is_timer(&fdt, &node));

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/memory"));
    KTEST_EXPECT(t, dtb_is_memory(&fdt, &node));
    KTEST_EXPECT(t, !dtb_is_gic(&fdt, &node));

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, fdt_console));
    KTEST_EXPECT(t, dtb_is_console(&fdt, &node));
    KTEST_EXPECT(t, !dtb_is_memory(&fdt, &node));
}
KTEST_CASE(dtb_matches_on_compatible);

static int fdt_fixture_irq(struct fdt *fdt, const char *path, uint32 index,
    struct fdt_irq *irq)
{
    struct fdt_node node;

    return (fdt_fixture_path(fdt, &node, path)
        && fdt_irq(fdt, &node, index, irq));
}

static void fdt_irq_walks_the_interrupt_tree(struct ktest *t)
{
    static const char ext[] = "/bus/motherboard-bus/iofpga-bus/ext";
    struct fdt fdt;
    struct fdt_irq irq;

    KTEST_ASSERT(t, fdt_fixture_irq(&fdt, fdt_console, 0, &irq));
    KTEST_EXPECT(t, fdt_node_is(&irq.ctrl, "interrupt-controller"));
    KTEST_EXPECT(t, irq.ncells == 3 && irq.cells[0] == 0
        && irq.cells[1] == 5 && irq.cells[2] == 4);
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 37U);
    KTEST_EXPECT(t, !fdt_fixture_irq(&fdt, fdt_console, 1, &irq));

    KTEST_EXPECT(t, fdt_fixture_irq(&fdt, "/timer", 1, &irq)
        && dtb_intid(&fdt, &irq) == 30U);
    KTEST_EXPECT(t, fdt_fixture_irq(&fdt, "/timer", 3, &irq)
        && dtb_intid(&fdt, &irq) == 26U);
    KTEST_EXPECT(t, !fdt_fixture_irq(&fdt, "/timer", 4, &irq));

    KTEST_EXPECT(t, fdt_fixture_irq(&fdt, ext, 0, &irq)
        && dtb_intid(&fdt, &irq) == 39U);
}
KTEST_CASE(fdt_irq_walks_the_interrupt_tree);

static void fdt_irq_fails_without_a_usable_route(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_irq irq;

    KTEST_EXPECT(t, !fdt_fixture_irq(&fdt,
        "/bus/motherboard-bus/iofpga-bus/nomap", 0, &irq));
    KTEST_EXPECT(t, !fdt_fixture_irq(&fdt,
        "/bus/motherboard-bus/iofpga-bus/offmap", 0, &irq));
    KTEST_EXPECT(t, !fdt_fixture_irq(&fdt, "/looped", 0, &irq));
    KTEST_EXPECT(t, !fdt_fixture_irq(&fdt, "/masked", 0, &irq));
}
KTEST_CASE(fdt_irq_fails_without_a_usable_route);

static void dtb_intid_accepts_only_gic_spi_and_ppi(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_irq irq;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &irq.ctrl, "/interrupt-controller"));

    irq.ncells = 3;
    irq.cells[2] = 4;

    irq.cells[0] = 0;
    irq.cells[1] = 987;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 1019U);
    irq.cells[1] = 988;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 0U);

    irq.cells[0] = 1;
    irq.cells[1] = 15;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 31U);
    irq.cells[1] = 16;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 0U);

    irq.cells[0] = 2;
    irq.cells[1] = 0;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 0U);

    irq.cells[0] = 0;
    irq.cells[1] = 5;
    irq.ncells = 4;
    irq.cells[3] = 0;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 37U);
    irq.cells[3] = 1;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 0U);

    irq.ncells = 2;
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 0U);

    irq.ncells = 3;
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &irq.ctrl, "/intc-off"));
    KTEST_EXPECT_EQ(t, dtb_intid(&fdt, &irq), 0U);
}
KTEST_CASE(dtb_intid_accepts_only_gic_spi_and_ppi);

static void dtb_psci_is_described_or_absent(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node node;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/psci"));
    KTEST_EXPECT_EQ(t, dtb_psci_conduit(&fdt, &node), PSCI_CONDUIT_SMC);
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/psci-v1"));
    KTEST_EXPECT_EQ(t, dtb_psci_conduit(&fdt, &node), PSCI_CONDUIT_HVC);
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/psci-v0"));
    KTEST_EXPECT_EQ(t, dtb_psci_conduit(&fdt, &node), PSCI_CONDUIT_NONE);
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/timer"));
    KTEST_EXPECT_EQ(t, dtb_psci_conduit(&fdt, &node), PSCI_CONDUIT_NONE);

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus/cpu@0"));
    KTEST_EXPECT(t, dtb_cpu_uses_psci(&fdt, &node));
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus/cpu@1"));
    KTEST_EXPECT(t, !dtb_cpu_uses_psci(&fdt, &node));
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus/cpu@2"));
    KTEST_EXPECT(t, !dtb_cpu_uses_psci(&fdt, &node));
}
KTEST_CASE(dtb_psci_is_described_or_absent);

static void dtb_cpu_mpidr_takes_only_affinity(struct ktest *t)
{
    struct fdt fdt;
    struct fdt_node node;
    uint64 mpidr;

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus/cpu@1"));
    KTEST_EXPECT(t, dtb_cpu_mpidr(&fdt, &node, 1, &mpidr) && mpidr == 1);
    KTEST_EXPECT(t, !dtb_cpu_mpidr(&fdt, &node, 2, &mpidr));
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus/cpu@3"));
    KTEST_EXPECT(t, !dtb_cpu_mpidr(&fdt, &node, 1, &mpidr));
    KTEST_EXPECT(t, dtb_cpu_mpidr(&fdt, &node, 2, &mpidr) && mpidr == 3);
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus/cpu@1000000"));
    KTEST_EXPECT(t, !dtb_cpu_mpidr(&fdt, &node, 1, &mpidr));

    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus2/cpu@100000000"));
    KTEST_EXPECT(t, dtb_cpu_mpidr(&fdt, &node, 2, &mpidr)
        && mpidr == 0x100000000UL);
    KTEST_ASSERT(t, fdt_fixture_path(&fdt, &node, "/cpus2/cpu@10000000000"));
    KTEST_EXPECT(t, !dtb_cpu_mpidr(&fdt, &node, 2, &mpidr));
}
KTEST_CASE(dtb_cpu_mpidr_takes_only_affinity);
