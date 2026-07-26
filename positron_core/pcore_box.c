/*
 * pcore_box.c - slim box-tree builder for the NetSurf layout/redraw port.
 *
 * NetSurf's real box_construct.c is unportable (talloc scheduler, corestrings,
 * html_content, form/object/iframe handling). This is a deliberately slim
 * replacement: it walks the styled DOM (computed styles attached by
 * PCore_StyleDocument) and emits a NetSurf `struct box` tree of the SAME shape
 * that the ported layout.c / redraw.c consume - block boxes, anonymous inline
 * containers, inline boxes (with INLINE_END markers), inline-blocks and text
 * boxes. It also performs the inline/block normalisation NetSurf's
 * box_normalise.c would (wrapping runs of inline content in anonymous
 * BOX_INLINE_CONTAINER), so the tree is layout-ready.
 *
 * Current scope: block/inline text, inline-block, flex, common table
 * structures, including NetSurf's table-span occupancy, plus cached <img>,
 * read-only checkbox/radio gadgets and CSS background-image resources decoded
 * by WM Imaging/libsvgtiny, are built for NetSurf's real layout/redraw path.
 * Editable/submittable forms and floats remain staged follow-ups.
 * Boxes borrow DOM node pointers (the document outlives the box tree) and are
 * allocated under one talloc context, freed in a single talloc_free.
 *
 * C89.
 */

#include <windows.h>
#include <stdlib.h>   /* malloc / free for the per-document render state */
#include <string.h>

#include <dom/dom.h>
#include <libcss/libcss.h>

#include "utils/talloc.h"
#include "utils/corestrings.h"
#include "content/handlers/html/box.h"

#include "positron_core.h"
#include "positron_image.h"
#include "pcore_internal.h"

#include "utils/errors.h"                    /* nserror (layout.h / private.h) */
#include "content/handlers/html/form_internal.h"
#include "netsurf/layout.h"                  /* struct gui_layout_table */
#include "content/handlers/html/private.h"   /* html_content (real NetSurf) */
#include "content/handlers/html/layout.h"    /* layout_document */
#include "content/handlers/html/box_inspect.h" /* box_coords */
#include "netsurf/content.h"                  /* content_redraw_data */
#include "netsurf/bitmap.h"                   /* thin WM Imaging carrier */
#include "netsurf/plotters.h"                 /* struct redraw_context */
#include "desktop/scrollbar.h"

/* GDI font measurement table (pcore_plot_gdi.c, M2). */
extern const struct gui_layout_table pcore_gdi_layout;

/* Referenced (extern) by content/handlers/css/utils.h; the device DPI in fixed
 * point. layout uses it for unit conversion. Set from PCore_SetViewport's dpi
 * in a later milestone; 96dpi default for now. */
css_fixed nscss_screen_dpi = 96 * (1 << CSS_RADIX_POINT);

/* ------------------------------------------------------------------ */
/* box allocation (over the talloc shim) + tree linking                */
/* ------------------------------------------------------------------ */

/* Allocate a zeroed box of `type` under talloc context `ctx`, carrying `style`
 * (borrowed unless an anonymous box owns it below). Mirrors box_create() in
 * NetSurf's box_manipulate.c initialises, minus the form/scrollbar/url state
 * our slim builder never produces. */
static struct box *pcore_box_new(box_type type, css_computed_style *style,
        void *ctx)
{
    struct box *b = talloc_zero(ctx, struct box);
    if (b == NULL) {
        return NULL;
    }
    b->type = type;
    b->style = style;
    b->width = UNKNOWN_WIDTH;
    b->max_width = UNKNOWN_MAX_WIDTH;
    b->columns = 1;
    b->rows = 1;
    b->list_value = 1;
    /* talloc_zero cleared everything else (pointers NULL, ints 0). */
    return b;
}

typedef struct pcore_owned_style {
    css_computed_style *style;
} pcore_owned_style;

static int pcore_owned_style_destroy(pcore_owned_style *owned)
{
    if (owned->style != NULL) {
        css_computed_style_destroy(owned->style);
        owned->style = NULL;
    }
    return 0;
}

/* NetSurf's normaliser gives implied table boxes a blank style composed with
 * the parent. Keep the composed style as a talloc child of its box so the box
 * tree owns it without changing the borrowed-style contract of DOM boxes. */
static struct box *pcore_make_anonymous_box(box_type type,
        css_computed_style *parent_style, dom_node *source_node, void *ctx,
        PCoreBoxStats *stats)
{
    struct box *box;
    pcore_owned_style *owned;
    dom_document *doc = NULL;
    css_computed_style *base;
    DWORD started = (stats != NULL) ? GetTickCount() : 0;

    if (parent_style == NULL || source_node == NULL ||
            dom_node_get_owner_document(source_node, &doc) != DOM_NO_ERR ||
            doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        box = NULL;
        goto done;
    }
    base = pcore_document_default_style(doc);
    dom_node_unref((dom_node *) doc);
    if (base == NULL) {
        box = NULL;
        goto done;
    }

    box = pcore_box_new(type, NULL, ctx);
    if (box == NULL) {
        goto done;
    }
    owned = talloc_zero(box, pcore_owned_style);
    if (owned == NULL) {
        talloc_free(box);
        box = NULL;
        goto done;
    }
    talloc_set_destructor(owned, pcore_owned_style_destroy);
    if (css_computed_style_compose(parent_style, base, pcore_get_unit_ctx(),
            &owned->style) != CSS_OK || owned->style == NULL) {
        talloc_free(box);
        box = NULL;
        goto done;
    }
    box->style = owned->style;
done:
    if (stats != NULL) {
        stats->anonymous_ms += GetTickCount() - started;
        stats->anonymous_calls++;
    }
    return box;
}

static css_computed_style *pcore_profile_style(dom_node *node,
        PCoreBoxStats *stats)
{
    css_computed_style *style;
    DWORD started = (stats != NULL) ? GetTickCount() : 0;

    style = pcore_node_computed_style(node);
    if (stats != NULL) {
        stats->style_ms += GetTickCount() - started;
        stats->style_calls++;
    }
    return style;
}

/* Append `child` as the last child of `parent` (NetSurf box_add_child). */
static void pcore_box_add_child(struct box *parent, struct box *child)
{
    if (parent->children != NULL) {
        parent->last->next = child;
        child->prev = parent->last;
    } else {
        parent->children = child;
        child->prev = NULL;
    }
    parent->last = child;
    child->parent = parent;
    child->next = NULL;
}

static void pcore_box_attach_dom_node(struct box *box, dom_node *node)
{
    void *old_box = NULL;

    box->node = node;
    if (node != NULL && corestring_dom___ns_key_box_node_data != NULL) {
        dom_node_set_user_data(node, corestring_dom___ns_key_box_node_data,
                box, NULL, &old_box);
    }
}

/* ------------------------------------------------------------------ */
/* display classification                                              */
/* ------------------------------------------------------------------ */

/* True if `style`'s display is an inline-level value. */
static int pcore_is_inline_level(css_computed_style *style, int is_root)
{
    uint8_t d = css_computed_display(style, is_root ? true : false);
    return (d == CSS_DISPLAY_INLINE ||
            d == CSS_DISPLAY_INLINE_BLOCK ||
            d == CSS_DISPLAY_INLINE_TABLE ||
            d == CSS_DISPLAY_INLINE_FLEX ||
            d == CSS_DISPLAY_INLINE_GRID) ? 1 : 0;
}

/* True if display is none (box not generated). */
static int pcore_is_display_none(css_computed_style *style, int is_root)
{
    return (css_computed_display(style, is_root ? true : false) ==
            CSS_DISPLAY_NONE) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* text / whitespace                                                   */
/* ------------------------------------------------------------------ */

static int pcore_text_all_ws(const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return 0;
        }
    }
    return 1;
}

static int pcore_node_name_is(dom_node *node, const char *want)
{
    dom_string *name = NULL;
    dom_string *want_name = NULL;
    int match = 0;

    if (node == NULL || want == NULL) {
        return 0;
    }
    if (dom_node_get_node_name(node, &name) != DOM_NO_ERR || name == NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) want, strlen(want), &want_name) ==
            DOM_NO_ERR && want_name != NULL) {
        match = dom_string_caseless_isequal(name, want_name) ? 1 : 0;
        dom_string_unref(want_name);
    }
    dom_string_unref(name);
    return match;
}

/* Copy an attribute's raw UTF-8 bytes into the box-tree talloc context.
 * Returns 1 when the attribute is present, even if its value is empty. */
static int pcore_copy_attr_text(dom_node *node, const char *attr, void *ctx,
        char **out, size_t *out_len)
{
    dom_string *name = NULL;
    dom_string *value = NULL;
    int present = 0;

    *out = NULL;
    *out_len = 0;
    if (dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
            DOM_NO_ERR) {
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        const char *data = dom_string_data(value);
        size_t len = dom_string_byte_length(value);

        present = 1;
        if (data != NULL && len > 0) {
            char *copy = (char *) talloc_memdup(ctx, data, len);
            if (copy != NULL) {
                *out = copy;
                *out_len = len;
            }
        }
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return present;
}

static int pcore_attr_value_is(dom_node *node, const char *attr,
        const char *want)
{
    dom_string *name = NULL;
    dom_string *value = NULL;
    dom_string *wanted = NULL;
    int match = 0;

    if (node == NULL || attr == NULL || want == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR ||
            dom_string_create((const uint8_t *) want, strlen(want), &wanted) !=
                    DOM_NO_ERR) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        if (wanted != NULL) {
            dom_string_unref(wanted);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        match = dom_string_caseless_isequal(value, wanted) ? 1 : 0;
        dom_string_unref(value);
    }
    dom_string_unref(wanted);
    dom_string_unref(name);
    return match;
}

static int pcore_node_has_attr(dom_node *node, const char *attr)
{
    dom_string *name = NULL;
    bool present = false;

    if (node == NULL || attr == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR) {
        return 0;
    }
    if (dom_element_has_attribute(node, name, &present) != DOM_NO_ERR) {
        present = false;
    }
    dom_string_unref(name);
    return present ? 1 : 0;
}

/* NetSurf box_special.c attaches a form_control to checkbox/radio boxes and
 * lets the already-ported layout.c/redraw.c supply their 1em geometry and
 * platform-independent painting. Positron does not yet build NetSurf's full
 * form list, so retain only the fields those read-only paths consume. */
static int pcore_form_toggle_type(dom_node *node)
{
    if (!pcore_node_name_is(node, "input")) {
        return 0;
    }
    if (pcore_attr_value_is(node, "type", "checkbox")) {
        return GADGET_CHECKBOX;
    }
    if (pcore_attr_value_is(node, "type", "radio")) {
        return GADGET_RADIO;
    }
    return 0;
}

static struct box *pcore_make_form_toggle_box(dom_node *node,
        css_computed_style *style, void *ctx, int gadget_type)
{
    struct box *box;
    struct form_control *gadget;
    dom_html_input_element *input;
    bool selected = false;
    bool disabled = false;

    if (gadget_type != GADGET_CHECKBOX && gadget_type != GADGET_RADIO) {
        return NULL;
    }
    box = pcore_box_new(BOX_INLINE_BLOCK, style, ctx);
    if (box == NULL) {
        return NULL;
    }
    gadget = talloc_zero(box, struct form_control);
    if (gadget == NULL) {
        talloc_free(box);
        return NULL;
    }
    pcore_box_attach_dom_node(box, node);
    box->flags |= IS_REPLACED;
    box->gadget = gadget;
    gadget->node = node;
    gadget->type = (form_control_type) gadget_type;
    gadget->box = box;
    input = (dom_html_input_element *) node;
    if (dom_html_input_element_get_checked(input, &selected) != DOM_NO_ERR) {
        selected = pcore_node_has_attr(node, "checked") ? true : false;
    }
    if (dom_html_input_element_get_disabled(input, &disabled) != DOM_NO_ERR) {
        disabled = pcore_node_has_attr(node, "disabled") ? true : false;
    }
    gadget->selected = selected;
    gadget->disabled = disabled;
    return box;
}

static struct box *pcore_make_owned_text_box(dom_node *owner,
        css_computed_style *style, void *ctx, char *text, size_t len)
{
    struct box *b;

    if (text == NULL || len == 0) {
        return NULL;
    }
    b = pcore_box_new(BOX_TEXT, style, ctx);
    if (b != NULL) {
        b->node = owner;
        b->text = text;
        b->length = len;
    }
    return b;
}

static struct box *pcore_make_literal_text_box(dom_node *owner,
        css_computed_style *style, void *ctx, const char *text)
{
    char *copy;
    size_t len;

    if (text == NULL) {
        return NULL;
    }
    len = strlen(text);
    if (len == 0) {
        return NULL;
    }
    copy = (char *) talloc_memdup(ctx, text, len);
    return pcore_make_owned_text_box(owner, style, ctx, copy, len);
}

/* The portable NetSurf part of an image is a replaced box with an object.
 * Our object is the small nsshim image carrier. Encoded bytes are owned by the
 * document cache; retained bitmap/SVG state is released by its destructor.
 * layout.c reads its intrinsic dimensions through
 * content_get_width/height; redraw.c reaches plot_bitmap through
 * content_redraw. Return NULL for absent/cache-miss/undecodable resources so
 * the caller can retain the established alt/src fallback. */
static int pcore_bitmap_destroy(struct bitmap *bitmap)
{
    if (bitmap != NULL && bitmap->owns_retained &&
            bitmap->kind == PCORE_BITMAP_WM_IMAGE &&
            bitmap->native_image != NULL) {
        PImage_FreeBitmap((PIMAGE_BITMAP) bitmap->native_image);
        bitmap->native_image = NULL;
    }
    if (bitmap != NULL && bitmap->owns_retained &&
            bitmap->kind == PCORE_BITMAP_SVG &&
            bitmap->svg != NULL) {
        PImage_FreeSvg((PIMAGE_SVG) bitmap->svg);
        bitmap->svg = NULL;
    }
    return 0;
}

static int pcore_image_bytes_are_markup(const char *data, int len)
{
    int offset = 0;
    unsigned char c;

    if (data == NULL || len <= 0) {
        return 0;
    }
    if (len >= 3 &&
            (unsigned char) data[0] == 0xef &&
            (unsigned char) data[1] == 0xbb &&
            (unsigned char) data[2] == 0xbf) {
        offset = 3;
    }
    while (offset < len) {
        c = (unsigned char) data[offset];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' &&
                c != '\f') {
            break;
        }
        offset++;
    }
    return offset < len && data[offset] == '<';
}

