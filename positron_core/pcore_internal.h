/*
 * pcore_internal.h - cross-file API used WITHIN positron_core.dll only.
 *
 * These symbols are not part of the public PCore_* surface in positron_core.h;
 * they let the engine's own translation units share state (e.g. the per-node
 * computed style that PCore_StyleDocument attaches via libdom user-data).
 */

#ifndef PCORE_INTERNAL_H
#define PCORE_INTERNAL_H

#include <dom/dom.h>
#include <libcss/libcss.h>

/* Return the css_computed_style PCore_StyleDocument attached to `node`, or NULL
 * if the node is not an element / has not been styled. Borrowed pointer (owned
 * by the node's user-data). */
css_computed_style *pcore_node_computed_style(struct dom_node *node);

/* The engine's unit-conversion context (viewport + dpi), for feeding NetSurf
 * layout's html_content.unit_len_ctx. Implemented in pcore_select.c. */
const css_unit_ctx *pcore_get_unit_ctx(void);

/* Build a NetSurf box tree (struct box) from the styled document element
 * `root`, allocating under talloc context `ctx`. Returns the root box, or NULL.
 * The tree is freed by talloc_free(ctx). Boxes borrow DOM node pointers. */
struct box *pcore_box_construct(struct dom_node *root, void *ctx);

/* Intern the corestrings the ported NetSurf layout.c references. Call once
 * before layout (PCore_Init). Implemented in pcore_nsshim.c. */
void pcore_nsshim_init(void);

#endif /* PCORE_INTERNAL_H */
