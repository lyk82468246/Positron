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
 * Staged scope (per the milestone plan): list-item / table* / flex display
 * values are treated as BOX_BLOCK for now; real tables/flex come later. Boxes
 * borrow DOM node pointers (the document outlives the box tree) and are all
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
#include "pcore_internal.h"

#include "utils/errors.h"                    /* nserror (layout.h / private.h) */
#include "netsurf/layout.h"                  /* struct gui_layout_table */
#include "content/handlers/html/private.h"   /* html_content (real NetSurf) */
#include "content/handlers/html/layout.h"    /* layout_document */
#include "content/handlers/html/box_inspect.h" /* box_coords */
#include "netsurf/content.h"                  /* content_redraw_data */
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
                char *copy = (char *) talloc_memdup(ctx, data, len);
                if (copy != NULL) {
                    b->text = copy;
                    b->length = len;
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

/* ------------------------------------------------------------------ */
/* construction                                                        */
/* ------------------------------------------------------------------ */

static struct box *pcore_construct_block(dom_node *node,
        css_computed_style *style, int is_root, void *ctx);
static void pcore_construct_inline(dom_node *node, css_computed_style *style,
        struct box *cont, void *ctx);
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
                    pcore_box_add_child(cont, t);
                }
            } else if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_node_computed_style(child);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    uint8_t d = css_computed_display(cs, false);
                    if (d == CSS_DISPLAY_INLINE_BLOCK ||
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
                        pcore_box_add_child(inline_cont, t);
                    }
                }
            } else if (type == DOM_ELEMENT_NODE) {
                css_computed_style *cs = pcore_node_computed_style(child);
                if (cs != NULL && !pcore_is_display_none(cs, 0)) {
                    if (pcore_is_inline_level(cs, 0)) {
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

PCORE_API void PCore_BoxTreeTest(char *out, int cap)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title>"
        "<style>i{font-style:italic}</style></head>"
        "<body><h1>Title</h1>"
        "<p>Some <b>bold</b> and <i>italic</i> text in a paragraph.</p>"
        "<div><p>Nested paragraph one.</p><p>Nested two.</p></div>"
        "</body></html>";
    HANDLE hDoc;
    dom_document *doc;
    dom_node *root = NULL;
    void *ctx;
    struct box *tree;
    int blk = 0, icont = 0, inl = 0, txt = 0, other = 0;
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
    }

    wsprintfW(w, L"box tree: blocks=%d inline_containers=%d\r\n"
                 L"inline(+end/iblock)=%d text=%d other=%d\r\n"
                 L"total=%d (expect blocks>=5, text>=4)",
              blk, icont, inl, txt, other,
              blk + icont + inl + txt + other);
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

/* M5e: render the built-in test page with NetSurf's real layout + redraw,
 * painting through the GDI plotter (M1) + GDI font table (M2). Builds the box
 * tree, runs layout_document, then drives html_redraw into `hdc` over a cw x ch
 * client area. Rebuilt on each call (the app paints this from WM_PAINT). This
 * is the first page rendered end-to-end by the ported NetSurf engine. */
PCORE_API void PCore_NsRenderTest(HDC hdc, int cw, int ch)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><style>"
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h1{color:#800000;font-size:24px;}"
        "p{color:#103080;}"
        "div{background-color:#cce6ff;padding:6px;}"
        ".row{display:flex;background-color:#eeeeee;}"
        ".c1{background-color:#ff8080;padding:4px;}"
        ".c2{background-color:#80ff80;padding:4px;}"
        ".c3{background-color:#8080ff;padding:4px;}"
        "</style></head><body>"
        "<h1>Positron + NetSurf</h1>"
        "<div><p>This paragraph is laid out by NetSurf's real layout.c and "
        "painted by its real redraw.c through our GDI plotter. It should wrap "
        "across several lines inside the light-blue block.</p>"
        "<p>A second paragraph follows below it.</p></div>"
        "<div class=\"row\"><div class=\"c1\">One</div>"
        "<div class=\"c2\">Two</div><div class=\"c3\">Three</div></div>"
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
         * NO background at all - not even background-color. We have no bitmap
         * decoder yet (plot_bitmap is a stub), so background images simply
         * don't draw, but background colours do. */
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