static struct bitmap *pcore_make_cached_bitmap(dom_document *doc,
        const char *url, void *ctx, PCoreBoxStats *stats)
{
    const char *data = NULL;
    int len = 0;
    int width = 0;
    int height = 0;
    int kind = 0;
    int retained_attempted = 0;
    int cache_owns_retained = 0;
    int markup_first = 0;
    void *cached_native = NULL;
    void *cached_svg = NULL;
    PIMAGE_SVG_CREATE_STATS svg_create_stats;
    PCoreImageDecodeStats decode_stats;
    PIMAGE_BITMAP native_image = NULL;
    PIMAGE_SVG svg = NULL;
    struct bitmap *bitmap;
    DWORD started = (stats != NULL) ? GetTickCount() : 0;

    memset(&svg_create_stats, 0, sizeof(svg_create_stats));
    memset(&decode_stats, 0, sizeof(decode_stats));
    if (doc == NULL || url == NULL || url[0] == '\0' ||
            pcore_image_resource_get(doc, url, &data, &len) != 0) {
        bitmap = NULL;
        goto done;
    }

    if (pcore_image_resource_retained_get(doc, url,
            &retained_attempted, &cached_native, &cached_svg,
            &width, &height) == 0 && retained_attempted) {
        native_image = (PIMAGE_BITMAP) cached_native;
        svg = (PIMAGE_SVG) cached_svg;
        if (native_image != NULL && width > 0 && height > 0) {
            kind = PCORE_BITMAP_WM_IMAGE;
        } else if (svg != NULL && width > 0 && height > 0) {
            kind = PCORE_BITMAP_SVG;
        }
        if (kind != 0 && stats != NULL) {
            stats->image_reuses++;
        }
        cache_owns_retained = 1;
        goto decoded;
    }

    markup_first = pcore_image_bytes_are_markup(data, len);
    if (markup_first) {
        if (stats != NULL) {
            stats->image_markup_first++;
        }
        if (PImage_CreateSvgFromMemory(data, len, 0, 0, &svg) ==
                PIMAGE_OK && svg != NULL &&
                PImage_SvgGetInfo(svg, &width, &height, NULL) ==
                PIMAGE_OK && width > 0 && height > 0) {
            kind = PCORE_BITMAP_SVG;
        } else if (svg != NULL) {
            PImage_FreeSvg(svg);
            svg = NULL;
        }
    }
    if (kind == 0 && PImage_CreateBitmapFromMemory(data, len,
            &native_image) == PIMAGE_OK && native_image != NULL) {
        if (PImage_BitmapGetInfo(native_image, &width, &height) ==
                PIMAGE_OK && width > 0 && height > 0) {
            kind = PCORE_BITMAP_WM_IMAGE;
        } else {
            PImage_FreeBitmap(native_image);
            native_image = NULL;
        }
    }
    if (kind == 0 && !markup_first &&
            PImage_CreateSvgFromMemory(data, len, 0, 0, &svg) ==
            PIMAGE_OK && svg != NULL &&
            PImage_SvgGetInfo(svg, &width, &height, NULL) == PIMAGE_OK &&
            width > 0 && height > 0) {
        kind = PCORE_BITMAP_SVG;
    } else if (kind == 0 && svg != NULL) {
        PImage_FreeSvg(svg);
        svg = NULL;
    }

    if (kind == PCORE_BITMAP_SVG &&
            PImage_SvgGetCreateStats(svg, &svg_create_stats) == PIMAGE_OK) {
        decode_stats.svg_total_ms = svg_create_stats.total_ms;
        decode_stats.svg_setup_ms = svg_create_stats.setup_ms;
        decode_stats.svg_parse_ms = svg_create_stats.parse_ms;
        decode_stats.svg_raster_ms = svg_create_stats.raster_ms;
        decode_stats.svg_creates = 1;
    }
    if (pcore_image_resource_retained_store(doc, url,
            (void *) native_image, (void *) svg, width, height,
            &decode_stats) == 0) {
        cache_owns_retained = 1;
    }
decoded:
    if (kind == 0) {
        if (!cache_owns_retained && native_image != NULL) {
            PImage_FreeBitmap(native_image);
        }
        if (!cache_owns_retained && svg != NULL) {
            PImage_FreeSvg(svg);
        }
        bitmap = NULL;
        goto done;
    }

    bitmap = talloc_zero(ctx, struct bitmap);
    if (bitmap == NULL) {
        if (!cache_owns_retained && native_image != NULL) {
            PImage_FreeBitmap(native_image);
        }
        if (!cache_owns_retained && svg != NULL) {
            PImage_FreeSvg(svg);
        }
        bitmap = NULL;
        goto done;
    }
    bitmap->kind = kind;
    bitmap->data = data;
    bitmap->len = len;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->native_image = (void *) native_image;
    bitmap->svg = (void *) svg;
    bitmap->owns_retained = !cache_owns_retained;
    talloc_set_destructor(bitmap, pcore_bitmap_destroy);
done:
    if (stats != NULL) {
        stats->image_ms += GetTickCount() - started;
        stats->image_calls++;
    }
    return bitmap;
}

static struct box *pcore_make_cached_image_box(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    char *src = NULL;
    size_t src_len = 0;
    char *url = NULL;
    dom_document *doc = NULL;
    struct bitmap *bitmap;
    struct box *box;

    if (!pcore_node_name_is(node, "img") ||
            !pcore_copy_attr_text(node, "src", ctx, &src, &src_len) ||
            src == NULL || src_len == 0) {
        return NULL;
    }
    url = (char *) malloc(src_len + 1);
    if (url == NULL) {
        return NULL;
    }
    memcpy(url, src, src_len);
    url[src_len] = '\0';
    if (dom_node_get_owner_document(node, &doc) != DOM_NO_ERR ||
            doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        free(url);
        return NULL;
    }
    bitmap = pcore_make_cached_bitmap(doc, url, ctx, stats);
    dom_node_unref((dom_node *) doc);
    free(url);
    if (bitmap == NULL) {
        return NULL;
    }

    box = pcore_box_new(BOX_INLINE, style, ctx);
    if (box == NULL) {
        return NULL;
    }
    box->node = node;
    box->object = (struct hlcache_handle *) bitmap;
    box->flags |= IS_REPLACED;
    return box;
}

static void pcore_attach_cached_backgrounds(struct box *box, void *ctx)
{
    struct box *child;

    if (box == NULL) {
        return;
    }
    if (box->background == NULL && box->style != NULL && box->node != NULL &&
            box->type != BOX_TEXT && box->type != BOX_INLINE_END) {
        lwc_string *uri = NULL;
        if (css_computed_background_image(box->style, &uri) ==
                CSS_BACKGROUND_IMAGE_IMAGE && uri != NULL) {
            dom_document *doc = NULL;
            const char *data = lwc_string_data(uri);
            size_t len = lwc_string_length(uri);
            char *url = (char *) malloc(len + 1);

            if (url != NULL) {
                memcpy(url, data, len);
                url[len] = '\0';
                if (dom_node_get_owner_document(box->node, &doc) ==
                        DOM_NO_ERR && doc != NULL) {
                    box->background = (struct hlcache_handle *)
                            pcore_make_cached_bitmap(doc, url, ctx, NULL);
                    dom_node_unref((dom_node *) doc);
                }
                free(url);
            }
        }
    }
    for (child = box->children; child != NULL; child = child->next) {
        pcore_attach_cached_backgrounds(child, ctx);
    }
}

/* Cache misses and decode failures retain accessible fallback text. alt=""
 * stays silent, matching the prior rendering behaviour. */
static struct box *pcore_make_image_fallback_box(dom_node *node,
        css_computed_style *style, void *ctx)
{
    char *text;
    size_t len;

    if (!pcore_node_name_is(node, "img")) {
        return NULL;
    }
    if (pcore_copy_attr_text(node, "alt", ctx, &text, &len)) {
        return pcore_make_owned_text_box(node, style, ctx, text, len);
    }
    if (pcore_copy_attr_text(node, "src", ctx, &text, &len) &&
            text != NULL && len > 0) {
        return pcore_make_owned_text_box(node, style, ctx, text, len);
    }
    return pcore_make_literal_text_box(node, style, ctx, "[image]");
}

/* Create a BOX_TEXT for a DOM text node, copying its UTF-8 into the talloc ctx.
 * Returns NULL for empty text. `style` is the governing inline style. */
static struct box *pcore_make_text_box(dom_node *tnode,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    dom_string *txt = NULL;
    struct box *b = NULL;
    DWORD started = (stats != NULL) ? GetTickCount() : 0;

    if (dom_node_get_text_content(tnode, &txt) != DOM_NO_ERR || txt == NULL) {
        goto done;
    }
    {
        const char *data = dom_string_data(txt);
        size_t len = dom_string_byte_length(txt);

        if (data != NULL && len > 0) {
            b = pcore_box_new(BOX_TEXT, style, ctx);
            if (b != NULL) {
                char *copy = (char *) talloc_size(ctx, len + 1);
                if (copy != NULL) {
                    uint8_t ws = (style != NULL) ?
                            css_computed_white_space(style) :
                            CSS_WHITE_SPACE_NORMAL;
                    size_t out_len = 0;
                    size_t i;

                    if (ws == CSS_WHITE_SPACE_NORMAL ||
                            ws == CSS_WHITE_SPACE_NOWRAP) {
                        int in_space = 0;
                        for (i = 0; i < len; i++) {
                            char c = data[i];
                            int is_space = (c == ' ' || c == '\t' ||
                                    c == '\r' || c == '\n' || c == '\f');
                            if (is_space) {
                                if (!in_space) {
                                    copy[out_len++] = ' ';
                                    in_space = 1;
                                }
                            } else {
                                copy[out_len++] = c;
                                in_space = 0;
                            }
                        }
                    } else {
                        memcpy(copy, data, len);
                        out_len = len;
                    }
                    copy[out_len] = '\0';
                    b->text = copy;
                    b->length = out_len;
                } else {
                    b->text = NULL;
                    b->length = 0;
                }
            }
        }
    }
    dom_string_unref(txt);
done:
    if (stats != NULL) {
        stats->text_ms += GetTickCount() - started;
        stats->text_calls++;
    }
    return b;
}

/* NetSurf stores collapsible whitespace between text runs as the width of a
 * trailing space on the previous box. Keep text boxes free of raw layout
 * whitespace so GDI never receives LF/TAB glyphs for white-space:normal. */
