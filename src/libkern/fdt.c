/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * fdt.c - flattened device tree reader
 *
 * Layout: a header, then a structure block of nested BEGIN_NODE / PROP /
 * END_NODE tokens, then a strings block holding the property names.
 *
 * fdt_next_tag() is the only function that advances over a token, so it is the
 * only one that validates. Everything else is built on it and inherits the
 * bounds for free.
 */

#include "sys/fdt.h"
#include "sys/kio.h"
#include "sys/string.h"
#include "sys/types.h"

#define FDT_MAGIC           0xD00DFEED
#define FDT_BEGIN_NODE      0x00000001
#define FDT_END_NODE        0x00000002
#define FDT_PROP            0x00000003
#define FDT_NOP             0x00000004
#define FDT_END             0x00000009

#define FDT_FIRST_SUPPORTED_VERSION     0x02
#define FDT_LAST_SUPPORTED_VERSION      0x11

struct fdt_tag {
    uint32 type;
    const char *name;       /* node name, or property name */
    const void *value;      /* properties only */
    uint32 len;
};

uint32 fdt32(uint32 v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8)
        | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

uint64 fdt64(const uint32 *cells)
{
    return ((uint64)fdt32(cells[0]) << 32) | fdt32(cells[1]);
}

int fdt_cells(const uint32 *cells, int n, uint64 *out)
{
    uint64 v;
    int i;

    if (n < 1 || n > FDT_MAX_CELLS) {
        return 0;
    }

    v = 0;

    for (i = 0; i < n; i++) {
        if ((v >> 32) != 0) {
            return 0;
        }

        v = (v << 32) | fdt32(cells[i]);
    }

    *out = v;

    return 1;
}

static uint32 align4(uint32 v)
{
    return (v + 3) & ~3;
}

/*
 * The name comes from the blob, so the terminator may not be there. Returns the
 * length including the NUL, or 0 if it runs past the block.
 */
static uint32 fdt_namelen(const char *name, const char *end)
{
    const char *s = name;

    while (s < end && *s != '\0') {
        s++;
    }

    return (s < end) ? (uint32)(s - name) + 1 : 0;
}

/*
 * Advance over one token and describe it. Offsets and lengths come from the
 * blob, so each is checked against the block extents and against wrapping past
 * the top of the address space, which a plain range test would miss.
 */
static const uint32 *fdt_next_tag(const struct fdt *fdt, const uint32 *p,
    struct fdt_tag *tag)
{
    uint32 nameoff, namelen;

    if (p >= fdt->struct_end) {
        panic("fdt: structure block ends without FDT_END");
    }

    tag->type = fdt32(*p++);
    tag->name = NULL;
    tag->value = NULL;
    tag->len = 0;

    switch (tag->type) {
        case FDT_BEGIN_NODE:
            tag->name = (const char *)p;
            namelen = fdt_namelen(tag->name, (const char *)fdt->struct_end);

            if (namelen == 0) {
                panic("fdt: unterminated node name");
            }

            p += align4(namelen) >> 2;
            break;
        case FDT_PROP:
            if (p + 2 > fdt->struct_end) {
                panic("fdt: truncated property header");
            }

            tag->len = fdt32(*p++);
            nameoff = fdt32(*p++);

            if (fdt->strings + nameoff >= fdt->strings_end) {
                panic("fdt: property name outside the strings block");
            }

            tag->name = fdt->strings + nameoff;

            if ((const char *)p + align4(tag->len) > (const char *)fdt->struct_end
                || (const char *)p + align4(tag->len) < (const char *)p) {
                panic("fdt: property value outside the structure block");
            }

            tag->value = p;
            p += align4(tag->len) >> 2;
            break;
        case FDT_END_NODE:
        case FDT_NOP:
        case FDT_END:
            break;
        default:
            panic("fdt: unknown token %#x", tag->type);
    }

    return p;
}

void fdt_open(struct fdt *fdt, const void *blob)
{
    const uint32 *header;
    uint32 totalsize, version, off_struct, off_strings;
    uint32 size_struct, size_strings;

    header = (const uint32 *)blob;

    if (fdt32(header[0]) != FDT_MAGIC) {
        panic("fdt: no valid device tree (bad magic)");
    }

    version = fdt32(header[5]);

    if (version < FDT_FIRST_SUPPORTED_VERSION
        || fdt32(header[6]) > FDT_LAST_SUPPORTED_VERSION) {
        panic("fdt: unsupported format version %u", version);
    }

    totalsize = fdt32(header[1]);
    off_struct = fdt32(header[2]);
    off_strings = fdt32(header[3]);
    size_strings = fdt32(header[8]);
    size_struct = fdt32(header[9]);

    if (off_struct + size_struct < off_struct
        || off_struct + size_struct > totalsize
        || off_strings + size_strings < off_strings
        || off_strings + size_strings > totalsize) {
        panic("fdt: block outside the %u-byte blob", totalsize);
    }

    fdt->struct_start = (const uint32 *)((const char *)blob + off_struct);
    fdt->struct_end = (const uint32 *)((const char *)blob + off_struct
        + size_struct);
    fdt->strings = (const char *)blob + off_strings;
    fdt->strings_end = fdt->strings + size_strings;
}

