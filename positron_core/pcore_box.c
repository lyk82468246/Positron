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
 * structures, plus cached <img> resources decoded by WM Imaging/libsvgtiny,
 * are built for NetSurf's real layout/redraw path. Forms/widgets, floats,
 * background images, and exact rowspan occupancy remain staged follow-ups.
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
#include "content/handlers/html/box.h"

#include "positron_core.h"
#include "positron_image.h"
#include "pcore_internal.h"

#include "utils/errors.h"                    /* nserror (layout.h / private.h) */
#include "netsurf/layout.h"                  /* struct gui_layout_table */
#include "content/handlers/html/private.h"   /* html_content (real NetSurf) */
#include "content/handlers/html/layout.h"    /* layout_document */
#include "content/handlers/html/box_inspect.h" /* box_coords */
#include "netsurf/content.h"                  /* content_redraw_data */
#include "netsurf/bitmap.h"                   /* thin WM Imaging carrier */
#include "netsurf/plotters.h"                 /* struct redraw_context */

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
 * (borrowed; NULL for anonymous boxes). Mirrors the fields box_create() in
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
            d == CSS_DISPLAY_INLINE_FLEX) ? 1 : 0;
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
 * document cache; retained SVG state is released by the carrier destructor.
 * layout.c reads its intrinsic dimensions through
 * content_get_width/height; redraw.c reaches plot_bitmap through
 * content_redraw. Return NULL for absent/cache-miss/undecodable resources so
 * the caller can retain the established alt/src fallback. */
static int pcore_bitmap_destroy(struct bitmap *bitmap)
{
    if (bitmap != NULL && bitmap->kind == PCORE_BITMAP_SVG &&
            bitmap->svg != NULL) {
        PImage_FreeSvg((PIMAGE_SVG) bitmap->svg);
        bitmap->svg = NULL;
    }
    return 0;
}