static void pcore_box_add_text(struct box *parent, struct box *text)
{
    int leading;
    int trailing;

    if (parent == NULL || text == NULL || text->text == NULL) {
        return;
    }
    leading = (text->length > 0 && text->text[0] == ' ');
    trailing = (text->length > 0 &&
            text->text[text->length - 1] == ' ');
    if (leading) {
        if (parent->last != NULL) {
            parent->last->space = UNKNOWN_WIDTH;
        }
        text->length--;
        memmove(text->text, text->text + 1, text->length);
    }
    if (trailing && text->length > 0) {
        text->length--;
        text->space = UNKNOWN_WIDTH;
    }
    text->text[text->length] = '\0';
    if (text->length > 0) {
        pcore_box_add_child(parent, text);
    }
}

/* ------------------------------------------------------------------ */
/* construction                                                        */
/* ------------------------------------------------------------------ */

static struct box *pcore_construct_block(dom_node *node,
        css_computed_style *style, int is_root, void *ctx,
        PCoreBoxStats *stats);
static void pcore_construct_inline(dom_node *node, css_computed_style *style,
        struct box *cont, void *ctx, PCoreBoxStats *stats);
static struct box *pcore_construct_table(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats);
static struct box *pcore_construct_flex(dom_node *node,
        css_computed_style *style, int is_inline, void *ctx,
        PCoreBoxStats *stats);
static struct box *pcore_construct_row(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats);
static struct box *pcore_construct_rowgroup(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats);

/* Flatten the inline content of `node` into inline container `cont`: text
 * children become BOX_TEXT; inline element children emit BOX_INLINE ...
 * BOX_INLINE_END around their (recursively flattened) content; inline-block
 * children become an atomic BOX_INLINE_BLOCK holding a block subtree. */
static void pcore_construct_inline(dom_node *node, css_computed_style *style,
        struct box *cont, void *ctx, PCoreBoxStats *stats)
{
    dom_node *child;

    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR) {
            if (type == DOM_TEXT_NODE) {
                struct box *t = pcore_make_text_box(child, style, ctx, stats);
                if (t != NULL) {
                    t->node = node;   /* attribute text to its inline
                                       * element (e.g. <a>) for hit-testing */
                    pcore_box_add_text(cont, t);
                }
            } else if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_profile_style(child, stats);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    uint8_t d = css_computed_display(cs, false);
                    int gadget_type = pcore_form_toggle_type(child);
                    if (gadget_type != 0) {
                        struct box *gadget = pcore_make_form_toggle_box(child,
                                cs, ctx, gadget_type);
                        if (gadget != NULL) {
                            pcore_box_add_child(cont, gadget);
                        }
                    } else if (pcore_node_name_is(child, "img")) {
                        struct box *img = pcore_make_cached_image_box(child,
                                cs, ctx, stats);
                        if (img == NULL) {
                            img = pcore_make_image_fallback_box(child, cs,
                                    ctx);
                        }
                        if (img != NULL) {
                            pcore_box_add_child(cont, img);
                        }
                    } else if (d == CSS_DISPLAY_INLINE_BLOCK ||
                            d == CSS_DISPLAY_INLINE_TABLE ||
                            d == CSS_DISPLAY_INLINE_FLEX ||
                            d == CSS_DISPLAY_INLINE_GRID) {
                        /* atomic inline: a block subtree typed inline-block */
                        struct box *ib = pcore_construct_block(child, cs, 0,
                                ctx, stats);
                        if (ib != NULL) {
                            ib->type = BOX_INLINE_BLOCK;
                            pcore_box_add_child(cont, ib);
                        }
                    } else {
                        /* inline element: INLINE ... INLINE_END markers */
                        struct box *start = pcore_box_new(BOX_INLINE, cs, ctx);
                        struct box *end;
                        if (start != NULL) {
                            start->node = child;
                            pcore_box_add_child(cont, start);
                            pcore_construct_inline(child, cs, cont, ctx,
                                    stats);
                            end = pcore_box_new(BOX_INLINE_END, cs, ctx);
                            if (end != NULL) {
                                start->inline_end = end;
                                end->inline_end = start;
                                pcore_box_add_child(cont, end);
                            }
                        }
                    }
                }
            }
        }

        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

/* Build a BOX_BLOCK for `node`, normalising children: runs of inline-level
 * content are wrapped in anonymous BOX_INLINE_CONTAINER boxes; block-level
 * children are added directly. */
static struct box *pcore_construct_block(dom_node *node,
        css_computed_style *style, int is_root, void *ctx,
        PCoreBoxStats *stats)
{
    struct box *box;
    struct box *inline_cont = NULL;
    dom_node *child;

    box = pcore_box_new(BOX_BLOCK, style, ctx);
    if (box == NULL) {
        return NULL;
    }
    pcore_box_attach_dom_node(box, node);

    if (css_computed_display(style, is_root ? true : false) ==
            CSS_DISPLAY_LIST_ITEM) {
        struct box *marker = pcore_box_new(BOX_BLOCK, style, box);
        enum css_list_style_type_e marker_type;
        lwc_string *image_uri = NULL;
        if (marker == NULL) {
            talloc_free(box);
            return NULL;
        }
        marker_type = css_computed_list_style_type(style);
        if (marker_type == CSS_LIST_STYLE_TYPE_DISC) {
            marker->text = (char *) "\342\200\242";
            marker->length = 3;
        } else if (marker_type == CSS_LIST_STYLE_TYPE_CIRCLE) {
            marker->text = (char *) "\342\227\213";
            marker->length = 3;
        } else if (marker_type == CSS_LIST_STYLE_TYPE_SQUARE) {
            marker->text = (char *) "\342\226\252";
            marker->length = 3;
        } else {
            marker->text = NULL;
            marker->length = 0;
        }
        if (css_computed_list_style_image(style, &image_uri) ==
                CSS_LIST_STYLE_IMAGE_URI && image_uri != NULL) {
            const char *uri_data = lwc_string_data(image_uri);
            size_t uri_len = lwc_string_length(image_uri);
            char *url = (char *) malloc(uri_len + 1);
            dom_document *doc = NULL;

            if (url != NULL) {
                memcpy(url, uri_data, uri_len);
                url[uri_len] = '\0';
                if (dom_node_get_owner_document(node, &doc) == DOM_NO_ERR &&
                        doc != NULL) {
                    marker->object = (struct hlcache_handle *)
                            pcore_make_cached_bitmap(doc, url, ctx, stats);
                    dom_node_unref((dom_node *) doc);
                }
                free(url);
            }
        }
        box->list_marker = marker;
        marker->parent = box;

        /* CSS Lists places an inside marker as the first inline content of
         * the list item. A zero-width anonymous run lets NetSurf create that
         * line even when the first author child is block-level or absent;
         * layout.c reserves and paints the retained marker itself. */
        if (css_computed_list_style_position(style) ==
                CSS_LIST_STYLE_POSITION_INSIDE) {
            struct box *placeholder;

            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER, NULL, ctx);
            if (inline_cont == NULL) {
                talloc_free(box);
                return NULL;
            }
            placeholder = pcore_box_new(BOX_TEXT, style, ctx);
            if (placeholder == NULL) {
                talloc_free(inline_cont);
                talloc_free(box);
                return NULL;
            }
            placeholder->text = (char *) "";
            placeholder->length = 0;
            pcore_box_add_child(inline_cont, placeholder);
            pcore_box_add_child(box, inline_cont);
        }
    }

    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return box;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR) {
            if (type == DOM_TEXT_NODE) {
                dom_string *txt = NULL;
                int skip = 0;
                if (dom_node_get_text_content(child, &txt) == DOM_NO_ERR &&
                        txt != NULL) {
                    const char *d = dom_string_data(txt);
                    size_t l = dom_string_byte_length(txt);
                    /* Inter-block whitespace with no open inline run: drop. */
                    if (inline_cont == NULL && (d == NULL || l == 0 ||
                            pcore_text_all_ws(d, l))) {
                        skip = 1;
                    }
                    dom_string_unref(txt);
                }
                if (!skip) {
                    struct box *t = pcore_make_text_box(child, style, ctx,
                            stats);
                    if (t != NULL) {
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        pcore_box_add_text(inline_cont, t);
                    }
                }
            } else if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_profile_style(child, stats);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    int gadget_type = pcore_form_toggle_type(child);
                    if (gadget_type != 0) {
                        struct box *gadget;
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        gadget = pcore_make_form_toggle_box(child, cs, ctx,
                                gadget_type);
                        if (gadget != NULL) {
                            pcore_box_add_child(inline_cont, gadget);
                        }
                    } else if (pcore_node_name_is(child, "img")) {
                        struct box *img;
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        img = pcore_make_cached_image_box(child, cs, ctx,
                                stats);
                        if (img == NULL) {
                            img = pcore_make_image_fallback_box(child, cs,
                                    ctx);
                        }
                        if (img != NULL) {
                            pcore_box_add_child(inline_cont, img);
                        }
                    } else if (pcore_is_inline_level(cs, 0)) {
                        uint8_t d = css_computed_display(cs, false);
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        if (d == CSS_DISPLAY_INLINE_BLOCK ||
                                d == CSS_DISPLAY_INLINE_TABLE ||
                                d == CSS_DISPLAY_INLINE_FLEX ||
                                d == CSS_DISPLAY_INLINE_GRID) {
                            struct box *ib = pcore_construct_block(child, cs,
                                    0, ctx, stats);
                            if (ib != NULL) {
                                ib->type = BOX_INLINE_BLOCK;
                                pcore_box_add_child(inline_cont, ib);
                            }
                        } else {
                            struct box *start = pcore_box_new(BOX_INLINE, cs,
                                    ctx);
                            struct box *end;
                            if (start != NULL) {
                                start->node = child;
                                pcore_box_add_child(inline_cont, start);
                                pcore_construct_inline(child, cs, inline_cont,
                                        ctx, stats);
                                end = pcore_box_new(BOX_INLINE_END, cs, ctx);
                                if (end != NULL) {
                                    start->inline_end = end;
                                    end->inline_end = start;
                                    pcore_box_add_child(inline_cont, end);
                                }
                            }
                        }
                    } else {
                        /* block-level child: close any open inline run. A
                         * display:flex child becomes BOX_FLEX so layout.c
                         * routes it through the ported layout_flex (M7). */
                        struct box *cbox;
                        inline_cont = NULL;
                        if (css_computed_display(cs, false) ==
                                CSS_DISPLAY_FLEX) {
                            cbox = pcore_construct_flex(child, cs, 0, ctx,
                                    stats);
                        } else if (css_computed_display(cs, false) ==
                                CSS_DISPLAY_TABLE) {
                            cbox = pcore_construct_table(child, cs, ctx,
                                    stats);
                        } else {
                            cbox = pcore_construct_block(child, cs, 0, ctx,
                                    stats);
                        }
                        if (cbox != NULL) {
                            pcore_box_add_child(box, cbox);
                        }
                    }
                }
            }
        }

        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            break;
        }
        dom_node_unref(child);
        child = next;
    }

    return box;
}

/* Build a BOX_FLEX (or BOX_INLINE_FLEX) flex container. Each element child is a
 * flex item: a nested flex stays flex, everything else is blockified to a
 * BOX_BLOCK (per CSS flex-item rules), and items are added directly - no
 * anonymous inline container, unlike block layout. layout.c routes BOX_FLEX
 * boxes through the ported layout_flex (M7). Bare text between items is dropped
 * (flex items are elements in the pages we target). */
static struct box *pcore_construct_flex(dom_node *node,
        css_computed_style *style, int is_inline, void *ctx,
        PCoreBoxStats *stats)
{
    struct box *box;
    dom_node *child;

    box = pcore_box_new(is_inline ? BOX_INLINE_FLEX : BOX_FLEX, style, ctx);
    if (box == NULL) {
        return NULL;
    }
    box->node = node;

    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return box;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                type == DOM_ELEMENT_NODE) {
            css_computed_style *cs = pcore_profile_style(child, stats);
            if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                uint8_t d = css_computed_display(cs, false);
                struct box *item;
                if (d == CSS_DISPLAY_FLEX || d == CSS_DISPLAY_INLINE_FLEX) {
                    item = pcore_construct_flex(child, cs, 0, ctx, stats);
                } else {
                    item = pcore_construct_block(child, cs, 0, ctx, stats);
                }
                if (item != NULL) {
                    pcore_box_add_child(box, item);
                }
            }
        }

        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            break;
        }
        dom_node_unref(child);
        child = next;
    }
    return box;
}

/* ------------------------------------------------------------------ */
/* table construction (M7-table)                                       */
/* ------------------------------------------------------------------ */

/* HTML limits colspan to 1000 and rowspan to 65534. Keeping those limits here
 * also prevents malformed pages from allocating unbounded WM working memory. */
#define PCORE_TABLE_MAX_COLSPAN 1000U
#define PCORE_TABLE_MAX_ROWSPAN 65534U
#define PCORE_TABLE_MAX_COLUMNS 4096U

