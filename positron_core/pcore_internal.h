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

#include "positron_core.h"

/* Parse one stylesheet through Positron's compatibility transforms. Unlike
 * the public PCore_ParseCSS entry point, this may return CSS_IMPORTS_PENDING
 * so pcore_select can fetch and register the native libcss import tree. */
css_stylesheet *pcore_parse_css_internal(const char *css, unsigned int len,
        const char *url, PCoreResolveUrlFn resolve, void *resolve_pw,
        css_error *out_done);

/* Return the css_computed_style PCore_StyleDocument attached to `node`, or NULL
 * if the node is not an element / has not been styled. Borrowed pointer (owned
 * by the node's user-data). */
css_computed_style *pcore_node_computed_style(struct dom_node *node);

/* Return the default computed style retained on a styled document. Anonymous
 * NetSurf boxes compose this with their parent style. Borrowed pointer. */
css_computed_style *pcore_document_default_style(struct dom_document *doc);

/* The engine's unit-conversion context (viewport + dpi), for feeding NetSurf
 * layout's html_content.unit_len_ctx. Implemented in pcore_select.c. */
const css_unit_ctx *pcore_get_unit_ctx(void);

/* Look up raw image bytes cached on `doc` by PCore_FetchImageResources.
 * Returned data is borrowed and remains valid until the document is freed. */
int pcore_image_resource_get(struct dom_document *doc, const char *url,
        const char **out_data, int *out_len);

/* Build a NetSurf box tree (struct box) from the styled document element
 * `root`, allocating under talloc context `ctx`. Returns the root box, or NULL.
 * The tree is freed by talloc_free(ctx). Boxes borrow DOM node pointers. */
struct box *pcore_box_construct(struct dom_node *root, void *ctx);

/* Overflow scrollbar helpers owned by pcore_box_inspect.c. */
struct scrollbar;
bool pcore_scrollbar_is_dragging(struct scrollbar *scrollbar);
void pcore_box_scrollbars_destroy(struct box *box);

/* Intern the corestrings the ported NetSurf layout.c references. Call once
 * before layout (PCore_Init). Implemented in pcore_nsshim.c. */
void pcore_nsshim_init(void);

/* WM GDI fallback fonts, loaded from the fonts directory beside the DLL. */
int pcore_font_initialize(HMODULE module);
void pcore_font_shutdown(void);
void pcore_font_status(int *symbols_loaded, int *emoji_loaded);

/* The GDI plotter table (defined in pcore_plot_gdi.c), for building a
 * redraw_context to drive NetSurf's html_redraw. */
struct plotter_table;
extern const struct plotter_table pcore_gdi_plotters;

#endif /* PCORE_INTERNAL_H */