static struct box *pcore_make_cached_image_box(dom_node *node,
        css_computed_style *style, void *ctx)
{
    char *src = NULL;
    size_t src_len = 0;
    char *url = NULL;
    const char *data = NULL;
    int len = 0;
    int width = 0;
    int height = 0;
    int kind = 0;
    PIMAGE_SVG svg = NULL;
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
            doc == NULL ||
            pcore_image_resource_get(doc, url, &data, &len) != 0) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        free(url);
        return NULL;
    }
    dom_node_unref((dom_node *) doc);
    free(url);

    if (PCore_ImageInfoFromMemory(data, len, &width, &height) == 0 &&
            width > 0 && height > 0) {
        kind = PCORE_BITMAP_WM_IMAGE;
    } else if (PImage_CreateSvgFromMemory(data, len, 0, 0, &svg) ==
            PIMAGE_OK && svg != NULL &&
            PImage_SvgGetInfo(svg, &width, &height, NULL) == PIMAGE_OK &&
            width > 0 && height > 0) {
        kind = PCORE_BITMAP_SVG;
    } else {
        if (svg != NULL) {
            PImage_FreeSvg(svg);
        }
        return NULL;
    }

    bitmap = talloc_zero(ctx, struct bitmap);
    if (bitmap == NULL) {
        if (svg != NULL) {
            PImage_FreeSvg(svg);
        }
        return NULL;
    }
    bitmap->kind = kind;
    bitmap->data = data;
    bitmap->len = len;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->svg = (void *) svg;
    talloc_set_destructor(bitmap, pcore_bitmap_destroy);

    box = pcore_box_new(BOX_INLINE, style, ctx);
    if (box == NULL) {
        return NULL;
    }
    box->node = node;
    box->object = (struct hlcache_handle *) bitmap;
    box->flags |= IS_REPLACED;
    return box;
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
        css_computed_style *style, void *ctx)
{
    dom_string *txt = NULL;
    struct box *b = NULL;

    if (dom_node_get_text_content(tnode, &txt) != DOM_NO_ERR || txt == NULL) {
        return NULL;
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
        css_computed_style *style, int is_root, void *ctx);
static void pcore_construct_inline(dom_node *node, css_computed_style *style,
        struct box *cont, void *ctx);
static struct box *pcore_construct_table(dom_node *node,
        css_computed_style *style, void *ctx);
static struct box *pcore_construct_flex(dom_node *node,
        css_computed_style *style, int is_inline, void *ctx);

/* Flatten the inline content of `node` into inline container `cont`: text
 * children become BOX_TEXT; inline element children emit BOX_INLINE ...
 * BOX_INLINE_END around their (recursively flattened) content; inline-block
 * children become an atomic BOX_INLINE_BLOCK holding a block subtree. */
static void pcore_construct_inline(dom_node *node, css_computed_style *style,
        struct box *cont, void *ctx)
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
                struct box *t = pcore_make_text_box(child, style, ctx);
                if (t != NULL) {
                    t->node = node;   /* attribute text to its inline
                                       * element (e.g. <a>) for hit-testing */
                    pcore_box_add_text(cont, t);
                }
            } else if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_node_computed_style(child);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    uint8_t d = css_computed_display(cs, false);
                    if (pcore_node_name_is(child, "img")) {
                        struct box *img = pcore_make_cached_image_box(child,
                                cs, ctx);
                        if (img == NULL) {
                            img = pcore_make_image_fallback_box(child, cs,
                                    ctx);
                        }
                        if (img != NULL) {
                            pcore_box_add_child(cont, img);
                        }
                    } else if (d == CSS_DISPLAY_INLINE_BLOCK ||
                            d == CSS_DISPLAY_INLINE_TABLE ||
                            d == CSS_DISPLAY_INLINE_FLEX) {
                        /* atomic inline: a block subtree typed inline-block */
                        struct box *ib = pcore_construct_block(child, cs, 0,
                                ctx);
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
                            pcore_construct_inline(child, cs, cont, ctx);
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
        css_computed_style *style, int is_root, void *ctx)
{
    struct box *box;
    struct box *inline_cont = NULL;
    dom_node *child;

    box = pcore_box_new(BOX_BLOCK, style, ctx);
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
                    struct box *t = pcore_make_text_box(child, style, ctx);
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
                css_computed_style *cs = pcore_node_computed_style(child);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    if (pcore_node_name_is(child, "img")) {
                        struct box *img;
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        img = pcore_make_cached_image_box(child, cs, ctx);
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
                                d == CSS_DISPLAY_INLINE_FLEX) {
                            struct box *ib = pcore_construct_block(child, cs,
                                    0, ctx);
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
                                        ctx);
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
                            cbox = pcore_construct_flex(child, cs, 0, ctx);
                        } else if (css_computed_display(cs, false) ==
                                CSS_DISPLAY_TABLE) {
                            cbox = pcore_construct_table(child, cs, ctx);
                        } else {
                            cbox = pcore_construct_block(child, cs, 0, ctx);
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
        css_computed_style *style, int is_inline, void *ctx)
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
            css_computed_style *cs = pcore_node_computed_style(child);
            if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                uint8_t d = css_computed_display(cs, false);
                struct box *item;
                if (d == CSS_DISPLAY_FLEX || d == CSS_DISPLAY_INLINE_FLEX) {
                    item = pcore_construct_flex(child, cs, 0, ctx);
                } else {
                    item = pcore_construct_block(child, cs, 0, ctx);
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

/* Read an HTML span attribute (colspan / rowspan) as a positive int (default
 * 1), parsing the leading ASCII digits of the attribute value. */
static unsigned int pcore_span_attr(dom_node *node, const char *attr)
{
    dom_string *nm = NULL;
    dom_string *vl = NULL;
    unsigned int n = 1;

    if (dom_string_create((const uint8_t *) attr, strlen(attr), &nm) !=
            DOM_NO_ERR) {
        return 1;
    }
    if (dom_element_get_attribute(node, nm, &vl) == DOM_NO_ERR && vl != NULL) {
        const char *s = dom_string_data(vl);
        size_t len = dom_string_byte_length(vl);
        int v = 0;
        size_t i;
        for (i = 0; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
            v = v * 10 + (s[i] - '0');
        }
        if (v > 0) {
            n = (unsigned int) v;
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
        css_computed_style *style, void *ctx)
{
    struct box *cell = pcore_construct_block(node, style, 0, ctx);
    if (cell != NULL) {
        cell->type = BOX_TABLE_CELL;
        cell->columns = pcore_span_attr(node, "colspan");
        cell->rows = pcore_span_attr(node, "rowspan");
    }
    return cell;
}

/* BOX_TABLE_ROW from a <tr>: its table-cell element children become cells. */
static struct box *pcore_construct_row(dom_node *node,
        css_computed_style *style, void *ctx)
{
    struct box *row = pcore_box_new(BOX_TABLE_ROW, style, ctx);
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

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                type == DOM_ELEMENT_NODE) {
            css_computed_style *cs = pcore_node_computed_style(child);
            if (cs != NULL && !pcore_is_display_none(cs, 0) &&
                    pcore_disp_table_cell(cs)) {
                struct box *cell = pcore_construct_cell(child, cs, ctx);
                if (cell != NULL) {
                    pcore_box_add_child(row, cell);
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
    return row;
}

/* BOX_TABLE_ROW_GROUP from <thead>/<tbody>/<tfoot>: children are rows. */
static struct box *pcore_construct_rowgroup(dom_node *node,
        css_computed_style *style, void *ctx)
{
    struct box *rg = pcore_box_new(BOX_TABLE_ROW_GROUP, style, ctx);
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

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                type == DOM_ELEMENT_NODE) {
            css_computed_style *cs = pcore_node_computed_style(child);
            if (cs != NULL && !pcore_is_display_none(cs, 0) &&
                    pcore_disp_table_row(cs)) {
                struct box *row = pcore_construct_row(child, cs, ctx);
                if (row != NULL) {
                    pcore_box_add_child(rg, row);
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
    return rg;
}

/* Build a BOX_TABLE tree (row-group > row > cell) from a display:table element.
 * Bare <tr> children are wrapped in an anonymous row group. Each cell is then
 * assigned a start_column (honouring colspan; rowspan column-occupancy across
 * rows is simplified - correct for the common no-rowspan tables), and
 * table->columns / table->rows / table->col[] are filled - the shape
 * layout_table needs (it asserts columns>0 and memcpy's table->col). Falls back
 * to BOX_BLOCK if no cells were produced, so layout never sees a malformed
 * table. */
static struct box *pcore_construct_table(dom_node *node,
        css_computed_style *style, void *ctx)
{
    struct box *table = pcore_box_new(BOX_TABLE, style, ctx);
    struct box *anon_rg = NULL;
    struct box *rg;
    struct box *row;
    struct box *cell;
    dom_node *child;
    unsigned int max_cols = 0;
    unsigned int nrows = 0;

    if (table == NULL) {
        return NULL;
    }
    table->node = node;

    if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
        while (child != NULL) {
            dom_node_type type;
            dom_node *next;

            if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                    type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_node_computed_style(child);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    if (pcore_disp_table_rowgroup(cs)) {
                        struct box *g = pcore_construct_rowgroup(child, cs,
                                ctx);
                        if (g != NULL) {
                            pcore_box_add_child(table, g);
                        }
                        anon_rg = NULL;
                    } else if (pcore_disp_table_row(cs)) {
                        struct box *r;
                        if (anon_rg == NULL) {
                            anon_rg = pcore_box_new(BOX_TABLE_ROW_GROUP, NULL,
                                    ctx);
                            if (anon_rg != NULL) {
                                pcore_box_add_child(table, anon_rg);
                            }
                        }
                        r = pcore_construct_row(child, cs, ctx);
                        if (r != NULL && anon_rg != NULL) {
                            pcore_box_add_child(anon_rg, r);
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
    }

    /* layout_table asserts table->children && children->children && columns. */
    if (table->children == NULL || table->children->children == NULL) {
        table->type = BOX_BLOCK;
        return table;
    }

    for (rg = table->children; rg != NULL; rg = rg->next) {
        for (row = rg->children; row != NULL; row = row->next) {
            unsigned int col = 0;
            nrows++;
            for (cell = row->children; cell != NULL; cell = cell->next) {
                cell->start_column = col;
                if (cell->columns == 0) {
                    cell->columns = 1;
                }
                col += cell->columns;
            }
            if (col > max_cols) {
                max_cols = col;
            }
        }
    }

    if (max_cols == 0) {
        table->type = BOX_BLOCK;
        return table;
    }

    table->columns = max_cols;
    table->rows = nrows;
    table->col = talloc_zero_array(ctx, struct column, max_cols);
    if (table->col == NULL) {
        table->type = BOX_BLOCK;
        return table;
    }
    /* col[].type stays COLUMN_WIDTH_UNKNOWN(0); table_calculate_column_types
     * recomputes the types from cell styles during layout_table. */

    return table;
}

/* Internal (pcore_internal.h). */
struct box *pcore_box_construct(struct dom_node *root, void *ctx)
{
    css_computed_style *style;

    if (root == NULL) {
        return NULL;
    }
    style = pcore_node_computed_style(root);
    if (style == NULL) {
        return NULL;
    }
    return pcore_construct_block(root, style, 1, ctx);
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

PCORE_API int PCore_LayoutDocument(HANDLE hDoc, int viewport_w, int viewport_h)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;
    void         *ctx;
    void         *old = NULL;
    struct box   *tree;
    pcore_render *st;

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
    tree = pcore_box_construct(root, ctx);
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

    layout_document(&st->content, viewport_w, viewport_h);

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