/* Read an HTML span attribute, preserving zero for NetSurf's normalisation
 * rules (colspan=0 becomes 1; rowspan=0 reaches the row-group end). */
static unsigned int pcore_span_attr(dom_node *node, const char *attr,
        unsigned int fallback, unsigned int maximum)
{
    dom_string *nm = NULL;
    dom_string *vl = NULL;
    unsigned int n = fallback;

    if (dom_string_create((const uint8_t *) attr, strlen(attr), &nm) !=
            DOM_NO_ERR) {
        return fallback;
    }
    if (dom_element_get_attribute(node, nm, &vl) == DOM_NO_ERR && vl != NULL) {
        const char *s = dom_string_data(vl);
        size_t len = dom_string_byte_length(vl);
        unsigned int v = 0;
        size_t i;

        for (i = 0; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
            unsigned int digit = (unsigned int) (s[i] - '0');
            if (v > (maximum - digit) / 10U) {
                v = maximum;
                while (i + 1 < len && s[i + 1] >= '0' &&
                        s[i + 1] <= '9') {
                    i++;
                }
                break;
            }
            v = v * 10U + digit;
        }
        if (i > 0) {
            if (v > maximum) {
                v = maximum;
            }
            n = v;
        }
        dom_string_unref(vl);
    }
    dom_string_unref(nm);
    return n;
}

static int pcore_disp_table_cell(css_computed_style *s)
{
    return css_computed_display(s, false) == CSS_DISPLAY_TABLE_CELL;
}

static int pcore_disp_table_row(css_computed_style *s)
{
    return css_computed_display(s, false) == CSS_DISPLAY_TABLE_ROW;
}

static int pcore_disp_table_rowgroup(css_computed_style *s)
{
    uint8_t d = css_computed_display(s, false);
    return (d == CSS_DISPLAY_TABLE_ROW_GROUP ||
            d == CSS_DISPLAY_TABLE_HEADER_GROUP ||
            d == CSS_DISPLAY_TABLE_FOOTER_GROUP) ? 1 : 0;
}

/* A table cell holds ordinary block flow, so build it as a block then retype to
 * BOX_TABLE_CELL and record its colspan / rowspan. */
static struct box *pcore_construct_cell(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    struct box *cell = pcore_construct_block(node, style, 0, ctx, stats);
    if (cell != NULL) {
        cell->type = BOX_TABLE_CELL;
        cell->columns = pcore_span_attr(node, "colspan", 1,
                PCORE_TABLE_MAX_COLSPAN);
        cell->rows = pcore_span_attr(node, "rowspan", 1,
                PCORE_TABLE_MAX_ROWSPAN);
    }
    return cell;
}

static struct box *pcore_construct_table_content(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    uint8_t display = css_computed_display(style, false);

    if (display == CSS_DISPLAY_TABLE) {
        return pcore_construct_table(node, style, ctx, stats);
    }
    if (pcore_disp_table_rowgroup(style)) {
        return pcore_construct_rowgroup(node, style, ctx, stats);
    }
    if (pcore_disp_table_row(style)) {
        return pcore_construct_row(node, style, ctx, stats);
    }
    if (pcore_disp_table_cell(style)) {
        return pcore_construct_cell(node, style, ctx, stats);
    }
    if (display == CSS_DISPLAY_FLEX || display == CSS_DISPLAY_INLINE_FLEX) {
        return pcore_construct_flex(node, style, 0, ctx, stats);
    }
    return pcore_construct_block(node, style, 0, ctx, stats);
}

/* Append one DOM child to a table row. Consecutive non-cell children share an
 * implied cell, matching box_normalise_table_row() in NetSurf 3.11. */
static int pcore_row_append_element(struct box *row, dom_node *node,
        css_computed_style *style, dom_node *style_source, void *ctx,
        struct box **implied_cell, PCoreBoxStats *stats)
{
    struct box *child_box;

    if (pcore_disp_table_cell(style)) {
        child_box = pcore_construct_cell(node, style, ctx, stats);
        *implied_cell = NULL;
        if (child_box == NULL) {
            return 0;
        }
        pcore_box_add_child(row, child_box);
        return 1;
    }

    if (*implied_cell == NULL) {
        *implied_cell = pcore_make_anonymous_box(BOX_TABLE_CELL, row->style,
                style_source, ctx, stats);
        if (*implied_cell == NULL) {
            return 0;
        }
        pcore_box_add_child(row, *implied_cell);
    }
    child_box = pcore_construct_table_content(node, style, ctx, stats);
    if (child_box == NULL) {
        return 0;
    }
    pcore_box_add_child(*implied_cell, child_box);
    return 1;
}

static int pcore_text_node_has_content(dom_node *node)
{
    dom_string *text = NULL;
    int has_content = 0;

    if (dom_node_get_text_content(node, &text) == DOM_NO_ERR && text != NULL) {
        const char *data = dom_string_data(text);
        size_t len = dom_string_byte_length(text);
        has_content = data != NULL && len > 0 &&
                !pcore_text_all_ws(data, len);
        dom_string_unref(text);
    }
    return has_content;
}

static int pcore_row_append_text(struct box *row, dom_node *node,
        dom_node *style_source, void *ctx, struct box **implied_cell,
        PCoreBoxStats *stats)
{
    struct box *container;
    struct box *text;

    if (!pcore_text_node_has_content(node)) {
        return 1;
    }
    if (*implied_cell == NULL) {
        *implied_cell = pcore_make_anonymous_box(BOX_TABLE_CELL, row->style,
                style_source, ctx, stats);
        if (*implied_cell == NULL) {
            return 0;
        }
        pcore_box_add_child(row, *implied_cell);
    }
    container = (*implied_cell)->last;
    if (container == NULL || container->type != BOX_INLINE_CONTAINER) {
        container = pcore_box_new(BOX_INLINE_CONTAINER, NULL, ctx);
        if (container == NULL) {
            return 0;
        }
        pcore_box_add_child(*implied_cell, container);
    }
    text = pcore_make_text_box(node, (*implied_cell)->style, ctx, stats);
    if (text == NULL) {
        return 0;
    }
    pcore_box_add_text(container, text);
    return 1;
}

/* BOX_TABLE_ROW from a <tr>, including implied cells for malformed content. */
static struct box *pcore_construct_row(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    struct box *row = pcore_box_new(BOX_TABLE_ROW, style, ctx);
    struct box *implied_cell = NULL;
    dom_node *child;

    if (row == NULL) {
        return NULL;
    }
    row->node = node;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return row;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR) {
            if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_profile_style(child, stats);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    pcore_row_append_element(row, child, cs, node, ctx,
                            &implied_cell, stats);
                }
            } else if (type == DOM_TEXT_NODE) {
                pcore_row_append_text(row, child, node, ctx, &implied_cell,
                        stats);
            }
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            break;
        }
        dom_node_unref(child);
        child = next;
    }
    return row;
}

/* BOX_TABLE_ROW_GROUP, including implied rows for non-row children. */
static struct box *pcore_construct_rowgroup(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    struct box *rg = pcore_box_new(BOX_TABLE_ROW_GROUP, style, ctx);
    struct box *implied_row = NULL;
    struct box *implied_cell = NULL;
    dom_node *child;

    if (rg == NULL) {
        return NULL;
    }
    rg->node = node;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return rg;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR) {
            if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_profile_style(child, stats);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    if (pcore_disp_table_row(cs)) {
                        struct box *row = pcore_construct_row(child, cs, ctx,
                                stats);
                        if (row != NULL) {
                            pcore_box_add_child(rg, row);
                        }
                        implied_row = NULL;
                        implied_cell = NULL;
                    } else {
                        if (implied_row == NULL) {
                            implied_row = pcore_make_anonymous_box(
                                    BOX_TABLE_ROW, rg->style, node, ctx,
                                    stats);
                            if (implied_row != NULL) {
                                pcore_box_add_child(rg, implied_row);
                            }
                        }
                        if (implied_row != NULL) {
                            pcore_row_append_element(implied_row, child, cs,
                                    node, ctx, &implied_cell, stats);
                        }
                    }
                }
            } else if (type == DOM_TEXT_NODE &&
                    pcore_text_node_has_content(child)) {
                if (implied_row == NULL) {
                    implied_row = pcore_make_anonymous_box(BOX_TABLE_ROW,
                            rg->style, node, ctx, stats);
                    if (implied_row != NULL) {
                        pcore_box_add_child(rg, implied_row);
                    }
                }
                if (implied_row != NULL) {
                    pcore_row_append_text(implied_row, child, node, ctx,
                            &implied_cell, stats);
                }
            }
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            break;
        }
        dom_node_unref(child);
        child = next;
    }
    if (rg->children == NULL) {
        implied_row = pcore_make_anonymous_box(BOX_TABLE_ROW, rg->style,
                node, ctx, stats);
        if (implied_row != NULL) {
            pcore_box_add_child(rg, implied_row);
        }
    }
    return rg;
}

/* Adapted from NetSurf box_normalise.c's calculate_table_row(). The temporary
 * records are construction-only; the final start_column/columns/rows fields
 * are consumed unchanged by NetSurf layout.c and table.c. */
typedef struct pcore_table_span {
    unsigned int row_span;
    struct box *row_group;
    int auto_row;
} pcore_table_span;

typedef struct pcore_table_columns {
    unsigned int current_column;
    unsigned int num_columns;
    unsigned int capacity;
    unsigned int num_rows;
    pcore_table_span *spans;
} pcore_table_columns;

static int pcore_place_table_cell(pcore_table_columns *info,
        struct box *cell)
{
    unsigned int start;
    unsigned int end;
    unsigned int i;
    unsigned int old_capacity;
    unsigned int new_capacity;
    pcore_table_span *spans;
    struct box *row_group;

    start = info->current_column;
    row_group = cell->parent->parent;
    while (start < info->capacity && info->spans[start].row_span != 0 &&
            info->spans[start].row_group == row_group) {
        start++;
    }
    if (cell->columns == 0) {
        cell->columns = 1;
    }
    if (start >= PCORE_TABLE_MAX_COLUMNS ||
            cell->columns > PCORE_TABLE_MAX_COLUMNS - start) {
        return 0;
    }
    end = start + cell->columns;
    if (end >= info->capacity) {
        old_capacity = info->capacity;
        new_capacity = end + 1;
        spans = (pcore_table_span *) realloc(info->spans,
                sizeof(*spans) * new_capacity);
        if (spans == NULL) {
            return 0;
        }
        info->spans = spans;
        info->capacity = new_capacity;
        memset(info->spans + old_capacity, 0,
                sizeof(*spans) * (new_capacity - old_capacity));
    }
    if (info->num_columns < end) {
        info->num_columns = end;
    }
    for (i = start; i < end; i++) {
        info->spans[i].row_span = cell->rows == 0 ? 1 : cell->rows;
        info->spans[i].auto_row = cell->rows == 0;
        info->spans[i].row_group = row_group;
    }
    info->current_column = end;
    cell->start_column = start;
    return 1;
}

static void pcore_finish_table_row(pcore_table_columns *info)
{
    unsigned int i;

    for (i = 0; i < info->num_columns; i++) {
        if (info->spans[i].row_span != 0 &&
                !info->spans[i].auto_row) {
            info->spans[i].row_span--;
        }
    }
    info->current_column = 0;
    info->num_rows++;
}

static void pcore_finish_table_spans(struct box *table)
{
    struct box *rg;
    struct box *row;
    struct box *cell;

    for (rg = table->children; rg != NULL; rg = rg->next) {
        unsigned int rows_left = rg->rows;
        for (row = rg->children; row != NULL; row = row->next) {
            for (cell = row->children; cell != NULL; cell = cell->next) {
                if (cell->columns == 0) {
                    cell->columns = 1;
                }
                if (cell->rows == 0 || cell->rows > rows_left) {
                    cell->rows = rows_left;
                }
            }
            if (rows_left > 0) {
                rows_left--;
            }
        }
    }
}

static void pcore_insert_table_cell(struct box *row, struct box *cell)
{
    struct box *before = row->children;
    struct box *previous = NULL;

    while (before != NULL && before->start_column < cell->start_column) {
        previous = before;
        before = before->next;
    }
    cell->parent = row;
    cell->prev = previous;
    cell->next = before;
    if (previous != NULL) {
        previous->next = cell;
    } else {
        row->children = cell;
    }
    if (before != NULL) {
        before->prev = cell;
    } else {
        row->last = cell;
    }
}

/* Complete gaps left by short rows and rowspans. Adapted from NetSurf 3.11's
 * box_normalise_table_spans(), with span state reset at row-group boundaries. */
