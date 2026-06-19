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
                        /* block-level child: close any open inline run. */
                        struct box *cbox;
                        inline_cont = NULL;
                        cbox = pcore_construct_block(child, cs, 0, ctx);
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
        "<style>body{width:200px}h1{font-size:24px}</style></head>"
        "<body><h1>Title</h1>"
        "<p>Some bold and italic text in a paragraph that should wrap onto "
        "several lines within the body width.</p></body></html>";
    HANDLE hDoc;
    dom_document *doc;
    dom_node *root = NULL;
    void *ctx;
    struct box *tree;
    struct box *t;
    html_content c;
    WCHAR w[256];
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

        t = pcore_first_text(tree);
        wsprintfW(w,
                L"layout_document ran.\r\nroot(html) w=%d h=%d\r\n"
                L"first text box: w=%d x=%d y=%d",
                tree->width, tree->height,
                (t != NULL) ? t->width : -1,
                (t != NULL) ? t->x : -1,
                (t != NULL) ? t->y : -1);
        WideCharToMultiByte(CP_ACP, 0, w, -1, out, cap, NULL, NULL);
        out[cap - 1] = '\0';
    }

    if (ctx != NULL) {
        talloc_free(ctx);
    }
    dom_node_unref(root);
    PCore_FreeDocument(hDoc);
}