int fdt_first_node(const struct fdt *fdt, struct fdt_node *node)
{
    node->props = fdt->struct_start;
    node->name = NULL;
    node->depth = -1;

    return fdt_next_node(fdt, node);
}

/* Document order, so a node's children are visited before its siblings. */
int fdt_next_node(const struct fdt *fdt, struct fdt_node *node)
{
    struct fdt_tag tag;
    const uint32 *p;

    p = node->props;

    for (;;) {
        p = fdt_next_tag(fdt, p, &tag);

        switch (tag.type) {
            case FDT_BEGIN_NODE:
                node->depth++;
                node->name = tag.name;
                node->props = p;
                return 1;
            case FDT_END_NODE:
                node->depth--;
                break;
            case FDT_END:
                return 0;
            default:
                break;
        }
    }
}

int fdt_first_subnode(const struct fdt *fdt, const struct fdt_node *parent,
    struct fdt_node *child)
{
    *child = *parent;

    return fdt_next_subnode(fdt, parent, child);
}

int fdt_next_subnode(const struct fdt *fdt, const struct fdt_node *parent,
    struct fdt_node *child)
{
    while (fdt_next_node(fdt, child)) {
        /* Back at or above the parent means its subtree is done. */
        if (child->depth <= parent->depth) {
            return 0;
        }

        if (child->depth == parent->depth + 1) {
            return 1;
        }
    }

    return 0;
}

int fdt_node_is(const struct fdt_node *node, const char *name)
{
    const char *s = node->name;

    while (*name != '\0') {
        if (*s != *name) {
            return 0;
        }

        s++;
        name++;
    }

    return (*s == '\0' || *s == '@');
}

static int fdt_name_eq(const char *s, const char *name, uint32 len)
{
    uint32 i;

    for (i = 0; i < len; i++) {
        if (s[i] != name[i]) {
            return 0;
        }
    }

    return s[len] == '\0';
}

static const void *fdt_getprop_namelen(const struct fdt *fdt,
    const struct fdt_node *node, const char *name, uint32 namelen, uint32 *lenp)
{
    struct fdt_tag tag;
    const uint32 *p;

    p = node->props;

    for (;;) {
        p = fdt_next_tag(fdt, p, &tag);

        if (tag.type == FDT_NOP) {
            continue;
        }

        /* Properties come before children, so anything else ends the list. */
        if (tag.type != FDT_PROP) {
            return NULL;
        }

        if (fdt_name_eq(tag.name, name, namelen)) {
            if (lenp != NULL) {
                *lenp = tag.len;
            }

            return tag.value;
        }
    }
}

const void *fdt_getprop(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *lenp)
{
    return fdt_getprop_namelen(fdt, node, name, (uint32)strlen(name), lenp);
}

int fdt_prop_u32(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *out)
{
    const uint32 *v;
    uint32 len;

    v = fdt_getprop(fdt, node, name, &len);

    if (v == NULL || len < 4) {
        return 0;
    }

    *out = fdt32(*v);

    return 1;
}

int fdt_node_by_phandle(const struct fdt *fdt, uint32 phandle,
    struct fdt_node *node)
{
    uint32 v;
    int ok;

    for (ok = fdt_first_node(fdt, node); ok; ok = fdt_next_node(fdt, node)) {
        if (fdt_prop_u32(fdt, node, "phandle", &v) && v == phandle) {
            return 1;
        }
    }

    return 0;
}

int fdt_node_is_compatible(const struct fdt *fdt, const struct fdt_node *node,
    const char *compat)
{
    const char *p;
    uint32 len, n;

    p = fdt_getprop(fdt, node, "compatible", &len);

    if (p == NULL) {
        return 0;
    }

    while (len > 0) {
        for (n = 0; n < len && p[n] != '\0'; n++) {
            continue;
        }

        if (n == len) {
            break;
        }

        if (strcmp(p, compat) == 0) {
            return 1;
        }

        n++;
        p += n;
        len -= n;
    }

    return 0;
}

int fdt_parent(const struct fdt *fdt, const struct fdt_node *node,
    struct fdt_node *parent)
{
    struct fdt_node cur, last;
    int ok, found;

    if (node->depth <= 0) {
        return 0;
    }

    found = 0;

    for (ok = fdt_first_node(fdt, &cur); ok; ok = fdt_next_node(fdt, &cur)) {
        if (cur.props == node->props) {
            break;
        }

        if (cur.depth == node->depth - 1) {
            last = cur;
            found = 1;
        }
    }

    if (!found) {
        return 0;
    }

    *parent = last;

    return 1;
}

static int fdt_bus_cells(const struct fdt *fdt, const struct fdt_node *bus,
    int *acells, int *scells)
{
    uint32 v;

    if (!fdt_prop_u32(fdt, bus, "#address-cells", &v)) {
        return 0;
    }

    *acells = (int)v;

    if (!fdt_prop_u32(fdt, bus, "#size-cells", &v)) {
        return 0;
    }

    *scells = (int)v;

    return (*acells >= 1 && *acells <= FDT_MAX_CELLS
        && *scells >= 1 && *scells <= FDT_MAX_CELLS);
}