static int pcore_fill_empty_table_cells(struct box *table,
        dom_node *style_source, void *ctx, PCoreBoxStats *stats)
{
    unsigned int *spans;
    struct box *rg;
    struct box *row;
    struct box *cell;

    spans = (unsigned int *) calloc(table->columns, sizeof(*spans));
    if (spans == NULL) {
        return 0;
    }
    for (rg = table->children; rg != NULL; rg = rg->next) {
        memset(spans, 0, table->columns * sizeof(*spans));
        for (row = rg->children; row != NULL; row = row->next) {
            unsigned int column;

            for (cell = row->children; cell != NULL; cell = cell->next) {
                unsigned int end = cell->start_column + cell->columns;
                unsigned int i;
                if (end > table->columns) {
                    end = table->columns;
                }
                for (i = cell->start_column; i < end; i++) {
                    spans[i] = cell->rows;
                }
            }
            column = 0;
            while (column < table->columns) {
                if (spans[column] == 0) {
                    unsigned int start = column;
                    struct box *empty;
                    while (column < table->columns && spans[column] == 0) {
                        column++;
                    }
                    empty = pcore_make_anonymous_box(BOX_TABLE_CELL,
                            row->style, style_source, ctx, stats);
                    if (empty == NULL) {
                        free(spans);
                        return 0;
                    }
                    empty->rows = 1;
                    empty->columns = column - start;
                    empty->start_column = start;
                    pcore_insert_table_cell(row, empty);
                } else {
                    spans[column]--;
                    column++;
                }
            }
        }
    }
    free(spans);
    return 1;
}

static void pcore_finish_table_profile(PCoreBoxStats *stats, DWORD started)
{
    if (stats != NULL) {
        stats->table_normalise_ms += GetTickCount() - started;
        stats->table_calls++;
    }
}

/* Build a BOX_TABLE tree (row-group > row > cell) from a display:table element.
 * Unexpected children receive NetSurf-style anonymous wrappers. Cell placement
 * uses NetSurf's span occupancy rules before table->columns / rows / col[] are
 * filled. Falls back to BOX_BLOCK if temporary normalisation state fails. */
static struct box *pcore_construct_table(dom_node *node,
        css_computed_style *style, void *ctx, PCoreBoxStats *stats)
{
    struct box *table = pcore_box_new(BOX_TABLE, style, ctx);
    struct box *anon_rg = NULL;
    struct box *anon_row = NULL;
    struct box *anon_cell = NULL;
    struct box *rg;
    struct box *row;
    struct box *cell;
    dom_node *child;
    pcore_table_columns info;
    DWORD normalise_started;

    if (table == NULL) {
        return NULL;
    }
    table->node = node;

    if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
        while (child != NULL) {
            dom_node_type type;
            dom_node *next;

            if (dom_node_get_node_type(child, &type) == DOM_NO_ERR) {
                if (type == DOM_ELEMENT_NODE) {
                    css_computed_style *cs = pcore_profile_style(child, stats);
                    if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                        if (pcore_disp_table_rowgroup(cs)) {
                            struct box *g = pcore_construct_rowgroup(child,
                                    cs, ctx, stats);
                            if (g != NULL) {
                                pcore_box_add_child(table, g);
                            }
                            anon_rg = NULL;
                            anon_row = NULL;
                            anon_cell = NULL;
                        } else {
                            if (anon_rg == NULL) {
                                anon_rg = pcore_make_anonymous_box(
                                        BOX_TABLE_ROW_GROUP, table->style,
                                        node, ctx, stats);
                                if (anon_rg != NULL) {
                                    pcore_box_add_child(table, anon_rg);
                                }
                            }
                            if (pcore_disp_table_row(cs)) {
                                struct box *r = pcore_construct_row(child,
                                        cs, ctx, stats);
                                if (r != NULL && anon_rg != NULL) {
                                    pcore_box_add_child(anon_rg, r);
                                }
                                anon_row = NULL;
                                anon_cell = NULL;
                            } else if (anon_rg != NULL) {
                                if (anon_row == NULL) {
                                    anon_row = pcore_make_anonymous_box(
                                            BOX_TABLE_ROW, anon_rg->style,
                                            node, ctx, stats);
                                    if (anon_row != NULL) {
                                        pcore_box_add_child(anon_rg, anon_row);
                                    }
                                }
                                if (anon_row != NULL) {
                                    pcore_row_append_element(anon_row, child,
                                            cs, node, ctx, &anon_cell, stats);
                                }
                            }
                        }
                    }
                } else if (type == DOM_TEXT_NODE &&
                        pcore_text_node_has_content(child)) {
                    if (anon_rg == NULL) {
                        anon_rg = pcore_make_anonymous_box(
                                BOX_TABLE_ROW_GROUP, table->style, node, ctx,
                                stats);
                        if (anon_rg != NULL) {
                            pcore_box_add_child(table, anon_rg);
                        }
                    }
                    if (anon_row == NULL && anon_rg != NULL) {
                        anon_row = pcore_make_anonymous_box(BOX_TABLE_ROW,
                                anon_rg->style, node, ctx, stats);
                        if (anon_row != NULL) {
                            pcore_box_add_child(anon_rg, anon_row);
                        }
                    }
                    if (anon_row != NULL) {
                        pcore_row_append_text(anon_row, child, node, ctx,
                                &anon_cell, stats);
                    }
                }
            }
            if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
                dom_node_unref(child);
                break;
            }
            dom_node_unref(child);
            child = next;
        }
    }

    if (table->children == NULL) {
        anon_rg = pcore_make_anonymous_box(BOX_TABLE_ROW_GROUP, table->style,
                node, ctx, stats);
        if (anon_rg != NULL) {
            pcore_box_add_child(table, anon_rg);
            anon_row = pcore_make_anonymous_box(BOX_TABLE_ROW,
                    anon_rg->style, node, ctx, stats);
            if (anon_row != NULL) {
                pcore_box_add_child(anon_rg, anon_row);
            }
        }
    }

    normalise_started = (stats != NULL) ? GetTickCount() : 0;
    /* layout_table asserts table->children && children->children && columns. */
    if (table->children == NULL || table->children->children == NULL) {
        table->type = BOX_BLOCK;
        pcore_finish_table_profile(stats, normalise_started);
        return table;
    }

    memset(&info, 0, sizeof(info));
    info.capacity = 2;
    info.num_columns = 1;
    info.spans = (pcore_table_span *) calloc(info.capacity,
            sizeof(*info.spans));
    if (info.spans == NULL) {
        table->type = BOX_BLOCK;
        pcore_finish_table_profile(stats, normalise_started);
        return table;
    }

    for (rg = table->children; rg != NULL; rg = rg->next) {
        unsigned int group_rows = 0;
        for (row = rg->children; row != NULL; row = row->next) {
            group_rows++;
            for (cell = row->children; cell != NULL; cell = cell->next) {
                if (!pcore_place_table_cell(&info, cell)) {
                    free(info.spans);
                    table->type = BOX_BLOCK;
                    pcore_finish_table_profile(stats, normalise_started);
                    return table;
                }
            }
            pcore_finish_table_row(&info);
        }
        rg->rows = group_rows;
    }

    if (info.num_columns == 0) {
        free(info.spans);
        table->type = BOX_BLOCK;
        pcore_finish_table_profile(stats, normalise_started);
        return table;
    }

    table->columns = info.num_columns;
    table->rows = info.num_rows;
    pcore_finish_table_spans(table);
    free(info.spans);
    /* Gap-cell anonymous style work belongs to this normalisation timer. */
    if (!pcore_fill_empty_table_cells(table, node, ctx, NULL)) {
        table->type = BOX_BLOCK;
        pcore_finish_table_profile(stats, normalise_started);
        return table;
    }
    table->col = talloc_zero_array(ctx, struct column, table->columns);
    if (table->col == NULL) {
        table->type = BOX_BLOCK;
        pcore_finish_table_profile(stats, normalise_started);
        return table;
    }
    /* col[].type stays COLUMN_WIDTH_UNKNOWN(0); table_calculate_column_types
     * recomputes the types from cell styles during layout_table. */

    pcore_finish_table_profile(stats, normalise_started);
    return table;
}

struct box *pcore_box_construct_profile(struct dom_node *root, void *ctx,
        PCoreBoxStats *stats)
{
    css_computed_style *style;
    struct box *tree;
    DWORD started;

    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    if (root == NULL) {
        return NULL;
    }
    started = (stats != NULL) ? GetTickCount() : 0;
    style = pcore_profile_style(root, stats);
    if (style == NULL) {
        if (stats != NULL) {
            stats->tree_ms = GetTickCount() - started;
        }
        return NULL;
    }
    tree = pcore_construct_block(root, style, 1, ctx, stats);
    if (stats != NULL) {
        stats->tree_ms = GetTickCount() - started;
    }
    started = (stats != NULL) ? GetTickCount() : 0;
    pcore_attach_cached_backgrounds(tree, ctx);
    if (stats != NULL) {
        stats->backgrounds_ms = GetTickCount() - started;
    }
    return tree;
}

/* Internal (pcore_internal.h). */
struct box *pcore_box_construct(struct dom_node *root, void *ctx)
{
    return pcore_box_construct_profile(root, ctx, NULL);
}

/* ------------------------------------------------------------------ */
/* M3 self-test                                                        */
/* ------------------------------------------------------------------ */

static void pcore_box_count(struct box *b, int *blk, int *icont, int *inl,
        int *txt, int *other)
{
    struct box *c;

    if (b == NULL) {
        return;
    }
    switch (b->type) {
    case BOX_BLOCK:            (*blk)++;   break;
    case BOX_INLINE_CONTAINER: (*icont)++; break;
    case BOX_INLINE:
    case BOX_INLINE_END:
    case BOX_INLINE_BLOCK:     (*inl)++;   break;
    case BOX_TEXT:             (*txt)++;   break;
    default:                   (*other)++; break;
    }
    for (c = b->children; c != NULL; c = c->next) {
        pcore_box_count(c, blk, icont, inl, txt, other);
    }
}

static int pcore_box_text_has_control_space(struct box *b)
{
    struct box *c;
    size_t i;

    if (b == NULL) {
        return 0;
    }
    if (b->type == BOX_TEXT && b->style != NULL &&
            (css_computed_white_space(b->style) == CSS_WHITE_SPACE_NORMAL ||
             css_computed_white_space(b->style) == CSS_WHITE_SPACE_NOWRAP)) {
        for (i = 0; i < b->length; i++) {
            if (b->text[i] == '\t' || b->text[i] == '\r' ||
                    b->text[i] == '\n' || b->text[i] == '\f') {
                return 1;
            }
        }
    }
    for (c = b->children; c != NULL; c = c->next) {
        if (pcore_box_text_has_control_space(c)) {
            return 1;
        }
    }
    return 0;
}

static int pcore_box_subtree_has_lf(struct box *b)
{
    struct box *c;
    size_t i;

    if (b == NULL) {
        return 0;
    }
    if (b->type == BOX_TEXT) {
        for (i = 0; i < b->length; i++) {
            if (b->text[i] == '\n') {
                return 1;
            }
        }
    }
    for (c = b->children; c != NULL; c = c->next) {
        if (pcore_box_subtree_has_lf(c)) {
            return 1;
        }
    }
    return 0;
}

static int pcore_pre_kept_lf(struct box *b)
{
    struct box *c;

    if (b == NULL) {
        return 0;
    }
    if (b->node != NULL && pcore_node_name_is(b->node, "pre")) {
        return pcore_box_subtree_has_lf(b);
    }
    for (c = b->children; c != NULL; c = c->next) {
        if (pcore_pre_kept_lf(c)) {
            return 1;
        }
    }
    return 0;
}

