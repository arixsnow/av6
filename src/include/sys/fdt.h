/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

#ifndef _AV6_FDT_H_
#define _AV6_FDT_H_

#include "sys/types.h"

/*
 * Flattened device tree reader.
 *
 * The blob is read in place, never copied, so every accessor is bounded by the
 * block extents recorded at open. Structural damage panics: a caller that
 * cannot trust the memory map has nothing to fall back to. An absent property
 * is not damage and returns NULL.
 */

struct fdt {
    const uint32 *struct_start;
    const uint32 *struct_end;
    const char *strings;
    const char *strings_end;
};

/* props is the first token inside the node. The root node is depth 0. */
struct fdt_node {
    const uint32 *props;
    const char *name;
    int depth;
};

void fdt_open(struct fdt *fdt, const void *blob);

/* Iterators return 0 when there is nothing left. */
int fdt_first_node(const struct fdt *fdt, struct fdt_node *node);
int fdt_next_node(const struct fdt *fdt, struct fdt_node *node);
int fdt_first_subnode(const struct fdt *fdt, const struct fdt_node *parent,
    struct fdt_node *child);
int fdt_next_subnode(const struct fdt *fdt, const struct fdt_node *parent,
    struct fdt_node *child);

/* A node name carries a unit address, so the compare stops at the '@'. */
int fdt_node_is(const struct fdt_node *node, const char *name);

const void *fdt_getprop(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *lenp);
int fdt_prop_u32(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *out);
int fdt_node_by_phandle(const struct fdt *fdt, uint32 phandle,
    struct fdt_node *node);

/* Everything in the blob is big-endian. */
uint32 fdt32(uint32 v);
uint64 fdt64(const uint32 *cells);
uint64 fdt_cells(const uint32 *cells, int n);

#endif  /* _AV6_FDT_H_ */
