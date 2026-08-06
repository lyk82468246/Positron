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

/* Parse an element's style="..." declaration list. libcss requires an
 * inline stylesheet flag here; parsing it as a normal rule sheet silently
 * drops the declarations. */
css_stylesheet *pcore_parse_inline_css_internal(const char *css,
        unsigned int len, const char *url, PCoreResolveUrlFn resolve,
        void *resolve_pw, css_error *out_done);

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

/* True when the next public layout must preserve the CSS viewport installed
 * by PCore_SetDeviceViewport while using its physical layout extent. */
extern int pcore_device_viewport_pending;

/* Dynamic selector state retained by the document. Nodes are referenced until
 * replaced or the document is freed. Return 1 when state changed, 0 when it was
 * already equal, and -1 on invalid input/allocation failure. */
int pcore_interaction_set_node(struct dom_document *doc,
        unsigned int state_flags, struct dom_node *node);
void pcore_interaction_snapshot(struct dom_document *doc,
        struct dom_node **focus_node, struct dom_node **active_node,
        struct dom_node **hover_node);

/* Look up raw image bytes cached on `doc` by PCore_FetchImageResources.
 * Returned data is borrowed and remains valid until the document is freed. */
int pcore_image_resource_get(struct dom_document *doc, const char *url,
        const char **out_data, int *out_len);
int pcore_image_resource_retained_get(struct dom_document *doc,
        const char *url, int *out_attempted, void **out_native_image,
        void **out_svg, int *out_width, int *out_height);
int pcore_image_resource_retained_store(struct dom_document *doc,
        const char *url, void *native_image, void *svg,
        int width, int height, const PCoreImageDecodeStats *decode_stats);
void pcore_image_shared_shutdown(void);

/* Build a NetSurf box tree (struct box) from the styled document element
 * `root`, allocating under talloc context `ctx`. Returns the root box, or NULL.
 * The tree is freed by talloc_free(ctx). Boxes borrow DOM node pointers. */
struct box *pcore_box_construct(struct dom_node *root, void *ctx);
struct box *pcore_box_construct_profile(struct dom_node *root, void *ctx,
        PCoreBoxStats *stats);

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
int pcore_font_supports(unsigned long codepoint);

/* The GDI plotter table (defined in pcore_plot_gdi.c), for building a
 * redraw_context to drive NetSurf's html_redraw. */
struct plotter_table;
extern const struct plotter_table pcore_gdi_plotters;

#endif /* PCORE_INTERNAL_H */