PCORE_API void PCore_BoxTreeTest(char *out, int cap)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title>"
        "<style>i{font-style:italic}</style></head>"
        "<body><h1>Title</h1>"
        "<p>Some <b>bold</b> and <i>italic</i> text in a paragraph.</p>"
        "<p>Whitespace a\nnumber stays readable.</p><pre>A\nB</pre>"
        "<p>Image fallback: <img alt=\"Logo\" src=\"logo.png\"></p>"
        "<div><p>Nested paragraph one.</p><p>Nested two.</p></div>"
        "</body></html>";
    HANDLE hDoc;
    dom_document *doc;
    dom_node *root = NULL;
    void *ctx;
    struct box *tree;
    int blk = 0, icont = 0, inl = 0, txt = 0, other = 0;
    int normal_ws_ok = 0;
    int pre_lf_ok = 0;
    WCHAR w[256];

    if (out == NULL || cap <= 0) {
        return;
    }
    out[0] = '\0';

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        return;
    }
    if (PCore_StyleDocument(hDoc, NULL) != 0) {
        PCore_FreeDocument(hDoc);
        return;
    }

    doc = (dom_document *) hDoc;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        PCore_FreeDocument(hDoc);
        return;
    }

    ctx = talloc_named_const(NULL, 0, "pcore_boxtree_test");
    tree = pcore_box_construct(root, ctx);
    if (tree != NULL) {
        pcore_box_count(tree, &blk, &icont, &inl, &txt, &other);
        normal_ws_ok = !pcore_box_text_has_control_space(tree);
        pre_lf_ok = pcore_pre_kept_lf(tree);
    }

    wsprintfW(w, L"box tree: blocks=%d inline_containers=%d\r\n"
                 L"inline(+end/iblock)=%d text=%d other=%d\r\n"
                 L"total=%d normal_ws=%s pre_lf=%s",
              blk, icont, inl, txt, other,
              blk + icont + inl + txt + other,
              normal_ws_ok ? L"ok" : L"FAIL",
              pre_lf_ok ? L"kept" : L"FAIL");
    WideCharToMultiByte(CP_ACP, 0, w, -1, out, cap, NULL, NULL);
    out[cap - 1] = '\0';

    if (ctx != NULL) {
        talloc_free(ctx);   /* frees the whole box tree */
    }
    dom_node_unref(root);
    PCore_FreeDocument(hDoc);
}

/* ------------------------------------------------------------------ */
/* M4 self-test: run NetSurf's real layout_document on the box tree    */
/* ------------------------------------------------------------------ */

static struct box *pcore_first_text(struct box *b)
{
    struct box *c;

    if (b == NULL) {
        return NULL;
    }
    if (b->type == BOX_TEXT) {
        return b;
    }
    for (c = b->children; c != NULL; c = c->next) {
        struct box *r = pcore_first_text(c);
        if (r != NULL) {
            return r;
        }
    }
    return NULL;
}

PCORE_API void PCore_LayoutBoxTest(char *out, int cap)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title>"
        "<style>body{width:200px}h1{font-size:24px}"
        "div{background-color:#cce6ff;padding:6px}</style></head>"
        "<body><h1>Title</h1>"
        "<div><p>Some text in a paragraph that should wrap onto several "
        "lines within the box.</p></div></body></html>";
    HANDLE hDoc;
    dom_document *doc;
    dom_node *root = NULL;
    void *ctx;
    struct box *tree;
    struct box *t;
    html_content c;
    WCHAR w[256];
    struct box *body;
    struct box *dv;
    css_color   dbg = 0;
    int ax = 0;
    int ay = 0;
    int vw = 240;
    int vh = 320;

    if (out == NULL || cap <= 0) {
        return;
    }
    out[0] = '\0';

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        return;
    }
    if (PCore_StyleDocument(hDoc, NULL) != 0) {
        PCore_FreeDocument(hDoc);
        return;
    }
    doc = (dom_document *) hDoc;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        PCore_FreeDocument(hDoc);
        return;
    }

    pcore_nsshim_init();   /* intern corestrings layout references */
    ctx = talloc_named_const(NULL, 0, "pcore_layout_test");
    tree = pcore_box_construct(root, ctx);
    if (tree != NULL) {
        memset(&c, 0, sizeof(c));
        c.layout = tree;
        c.bctx = (int *) ctx;
        c.font_func = &pcore_gdi_layout;
        memcpy(&c.unit_len_ctx, pcore_get_unit_ctx(),
                sizeof(c.unit_len_ctx));   /* struct has const-qualified member */
        c.background_colour = 0x00ffffff;

        layout_document(&c, vw, vh);

        body = tree->children;   /* <head> is display:none, so child 0 = body */
        dv = (body != NULL && body->children != NULL) ?
                body->children->next : NULL;   /* h1 then div */
        if (dv != NULL && dv->style != NULL) {
            css_computed_background_color(dv->style, &dbg);
        }
        t = pcore_first_text(tree);
        if (t != NULL) {
            box_coords(t, &ax, &ay);   /* absolute coords up the parent chain */
        }
        wsprintfW(w,
                L"root w=%d h=%d  body w=%d h=%d\r\n"
                L"div x=%d y=%d w=%d h=%d bg=%08lx\r\n"
                L"first text abs x=%d y=%d w=%d",
                tree->width, tree->height,
                (body != NULL) ? body->width : -1,
                (body != NULL) ? body->height : -1,
                (dv != NULL) ? dv->x : -1,
                (dv != NULL) ? dv->y : -1,
                (dv != NULL) ? dv->width : -1,
                (dv != NULL) ? dv->height : -1,
                (unsigned long) dbg,
                ax, ay,
                (t != NULL) ? t->width : -1);
        WideCharToMultiByte(CP_ACP, 0, w, -1, out, cap, NULL, NULL);
        out[cap - 1] = '\0';
    }

    if (ctx != NULL) {
        talloc_free(ctx);
    }
    dom_node_unref(root);
    PCore_FreeDocument(hDoc);
}

/* M5f: render the built-in test page with NetSurf's real layout + redraw,
 * painting through the GDI plotter (M1) + GDI font table (M2). Builds the box
 * tree, runs layout_document, then drives html_redraw into `hdc` over a cw x ch
 * client area. Rebuilt on each call (the app paints this from WM_PAINT). This
 * is the first page rendered end-to-end by the ported NetSurf engine. */
PCORE_API void PCore_NsRenderTest(HDC hdc, int cw, int ch)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><style>"
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h1{color:#800000;font-size:24px;border-bottom:3px solid #800000;}"
        "p{color:#103080;}"
        "div{background-color:#cce6ff;padding:6px;border:2px solid #4060a0;}"
        ".row{display:flex;background-color:#eeeeee;border:2px dashed #404040;}"
        ".c1{background-color:#ff8080;padding:4px;border:1px solid #a00000;}"
        ".c2{background-color:#80ff80;padding:4px;border:1px dotted #006000;}"
        ".c3{background-color:#8080ff;padding:4px;border:1px solid #0000a0;}"
        "table{border:2px solid #202020;}td{padding:4px;border:1px solid #202020;}"
        "</style></head><body>"
        "<h1>Positron + NetSurf</h1>"
        "<div class=\"row\"><div class=\"c1\">One</div>"
        "<div class=\"c2\">Two</div><div class=\"c3\">Three</div></div>"
        "<table><tr><td class=\"c1\">A1</td><td class=\"c2\">B1</td></tr>"
        "<tr><td class=\"c3\">A2</td><td class=\"c1\">B2</td></tr></table>"
        "<p>Image fallback: <img alt=\"Logo\" src=\"logo.png\"></p>"
        "<div><p>This paragraph is laid out by NetSurf's real layout.c and "
        "painted by its real redraw.c through our GDI plotter. It should wrap "
        "across several lines inside the light-blue block.</p>"
        "<p>A second paragraph follows below it.</p></div>"
        "</body></html>";

    HANDLE        hDoc;
    dom_document *doc;
    dom_node     *root = NULL;
    void         *ctx;
    struct box   *tree;
    html_content  c;
    struct redraw_context     rc;
    struct content_redraw_data data;
    struct rect   clip;
    struct { HDC hdc; } pv;   /* layout matches pcore_plot_ctx (one HDC) */

    if (hdc == NULL || cw <= 0 || ch <= 0) {
        return;
    }
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        return;
    }
    if (PCore_StyleDocument(hDoc, NULL) != 0) {
        PCore_FreeDocument(hDoc);
        return;
    }
    doc = (dom_document *) hDoc;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        PCore_FreeDocument(hDoc);
        return;
    }

    pcore_nsshim_init();
    ctx = talloc_named_const(NULL, 0, "pcore_render_test");
    tree = pcore_box_construct(root, ctx);
    if (tree != NULL) {
        memset(&c, 0, sizeof(c));
        c.layout = tree;
        c.bctx = (int *) ctx;
        c.font_func = &pcore_gdi_layout;
        memcpy(&c.unit_len_ctx, pcore_get_unit_ctx(),
                sizeof(c.unit_len_ctx));
        c.background_colour = 0x00ffffff;

        PCore_SetViewport(cw, ch, 0);
        layout_document(&c, cw, ch);

        pv.hdc = hdc;
        memset(&rc, 0, sizeof(rc));
        /* Must be true or html_redraw_background() returns early and paints
         * NO background at all - not even background-color. This self-test
         * does not populate a document image cache, and CSS background-image
         * is still staged, so only background colours draw here. */
        rc.background_images = true;
        rc.plot = &pcore_gdi_plotters;
        rc.priv = &pv;

        memset(&data, 0, sizeof(data));
        data.width = cw;
        data.height = ch;
        data.background_colour = 0x00ffffff;
        data.scale = 1.0f;

        clip.x0 = 0;
        clip.y0 = 0;
        clip.x1 = cw;
        clip.y1 = ch;

        html_redraw((struct content *) &c, &data, &clip, &rc);
    }

    if (ctx != NULL) {
        talloc_free(ctx);
    }
    dom_node_unref(root);
    PCore_FreeDocument(hDoc);
}

/* ================================================================== */
/* M6: the official render path, now on the NetSurf engine             */
/*                                                                     */
/* PCore_LayoutDocument / PaintDocument / DocumentHeight / NodeBox /    */
/* LinkAt drive box_construct + NetSurf's layout_document + html_redraw */
/* in place of the retired hand-rolled block engine. Per-document       */
/* render state (the box tree, its talloc arena and the html_content    */
/* shim) hangs off the dom_document via libdom user-data and is freed   */
/* automatically when the document is destroyed (or re-laid-out).       */
/* ================================================================== */

/* Per-document render state. */
typedef struct pcore_render {
    void         *bctx;        /* talloc root; talloc_free frees the box tree */
    struct box   *root_box;    /* == content.layout                          */
    html_content  content;     /* operated on by layout_document/html_redraw */
    int           doc_height;  /* total laid-out height (CSS px)             */
    int           vw;          /* viewport used for layout (redraw extent)   */
    int           vh;
    struct scrollbar *active_scrollbar;
    int           active_scrollbar_x;
    int           active_scrollbar_y;
    int           overflow_dirty_valid;
    int           overflow_dirty_x;
    int           overflow_dirty_y;
    int           overflow_dirty_w;
    int           overflow_dirty_h;
    PCoreLayoutStats layout_stats;
    PCoreBoxStats box_stats;
} pcore_render;

/* libdom user-data key under which the render state hangs on the document. */
static dom_string *pcore_render_key = NULL;

