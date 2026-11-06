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

#define FDT_MAX_CELLS           4
#define FDT_MAX_IRQ_ARGS        16

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

/* cells are in host order, unlike raw property data. */
struct fdt_irq {
    struct fdt_node ctrl;
    uint32 ncells;
    uint32 cells[FDT_MAX_IRQ_ARGS];
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

int fdt_node_is_compatible(const struct fdt *fdt, const struct fdt_node *node,
    const char *compat);

int fdt_parent(const struct fdt *fdt, const struct fdt_node *node,
    struct fdt_node *parent);

int fdt_node_is_available(const struct fdt *fdt, const struct fdt_node *node);

int fdt_find_path(const struct fdt *fdt, const char *path, uint32 len,
    struct fdt_node *node);

int fdt_translate_reg(const struct fdt *fdt, const struct fdt_node *node,
    uint32 index, uint64 *addrp, uint64 *sizep);

int fdt_irq(const struct fdt *fdt, const struct fdt_node *node, uint32 index,
    struct fdt_irq *irq);

const void *fdt_getprop(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *lenp);
int fdt_prop_u32(const struct fdt *fdt, const struct fdt_node *node,
    const char *name, uint32 *out);
int fdt_node_by_phandle(const struct fdt *fdt, uint32 phandle,
    struct fdt_node *node);

/* Everything in the blob is big-endian. */
uint32 fdt32(uint32 v);
uint64 fdt64(const uint32 *cells);
int fdt_cells(const uint32 *cells, int n, uint64 *out);

#endif  /* _AV6_FDT_H_ */
