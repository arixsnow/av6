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

uint64 fdt_cells(const uint32 *cells, int n)
{
    if (n == 1) {
        return fdt32(*cells);
    }

    if (n == 2) {
        return fdt64(cells);
    }

    panic("fdt: unsupported cell count %d", n);
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

const void *fdt_getprop(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *lenp)
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

        if (strcmp(tag.name, name) == 0) {
            if (lenp != NULL) {
                *lenp = tag.len;
            }

            return tag.value;
        }
    }
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