static int pcore_ensure_render_key(void)
{
    if (pcore_render_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) "__pcore_render__", 16,
            &pcore_render_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

/* "a" / "href", interned once for link hit-testing. */
static dom_string *pcore_a_name = NULL;
static dom_string *pcore_href_name = NULL;

static int pcore_ensure_link_strings(void)
{
    if (pcore_a_name == NULL &&
            dom_string_create((const uint8_t *) "a", 1, &pcore_a_name)
                    != DOM_NO_ERR) {
        return 1;
    }
    if (pcore_href_name == NULL &&
            dom_string_create((const uint8_t *) "href", 4, &pcore_href_name)
                    != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_render_free(pcore_render *st)
{
    if (st == NULL) {
        return;
    }
    pcore_box_scrollbars_destroy(st->root_box);
    if (st->bctx != NULL) {
        talloc_free(st->bctx);   /* frees the whole box tree in one shot */
    }
    free(st);
}

/* Free the render state when libdom destroys the document. */
static void pcore_render_ud_handler(dom_node_operation op, dom_string *key,
        void *data, struct dom_node *src, struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_render_free((pcore_render *) data);
    }
}

/* Fetch the render state hung off `doc`, or NULL if not laid out. */
static pcore_render *pcore_get_render(dom_document *doc)
{
    void *d = NULL;

    if (doc == NULL || pcore_render_key == NULL) {
        return NULL;
    }
    if (dom_node_get_user_data((struct dom_node *) doc, pcore_render_key, &d)
            != DOM_NO_ERR) {
        return NULL;
    }
    return (pcore_render *) d;
}

/* NetSurf normally reflows a retained box tree, so auto-overflow blocks can
 * use descendant bounds calculated by the previous pass when reserving a
 * horizontal scrollbar. Positron rebuilds the tree for each public layout
 * call. Detect the first-pass case and request one targeted settling pass;
 * ordinary pages still perform exactly one layout. */
static int pcore_needs_auto_hscroll_reflow(struct box *box)
{
    struct box *child;
    enum css_height_e height_type;
    css_fixed height;
    css_unit unit;

    if (box == NULL) {
        return 0;
    }
    if (box->type == BOX_BLOCK && box->style != NULL &&
            css_computed_overflow_x(box->style) == CSS_OVERFLOW_AUTO &&
            box_hscrollbar_present(box)) {
        height = 0;
        unit = CSS_UNIT_PX;
        height_type = css_computed_height(box->style, &height, &unit);
        if (height_type == CSS_HEIGHT_AUTO) {
            return 1;
        }
    }
    for (child = box->children; child != NULL; child = child->next) {
        if (pcore_needs_auto_hscroll_reflow(child)) {
            return 1;
        }
    }
    return 0;
}

/* A settling pass is only for auto-height boxes. Fixed-height overflow boxes
 * already have an established WM baseline where the scrollbar occupies their
 * CSS height. Hide their first-pass extent while the shared tree is reflowed,
 * then layout_calculate_descendant_bboxes restores the real extent for redraw. */
static void pcore_mask_fixed_auto_hscroll_extent(struct box *box)
{
    struct box *child;
    enum css_height_e height_type;
    css_fixed height;
    css_unit unit;

    if (box == NULL) {
        return;
    }
    if (box->type == BOX_BLOCK && box->style != NULL &&
            css_computed_overflow_x(box->style) == CSS_OVERFLOW_AUTO &&
            box_hscrollbar_present(box)) {
        height = 0;
        unit = CSS_UNIT_PX;
        height_type = css_computed_height(box->style, &height, &unit);
        if (height_type != CSS_HEIGHT_AUTO) {
            box->descendant_x1 = box->padding[LEFT] + box->width +
                    box->padding[RIGHT] + box->border[RIGHT].width;
        }
    }
    for (child = box->children; child != NULL; child = child->next) {
        pcore_mask_fixed_auto_hscroll_extent(child);
    }
}

PCORE_API int PCore_LayoutDocument(HANDLE hDoc, int viewport_w, int viewport_h)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;
    void         *ctx;
    void         *old = NULL;
    struct box   *tree;
    pcore_render *st;
    PCoreLayoutStats stats;
    PCoreBoxStats stats_box;
    DWORD total_started;
    DWORD started;

    total_started = GetTickCount();
    memset(&stats, 0, sizeof(stats));
    if (doc == NULL || viewport_w <= 0 || viewport_h <= 0) {
        return 1;
    }
    if (pcore_ensure_render_key() != 0) {
        return 1;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        return 1;   /* PCore_StyleDocument must have run first (styles per node) */
    }

    pcore_nsshim_init();

    ctx = talloc_named_const(NULL, 0, "pcore_render");
    if (ctx == NULL) {
        dom_node_unref(root);
        return 1;
    }
    started = GetTickCount();
    tree = pcore_box_construct_profile(root, ctx, &stats_box);
    stats.box_construct_ms = GetTickCount() - started;
    dom_node_unref(root);
    if (tree == NULL) {
        talloc_free(ctx);
        return 1;
    }

    st = (pcore_render *) malloc(sizeof(*st));
    if (st == NULL) {
        talloc_free(ctx);
        return 1;
    }
    memset(st, 0, sizeof(*st));
    st->bctx = ctx;
    st->root_box = tree;
    st->vw = viewport_w;
    st->vh = viewport_h;

    /* Update the unit context (vw/vh CSS units) before copying it in. */
    PCore_SetViewport(viewport_w, viewport_h, 0);

    memset(&st->content, 0, sizeof(st->content));
    st->content.layout = tree;
    st->content.bctx = (int *) ctx;
    st->content.font_func = &pcore_gdi_layout;
    memcpy((void *) &st->content.unit_len_ctx, pcore_get_unit_ctx(),
            sizeof(st->content.unit_len_ctx));
    st->content.background_colour = 0x00ffffff;

    started = GetTickCount();
    layout_document(&st->content, viewport_w, viewport_h);
    stats.first_layout_ms = GetTickCount() - started;
    started = GetTickCount();
    if (pcore_needs_auto_hscroll_reflow(tree)) {
        stats.settling_pass = 1;
        pcore_mask_fixed_auto_hscroll_extent(tree);
        layout_document(&st->content, viewport_w, viewport_h);
    }
    stats.settling_ms = GetTickCount() - started;

    started = GetTickCount();
    st->doc_height = tree->height;
    if (tree->descendant_y1 > st->doc_height) {
        st->doc_height = tree->descendant_y1;   /* include overflowing content */
    }

    /* Replace any previous state (re-layout on resize). set_user_data does NOT
     * run the handler on replace, so free the old state by hand. */
    dom_node_set_user_data((struct dom_node *) doc, pcore_render_key, st,
            pcore_render_ud_handler, &old);
    if (old != NULL && old != st) {
        pcore_render_free((pcore_render *) old);
    }
    stats.finalize_ms = GetTickCount() - started;
    stats.total_ms = GetTickCount() - total_started;
    st->layout_stats = stats;
    st->box_stats = stats_box;
    return 0;
}

PCORE_API int PCore_GetLayoutStats(HANDLE hDoc,
        PCoreLayoutStats *out_stats)
{
    pcore_render *st;

    if (out_stats == NULL) {
        return 1;
    }
    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
        return 1;
    }
    *out_stats = st->layout_stats;
    return 0;
}

PCORE_API int PCore_GetBoxStats(HANDLE hDoc, PCoreBoxStats *out_stats)
{
    pcore_render *st;

    if (out_stats == NULL) {
        return 1;
    }
    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
        return 1;
    }
    *out_stats = st->box_stats;
    return 0;
}

PCORE_API int PCore_DocumentHeight(HANDLE hDoc)
{
    pcore_render *st = pcore_get_render((dom_document *) hDoc);
    return (st != NULL) ? st->doc_height : 0;
}

/* DFS for the first box whose DOM node is an element named `want` (caseless). */
static struct box *pcore_box_for_tag(struct box *b, dom_string *want)
{
    struct box *c;

    if (b == NULL) {
        return NULL;
    }
    if (b->node != NULL) {
        dom_string *nm = NULL;
        if (dom_node_get_node_name(b->node, &nm) == DOM_NO_ERR &&
                nm != NULL) {
            bool eq = dom_string_caseless_isequal(nm, want);
            dom_string_unref(nm);
            if (eq) {
                return b;
            }
        }
    }
    for (c = b->children; c != NULL; c = c->next) {
        struct box *r = pcore_box_for_tag(c, want);
        if (r != NULL) {
            return r;
        }
    }
    return NULL;
}

PCORE_API int PCore_NodeBox(HANDLE hDoc, const char *tag,
        int *x, int *y, int *w, int *h)
{
    dom_document *doc = (dom_document *) hDoc;
    pcore_render *st = pcore_get_render(doc);
    dom_string   *want = NULL;
    struct box   *box;
    int           ax = 0;
    int           ay = 0;

    if (st == NULL || tag == NULL) {
        return 1;
    }
    if (dom_string_create((const uint8_t *) tag, strlen(tag), &want)
            != DOM_NO_ERR) {
        return 1;
    }
    box = pcore_box_for_tag(st->root_box, want);
    dom_string_unref(want);
    if (box == NULL) {
        return 1;
    }
    box_coords(box, &ax, &ay);   /* absolute page coords up the parent chain */
    if (x != NULL) { *x = ax; }
    if (y != NULL) { *y = ay; }
    if (w != NULL) { *w = box->width; }
    if (h != NULL) { *h = box->height; }
    return 0;
}

PCORE_API int PCore_NodeFormControlState(HANDLE hDoc, const char *tag,
        int *kind, int *selected, int *disabled)
{
    dom_document *doc = (dom_document *) hDoc;
    pcore_render *st = pcore_get_render(doc);
    dom_string *want = NULL;
    struct box *box;
    int control_kind;

    if (st == NULL || tag == NULL) {
        return 1;
    }
    if (dom_string_create((const uint8_t *) tag, strlen(tag), &want)
            != DOM_NO_ERR) {
        return 1;
    }
    box = pcore_box_for_tag(st->root_box, want);
    dom_string_unref(want);
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    if (box->gadget->type == GADGET_CHECKBOX) {
        control_kind = 1;
    } else if (box->gadget->type == GADGET_RADIO) {
        control_kind = 2;
    } else {
        return 1;
    }
    if (kind != NULL) {
        *kind = control_kind;
    }
    if (selected != NULL) {
        *selected = box->gadget->selected ? 1 : 0;
    }
    if (disabled != NULL) {
        *disabled = box->gadget->disabled ? 1 : 0;
    }
    return 0;
}