static int fdt_range_up(const struct fdt *fdt, const struct fdt_node *bus,
    uint64 *addr, int na, int ns, int pna)
{
    const uint32 *ranges;
    uint64 cbase, pbase, size;
    uint32 len, one, off;

    ranges = fdt_getprop(fdt, bus, "ranges", &len);

    if (ranges == NULL) {
        return 0;
    }

    if (len == 0) {
        return 1;
    }

    one = (uint32)(na + pna + ns);
    len >>= 2;

    for (off = 0; off + one <= len; off += one) {
        if (!fdt_cells(ranges + off, na, &cbase)
            || !fdt_cells(ranges + off + na, pna, &pbase)
            || !fdt_cells(ranges + off + na + pna, ns, &size)) {
            return 0;
        }

        if (*addr >= cbase && *addr - cbase < size) {
            if (pbase + (*addr - cbase) < pbase) {
                return 0;
            }

            *addr = pbase + (*addr - cbase);
            return 1;
        }
    }

    return 0;
}

int fdt_translate_reg(const struct fdt *fdt, const struct fdt_node *node,
    uint32 index, uint64 *addrp, uint64 *sizep)
{
    struct fdt_node bus, up;
    const uint32 *reg;
    uint64 addr, size;
    uint32 len;
    int na, ns, pna, pns;

    reg = fdt_getprop(fdt, node, "reg", &len);

    if (reg == NULL || !fdt_parent(fdt, node, &bus)
        || !fdt_bus_cells(fdt, &bus, &na, &ns)) {
        return 0;
    }

    if ((index + 1) * ((uint32)(na + ns) << 2) > len) {
        return 0;
    }

    reg += index * (uint32)(na + ns);

    if (!fdt_cells(reg, na, &addr) || !fdt_cells(reg + na, ns, &size)) {
        return 0;
    }

    while (fdt_parent(fdt, &bus, &up)) {
        if (!fdt_bus_cells(fdt, &up, &pna, &pns)) {
            return 0;
        }

        if (!fdt_range_up(fdt, &bus, &addr, na, ns, pna)) {
            return 0;
        }

        na = pna;
        ns = pns;
        bus = up;
    }

    *addrp = addr;

    if (sizep != NULL) {
        *sizep = size;
    }

    return 1;
}

int fdt_node_is_available(const struct fdt *fdt, const struct fdt_node *node)
{
    const char *status;
    uint32 len;

    status = fdt_getprop(fdt, node, "status", &len);

    if (status == NULL) {
        return 1;
    }

    if (len == 0 || status[len - 1] != '\0') {
        return 0;
    }

    return (strcmp(status, "okay") == 0 || strcmp(status, "ok") == 0);
}

static int fdt_component_eq(const char *name, const char *comp, uint32 len)
{
    uint32 i;

    for (i = 0; i < len; i++) {
        if (name[i] != comp[i]) {
            return 0;
        }
    }

    if (name[len] == '\0') {
        return 1;
    }

    if (name[len] != '@') {
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (comp[i] == '@') {
            return 0;
        }
    }

    return 1;
}

static int fdt_walk_path(const struct fdt *fdt, struct fdt_node *node,
    const char *p, const char *end)
{
    struct fdt_node child;
    const char *q;
    int ok;

    while (p < end) {
        while (p < end && *p == '/') {
            p++;
        }

        if (p == end) {
            break;
        }

        for (q = p; q < end && *q != '/'; q++) {
            continue;
        }

        for (ok = fdt_first_subnode(fdt, node, &child); ok;
            ok = fdt_next_subnode(fdt, node, &child)) {
            if (fdt_component_eq(child.name, p, (uint32)(q - p))) {
                break;
            }
        }

        if (!ok) {
            return 0;
        }

        *node = child;
        p = q;
    }

    return 1;
}

static const char fdt_aliases[] = "aliases";

int fdt_find_path(const struct fdt *fdt, const char *path, uint32 len,
    struct fdt_node *node)
{
    struct fdt_node aliases;
    const char *end, *q, *target;
    uint32 tlen;

    end = path + len;

    if (len == 0 || !fdt_first_node(fdt, node)) {
        return 0;
    }

    if (len > 0 && path[0] != '/') {
        for (q = path; q < end && *q != '/'; q++) {
            continue;
        }

        aliases = *node;

        if (!fdt_walk_path(fdt, &aliases, fdt_aliases,
            fdt_aliases + sizeof(fdt_aliases) - 1)) {
            return 0;
        }

        target = fdt_getprop_namelen(fdt, &aliases, path, (uint32)(q - path),
            &tlen);

        if (target == NULL || tlen < 2 || target[0] != '/'
            || target[tlen - 1] != '\0') {
            return 0;
        }

        if (!fdt_walk_path(fdt, node, target, target + tlen - 1)) {
            return 0;
        }

        path = q;
    }

    return fdt_walk_path(fdt, node, path, end);
}