static struct box *pcore_table_cell_at(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->type == BOX_TABLE_CELL) {
        if (*current == target) {
            return box;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_table_cell_at(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

PCORE_API int PCore_TableCellBorder(HANDLE hDoc, unsigned int cell_index,
        int side, int *style, unsigned long *argb, int *width)
{
    pcore_render *st;
    struct box *cell;
    unsigned int current;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL || side < TOP || side > LEFT) {
        return 1;
    }
    current = 0;
    cell = pcore_table_cell_at(st->root_box, cell_index, &current);
    if (cell == NULL) {
        return 1;
    }
    if (style != NULL) {
        *style = (int) cell->border[side].style;
    }
    if (argb != NULL) {
        *argb = (unsigned long) cell->border[side].c;
    }
    if (width != NULL) {
        *width = cell->border[side].width;
    }
    return 0;
}

static void pcore_table_first_text(struct box *box, struct box *cell,
        PCoreTableCellGeometry *geometry, int *found)
{
    struct box *child;
    int x;
    int y;

    if (box == NULL || geometry == NULL || found == NULL || *found) {
        return;
    }
    if (box != cell && box->type == BOX_TABLE_CELL) {
        return;
    }
    if (box->type == BOX_TEXT && box->text != NULL && box->length > 0 &&
            box->width > 0 && box->height > 0) {
        x = 0;
        y = 0;
        box_coords(box, &x, &y);
        geometry->first_text_x = x;
        geometry->first_text_y = y;
        geometry->first_text_width = box->width;
        geometry->first_text_height = box->height;
        *found = 1;
        return;
    }
    for (child = box->children; child != NULL; child = child->next) {
        pcore_table_first_text(child, cell, geometry, found);
        if (*found) {
            return;
        }
    }
}

PCORE_API int PCore_TableCellGeometry(HANDLE hDoc,
        unsigned int cell_index, PCoreTableCellGeometry *out_geometry)
{
    pcore_render *st;
    struct box *cell;
    unsigned int current;
    int found;

    if (out_geometry == NULL) {
        return 1;
    }
    memset(out_geometry, 0, sizeof(*out_geometry));
    out_geometry->first_text_x = -1;
    out_geometry->first_text_y = -1;
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    cell = (st != NULL) ?
            pcore_table_cell_at(st->root_box, cell_index, &current) : NULL;
    if (cell == NULL) {
        return 1;
    }
    box_coords(cell, &out_geometry->cell_x, &out_geometry->cell_y);
    out_geometry->cell_width = cell->width;
    out_geometry->cell_height = cell->height;
    found = 0;
    pcore_table_first_text(cell, cell, out_geometry, &found);
    return 0;
}

PCORE_API int PCore_TableCellVerticalAlign(HANDLE hDoc,
        unsigned int cell_index, int *kind)
{
    pcore_render *st;
    struct box *cell;
    unsigned int current;
    css_fixed value;
    css_unit unit;
    uint8_t align;

    if (kind == NULL) {
        return 1;
    }
    *kind = 0;
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    cell = (st != NULL) ?
            pcore_table_cell_at(st->root_box, cell_index, &current) : NULL;
    if (cell == NULL || cell->style == NULL) {
        return 1;
    }
    value = 0;
    unit = CSS_UNIT_PX;
    align = css_computed_vertical_align(cell->style, &value, &unit);
    if (align == CSS_VERTICAL_ALIGN_TOP) {
        *kind = 1;
    } else if (align == CSS_VERTICAL_ALIGN_MIDDLE) {
        *kind = 2;
    } else if (align == CSS_VERTICAL_ALIGN_BOTTOM) {
        *kind = 3;
    }
    return 0;
}

static struct box *pcore_table_row_at(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->type == BOX_TABLE_ROW) {
        if (*current == target) {
            return box;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_table_row_at(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

PCORE_API int PCore_TableRowGeometry(HANDLE hDoc,
        unsigned int row_index, PCoreTableRowGeometry *out_geometry)
{
    pcore_render *st;
    struct box *row;
    unsigned int current;

    if (out_geometry == NULL) {
        return 1;
    }
    memset(out_geometry, 0, sizeof(*out_geometry));
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    row = (st != NULL) ?
            pcore_table_row_at(st->root_box, row_index, &current) : NULL;
    if (row == NULL) {
        return 1;
    }
    box_coords(row, &out_geometry->row_x, &out_geometry->row_y);
    out_geometry->row_width = row->width;
    out_geometry->row_height = row->height;
    return 0;
}

PCORE_API int PCore_TableRowSpecifiedHeight(HANDLE hDoc,
        unsigned int row_index, int *kind, int *value)
{
    pcore_render *st;
    struct box *row;
    unsigned int current;
    css_fixed height_value;
    css_unit unit;
    enum css_height_e type;

    if (kind == NULL || value == NULL) {
        return 1;
    }
    *kind = 0;
    *value = 0;
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    row = (st != NULL) ?
            pcore_table_row_at(st->root_box, row_index, &current) : NULL;
    if (row == NULL || row->style == NULL) {
        return 1;
    }
    height_value = 0;
    unit = CSS_UNIT_PX;
    type = css_computed_height(row->style, &height_value, &unit);
    if (type == CSS_HEIGHT_AUTO) {
        return 0;
    }
    if (type == CSS_HEIGHT_SET && unit == CSS_UNIT_PCT) {
        *kind = 1;
        *value = FIXTOINT(height_value);
        return 0;
    }
    *kind = 2;
    *value = (type == CSS_HEIGHT_SET && unit == CSS_UNIT_PX) ?
            FIXTOINT(height_value) : 0;
    return 0;
}

static struct box *pcore_list_marker_at(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->list_marker != NULL) {
        if (*current == target) {
            return box->list_marker;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_list_marker_at(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

PCORE_API int PCore_ListMarker(HANDLE hDoc, unsigned int index,
        char *text, int cap, int *x, int *y, int *w, int *h)
{
    pcore_render *st;
    struct box *marker;
    unsigned int current;
    size_t copy;
    int ax;
    int ay;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    marker = (st != NULL) ?
            pcore_list_marker_at(st->root_box, index, &current) : NULL;
    if (marker == NULL) {
        if (text != NULL && cap > 0) {
            text[0] = '\0';
        }
        return 1;
    }

    if (text != NULL && cap > 0) {
        copy = marker->length;
        if (copy > (size_t) (cap - 1)) {
            copy = (size_t) (cap - 1);
        }
        if (copy > 0 && marker->text != NULL) {
            memcpy(text, marker->text, copy);
        }
        text[copy] = '\0';
    }
    ax = 0;
    ay = 0;
    box_coords(marker, &ax, &ay);
    if (x != NULL) { *x = ax; }
    if (y != NULL) { *y = ay; }
    if (w != NULL) { *w = marker->width; }
    if (h != NULL) { *h = marker->height; }
    return 0;
}

static void pcore_list_text_geometry(struct box *box, struct box *item,
        PCoreListItemGeometry *geometry, int *have_first)
{
    struct box *child;
    int x;
    int y;

    if (box == NULL || geometry == NULL || have_first == NULL) {
        return;
    }
    if (box != item && box->list_marker != NULL) {
        return;
    }
    if (box->type == BOX_TEXT && box->text != NULL && box->length > 0) {
        x = 0;
        y = 0;
        box_coords(box, &x, &y);
        if (!*have_first) {
            geometry->first_text_x = x;
            geometry->first_text_y = y;
            *have_first = 1;
        } else if (geometry->wrapped_text_y < 0 &&
                y > geometry->first_text_y) {
            geometry->wrapped_text_x = x;
            geometry->wrapped_text_y = y;
        }
    }
    for (child = box->children; child != NULL; child = child->next) {
        pcore_list_text_geometry(child, item, geometry, have_first);
    }
}

PCORE_API int PCore_ListItemGeometry(HANDLE hDoc, unsigned int index,
        PCoreListItemGeometry *out_geometry)
{
    pcore_render *st;
    struct box *marker;
    struct box *item;
    unsigned int current;
    int have_first;

    if (out_geometry == NULL) {
        return 1;
    }
    memset(out_geometry, 0, sizeof(*out_geometry));
    out_geometry->first_text_x = -1;
    out_geometry->first_text_y = -1;
    out_geometry->wrapped_text_x = -1;
    out_geometry->wrapped_text_y = -1;
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    marker = (st != NULL) ?
            pcore_list_marker_at(st->root_box, index, &current) : NULL;
    if (marker == NULL || marker->parent == NULL) {
        return 1;
    }
    item = marker->parent;
    box_coords(item, &out_geometry->item_x, &out_geometry->item_y);
    out_geometry->item_width = item->width;
    out_geometry->item_height = item->height;
    box_coords(marker, &out_geometry->marker_x, &out_geometry->marker_y);
    out_geometry->marker_width = marker->width;
    out_geometry->marker_height = marker->height;
    have_first = 0;
    pcore_list_text_geometry(item, item, out_geometry, &have_first);
    return have_first ? 0 : 1;
}

/* Deepest box (absolute coords via box_coords) containing point (px,py). */
static struct box *pcore_hit(struct box *b, int px, int py)
{
    struct box *c;
    int ax = 0;
    int ay = 0;

    for (c = b->children; c != NULL; c = c->next) {
        struct box *r = pcore_hit(c, px, py);
        if (r != NULL) {
            return r;   /* children first: report the deepest match */
        }
    }
    box_coords(b, &ax, &ay);
    if (px >= ax && px < ax + b->width &&
            py >= ay && py < ay + b->height) {
        return b;
    }
    return NULL;
}

PCORE_API int PCore_LinkAt(HANDLE hDoc, int x, int y, char *out_href, int cap)
{
    dom_document *doc = (dom_document *) hDoc;
    pcore_render *st = pcore_get_render(doc);
    struct box   *hit;
    struct box   *b;
    int           rc = 0;

    if (st == NULL || out_href == NULL || cap <= 0) {
        return 0;
    }
    if (pcore_ensure_link_strings() != 0) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    if (hit == NULL) {
        return 0;
    }
    /* Walk up to the nearest <a> ancestor and read its href. */
    for (b = hit; b != NULL; b = b->parent) {
        dom_string *nm = NULL;
        bool isa = false;

        if (b->node == NULL) {
            continue;
        }
        if (dom_node_get_node_name(b->node, &nm) == DOM_NO_ERR && nm != NULL) {
            isa = dom_string_caseless_isequal(nm, pcore_a_name);
            dom_string_unref(nm);
        }
        if (isa) {
            dom_string *val = NULL;
            if (dom_element_get_attribute((dom_element *) b->node,
                    pcore_href_name, &val) == DOM_NO_ERR && val != NULL) {
                const char *s = dom_string_data(val);
                int n = (int) dom_string_byte_length(val);
                if (n > cap - 1) {
                    n = cap - 1;
                }
                memcpy(out_href, s, n);
                out_href[n] = '\0';
                dom_string_unref(val);
                rc = 1;
            }
            break;
        }
    }
    return rc;
}

static struct scrollbar *pcore_scrollbar_at(struct box *box, int x, int y,
        int *out_x, int *out_y, int *dirty_x, int *dirty_y,
        int *dirty_w, int *dirty_h)
{
    struct box *child;
    struct scrollbar *found;
    int ax;
    int ay;
    int visible_width;
    int visible_height;
    int length;
    int bar_x;
    int bar_y;

    if (box == NULL) {
        return NULL;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_scrollbar_at(child, x, y, out_x, out_y,
                dirty_x, dirty_y, dirty_w, dirty_h);
        if (found != NULL) {
            return found;
        }
    }

    box_coords(box, &ax, &ay);
    visible_width = box->width + box->padding[LEFT] + box->padding[RIGHT];
    visible_height = box->height + box->padding[TOP] + box->padding[BOTTOM];
    if (box->scroll_x != NULL) {
        length = visible_width -
                ((box->scroll_y != NULL) ? SCROLLBAR_WIDTH : 0);
        bar_x = ax;
        bar_y = ay + visible_height - SCROLLBAR_WIDTH;
        if (x >= bar_x && x < bar_x + length && y >= bar_y &&
                y < bar_y + SCROLLBAR_WIDTH) {
            *out_x = bar_x;
            *out_y = bar_y;
            *dirty_x = ax;
            *dirty_y = ay;
            *dirty_w = visible_width;
            *dirty_h = visible_height;
            return box->scroll_x;
        }
    }
    if (box->scroll_y != NULL) {
        length = visible_height;
        bar_x = ax + visible_width - SCROLLBAR_WIDTH;
        bar_y = ay;
        if (x >= bar_x && x < bar_x + SCROLLBAR_WIDTH && y >= bar_y &&
                y < bar_y + length) {
            *out_x = bar_x;
            *out_y = bar_y;
            *dirty_x = ax;
            *dirty_y = ay;
            *dirty_w = visible_width;
            *dirty_h = visible_height;
            return box->scroll_y;
        }
    }
    return NULL;
}

PCORE_API int PCore_OverflowPointer(HANDLE hDoc, int action, int x, int y)
{
    pcore_render *st;
    struct scrollbar *scrollbar;
    int origin_x;
    int origin_y;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    if (action == PCORE_POINTER_DOWN) {
        origin_x = 0;
        origin_y = 0;
        st->overflow_dirty_valid = 0;
        scrollbar = pcore_scrollbar_at(st->root_box, x, y,
                &origin_x, &origin_y, &st->overflow_dirty_x,
                &st->overflow_dirty_y, &st->overflow_dirty_w,
                &st->overflow_dirty_h);
        if (scrollbar == NULL) {
            return 0;
        }
        st->overflow_dirty_valid = 1;
        st->active_scrollbar = scrollbar;
        st->active_scrollbar_x = origin_x;
        st->active_scrollbar_y = origin_y;
        scrollbar_mouse_action(scrollbar, BROWSER_MOUSE_PRESS_1,
                x - origin_x, y - origin_y);
        return 1;
    }
    scrollbar = st->active_scrollbar;
    if (scrollbar == NULL) {
        return 0;
    }
    origin_x = st->active_scrollbar_x;
    origin_y = st->active_scrollbar_y;
    if (action == PCORE_POINTER_MOVE) {
        scrollbar_mouse_action(scrollbar, BROWSER_MOUSE_DRAG_1,
                x - origin_x, y - origin_y);
        return 1;
    }
    if (action == PCORE_POINTER_UP) {
        if (pcore_scrollbar_is_dragging(scrollbar)) {
            scrollbar_mouse_drag_end(scrollbar, BROWSER_MOUSE_HOVER,
                    x - origin_x, y - origin_y);
        }
        st->active_scrollbar = NULL;
        return 1;
    }
    return 0;
}

PCORE_API int PCore_OverflowDirtyRect(HANDLE hDoc,
        int *x, int *y, int *w, int *h)
{
    pcore_render *st;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL || !st->overflow_dirty_valid || x == NULL || y == NULL ||
            w == NULL || h == NULL) {
        return 0;
    }
    *x = st->overflow_dirty_x;
    *y = st->overflow_dirty_y;
    *w = st->overflow_dirty_w;
    *h = st->overflow_dirty_h;
    return 1;
}

PCORE_API void PCore_PaintDocument(HANDLE hDoc, HDC hdc,
        int scroll_x, int scroll_y)
{
    pcore_render *st = pcore_get_render((dom_document *) hDoc);
    struct redraw_context      rc;
    struct content_redraw_data data;
    struct rect   clip;
    struct { HDC hdc; } pv;   /* layout matches pcore_plot_ctx (one HDC) */
    RECT          cb;

    if (st == NULL || hdc == NULL) {
        return;
    }

    pv.hdc = hdc;
    memset(&rc, 0, sizeof(rc));
    rc.background_images = true;   /* else html_redraw_background paints nothing */
    rc.plot = &pcore_gdi_plotters;
    rc.priv = &pv;

    memset(&data, 0, sizeof(data));
    data.x = -scroll_x;            /* shift the page beneath the viewport */
    data.y = -scroll_y;
    data.width = st->vw;
    data.height = st->vh;
    data.background_colour = 0x00ffffff;
    data.scale = 1.0f;

    /* Clip to the DC's update region (the WM_PAINT invalid rect), falling back
     * to the layout viewport. */
    if (GetClipBox(hdc, &cb) != ERROR &&
            cb.right > cb.left && cb.bottom > cb.top) {
        clip.x0 = cb.left;
        clip.y0 = cb.top;
        clip.x1 = cb.right;
        clip.y1 = cb.bottom;
    } else {
        clip.x0 = 0;
        clip.y0 = 0;
        clip.x1 = st->vw;
        clip.y1 = st->vh;
    }

    html_redraw((struct content *) &st->content, &data, &clip, &rc);
}
