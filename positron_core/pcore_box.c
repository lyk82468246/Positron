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
 * structures, including NetSurf's table-span occupancy, plus basic relative
 * and absolute positioning, cached <img>,
 * interactive checkbox/radio/button/file gadgets, native-hosted
 * text/password/textarea/select controls and CSS background-image resources
 * decoded by WM Imaging/libsvgtiny, are built for NetSurf's real
 * layout/redraw path. Multipart successful controls are exposed as an opaque
 * snapshot for the host transport; floats remain a staged follow-up.
 * Boxes borrow DOM node pointers (the document outlives the box tree) and are
 * allocated under one talloc context, freed in a single talloc_free.
 *
 * C89.
 */

#include <windows.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>   /* malloc / free for the per-document render state */
#include <string.h>

#include <dom/dom.h>
#include <dom/html/html_button_element.h>
#include <dom/html/html_area_element.h>
#include <dom/html/html_collection.h>
#include <dom/html/html_form_element.h>
#include <dom/html/html_image_element.h>
#include <dom/html/html_input_element.h>
#include <dom/html/html_label_element.h>
#include <dom/html/html_document.h>
#include <dom/html/html_map_element.h>
#include <dom/html/html_option_element.h>
#include <dom/html/html_opt_group_element.h>
#include <dom/html/html_options_collection.h>
#include <dom/html/html_select_element.h>
#include <dom/html/html_text_area_element.h>
#include <dom/events/event.h>
#include <dom/events/event_listener.h>
#include <dom/events/event_target.h>
#include <libcss/libcss.h>

#include "utils/talloc.h"
#include "utils/corestrings.h"
#include "content/handlers/html/box.h"

#include "positron_core.h"
#include "positron_image.h"
#include "pcore_internal.h"
#include "pcore_pattern.h"

#include "utils/errors.h"                    /* nserror (layout.h / private.h) */
#include "content/handlers/html/form_internal.h"
#include "netsurf/layout.h"                  /* struct gui_layout_table */
#include "content/handlers/html/private.h"   /* html_content (real NetSurf) */
#include "content/handlers/html/layout.h"    /* layout_document */
#include "content/handlers/html/box_manipulate.h" /* box_handle_scrollbars */
#include "content/handlers/html/box_inspect.h" /* box_coords */
#include "netsurf/content.h"                  /* content_redraw_data */
#include "netsurf/bitmap.h"                   /* thin WM Imaging carrier */
#include "netsurf/plotters.h"                 /* struct redraw_context */
#include "desktop/scrollbar.h"

/* GDI font measurement table (pcore_plot_gdi.c, M2). */
extern const struct gui_layout_table pcore_gdi_layout;

#define PCORE_IMAGE_MAP_MAX_AREAS 64
#define PCORE_IMAGE_MAP_MAX_COORDS 64
#define PCORE_IMAGE_MAP_MAX_MAPS 64
#define PCORE_IMAGE_MAP_TOKEN_MAX 128
#define PCORE_IMAGE_MAP_COORD_MAX 32767

static struct box *pcore_hit(struct box *box, int px, int py);
static struct box *pcore_box_for_node(struct box *box, dom_node *node);
static struct box *pcore_box_for_any_node(struct box *box, dom_node *node);
static dom_node *pcore_image_map_area_at(struct pcore_render *st,
        dom_document *doc, int x, int y);
static int pcore_image_map_area_geometry(struct pcore_render *st,
        dom_document *doc, dom_node *target, int *x, int *y, int *w, int *h);
static int pcore_link_copy_attribute(dom_element *element,
        dom_string *attribute, char *out, int cap, int required);
static int pcore_link_copy_href_truncated(dom_element *element,
        dom_string *attribute, char *out, int cap);
static dom_element *pcore_box_element_by_id(dom_document *doc,
        const char *element_id);
static int pcore_disclosure_summary_box_info(struct pcore_render *st,
        dom_node *summary, struct box *summary_box, int *x, int *y,
        int *w, int *h, int *open, dom_element **out_details);
static int pcore_utf8_character_count(const char *text,
        unsigned int *out_count);
static int pcore_range_default_value(dom_node *node, char *buffer,
        size_t capacity);
static int pcore_range_fill_default(dom_node *node, dom_string **value_out);

/* Referenced (extern) by content/handlers/css/utils.h; the device DPI in fixed
 * point. Kept in sync by PCore_SetViewport / PCore_SetDeviceViewport. */
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

/* NetSurf blockifies an out-of-flow inline into an inline-block. Without this
 * conversion layout_position_absolute() cannot consume the box because its
 * positioning path intentionally excludes BOX_INLINE markers. */
static int pcore_is_positioned_inline(css_computed_style *style, uint8_t d)
{
    uint8_t position;

    if (style == NULL || d != CSS_DISPLAY_INLINE) {
        return 0;
    }
    position = css_computed_position(style);
    return (position == CSS_POSITION_ABSOLUTE ||
            position == CSS_POSITION_FIXED) ? 1 : 0;
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

/* Return whether `node` is below the first legend child of `fieldset`. The
 * first legend is the one exemption from a disabled fieldset's inherited
 * disabled state. All DOM references acquired by this walk are released
 * before returning. */
static int pcore_node_in_first_legend(dom_node *node, dom_node *fieldset)
{
    dom_node *current;
    dom_node *parent;
    dom_node *direct;
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    int result;

    if (node == NULL || fieldset == NULL) {
        return 0;
    }
    current = dom_node_ref(node);
    direct = NULL;
    while (current != NULL) {
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return -1;
        }
        if (parent == fieldset) {
            direct = current;
            if (parent != NULL) {
                dom_node_unref(parent);
            }
            break;
        }
        dom_node_unref(current);
        current = parent;
    }
    if (direct == NULL) {
        return 0;
    }
    child = NULL;
    if (dom_node_get_first_child(fieldset, &child) != DOM_NO_ERR) {
        dom_node_unref(direct);
        return -1;
    }
    result = 0;
    while (child != NULL) {
        next = NULL;
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            dom_node_unref(direct);
            return -1;
        }
        if (type == DOM_ELEMENT_NODE &&
                pcore_node_name_is(child, "legend")) {
            result = (child == direct) ? 1 : 0;
            dom_node_unref(child);
            dom_node_unref(direct);
            return result;
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            dom_node_unref(direct);
            return -1;
        }
        dom_node_unref(child);
        child = next;
    }
    dom_node_unref(direct);
    return result;
}

/* Walk ancestors and find a disabled fieldset that does not exempt this
 * control through its first legend. The result is -1 on a DOM traversal
 * error, 0 when no inherited disabled state applies, or 1 when it applies. */
static int pcore_node_disabled_fieldset(dom_node *node)
{
    dom_node *current;
    dom_node *parent;
    int in_legend;

    if (node == NULL) {
        return 0;
    }
    current = dom_node_ref(node);
    while (current != NULL) {
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return -1;
        }
        dom_node_unref(current);
        current = parent;
        if (current == NULL) {
            break;
        }
        if (pcore_node_name_is(current, "fieldset") &&
                pcore_node_has_attr(current, "disabled")) {
            in_legend = pcore_node_in_first_legend(node, current);
            if (in_legend < 0) {
                dom_node_unref(current);
                return -1;
            }
            if (!in_legend) {
                dom_node_unref(current);
                return 1;
            }
        }
    }
    return 0;
}

/* Walk option ancestors and find a disabled optgroup. The result is -1 on a
 * DOM traversal error, 0 when no disabled optgroup applies, or 1 when one is
 * found. The option itself is included in the walk so the helper is safe for
 * callers that pass a borrowed option node. */
static int pcore_node_disabled_optgroup(dom_node *node)
{
    dom_node *current;
    dom_node *parent;
    bool disabled;

    if (node == NULL) {
        return 0;
    }
    current = dom_node_ref(node);
    while (current != NULL) {
        if (pcore_node_name_is(current, "optgroup")) {
            disabled = false;
            if (dom_html_opt_group_element_get_disabled(
                    (dom_html_opt_group_element *) current,
                    &disabled) != DOM_NO_ERR) {
                disabled = pcore_node_has_attr(current, "disabled") ?
                        true : false;
            }
            if (disabled) {
                dom_node_unref(current);
                return 1;
            }
        }
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return -1;
        }
        dom_node_unref(current);
        current = parent;
    }
    return 0;
}

int pcore_node_effectively_disabled(dom_node *node, bool *applies,
        bool *disabled)
{
    dom_exception err;
    int inherited;
    bool local_applies;

    if (disabled == NULL) {
        return 1;
    }
    if (applies == NULL) {
        local_applies = false;
        applies = &local_applies;
    }
    *applies = false;
    *disabled = false;
    if (node == NULL) {
        return 1;
    }
    *applies = pcore_node_name_is(node, "input") ||
            pcore_node_name_is(node, "button") ||
            pcore_node_name_is(node, "select") ||
            pcore_node_name_is(node, "textarea") ||
            pcore_node_name_is(node, "option") ||
            pcore_node_name_is(node, "optgroup");
    if (!*applies) {
        return 0;
    }
    if (pcore_node_name_is(node, "input")) {
        err = dom_html_input_element_get_disabled(
                (dom_html_input_element *) node, disabled);
    } else if (pcore_node_name_is(node, "button")) {
        err = dom_html_button_element_get_disabled(
                (dom_html_button_element *) node, disabled);
    } else if (pcore_node_name_is(node, "select")) {
        err = dom_html_select_element_get_disabled(
                (dom_html_select_element *) node, disabled);
    } else if (pcore_node_name_is(node, "textarea")) {
        err = dom_html_text_area_element_get_disabled(
                (dom_html_text_area_element *) node, disabled);
    } else if (pcore_node_name_is(node, "option")) {
        err = dom_html_option_element_get_disabled(
                (dom_html_option_element *) node, disabled);
    } else {
        err = dom_html_opt_group_element_get_disabled(
                (dom_html_opt_group_element *) node, disabled);
    }
    if (err != DOM_NO_ERR) {
        *disabled = pcore_node_has_attr(node, "disabled") ? true : false;
    }
    if (*disabled) {
        return 0;
    }
    if (pcore_node_name_is(node, "option")) {
        inherited = pcore_node_disabled_optgroup(node);
        if (inherited < 0) {
            return 1;
        }
        if (inherited) {
            *disabled = true;
        }
        return 0;
    }
    inherited = pcore_node_disabled_fieldset(node);
    if (inherited < 0) {
        return 1;
    }
    if (inherited) {
        *disabled = true;
    }
    return 0;
}

/* Parse a valid HTML non-negative integer attribute. Malformed values are
 * ignored by constraint validation, matching the platform's conservative
 * form behavior rather than inventing a value for the page. */
static int pcore_node_attr_unsigned(dom_node *node, const char *attr,
        unsigned int *out_value)
{
    dom_string *name;
    dom_string *value;
    const unsigned char *data;
    size_t length;
    size_t index;
    unsigned int result;
    unsigned int digit;

    name = NULL;
    value = NULL;
    if (node == NULL || attr == NULL || out_value == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR) {
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) != DOM_NO_ERR ||
            value == NULL) {
        dom_string_unref(name);
        return 0;
    }
    data = (const unsigned char *) dom_string_data(value);
    length = dom_string_byte_length(value);
    index = 0;
    while (index < length && (data[index] == ' ' || data[index] == '\t' ||
            data[index] == '\r' || data[index] == '\n' ||
            data[index] == '\f')) {
        index++;
    }
    result = 0;
    if (index == length || data[index] < '0' || data[index] > '9') {
        dom_string_unref(value);
        dom_string_unref(name);
        return 0;
    }
    while (index < length && data[index] >= '0' && data[index] <= '9') {
        digit = (unsigned int) (data[index] - '0');
        if (result > (UINT_MAX - digit) / 10u) {
            dom_string_unref(value);
            dom_string_unref(name);
            return 0;
        }
        result = result * 10u + digit;
        index++;
    }
    while (index < length && (data[index] == ' ' || data[index] == '\t' ||
            data[index] == '\r' || data[index] == '\n' ||
            data[index] == '\f')) {
        index++;
    }
    if (index != length) {
        dom_string_unref(value);
        dom_string_unref(name);
        return 0;
    }
    *out_value = result;
    dom_string_unref(value);
    dom_string_unref(name);
    return 1;
}

/* Parse the signed integer grammar used by the tabindex content attribute.
 * Keep this parser locale-independent and bounded: the WM6 CRT's strtol is
 * not a suitable HTML parser, and an overflowing value must not wrap into a
 * different focus order. Leading/trailing HTML ASCII whitespace and an
 * optional sign are accepted. */
static int pcore_dom_tabindex_integer(dom_string *value, int *out_value)
{
    const unsigned char *data;
    size_t length;
    size_t index;
    unsigned int result;
    unsigned int digit;
    unsigned int limit;
    int negative;

    if (value == NULL || out_value == NULL) {
        return 0;
    }
    data = (const unsigned char *) dom_string_data(value);
    length = dom_string_byte_length(value);
    index = 0;
    while (index < length && (data[index] == ' ' || data[index] == '\t' ||
            data[index] == '\r' || data[index] == '\n' ||
            data[index] == '\f')) {
        index++;
    }
    negative = 0;
    if (index < length && (data[index] == '+' || data[index] == '-')) {
        negative = data[index] == '-' ? 1 : 0;
        index++;
    }
    if (index >= length || data[index] < '0' || data[index] > '9') {
        return 0;
    }
    limit = negative ? (unsigned int) INT_MAX + 1U :
            (unsigned int) INT_MAX;
    result = 0;
    while (index < length && data[index] >= '0' && data[index] <= '9') {
        digit = (unsigned int) (data[index] - '0');
        if (result > (limit - digit) / 10U) {
            return 0;
        }
        result = result * 10U + digit;
        index++;
    }
    while (index < length && (data[index] == ' ' || data[index] == '\t' ||
            data[index] == '\r' || data[index] == '\n' ||
            data[index] == '\f')) {
        index++;
    }
    if (index != length) {
        return 0;
    }
    if (negative) {
        *out_value = result == (unsigned int) INT_MAX + 1U ? INT_MIN :
                -(int) result;
    } else {
        *out_value = (int) result;
    }
    return 1;
}

/* Return the raw tabindex state without conflating an absent attribute with
 * a malformed one. Natural controls treat malformed/missing values as the
 * default (zero) group; arbitrary elements require a present, valid value to
 * become keyboard-focusable. */
static int pcore_node_attr_tabindex(dom_node *node, int *out_value,
        int *out_present, int *out_valid)
{
    dom_string *name;
    dom_string *value;

    if (out_value != NULL) {
        *out_value = 0;
    }
    if (out_present != NULL) {
        *out_present = 0;
    }
    if (out_valid != NULL) {
        *out_valid = 0;
    }
    name = NULL;
    value = NULL;
    if (node == NULL || out_value == NULL || out_present == NULL ||
            out_valid == NULL || dom_string_create((const uint8_t *)
            "tabindex", 8, &name) != DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) != DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    if (value != NULL) {
        *out_present = 1;
        *out_valid = pcore_dom_tabindex_integer(value, out_value);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return 1;
}

/* HTML number controls use a deliberately small ASCII grammar. Validate the
 * spelling before calling strtod so the C runtime cannot accept hexadecimal,
 * NaN/Infinity or locale-specific forms that are not valid HTML numbers. */
static int pcore_html_number_syntax(const char *data, size_t length)
{
    size_t index;
    int digits;

    if (data == NULL || length == 0) {
        return 0;
    }
    index = 0;
    if (data[index] == '-') {
        index++;
    }
    if (index >= length) {
        return 0;
    }
    digits = 0;
    while (index < length && data[index] >= '0' && data[index] <= '9') {
        digits = 1;
        index++;
    }
    if (!digits) {
        return 0;
    }
    if (index < length && data[index] == '.') {
        index++;
        digits = 0;
        while (index < length && data[index] >= '0' && data[index] <= '9') {
            digits = 1;
            index++;
        }
        if (!digits) {
            return 0;
        }
    }
    if (index < length && (data[index] == 'e' || data[index] == 'E')) {
        index++;
        if (index < length && (data[index] == '+' || data[index] == '-')) {
            index++;
        }
        digits = 0;
        while (index < length && data[index] >= '0' && data[index] <= '9') {
            digits = 1;
            index++;
        }
        if (!digits) {
            return 0;
        }
    }
    return index == length;
}

static int pcore_dom_number(dom_string *value, double *out_value)
{
    const char *data;
    size_t length;
    char *copy;
    char *end;
    double parsed;
    int result;

    if (value == NULL || out_value == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (!pcore_html_number_syntax(data, length)) {
        return 0;
    }
    copy = (char *) malloc(length + 1);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, data, length);
    copy[length] = '\0';
    end = NULL;
    parsed = strtod(copy, &end);
    result = (end == copy + length && parsed <= DBL_MAX &&
            parsed >= -DBL_MAX && parsed == parsed);
    free(copy);
    if (result) {
        *out_value = parsed;
    }
    return result;
}

static int pcore_node_attr_number(dom_node *node, const char *attr,
        double *out_value)
{
    dom_string *name;
    dom_string *value;
    int result;

    name = NULL;
    value = NULL;
    result = 0;
    if (node == NULL || attr == NULL || out_value == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        result = pcore_dom_number(value, out_value);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

/* NetSurf box_special.c attaches a form_control to form element boxes. The
 * retained layout/redraw path paints checkbox/radio directly; editable text
 * and select controls keep NetSurf's geometry but expose their state to
 * platform-native widgets through the public PCore_* bridges below. */
static int pcore_form_control_type(dom_node *node)
{
    if (pcore_node_name_is(node, "button")) {
        if (pcore_attr_value_is(node, "type", "reset")) {
            return GADGET_RESET;
        }
        if (pcore_attr_value_is(node, "type", "button")) {
            return GADGET_BUTTON;
        }
        return GADGET_SUBMIT;
    }
    if (pcore_node_name_is(node, "select")) {
        return GADGET_SELECT;
    }
    if (pcore_node_name_is(node, "textarea")) {
        return GADGET_TEXTAREA;
    }
    if (!pcore_node_name_is(node, "input")) {
        return 0;
    }
    if (pcore_attr_value_is(node, "type", "checkbox")) {
        return GADGET_CHECKBOX;
    }
    if (pcore_attr_value_is(node, "type", "radio")) {
        return GADGET_RADIO;
    }
    if (pcore_attr_value_is(node, "type", "password")) {
        return GADGET_PASSWORD;
    }
    if (pcore_attr_value_is(node, "type", "submit")) {
        return GADGET_SUBMIT;
    }
    if (pcore_attr_value_is(node, "type", "reset")) {
        return GADGET_RESET;
    }
    if (pcore_attr_value_is(node, "type", "button")) {
        return GADGET_BUTTON;
    }
    if (pcore_attr_value_is(node, "type", "file")) {
        return GADGET_FILE;
    }
    if (pcore_attr_value_is(node, "type", "hidden") ||
            pcore_attr_value_is(node, "type", "image")) {
        return 0;
    }
    return GADGET_TEXTBOX;
}

static char *pcore_squash_text(void *ctx, dom_string *text)
{
    const char *source;
    char *copy;
    size_t length;
    size_t source_index;
    size_t target_index;
    int pending_space;

    source = (text != NULL) ? dom_string_data(text) : NULL;
    length = (text != NULL) ? dom_string_byte_length(text) : 0;
    copy = (char *) talloc_size(ctx, length + 1);
    if (copy == NULL) {
        return NULL;
    }
    source_index = 0;
    target_index = 0;
    pending_space = 0;
    while (source != NULL && source_index < length) {
        char c;
        int is_space;

        c = source[source_index++];
        is_space = (c == ' ' || c == '\t' || c == '\r' ||
                c == '\n' || c == '\f');
        if (is_space) {
            if (target_index > 0) {
                pending_space = 1;
            }
        } else {
            if (pending_space) {
                copy[target_index++] = ' ';
                pending_space = 0;
            }
            copy[target_index++] = c;
        }
    }
    copy[target_index] = '\0';
    return copy;
}

static int pcore_make_button_control(struct box *box,
        struct form_control *gadget, dom_node *node, css_computed_style *style)
{
    dom_string *name;
    dom_string *value;
    dom_string *content;
    struct box *inline_container;
    struct box *inline_box;
    const char *default_label;
    char *label;
    bool disabled;

    name = NULL;
    value = NULL;
    content = NULL;
    label = NULL;
    disabled = false;
    if (pcore_node_name_is(node, "button")) {
        dom_html_button_element *button;

        button = (dom_html_button_element *) node;
        if (dom_html_button_element_get_name(button, &name) != DOM_NO_ERR) {
            name = NULL;
        }
        if (dom_html_button_element_get_value(button, &value) != DOM_NO_ERR) {
            value = NULL;
        }
        if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
            disabled = pcore_node_has_attr(node, "disabled") ? true : false;
        }
        if (dom_node_get_text_content(node, &content) == DOM_NO_ERR &&
                content != NULL) {
            label = pcore_squash_text(gadget, content);
            dom_string_unref(content);
        }
    } else {
        dom_html_input_element *input;

        input = (dom_html_input_element *) node;
        if (dom_html_input_element_get_name(input, &name) != DOM_NO_ERR) {
            name = NULL;
        }
        if (dom_html_input_element_get_value(input, &value) != DOM_NO_ERR) {
            value = NULL;
        }
        if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
            disabled = pcore_node_has_attr(node, "disabled") ? true : false;
        }
    }
    gadget->name = (name != NULL) ?
            talloc_strndup(gadget, dom_string_data(name),
                    dom_string_byte_length(name)) :
            talloc_strdup(gadget, "");
    gadget->value = (value != NULL) ?
            talloc_strndup(gadget, dom_string_data(value),
                    dom_string_byte_length(value)) :
            talloc_strdup(gadget, "");
    if (name != NULL) {
        dom_string_unref(name);
    }
    if (value != NULL) {
        dom_string_unref(value);
    }
    if (gadget->name == NULL || gadget->value == NULL) {
        return 0;
    }
    gadget->disabled = disabled;
    gadget->length = (unsigned int) strlen(gadget->value);
    default_label = (gadget->type == GADGET_SUBMIT) ? "Submit" :
            ((gadget->type == GADGET_RESET) ? "Reset" : "Button");
    if (label == NULL || label[0] == '\0') {
        if (label != NULL) {
            talloc_free(label);
        }
        if (!pcore_node_name_is(node, "button") &&
                gadget->value[0] != '\0') {
            label = talloc_strdup(gadget, gadget->value);
        } else {
            label = talloc_strdup(gadget, default_label);
        }
    }
    if (label == NULL) {
        return 0;
    }
    inline_container = pcore_box_new(BOX_INLINE_CONTAINER, NULL, box);
    inline_box = pcore_box_new(BOX_TEXT, style, box);
    if (inline_container == NULL || inline_box == NULL) {
        if (inline_container != NULL) { talloc_free(inline_container); }
        if (inline_box != NULL) { talloc_free(inline_box); }
        talloc_free(label);
        return 0;
    }
    inline_box->node = node;
    inline_box->text = talloc_strdup(inline_box, label);
    talloc_free(label);
    if (inline_box->text == NULL) {
        talloc_free(inline_container);
        talloc_free(inline_box);
        return 0;
    }
    inline_box->length = strlen(inline_box->text);
    pcore_box_add_child(inline_container, inline_box);
    pcore_box_add_child(box, inline_container);
    return 1;
}

static int pcore_select_add_option(struct form_control *gadget,
        dom_node *node)
{
    struct form_option *option;
    dom_html_option_element *element;
    dom_string *text;
    dom_string *value;
    bool selected;

    element = (dom_html_option_element *) node;
    text = NULL;
    value = NULL;
    selected = false;
    option = talloc_zero(gadget, struct form_option);
    if (option == NULL) {
        return 0;
    }
    option->node = node;
    if (dom_html_option_element_get_text(element, &text) == DOM_NO_ERR &&
            text != NULL) {
        option->text = pcore_squash_text(option, text);
        dom_string_unref(text);
    } else {
        option->text = talloc_strdup(option, "");
    }
    if (option->text == NULL) {
        talloc_free(option);
        return 0;
    }
    if (dom_html_option_element_get_value(element, &value) == DOM_NO_ERR &&
            value != NULL) {
        option->value = talloc_strndup(option, dom_string_data(value),
                dom_string_byte_length(value));
        dom_string_unref(value);
    } else {
        option->value = talloc_strdup(option, option->text);
    }
    if (option->value == NULL) {
        talloc_free(option);
        return 0;
    }
    if (dom_html_option_element_get_selected(element, &selected) !=
            DOM_NO_ERR) {
        selected = pcore_node_has_attr(node, "selected") ? true : false;
    }
    if (selected && (gadget->data.select.multiple ||
            gadget->data.select.num_selected == 0)) {
        option->selected = true;
        option->initial_selected = true;
        gadget->data.select.num_selected++;
        gadget->data.select.current = option;
    } else if (selected && !gadget->data.select.multiple) {
        dom_html_option_element_set_selected(element, false);
    }
    if (gadget->data.select.items == NULL) {
        gadget->data.select.items = option;
    } else {
        gadget->data.select.last_item->next = option;
    }
    gadget->data.select.last_item = option;
    gadget->data.select.num_items++;
    return 1;
}

static int pcore_make_select_control(struct box *box,
        struct form_control *gadget, dom_node *node, css_computed_style *style)
{
    dom_html_select_element *select;
    dom_html_options_collection *options;
    dom_string *name;
    dom_node *option_node;
    struct form_option *option;
    struct box *inline_container;
    struct box *inline_box;
    const char *display_text;
    uint32_t option_count;
    uint32_t option_index;
    bool disabled;
    bool multiple;

    select = (dom_html_select_element *) node;
    options = NULL;
    name = NULL;
    option_node = NULL;
    option_count = 0;
    disabled = false;
    multiple = false;
    if (dom_html_select_element_get_name(select, &name) == DOM_NO_ERR &&
            name != NULL) {
        gadget->name = talloc_strndup(gadget, dom_string_data(name),
                dom_string_byte_length(name));
        dom_string_unref(name);
    } else {
        gadget->name = talloc_strdup(gadget, "");
    }
    if (gadget->name == NULL) {
        return 0;
    }
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
        disabled = pcore_node_has_attr(node, "disabled") ? true : false;
    }
    if (dom_html_select_element_get_multiple(select, &multiple) !=
            DOM_NO_ERR) {
        multiple = pcore_node_has_attr(node, "multiple") ? true : false;
    }
    gadget->disabled = disabled;
    gadget->data.select.multiple = multiple;
    if (dom_html_select_element_get_options(select, &options) != DOM_NO_ERR ||
            options == NULL ||
            dom_html_options_collection_get_length(options,
                    &option_count) != DOM_NO_ERR) {
        if (options != NULL) {
            dom_html_options_collection_unref(options);
        }
        return 0;
    }
    for (option_index = 0; option_index < option_count; option_index++) {
        option_node = NULL;
        if (dom_html_options_collection_item(options, option_index,
                &option_node) != DOM_NO_ERR || option_node == NULL ||
                !pcore_select_add_option(gadget, option_node)) {
            if (option_node != NULL) {
                dom_node_unref(option_node);
            }
            dom_html_options_collection_unref(options);
            return 0;
        }
        dom_node_unref(option_node);
    }
    dom_html_options_collection_unref(options);
    if (gadget->data.select.num_items == 0) {
        return 0;
    }
    if (!gadget->data.select.multiple &&
            gadget->data.select.num_selected == 0) {
        option = gadget->data.select.items;
        option->selected = true;
        option->initial_selected = true;
        gadget->data.select.current = option;
        gadget->data.select.num_selected = 1;
        dom_html_option_element_set_selected(
                (dom_html_option_element *) option->node, true);
    }
    display_text = (gadget->data.select.current != NULL) ?
            gadget->data.select.current->text : "";
    inline_container = pcore_box_new(BOX_INLINE_CONTAINER, NULL, box);
    inline_box = pcore_box_new(BOX_TEXT, style, box);
    if (inline_container == NULL || inline_box == NULL) {
        if (inline_container != NULL) { talloc_free(inline_container); }
        if (inline_box != NULL) { talloc_free(inline_box); }
        return 0;
    }
    inline_box->node = node;
    inline_box->text = talloc_strdup(inline_box, display_text);
    if (inline_box->text == NULL) {
        talloc_free(inline_container);
        talloc_free(inline_box);
        return 0;
    }
    inline_box->length = strlen(inline_box->text);
    pcore_box_add_child(inline_container, inline_box);
    pcore_box_add_child(box, inline_container);
    return 1;
}

static struct box *pcore_make_form_control_box(dom_node *node,
        css_computed_style *style, void *ctx, int gadget_type)
{
    struct box *box;
    struct form_control *gadget;
    dom_html_input_element *input;
    dom_html_text_area_element *textarea;
    dom_string *name = NULL;
    dom_string *value = NULL;
    int32_t max_length;
    bool selected = false;
    bool disabled = false;

    if (gadget_type != GADGET_CHECKBOX && gadget_type != GADGET_RADIO &&
            gadget_type != GADGET_TEXTBOX &&
            gadget_type != GADGET_PASSWORD &&
            gadget_type != GADGET_TEXTAREA &&
            gadget_type != GADGET_SELECT &&
            gadget_type != GADGET_SUBMIT &&
            gadget_type != GADGET_RESET &&
            gadget_type != GADGET_BUTTON &&
            gadget_type != GADGET_FILE) {
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
    if (gadget_type == GADGET_SUBMIT ||
            gadget_type == GADGET_RESET ||
            gadget_type == GADGET_BUTTON) {
        if (!pcore_make_button_control(box, gadget, node, style)) {
            talloc_free(box);
            return NULL;
        }
        return box;
    }
    if (gadget_type == GADGET_SELECT) {
        if (!pcore_make_select_control(box, gadget, node, style)) {
            talloc_free(box);
            return NULL;
        }
        return box;
    }
    if (gadget_type == GADGET_TEXTAREA) {
        textarea = (dom_html_text_area_element *) node;
        if (dom_html_text_area_element_get_name(textarea, &name) ==
                DOM_NO_ERR && name != NULL) {
            gadget->name = talloc_strndup(gadget,
                    dom_string_data(name),
                    dom_string_byte_length(name));
            dom_string_unref(name);
        } else {
            gadget->name = talloc_strdup(gadget, "");
        }
        if (gadget->name == NULL) {
            talloc_free(box);
            return NULL;
        }
        if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
            disabled = pcore_node_has_attr(node, "disabled") ?
                    true : false;
        }
        gadget->disabled = disabled;
        if (dom_html_text_area_element_get_value(textarea, &value) ==
                DOM_NO_ERR && value != NULL) {
            gadget->value = talloc_strndup(gadget,
                    dom_string_data(value),
                    dom_string_byte_length(value));
            dom_string_unref(value);
        } else {
            gadget->value = talloc_strdup(gadget, "");
        }
        if (gadget->value == NULL) {
            talloc_free(box);
            return NULL;
        }
        gadget->length = (unsigned int) strlen(gadget->value);
        gadget->initial_value = talloc_strdup(gadget, gadget->value);
        gadget->last_synced_value = talloc_strdup(gadget, gadget->value);
        if (gadget->initial_value == NULL ||
                gadget->last_synced_value == NULL) {
            talloc_free(box);
            return NULL;
        }
        gadget->maxlength = UINT_MAX;
        return box;
    }
    input = (dom_html_input_element *) node;
    if (dom_html_input_element_get_name(input, &name) == DOM_NO_ERR &&
            name != NULL) {
        gadget->name = talloc_strndup(gadget, dom_string_data(name),
                dom_string_byte_length(name));
        dom_string_unref(name);
    } else {
        gadget->name = talloc_strdup(gadget, "");
    }
    if (gadget->name == NULL) {
        talloc_free(box);
        return NULL;
    }
    if (dom_html_input_element_get_checked(input, &selected) != DOM_NO_ERR) {
        selected = pcore_node_has_attr(node, "checked") ? true : false;
    }
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
        disabled = pcore_node_has_attr(node, "disabled") ? true : false;
    }
    gadget->selected = selected;
    gadget->disabled = disabled;
    if (gadget_type == GADGET_FILE) {
        if (dom_html_input_element_get_value(input, &value) ==
                DOM_NO_ERR && value != NULL &&
                dom_string_byte_length(value) > 0) {
            gadget->value = talloc_strndup(gadget,
                    dom_string_data(value),
                    dom_string_byte_length(value));
            dom_string_unref(value);
            if (gadget->value == NULL) {
                talloc_free(box);
                return NULL;
            }
            gadget->length = (unsigned int) strlen(gadget->value);
        } else if (value != NULL) {
            dom_string_unref(value);
        }
        return box;
    }
    if (gadget_type == GADGET_TEXTBOX ||
            gadget_type == GADGET_PASSWORD) {
        if (dom_html_input_element_get_value(input, &value) == DOM_NO_ERR &&
                value != NULL) {
            gadget->value = talloc_strndup(gadget,
                    dom_string_data(value),
                    dom_string_byte_length(value));
            dom_string_unref(value);
        } else {
            gadget->value = talloc_strdup(gadget, "");
        }
        if (gadget->value == NULL) {
            talloc_free(box);
            return NULL;
        }
        if (pcore_attr_value_is(node, "type", "range") &&
                gadget->value[0] == '\0') {
            char default_value[64];
            char *default_copy;

            default_copy = NULL;
            if (!pcore_range_default_value(node, default_value,
                    sizeof(default_value))) {
                talloc_free(box);
                return NULL;
            }
            default_copy = talloc_strdup(gadget, default_value);
            if (default_copy == NULL) {
                talloc_free(box);
                return NULL;
            }
            talloc_free(gadget->value);
            gadget->value = default_copy;
        }
        gadget->length = (unsigned int) strlen(gadget->value);
        gadget->initial_value = talloc_strdup(gadget, gadget->value);
        gadget->last_synced_value = talloc_strdup(gadget, gadget->value);
        if (gadget->initial_value == NULL ||
                gadget->last_synced_value == NULL) {
            talloc_free(box);
            return NULL;
        }
        gadget->maxlength = UINT_MAX;
        if (pcore_node_has_attr(node, "maxlength") &&
                dom_html_input_element_get_max_length(input,
                        &max_length) == DOM_NO_ERR &&
                max_length >= 0) {
            gadget->maxlength = (unsigned int) max_length;
        }
    }
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
                    int gadget_type = pcore_form_control_type(child);
                    if (pcore_is_positioned_inline(cs, d)) {
                        struct box *ib = pcore_construct_block(child, cs, 0,
                                ctx, stats);
                        if (ib != NULL) {
                            ib->type = BOX_INLINE_BLOCK;
                            pcore_box_add_child(cont, ib);
                        }
                    } else if (gadget_type != 0) {
                        struct box *gadget = pcore_make_form_control_box(child,
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
                        t->node = node;   /* attribute block text to its
                                           * parent for label hit-testing */
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
                    uint8_t d = css_computed_display(cs, false);
                    int gadget_type = pcore_form_control_type(child);
                    if (pcore_is_positioned_inline(cs, d)) {
                        struct box *ib;
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        ib = pcore_construct_block(child, cs, 0, ctx, stats);
                        if (ib != NULL) {
                            ib->type = BOX_INLINE_BLOCK;
                            pcore_box_add_child(inline_cont, ib);
                        }
                    } else if (gadget_type != 0) {
                        struct box *gadget;
                        if (inline_cont == NULL) {
                            inline_cont = pcore_box_new(BOX_INLINE_CONTAINER,
                                    NULL, ctx);
                            pcore_box_add_child(box, inline_cont);
                        }
                        gadget = pcore_make_form_control_box(child, cs, ctx,
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
    int           doc_width;   /* total laid-out width (CSS px)              */
    int           vw;          /* viewport used for layout (redraw extent)   */
    int           vh;
    int           geometry_device_backed;
    int           geometry_dpi;
    struct scrollbar *active_scrollbar;
    struct box   *overflow_target_box;
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

/* Link names, interned once for hit-testing and metadata queries. */
static dom_string *pcore_a_name = NULL;
static dom_string *pcore_href_name = NULL;
static dom_string *pcore_name_attr = NULL;
static dom_string *pcore_target_name = NULL;
static dom_string *pcore_rel_name = NULL;

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
    if (pcore_name_attr == NULL &&
            dom_string_create((const uint8_t *) "name", 4, &pcore_name_attr)
                    != DOM_NO_ERR) {
        return 1;
    }
    if (pcore_target_name == NULL &&
            dom_string_create((const uint8_t *) "target", 6,
            &pcore_target_name) != DOM_NO_ERR) {
        return 1;
    }
    if (pcore_rel_name == NULL &&
            dom_string_create((const uint8_t *) "rel", 3,
            &pcore_rel_name) != DOM_NO_ERR) {
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

/* ------------------------------------------------------------------ */
/* DOM events                                                         */
/* ------------------------------------------------------------------ */

typedef struct pcore_event_binding pcore_event_binding;

typedef struct pcore_event_state {
    pcore_event_binding *head;
    const PCoreKeyEventData *key_data;
    const PCoreInputEventData *input_data;
    int is_composing;
} pcore_event_state;

struct pcore_event_binding {
    pcore_event_binding *next;
    dom_node *target;
    dom_string *type;
    dom_event_listener *listener;
    int capture;
    PCoreEventListenerFn callback;
    void *pw;
    pcore_event_state *state;
};

static dom_string *pcore_event_state_key = NULL;

static int pcore_ensure_event_state_key(void)
{
    static const char KEY[] = "__pcore_events__";

    if (pcore_event_state_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) KEY, sizeof(KEY) - 1,
            &pcore_event_state_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_event_binding_free(pcore_event_binding *binding)
{
    if (binding == NULL) {
        return;
    }
    if (binding->target != NULL && binding->type != NULL &&
            binding->listener != NULL) {
        dom_event_target_remove_event_listener(binding->target,
                binding->type, binding->listener,
                binding->capture ? true : false);
    }
    if (binding->listener != NULL) {
        dom_event_listener_unref(binding->listener);
    }
    if (binding->type != NULL) {
        dom_string_unref(binding->type);
    }
    if (binding->target != NULL) {
        dom_node_unref(binding->target);
    }
    free(binding);
}

static void pcore_event_state_free(pcore_event_state *state)
{
    pcore_event_binding *binding;
    pcore_event_binding *next;

    if (state == NULL) {
        return;
    }
    binding = state->head;
    while (binding != NULL) {
        next = binding->next;
        pcore_event_binding_free(binding);
        binding = next;
    }
    free(state);
}

static void pcore_event_state_ud_handler(dom_node_operation op,
        dom_string *key, void *data, struct dom_node *src,
        struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_event_state_free((pcore_event_state *) data);
    }
}

static pcore_event_state *pcore_event_state_get(dom_document *doc, int create)
{
    pcore_event_state *state;
    void *data;
    void *old;

    if (doc == NULL || pcore_ensure_event_state_key() != 0) {
        return NULL;
    }
    data = NULL;
    if (dom_node_get_user_data((dom_node *) doc, pcore_event_state_key,
            &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL || !create) {
        return (pcore_event_state *) data;
    }
    state = (pcore_event_state *) calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }
    old = NULL;
    if (dom_node_set_user_data((dom_node *) doc, pcore_event_state_key,
            state, pcore_event_state_ud_handler, &old) != DOM_NO_ERR) {
        free(state);
        return NULL;
    }
    if (old != NULL && old != state) {
        pcore_event_state_free((pcore_event_state *) old);
    }
    return state;
}

static dom_string *pcore_event_element_id(dom_event_target *target)
{
    dom_node_type node_type;
    dom_string *name;
    dom_string *value;

    if (target == NULL ||
            dom_node_get_node_type((dom_node *) target, &node_type) !=
                    DOM_NO_ERR ||
            node_type != DOM_ELEMENT_NODE) {
        return NULL;
    }
    name = NULL;
    if (dom_string_create((const uint8_t *) "id", 2, &name) !=
            DOM_NO_ERR || name == NULL) {
        return NULL;
    }
    value = NULL;
    if (dom_element_get_attribute((dom_element *) target, name, &value) !=
            DOM_NO_ERR) {
        value = NULL;
    }
    dom_string_unref(name);
    return value;
}

static char *pcore_event_element_id_copy(dom_node *node)
{
    dom_string *id;
    const char *data;
    size_t length;
    char *copy;

    id = pcore_event_element_id((dom_event_target *) node);
    if (id == NULL) {
        return NULL;
    }
    data = dom_string_data(id);
    length = dom_string_byte_length(id);
    if (data == NULL || length == 0 || length > (size_t) INT_MAX) {
        dom_string_unref(id);
        return NULL;
    }
    copy = (char *) malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, data, length);
        copy[length] = '\0';
    }
    dom_string_unref(id);
    return copy;
}

static void pcore_event_listener_adapter(dom_event *event, void *pw)
{
    pcore_event_binding *binding;
    PCoreEventInfo info;
    dom_event_flow_phase phase;
    dom_event_target *target;
    dom_event_target *current_target;
    dom_string *target_id;
    dom_string *current_target_id;
    bool value;
    unsigned int actions;

    binding = (pcore_event_binding *) pw;
    if (binding == NULL || binding->callback == NULL) {
        return;
    }
    memset(&info, 0, sizeof(info));
    target = NULL;
    current_target = NULL;
    target_id = NULL;
    current_target_id = NULL;
    (void) dom_event_get_target(event, &target);
    (void) dom_event_get_current_target(event, &current_target);
    target_id = pcore_event_element_id(target);
    current_target_id = pcore_event_element_id(current_target);
    if (target_id != NULL) {
        info.target_id = dom_string_data(target_id);
    }
    if (current_target_id != NULL) {
        info.current_target_id = dom_string_data(current_target_id);
    }
    phase = DOM_AT_TARGET;
    if (dom_event_get_event_phase(event, &phase) == DOM_NO_ERR) {
        info.phase = (unsigned int) phase;
    }
    value = false;
    if (dom_event_get_bubbles(event, &value) == DOM_NO_ERR) {
        info.bubbles = value ? 1 : 0;
    }
    value = false;
    if (dom_event_get_cancelable(event, &value) == DOM_NO_ERR) {
        info.cancelable = value ? 1 : 0;
    }
    value = false;
    if (dom_event_get_is_trusted(event, &value) == DOM_NO_ERR) {
        info.trusted = value ? 1 : 0;
    }
    value = false;
    if (dom_event_is_default_prevented(event, &value) == DOM_NO_ERR) {
        info.default_prevented = value ? 1 : 0;
    }
    if (binding->state != NULL && binding->state->key_data != NULL) {
        info.key = binding->state->key_data->key;
        info.key_code = binding->state->key_data->key_code;
        info.char_code = binding->state->key_data->char_code;
        info.repeat = binding->state->key_data->repeat ? 1 : 0;
        info.shift = binding->state->key_data->shift ? 1 : 0;
        info.ctrl = binding->state->key_data->ctrl ? 1 : 0;
        info.alt = binding->state->key_data->alt ? 1 : 0;
        info.is_composing = binding->state->is_composing ? 1 : 0;
    }
    if (binding->state != NULL && binding->state->input_data != NULL) {
        info.input_type = binding->state->input_data->input_type;
        info.data = binding->state->input_data->data;
        info.is_composing = binding->state->is_composing ? 1 : 0;
    }
    actions = binding->callback(binding->pw, &info);
    if ((actions & PCORE_EVENT_ACTION_PREVENT_DEFAULT) != 0) {
        dom_event_prevent_default(event);
    }
    if ((actions & PCORE_EVENT_ACTION_STOP_IMMEDIATE) != 0) {
        dom_event_stop_immediate_propagation(event);
    } else if ((actions & PCORE_EVENT_ACTION_STOP_PROPAGATION) != 0) {
        dom_event_stop_propagation(event);
    }
    if (target_id != NULL) {
        dom_string_unref(target_id);
    }
    if (current_target_id != NULL) {
        dom_string_unref(current_target_id);
    }
    if (target != NULL) {
        dom_node_unref((dom_node *) target);
    }
    if (current_target != NULL) {
        dom_node_unref((dom_node *) current_target);
    }
}

PCORE_API HANDLE PCore_EventListenerAdd(HANDLE hDoc,
        const char *element_id, const char *event_type, int capture,
        PCoreEventListenerFn callback, void *pw)
{
    dom_document *doc;
    dom_element *element;
    dom_string *id;
    dom_string *type;
    dom_event_listener *listener;
    pcore_event_state *state;
    pcore_event_binding *binding;

    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0' ||
            event_type == NULL || event_type[0] == '\0' || callback == NULL) {
        return NULL;
    }
    doc = (dom_document *) hDoc;
    state = pcore_event_state_get(doc, 1);
    if (state == NULL) {
        return NULL;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id, strlen(element_id),
            &id) != DOM_NO_ERR || id == NULL) {
        return NULL;
    }
    if (dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR ||
            element == NULL) {
        dom_string_unref(id);
        return NULL;
    }
    dom_string_unref(id);
    type = NULL;
    if (dom_string_create((const uint8_t *) event_type, strlen(event_type),
            &type) != DOM_NO_ERR || type == NULL) {
        dom_node_unref((dom_node *) element);
        return NULL;
    }
    binding = (pcore_event_binding *) calloc(1, sizeof(*binding));
    if (binding == NULL) {
        dom_string_unref(type);
        dom_node_unref((dom_node *) element);
        return NULL;
    }
    binding->target = (dom_node *) element;
    binding->type = type;
    binding->capture = capture ? 1 : 0;
    binding->callback = callback;
    binding->pw = pw;
    binding->state = state;
    listener = NULL;
    if (dom_event_listener_create(pcore_event_listener_adapter, binding,
            &listener) != DOM_NO_ERR || listener == NULL) {
        pcore_event_binding_free(binding);
        return NULL;
    }
    binding->listener = listener;
    if (dom_event_target_add_event_listener(binding->target, binding->type,
            binding->listener, binding->capture ? true : false) != DOM_NO_ERR) {
        pcore_event_binding_free(binding);
        return NULL;
    }
    binding->next = state->head;
    state->head = binding;
    return (HANDLE) binding;
}

PCORE_API int PCore_EventListenerRemove(HANDLE hDoc, HANDLE hListener)
{
    pcore_event_state *state;
    pcore_event_binding *binding;
    pcore_event_binding *previous;

    if (hDoc == NULL || hListener == NULL) {
        return 0;
    }
    state = pcore_event_state_get((dom_document *) hDoc, 0);
    if (state == NULL) {
        return 0;
    }
    previous = NULL;
    binding = state->head;
    while (binding != NULL && binding != (pcore_event_binding *) hListener) {
        previous = binding;
        binding = binding->next;
    }
    if (binding == NULL) {
        return 0;
    }
    if (previous != NULL) {
        previous->next = binding->next;
    } else {
        state->head = binding->next;
    }
    pcore_event_binding_free(binding);
    return 1;
}

static int pcore_event_dispatch_node(dom_node *target,
        const char *event_type, int bubbles, int cancelable,
        pcore_event_state *state, const PCoreKeyEventData *key_data,
        const PCoreInputEventData *input_data, int is_composing,
        int *default_allowed)
{
    dom_string *type;
    dom_event *event;
    dom_exception err;
    bool success;
    const PCoreKeyEventData *previous_key_data;
    const PCoreInputEventData *previous_input_data;
    int previous_is_composing;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (target == NULL || event_type == NULL || event_type[0] == '\0') {
        return -1;
    }
    type = NULL;
    event = NULL;
    if (dom_string_create((const uint8_t *) event_type, strlen(event_type),
            &type) != DOM_NO_ERR || type == NULL) {
        return -1;
    }
    if (dom_event_create(&event) != DOM_NO_ERR || event == NULL) {
        dom_string_unref(type);
        return -1;
    }
    err = dom_event_init(event, type, bubbles ? true : false,
            cancelable ? true : false);
    if (err == DOM_NO_ERR) {
        err = dom_event_set_is_trusted(event, true);
    }
    success = true;
    if (err == DOM_NO_ERR) {
        previous_key_data = NULL;
        previous_input_data = NULL;
        previous_is_composing = 0;
        if (state != NULL) {
            previous_key_data = state->key_data;
            previous_input_data = state->input_data;
            previous_is_composing = state->is_composing;
            state->key_data = key_data;
            state->input_data = input_data;
            state->is_composing = is_composing ? 1 : 0;
        }
        err = dom_event_target_dispatch_event(target, event, &success);
        if (state != NULL) {
            state->key_data = previous_key_data;
            state->input_data = previous_input_data;
            state->is_composing = previous_is_composing;
        }
    }
    if (default_allowed != NULL && err == DOM_NO_ERR) {
        *default_allowed = success ? 1 : 0;
    }
    dom_event_unref(event);
    dom_string_unref(type);
    return (err == DOM_NO_ERR) ? 1 : -1;
}

static int pcore_event_dispatch_to_id(dom_document *doc,
        const char *element_id, const char *event_type, int bubbles,
        int cancelable, const PCoreKeyEventData *key_data,
        const PCoreInputEventData *input_data, int is_composing,
        int *default_allowed)
{
    dom_element *element;
    dom_string *id;
    pcore_event_state *state;
    int result;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (doc == NULL || element_id == NULL || element_id[0] == '\0') {
        return -1;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id, strlen(element_id),
            &id) != DOM_NO_ERR || id == NULL) {
        return -1;
    }
    if (dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR) {
        dom_string_unref(id);
        return -1;
    }
    dom_string_unref(id);
    if (element == NULL) {
        return 0;
    }
    state = pcore_event_state_get(doc, 0);
    result = pcore_event_dispatch_node((dom_node *) element, event_type,
            bubbles, cancelable, state, key_data, input_data,
            is_composing,
            default_allowed);
    dom_node_unref((dom_node *) element);
    return result;
}

PCORE_API int PCore_EventDispatchToId(HANDLE hDoc, const char *element_id,
        const char *event_type, int bubbles, int cancelable,
        int *default_allowed)
{
    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0') {
        return -1;
    }
    return pcore_event_dispatch_to_id((dom_document *) hDoc, element_id,
            event_type, bubbles, cancelable, NULL, NULL, 0,
            default_allowed);
}

PCORE_API int PCore_EventDispatchKeyToId(HANDLE hDoc,
        const char *element_id, const char *event_type, int bubbles,
        int cancelable, const PCoreKeyEventData *key_data,
        int *default_allowed)
{
    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0') {
        return -1;
    }
    return pcore_event_dispatch_to_id((dom_document *) hDoc, element_id,
            event_type, bubbles, cancelable, key_data, NULL, 0,
            default_allowed);
}

PCORE_API int PCore_EventDispatchKeyExToId(HANDLE hDoc,
        const char *element_id, const char *event_type, int bubbles,
        int cancelable, const PCoreKeyEventDataEx *data,
        int *default_allowed)
{
    PCoreKeyEventData legacy;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0' ||
            data == NULL || data->struct_size < sizeof(*data)) {
        return -1;
    }
    legacy.key = data->key;
    legacy.key_code = data->key_code;
    legacy.char_code = data->char_code;
    legacy.repeat = data->repeat;
    legacy.shift = data->shift;
    legacy.ctrl = data->ctrl;
    legacy.alt = data->alt;
    return pcore_event_dispatch_to_id((dom_document *) hDoc, element_id,
            event_type, bubbles, cancelable, &legacy, NULL,
            data->is_composing, default_allowed);
}

PCORE_API int PCore_EventDispatchInputToId(HANDLE hDoc,
        const char *element_id, const char *event_type, int bubbles,
        int cancelable, const PCoreInputEventData *input_data,
        int *default_allowed)
{
    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0') {
        return -1;
    }
    return pcore_event_dispatch_to_id((dom_document *) hDoc, element_id,
            event_type, bubbles, cancelable, NULL, input_data, 0,
            default_allowed);
}

PCORE_API int PCore_EventDispatchInputExToId(HANDLE hDoc,
        const char *element_id, const char *event_type, int bubbles,
        int cancelable, const PCoreInputEventDataEx *data,
        int *default_allowed)
{
    PCoreInputEventData legacy;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0' ||
            data == NULL || data->struct_size < sizeof(*data)) {
        return -1;
    }
    legacy.input_type = data->input_type;
    legacy.data = data->data;
    return pcore_event_dispatch_to_id((dom_document *) hDoc, element_id,
            event_type, bubbles, cancelable, NULL, &legacy,
            data->is_composing, default_allowed);
}

static int pcore_event_dispatch_at(dom_document *doc, int x, int y,
        const char *event_type, int bubbles, int cancelable,
        const PCoreKeyEventData *key_data,
        const PCoreInputEventData *input_data, int input_is_composing,
        int *default_allowed)
{
    pcore_render *state;
    pcore_event_state *event_state;
    struct box *box;
    dom_node_type node_type;
    dom_node *target;
    int result;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (doc == NULL || event_type == NULL || event_type[0] == '\0') {
        return -1;
    }
    state = pcore_get_render(doc);
    if (state == NULL) {
        return 0;
    }
    target = pcore_image_map_area_at(state, doc, x, y);
    if (target == NULL) {
        box = pcore_hit(state->root_box, x, y);
        while (box != NULL) {
            if (box->node != NULL &&
                    dom_node_get_node_type(box->node, &node_type) ==
                    DOM_NO_ERR && node_type == DOM_ELEMENT_NODE) {
                target = dom_node_ref(box->node);
                break;
            }
            box = box->parent;
        }
    }
    if (target == NULL) {
        return 0;
    }
    event_state = pcore_event_state_get(doc, 0);
    result = pcore_event_dispatch_node(target, event_type, bubbles,
            cancelable, event_state, key_data, input_data,
            input_is_composing,
            default_allowed);
    dom_node_unref(target);
    return result;
}

PCORE_API int PCore_EventDispatchAt(HANDLE hDoc, int x, int y,
        const char *event_type, int bubbles, int cancelable,
        int *default_allowed)
{
    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || event_type == NULL || event_type[0] == '\0') {
        return -1;
    }
    return pcore_event_dispatch_at((dom_document *) hDoc, x, y, event_type,
            bubbles, cancelable, NULL, NULL, 0, default_allowed);
}

PCORE_API int PCore_EventDispatchFocus(HANDLE hDoc, const char *event_type,
        int bubbles, int cancelable, int *default_allowed)
{
    dom_document *doc;
    dom_node *target;
    pcore_event_state *event_state;
    int result;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || event_type == NULL || event_type[0] == '\0' ||
            (bubbles != 0 && bubbles != 1) ||
            (cancelable != 0 && cancelable != 1)) {
        return -1;
    }
    doc = (dom_document *) hDoc;
    target = NULL;
    pcore_interaction_snapshot(doc, &target, NULL, NULL);
    if (target == NULL) {
        return 0;
    }
    target = dom_node_ref(target);
    if (target == NULL) {
        return -1;
    }
    event_state = pcore_event_state_get(doc, 0);
    result = pcore_event_dispatch_node(target, event_type, bubbles,
            cancelable, event_state, NULL, NULL, 0, default_allowed);
    dom_node_unref(target);
    return result < 0 ? -1 : 1;
}

PCORE_API int PCore_EventDispatchKeyAt(HANDLE hDoc, int x, int y,
        const char *event_type, int bubbles, int cancelable,
        const PCoreKeyEventData *key_data, int *default_allowed)
{
    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || event_type == NULL || event_type[0] == '\0') {
        return -1;
    }
    return pcore_event_dispatch_at((dom_document *) hDoc, x, y,
            event_type, bubbles, cancelable, key_data, NULL, 0,
            default_allowed);
}

PCORE_API int PCore_EventDispatchKeyExAt(HANDLE hDoc, int x, int y,
        const char *event_type, int bubbles, int cancelable,
        const PCoreKeyEventDataEx *data, int *default_allowed)
{
    PCoreKeyEventData legacy;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || data == NULL ||
            data->struct_size < sizeof(*data)) {
        return -1;
    }
    legacy.key = data->key;
    legacy.key_code = data->key_code;
    legacy.char_code = data->char_code;
    legacy.repeat = data->repeat;
    legacy.shift = data->shift;
    legacy.ctrl = data->ctrl;
    legacy.alt = data->alt;
    return pcore_event_dispatch_at((dom_document *) hDoc, x, y,
            event_type, bubbles, cancelable, &legacy, NULL,
            data->is_composing, default_allowed);
}

PCORE_API int PCore_EventDispatchInputAt(HANDLE hDoc, int x, int y,
        const char *event_type, int bubbles, int cancelable,
        const PCoreInputEventData *input_data, int *default_allowed)
{
    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || event_type == NULL || event_type[0] == '\0') {
        return -1;
    }
    return pcore_event_dispatch_at((dom_document *) hDoc, x, y,
            event_type, bubbles, cancelable, NULL, input_data, 0,
            default_allowed);
}

PCORE_API int PCore_EventDispatchInputExAt(HANDLE hDoc, int x, int y,
        const char *event_type, int bubbles, int cancelable,
        const PCoreInputEventDataEx *data, int *default_allowed)
{
    PCoreInputEventData legacy;

    if (default_allowed != NULL) {
        *default_allowed = 1;
    }
    if (hDoc == NULL || data == NULL ||
            data->struct_size < sizeof(*data)) {
        return -1;
    }
    legacy.input_type = data->input_type;
    legacy.data = data->data;
    return pcore_event_dispatch_at((dom_document *) hDoc, x, y,
            event_type, bubbles, cancelable, NULL, &legacy,
            data->is_composing, default_allowed);
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
    css_unit_ctx layout_unit_ctx;
    int device_backed;
    int geometry_dpi;
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

    /* Freeze the viewport contract before building anonymous boxes. Device
     * callers have already installed CSS dimensions with
     * PCore_SetDeviceViewport; legacy callers still pass CSS dimensions
     * directly. Anonymous style composition reads this context too, so
     * delaying the decision until after construction can mix CSS and device
     * pixels on a high-DPI relayout. */
    device_backed = (pcore_device_viewport_pending != 0) ? 1 : 0;
    geometry_dpi = FIXTOINT(pcore_get_unit_ctx()->device_dpi);
    if (geometry_dpi <= 0) {
        geometry_dpi = 96;
    }
    if (device_backed) {
        pcore_device_viewport_pending = 0;
    } else {
        PCore_SetViewport(viewport_w, viewport_h, 0);
    }
    memcpy(&layout_unit_ctx, pcore_get_unit_ctx(),
            sizeof(layout_unit_ctx));

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
    st->geometry_device_backed = device_backed;
    st->geometry_dpi = geometry_dpi;

    memset(&st->content, 0, sizeof(st->content));
    st->content.layout = tree;
    st->content.bctx = (int *) ctx;
    st->content.font_func = &pcore_gdi_layout;
    memcpy((void *) &st->content.unit_len_ctx, &layout_unit_ctx,
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
    st->doc_width = tree->x + tree->padding[LEFT] + tree->width +
            tree->padding[RIGHT] + tree->border[RIGHT].width +
            tree->margin[RIGHT];
    if (tree->x + tree->descendant_x1 > st->doc_width) {
        st->doc_width = tree->x + tree->descendant_x1;
                                                    /* include overflowing content */
    }
    if (st->doc_width < viewport_w) {
        st->doc_width = viewport_w;
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

PCORE_API int PCore_DocumentWidth(HANDLE hDoc)
{
    pcore_render *st = pcore_get_render((dom_document *) hDoc);
    return (st != NULL) ? st->doc_width : 0;
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

static struct box *pcore_form_control_at(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->gadget != NULL &&
            (box->gadget->type == GADGET_CHECKBOX ||
             box->gadget->type == GADGET_RADIO ||
             box->gadget->type == GADGET_TEXTBOX ||
             box->gadget->type == GADGET_PASSWORD ||
             box->gadget->type == GADGET_TEXTAREA ||
             box->gadget->type == GADGET_SELECT ||
             box->gadget->type == GADGET_SUBMIT ||
             box->gadget->type == GADGET_RESET ||
             box->gadget->type == GADGET_BUTTON ||
             box->gadget->type == GADGET_FILE)) {
        if (*current == target) {
            return box;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_form_control_at(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

PCORE_API int PCore_FormControlInfo(HANDLE hDoc, unsigned int index,
        int *x, int *y, int *w, int *h, int *kind, int *selected,
        int *disabled)
{
    pcore_render *st;
    struct box *box;
    unsigned int current;
    int ax;
    int ay;
    int control_kind;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_form_control_at(st->root_box, index, &current) : NULL;
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    if (box->gadget->type == GADGET_CHECKBOX) {
        control_kind = 1;
    } else if (box->gadget->type == GADGET_RADIO) {
        control_kind = 2;
    } else if (box->gadget->type == GADGET_TEXTBOX) {
        control_kind = 3;
    } else if (box->gadget->type == GADGET_PASSWORD) {
        control_kind = 4;
    } else if (box->gadget->type == GADGET_TEXTAREA) {
        control_kind = 5;
    } else if (box->gadget->type == GADGET_SELECT) {
        control_kind = 6;
    } else if (box->gadget->type == GADGET_SUBMIT) {
        control_kind = 7;
    } else if (box->gadget->type == GADGET_RESET) {
        control_kind = 8;
    } else if (box->gadget->type == GADGET_BUTTON) {
        control_kind = 9;
    } else if (box->gadget->type == GADGET_FILE) {
        control_kind = 10;
    } else {
        return 1;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (x != NULL) { *x = ax; }
    if (y != NULL) { *y = ay; }
    if (w != NULL) { *w = box->width; }
    if (h != NULL) { *h = box->height; }
    if (kind != NULL) { *kind = control_kind; }
    if (selected != NULL) {
        *selected = (box->gadget->type == GADGET_SELECT) ?
                ((box->gadget->data.select.num_selected > 0) ? 1 : 0) :
                (box->gadget->selected ? 1 : 0);
    }
    if (disabled != NULL) {
        effective_disabled = box->gadget->disabled;
        if (pcore_node_effectively_disabled(box->gadget->node, NULL,
                &effective_disabled) != 0) {
            return 1;
        }
        *disabled = effective_disabled ? 1 : 0;
    }
    return 0;
}

PCORE_API int PCore_FormControlInfoById(HANDLE hDoc, const char *element_id,
        int *x, int *y, int *w, int *h, int *kind, int *selected,
        int *disabled)
{
    dom_document *doc;
    pcore_render *st;
    dom_string *id;
    dom_element *element;
    struct box *box;
    int ax;
    int ay;
    int control_kind;
    bool effective_disabled;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || element_id == NULL || element_id[0] == '\0') {
        return 1;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id,
            strlen(element_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR ||
            element == NULL) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    dom_string_unref(id);
    box = pcore_box_for_node(st->root_box, (dom_node *) element);
    if (box == NULL || box->gadget == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    if (box->gadget->type == GADGET_CHECKBOX) {
        control_kind = 1;
    } else if (box->gadget->type == GADGET_RADIO) {
        control_kind = 2;
    } else if (box->gadget->type == GADGET_TEXTBOX) {
        control_kind = 3;
    } else if (box->gadget->type == GADGET_PASSWORD) {
        control_kind = 4;
    } else if (box->gadget->type == GADGET_TEXTAREA) {
        control_kind = 5;
    } else if (box->gadget->type == GADGET_SELECT) {
        control_kind = 6;
    } else if (box->gadget->type == GADGET_SUBMIT) {
        control_kind = 7;
    } else if (box->gadget->type == GADGET_RESET) {
        control_kind = 8;
    } else if (box->gadget->type == GADGET_BUTTON) {
        control_kind = 9;
    } else if (box->gadget->type == GADGET_FILE) {
        control_kind = 10;
    } else {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (x != NULL) { *x = ax; }
    if (y != NULL) { *y = ay; }
    if (w != NULL) { *w = box->width; }
    if (h != NULL) { *h = box->height; }
    if (kind != NULL) { *kind = control_kind; }
    if (selected != NULL) {
        *selected = (box->gadget->type == GADGET_SELECT) ?
                ((box->gadget->data.select.num_selected > 0) ? 1 : 0) :
                (box->gadget->selected ? 1 : 0);
    }
    if (disabled != NULL) {
        effective_disabled = box->gadget->disabled;
        if (pcore_node_effectively_disabled((dom_node *) element, NULL,
                &effective_disabled) != 0) {
            dom_node_unref((dom_node *) element);
            return 1;
        }
        *disabled = effective_disabled ? 1 : 0;
    }
    dom_node_unref((dom_node *) element);
    return 0;
}

/* Return the effective contenteditable mode only for an editing host. A
 * descendant that inherits its parent's mode is part of that host, not a
 * second native editing surface. The parent reference returned by libdom is
 * released before this helper returns. */
#define PCORE_FOCUS_WALK_DEPTH_MAX 64U

static int pcore_contenteditable_host_mode(dom_node *node, int *out_mode)
{
    dom_node *parent;
    dom_node_type parent_type;
    int mode;
    int parent_mode;

    if (out_mode != NULL) {
        *out_mode = PCORE_CONTENTEDITABLE_MODE_NONE;
    }
    if (node == NULL || out_mode == NULL ||
            pcore_contenteditable_mode(node, &mode) != 0 ||
            mode == PCORE_CONTENTEDITABLE_MODE_NONE) {
        return 0;
    }
    parent = NULL;
    if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR) {
        return 0;
    }
    if (parent != NULL) {
        parent_type = DOM_NODE_TYPE_COUNT;
        if (dom_node_get_node_type(parent, &parent_type) != DOM_NO_ERR) {
            dom_node_unref(parent);
            return 0;
        }
        if (parent_type == DOM_ELEMENT_NODE &&
                pcore_contenteditable_mode(parent, &parent_mode) == 0 &&
                parent_mode != PCORE_CONTENTEDITABLE_MODE_NONE) {
            dom_node_unref(parent);
            return 0;
        }
        dom_node_unref(parent);
    }
    *out_mode = mode;
    return 1;
}

static int pcore_contenteditable_id_present(dom_node *node)
{
    dom_string *name;
    dom_string *value;
    const char *data;
    size_t length;
    int present;

    if (node == NULL) {
        return 0;
    }
    name = NULL;
    value = NULL;
    if (dom_string_create((const uint8_t *) "id", 2, &name) !=
            DOM_NO_ERR || name == NULL ||
            dom_element_get_attribute((dom_element *) node, name, &value) !=
            DOM_NO_ERR) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    data = (value != NULL) ? dom_string_data(value) : NULL;
    length = (value != NULL) ? dom_string_byte_length(value) : 0;
    present = value != NULL && length > 0 && data != NULL;
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return present;
}

static void pcore_contenteditable_box_geometry(struct box *box, int *x,
        int *y, int *w, int *h)
{
    int ax;
    int ay;

    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (x != NULL) {
        *x = ax - box->border[LEFT].width;
    }
    if (y != NULL) {
        *y = ay - box->border[TOP].width;
    }
    if (w != NULL) {
        *w = box->border[LEFT].width + box->padding[LEFT] + box->width +
                box->padding[RIGHT] + box->border[RIGHT].width;
    }
    if (h != NULL) {
        *h = box->border[TOP].width + box->padding[TOP] + box->height +
                box->padding[BOTTOM] + box->border[BOTTOM].width;
    }
}

/* Walk the styled DOM in document order and copy one bounded editing-host
 * snapshot. Return 1 when the requested target was found, 2 when a caller
 * buffer was too small, 0 when the subtree has no such target, and -1 on a
 * DOM/layout failure. */
static int pcore_contenteditable_target_walk(pcore_render *st,
        dom_node *node, unsigned int depth, unsigned int wanted,
        unsigned int *seen, PCoreContentEditableTargetInfo *out_info,
        char *element_id, int id_capacity, char *text, int text_capacity)
{
    dom_node_type node_type;
    dom_node *child;
    dom_node *next;
    dom_string *id_name;
    dom_string *id_value;
    dom_string *content;
    struct box *box;
    const char *id_data;
    const char *text_data;
    size_t id_length;
    size_t text_length;
    int mode;
    int ax;
    int ay;
    int width;
    int height;
    int status;
    int copy_length;
    int result;

    if (st == NULL || node == NULL || seen == NULL || out_info == NULL ||
            depth > PCORE_FOCUS_WALK_DEPTH_MAX) {
        return -1;
    }
    node_type = DOM_NODE_TYPE_COUNT;
    if (dom_node_get_node_type(node, &node_type) != DOM_NO_ERR) {
        return -1;
    }
    if (node_type == DOM_ELEMENT_NODE &&
            pcore_contenteditable_host_mode(node, &mode) &&
            pcore_contenteditable_id_present(node)) {
        id_name = NULL;
        id_value = NULL;
        content = NULL;
        if (dom_string_create((const uint8_t *) "id", 2, &id_name) !=
                DOM_NO_ERR || id_name == NULL ||
                dom_element_get_attribute((dom_element *) node, id_name,
                &id_value) != DOM_NO_ERR || id_value == NULL ||
                dom_node_get_text_content(node, &content) != DOM_NO_ERR ||
                content == NULL) {
            if (content != NULL) {
                dom_string_unref(content);
            }
            if (id_value != NULL) {
                dom_string_unref(id_value);
            }
            if (id_name != NULL) {
                dom_string_unref(id_name);
            }
            return -1;
        }
        id_data = dom_string_data(id_value);
        text_data = dom_string_data(content);
        id_length = dom_string_byte_length(id_value);
        text_length = dom_string_byte_length(content);
        box = pcore_box_for_any_node(st->root_box, node);
        if ((id_data == NULL && id_length != 0) ||
                (text_data == NULL && text_length != 0) ||
                id_length > (size_t) INT_MAX ||
                text_length > (size_t) INT_MAX ||
                text_length > PCORE_CONTENTEDITABLE_TEXT_MAX_BYTES) {
            dom_string_unref(content);
            dom_string_unref(id_value);
            dom_string_unref(id_name);
            return -1;
        }
        if (box != NULL) {
            pcore_contenteditable_box_geometry(box, &ax, &ay, &width,
                    &height);
            if (width > 0 && height > 0) {
                if (*seen == wanted) {
                    memset(out_info, 0, sizeof(*out_info));
                    out_info->size = sizeof(*out_info);
                    out_info->x = ax;
                    out_info->y = ay;
                    out_info->width = width;
                    out_info->height = height;
                    out_info->mode = mode;
                    out_info->text_bytes = (int) text_length;
                    out_info->id_bytes = (int) id_length;
                    status = 0;
                    if (element_id != NULL && id_capacity > 0) {
                        copy_length = (id_capacity - 1 <
                                (int) id_length) ? id_capacity - 1 :
                                (int) id_length;
                        if (copy_length > 0) {
                            memcpy(element_id, id_data,
                                    (size_t) copy_length);
                        }
                        element_id[copy_length] = '\0';
                        if ((size_t) id_capacity <= id_length) {
                            status = 2;
                        }
                    }
                    if (text != NULL && text_capacity > 0) {
                        copy_length = (text_capacity - 1 <
                                (int) text_length) ? text_capacity - 1 :
                                (int) text_length;
                        if (copy_length > 0) {
                            memcpy(text, text_data, (size_t) copy_length);
                        }
                        text[copy_length] = '\0';
                        if ((size_t) text_capacity <= text_length) {
                            status = 2;
                        }
                    }
                    dom_string_unref(content);
                    dom_string_unref(id_value);
                    dom_string_unref(id_name);
                    return status == 2 ? 2 : 1;
                }
                if (*seen == UINT_MAX) {
                    dom_string_unref(content);
                    dom_string_unref(id_value);
                    dom_string_unref(id_name);
                    return -1;
                }
                *seen += 1;
            }
        }
        dom_string_unref(content);
        dom_string_unref(id_value);
        dom_string_unref(id_name);
    }
    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return -1;
    }
    while (child != NULL) {
        result = pcore_contenteditable_target_walk(st, child, depth + 1U,
                wanted, seen, out_info, element_id, id_capacity, text,
                text_capacity);
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return -1;
        }
        dom_node_unref(child);
        if (result != 0) {
            if (next != NULL) {
                dom_node_unref(next);
            }
            return result;
        }
        child = next;
    }
    return 0;
}

PCORE_API int PCore_ContentEditableTargetInfo(HANDLE hDoc,
        unsigned int index, PCoreContentEditableTargetInfo *out_info,
        char *element_id, int id_capacity, char *text, int text_capacity)
{
    dom_document *doc;
    dom_element *root;
    pcore_render *st;
    unsigned int seen;
    int result;

    if (out_info == NULL || out_info->size < sizeof(*out_info) ||
            hDoc == NULL || index >= PCORE_CONTENTEDITABLE_TARGET_MAX) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    out_info->size = sizeof(*out_info);
    if (element_id != NULL && id_capacity > 0) {
        element_id[0] = '\0';
    }
    if (text != NULL && text_capacity > 0) {
        text[0] = '\0';
    }
    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL) {
        return 1;
    }
    root = NULL;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        if (root != NULL) {
            dom_node_unref((dom_node *) root);
        }
        return 1;
    }
    seen = 0;
    result = pcore_contenteditable_target_walk(st, (dom_node *) root, 0,
            index, &seen, out_info, element_id, id_capacity, text,
            text_capacity);
    dom_node_unref((dom_node *) root);
    return result == 1 || result == 2 ? (result == 2 ? 2 : 0) : 1;
}

static int pcore_focus_form_kind(struct form_control *gadget)
{
    if (gadget == NULL) {
        return 0;
    }
    if (gadget->type == GADGET_CHECKBOX) {
        return 1;
    }
    if (gadget->type == GADGET_RADIO) {
        return 2;
    }
    if (gadget->type == GADGET_TEXTBOX) {
        return 3;
    }
    if (gadget->type == GADGET_PASSWORD) {
        return 4;
    }
    if (gadget->type == GADGET_TEXTAREA) {
        return 5;
    }
    if (gadget->type == GADGET_SELECT) {
        return 6;
    }
    if (gadget->type == GADGET_SUBMIT) {
        return 7;
    }
    if (gadget->type == GADGET_RESET) {
        return 8;
    }
    if (gadget->type == GADGET_BUTTON) {
        return 9;
    }
    if (gadget->type == GADGET_FILE) {
        return 10;
    }
    return 0;
}

static int pcore_focus_generic_target(pcore_render *st, dom_node *node,
        int tabindex_present, int tabindex_valid, int tabindex,
        PCoreFocusTargetInfo *out_info, int *out_tabindex)
{
    struct box *box;
    int ax;
    int ay;

    if (st == NULL || node == NULL || out_info == NULL ||
            !tabindex_present || !tabindex_valid || tabindex < 0) {
        return 0;
    }
    box = pcore_box_for_any_node(st->root_box, node);
    if (box == NULL || box->width <= 0 || box->height <= 0) {
        return 0;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    out_info->x = ax;
    out_info->y = ay;
    out_info->width = box->width;
    out_info->height = box->height;
    out_info->kind = PCORE_FOCUS_TARGET_GENERIC;
    if (out_tabindex != NULL) {
        *out_tabindex = tabindex;
    }
    return 1;
}

static int pcore_focus_target_for_element(pcore_render *st,
        dom_node *node, PCoreFocusTargetInfo *out_info,
        int *out_tabindex)
{
    struct box *box;
    struct form_control *gadget;
    bool effective_disabled;
    dom_string *href;
    dom_element *details;
    int ax;
    int ay;
    int kind;
    int tabindex;
    int tabindex_present;
    int tabindex_valid;
    int contenteditable_mode;

    if (out_tabindex != NULL) {
        *out_tabindex = 0;
    }
    if (st == NULL || node == NULL || out_info == NULL ||
            !pcore_node_attr_tabindex(node, &tabindex, &tabindex_present,
            &tabindex_valid)) {
        return 0;
    }
    memset(out_info, 0, sizeof(*out_info));
    box = NULL;
    gadget = NULL;
    if (pcore_node_name_is(node, "input") ||
            pcore_node_name_is(node, "textarea") ||
            pcore_node_name_is(node, "select") ||
            pcore_node_name_is(node, "button")) {
        box = pcore_box_for_node(st->root_box, node);
        if (box != NULL) {
            gadget = box->gadget;
        }
        kind = pcore_focus_form_kind(gadget);
        if (kind == 0) {
            return pcore_focus_generic_target(st, node, tabindex_present,
                    tabindex_valid, tabindex, out_info, out_tabindex);
        }
        effective_disabled = (gadget != NULL) ? gadget->disabled : false;
        if (kind == 0 || kind == 10 || box == NULL || box->width <= 0 ||
                box->height <= 0 ||
                pcore_node_effectively_disabled(node, NULL,
                &effective_disabled) != 0 || effective_disabled) {
            return 0;
        }
        if (tabindex_present && tabindex_valid && tabindex < 0) {
            return 0;
        }
        ax = 0;
        ay = 0;
        box_coords(box, &ax, &ay);
        out_info->x = ax;
        out_info->y = ay;
        out_info->width = box->width;
        out_info->height = box->height;
        out_info->kind = kind;
        if (out_tabindex != NULL && tabindex_present && tabindex_valid) {
            *out_tabindex = tabindex;
        }
        return 1;
    }
    if (pcore_contenteditable_host_mode(node, &contenteditable_mode) &&
            pcore_contenteditable_id_present(node)) {
        if (tabindex_present && tabindex_valid && tabindex < 0) {
            return 0;
        }
        box = pcore_box_for_any_node(st->root_box, node);
        if (box == NULL || box->width <= 0 || box->height <= 0) {
            return 0;
        }
        pcore_contenteditable_box_geometry(box, &ax, &ay, NULL, NULL);
        out_info->x = ax;
        out_info->y = ay;
        pcore_contenteditable_box_geometry(box, NULL, NULL,
                &out_info->width, &out_info->height);
        out_info->kind = PCORE_FOCUS_TARGET_CONTENTEDITABLE;
        if (out_tabindex != NULL && tabindex_present && tabindex_valid) {
            *out_tabindex = tabindex;
        }
        return 1;
    }
    if (pcore_node_name_is(node, "a")) {
        href = NULL;
        if (pcore_ensure_link_strings() == 0 &&
                dom_element_get_attribute((dom_element *) node,
                pcore_href_name, &href) == DOM_NO_ERR && href != NULL &&
                dom_string_byte_length(href) != 0) {
            if (!(tabindex_present && tabindex_valid && tabindex < 0)) {
                box = pcore_box_for_any_node(st->root_box, node);
                if (box != NULL && box->width > 0 && box->height > 0) {
                    ax = 0;
                    ay = 0;
                    box_coords(box, &ax, &ay);
                    out_info->x = ax;
                    out_info->y = ay;
                    out_info->width = box->width;
                    out_info->height = box->height;
                    out_info->kind = PCORE_FOCUS_TARGET_LINK;
                    if (out_tabindex != NULL && tabindex_present &&
                            tabindex_valid) {
                        *out_tabindex = tabindex;
                    }
                    dom_string_unref(href);
                    return 1;
                }
            }
        }
        if (href != NULL) {
            dom_string_unref(href);
        }
        return pcore_focus_generic_target(st, node, tabindex_present,
                tabindex_valid, tabindex, out_info, out_tabindex);
    }
    if (pcore_node_name_is(node, "summary")) {
        if (tabindex_present && tabindex_valid && tabindex < 0) {
            return 0;
        }
        details = NULL;
        box = pcore_box_for_any_node(st->root_box, node);
        if (box != NULL && pcore_disclosure_summary_box_info(st, node, box,
                &out_info->x, &out_info->y, &out_info->width,
                &out_info->height, NULL, &details)) {
            if (details != NULL) {
                dom_node_unref((dom_node *) details);
            }
            out_info->kind = PCORE_FOCUS_TARGET_DISCLOSURE;
            if (out_tabindex != NULL && tabindex_present && tabindex_valid) {
                *out_tabindex = tabindex;
            }
            return 1;
        }
        if (details != NULL) {
            dom_node_unref((dom_node *) details);
        }
        return pcore_focus_generic_target(st, node, tabindex_present,
                tabindex_valid, tabindex, out_info, out_tabindex);
    }
    return pcore_focus_generic_target(st, node, tabindex_present,
            tabindex_valid, tabindex, out_info, out_tabindex);
}

/* Find the first eligible autofocus target in DOM order. The retained box
 * tree is private to Core, so the caller receives a referenced DOM node only
 * inside this DLL and copies any public data before releasing it. A deep or
 * otherwise unreadable subtree fails closed instead of silently selecting a
 * later target. */
static int pcore_autofocus_target_walk(pcore_render *st, dom_node *node,
        unsigned int depth, dom_node **out_node,
        PCoreFocusTargetInfo *out_info)
{
    dom_node_type node_type;
    dom_node *child;
    dom_node *next;
    PCoreFocusTargetInfo candidate;
    int result;

    if (st == NULL || node == NULL || out_node == NULL || out_info == NULL) {
        return -1;
    }
    if (depth > PCORE_FOCUS_WALK_DEPTH_MAX) {
        return -1;
    }
    node_type = DOM_NODE_TYPE_COUNT;
    if (dom_node_get_node_type(node, &node_type) != DOM_NO_ERR) {
        return -1;
    }
    if (node_type == DOM_ELEMENT_NODE &&
            pcore_node_has_attr(node, "autofocus") &&
            pcore_focus_target_for_element(st, node, &candidate, NULL)) {
        *out_node = dom_node_ref(node);
        if (*out_node == NULL) {
            return -1;
        }
        *out_info = candidate;
        return 1;
    }
    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return -1;
    }
    while (child != NULL) {
        result = pcore_autofocus_target_walk(st, child, depth + 1U,
                out_node, out_info);
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return -1;
        }
        dom_node_unref(child);
        if (result != 0) {
            if (next != NULL) {
                dom_node_unref(next);
            }
            return result;
        }
        child = next;
    }
    return 0;
}

/* Copy the optional UTF-8 id of an autofocus node. An empty or absent id is
 * a successful zero-byte result, while an undersized caller buffer is
 * reported without a partial copy. */
static int pcore_autofocus_copy_id(dom_node *node, char *element_id,
        int id_capacity, int *out_bytes)
{
    dom_string *name;
    dom_string *value;
    const char *data;
    size_t length;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (element_id != NULL && id_capacity > 0) {
        element_id[0] = '\0';
    }
    if (node == NULL || id_capacity < 0) {
        return 1;
    }
    name = NULL;
    value = NULL;
    if (dom_string_create((const uint8_t *) "id", 2, &name) !=
            DOM_NO_ERR || name == NULL ||
            dom_element_get_attribute((dom_element *) node, name, &value) !=
            DOM_NO_ERR) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 1;
    }
    data = (value != NULL) ? dom_string_data(value) : NULL;
    length = (value != NULL) ? dom_string_byte_length(value) : 0;
    if ((data == NULL && length != 0) || length > (size_t) INT_MAX) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        dom_string_unref(name);
        return 1;
    }
    if (out_bytes != NULL) {
        *out_bytes = (int) length;
    }
    if (length == 0) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        dom_string_unref(name);
        return 0;
    }
    if (element_id == NULL || id_capacity == 0) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        dom_string_unref(name);
        return 0;
    }
    if (length >= (size_t) id_capacity) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        dom_string_unref(name);
        return 2;
    }
    memcpy(element_id, data, length);
    element_id[length] = '\0';
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return 0;
}

static int pcore_autofocus_target_find(dom_document *doc,
        pcore_render *st, dom_node **out_node,
        PCoreFocusTargetInfo *out_info)
{
    dom_element *root;
    int result;

    if (out_node != NULL) {
        *out_node = NULL;
    }
    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (doc == NULL || st == NULL || out_node == NULL ||
            out_info == NULL) {
        return -1;
    }
    root = NULL;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        if (root != NULL) {
            dom_node_unref((dom_node *) root);
        }
        return -1;
    }
    result = pcore_autofocus_target_walk(st, (dom_node *) root, 0,
            out_node, out_info);
    dom_node_unref((dom_node *) root);
    return result;
}

PCORE_API int PCore_AutofocusTargetInfo(HANDLE hDoc,
        PCoreFocusTargetInfo *out_info, char *element_id, int id_capacity,
        int *out_bytes)
{
    dom_document *doc;
    pcore_render *st;
    dom_node *target;
    PCoreFocusTargetInfo info;
    int result;
    int id_result;

    if (out_info == NULL || id_capacity < 0) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (element_id != NULL && id_capacity > 0) {
        element_id[0] = '\0';
    }
    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (doc == NULL || st == NULL) {
        return 1;
    }
    target = NULL;
    memset(&info, 0, sizeof(info));
    result = pcore_autofocus_target_find(doc, st, &target, &info);
    if (result != 1 || target == NULL) {
        if (target != NULL) {
            dom_node_unref(target);
        }
        return 1;
    }
    *out_info = info;
    id_result = pcore_autofocus_copy_id(target, element_id, id_capacity,
            out_bytes);
    dom_node_unref(target);
    return id_result == 2 ? 2 : (id_result == 0 ? 0 : 1);
}

PCORE_API int PCore_InteractionFocusAutofocus(HANDLE hDoc)
{
    dom_document *doc;
    pcore_render *st;
    dom_node *target;
    PCoreFocusTargetInfo info;
    int result;

    doc = (dom_document *) hDoc;
    if (doc == NULL) {
        return -1;
    }
    st = pcore_get_render(doc);
    if (st == NULL) {
        return 0;
    }
    target = NULL;
    memset(&info, 0, sizeof(info));
    result = pcore_autofocus_target_find(doc, st, &target, &info);
    if (result < 0) {
        return -1;
    }
    if (result == 0 || target == NULL) {
        return 0;
    }
    result = pcore_interaction_set_node(doc, PCORE_INTERACTION_FOCUS,
            target);
    dom_node_unref(target);
    return result < 0 ? -1 : result;
}

/* Collect a bounded natural-order snapshot before sorting the positive
 * tabindex group. The stable insertion sort below preserves document order
 * for equal values and for the default/zero group. The candidate array lives
 * on the heap so a page with many focus targets cannot consume the small
 * device thread stack. */
#define PCORE_FOCUS_TARGET_MAX 256U

typedef struct pcore_focus_candidate {
    PCoreFocusTargetInfo info;
    int tabindex;
} pcore_focus_candidate;

static void pcore_focus_target_collect(pcore_render *st, dom_node *node,
        pcore_focus_candidate *items, unsigned int *count, int *overflow,
        int *failed, unsigned int depth)
{
    dom_node_type node_type;
    dom_node *child;
    dom_node *next;
    PCoreFocusTargetInfo candidate;
    int candidate_tabindex;

    if (st == NULL || node == NULL || items == NULL || count == NULL ||
            overflow == NULL || failed == NULL || *overflow || *failed ||
            depth > PCORE_FOCUS_WALK_DEPTH_MAX) {
        return;
    }
    node_type = DOM_NODE_TYPE_COUNT;
    if (dom_node_get_node_type(node, &node_type) != DOM_NO_ERR) {
        *failed = 1;
        return;
    }
    if (node_type == DOM_ELEMENT_NODE &&
            pcore_focus_target_for_element(st, node, &candidate,
            &candidate_tabindex)) {
        if (*count >= PCORE_FOCUS_TARGET_MAX) {
            *overflow = 1;
            return;
        }
        items[*count].info = candidate;
        items[*count].tabindex = candidate_tabindex;
        *count += 1;
    }
    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        *failed = 1;
        return;
    }
    while (child != NULL) {
        pcore_focus_target_collect(st, child, items, count, overflow,
                failed, depth + 1U);
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            *failed = 1;
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

static int pcore_focus_candidate_after(const pcore_focus_candidate *left,
        const pcore_focus_candidate *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
    if (left->tabindex <= 0) {
        return right->tabindex > 0;
    }
    if (right->tabindex <= 0) {
        return 0;
    }
    return left->tabindex > right->tabindex;
}

static void pcore_focus_target_sort(pcore_focus_candidate *items,
        unsigned int count)
{
    pcore_focus_candidate key;
    unsigned int i;
    unsigned int j;

    if (items == NULL) {
        return;
    }
    for (i = 1; i < count; i++) {
        key = items[i];
        j = i;
        while (j > 0 && pcore_focus_candidate_after(&items[j - 1],
                &key)) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }
}

static int pcore_focus_target_info_from_root(pcore_render *st,
        dom_node *root, unsigned int index, PCoreFocusTargetInfo *out_info)
{
    pcore_focus_candidate *candidates;
    unsigned int count;
    int overflow;
    int failed;

    if (st == NULL || root == NULL || out_info == NULL) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    candidates = (pcore_focus_candidate *) malloc(
            sizeof(*candidates) * PCORE_FOCUS_TARGET_MAX);
    if (candidates == NULL) {
        return 1;
    }
    count = 0;
    overflow = 0;
    failed = 0;
    pcore_focus_target_collect(st, root, candidates, &count, &overflow,
            &failed, 0);
    if (failed || overflow) {
        free(candidates);
        return 1;
    }
    pcore_focus_target_sort(candidates, count);
    if (index >= count) {
        free(candidates);
        return 1;
    }
    *out_info = candidates[index].info;
    free(candidates);
    return 0;
}

PCORE_API int PCore_FocusTargetInfo(HANDLE hDoc, unsigned int index,
        PCoreFocusTargetInfo *out_info)
{
    dom_document *doc;
    dom_element *root;
    pcore_render *st;
    int result;

    if (out_info == NULL) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || doc == NULL) {
        return 1;
    }
    root = NULL;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        if (root != NULL) {
            dom_node_unref((dom_node *) root);
        }
        return 1;
    }
    result = pcore_focus_target_info_from_root(st, (dom_node *) root,
            index, out_info);
    dom_node_unref((dom_node *) root);
    return result;
}

PCORE_API int PCore_FocusTargetInfoWithin(HANDLE hDoc,
        const char *ancestor_id, unsigned int index,
        PCoreFocusTargetInfo *out_info)
{
    dom_document *doc;
    dom_string *id;
    dom_element *ancestor;
    pcore_render *st;
    int result;

    if (out_info == NULL) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    doc = (dom_document *) hDoc;
    if (doc == NULL || ancestor_id == NULL || ancestor_id[0] == '\0') {
        return 1;
    }
    st = pcore_get_render(doc);
    if (st == NULL) {
        return 1;
    }
    id = NULL;
    ancestor = NULL;
    if (dom_string_create((const uint8_t *) ancestor_id,
            strlen(ancestor_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &ancestor) !=
            DOM_NO_ERR || ancestor == NULL) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (ancestor != NULL) {
            dom_node_unref((dom_node *) ancestor);
        }
        return 1;
    }
    dom_string_unref(id);
    result = pcore_focus_target_info_from_root(st,
            (dom_node *) ancestor, index, out_info);
    dom_node_unref((dom_node *) ancestor);
    return result;
}

PCORE_API int PCore_FocusTargetInfoById(HANDLE hDoc, const char *element_id,
        PCoreFocusTargetInfo *out_info)
{
    dom_document *doc;
    pcore_render *st;
    dom_element *element;
    int result;

    if (out_info == NULL) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    doc = (dom_document *) hDoc;
    if (doc == NULL || element_id == NULL || element_id[0] == '\0') {
        return 1;
    }
    st = pcore_get_render(doc);
    if (st == NULL) {
        return 1;
    }
    element = pcore_box_element_by_id(doc, element_id);
    if (element == NULL) {
        return 1;
    }
    result = pcore_focus_target_for_element(st, (dom_node *) element,
            out_info, NULL) ? 0 : 1;
    dom_node_unref((dom_node *) element);
    return result;
}

PCORE_API int PCore_InteractionFocusById(HANDLE hDoc,
        const char *element_id)
{
    dom_document *doc;
    pcore_render *st;
    dom_element *element;
    PCoreFocusTargetInfo target;
    int result;

    doc = (dom_document *) hDoc;
    if (doc == NULL || element_id == NULL || element_id[0] == '\0') {
        return -1;
    }
    st = pcore_get_render(doc);
    if (st == NULL) {
        return 0;
    }
    element = pcore_box_element_by_id(doc, element_id);
    if (element == NULL) {
        return 0;
    }
    if (!pcore_focus_target_for_element(st, (dom_node *) element,
            &target, NULL)) {
        dom_node_unref((dom_node *) element);
        return 0;
    }
    result = pcore_interaction_set_node(doc, PCORE_INTERACTION_FOCUS,
            (dom_node *) element);
    dom_node_unref((dom_node *) element);
    return result < 0 ? -1 : result;
}

static struct box *pcore_file_input_at_index(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->gadget != NULL &&
            box->gadget->type == GADGET_FILE) {
        if (*current == target) {
            return box;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_file_input_at_index(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static int pcore_file_input_box_index(struct box *box,
        struct box *target, unsigned int *current,
        unsigned int *result)
{
    struct box *child;

    if (box == NULL) {
        return 0;
    }
    if (box->gadget != NULL &&
            box->gadget->type == GADGET_FILE) {
        if (box == target) {
            *result = *current;
            return 1;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        if (pcore_file_input_box_index(child, target, current, result)) {
            return 1;
        }
    }
    return 0;
}

static char *pcore_heap_string(const char *value)
{
    char *copy;
    size_t length;

    value = (value != NULL) ? value : "";
    length = strlen(value);
    copy = (char *) malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static void pcore_file_path_handler(dom_node_operation operation,
        dom_string *key, void *data, dom_node *source, dom_node *target)
{
    char *old_path;
    char *copy;

    (void) source;
    if (data == NULL ||
            corestring_dom___ns_key_file_name_node_data == NULL ||
            !dom_string_isequal(
                    corestring_dom___ns_key_file_name_node_data, key)) {
        return;
    }
    if (operation == DOM_NODE_CLONED) {
        copy = pcore_heap_string((const char *) data);
        if (copy == NULL) {
            return;
        }
        old_path = NULL;
        if (dom_node_set_user_data(target,
                corestring_dom___ns_key_file_name_node_data,
                copy, pcore_file_path_handler,
                (void **) &old_path) != DOM_NO_ERR) {
            free(copy);
        } else {
            free(old_path);
        }
    } else if (operation == DOM_NODE_DELETED) {
        free(data);
    }
}

PCORE_API int PCore_FileInputAt(HANDLE hDoc, int x, int y,
        unsigned int *file_index, int *disabled)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    unsigned int current;
    unsigned int index;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* Resolve text descendants to the owning file gadget. */
    }
    if (box == NULL || box->gadget == NULL ||
            box->gadget->type != GADGET_FILE) {
        return 0;
    }
    current = 0;
    index = 0;
    if (!pcore_file_input_box_index(st->root_box, box, &current, &index)) {
        return 0;
    }
    if (file_index != NULL) {
        *file_index = index;
    }
    if (disabled != NULL) {
        effective_disabled = box->gadget->disabled;
        if (pcore_node_effectively_disabled(box->gadget->node, NULL,
                &effective_disabled) != 0) {
            return 0;
        }
        *disabled = effective_disabled ? 1 : 0;
    }
    return 1;
}

PCORE_API int PCore_FileInputInfo(HANDLE hDoc, unsigned int file_index,
        PCoreFileInputInfo *out_info, char *value, int value_capacity,
        char *path, int path_capacity)
{
    pcore_render *st;
    struct box *box;
    const char *display_value;
    const char *raw_path;
    unsigned int current;
    size_t value_length;
    size_t path_length;
    int ax;
    int ay;
    void *stored_path;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_file_input_at_index(st->root_box, file_index,
                    &current) : NULL;
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    display_value = (box->gadget->value != NULL) ?
            box->gadget->value : "";
    stored_path = NULL;
    if (corestring_dom___ns_key_file_name_node_data == NULL ||
            dom_node_get_user_data(box->gadget->node,
                    corestring_dom___ns_key_file_name_node_data,
                    &stored_path) != DOM_NO_ERR) {
        return 1;
    }
    raw_path = (stored_path != NULL) ?
            (const char *) stored_path : "";
    value_length = strlen(display_value);
    path_length = strlen(raw_path);
    if (value_length > INT_MAX || path_length > INT_MAX) {
        return 1;
    }
    if (out_info != NULL) {
        ax = 0;
        ay = 0;
        box_coords(box, &ax, &ay);
        out_info->x = ax;
        out_info->y = ay;
        out_info->width = box->width;
        out_info->height = box->height;
        effective_disabled = box->gadget->disabled;
        if (pcore_node_effectively_disabled(box->gadget->node, NULL,
                &effective_disabled) != 0) {
            return 1;
        }
        out_info->disabled = effective_disabled ? 1 : 0;
        out_info->value_bytes = (int) value_length;
        out_info->path_bytes = (int) path_length;
    }
    if (value != NULL && value_capacity > 0) {
        if ((size_t) value_capacity <= value_length) {
            value[0] = '\0';
            return 2;
        }
        memcpy(value, display_value, value_length + 1);
    }
    if (path != NULL && path_capacity > 0) {
        if ((size_t) path_capacity <= path_length) {
            path[0] = '\0';
            return 2;
        }
        memcpy(path, raw_path, path_length + 1);
    }
    return 0;
}

PCORE_API int PCore_FileInputSetPath(HANDLE hDoc,
        unsigned int file_index, const char *value, const char *path)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_string *dom_value;
    char *gadget_value;
    char *stored_path;
    char *old_path;
    unsigned int current;
    bool effective_disabled;

    if (value == NULL || path == NULL) {
        return 1;
    }
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_file_input_at_index(st->root_box, file_index,
                    &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL) {
        return 1;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 1;
    }
    gadget_value = talloc_strdup(control, value);
    stored_path = pcore_heap_string(path);
    dom_value = NULL;
    if (gadget_value == NULL || stored_path == NULL ||
            dom_string_create((const uint8_t *) value, strlen(value),
                    &dom_value) != DOM_NO_ERR) {
        if (gadget_value != NULL) {
            talloc_free(gadget_value);
        }
        free(stored_path);
        return 1;
    }
    if (dom_html_input_element_set_value(
            (dom_html_input_element *) control->node,
            dom_value) != DOM_NO_ERR) {
        dom_string_unref(dom_value);
        talloc_free(gadget_value);
        free(stored_path);
        return 1;
    }
    dom_string_unref(dom_value);
    old_path = NULL;
    if (corestring_dom___ns_key_file_name_node_data == NULL ||
            dom_node_set_user_data(control->node,
                    corestring_dom___ns_key_file_name_node_data,
                    stored_path, pcore_file_path_handler,
                    (void **) &old_path) != DOM_NO_ERR) {
        talloc_free(gadget_value);
        free(stored_path);
        return 1;
    }
    free(old_path);
    if (control->value != NULL) {
        talloc_free(control->value);
    }
    control->value = gadget_value;
    control->length = (unsigned int) strlen(gadget_value);
    return 0;
}

static struct box *pcore_text_input_at(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->gadget != NULL &&
            (box->gadget->type == GADGET_TEXTBOX ||
             box->gadget->type == GADGET_PASSWORD ||
             box->gadget->type == GADGET_TEXTAREA)) {
        if (*current == target) {
            return box;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_text_input_at(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static int pcore_utf8_character_count(const char *text,
        unsigned int *out_count)
{
    const unsigned char *p;
    unsigned int count;
    unsigned int cp;
    int need;
    int i;

    if (text == NULL || out_count == NULL) {
        return 1;
    }
    p = (const unsigned char *) text;
    count = 0;
    while (*p != 0) {
        if (*p < 0x80) {
            p++;
            count++;
            continue;
        }
        if (*p >= 0xc2 && *p <= 0xdf) {
            need = 1;
            cp = *p & 0x1f;
        } else if (*p >= 0xe0 && *p <= 0xef) {
            need = 2;
            cp = *p & 0x0f;
        } else if (*p >= 0xf0 && *p <= 0xf4) {
            need = 3;
            cp = *p & 0x07;
        } else {
            return 1;
        }
        for (i = 1; i <= need; i++) {
            if ((p[i] & 0xc0) != 0x80) {
                return 1;
            }
            cp = (cp << 6) | (p[i] & 0x3f);
        }
        if ((need == 2 && cp < 0x800) ||
                (need == 3 && cp < 0x10000) ||
                (cp >= 0xd800 && cp <= 0xdfff) ||
                cp > 0x10ffff) {
            return 1;
        }
        p += need + 1;
        count++;
    }
    *out_count = count;
    return 0;
}

PCORE_API int PCore_TextInputInfo(HANDLE hDoc, unsigned int index,
        PCoreTextInputInfo *out_info, char *value, int cap)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_html_input_element *input;
    dom_html_text_area_element *textarea;
    unsigned int current;
    bool read_only;
    bool effective_disabled;
    int ax;
    int ay;
    size_t value_len;
    size_t copy_len;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, index, &current) : NULL;
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    control = box->gadget;
    read_only = false;
    if (control->type == GADGET_TEXTAREA) {
        textarea = (dom_html_text_area_element *) control->node;
        if (dom_html_text_area_element_get_read_only(textarea,
                &read_only) != DOM_NO_ERR) {
            read_only = pcore_node_has_attr(control->node, "readonly") ?
                    true : false;
        }
    } else {
        input = (dom_html_input_element *) control->node;
        if (dom_html_input_element_get_read_only(input, &read_only) !=
                DOM_NO_ERR) {
            read_only = pcore_node_has_attr(control->node, "readonly") ?
                    true : false;
        }
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (out_info != NULL) {
        out_info->x = ax - box->border[LEFT].width;
        out_info->y = ay - box->border[TOP].width;
        out_info->width = box->border[LEFT].width +
                box->padding[LEFT] + box->width + box->padding[RIGHT] +
                box->border[RIGHT].width;
        out_info->height = box->border[TOP].width +
                box->padding[TOP] + box->height + box->padding[BOTTOM] +
                box->border[BOTTOM].width;
        out_info->password =
                (control->type == GADGET_PASSWORD) ? 1 : 0;
        out_info->read_only = read_only ? 1 : 0;
        effective_disabled = control->disabled;
        if (pcore_node_effectively_disabled(control->node, NULL,
                &effective_disabled) != 0) {
            return 1;
        }
        out_info->disabled = effective_disabled ? 1 : 0;
        out_info->max_length =
                (control->maxlength == UINT_MAX ||
                 control->maxlength > (unsigned int) INT_MAX) ?
                -1 : (int) control->maxlength;
        out_info->value_bytes = (control->value != NULL) ?
                (int) strlen(control->value) : 0;
    }
    if (value != NULL && cap > 0) {
        value_len = (control->value != NULL) ?
                strlen(control->value) : 0;
        copy_len = value_len;
        if (copy_len > (size_t) (cap - 1)) {
            copy_len = (size_t) (cap - 1);
        }
        if (copy_len > 0) {
            memcpy(value, control->value, copy_len);
        }
        value[copy_len] = '\0';
    }
    return 0;
}

PCORE_API int PCore_TextInputIsMultiline(HANDLE hDoc,
        unsigned int index, int *multiline)
{
    pcore_render *st;
    struct box *box;
    unsigned int current;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, index, &current) : NULL;
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    if (multiline != NULL) {
        *multiline =
                (box->gadget->type == GADGET_TEXTAREA) ? 1 : 0;
    }
    return 0;
}

/* libdom 0.4.2 implements input.value by mutating the value attribute. When
 * markup omitted that attribute, the first mutation also initialises its
 * defaultValue slot to the new text. Freeze the pre-edit value first so form
 * reset restores the parsed default rather than the first user edit. */
static int pcore_input_preserve_default(dom_html_input_element *input)
{
    dom_string *default_value;
    dom_string *current_value;
    dom_string *empty;
    dom_exception error;

    default_value = NULL;
    current_value = NULL;
    empty = NULL;
    if (dom_html_input_element_get_default_value(input,
            &default_value) != DOM_NO_ERR) {
        return 0;
    }
    if (default_value != NULL) {
        dom_string_unref(default_value);
        return 1;
    }
    if (dom_html_input_element_get_value(input, &current_value) !=
            DOM_NO_ERR) {
        return 0;
    }
    if (current_value == NULL &&
            dom_string_create((const uint8_t *) "", 0, &empty) !=
                    DOM_NO_ERR) {
        return 0;
    }
    error = dom_html_input_element_set_default_value(input,
            (current_value != NULL) ? current_value : empty);
    if (current_value != NULL) {
        dom_string_unref(current_value);
    }
    if (empty != NULL) {
        dom_string_unref(empty);
    }
    return (error == DOM_NO_ERR) ? 1 : 0;
}

PCORE_API int PCore_TextInputSetValue(HANDLE hDoc, unsigned int index,
        const char *value)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_html_input_element *input;
    dom_html_text_area_element *textarea;
    dom_string *dom_value;
    char *copy;
    char *synced;
    unsigned int current;
    unsigned int characters;
    bool read_only;
    bool effective_disabled;
    size_t source_index;
    size_t target_index;
    size_t value_len;

    if (value == NULL ||
            pcore_utf8_character_count(value, &characters) != 0) {
        return 3;
    }
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, index, &current) : NULL;
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    control = box->gadget;
    read_only = false;
    if (control->type == GADGET_TEXTAREA) {
        textarea = (dom_html_text_area_element *) control->node;
        if (dom_html_text_area_element_get_read_only(textarea,
                &read_only) != DOM_NO_ERR) {
            read_only = pcore_node_has_attr(control->node, "readonly") ?
                    true : false;
        }
    } else {
        input = (dom_html_input_element *) control->node;
        if (dom_html_input_element_get_read_only(input, &read_only) !=
                DOM_NO_ERR) {
            read_only = pcore_node_has_attr(control->node, "readonly") ?
                    true : false;
        }
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0) {
        return 1;
    }
    if (effective_disabled || read_only) {
        return 2;
    }
    if (control->maxlength != UINT_MAX &&
            characters > control->maxlength) {
        return 3;
    }
    value_len = strlen(value);
    if (control->type == GADGET_TEXTAREA) {
        copy = (char *) talloc_size(control, value_len + 1);
        if (copy != NULL) {
            source_index = 0;
            target_index = 0;
            while (source_index < value_len) {
                if (value[source_index] == '\r') {
                    copy[target_index++] = '\n';
                    source_index++;
                    if (source_index < value_len &&
                            value[source_index] == '\n') {
                        source_index++;
                    }
                } else {
                    copy[target_index++] = value[source_index++];
                }
            }
            copy[target_index] = '\0';
            value_len = target_index;
        }
    } else {
        copy = talloc_strdup(control, value);
    }
    synced = (copy != NULL) ? talloc_strdup(control, copy) : NULL;
    if (copy == NULL || synced == NULL) {
        if (copy != NULL) { talloc_free(copy); }
        if (synced != NULL) { talloc_free(synced); }
        return 1;
    }
    dom_value = NULL;
    if (dom_string_create((const uint8_t *) copy, value_len,
            &dom_value) != DOM_NO_ERR) {
        if (dom_value != NULL) { dom_string_unref(dom_value); }
        talloc_free(copy);
        talloc_free(synced);
        return 1;
    }
    if (control->type == GADGET_TEXTAREA) {
        textarea = (dom_html_text_area_element *) control->node;
        if (dom_html_text_area_element_set_value(textarea, dom_value) !=
                DOM_NO_ERR) {
            dom_string_unref(dom_value);
            talloc_free(copy);
            talloc_free(synced);
            return 1;
        }
    } else {
        input = (dom_html_input_element *) control->node;
        if (!pcore_input_preserve_default(input) ||
                dom_html_input_element_set_value(input, dom_value) !=
                DOM_NO_ERR) {
            dom_string_unref(dom_value);
            talloc_free(copy);
            talloc_free(synced);
            return 1;
        }
    }
    dom_string_unref(dom_value);
    if (control->value != NULL) {
        talloc_free(control->value);
    }
    if (control->last_synced_value != NULL) {
        talloc_free(control->last_synced_value);
    }
    control->value = copy;
    control->last_synced_value = synced;
    control->length = (unsigned int) value_len;
    return 0;
}

static struct box *pcore_select_control_at(struct box *box,
        unsigned int target, unsigned int *current)
{
    struct box *child;
    struct box *found;

    if (box == NULL) {
        return NULL;
    }
    if (box->gadget != NULL &&
            box->gadget->type == GADGET_SELECT) {
        if (*current == target) {
            return box;
        }
        *current += 1;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_select_control_at(child, target, current);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static struct form_option *pcore_select_option_at(
        struct form_control *control, unsigned int index)
{
    struct form_option *option;

    if (control == NULL || control->type != GADGET_SELECT) {
        return NULL;
    }
    option = control->data.select.items;
    while (option != NULL && index > 0) {
        option = option->next;
        index--;
    }
    return option;
}

static void pcore_copy_public_text(const char *source, char *target, int cap)
{
    size_t length;

    if (target == NULL || cap <= 0) {
        return;
    }
    length = (source != NULL) ? strlen(source) : 0;
    if (length > (size_t) (cap - 1)) {
        length = (size_t) (cap - 1);
    }
    if (length > 0) {
        memcpy(target, source, length);
    }
    target[length] = '\0';
}

PCORE_API int PCore_SelectInfo(HANDLE hDoc, unsigned int index,
        PCoreSelectInfo *out_info)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    unsigned int current;
    int ax;
    int ay;
    int selected_index;
    int option_index;
    struct form_option *option;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_select_control_at(st->root_box, index, &current) : NULL;
    if (box == NULL || box->gadget == NULL) {
        return 1;
    }
    if (out_info == NULL) {
        return 0;
    }
    control = box->gadget;
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    selected_index = -1;
    option_index = 0;
    if (control->data.select.num_selected == 1) {
        for (option = control->data.select.items; option != NULL;
                option = option->next, option_index++) {
            if (option->selected) {
                selected_index = option_index;
                break;
            }
        }
    }
    out_info->x = ax - box->border[LEFT].width;
    out_info->y = ay - box->border[TOP].width;
    out_info->width = box->border[LEFT].width +
            box->padding[LEFT] + box->width + box->padding[RIGHT] +
            box->border[RIGHT].width;
    out_info->height = box->border[TOP].width +
            box->padding[TOP] + box->height + box->padding[BOTTOM] +
            box->border[BOTTOM].width;
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0) {
        return 1;
    }
    out_info->disabled = effective_disabled ? 1 : 0;
    out_info->multiple = control->data.select.multiple ? 1 : 0;
    out_info->option_count = control->data.select.num_items;
    out_info->selected_count = control->data.select.num_selected;
    out_info->selected_index = selected_index;
    return 0;
}

PCORE_API int PCore_SelectOptionInfo(HANDLE hDoc,
        unsigned int select_index, unsigned int option_index,
        char *label, int label_cap, char *value, int value_cap,
        int *selected, int *disabled, int *label_bytes, int *value_bytes)
{
    pcore_render *st;
    struct box *box;
    struct form_option *option;
    unsigned int current;
    bool option_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ? pcore_select_control_at(st->root_box,
            select_index, &current) : NULL;
    option = (box != NULL) ?
            pcore_select_option_at(box->gadget, option_index) : NULL;
    if (option == NULL) {
        return 1;
    }
    pcore_copy_public_text(option->text, label, label_cap);
    pcore_copy_public_text(option->value, value, value_cap);
    if (selected != NULL) {
        *selected = option->selected ? 1 : 0;
    }
    if (label_bytes != NULL) {
        *label_bytes = (option->text != NULL) ?
                (int) strlen(option->text) : 0;
    }
    if (value_bytes != NULL) {
        *value_bytes = (option->value != NULL) ?
                (int) strlen(option->value) : 0;
    }
    if (pcore_node_effectively_disabled(option->node, NULL,
            &option_disabled) != 0) {
        return 1;
    }
    if (disabled != NULL) {
        *disabled = option_disabled ? 1 : 0;
    }
    return 0;
}

PCORE_API int PCore_SelectSetOptionSelected(HANDLE hDoc,
        unsigned int select_index, unsigned int option_index, int selected)
{
    pcore_render *st;
    struct box *box;
    struct box *inline_box;
    struct form_control *control;
    struct form_option *option;
    struct form_option *target;
    unsigned int current;
    bool option_disabled;
    bool effective_disabled;
    const char *display_text;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ? pcore_select_control_at(st->root_box,
            select_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    target = pcore_select_option_at(control, option_index);
    if (target == NULL) {
        return 1;
    }
    if (pcore_node_effectively_disabled(target->node, NULL,
            &option_disabled) != 0) {
        return 1;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0) {
        return 1;
    }
    if (effective_disabled || option_disabled) {
        return 2;
    }
    if (!control->data.select.multiple && selected) {
        for (option = control->data.select.items; option != NULL;
                option = option->next) {
            option->selected = (option == target) ? true : false;
            dom_html_option_element_set_selected(
                    (dom_html_option_element *) option->node,
                    option->selected);
        }
    } else {
        target->selected = selected ? true : false;
        dom_html_option_element_set_selected(
                (dom_html_option_element *) target->node,
                target->selected);
    }
    control->data.select.num_selected = 0;
    control->data.select.current = NULL;
    for (option = control->data.select.items; option != NULL;
            option = option->next) {
        if (option->selected) {
            control->data.select.num_selected++;
            if (control->data.select.current == NULL) {
                control->data.select.current = option;
            }
        }
    }
    inline_box = (box->children != NULL) ?
            box->children->children : NULL;
    if (inline_box != NULL) {
        display_text = (control->data.select.current != NULL) ?
                control->data.select.current->text : "";
        if (inline_box->text != NULL) {
            talloc_free(inline_box->text);
        }
        inline_box->text = talloc_strdup(inline_box, display_text);
        if (inline_box->text == NULL) {
            inline_box->length = 0;
            return 1;
        }
        inline_box->length = strlen(inline_box->text);
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

/* Image-map hit testing is intentionally kept small and deterministic.  The
 * map/area DOM nodes do not receive layout boxes of their own, so the image
 * box supplies the rendered coordinate system and these helpers project the
 * bounded HTML shape grammar into it. */
typedef struct pcore_image_map_region {
    int kind;       /* 0=default, 1=rect, 2=circle, 3=poly */
    int count;
    int coords[PCORE_IMAGE_MAP_MAX_COORDS];
    int x;
    int y;
    int w;
    int h;
    int center_x;
    int center_y;
    int radius_x;
    int radius_y;
} pcore_image_map_region;

static int pcore_image_map_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static char pcore_image_map_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char) (c + ('a' - 'A'));
    }
    return c;
}

/* Compare an ASCII token after trimming HTML whitespace.  Shape keywords and
 * usemap names are deliberately limited to this bounded comparison; non-ASCII
 * values simply fail closed instead of being silently normalised. */
static int pcore_image_map_token_is(dom_string *value, const char *wanted,
        int caseless)
{
    const char *data;
    size_t length;
    size_t start;
    size_t end;
    size_t wanted_length;
    size_t index;

    if (value == NULL || wanted == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (data == NULL) {
        return 0;
    }
    start = 0;
    while (start < length && pcore_image_map_ascii_space(data[start])) {
        start++;
    }
    end = length;
    while (end > start && pcore_image_map_ascii_space(data[end - 1])) {
        end--;
    }
    wanted_length = strlen(wanted);
    if (end - start != wanted_length) {
        return 0;
    }
    for (index = 0; index < wanted_length; index++) {
        char left = data[start + index];
        char right = wanted[index];
        if (caseless) {
            left = pcore_image_map_ascii_lower(left);
            right = pcore_image_map_ascii_lower(right);
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

static int pcore_image_map_copy_token(dom_string *value, char *out,
        int capacity)
{
    const char *data;
    size_t length;
    size_t start;
    size_t end;
    size_t copy_length;

    if (out == NULL || capacity <= 0) {
        return 0;
    }
    out[0] = '\0';
    if (value == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (data == NULL) {
        return 0;
    }
    start = 0;
    while (start < length && pcore_image_map_ascii_space(data[start])) {
        start++;
    }
    end = length;
    while (end > start && pcore_image_map_ascii_space(data[end - 1])) {
        end--;
    }
    copy_length = end - start;
    if (copy_length == 0 || copy_length >= (size_t) capacity) {
        return 0;
    }
    memcpy(out, data + start, copy_length);
    out[copy_length] = '\0';
    return 1;
}

/* Resolve the map named by an image's usemap attribute.  A fragment id is
 * preferred when present; bare names are accepted as a bounded compatibility
 * extension for old WM pages.  The returned map node is retained. */
static dom_element *pcore_image_map_for_image(dom_document *doc,
        dom_node *image)
{
    dom_string *use_map;
    char token[PCORE_IMAGE_MAP_TOKEN_MAX];
    dom_element *map;
    dom_string *id;
    dom_string *tag;
    dom_html_collection *maps;
    dom_node *node;
    dom_string *name;
    uint32_t count;
    uint32_t index;

    use_map = NULL;
    map = NULL;
    id = NULL;
    tag = NULL;
    maps = NULL;
    node = NULL;
    name = NULL;
    count = 0;
    if (doc == NULL || image == NULL ||
            !pcore_node_name_is(image, "img") ||
            dom_html_image_element_get_use_map(
            (dom_html_image_element *) image, &use_map) != DOM_NO_ERR ||
            !pcore_image_map_copy_token(use_map, token, sizeof(token))) {
        if (use_map != NULL) {
            dom_string_unref(use_map);
        }
        return NULL;
    }
    dom_string_unref(use_map);

    if (token[0] == '#') {
        if (token[1] == '\0' ||
                dom_string_create((const uint8_t *) (token + 1),
                strlen(token + 1), &id) != DOM_NO_ERR || id == NULL ||
                dom_document_get_element_by_id(doc, id, &map) != DOM_NO_ERR) {
            if (id != NULL) {
                dom_string_unref(id);
            }
            if (map != NULL) {
                dom_node_unref((dom_node *) map);
            }
            return NULL;
        }
        dom_string_unref(id);
        if (map != NULL && pcore_node_name_is((dom_node *) map, "map")) {
            return map;
        }
        if (map != NULL) {
            dom_node_unref((dom_node *) map);
        }
        return NULL;
    }

    if (dom_string_create((const uint8_t *) "map", 3, &tag) != DOM_NO_ERR ||
            tag == NULL || dom_document_get_elements_by_tag_name(doc, tag,
            &maps) != DOM_NO_ERR || maps == NULL ||
            dom_html_collection_get_length(maps, &count) != DOM_NO_ERR) {
        if (tag != NULL) {
            dom_string_unref(tag);
        }
        if (maps != NULL) {
            dom_html_collection_unref(maps);
        }
        return NULL;
    }
    if (count > PCORE_IMAGE_MAP_MAX_MAPS) {
        count = PCORE_IMAGE_MAP_MAX_MAPS;
    }
    for (index = 0; index < count; index++) {
        node = NULL;
        name = NULL;
        if (dom_html_collection_item(maps, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            break;
        }
        if (pcore_node_name_is(node, "map") &&
                dom_html_map_element_get_name(
                (dom_html_map_element *) node, &name) == DOM_NO_ERR &&
                name != NULL && pcore_image_map_token_is(name, token, 0)) {
            if (name != NULL) {
                dom_string_unref(name);
            }
            dom_html_collection_unref(maps);
            dom_string_unref(tag);
            return (dom_element *) node;
        }
        if (name != NULL) {
            dom_string_unref(name);
        }
        dom_node_unref(node);
    }
    dom_html_collection_unref(maps);
    dom_string_unref(tag);
    return NULL;
}

static int pcore_image_map_area_is_link(dom_node *area)
{
    bool no_href;
    dom_string *href;

    if (area == NULL || !pcore_node_name_is(area, "area") ||
            pcore_ensure_link_strings() != 0) {
        return 0;
    }
    no_href = false;
    if (dom_html_area_element_get_no_href(
            (dom_html_area_element *) area, &no_href) != DOM_NO_ERR ||
            no_href) {
        return 0;
    }
    href = NULL;
    if (dom_element_get_attribute((dom_element *) area, pcore_href_name,
            &href) != DOM_NO_ERR || href == NULL ||
            dom_string_byte_length(href) == 0) {
        if (href != NULL) {
            dom_string_unref(href);
        }
        return 0;
    }
    dom_string_unref(href);
    return 1;
}

static int pcore_image_map_parse_coords(dom_string *value, int *coords,
        int capacity)
{
    const char *data;
    size_t length;
    size_t index;
    int count;
    int sign;
    int number;
    int digit;
    int digits;

    if (value == NULL || coords == NULL || capacity <= 0) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (data == NULL || length == 0) {
        return 0;
    }
    index = 0;
    count = 0;
    while (index < length) {
        while (index < length && (pcore_image_map_ascii_space(data[index]) ||
                data[index] == ',')) {
            index++;
        }
        if (index >= length) {
            break;
        }
        sign = 1;
        if (data[index] == '+' || data[index] == '-') {
            if (data[index] == '-') {
                sign = -1;
            }
            index++;
        }
        number = 0;
        digits = 0;
        while (index < length && data[index] >= '0' &&
                data[index] <= '9') {
            digit = data[index] - '0';
            if (number > (PCORE_IMAGE_MAP_COORD_MAX - digit) / 10) {
                return 0;
            }
            number = number * 10 + digit;
            index++;
            digits = 1;
        }
        if (!digits || count >= capacity) {
            return 0;
        }
        coords[count++] = sign * number;
        if (index < length && !pcore_image_map_ascii_space(data[index]) &&
                data[index] != ',') {
            return 0;
        }
    }
    return count;
}

static int pcore_image_map_scale_coord(int value, int rendered, int base)
{
    double scaled;

    if (base <= 0 || rendered <= 0) {
        return value;
    }
    scaled = ((double) value * (double) rendered) / (double) base;
    if (scaled > (double) INT_MAX) {
        return INT_MAX;
    }
    if (scaled < (double) INT_MIN) {
        return INT_MIN;
    }
    return (int) scaled;
}

static void pcore_image_map_dimensions(dom_document *doc, dom_node *image,
        int image_w, int image_h, int *base_w, int *base_h)
{
    dom_string *src_name;
    dom_string *src;
    const char *src_data;
    int attempted;
    int natural_w;
    int natural_h;
    void *native_image;
    void *svg;

    *base_w = image_w;
    *base_h = image_h;
    src_name = NULL;
    src = NULL;
    attempted = 0;
    natural_w = 0;
    natural_h = 0;
    native_image = NULL;
    svg = NULL;
    if (doc == NULL || image == NULL ||
            dom_string_create((const uint8_t *) "src", 3,
            &src_name) != DOM_NO_ERR || src_name == NULL ||
            dom_element_get_attribute((dom_element *) image, src_name,
            &src) != DOM_NO_ERR || src == NULL ||
            dom_string_byte_length(src) == 0) {
        if (src != NULL) {
            dom_string_unref(src);
        }
        if (src_name != NULL) {
            dom_string_unref(src_name);
        }
        return;
    }
    src_data = dom_string_data(src);
    if (src_data != NULL) {
        (void) pcore_image_resource_retained_get(doc, src_data, &attempted,
                &native_image, &svg, &natural_w, &natural_h);
    }
    if (attempted && natural_w > 0 && natural_h > 0) {
        *base_w = natural_w;
        *base_h = natural_h;
    }
    dom_string_unref(src);
    dom_string_unref(src_name);
}

static int pcore_image_map_shape_kind(dom_string *shape)
{
    if (shape == NULL || pcore_image_map_token_is(shape, "default", 1)) {
        return 0;
    }
    if (pcore_image_map_token_is(shape, "rect", 1) ||
            pcore_image_map_token_is(shape, "rectangle", 1)) {
        return 1;
    }
    if (pcore_image_map_token_is(shape, "circle", 1)) {
        return 2;
    }
    if (pcore_image_map_token_is(shape, "poly", 1) ||
            pcore_image_map_token_is(shape, "polygon", 1)) {
        return 3;
    }
    /* The empty/missing shape has the HTML default shape. */
    if (shape != NULL && pcore_image_map_token_is(shape, "", 1)) {
        return 0;
    }
    return -1;
}

/* Add two bounded integer coordinates without wrapping.  DOM coordinates are
 * untrusted; saturating here lets the subsequent image-space clip reduce them
 * to the rendered image instead of turning a huge value into an unrelated
 * point. */
static int pcore_image_map_saturating_add(int left, int right)
{
    if (right > 0 && left > INT_MAX - right) {
        return INT_MAX;
    }
    if (right < 0 && left < INT_MIN - right) {
        return INT_MIN;
    }
    return left + right;
}

static int pcore_image_map_saturating_double(int value)
{
    if (value > INT_MAX / 2) {
        return INT_MAX;
    }
    if (value < INT_MIN / 2) {
        return INT_MIN;
    }
    return value * 2;
}

static int pcore_image_map_clip_coord(int value, int limit)
{
    if (value < 0) {
        return 0;
    }
    if (value > limit) {
        return limit;
    }
    return value;
}

static void pcore_image_map_region_clip(pcore_image_map_region *region,
        int image_w, int image_h)
{
    int right;
    int bottom;
    int left;
    int top;

    left = region->x;
    top = region->y;
    right = pcore_image_map_saturating_add(region->x, region->w);
    bottom = pcore_image_map_saturating_add(region->y, region->h);
    left = pcore_image_map_clip_coord(left, image_w);
    top = pcore_image_map_clip_coord(top, image_h);
    right = pcore_image_map_clip_coord(right, image_w);
    bottom = pcore_image_map_clip_coord(bottom, image_h);
    region->x = left;
    region->y = top;
    region->w = (right > left) ? right - left : 0;
    region->h = (bottom > top) ? bottom - top : 0;
}

/* Build one rendered-space region.  Coordinates are bounded to avoid the
 * untrusted DOM creating unbounded work or integer overflow. */
static int pcore_image_map_region_from_area(dom_node *area, int image_w,
        int image_h, int base_w, int base_h, pcore_image_map_region *region)
{
    dom_string *shape;
    dom_string *coords_string;
    int parsed[PCORE_IMAGE_MAP_MAX_COORDS];
    int parsed_count;
    int kind;
    int index;
    int left;
    int top;
    int right;
    int bottom;
    int radius_x;
    int radius_y;
    int center_x;
    int center_y;

    if (area == NULL || region == NULL || image_w <= 0 || image_h <= 0 ||
            base_w <= 0 || base_h <= 0 ||
            !pcore_image_map_area_is_link(area)) {
        return 1;
    }
    memset(region, 0, sizeof(*region));
    shape = NULL;
    if (dom_html_area_element_get_shape((dom_html_area_element *) area,
            &shape) != DOM_NO_ERR) {
        shape = NULL;
    }
    kind = pcore_image_map_shape_kind(shape);
    if (shape != NULL) {
        dom_string_unref(shape);
    }
    if (kind < 0) {
        return 1;
    }
    region->kind = kind;
    if (kind == 0) {
        region->x = 0;
        region->y = 0;
        region->w = image_w;
        region->h = image_h;
        return 0;
    }
    coords_string = NULL;
    if (dom_html_area_element_get_coords(
            (dom_html_area_element *) area, &coords_string) != DOM_NO_ERR ||
            coords_string == NULL) {
        if (coords_string != NULL) {
            dom_string_unref(coords_string);
        }
        return 1;
    }
    parsed_count = pcore_image_map_parse_coords(coords_string, parsed,
            PCORE_IMAGE_MAP_MAX_COORDS);
    dom_string_unref(coords_string);
    if (parsed_count <= 0) {
        return 1;
    }
    if (kind == 1) {
        if (parsed_count < 4) {
            return 1;
        }
        left = pcore_image_map_scale_coord(parsed[0], image_w, base_w);
        top = pcore_image_map_scale_coord(parsed[1], image_h, base_h);
        right = pcore_image_map_scale_coord(parsed[2], image_w, base_w);
        bottom = pcore_image_map_scale_coord(parsed[3], image_h, base_h);
        if (left > right) {
            index = left;
            left = right;
            right = index;
        }
        if (top > bottom) {
            index = top;
            top = bottom;
            bottom = index;
        }
        left = pcore_image_map_clip_coord(left, image_w);
        top = pcore_image_map_clip_coord(top, image_h);
        right = pcore_image_map_clip_coord(right, image_w);
        bottom = pcore_image_map_clip_coord(bottom, image_h);
        if (right <= left || bottom <= top) {
            return 1;
        }
        region->x = left;
        region->y = top;
        region->w = right - left;
        region->h = bottom - top;
    } else if (kind == 2) {
        if (parsed_count < 3 || parsed[2] <= 0) {
            return 1;
        }
        center_x = pcore_image_map_scale_coord(parsed[0], image_w, base_w);
        center_y = pcore_image_map_scale_coord(parsed[1], image_h, base_h);
        radius_x = pcore_image_map_scale_coord(parsed[2], image_w, base_w);
        radius_y = pcore_image_map_scale_coord(parsed[2], image_h, base_h);
        if (radius_x <= 0 || radius_y <= 0) {
            return 1;
        }
        region->center_x = center_x;
        region->center_y = center_y;
        region->radius_x = radius_x;
        region->radius_y = radius_y;
        region->x = pcore_image_map_saturating_add(center_x, -radius_x);
        region->y = pcore_image_map_saturating_add(center_y, -radius_y);
        region->w = pcore_image_map_saturating_double(radius_x);
        region->h = pcore_image_map_saturating_double(radius_y);
    } else {
        if (parsed_count < 6 || (parsed_count & 1) != 0) {
            return 1;
        }
        if (parsed_count > PCORE_IMAGE_MAP_MAX_COORDS) {
            return 1;
        }
        region->count = parsed_count;
        left = INT_MAX;
        top = INT_MAX;
        right = INT_MIN;
        bottom = INT_MIN;
        for (index = 0; index < parsed_count; index += 2) {
            region->coords[index] =
                    pcore_image_map_scale_coord(parsed[index], image_w,
                    base_w);
            region->coords[index + 1] =
                    pcore_image_map_scale_coord(parsed[index + 1], image_h,
                    base_h);
            if (region->coords[index] < left) {
                left = region->coords[index];
            }
            if (region->coords[index] > right) {
                right = region->coords[index];
            }
            if (region->coords[index + 1] < top) {
                top = region->coords[index + 1];
            }
            if (region->coords[index + 1] > bottom) {
                bottom = region->coords[index + 1];
            }
        }
        left = pcore_image_map_clip_coord(left, image_w);
        top = pcore_image_map_clip_coord(top, image_h);
        right = pcore_image_map_clip_coord(right, image_w);
        bottom = pcore_image_map_clip_coord(bottom, image_h);
        if (right <= left || bottom <= top) {
            return 1;
        }
        region->x = left;
        region->y = top;
        region->w = right - left;
        region->h = bottom - top;
    }
    pcore_image_map_region_clip(region, image_w, image_h);
    return (region->w > 0 && region->h > 0) ? 0 : 1;
}

static int pcore_image_map_point_on_segment(double px, double py,
        double x1, double y1, double x2, double y2)
{
    double cross;
    double min_x;
    double max_x;
    double min_y;
    double max_y;

    cross = (px - x1) * (y2 - y1) - (py - y1) * (x2 - x1);
    if (fabs(cross) > 0.0001) {
        return 0;
    }
    min_x = (x1 < x2) ? x1 : x2;
    max_x = (x1 > x2) ? x1 : x2;
    min_y = (y1 < y2) ? y1 : y2;
    max_y = (y1 > y2) ? y1 : y2;
    return px >= min_x && px <= max_x && py >= min_y && py <= max_y;
}

static int pcore_image_map_region_contains(
        const pcore_image_map_region *region, int x, int y)
{
    int index;
    int previous;
    int inside;
    double px;
    double py;
    double x1;
    double y1;
    double x2;
    double y2;
    double crossing;
    double radius_x;
    double radius_y;
    double dx;
    double dy;

    if (region == NULL || region->w <= 0 || region->h <= 0 ||
            x < region->x || y < region->y ||
            x - region->x >= region->w || y - region->y >= region->h) {
        return 0;
    }
    if (region->kind == 0 || region->kind == 1) {
        return 1;
    }
    if (region->kind == 2) {
        radius_x = (region->radius_x > 0) ?
                (double) region->radius_x : region->w / 2.0;
        radius_y = (region->radius_y > 0) ?
                (double) region->radius_y : region->h / 2.0;
        if (radius_x <= 0.0 || radius_y <= 0.0) {
            return 0;
        }
        dx = (double) x - (double) region->center_x;
        dy = (double) y - (double) region->center_y;
        return (dx * dx) / (radius_x * radius_x) +
                (dy * dy) / (radius_y * radius_y) <= 1.0;
    }
    if (region->kind != 3 || region->count < 6) {
        return 0;
    }
    px = (double) x;
    py = (double) y;
    inside = 0;
    previous = region->count - 2;
    for (index = 0; index < region->count; index += 2) {
        x1 = (double) region->coords[previous];
        y1 = (double) region->coords[previous + 1];
        x2 = (double) region->coords[index];
        y2 = (double) region->coords[index + 1];
        if (pcore_image_map_point_on_segment(px, py, x1, y1, x2, y2)) {
            return 1;
        }
        if ((y1 > py) != (y2 > py)) {
            crossing = (x2 - x1) * (py - y1) / (y2 - y1) + x1;
            if (px < crossing) {
                inside = !inside;
            }
        }
        previous = index;
    }
    return inside;
}

static int pcore_image_map_area_region_for_image(dom_document *doc,
        dom_node *image, struct box *image_box, dom_node *area,
        pcore_image_map_region *region)
{
    int base_w;
    int base_h;

    if (doc == NULL || image == NULL || image_box == NULL ||
            area == NULL || region == NULL || image_box->width <= 0 ||
            image_box->height <= 0) {
        return 1;
    }
    pcore_image_map_dimensions(doc, image, image_box->width,
            image_box->height, &base_w, &base_h);
    return pcore_image_map_region_from_area(area, image_box->width,
            image_box->height, base_w, base_h, region);
}

/* Find the first linked area covering a point in one rendered image.  The
 * collection and area references are released on every non-match; a match is
 * returned retained for the caller. */
static dom_node *pcore_image_map_area_for_image_point(dom_document *doc,
        dom_node *image, struct box *image_box, int x, int y)
{
    dom_element *map;
    dom_html_collection *areas;
    dom_node *area;
    uint32_t count;
    uint32_t index;
    int image_x;
    int image_y;
    int local_x;
    int local_y;
    pcore_image_map_region region;

    if (doc == NULL || image == NULL || image_box == NULL ||
            image_box->width <= 0 || image_box->height <= 0) {
        return NULL;
    }
    image_x = 0;
    image_y = 0;
    box_coords(image_box, &image_x, &image_y);
    local_x = x - image_x;
    local_y = y - image_y;
    if (local_x < 0 || local_y < 0 || local_x >= image_box->width ||
            local_y >= image_box->height) {
        return NULL;
    }
    areas = NULL;
    map = pcore_image_map_for_image(doc, image);
    if (map == NULL || dom_html_map_element_get_areas(
            (dom_html_map_element *) map, &areas) != DOM_NO_ERR ||
            areas == NULL || dom_html_collection_get_length(areas,
            &count) != DOM_NO_ERR) {
        if (map != NULL) {
            dom_node_unref((dom_node *) map);
        }
        if (areas != NULL) {
            dom_html_collection_unref(areas);
        }
        return NULL;
    }
    if (count > PCORE_IMAGE_MAP_MAX_AREAS) {
        count = PCORE_IMAGE_MAP_MAX_AREAS;
    }
    for (index = 0; index < count; index++) {
        area = NULL;
        if (dom_html_collection_item(areas, index, &area) != DOM_NO_ERR ||
                area == NULL) {
            break;
        }
        if (pcore_image_map_area_region_for_image(doc, image, image_box,
                area, &region) == 0 && pcore_image_map_region_contains(
                &region, local_x, local_y)) {
            dom_html_collection_unref(areas);
            dom_node_unref((dom_node *) map);
            return area;
        }
        dom_node_unref(area);
    }
    dom_html_collection_unref(areas);
    dom_node_unref((dom_node *) map);
    return NULL;
}

static dom_node *pcore_image_map_area_at_box(dom_document *doc,
        struct box *hit, int x, int y)
{
    struct box *box;
    dom_node *area;

    for (box = hit; box != NULL; box = box->parent) {
        if (box->node != NULL && pcore_node_name_is(box->node, "img")) {
            area = pcore_image_map_area_for_image_point(doc, box->node,
                    box, x, y);
            if (area != NULL) {
                return area;
            }
        }
    }
    return NULL;
}

static dom_node *pcore_image_map_area_at(struct pcore_render *st,
        dom_document *doc, int x, int y)
{
    struct box *hit;

    if (st == NULL || st->root_box == NULL || doc == NULL) {
        return NULL;
    }
    hit = pcore_hit(st->root_box, x, y);
    if (hit == NULL) {
        return NULL;
    }
    return pcore_image_map_area_at_box(doc, hit, x, y);
}

static int pcore_image_map_area_geometry_in_image(dom_document *doc,
        dom_node *image, struct box *image_box, dom_node *target,
        int *x, int *y, int *w, int *h)
{
    dom_element *map;
    dom_html_collection *areas;
    dom_node *area;
    uint32_t count;
    uint32_t index;
    int image_x;
    int image_y;
    pcore_image_map_region region;

    if (doc == NULL || image == NULL || image_box == NULL ||
            target == NULL || x == NULL || y == NULL || w == NULL ||
            h == NULL) {
        return 1;
    }
    areas = NULL;
    map = pcore_image_map_for_image(doc, image);
    if (map == NULL || dom_html_map_element_get_areas(
            (dom_html_map_element *) map, &areas) != DOM_NO_ERR ||
            areas == NULL || dom_html_collection_get_length(areas,
            &count) != DOM_NO_ERR) {
        if (map != NULL) {
            dom_node_unref((dom_node *) map);
        }
        if (areas != NULL) {
            dom_html_collection_unref(areas);
        }
        return 1;
    }
    if (count > PCORE_IMAGE_MAP_MAX_AREAS) {
        count = PCORE_IMAGE_MAP_MAX_AREAS;
    }
    for (index = 0; index < count; index++) {
        area = NULL;
        if (dom_html_collection_item(areas, index, &area) != DOM_NO_ERR ||
                area == NULL) {
            break;
        }
        if (area == target && pcore_image_map_area_region_for_image(doc,
                image, image_box, area, &region) == 0) {
            image_x = 0;
            image_y = 0;
            box_coords(image_box, &image_x, &image_y);
            *x = image_x + region.x;
            *y = image_y + region.y;
            *w = region.w;
            *h = region.h;
            dom_node_unref(area);
            dom_html_collection_unref(areas);
            dom_node_unref((dom_node *) map);
            return 0;
        }
        dom_node_unref(area);
    }
    dom_html_collection_unref(areas);
    dom_node_unref((dom_node *) map);
    return 1;
}

static int pcore_image_map_area_geometry_in_tree(dom_document *doc,
        struct box *box, dom_node *target, int *x, int *y, int *w, int *h)
{
    struct box *child;

    if (box == NULL) {
        return 1;
    }
    if (box->node != NULL && pcore_node_name_is(box->node, "img") &&
            pcore_image_map_area_geometry_in_image(doc, box->node, box,
            target, x, y, w, h) == 0) {
        return 0;
    }
    for (child = box->children; child != NULL; child = child->next) {
        if (pcore_image_map_area_geometry_in_tree(doc, child, target, x, y,
                w, h) == 0) {
            return 0;
        }
    }
    return 1;
}

static int pcore_image_map_area_geometry(struct pcore_render *st,
        dom_document *doc, dom_node *target, int *x, int *y, int *w, int *h)
{
    if (st == NULL || st->root_box == NULL || doc == NULL ||
            target == NULL || !pcore_node_name_is(target, "area")) {
        return 1;
    }
    return pcore_image_map_area_geometry_in_tree(doc, st->root_box, target,
            x, y, w, h);
}

#define PCORE_FORM_DATA_MAX 65535

typedef struct pcore_form_buffer {
    char *data;
    size_t length;
    size_t capacity;
    int pairs;
} pcore_form_buffer;

static int pcore_form_buffer_reserve(pcore_form_buffer *buffer,
        size_t additional)
{
    char *grown;
    size_t needed;
    size_t capacity;

    if (buffer == NULL || additional >
            PCORE_FORM_DATA_MAX - buffer->length) {
        return 0;
    }
    needed = buffer->length + additional + 1;
    if (needed <= buffer->capacity) {
        return 1;
    }
    capacity = (buffer->capacity > 0) ? buffer->capacity : 64;
    while (capacity < needed) {
        if (capacity > PCORE_FORM_DATA_MAX / 2) {
            capacity = PCORE_FORM_DATA_MAX + 1;
            break;
        }
        capacity *= 2;
    }
    grown = (char *) realloc(buffer->data, capacity);
    if (grown == NULL) {
        return 0;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int pcore_form_buffer_byte(pcore_form_buffer *buffer, char value)
{
    if (!pcore_form_buffer_reserve(buffer, 1)) {
        return 0;
    }
    buffer->data[buffer->length++] = value;
    buffer->data[buffer->length] = '\0';
    return 1;
}

/* Port of NetSurf utils/url.c url_escape(..., sptoplus=true): form spaces
 * become '+', '~' is escaped for compatibility, and UTF-8 is escaped byte by
 * byte without pretending that a legacy document charset was transcoded. */
static int pcore_form_buffer_encoded(pcore_form_buffer *buffer,
        const char *text, size_t length)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t i;

    for (i = 0; i < length; i++) {
        unsigned char c;
        int plain;

        c = (unsigned char) text[i];
        plain = (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '.' || c == '_';
        if (plain) {
            if (!pcore_form_buffer_byte(buffer, (char) c)) {
                return 0;
            }
        } else if (c == ' ') {
            if (!pcore_form_buffer_byte(buffer, '+')) {
                return 0;
            }
        } else {
            if (!pcore_form_buffer_reserve(buffer, 3)) {
                return 0;
            }
            buffer->data[buffer->length++] = '%';
            buffer->data[buffer->length++] = HEX[(c >> 4) & 15];
            buffer->data[buffer->length++] = HEX[c & 15];
            buffer->data[buffer->length] = '\0';
        }
    }
    return 1;
}

static int pcore_form_append_pair(pcore_form_buffer *buffer,
        dom_string *name, dom_string *value, const char *default_value)
{
    const char *name_data;
    const char *value_data;
    size_t name_length;
    size_t value_length;

    if (buffer == NULL || name == NULL) {
        return 0;
    }
    name_data = dom_string_data(name);
    name_length = dom_string_byte_length(name);
    if (value != NULL) {
        value_data = dom_string_data(value);
        value_length = dom_string_byte_length(value);
    } else {
        value_data = (default_value != NULL) ? default_value : "";
        value_length = strlen(value_data);
    }
    if (buffer->pairs > 0 && !pcore_form_buffer_byte(buffer, '&')) {
        return 0;
    }
    if (!pcore_form_buffer_encoded(buffer, name_data, name_length) ||
            !pcore_form_buffer_byte(buffer, '=') ||
            !pcore_form_buffer_encoded(buffer, value_data, value_length)) {
        return 0;
    }
    buffer->pairs++;
    return 1;
}

static int pcore_form_append_input(pcore_form_buffer *buffer,
        dom_html_input_element *input, dom_node *activated)
{
    dom_string *name;
    dom_string *value;
    bool disabled;
    bool checked;
    dom_node *node;
    int append;

    name = NULL;
    value = NULL;
    disabled = false;
    checked = false;
    append = 1;
    node = (dom_node *) input;
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
        return 0;
    }
    if (disabled) {
        return (disabled) ? 1 : 0;
    }
    if (dom_html_input_element_get_name(input, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (pcore_attr_value_is(node, "type", "reset") ||
            pcore_attr_value_is(node, "type", "button") ||
            pcore_attr_value_is(node, "type", "file") ||
            pcore_attr_value_is(node, "type", "image")) {
        dom_string_unref(name);
        return 1;
    }
    if (pcore_attr_value_is(node, "type", "submit") &&
            node != activated) {
        dom_string_unref(name);
        return 1;
    }
    if (pcore_attr_value_is(node, "type", "checkbox") ||
            pcore_attr_value_is(node, "type", "radio")) {
        if (dom_html_input_element_get_checked(input, &checked) !=
                DOM_NO_ERR) {
            dom_string_unref(name);
            return 0;
        }
        if (!checked) {
            dom_string_unref(name);
            return 1;
        }
    }
    if (dom_html_input_element_get_value(input, &value) != DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    if (!pcore_range_fill_default(node, &value)) {
        dom_string_unref(name);
        return 0;
    }
    append = pcore_form_append_pair(buffer, name, value,
            (pcore_attr_value_is(node, "type", "checkbox") ||
             pcore_attr_value_is(node, "type", "radio")) ? "on" : "");
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return append;
}

static int pcore_form_append_textarea(pcore_form_buffer *buffer,
        dom_html_text_area_element *textarea)
{
    dom_string *name;
    dom_string *value;
    bool disabled;
    int append;

    name = NULL;
    value = NULL;
    disabled = false;
    if (pcore_node_effectively_disabled((dom_node *) textarea, NULL,
            &disabled) != 0) {
        return 0;
    }
    if (disabled) {
        return (disabled) ? 1 : 0;
    }
    if (dom_html_text_area_element_get_name(textarea, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (dom_html_text_area_element_get_value(textarea, &value) !=
            DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    append = pcore_form_append_pair(buffer, name, value, "");
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return append;
}

static int pcore_form_append_select(pcore_form_buffer *buffer,
        dom_html_select_element *select)
{
    dom_html_options_collection *options;
    dom_string *name;
    dom_node *node;
    dom_string *value;
    uint32_t count;
    uint32_t index;
    bool disabled;
    bool option_disabled;
    bool selected;
    int append;

    options = NULL;
    name = NULL;
    node = NULL;
    value = NULL;
    count = 0;
    disabled = false;
    option_disabled = false;
    if (pcore_node_effectively_disabled((dom_node *) select, NULL,
            &disabled) != 0) {
        return 0;
    }
    if (disabled) {
        return (disabled) ? 1 : 0;
    }
    if (dom_html_select_element_get_name(select, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (dom_html_select_element_get_options(select, &options) != DOM_NO_ERR ||
            options == NULL ||
            dom_html_options_collection_get_length(options, &count) !=
                    DOM_NO_ERR) {
        if (options != NULL) {
            dom_html_options_collection_unref(options);
        }
        dom_string_unref(name);
        return 0;
    }
    append = 1;
    for (index = 0; index < count && append; index++) {
        node = NULL;
        value = NULL;
        selected = false;
        if (dom_html_options_collection_item(options, index, &node) !=
                DOM_NO_ERR || node == NULL ||
                dom_html_option_element_get_selected(
                        (dom_html_option_element *) node,
                        &selected) != DOM_NO_ERR) {
            append = 0;
        } else if (selected) {
            if (pcore_node_effectively_disabled(node, NULL,
                    &option_disabled) != 0) {
                append = 0;
            } else if (option_disabled) {
                append = 1;
            } else if (dom_html_option_element_get_value(
                    (dom_html_option_element *) node, &value) !=
                    DOM_NO_ERR) {
                append = 0;
            } else {
                append = pcore_form_append_pair(buffer, name, value, "");
            }
        }
        if (value != NULL) {
            dom_string_unref(value);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
    }
    dom_html_options_collection_unref(options);
    dom_string_unref(name);
    return append;
}

static int pcore_form_append_button(pcore_form_buffer *buffer,
        dom_html_button_element *button, dom_node *activated)
{
    dom_node *node;
    dom_string *name;
    dom_string *value;
    bool disabled;
    int append;

    node = (dom_node *) button;
    name = NULL;
    value = NULL;
    disabled = false;
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
        return 0;
    }
    if (disabled) {
        return (disabled) ? 1 : 0;
    }
    if (pcore_attr_value_is(node, "type", "reset") ||
            pcore_attr_value_is(node, "type", "button") ||
            node != activated) {
        return 1;
    }
    if (dom_html_button_element_get_name(button, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (dom_html_button_element_get_value(button, &value) != DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    append = pcore_form_append_pair(buffer, name, value, "");
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return append;
}

typedef struct pcore_form_build_data_context {
    dom_node *activated;
    pcore_form_buffer *buffer;
} pcore_form_build_data_context;

static int pcore_form_build_data_visit(dom_node *node, void *pw)
{
    pcore_form_build_data_context *context;
    int result;

    context = (pcore_form_build_data_context *) pw;
    if (context == NULL || context->buffer == NULL || node == NULL) {
        return -1;
    }
    result = 1;
    if (pcore_node_name_is(node, "input")) {
        result = pcore_form_append_input(context->buffer,
                (dom_html_input_element *) node, context->activated);
    } else if (pcore_node_name_is(node, "textarea")) {
        result = pcore_form_append_textarea(context->buffer,
                (dom_html_text_area_element *) node);
    } else if (pcore_node_name_is(node, "select")) {
        result = pcore_form_append_select(context->buffer,
                (dom_html_select_element *) node);
    } else if (pcore_node_name_is(node, "button")) {
        result = pcore_form_append_button(context->buffer,
                (dom_html_button_element *) node, context->activated);
    }
    return result ? 0 : -1;
}

static int pcore_form_build_data(dom_html_form_element *form,
        dom_node *activated, pcore_form_buffer *buffer)
{
    dom_document *doc;
    pcore_form_build_data_context context;
    int result;

    doc = NULL;
    if (form == NULL || buffer == NULL ||
            dom_node_get_owner_document((dom_node *) form, &doc) !=
                    DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return 0;
    }
    context.activated = activated;
    context.buffer = buffer;
    result = pcore_form_controls_visit(doc, (dom_element *) form,
            pcore_form_build_data_visit, &context);
    dom_node_unref((dom_node *) doc);
    return result == 0;
}

static dom_html_form_element *pcore_control_form(struct form_control *control)
{
    dom_document *doc;
    dom_element *owner;
    int has_attribute;
    int result;

    if (control == NULL || control->node == NULL) {
        return NULL;
    }
    doc = NULL;
    owner = NULL;
    has_attribute = 0;
    if (dom_node_get_owner_document(control->node, &doc) != DOM_NO_ERR ||
            doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return NULL;
    }
    result = pcore_form_control_owner(doc, control->node, &owner,
            &has_attribute);
    (void) has_attribute;
    dom_node_unref((dom_node *) doc);
    if (result != 0) {
        if (owner != NULL) {
            dom_node_unref((dom_node *) owner);
        }
        return NULL;
    }
    return (dom_html_form_element *) owner;
}

typedef struct pcore_form_first_submit_context {
    dom_node *found;
    int error;
} pcore_form_first_submit_context;

static int pcore_form_first_submit_visit(dom_node *node, void *pw)
{
    pcore_form_first_submit_context *context;
    bool disabled;
    int is_submit;

    context = (pcore_form_first_submit_context *) pw;
    if (context == NULL || node == NULL) {
        return -1;
    }
    disabled = false;
    is_submit = 0;
    if (pcore_node_name_is(node, "input")) {
        if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
            context->error = 1;
        } else if (!disabled && pcore_attr_value_is(node, "type",
                "submit")) {
            is_submit = 1;
        }
    } else if (pcore_node_name_is(node, "button")) {
        if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
            context->error = 1;
        } else if (!disabled && !pcore_attr_value_is(node, "type", "reset") &&
                !pcore_attr_value_is(node, "type", "button")) {
            is_submit = 1;
        }
    }
    if (context->error) {
        return -1;
    }
    if (is_submit) {
        context->found = dom_node_ref(node);
        return 1;
    }
    return 0;
}

static dom_node *pcore_form_first_submit(dom_html_form_element *form,
        int *error)
{
    dom_document *doc;
    pcore_form_first_submit_context context;
    int result;

    if (error == NULL) {
        return NULL;
    }
    *error = 0;
    context.found = NULL;
    context.error = 0;
    doc = NULL;
    if (form == NULL ||
            dom_node_get_owner_document((dom_node *) form, &doc) !=
                    DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        *error = 1;
        return NULL;
    }
    result = pcore_form_controls_visit(doc, (dom_element *) form,
            pcore_form_first_submit_visit, &context);
    dom_node_unref((dom_node *) doc);
    if (result != 0 || context.error) {
        if (context.found != NULL) {
            dom_node_unref(context.found);
            context.found = NULL;
        }
        *error = 1;
    }
    return context.found;
}

static int pcore_public_control_kind(int gadget_type)
{
    if (gadget_type == GADGET_CHECKBOX) { return 1; }
    if (gadget_type == GADGET_RADIO) { return 2; }
    if (gadget_type == GADGET_TEXTBOX) { return 3; }
    if (gadget_type == GADGET_PASSWORD) { return 4; }
    if (gadget_type == GADGET_TEXTAREA) { return 5; }
    if (gadget_type == GADGET_SELECT) { return 6; }
    if (gadget_type == GADGET_SUBMIT) { return 7; }
    if (gadget_type == GADGET_RESET) { return 8; }
    if (gadget_type == GADGET_BUTTON) { return 9; }
    if (gadget_type == GADGET_FILE) { return 10; }
    return 0;
}

static void pcore_form_validation_init(PCoreFormValidationInfo *info)
{
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->valid = 1;
    }
}

typedef struct pcore_radio_group_context {
    dom_string *name;
    int *checked_out;
} pcore_radio_group_context;

static int pcore_radio_group_visit(dom_node *node, void *pw)
{
    pcore_radio_group_context *context;
    dom_string *other_name;
    bool checked;

    context = (pcore_radio_group_context *) pw;
    if (context == NULL || context->name == NULL ||
            context->checked_out == NULL || node == NULL) {
        return -1;
    }
    if (!pcore_node_name_is(node, "input") ||
            !pcore_attr_value_is(node, "type", "radio")) {
        return 0;
    }
    other_name = NULL;
    checked = false;
    if (dom_html_input_element_get_name((dom_html_input_element *) node,
            &other_name) != DOM_NO_ERR ||
            dom_html_input_element_get_checked(
            (dom_html_input_element *) node, &checked) != DOM_NO_ERR) {
        if (other_name != NULL) {
            dom_string_unref(other_name);
        }
        return -1;
    }
    if (other_name != NULL && dom_string_isequal(context->name,
            other_name) && checked) {
        *context->checked_out = 1;
    }
    if (other_name != NULL) {
        dom_string_unref(other_name);
    }
    return *context->checked_out ? 1 : 0;
}

static int pcore_radio_group_checked(dom_html_form_element *form,
        dom_html_input_element *radio, int *checked_out)
{
    dom_document *doc;
    dom_string *name;
    bool checked;
    pcore_radio_group_context context;
    int result;

    if (checked_out == NULL) {
        return 0;
    }
    *checked_out = 0;
    name = NULL;
    checked = false;
    if (radio == NULL ||
            dom_html_input_element_get_name(radio, &name) != DOM_NO_ERR ||
            dom_html_input_element_get_checked(radio, &checked) !=
                    DOM_NO_ERR) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (name == NULL || dom_string_byte_length(name) == 0) {
        *checked_out = checked ? 1 : 0;
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 1;
    }
    doc = NULL;
    if (form == NULL ||
            dom_node_get_owner_document((dom_node *) form, &doc) !=
                    DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        dom_string_unref(name);
        return 0;
    }
    context.name = name;
    context.checked_out = checked_out;
    result = pcore_form_controls_visit(doc, (dom_element *) form,
            pcore_radio_group_visit, &context);
    dom_node_unref((dom_node *) doc);
    dom_string_unref(name);
    return result == 0;
}

static int pcore_required_select_missing(dom_html_select_element *select,
        int *missing)
{
    dom_html_options_collection *options;
    dom_node *node;
    dom_string *value;
    uint32_t count;
    uint32_t index;
    bool selected;
    int valid_selection;
    int result;

    options = NULL;
    node = NULL;
    value = NULL;
    count = 0;
    selected = false;
    valid_selection = 0;
    result = 1;
    if (dom_html_select_element_get_options(select, &options) != DOM_NO_ERR ||
            options == NULL ||
            dom_html_options_collection_get_length(options, &count) !=
                    DOM_NO_ERR) {
        if (options != NULL) {
            dom_html_options_collection_unref(options);
        }
        return 0;
    }
    for (index = 0; index < count && !valid_selection; index++) {
        node = NULL;
        value = NULL;
        selected = false;
        if (dom_html_options_collection_item(options, index, &node) !=
                DOM_NO_ERR || node == NULL ||
                dom_html_option_element_get_selected(
                        (dom_html_option_element *) node, &selected) !=
                                DOM_NO_ERR) {
            result = 0;
        } else if (selected) {
            if (dom_html_option_element_get_value(
                    (dom_html_option_element *) node, &value) != DOM_NO_ERR) {
                result = 0;
            } else if (value != NULL &&
                    dom_string_byte_length(value) > 0) {
                valid_selection = 1;
            }
        }
        if (value != NULL) {
            dom_string_unref(value);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
        if (!result) {
            break;
        }
    }
    dom_html_options_collection_unref(options);
    *missing = valid_selection ? 0 : 1;
    return result;
}

static int pcore_text_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    const char *pattern_data;
    size_t pattern_length;
    dom_string *pattern_name;
    dom_string *pattern_value;
    int pattern_result;
    unsigned int minimum;
    unsigned int maximum;
    unsigned int length;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (pcore_utf8_character_count(dom_string_data(value), &length) != 0) {
        return 0;
    }
    if (pcore_node_attr_unsigned(node, "minlength", &minimum) &&
            length < minimum) {
        *flags_out |= PCORE_VALIDITY_TOO_SHORT;
    }
    if (pcore_node_attr_unsigned(node, "maxlength", &maximum) &&
            length > maximum) {
        *flags_out |= PCORE_VALIDITY_TOO_LONG;
    }
    if (pcore_node_name_is(node, "input") &&
            pcore_node_has_attr(node, "pattern")) {
        pattern_name = NULL;
        pattern_value = NULL;
        pattern_result = -1;
        if (dom_string_create((const uint8_t *) "pattern", 7,
                &pattern_name) == DOM_NO_ERR && pattern_name != NULL &&
                dom_element_get_attribute(node, pattern_name,
                &pattern_value) == DOM_NO_ERR && pattern_value != NULL) {
            pattern_data = dom_string_data(pattern_value);
            pattern_length = dom_string_byte_length(pattern_value);
            pattern_result = pcore_pattern_match_full(pattern_data,
                    pattern_length, dom_string_data(value),
                    dom_string_byte_length(value));
        }
        if (pattern_value != NULL) {
            dom_string_unref(pattern_value);
        }
        if (pattern_name != NULL) {
            dom_string_unref(pattern_name);
        }
        if (pattern_result == 0) {
            *flags_out |= PCORE_VALIDITY_PATTERN_MISMATCH;
        }
    }
    return 1;
}

static int pcore_number_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    double number;
    double minimum;
    double maximum;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_dom_number(value, &number)) {
        *flags_out = PCORE_VALIDITY_BAD_INPUT;
        return 1;
    }
    if (pcore_node_attr_number(node, "min", &minimum) && number < minimum) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (pcore_node_attr_number(node, "max", &maximum) && number > maximum) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    if (!pcore_attr_value_is(node, "step", "any")) {
        double step;
        double base;
        double remainder;
        double tolerance;

        step = 1.0;
        if (!pcore_node_attr_number(node, "step", &step) || step <= 0.0) {
            step = 1.0;
        }
        base = 0.0;
        (void) pcore_node_attr_number(node, "min", &base);
        remainder = fmod(number - base, step);
        if (remainder < 0.0) {
            remainder = -remainder;
        }
        tolerance = step * 0.000000001;
        if (remainder > tolerance && step - remainder > tolerance) {
            *flags_out |= PCORE_VALIDITY_STEP_MISMATCH;
        }
    }
    return 1;
}

static int pcore_range_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    double number;
    double minimum;
    double maximum;

    if (!pcore_number_constraint_flags(node, value, flags_out)) {
        return 0;
    }
    if (value == NULL || dom_string_byte_length(value) == 0 ||
            (*flags_out & PCORE_VALIDITY_BAD_INPUT) != 0) {
        return 1;
    }
    if (!pcore_dom_number(value, &number)) {
        return 1;
    }
    if (!pcore_node_attr_number(node, "min", &minimum) && number < 0.0) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (!pcore_node_attr_number(node, "max", &maximum) && number > 100.0) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    return 1;
}

static int pcore_range_default_value(dom_node *node, char *buffer,
        size_t capacity)
{
    double minimum;
    double maximum;
    double default_value;
    int written;

    if (node == NULL || buffer == NULL || capacity == 0) {
        return 0;
    }
    minimum = 0.0;
    maximum = 100.0;
    (void) pcore_node_attr_number(node, "min", &minimum);
    (void) pcore_node_attr_number(node, "max", &maximum);
    if (minimum > maximum) {
        minimum = 0.0;
        maximum = 100.0;
    }
    default_value = (minimum + maximum) / 2.0;
    written = _snprintf(buffer, capacity, "%.15g", default_value);
    if (written < 0 || (size_t) written >= capacity) {
        buffer[capacity - 1] = '\0';
        return 0;
    }
    return 1;
}

static int pcore_range_fill_default(dom_node *node, dom_string **value_out)
{
    char default_value[64];

    if (node == NULL || value_out == NULL ||
            !pcore_attr_value_is(node, "type", "range") ||
            (*value_out != NULL && dom_string_byte_length(*value_out) > 0)) {
        return 1;
    }
    if (*value_out != NULL) {
        dom_string_unref(*value_out);
        *value_out = NULL;
    }
    if (!pcore_range_default_value(node, default_value,
            sizeof(default_value)) ||
            dom_string_create((const uint8_t *) default_value,
            strlen(default_value), value_out) != DOM_NO_ERR ||
            *value_out == NULL) {
        if (*value_out != NULL) {
            dom_string_unref(*value_out);
            *value_out = NULL;
        }
        return 0;
    }
    return 1;
}

static int pcore_dom_date(dom_string *value, int *year_out, int *month_out,
        int *day_out)
{
    const char *data;
    size_t length;
    size_t index;
    int year;
    int month;
    int day;
    int leap;
    int days;

    if (value == NULL || year_out == NULL || month_out == NULL ||
            day_out == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (length != 10 || data[4] != '-' || data[7] != '-') {
        return 0;
    }
    year = 0;
    for (index = 0; index < 4; index++) {
        if (data[index] < '0' || data[index] > '9') {
            return 0;
        }
        year = year * 10 + (int) (data[index] - '0');
    }
    month = 0;
    for (index = 5; index < 7; index++) {
        if (data[index] < '0' || data[index] > '9') {
            return 0;
        }
        month = month * 10 + (int) (data[index] - '0');
    }
    day = 0;
    for (index = 8; index < 10; index++) {
        if (data[index] < '0' || data[index] > '9') {
            return 0;
        }
        day = day * 10 + (int) (data[index] - '0');
    }
    if (year == 0 || month < 1 || month > 12) {
        return 0;
    }
    leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    days = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        days = 30;
    } else if (month == 2) {
        days = leap ? 29 : 28;
    }
    if (day < 1 || day > days) {
        return 0;
    }
    *year_out = year;
    *month_out = month;
    *day_out = day;
    return 1;
}

static int pcore_node_attr_date(dom_node *node, const char *attr,
        int *year_out, int *month_out, int *day_out)
{
    dom_string *name;
    dom_string *value;
    int result;

    name = NULL;
    value = NULL;
    result = 0;
    if (node == NULL || attr == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        result = pcore_dom_date(value, year_out, month_out, day_out);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_node_default_date(dom_node *node, int *year_out,
        int *month_out, int *day_out)
{
    dom_string *value;
    int result;

    value = NULL;
    if (node == NULL || !pcore_node_name_is(node, "input") ||
            dom_html_input_element_get_default_value(
            (dom_html_input_element *) node, &value) != DOM_NO_ERR ||
            value == NULL) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    result = pcore_dom_date(value, year_out, month_out, day_out);
    dom_string_unref(value);
    return result;
}

static int pcore_date_compare(int year, int month, int day,
        int other_year, int other_month, int other_day)
{
    if (year != other_year) {
        return (year < other_year) ? -1 : 1;
    }
    if (month != other_month) {
        return (month < other_month) ? -1 : 1;
    }
    if (day != other_day) {
        return (day < other_day) ? -1 : 1;
    }
    return 0;
}

static int pcore_date_day_number(int year, int month, int day)
{
    static const int days_before_month[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int previous_year;
    int result;

    previous_year = year - 1;
    result = previous_year * 365 + previous_year / 4 -
            previous_year / 100 + previous_year / 400;
    result += days_before_month[month] + day - 1;
    if (month > 2 && year % 4 == 0 &&
            (year % 100 != 0 || year % 400 == 0)) {
        result++;
    }
    return result;
}

static int pcore_step_mismatch(double value, double base, double step)
{
    double remainder;
    double tolerance;

    if (step <= 0.0) {
        return 0;
    }
    remainder = fmod(value - base, step);
    if (remainder < 0.0) {
        remainder = -remainder;
    }
    tolerance = step * 0.000000001;
    return remainder > tolerance && step - remainder > tolerance;
}

static int pcore_date_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    int year;
    int month;
    int day;
    int minimum_year;
    int minimum_month;
    int minimum_day;
    int maximum_year;
    int maximum_month;
    int maximum_day;
    double step;
    int base_year;
    int base_month;
    int base_day;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_dom_date(value, &year, &month, &day)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
        return 1;
    }
    if (pcore_node_attr_date(node, "min", &minimum_year, &minimum_month,
            &minimum_day) && pcore_date_compare(year, month, day,
            minimum_year, minimum_month, minimum_day) < 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (pcore_node_attr_date(node, "max", &maximum_year, &maximum_month,
            &maximum_day) && pcore_date_compare(year, month, day,
            maximum_year, maximum_month, maximum_day) > 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    if (!pcore_attr_value_is(node, "step", "any")) {
        step = 1.0;
        if (!pcore_node_attr_number(node, "step", &step) || step <= 0.0) {
            step = 1.0;
        }
        base_year = 1970;
        base_month = 1;
        base_day = 1;
        if (!pcore_node_attr_date(node, "min", &base_year, &base_month,
                &base_day) && !pcore_node_default_date(node, &base_year,
                &base_month, &base_day)) {
            base_year = 1970;
            base_month = 1;
            base_day = 1;
        }
        if (pcore_step_mismatch(
                (double) pcore_date_day_number(year, month, day),
                (double) pcore_date_day_number(base_year, base_month,
                        base_day), step)) {
            *flags_out |= PCORE_VALIDITY_STEP_MISMATCH;
        }
    }
    return 1;
}

static int pcore_dom_time(dom_string *value, int *milliseconds_out)
{
    const char *data;
    size_t length;
    size_t index;
    int hour;
    int minute;
    int second;
    int milliseconds;
    int digits;

    if (value == NULL || milliseconds_out == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (length < 5 || (length != 5 && length < 8) || data[2] != ':') {
        return 0;
    }
    if (data[0] < '0' || data[0] > '9' || data[1] < '0' ||
            data[1] > '9' || data[3] < '0' || data[3] > '9' ||
            data[4] < '0' || data[4] > '9') {
        return 0;
    }
    hour = (int) (data[0] - '0') * 10 + (int) (data[1] - '0');
    minute = (int) (data[3] - '0') * 10 + (int) (data[4] - '0');
    second = 0;
    milliseconds = 0;
    if (length > 5) {
        if (length < 8 || data[5] != ':' || data[6] < '0' ||
                data[6] > '9' || data[7] < '0' || data[7] > '9') {
            return 0;
        }
        second = (int) (data[6] - '0') * 10 + (int) (data[7] - '0');
        if (length > 8) {
            if (data[8] != '.' || length > 12) {
                return 0;
            }
            digits = (int) length - 9;
            if (digits < 1 || digits > 3) {
                return 0;
            }
            for (index = 9; index < length; index++) {
                if (data[index] < '0' || data[index] > '9') {
                    return 0;
                }
                milliseconds = milliseconds * 10 +
                        (int) (data[index] - '0');
            }
            if (digits == 1) {
                milliseconds *= 100;
            } else if (digits == 2) {
                milliseconds *= 10;
            }
        }
    }
    if (hour > 23 || minute > 59 || second > 59) {
        return 0;
    }
    *milliseconds_out = ((hour * 60 + minute) * 60 + second) * 1000 +
            milliseconds;
    return 1;
}

static int pcore_node_attr_time(dom_node *node, const char *attr,
        int *milliseconds_out)
{
    dom_string *name;
    dom_string *value;
    int result;

    name = NULL;
    value = NULL;
    result = 0;
    if (node == NULL || attr == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        result = pcore_dom_time(value, milliseconds_out);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_node_default_time(dom_node *node, int *milliseconds_out)
{
    dom_string *value;
    int result;

    value = NULL;
    if (node == NULL || !pcore_node_name_is(node, "input") ||
            dom_html_input_element_get_default_value(
            (dom_html_input_element *) node, &value) != DOM_NO_ERR ||
            value == NULL) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    result = pcore_dom_time(value, milliseconds_out);
    dom_string_unref(value);
    return result;
}

static int pcore_time_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    int milliseconds;
    int minimum;
    int maximum;
    double step;
    int base;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_dom_time(value, &milliseconds)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
        return 1;
    }
    if (pcore_node_attr_time(node, "min", &minimum) &&
            milliseconds < minimum) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (pcore_node_attr_time(node, "max", &maximum) &&
            milliseconds > maximum) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    if (!pcore_attr_value_is(node, "step", "any")) {
        step = 60.0;
        if (!pcore_node_attr_number(node, "step", &step) || step <= 0.0) {
            step = 60.0;
        }
        base = 0;
        if (!pcore_node_attr_time(node, "min", &base)) {
            (void) pcore_node_default_time(node, &base);
        }
        if (pcore_step_mismatch((double) milliseconds, (double) base,
                step * 1000.0)) {
            *flags_out |= PCORE_VALIDITY_STEP_MISMATCH;
        }
    }
    return 1;
}

static int pcore_dom_month(dom_string *value, int *year_out, int *month_out)
{
    const char *data;
    size_t length;
    size_t index;
    int year;
    int month;

    if (value == NULL || year_out == NULL || month_out == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (length != 7 || data[4] != '-') {
        return 0;
    }
    year = 0;
    for (index = 0; index < 4; index++) {
        if (data[index] < '0' || data[index] > '9') {
            return 0;
        }
        year = year * 10 + (int) (data[index] - '0');
    }
    if (data[5] < '0' || data[5] > '9' || data[6] < '0' ||
            data[6] > '9') {
        return 0;
    }
    month = (int) (data[5] - '0') * 10 + (int) (data[6] - '0');
    if (year == 0 || month < 1 || month > 12) {
        return 0;
    }
    *year_out = year;
    *month_out = month;
    return 1;
}

static int pcore_node_attr_month(dom_node *node, const char *attr,
        int *year_out, int *month_out)
{
    dom_string *name;
    dom_string *value;
    int result;

    name = NULL;
    value = NULL;
    result = 0;
    if (node == NULL || attr == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        result = pcore_dom_month(value, year_out, month_out);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_node_default_month(dom_node *node, int *year_out,
        int *month_out)
{
    dom_string *value;
    int result;

    value = NULL;
    if (node == NULL || !pcore_node_name_is(node, "input") ||
            dom_html_input_element_get_default_value(
            (dom_html_input_element *) node, &value) != DOM_NO_ERR ||
            value == NULL) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    result = pcore_dom_month(value, year_out, month_out);
    dom_string_unref(value);
    return result;
}

static int pcore_month_compare(int year, int month, int other_year,
        int other_month)
{
    if (year != other_year) {
        return (year < other_year) ? -1 : 1;
    }
    if (month != other_month) {
        return (month < other_month) ? -1 : 1;
    }
    return 0;
}

static int pcore_month_number(int year, int month)
{
    return year * 12 + month;
}

static int pcore_month_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    int year;
    int month;
    int minimum_year;
    int minimum_month;
    int maximum_year;
    int maximum_month;
    double step;
    int base_year;
    int base_month;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_dom_month(value, &year, &month)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
        return 1;
    }
    if (pcore_node_attr_month(node, "min", &minimum_year, &minimum_month) &&
            pcore_month_compare(year, month, minimum_year,
            minimum_month) < 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (pcore_node_attr_month(node, "max", &maximum_year, &maximum_month) &&
            pcore_month_compare(year, month, maximum_year,
            maximum_month) > 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    if (!pcore_attr_value_is(node, "step", "any")) {
        step = 1.0;
        if (!pcore_node_attr_number(node, "step", &step) || step <= 0.0) {
            step = 1.0;
        }
        base_year = 1970;
        base_month = 1;
        if (!pcore_node_attr_month(node, "min", &base_year, &base_month) &&
                !pcore_node_default_month(node, &base_year, &base_month)) {
            base_year = 1970;
            base_month = 1;
        }
        if (pcore_step_mismatch(
                (double) pcore_month_number(year, month),
                (double) pcore_month_number(base_year, base_month), step)) {
            *flags_out |= PCORE_VALIDITY_STEP_MISMATCH;
        }
    }
    return 1;
}

static int pcore_iso_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int pcore_iso_weeks_in_year(int year)
{
    int previous;
    int january_weekday;

    previous = year - 1;
    january_weekday = (previous + previous / 4 - previous / 100 +
            previous / 400 + 1) % 7;
    return (january_weekday == 4 ||
            (january_weekday == 3 && pcore_iso_leap_year(year))) ? 53 : 52;
}

static int pcore_dom_week(dom_string *value, int *year_out, int *week_out)
{
    const char *data;
    size_t index;
    size_t length;
    int year;
    int week;

    if (value == NULL || year_out == NULL || week_out == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (length != 8 || data[4] != '-' || data[5] != 'W') {
        return 0;
    }
    year = 0;
    for (index = 0; index < 4; index++) {
        if (data[index] < '0' || data[index] > '9') {
            return 0;
        }
        year = year * 10 + (int) (data[index] - '0');
    }
    if (data[6] < '0' || data[6] > '9' || data[7] < '0' ||
            data[7] > '9') {
        return 0;
    }
    week = (int) (data[6] - '0') * 10 + (int) (data[7] - '0');
    if (year == 0 || week < 1 || week > pcore_iso_weeks_in_year(year)) {
        return 0;
    }
    *year_out = year;
    *week_out = week;
    return 1;
}

static int pcore_node_attr_week(dom_node *node, const char *attr,
        int *year_out, int *week_out)
{
    dom_string *name;
    dom_string *value;
    int result;

    name = NULL;
    value = NULL;
    result = 0;
    if (node == NULL || attr == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        result = pcore_dom_week(value, year_out, week_out);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_node_default_week(dom_node *node, int *year_out,
        int *week_out)
{
    dom_string *value;
    int result;

    value = NULL;
    if (node == NULL || !pcore_node_name_is(node, "input") ||
            dom_html_input_element_get_default_value(
            (dom_html_input_element *) node, &value) != DOM_NO_ERR ||
            value == NULL) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    result = pcore_dom_week(value, year_out, week_out);
    dom_string_unref(value);
    return result;
}

static int pcore_week_compare(int year, int week, int other_year,
        int other_week)
{
    if (year != other_year) {
        return (year < other_year) ? -1 : 1;
    }
    if (week != other_week) {
        return (week < other_week) ? -1 : 1;
    }
    return 0;
}

static int pcore_week_number(int year, int week)
{
    int current_year;
    int result;

    result = 0;
    if (year >= 1970) {
        for (current_year = 1970; current_year < year; current_year++) {
            result += pcore_iso_weeks_in_year(current_year);
        }
    } else {
        for (current_year = year; current_year < 1970; current_year++) {
            result -= pcore_iso_weeks_in_year(current_year);
        }
    }
    return result + week - 1;
}

static int pcore_week_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    int year;
    int week;
    int minimum_year;
    int minimum_week;
    int maximum_year;
    int maximum_week;
    double step;
    int base_year;
    int base_week;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_dom_week(value, &year, &week)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
        return 1;
    }
    if (pcore_node_attr_week(node, "min", &minimum_year, &minimum_week) &&
            pcore_week_compare(year, week, minimum_year, minimum_week) < 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (pcore_node_attr_week(node, "max", &maximum_year, &maximum_week) &&
            pcore_week_compare(year, week, maximum_year, maximum_week) > 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    if (!pcore_attr_value_is(node, "step", "any")) {
        step = 1.0;
        if (!pcore_node_attr_number(node, "step", &step) || step <= 0.0) {
            step = 1.0;
        }
        base_year = 1970;
        base_week = 1;
        if (!pcore_node_attr_week(node, "min", &base_year, &base_week) &&
                !pcore_node_default_week(node, &base_year, &base_week)) {
            base_year = 1970;
            base_week = 1;
        }
        if (pcore_step_mismatch(
                (double) pcore_week_number(year, week),
                (double) pcore_week_number(base_year, base_week), step)) {
            *flags_out |= PCORE_VALIDITY_STEP_MISMATCH;
        }
    }
    return 1;
}

static int pcore_dom_datetime_local(dom_string *value, int *year_out,
        int *month_out, int *day_out, int *milliseconds_out)
{
    const char *data;
    size_t length;
    size_t time_length;
    char date_data[11];
    char time_data[16];
    dom_string *date_value;
    dom_string *time_value;
    int result;

    date_value = NULL;
    time_value = NULL;
    if (value == NULL || year_out == NULL || month_out == NULL ||
            day_out == NULL || milliseconds_out == NULL) {
        return 0;
    }
    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (length < 16 || data[10] != 'T') {
        return 0;
    }
    time_length = length - 11;
    if (time_length >= sizeof(time_data)) {
        return 0;
    }
    memcpy(date_data, data, 10);
    date_data[10] = '\0';
    memcpy(time_data, data + 11, time_length);
    time_data[time_length] = '\0';
    if (dom_string_create((const uint8_t *) date_data, 10, &date_value) !=
            DOM_NO_ERR || date_value == NULL ||
            dom_string_create((const uint8_t *) time_data, time_length,
            &time_value) != DOM_NO_ERR || time_value == NULL) {
        if (date_value != NULL) {
            dom_string_unref(date_value);
        }
        if (time_value != NULL) {
            dom_string_unref(time_value);
        }
        return 0;
    }
    result = pcore_dom_date(date_value, year_out, month_out, day_out) &&
            pcore_dom_time(time_value, milliseconds_out);
    dom_string_unref(date_value);
    dom_string_unref(time_value);
    return result;
}

static int pcore_node_attr_datetime_local(dom_node *node, const char *attr,
        int *year_out, int *month_out, int *day_out,
        int *milliseconds_out)
{
    dom_string *name;
    dom_string *value;
    int result;

    name = NULL;
    value = NULL;
    result = 0;
    if (node == NULL || attr == NULL ||
            dom_string_create((const uint8_t *) attr, strlen(attr), &name) !=
                    DOM_NO_ERR || name == NULL) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 0;
    }
    if (dom_element_get_attribute(node, name, &value) == DOM_NO_ERR &&
            value != NULL) {
        result = pcore_dom_datetime_local(value, year_out, month_out,
                day_out, milliseconds_out);
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_node_default_datetime_local(dom_node *node, int *year_out,
        int *month_out, int *day_out, int *milliseconds_out)
{
    dom_string *value;
    int result;

    value = NULL;
    if (node == NULL || !pcore_node_name_is(node, "input") ||
            dom_html_input_element_get_default_value(
            (dom_html_input_element *) node, &value) != DOM_NO_ERR ||
            value == NULL) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    result = pcore_dom_datetime_local(value, year_out, month_out, day_out,
            milliseconds_out);
    dom_string_unref(value);
    return result;
}

static int pcore_datetime_compare(int year, int month, int day,
        int milliseconds, int other_year, int other_month, int other_day,
        int other_milliseconds)
{
    int date_result;

    date_result = pcore_date_compare(year, month, day, other_year,
            other_month, other_day);
    if (date_result != 0) {
        return date_result;
    }
    if (milliseconds != other_milliseconds) {
        return (milliseconds < other_milliseconds) ? -1 : 1;
    }
    return 0;
}

static double pcore_datetime_number(int year, int month, int day,
        int milliseconds)
{
    return (double) pcore_date_day_number(year, month, day) * 86400000.0 +
            (double) milliseconds;
}

static int pcore_datetime_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    int year;
    int month;
    int day;
    int milliseconds;
    int minimum_year;
    int minimum_month;
    int minimum_day;
    int minimum_milliseconds;
    int maximum_year;
    int maximum_month;
    int maximum_day;
    int maximum_milliseconds;
    double step;
    int base_year;
    int base_month;
    int base_day;
    int base_milliseconds;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_dom_datetime_local(value, &year, &month, &day,
            &milliseconds)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
        return 1;
    }
    if (pcore_node_attr_datetime_local(node, "min", &minimum_year,
            &minimum_month, &minimum_day, &minimum_milliseconds) &&
            pcore_datetime_compare(year, month, day, milliseconds,
            minimum_year, minimum_month, minimum_day,
            minimum_milliseconds) < 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_UNDERFLOW;
    }
    if (pcore_node_attr_datetime_local(node, "max", &maximum_year,
            &maximum_month, &maximum_day, &maximum_milliseconds) &&
            pcore_datetime_compare(year, month, day, milliseconds,
            maximum_year, maximum_month, maximum_day,
            maximum_milliseconds) > 0) {
        *flags_out |= PCORE_VALIDITY_RANGE_OVERFLOW;
    }
    if (!pcore_attr_value_is(node, "step", "any")) {
        step = 60.0;
        if (!pcore_node_attr_number(node, "step", &step) || step <= 0.0) {
            step = 60.0;
        }
        base_year = 1970;
        base_month = 1;
        base_day = 1;
        base_milliseconds = 0;
        if (!pcore_node_attr_datetime_local(node, "min", &base_year,
                &base_month, &base_day, &base_milliseconds) &&
                !pcore_node_default_datetime_local(node, &base_year,
                &base_month, &base_day, &base_milliseconds)) {
            base_year = 1970;
            base_month = 1;
            base_day = 1;
            base_milliseconds = 0;
        }
        if (pcore_step_mismatch(
                pcore_datetime_number(year, month, day, milliseconds),
                pcore_datetime_number(base_year, base_month, base_day,
                        base_milliseconds), step * 1000.0)) {
            *flags_out |= PCORE_VALIDITY_STEP_MISMATCH;
        }
    }
    return 1;
}

typedef struct pcore_custom_validity_entry pcore_custom_validity_entry;

struct pcore_custom_validity_entry {
    pcore_custom_validity_entry *next;
    dom_node *node;
    char *message;
};

typedef struct pcore_custom_validity_state {
    pcore_custom_validity_entry *head;
} pcore_custom_validity_state;

static dom_string *pcore_custom_validity_key = NULL;

static int pcore_ensure_custom_validity_key(void)
{
    static const char KEY[] = "__pcore_custom_validity__";

    if (pcore_custom_validity_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) KEY, sizeof(KEY) - 1,
            &pcore_custom_validity_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_custom_validity_entry_free(
        pcore_custom_validity_entry *entry)
{
    if (entry == NULL) {
        return;
    }
    if (entry->node != NULL) {
        dom_node_unref(entry->node);
    }
    free(entry->message);
    free(entry);
}

static void pcore_custom_validity_state_free(
        pcore_custom_validity_state *state)
{
    pcore_custom_validity_entry *entry;
    pcore_custom_validity_entry *next;

    if (state == NULL) {
        return;
    }
    entry = state->head;
    while (entry != NULL) {
        next = entry->next;
        pcore_custom_validity_entry_free(entry);
        entry = next;
    }
    free(state);
}

static void pcore_custom_validity_ud_handler(dom_node_operation op,
        dom_string *key, void *data, struct dom_node *src,
        struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_custom_validity_state_free(
                (pcore_custom_validity_state *) data);
    }
}

static pcore_custom_validity_state *pcore_custom_validity_state_get(
        dom_document *doc, int create)
{
    pcore_custom_validity_state *state;
    void *data;
    void *old;

    if (doc == NULL || pcore_ensure_custom_validity_key() != 0) {
        return NULL;
    }
    data = NULL;
    if (dom_node_get_user_data((dom_node *) doc,
            pcore_custom_validity_key, &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL || !create) {
        return (pcore_custom_validity_state *) data;
    }
    state = (pcore_custom_validity_state *) calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }
    old = NULL;
    if (dom_node_set_user_data((dom_node *) doc,
            pcore_custom_validity_key, state,
            pcore_custom_validity_ud_handler, &old) != DOM_NO_ERR) {
        free(state);
        return NULL;
    }
    if (old != NULL && old != state) {
        pcore_custom_validity_state_free(
                (pcore_custom_validity_state *) old);
    }
    return state;
}

static pcore_custom_validity_entry *pcore_custom_validity_find(
        pcore_custom_validity_state *state, dom_node *node,
        pcore_custom_validity_entry **previous_out)
{
    pcore_custom_validity_entry *entry;
    pcore_custom_validity_entry *previous;

    previous = NULL;
    if (previous_out != NULL) {
        *previous_out = NULL;
    }
    if (state == NULL || node == NULL) {
        return NULL;
    }
    entry = state->head;
    while (entry != NULL) {
        if (entry->node == node) {
            if (previous_out != NULL) {
                *previous_out = previous;
            }
            return entry;
        }
        previous = entry;
        entry = entry->next;
    }
    return NULL;
}

static int pcore_custom_validity_has_error(dom_node *node)
{
    dom_document *doc;
    pcore_custom_validity_state *state;
    pcore_custom_validity_entry *entry;

    doc = NULL;
    if (node == NULL || dom_node_get_owner_document(node, &doc) !=
            DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return 0;
    }
    state = pcore_custom_validity_state_get(doc, 0);
    entry = pcore_custom_validity_find(state, node, NULL);
    dom_node_unref((dom_node *) doc);
    return entry != NULL && entry->message != NULL &&
            entry->message[0] != '\0';
}

static char *pcore_custom_validity_copy(const char *message)
{
    size_t length;
    char *copy;

    if (message == NULL) {
        message = "";
    }
    length = strlen(message);
    copy = (char *) malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, message, length);
        copy[length] = '\0';
    }
    return copy;
}

static int pcore_custom_validity_set(dom_document *doc, dom_node *node,
        const char *message)
{
    pcore_custom_validity_state *state;
    pcore_custom_validity_entry *entry;
    pcore_custom_validity_entry *previous;
    char *copy;

    if (doc == NULL || node == NULL) {
        return -1;
    }
    state = pcore_custom_validity_state_get(doc, 1);
    if (state == NULL) {
        return -1;
    }
    if (message == NULL || message[0] == '\0') {
        entry = pcore_custom_validity_find(state, node, &previous);
        if (entry != NULL) {
            if (previous != NULL) {
                previous->next = entry->next;
            } else {
                state->head = entry->next;
            }
            pcore_custom_validity_entry_free(entry);
        }
        return 0;
    }
    copy = pcore_custom_validity_copy(message);
    if (copy == NULL) {
        return -1;
    }
    entry = pcore_custom_validity_find(state, node, NULL);
    if (entry != NULL) {
        free(entry->message);
        entry->message = copy;
        return 0;
    }
    entry = (pcore_custom_validity_entry *) calloc(1, sizeof(*entry));
    if (entry == NULL) {
        free(copy);
        return -1;
    }
    entry->node = dom_node_ref(node);
    entry->message = copy;
    entry->next = state->head;
    state->head = entry;
    return 0;
}

static int pcore_custom_validity_get(dom_document *doc, dom_node *node,
        char *message, unsigned int capacity)
{
    pcore_custom_validity_state *state;
    pcore_custom_validity_entry *entry;
    size_t length;
    size_t copy_length;

    if (doc == NULL || node == NULL) {
        return -1;
    }
    state = pcore_custom_validity_state_get(doc, 0);
    entry = pcore_custom_validity_find(state, node, NULL);
    length = (entry != NULL && entry->message != NULL) ?
            strlen(entry->message) : 0;
    if (length > (size_t) INT_MAX) {
        return -1;
    }
    if (message != NULL && capacity > 0) {
        copy_length = length;
        if (copy_length >= (size_t) capacity) {
            copy_length = (size_t) capacity - 1;
        }
        if (copy_length > 0 && entry != NULL && entry->message != NULL) {
            memcpy(message, entry->message, copy_length);
        }
        message[copy_length] = '\0';
    }
    return (int) length;
}

static int pcore_hex_digit(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');
}

static int pcore_color_constraint_flags(dom_string *value,
        unsigned int *flags_out)
{
    const char *data;
    size_t index;

    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    data = dom_string_data(value);
    if (dom_string_byte_length(value) != 7 || data[0] != '#') {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
        return 1;
    }
    for (index = 1; index < 7; index++) {
        if (!pcore_hex_digit((unsigned char) data[index])) {
            *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
            break;
        }
    }
    return 1;
}

static int pcore_email_local_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
        return 1;
    }
    return strchr("!#$%&'*+-/=?^_`{|}~", (int) c) != NULL;
}

static int pcore_email_address_valid(const char *data, size_t start,
        size_t end)
{
    size_t index;
    size_t at;
    size_t label_start;

    if (data == NULL || start >= end) {
        return 0;
    }
    at = end;
    for (index = start; index < end; index++) {
        if (data[index] == '@') {
            if (at != end) {
                return 0;
            }
            at = index;
        }
    }
    if (at == end || at == start || at + 1 >= end) {
        return 0;
    }
    if (data[start] == '.' || data[at - 1] == '.') {
        return 0;
    }
    for (index = start; index < at; index++) {
        if (data[index] == '.') {
            if (index + 1 == at || data[index + 1] == '.') {
                return 0;
            }
        } else if (!pcore_email_local_char((unsigned char) data[index])) {
            return 0;
        }
    }
    label_start = at + 1;
    for (index = label_start; index < end; index++) {
        if (data[index] == '.') {
            if (index == label_start || data[index - 1] == '-') {
                return 0;
            }
            label_start = index + 1;
        } else if (!((data[index] >= 'A' && data[index] <= 'Z') ||
                (data[index] >= 'a' && data[index] <= 'z') ||
                (data[index] >= '0' && data[index] <= '9') ||
                data[index] == '-')) {
            return 0;
        }
    }
    if (label_start >= end || data[label_start] == '-' ||
            data[end - 1] == '-') {
        return 0;
    }
    return 1;
}

static int pcore_email_value_valid(dom_node *node, dom_string *value)
{
    const char *data;
    size_t length;
    size_t start;
    size_t end;
    int multiple;
    int token_count;

    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    multiple = pcore_node_has_attr(node, "multiple");
    start = 0;
    token_count = 0;
    while (start < length) {
        end = start;
        while (end < length && data[end] != ',') {
            end++;
        }
        while (start < end && (data[start] == ' ' || data[start] == '\t' ||
                data[start] == '\r' || data[start] == '\n' ||
                data[start] == '\f')) {
            start++;
        }
        while (end > start && (data[end - 1] == ' ' ||
                data[end - 1] == '\t' || data[end - 1] == '\r' ||
                data[end - 1] == '\n' || data[end - 1] == '\f')) {
            end--;
        }
        if (!pcore_email_address_valid(data, start, end)) {
            return 0;
        }
        token_count++;
        if (end == length) {
            break;
        }
        if (!multiple) {
            return 0;
        }
        start = end + 1;
        if (start >= length) {
            return 0;
        }
    }
    return token_count > 0;
}

static int pcore_email_constraint_flags(dom_node *node, dom_string *value,
        unsigned int *flags_out)
{
    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_email_value_valid(node, value)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
    }
    return 1;
}

static int pcore_url_scheme_char(unsigned char c, int first)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 1;
    }
    if (!first && ((c >= '0' && c <= '9') || c == '+' || c == '-' ||
            c == '.')) {
        return 1;
    }
    return 0;
}

static int pcore_url_value_valid(dom_string *value)
{
    const char *data;
    size_t length;
    size_t index;
    size_t colon;
    size_t authority_start;
    size_t authority_end;

    data = dom_string_data(value);
    length = dom_string_byte_length(value);
    if (length == 0) {
        return 1;
    }
    for (index = 0; index < length; index++) {
        if ((unsigned char) data[index] <= 0x20 ||
                (unsigned char) data[index] == 0x7f) {
            return 0;
        }
    }
    colon = length;
    for (index = 0; index < length; index++) {
        if (data[index] == ':') {
            colon = index;
            break;
        }
        if (data[index] == '/' || data[index] == '?' || data[index] == '#') {
            break;
        }
    }
    if (colon == 0 || (colon < length && colon == length - 1)) {
        return 0;
    }
    if (colon < length) {
        for (index = 0; index < colon; index++) {
            if (!pcore_url_scheme_char((unsigned char) data[index],
                    index == 0)) {
                return 0;
            }
        }
        if (colon + 2 < length && data[colon + 1] == '/' &&
                data[colon + 2] == '/') {
            authority_start = colon + 3;
            authority_end = authority_start;
            while (authority_end < length && data[authority_end] != '/' &&
                    data[authority_end] != '?' && data[authority_end] != '#') {
                authority_end++;
            }
            if (authority_start == authority_end) {
                return 0;
            }
        }
        return 1;
    }
    if (length >= 2 && data[0] == '/' && data[1] == '/') {
        authority_start = 2;
        authority_end = authority_start;
        while (authority_end < length && data[authority_end] != '/' &&
                data[authority_end] != '?' && data[authority_end] != '#') {
            authority_end++;
        }
        if (authority_start == authority_end) {
            return 0;
        }
    }
    return data[0] != ':';
}

static int pcore_url_constraint_flags(dom_string *value,
        unsigned int *flags_out)
{
    *flags_out = 0;
    if (value == NULL || dom_string_byte_length(value) == 0) {
        return 1;
    }
    if (!pcore_url_value_valid(value)) {
        *flags_out = PCORE_VALIDITY_TYPE_MISMATCH;
    }
    return 1;
}

static int pcore_required_control_missing(dom_html_form_element *form,
        dom_node *node, int *kind_out, int *missing_out,
        unsigned int *flags_out)
{
    dom_string *value;
    bool disabled;
    bool read_only;
    bool checked;
    int gadget_type;
    int group_checked;
    int required;
    unsigned int email_flags;

    value = NULL;
    disabled = false;
    read_only = false;
    checked = false;
    *kind_out = 0;
    *missing_out = 0;
    *flags_out = 0;
    required = pcore_node_has_attr(node, "required");
    gadget_type = pcore_form_control_type(node);
    *kind_out = pcore_public_control_kind(gadget_type);
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
        return 0;
    }
    if (disabled) {
        return 1;
    }
    if (pcore_node_name_is(node, "input")) {
        if (dom_html_input_element_get_disabled(
                (dom_html_input_element *) node, &disabled) != DOM_NO_ERR) {
            return 0;
        }
        if (disabled || gadget_type == 0 ||
                gadget_type == GADGET_SUBMIT ||
                gadget_type == GADGET_RESET ||
                gadget_type == GADGET_BUTTON) {
            return 1;
        }
        if (gadget_type == GADGET_TEXTBOX ||
                gadget_type == GADGET_PASSWORD) {
            if (dom_html_input_element_get_read_only(
                    (dom_html_input_element *) node, &read_only) !=
                            DOM_NO_ERR) {
                return 0;
            }
            if (read_only) {
                return 1;
            }
            if (dom_html_input_element_get_value(
                    (dom_html_input_element *) node, &value) != DOM_NO_ERR) {
                return 0;
            }
            if (required && (value == NULL ||
                    dom_string_byte_length(value) == 0)) {
                *flags_out = PCORE_VALIDITY_VALUE_MISSING;
            } else {
                if (pcore_attr_value_is(node, "type", "color")) {
                    if (!pcore_color_constraint_flags(value, flags_out)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (pcore_attr_value_is(node, "type", "datetime-local")) {
                    if (!pcore_datetime_constraint_flags(node, value,
                            flags_out)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (pcore_attr_value_is(node, "type", "week")) {
                    if (!pcore_week_constraint_flags(node, value, flags_out)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (pcore_attr_value_is(node, "type", "month")) {
                    if (!pcore_month_constraint_flags(node, value, flags_out)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (pcore_attr_value_is(node, "type", "time")) {
                    if (!pcore_time_constraint_flags(node, value, flags_out)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (pcore_attr_value_is(node, "type", "date")) {
                    if (!pcore_date_constraint_flags(node, value, flags_out)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (pcore_attr_value_is(node, "type", "number") ||
                        pcore_attr_value_is(node, "type", "range")) {
                    if ((pcore_attr_value_is(node, "type", "range") ?
                            !pcore_range_constraint_flags(node, value,
                            flags_out) :
                            !pcore_number_constraint_flags(node, value,
                            flags_out))) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                } else if (!pcore_text_constraint_flags(node, value,
                        flags_out)) {
                    if (value != NULL) {
                        dom_string_unref(value);
                    }
                    return 0;
                }
                if (pcore_attr_value_is(node, "type", "email")) {
                    email_flags = 0;
                    if (!pcore_email_constraint_flags(node, value,
                            &email_flags)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                    *flags_out |= email_flags;
                }
                if (pcore_attr_value_is(node, "type", "url")) {
                    email_flags = 0;
                    if (!pcore_url_constraint_flags(value, &email_flags)) {
                        if (value != NULL) {
                            dom_string_unref(value);
                        }
                        return 0;
                    }
                    *flags_out |= email_flags;
                }
            }
            if (pcore_custom_validity_has_error(node)) {
                *flags_out |= PCORE_VALIDITY_CUSTOM_ERROR;
            }
            if (value != NULL) {
                dom_string_unref(value);
            }
            *missing_out = (*flags_out != 0) ? 1 : 0;
            return 1;
        }
        if (gadget_type == GADGET_CHECKBOX) {
            if (required) {
                if (dom_html_input_element_get_checked(
                        (dom_html_input_element *) node, &checked) !=
                                DOM_NO_ERR) {
                    return 0;
                }
                if (!checked) {
                    *flags_out |= PCORE_VALIDITY_VALUE_MISSING;
                }
            }
            if (pcore_custom_validity_has_error(node)) {
                *flags_out |= PCORE_VALIDITY_CUSTOM_ERROR;
            }
            *missing_out = (*flags_out != 0) ? 1 : 0;
            return 1;
        }
        if (gadget_type == GADGET_RADIO) {
            if (required) {
                group_checked = 0;
                if (!pcore_radio_group_checked(form,
                        (dom_html_input_element *) node, &group_checked)) {
                    return 0;
                }
                if (!group_checked) {
                    *flags_out |= PCORE_VALIDITY_VALUE_MISSING;
                }
            }
            if (pcore_custom_validity_has_error(node)) {
                *flags_out |= PCORE_VALIDITY_CUSTOM_ERROR;
            }
            *missing_out = (*flags_out != 0) ? 1 : 0;
            return 1;
        }
        if (gadget_type == GADGET_FILE) {
            if (required && dom_html_input_element_get_value(
                    (dom_html_input_element *) node, &value) != DOM_NO_ERR) {
                return 0;
            }
            if (required && (value == NULL ||
                    dom_string_byte_length(value) == 0)) {
                *flags_out |= PCORE_VALIDITY_VALUE_MISSING;
            }
            if (pcore_custom_validity_has_error(node)) {
                *flags_out |= PCORE_VALIDITY_CUSTOM_ERROR;
            }
            *missing_out = (*flags_out != 0) ? 1 : 0;
            if (value != NULL) {
                dom_string_unref(value);
            }
            return 1;
        }
        if (!required) {
            return 1;
        }
    }
    if (pcore_node_name_is(node, "textarea")) {
        if (dom_html_text_area_element_get_disabled(
                (dom_html_text_area_element *) node, &disabled) !=
                        DOM_NO_ERR ||
                dom_html_text_area_element_get_read_only(
                (dom_html_text_area_element *) node, &read_only) !=
                        DOM_NO_ERR) {
            return 0;
        }
        if (disabled || read_only) {
            return 1;
        }
        if (dom_html_text_area_element_get_value(
                (dom_html_text_area_element *) node, &value) != DOM_NO_ERR) {
            return 0;
        }
        if (required && (value == NULL ||
                dom_string_byte_length(value) == 0)) {
            *flags_out = PCORE_VALIDITY_VALUE_MISSING;
        } else if (!pcore_text_constraint_flags(node, value, flags_out)) {
            if (value != NULL) {
                dom_string_unref(value);
            }
            return 0;
        }
        if (pcore_custom_validity_has_error(node)) {
            *flags_out |= PCORE_VALIDITY_CUSTOM_ERROR;
        }
        *missing_out = (*flags_out != 0) ? 1 : 0;
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 1;
    }
    if (pcore_node_name_is(node, "select")) {
        if (dom_html_select_element_get_disabled(
                (dom_html_select_element *) node, &disabled) != DOM_NO_ERR) {
            return 0;
        }
        if (disabled) {
            return 1;
        }
        if (required) {
            if (!pcore_required_select_missing(
                    (dom_html_select_element *) node, missing_out)) {
                return 0;
            }
            *flags_out = (*missing_out != 0) ?
                    PCORE_VALIDITY_VALUE_MISSING : 0;
        }
        if (pcore_custom_validity_has_error(node)) {
            *flags_out |= PCORE_VALIDITY_CUSTOM_ERROR;
        }
        *missing_out = (*flags_out != 0) ? 1 : 0;
        return 1;
    }
    return 1;
}

static dom_html_form_element *pcore_form_for_node(dom_node *node)
{
    dom_document *doc;
    dom_element *owner;
    int has_attribute;
    int result;

    doc = NULL;
    owner = NULL;
    has_attribute = 0;
    if (node == NULL ||
            dom_node_get_owner_document(node, &doc) != DOM_NO_ERR ||
            doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return NULL;
    }
    result = pcore_form_control_owner(doc, node, &owner,
            &has_attribute);
    (void) has_attribute;
    dom_node_unref((dom_node *) doc);
    if (result != 0) {
        if (owner != NULL) {
            dom_node_unref((dom_node *) owner);
        }
        return NULL;
    }
    return (dom_html_form_element *) owner;
}

/* Report whether a control participates in constraint validation. This is
 * deliberately kept beside pcore_required_control_missing so both the
 * submission path and the script-visible query use the same control-kind
 * classification, including effective disabled fieldset inheritance. */
static int pcore_control_will_validate(dom_node *node, int gadget_type,
        int *out_will_validate)
{
    bool disabled;
    bool read_only;

    if (out_will_validate == NULL) {
        return 0;
    }
    *out_will_validate = 0;
    if (node == NULL || gadget_type == 0 ||
            gadget_type == GADGET_SUBMIT ||
            gadget_type == GADGET_RESET ||
            gadget_type == GADGET_BUTTON) {
        return 1;
    }
    disabled = false;
    read_only = false;
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0) {
        return 0;
    }
    if (disabled) {
        return 1;
    }
    if (pcore_node_name_is(node, "input")) {
        if (dom_html_input_element_get_disabled(
                (dom_html_input_element *) node, &disabled) != DOM_NO_ERR) {
            return 0;
        }
        if (disabled) {
            return 1;
        }
        if ((gadget_type == GADGET_TEXTBOX ||
             gadget_type == GADGET_PASSWORD) &&
                dom_html_input_element_get_read_only(
                (dom_html_input_element *) node, &read_only) != DOM_NO_ERR) {
            return 0;
        }
        if (read_only) {
            return 1;
        }
    } else if (pcore_node_name_is(node, "textarea")) {
        if (dom_html_text_area_element_get_disabled(
                (dom_html_text_area_element *) node, &disabled) != DOM_NO_ERR ||
                dom_html_text_area_element_get_read_only(
                (dom_html_text_area_element *) node, &read_only) !=
                        DOM_NO_ERR) {
            return 0;
        }
        if (disabled || read_only) {
            return 1;
        }
    } else if (pcore_node_name_is(node, "select")) {
        if (dom_html_select_element_get_disabled(
                (dom_html_select_element *) node, &disabled) != DOM_NO_ERR) {
            return 0;
        }
        if (disabled) {
            return 1;
        }
    }
    *out_will_validate = 1;
    return 1;
}

static dom_element *pcore_custom_validity_element_by_id(
        dom_document *doc, const char *element_id)
{
    dom_string *id;
    dom_element *element;

    if (doc == NULL || element_id == NULL || element_id[0] == '\0') {
        return NULL;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id,
            strlen(element_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR ||
            element == NULL) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return NULL;
    }
    dom_string_unref(id);
    return element;
}

static int pcore_form_validate(pcore_render *st,
        dom_html_form_element *form, dom_node *activated,
        PCoreFormValidationInfo *out_info);

/* Resolve the optional submitter used by a script requestSubmit() call. The
 * returned element is retained by the caller. An empty or NULL id means that
 * no explicit submitter was supplied; any non-empty id must name an enabled
 * input type=submit or submit-type button owned by the target form. */
static dom_node *pcore_form_submitter_by_id(dom_document *doc,
        dom_html_form_element *form, const char *submitter_id, int *error)
{
    dom_element *element;
    dom_element *owner;
    bool disabled;
    int has_attribute;
    int result;

    if (error == NULL) {
        return NULL;
    }
    *error = 0;
    if (submitter_id == NULL || submitter_id[0] == '\0') {
        return NULL;
    }
    element = pcore_custom_validity_element_by_id(doc, submitter_id);
    owner = NULL;
    if (element == NULL || pcore_form_control_type((dom_node *) element) !=
            GADGET_SUBMIT) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        *error = 1;
        return NULL;
    }
    disabled = false;
    if (pcore_node_effectively_disabled((dom_node *) element, NULL,
            &disabled) != 0 || disabled) {
        dom_node_unref((dom_node *) element);
        *error = 1;
        return NULL;
    }
    has_attribute = 0;
    result = pcore_form_control_owner(doc, (dom_node *) element, &owner,
            &has_attribute);
    (void) has_attribute;
    if (result != 0 || owner != (dom_element *) form) {
        if (owner != NULL) {
            dom_node_unref((dom_node *) owner);
        }
        dom_node_unref((dom_node *) element);
        *error = 1;
        return NULL;
    }
    dom_node_unref((dom_node *) owner);
    return (dom_node *) element;
}

static int pcore_custom_validity_control_supported(dom_node *node)
{
    int gadget_type;

    gadget_type = pcore_form_control_type(node);
    return gadget_type != 0 && gadget_type != GADGET_SUBMIT &&
            gadget_type != GADGET_RESET && gadget_type != GADGET_BUTTON;
}

PCORE_API int PCore_FormSetCustomValidityById(HANDLE hDoc,
        const char *element_id, const char *message)
{
    dom_element *element;
    int result;

    element = pcore_custom_validity_element_by_id(
            (dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_custom_validity_control_supported(
            (dom_node *) element)) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    result = pcore_custom_validity_set((dom_document *) hDoc,
            (dom_node *) element, message);
    dom_node_unref((dom_node *) element);
    return result == 0 ? 0 : 1;
}

PCORE_API int PCore_FormGetCustomValidityById(HANDLE hDoc,
        const char *element_id, char *message, unsigned int capacity)
{
    dom_element *element;
    int result;

    if (message == NULL && capacity != 0) {
        return -1;
    }
    element = pcore_custom_validity_element_by_id(
            (dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_custom_validity_control_supported(
            (dom_node *) element)) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return -1;
    }
    result = pcore_custom_validity_get((dom_document *) hDoc,
            (dom_node *) element, message, capacity);
    dom_node_unref((dom_node *) element);
    return result;
}

static int pcore_validation_message_copy(const char *source,
        char *message, unsigned int capacity)
{
    size_t length;
    size_t copy_length;

    if (source == NULL || (message == NULL && capacity != 0)) {
        return -1;
    }
    length = strlen(source);
    if (length > (size_t) INT_MAX) {
        return -1;
    }
    if (message != NULL && capacity > 0) {
        copy_length = length;
        if (copy_length >= (size_t) capacity) {
            copy_length = (size_t) capacity - 1;
        }
        if (copy_length > 0) {
            memcpy(message, source, copy_length);
        }
        message[copy_length] = '\0';
    }
    return (int) length;
}

static const char *pcore_validation_message_for_flags(unsigned int flags)
{
    if ((flags & PCORE_VALIDITY_VALUE_MISSING) != 0) {
        return "Please fill out this field.";
    }
    if ((flags & PCORE_VALIDITY_TYPE_MISMATCH) != 0) {
        return "Please enter a valid value.";
    }
    if ((flags & PCORE_VALIDITY_BAD_INPUT) != 0) {
        return "Please enter a number.";
    }
    if ((flags & PCORE_VALIDITY_RANGE_UNDERFLOW) != 0) {
        return "Value is too low.";
    }
    if ((flags & PCORE_VALIDITY_RANGE_OVERFLOW) != 0) {
        return "Value is too high.";
    }
    if ((flags & PCORE_VALIDITY_STEP_MISMATCH) != 0) {
        return "Please enter a valid value.";
    }
    if ((flags & PCORE_VALIDITY_TOO_LONG) != 0) {
        return "Please shorten this text.";
    }
    if ((flags & PCORE_VALIDITY_TOO_SHORT) != 0) {
        return "Please lengthen this text.";
    }
    if ((flags & PCORE_VALIDITY_PATTERN_MISMATCH) != 0) {
        return "Please match the requested format.";
    }
    if (flags != 0) {
        return "Please fix this field.";
    }
    return "";
}

PCORE_API int PCore_FormGetValidationMessageById(HANDLE hDoc,
        const char *element_id, char *message, unsigned int capacity)
{
    PCoreFormControlValidationInfo info;
    const char *fallback;

    if (message == NULL && capacity != 0) {
        return -1;
    }
    if (PCore_FormControlValidationById(hDoc, element_id, &info) != 0) {
        return -1;
    }
    if (!info.will_validate || info.valid) {
        return pcore_validation_message_copy("", message, capacity);
    }
    if ((info.flags & PCORE_VALIDITY_CUSTOM_ERROR) != 0) {
        return PCore_FormGetCustomValidityById(hDoc, element_id, message,
                capacity);
    }
    fallback = pcore_validation_message_for_flags(info.flags);
    return pcore_validation_message_copy(fallback, message, capacity);
}

PCORE_API int PCore_FormControlValidationById(HANDLE hDoc,
        const char *element_id, PCoreFormControlValidationInfo *out_info)
{
    dom_document *doc;
    dom_string *id;
    dom_element *element;
    dom_html_form_element *form;
    int gadget_type;
    int kind;
    int missing;
    int will_validate;
    bool checked;
    unsigned int flags;
    bool is_required;

    if (out_info == NULL || hDoc == NULL || element_id == NULL ||
            element_id[0] == '\0') {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    out_info->valid = 1;
    doc = (dom_document *) hDoc;
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id,
            strlen(element_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR ||
            element == NULL) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    dom_string_unref(id);
    gadget_type = pcore_form_control_type((dom_node *) element);
    out_info->kind = pcore_public_control_kind(gadget_type);
    if (gadget_type == 0 || !pcore_control_will_validate(
            (dom_node *) element, gadget_type, &will_validate)) {
        dom_node_unref((dom_node *) element);
        return (gadget_type == 0) ? 0 : 1;
    }
    out_info->will_validate = will_validate;
    if (!will_validate) {
        dom_node_unref((dom_node *) element);
        return 0;
    }
    form = pcore_form_for_node((dom_node *) element);
    kind = 0;
    missing = 0;
    flags = 0;
    if (form == NULL && gadget_type == GADGET_RADIO) {
        is_required = pcore_node_has_attr((dom_node *) element,
                "required");
        checked = false;
        if (dom_html_input_element_get_checked(
                (dom_html_input_element *) element, &checked) !=
                        DOM_NO_ERR) {
            dom_node_unref((dom_node *) element);
            return 1;
        }
        if (is_required && !checked) {
            flags = PCORE_VALIDITY_VALUE_MISSING;
        }
        if (pcore_custom_validity_has_error((dom_node *) element)) {
            flags |= PCORE_VALIDITY_CUSTOM_ERROR;
        }
        missing = (flags != 0) ? 1 : 0;
    } else if (!pcore_required_control_missing(form,
            (dom_node *) element, &kind, &missing, &flags)) {
        if (form != NULL) {
            dom_node_unref((dom_node *) form);
        }
        dom_node_unref((dom_node *) element);
        return 1;
    }
    out_info->flags = flags;
    out_info->valid = (missing || flags != 0) ? 0 : 1;
    if (form != NULL) {
        dom_node_unref((dom_node *) form);
    }
    dom_node_unref((dom_node *) element);
    return 0;
}

typedef struct pcore_form_validation_context {
    dom_html_form_element *form;
    pcore_render *st;
    PCoreFormValidationInfo *info;
} pcore_form_validation_context;

static int pcore_form_validation_visit(dom_node *node, void *pw)
{
    pcore_form_validation_context *context;
    struct box *box;
    int kind;
    int missing;
    unsigned int flags;
    int x;
    int y;

    context = (pcore_form_validation_context *) pw;
    if (context == NULL || context->form == NULL ||
            context->info == NULL || node == NULL) {
        return -1;
    }
    kind = 0;
    missing = 0;
    flags = 0;
    if (!pcore_required_control_missing(context->form, node, &kind,
            &missing, &flags)) {
        return -1;
    }
    if (missing || flags != 0) {
        context->info->valid = 0;
        context->info->invalid_count++;
        if (context->info->invalid_count == 1) {
            context->info->first_control_kind = kind;
            context->info->first_flags = flags;
            if (context->st != NULL) {
                box = pcore_box_for_node(context->st->root_box, node);
                if (box != NULL) {
                    x = 0;
                    y = 0;
                    box_coords(box, &x, &y);
                    context->info->first_x = x;
                    context->info->first_y = y;
                    context->info->first_width = box->width;
                    context->info->first_height = box->height;
                }
            }
        }
    }
    return 0;
}

PCORE_API int PCore_FormValidationById(HANDLE hDoc, const char *form_id,
        PCoreFormValidationInfo *out_info)
{
    dom_element *element;
    dom_html_form_element *form;
    pcore_form_validation_context context;
    int result;

    if (out_info == NULL || hDoc == NULL || form_id == NULL ||
            form_id[0] == '\0') {
        return 1;
    }
    pcore_form_validation_init(out_info);
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    form = (dom_html_form_element *) element;
    context.form = form;
    context.st = NULL;
    context.info = out_info;
    result = pcore_form_controls_visit((dom_document *) hDoc, element,
            pcore_form_validation_visit, &context);
    dom_node_unref((dom_node *) element);
    return result == 0 ? 0 : 1;
}

PCORE_API int PCore_FormValidationSubmitById(HANDLE hDoc,
        const char *form_id, const char *submitter_id,
        PCoreFormValidationInfo *out_info)
{
    dom_element *element;
    dom_node *submitter;
    dom_html_form_element *form;
    pcore_render *st;
    int submitter_error;
    int result;

    if (out_info == NULL || hDoc == NULL || form_id == NULL ||
            form_id[0] == '\0') {
        return 1;
    }
    pcore_form_validation_init(out_info);
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    form = (dom_html_form_element *) element;
    submitter_error = 0;
    submitter = pcore_form_submitter_by_id((dom_document *) hDoc, form,
            submitter_id, &submitter_error);
    if (submitter_error) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    st = pcore_get_render((dom_document *) hDoc);
    result = pcore_form_validate(st, form, submitter, out_info);
    if (submitter != NULL) {
        dom_node_unref(submitter);
    }
    dom_node_unref((dom_node *) element);
    return result ? 0 : 1;
}

typedef struct pcore_form_report_context {
    dom_html_form_element *form;
    char **invalid_ids;
    size_t capacity;
    uint32_t invalid_id_count;
} pcore_form_report_context;

static int pcore_form_report_visit(dom_node *node, void *pw)
{
    pcore_form_report_context *context;
    dom_string *node_id;
    int kind;
    int missing;
    unsigned int flags;

    context = (pcore_form_report_context *) pw;
    if (context == NULL || context->form == NULL ||
            context->invalid_ids == NULL || node == NULL) {
        return -1;
    }
    kind = 0;
    missing = 0;
    flags = 0;
    if (!pcore_required_control_missing(context->form, node, &kind,
            &missing, &flags)) {
        return -1;
    }
    if (!missing && flags == 0) {
        return 0;
    }
    node_id = pcore_event_element_id((dom_event_target *) node);
    if (node_id != NULL && dom_string_byte_length(node_id) > 0) {
        if ((size_t) context->invalid_id_count >= context->capacity) {
            dom_string_unref(node_id);
            return -1;
        }
        dom_string_unref(node_id);
        context->invalid_ids[context->invalid_id_count] =
                pcore_event_element_id_copy(node);
        if (context->invalid_ids[context->invalid_id_count] == NULL) {
            return -1;
        }
        context->invalid_id_count++;
    } else if (node_id != NULL) {
        dom_string_unref(node_id);
    }
    return 0;
}

PCORE_API int PCore_FormReportValidityById(HANDLE hDoc, const char *form_id,
        PCoreFormValidationInfo *out_info)
{
    dom_element *element;
    dom_html_form_element *form;
    char **invalid_ids;
    uint32_t index;
    size_t alloc_count;
    pcore_form_report_context context;
    int default_allowed;
    int dispatch_result;
    int result;

    if (out_info == NULL || hDoc == NULL || form_id == NULL ||
            form_id[0] == '\0') {
        return 1;
    }
    if (PCore_FormValidationById(hDoc, form_id, out_info) != 0) {
        return 1;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    form = (dom_html_form_element *) element;
    alloc_count = out_info->invalid_count > 0 ?
            (size_t) out_info->invalid_count : 1;
    if (alloc_count == (size_t) -1 ||
            alloc_count + 1 > (size_t) -1 / sizeof(*invalid_ids)) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    invalid_ids = (char **) calloc(alloc_count + 1,
            sizeof(*invalid_ids));
    if (invalid_ids == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    context.form = form;
    context.invalid_ids = invalid_ids;
    context.capacity = alloc_count;
    context.invalid_id_count = 0;
    result = pcore_form_controls_visit((dom_document *) hDoc, element,
            pcore_form_report_visit, &context);
    dom_node_unref((dom_node *) element);
    if (result != 0) {
        for (index = 0; index < context.invalid_id_count; index++) {
            free(invalid_ids[index]);
        }
        free(invalid_ids);
        return 1;
    }
    for (index = 0; index < context.invalid_id_count; index++) {
        default_allowed = 1;
        dispatch_result = PCore_EventDispatchToId(hDoc, invalid_ids[index],
                "invalid", 0, 1, &default_allowed);
        if (dispatch_result < 0) {
            for (; index < context.invalid_id_count; index++) {
                free(invalid_ids[index]);
            }
            free(invalid_ids);
            return 1;
        }
        free(invalid_ids[index]);
    }
    free(invalid_ids);
    return 0;
}

static int pcore_form_validate(pcore_render *st,
        dom_html_form_element *form, dom_node *activated,
        PCoreFormValidationInfo *out_info)
{
    PCoreFormValidationInfo local_info;
    PCoreFormValidationInfo *info;
    dom_document *doc;
    pcore_form_validation_context context;
    int result;

    info = (out_info != NULL) ? out_info : &local_info;
    pcore_form_validation_init(info);
    /* Validation itself is DOM-only. A NULL render state simply omits the
     * optional geometry for the first invalid control, which lets script and
     * by-id submission paths run before the first layout. */
    if (form == NULL) {
        return 0;
    }
    if (pcore_node_has_attr((dom_node *) form, "novalidate") ||
            (activated != NULL &&
             pcore_node_has_attr(activated, "formnovalidate"))) {
        return 1;
    }
    doc = NULL;
    if (dom_node_get_owner_document((dom_node *) form, &doc) !=
            DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return 0;
    }
    context.form = form;
    context.st = st;
    context.info = info;
    result = pcore_form_controls_visit(doc, (dom_element *) form,
            pcore_form_validation_visit, &context);
    dom_node_unref((dom_node *) doc);
    return result == 0 ? 1 : 0;
}

static int pcore_form_validate_default(pcore_render *st,
        dom_html_form_element *form, PCoreFormValidationInfo *out_info)
{
    dom_node *default_submit;
    int error;
    int result;

    error = 0;
    default_submit = pcore_form_first_submit(form, &error);
    if (error) {
        return 0;
    }
    result = pcore_form_validate(st, form, default_submit, out_info);
    if (default_submit != NULL) {
        dom_node_unref(default_submit);
    }
    return result;
}

PCORE_API int PCore_FormValidationAt(HANDLE hDoc, int x, int y,
        PCoreFormValidationInfo *out_info)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    int result;
    bool effective_disabled;

    pcore_form_validation_init(out_info);
    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* Resolve submit-button text to its gadget-bearing ancestor. */
    }
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->type != GADGET_SUBMIT) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 0;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 0;
    }
    result = pcore_form_validate(st, form, control->node, out_info);
    dom_node_unref((dom_node *) form);
    return result;
}

PCORE_API int PCore_FormValidationForTextInput(HANDLE hDoc,
        unsigned int text_index, PCoreFormValidationInfo *out_info)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    unsigned int current;
    int result;
    bool effective_disabled;

    pcore_form_validation_init(out_info);
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
             control->type != GADGET_PASSWORD)) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 0;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 0;
    }
    result = pcore_form_validate_default(st, form, out_info);
    dom_node_unref((dom_node *) form);
    return result;
}

PCORE_API int PCore_FormSetCustomValidityForTextInput(HANDLE hDoc,
        unsigned int text_index, const char *message)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    unsigned int current;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->node == NULL ||
            (control->type != GADGET_TEXTBOX &&
             control->type != GADGET_PASSWORD)) {
        return -1;
    }
    return pcore_custom_validity_set((dom_document *) hDoc,
            control->node, message);
}

PCORE_API int PCore_FormSetCustomValidityForTextarea(HANDLE hDoc,
        unsigned int textarea_index, const char *message)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    unsigned int current;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, textarea_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->node == NULL ||
            control->type != GADGET_TEXTAREA) {
        return -1;
    }
    return pcore_custom_validity_set((dom_document *) hDoc,
            control->node, message);
}

PCORE_API int PCore_FormGetCustomValidityForTextInput(HANDLE hDoc,
        unsigned int text_index, char *message, unsigned int capacity)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    unsigned int current;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->node == NULL ||
            (control->type != GADGET_TEXTBOX &&
             control->type != GADGET_PASSWORD)) {
        return -1;
    }
    return pcore_custom_validity_get((dom_document *) hDoc,
            control->node, message, capacity);
}

PCORE_API int PCore_FormGetCustomValidityForTextarea(HANDLE hDoc,
        unsigned int textarea_index, char *message, unsigned int capacity)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    unsigned int current;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, textarea_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->node == NULL ||
            control->type != GADGET_TEXTAREA) {
        return -1;
    }
    return pcore_custom_validity_get((dom_document *) hDoc,
            control->node, message, capacity);
}

typedef struct pcore_multipart_part {
    struct pcore_multipart_part *next;
    char *name;
    char *value;
    char *path;
    int kind;
} pcore_multipart_part;

typedef struct pcore_multipart_submission {
    char *action;
    pcore_multipart_part *first;
    pcore_multipart_part *last;
    unsigned int part_count;
} pcore_multipart_submission;

static char *pcore_dom_string_copy(dom_string *value,
        const char *default_value)
{
    const char *data;
    size_t length;
    char *copy;

    if (value != NULL) {
        data = dom_string_data(value);
        length = dom_string_byte_length(value);
    } else {
        data = (default_value != NULL) ? default_value : "";
        length = strlen(data);
    }
    if (length > INT_MAX) {
        return NULL;
    }
    copy = (char *) malloc(length + 1);
    if (copy != NULL) {
        if (length > 0) {
            memcpy(copy, data, length);
        }
        copy[length] = '\0';
    }
    return copy;
}

static void pcore_multipart_free(pcore_multipart_submission *submission)
{
    pcore_multipart_part *part;

    if (submission == NULL) {
        return;
    }
    part = submission->first;
    while (part != NULL) {
        pcore_multipart_part *next;

        next = part->next;
        free(part->name);
        free(part->value);
        free(part->path);
        free(part);
        part = next;
    }
    free(submission->action);
    free(submission);
}

static int pcore_multipart_append(pcore_multipart_submission *submission,
        dom_string *name, dom_string *value, const char *default_value,
        const char *path, int kind)
{
    pcore_multipart_part *part;

    if (submission == NULL || name == NULL ||
            submission->part_count == UINT_MAX) {
        return 0;
    }
    part = (pcore_multipart_part *) calloc(1, sizeof(*part));
    if (part == NULL) {
        return 0;
    }
    part->name = pcore_dom_string_copy(name, "");
    part->value = pcore_dom_string_copy(value, default_value);
    part->path = pcore_heap_string((path != NULL) ? path : "");
    part->kind = kind;
    if (part->name == NULL || part->value == NULL ||
            part->path == NULL) {
        free(part->name);
        free(part->value);
        free(part->path);
        free(part);
        return 0;
    }
    if (submission->last != NULL) {
        submission->last->next = part;
    } else {
        submission->first = part;
    }
    submission->last = part;
    submission->part_count++;
    return 1;
}

static int pcore_multipart_append_input(
        pcore_multipart_submission *submission,
        dom_html_input_element *input, dom_node *activated)
{
    dom_node *node;
    dom_string *name;
    dom_string *value;
    bool disabled;
    bool checked;
    void *stored_path;
    int result;

    node = (dom_node *) input;
    name = NULL;
    value = NULL;
    disabled = false;
    checked = false;
    stored_path = NULL;
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0 ||
            disabled) {
        return disabled ? 1 : 0;
    }
    if (dom_html_input_element_get_name(input, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (pcore_attr_value_is(node, "type", "reset") ||
            pcore_attr_value_is(node, "type", "button") ||
            pcore_attr_value_is(node, "type", "image") ||
            (pcore_attr_value_is(node, "type", "submit") &&
             node != activated)) {
        dom_string_unref(name);
        return 1;
    }
    if (pcore_attr_value_is(node, "type", "checkbox") ||
            pcore_attr_value_is(node, "type", "radio")) {
        if (dom_html_input_element_get_checked(input, &checked) !=
                DOM_NO_ERR) {
            dom_string_unref(name);
            return 0;
        }
        if (!checked) {
            dom_string_unref(name);
            return 1;
        }
    }
    if (dom_html_input_element_get_value(input, &value) != DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    if (!pcore_range_fill_default(node, &value)) {
        dom_string_unref(name);
        return 0;
    }
    if (pcore_attr_value_is(node, "type", "file")) {
        if (corestring_dom___ns_key_file_name_node_data == NULL ||
                dom_node_get_user_data(node,
                        corestring_dom___ns_key_file_name_node_data,
                        &stored_path) != DOM_NO_ERR) {
            result = 0;
        } else {
            result = pcore_multipart_append(submission, name, value, "",
                    (stored_path != NULL) ?
                            (const char *) stored_path : "", 2);
        }
    } else {
        result = pcore_multipart_append(submission, name, value,
                (pcore_attr_value_is(node, "type", "checkbox") ||
                 pcore_attr_value_is(node, "type", "radio")) ?
                        "on" : "", "", 1);
    }
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_multipart_append_textarea(
        pcore_multipart_submission *submission,
        dom_html_text_area_element *textarea)
{
    dom_string *name;
    dom_string *value;
    bool disabled;
    int result;

    name = NULL;
    value = NULL;
    disabled = false;
    if (pcore_node_effectively_disabled((dom_node *) textarea, NULL,
            &disabled) != 0 || disabled) {
        return disabled ? 1 : 0;
    }
    if (dom_html_text_area_element_get_name(textarea, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (dom_html_text_area_element_get_value(textarea, &value) !=
            DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    result = pcore_multipart_append(submission, name, value, "", "", 1);
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

static int pcore_multipart_append_select(
        pcore_multipart_submission *submission,
        dom_html_select_element *select)
{
    dom_html_options_collection *options;
    dom_string *name;
    dom_node *node;
    dom_string *value;
    uint32_t count;
    uint32_t index;
    bool disabled;
    bool option_disabled;
    bool selected;
    int result;

    options = NULL;
    name = NULL;
    node = NULL;
    value = NULL;
    count = 0;
    disabled = false;
    option_disabled = false;
    if (pcore_node_effectively_disabled((dom_node *) select, NULL,
            &disabled) != 0 || disabled) {
        return disabled ? 1 : 0;
    }
    if (dom_html_select_element_get_name(select, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (dom_html_select_element_get_options(select, &options) != DOM_NO_ERR ||
            options == NULL ||
            dom_html_options_collection_get_length(options, &count) !=
                    DOM_NO_ERR) {
        if (options != NULL) {
            dom_html_options_collection_unref(options);
        }
        dom_string_unref(name);
        return 0;
    }
    result = 1;
    for (index = 0; index < count && result; index++) {
        node = NULL;
        value = NULL;
        selected = false;
        if (dom_html_options_collection_item(options, index, &node) !=
                DOM_NO_ERR || node == NULL ||
                dom_html_option_element_get_selected(
                        (dom_html_option_element *) node,
                        &selected) != DOM_NO_ERR) {
            result = 0;
        } else if (selected) {
            if (pcore_node_effectively_disabled(node, NULL,
                    &option_disabled) != 0) {
                result = 0;
            } else if (option_disabled) {
                result = 1;
            } else if (dom_html_option_element_get_value(
                    (dom_html_option_element *) node, &value) !=
                    DOM_NO_ERR) {
                result = 0;
            } else {
                result = pcore_multipart_append(submission, name, value,
                        "", "", 1);
            }
        }
        if (value != NULL) {
            dom_string_unref(value);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
    }
    dom_html_options_collection_unref(options);
    dom_string_unref(name);
    return result;
}

static int pcore_multipart_append_button(
        pcore_multipart_submission *submission,
        dom_html_button_element *button, dom_node *activated)
{
    dom_node *node;
    dom_string *name;
    dom_string *value;
    bool disabled;
    int result;

    node = (dom_node *) button;
    name = NULL;
    value = NULL;
    disabled = false;
    if (pcore_node_effectively_disabled(node, NULL, &disabled) != 0 ||
            disabled) {
        return disabled ? 1 : 0;
    }
    if (pcore_attr_value_is(node, "type", "reset") ||
            pcore_attr_value_is(node, "type", "button") ||
            node != activated) {
        return 1;
    }
    if (dom_html_button_element_get_name(button, &name) != DOM_NO_ERR) {
        return 0;
    }
    if (name == NULL) {
        return 1;
    }
    if (dom_html_button_element_get_value(button, &value) != DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    result = pcore_multipart_append(submission, name, value, "", "", 1);
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    return result;
}

typedef struct pcore_multipart_parts_context {
    dom_node *activated;
    pcore_multipart_submission *submission;
} pcore_multipart_parts_context;

static int pcore_multipart_parts_visit(dom_node *node, void *pw)
{
    pcore_multipart_parts_context *context;
    int result;

    context = (pcore_multipart_parts_context *) pw;
    if (context == NULL || context->submission == NULL || node == NULL) {
        return -1;
    }
    result = 1;
    if (pcore_node_name_is(node, "input")) {
        result = pcore_multipart_append_input(context->submission,
                (dom_html_input_element *) node, context->activated);
    } else if (pcore_node_name_is(node, "textarea")) {
        result = pcore_multipart_append_textarea(context->submission,
                (dom_html_text_area_element *) node);
    } else if (pcore_node_name_is(node, "select")) {
        result = pcore_multipart_append_select(context->submission,
                (dom_html_select_element *) node);
    } else if (pcore_node_name_is(node, "button")) {
        result = pcore_multipart_append_button(context->submission,
                (dom_html_button_element *) node, context->activated);
    }
    return result ? 0 : -1;
}

static int pcore_multipart_build_parts(dom_html_form_element *form,
        dom_node *activated, pcore_multipart_submission *submission)
{
    dom_document *doc;
    pcore_multipart_parts_context context;
    int result;

    doc = NULL;
    if (form == NULL || submission == NULL ||
            dom_node_get_owner_document((dom_node *) form, &doc) !=
                    DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return 0;
    }
    context.activated = activated;
    context.submission = submission;
    result = pcore_form_controls_visit(doc, (dom_element *) form,
            pcore_multipart_parts_visit, &context);
    dom_node_unref((dom_node *) doc);
    return result == 0;
}

static dom_string *pcore_form_submission_action(
        dom_html_form_element *form, dom_node *activated);
static int pcore_form_submission_method(
        dom_html_form_element *form, dom_node *activated);
static int pcore_form_submission_multipart(
        dom_html_form_element *form, dom_node *activated);

static pcore_multipart_submission *pcore_multipart_snapshot(
        pcore_render *st, dom_html_form_element *form, dom_node *activated,
        int choose_default, int validate)
{
    pcore_multipart_submission *submission;
    PCoreFormValidationInfo validation;
    dom_string *action;
    dom_node *default_submit;
    int default_error;

    if (form == NULL) {
        return NULL;
    }
    submission = (pcore_multipart_submission *) calloc(1,
            sizeof(*submission));
    action = NULL;
    default_submit = NULL;
    if (submission == NULL) {
        return NULL;
    }
    if (choose_default) {
        default_error = 0;
        default_submit = pcore_form_first_submit(form, &default_error);
        if (default_error) {
            pcore_multipart_free(submission);
            return NULL;
        }
        activated = default_submit;
    }
    if (pcore_form_submission_method(form, activated) != 2 ||
            !pcore_form_submission_multipart(form, activated)) {
        if (default_submit != NULL) {
            dom_node_unref(default_submit);
        }
        pcore_multipart_free(submission);
        return NULL;
    }
    if (validate && (!pcore_form_validate(st, form, activated, &validation) ||
            !validation.valid)) {
        if (default_submit != NULL) {
            dom_node_unref(default_submit);
        }
        pcore_multipart_free(submission);
        return NULL;
    }
    action = pcore_form_submission_action(form, activated);
    if (action == NULL) {
        if (default_submit != NULL) {
            dom_node_unref(default_submit);
        }
        pcore_multipart_free(submission);
        return NULL;
    }
    submission->action = pcore_dom_string_copy(action, "");
    if (action != NULL) {
        dom_string_unref(action);
    }
    if (submission->action == NULL ||
            !pcore_multipart_build_parts(form, activated, submission)) {
        if (default_submit != NULL) {
            dom_node_unref(default_submit);
        }
        pcore_multipart_free(submission);
        return NULL;
    }
    if (default_submit != NULL) {
        dom_node_unref(default_submit);
    }
    return submission;
}

static dom_string *pcore_form_submission_action(
        dom_html_form_element *form, dom_node *activated)
{
    dom_string *attribute;
    dom_string *action;

    attribute = NULL;
    action = NULL;
    if (activated != NULL && dom_string_create(
            (const uint8_t *) "formaction", 10, &attribute) == DOM_NO_ERR &&
            attribute != NULL) {
        if (dom_element_get_attribute(activated, attribute, &action) ==
                DOM_NO_ERR && action != NULL) {
            dom_string_unref(attribute);
            return action;
        }
        if (action != NULL) {
            dom_string_unref(action);
        }
        dom_string_unref(attribute);
    }
    action = NULL;
    if (form == NULL || dom_html_form_element_get_action(form, &action) !=
            DOM_NO_ERR) {
        return NULL;
    }
    return action;
}

static int pcore_form_submission_method(
        dom_html_form_element *form, dom_node *activated)
{
    if (activated != NULL &&
            pcore_attr_value_is(activated, "formmethod", "dialog")) {
        return PCORE_FORM_METHOD_DIALOG;
    }
    if (activated != NULL &&
            pcore_attr_value_is(activated, "formmethod", "post")) {
        return PCORE_FORM_METHOD_POST;
    }
    if (activated != NULL &&
            pcore_attr_value_is(activated, "formmethod", "get")) {
        return PCORE_FORM_METHOD_GET;
    }
    if (form != NULL && pcore_attr_value_is((dom_node *) form,
            "method", "dialog")) {
        return PCORE_FORM_METHOD_DIALOG;
    }
    return (form != NULL && pcore_attr_value_is((dom_node *) form,
            "method", "post")) ? PCORE_FORM_METHOD_POST :
            PCORE_FORM_METHOD_GET;
}

static int pcore_form_submission_multipart(
        dom_html_form_element *form, dom_node *activated)
{
    if (activated != NULL && pcore_attr_value_is(activated,
            "formenctype", "multipart/form-data")) {
        return 1;
    }
    if (activated != NULL && pcore_attr_value_is(activated,
            "formenctype", "application/x-www-form-urlencoded")) {
        return 0;
    }
    return form != NULL && pcore_attr_value_is((dom_node *) form,
            "enctype", "multipart/form-data");
}

static int pcore_form_submission(pcore_render *st,
        dom_html_form_element *form,
        dom_node *activated, int choose_default, int validate,
        PCoreFormSubmissionInfo *out_info, char *action, int action_capacity,
        char *body, int body_capacity)
{
    dom_string *action_string;
    dom_node *default_submit;
    pcore_form_buffer buffer;
    const char *action_data;
    size_t action_length;
    int default_error;
    int method;
    int result;
    PCoreFormValidationInfo validation;

    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (action != NULL && action_capacity > 0) {
        action[0] = '\0';
    }
    if (body != NULL && body_capacity > 0) {
        body[0] = '\0';
    }
    if (form == NULL) {
        return 0;
    }
    action_string = NULL;
    default_submit = NULL;
    memset(&buffer, 0, sizeof(buffer));
    method = PCORE_FORM_METHOD_GET;
    result = 4;
    if (choose_default) {
        default_error = 0;
        default_submit = pcore_form_first_submit(form, &default_error);
        if (default_error) {
            goto form_submission_done;
        }
        activated = default_submit;
    }
    if (validate && !pcore_form_validate(st, form, activated, &validation)) {
        goto form_submission_done;
    }
    if (validate && !validation.valid) {
        result = 5;
        goto form_submission_done;
    }
    method = pcore_form_submission_method(form, activated);
    if (out_info != NULL) {
        out_info->method = method;
    }
    if (method == PCORE_FORM_METHOD_DIALOG) {
        result = 6;
        goto form_submission_done;
    }
    action_string = pcore_form_submission_action(form, activated);
    if (action_string == NULL) {
        goto form_submission_done;
    }
    if (method == PCORE_FORM_METHOD_POST) {
        if (pcore_form_submission_multipart(form, activated)) {
            method = PCORE_FORM_METHOD_MULTIPART;
        }
    }
    action_data = (action_string != NULL) ?
            dom_string_data(action_string) : "";
    action_length = (action_string != NULL) ?
            dom_string_byte_length(action_string) : 0;
    if (out_info != NULL) {
        out_info->method = method;
        out_info->action_bytes = (int) action_length;
    }
    if (method == PCORE_FORM_METHOD_MULTIPART) {
        result = 3;
        goto form_submission_done;
    }
    if (!pcore_form_build_data(form, activated, &buffer)) {
        goto form_submission_done;
    }
    if (out_info != NULL) {
        out_info->body_bytes = (int) buffer.length;
    }
    if (action == NULL || action_capacity <= (int) action_length ||
            body == NULL || body_capacity <= (int) buffer.length) {
        goto form_submission_done;
    }
    if (action_length > 0) {
        memcpy(action, action_data, action_length);
    }
    action[action_length] = '\0';
    if (buffer.length > 0) {
        memcpy(body, buffer.data, buffer.length);
    }
    body[buffer.length] = '\0';
    result = 1;

form_submission_done:
    free(buffer.data);
    if (default_submit != NULL) {
        dom_node_unref(default_submit);
    }
    if (action_string != NULL) {
        dom_string_unref(action_string);
    }
    return result;
}

PCORE_API int PCore_FormSubmissionAt(HANDLE hDoc, int x, int y,
        PCoreFormSubmissionInfo *out_info, char *action, int action_capacity,
        char *body, int body_capacity)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    int result;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* The button's text child resolves to its gadget-bearing ancestor. */
    }
    if (box == NULL || box->gadget == NULL) {
        return 0;
    }
    control = box->gadget;
    if (control->type != GADGET_SUBMIT &&
            control->type != GADGET_RESET &&
            control->type != GADGET_BUTTON) {
        return 0;
    }
    if (control->type != GADGET_SUBMIT) {
        return 2;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 2;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 2;
    }
    result = pcore_form_submission(st, form, control->node, 0, 1, out_info,
            action, action_capacity, body, body_capacity);
    dom_node_unref((dom_node *) form);
    return result;
}

PCORE_API int PCore_FormSubmissionForTextInput(HANDLE hDoc,
        unsigned int text_index, PCoreFormSubmissionInfo *out_info,
        char *action, int action_capacity, char *body, int body_capacity)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    unsigned int current;
    int result;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
            control->type != GADGET_PASSWORD)) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 0;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 0;
    }
    result = pcore_form_submission(st, form, NULL, 1, 1, out_info,
            action, action_capacity, body, body_capacity);
    dom_node_unref((dom_node *) form);
    return result;
}

PCORE_API int PCore_FormSubmissionById(HANDLE hDoc, const char *form_id,
        const char *submitter_id, PCoreFormSubmissionInfo *out_info,
        char *action, int action_capacity, char *body, int body_capacity)
{
    dom_element *element;
    dom_node *submitter;
    dom_html_form_element *form;
    pcore_render *st;
    int submitter_error;
    int result;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return 0;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 0;
    }
    form = (dom_html_form_element *) element;
    submitter_error = 0;
    submitter = pcore_form_submitter_by_id((dom_document *) hDoc, form,
            submitter_id, &submitter_error);
    if (submitter_error) {
        dom_node_unref((dom_node *) element);
        return 0;
    }
    st = pcore_get_render((dom_document *) hDoc);
    result = pcore_form_submission(st, form, submitter, 0, 1, out_info,
            action, action_capacity, body, body_capacity);
    if (submitter != NULL) {
        dom_node_unref(submitter);
    }
    dom_node_unref((dom_node *) element);
    return result;
}

PCORE_API int PCore_FormSubmissionNoValidationById(HANDLE hDoc,
        const char *form_id, PCoreFormSubmissionInfo *out_info,
        char *action, int action_capacity, char *body, int body_capacity)
{
    dom_element *element;
    dom_html_form_element *form;
    pcore_render *st;
    int result;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return 0;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 0;
    }
    form = (dom_html_form_element *) element;
    st = pcore_get_render((dom_document *) hDoc);
    result = pcore_form_submission(st, form, NULL, 0, 0, out_info,
            action, action_capacity, body, body_capacity);
    dom_node_unref((dom_node *) element);
    return result;
}

static dom_node *pcore_form_dialog_ancestor(dom_html_form_element *form)
{
    dom_node *current;
    dom_node *parent;

    current = (form != NULL) ? dom_node_ref((dom_node *) form) : NULL;
    while (current != NULL) {
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return NULL;
        }
        dom_node_unref(current);
        current = parent;
        if (current != NULL && pcore_node_name_is(current, "dialog")) {
            return current;
        }
    }
    return NULL;
}

static dom_string *pcore_form_submitter_value(dom_node *activated)
{
    dom_string *value;

    value = NULL;
    if (activated == NULL) {
        return NULL;
    }
    if (pcore_node_name_is(activated, "button")) {
        if (dom_html_button_element_get_value(
                (dom_html_button_element *) activated, &value) !=
                DOM_NO_ERR) {
            value = NULL;
        }
    } else if (pcore_node_name_is(activated, "input")) {
        if (dom_html_input_element_get_value(
                (dom_html_input_element *) activated, &value) !=
                DOM_NO_ERR) {
            value = NULL;
        }
    }
    return value;
}

static int pcore_form_dialog_submission(pcore_render *st,
        dom_html_form_element *form, dom_node *activated,
        int choose_default, int validate,
        PCoreDialogFormSubmissionInfo *out_info,
        char *dialog_id, int dialog_id_capacity,
        char *return_value, int return_value_capacity)
{
    PCoreFormValidationInfo validation;
    dom_node *default_submit;
    dom_node *dialog;
    dom_string *id;
    dom_string *value;
    const char *id_data;
    const char *value_data;
    size_t id_length;
    size_t value_length;
    int default_error;
    int result;

    if (dialog_id != NULL && dialog_id_capacity > 0) {
        dialog_id[0] = '\0';
    }
    if (return_value != NULL && return_value_capacity > 0) {
        return_value[0] = '\0';
    }
    if (out_info == NULL || out_info->size < sizeof(*out_info) ||
            dialog_id_capacity < 0 || return_value_capacity < 0 ||
            (dialog_id == NULL && dialog_id_capacity != 0) ||
            (dialog_id != NULL && dialog_id_capacity == 0) ||
            (return_value == NULL && return_value_capacity != 0) ||
            (return_value != NULL && return_value_capacity == 0)) {
        return 4;
    }
    out_info->dialog_id_bytes = 0;
    out_info->return_value_bytes = 0;
    /* Validation and dialog-result extraction are DOM-only. A NULL render
     * state is allowed for scripts that submit during initial bootstrap. */
    if (form == NULL) {
        return 0;
    }
    default_submit = NULL;
    dialog = NULL;
    id = NULL;
    value = NULL;
    result = 4;
    if (choose_default) {
        default_error = 0;
        default_submit = pcore_form_first_submit(form, &default_error);
        if (default_error) {
            goto dialog_submission_done;
        }
        activated = default_submit;
    }
    if (pcore_form_submission_method(form, activated) !=
            PCORE_FORM_METHOD_DIALOG) {
        result = 0;
        goto dialog_submission_done;
    }
    if (validate && !pcore_form_validate(st, form, activated, &validation)) {
        goto dialog_submission_done;
    }
    if (validate && !validation.valid) {
        result = 5;
        goto dialog_submission_done;
    }
    dialog = pcore_form_dialog_ancestor(form);
    if (dialog == NULL) {
        result = 2;
        goto dialog_submission_done;
    }
    id = pcore_event_element_id((dom_event_target *) dialog);
    if (id == NULL || dom_string_byte_length(id) == 0) {
        result = 2;
        goto dialog_submission_done;
    }
    value = pcore_form_submitter_value(activated);
    id_data = dom_string_data(id);
    id_length = dom_string_byte_length(id);
    value_data = (value != NULL) ? dom_string_data(value) : "";
    value_length = (value != NULL) ? dom_string_byte_length(value) : 0;
    if (id_data == NULL || id_length > (size_t) INT_MAX ||
            value_length > (size_t) INT_MAX ||
            (value_length > 0 && value_data == NULL)) {
        goto dialog_submission_done;
    }
    out_info->dialog_id_bytes = (int) id_length;
    out_info->return_value_bytes = (int) value_length;
    if (dialog_id == NULL ||
            dialog_id_capacity <= (int) id_length ||
            return_value == NULL ||
            return_value_capacity <= (int) value_length) {
        goto dialog_submission_done;
    }
    memcpy(dialog_id, id_data, id_length);
    dialog_id[id_length] = '\0';
    if (value_length > 0) {
        memcpy(return_value, value_data, value_length);
    }
    return_value[value_length] = '\0';
    result = 1;

dialog_submission_done:
    if (value != NULL) {
        dom_string_unref(value);
    }
    if (id != NULL) {
        dom_string_unref(id);
    }
    if (dialog != NULL) {
        dom_node_unref(dialog);
    }
    if (default_submit != NULL) {
        dom_node_unref(default_submit);
    }
    return result;
}

PCORE_API int PCore_FormDialogSubmissionAt(HANDLE hDoc, int x, int y,
        PCoreDialogFormSubmissionInfo *out_info,
        char *dialog_id, int dialog_id_capacity,
        char *return_value, int return_value_capacity)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    bool effective_disabled;
    int result;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* Resolve submit-button text to its gadget-bearing ancestor. */
    }
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->type != GADGET_SUBMIT) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 0;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 0;
    }
    result = pcore_form_dialog_submission(st, form, control->node, 0, 1,
            out_info, dialog_id, dialog_id_capacity, return_value,
            return_value_capacity);
    dom_node_unref((dom_node *) form);
    return result;
}

PCORE_API int PCore_FormDialogSubmissionForTextInput(HANDLE hDoc,
        unsigned int text_index, PCoreDialogFormSubmissionInfo *out_info,
        char *dialog_id, int dialog_id_capacity,
        char *return_value, int return_value_capacity)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    unsigned int current;
    bool effective_disabled;
    int result;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
            control->type != GADGET_PASSWORD)) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 0;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 0;
    }
    result = pcore_form_dialog_submission(st, form, NULL, 1, 1, out_info,
            dialog_id, dialog_id_capacity, return_value,
            return_value_capacity);
    dom_node_unref((dom_node *) form);
    return result;
}

PCORE_API int PCore_FormDialogSubmissionById(HANDLE hDoc,
        const char *form_id, const char *submitter_id,
        PCoreDialogFormSubmissionInfo *out_info, char *dialog_id,
        int dialog_id_capacity, char *return_value, int return_value_capacity)
{
    dom_element *element;
    dom_node *submitter;
    dom_html_form_element *form;
    pcore_render *st;
    int submitter_error;
    int result;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return 0;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 0;
    }
    form = (dom_html_form_element *) element;
    submitter_error = 0;
    submitter = pcore_form_submitter_by_id((dom_document *) hDoc, form,
            submitter_id, &submitter_error);
    if (submitter_error) {
        dom_node_unref((dom_node *) element);
        return 0;
    }
    st = pcore_get_render((dom_document *) hDoc);
    result = pcore_form_dialog_submission(st, form, submitter, 0, 1, out_info,
            dialog_id, dialog_id_capacity, return_value,
            return_value_capacity);
    if (submitter != NULL) {
        dom_node_unref(submitter);
    }
    dom_node_unref((dom_node *) element);
    return result;
}

PCORE_API int PCore_FormDialogSubmissionNoValidationById(HANDLE hDoc,
        const char *form_id, PCoreDialogFormSubmissionInfo *out_info,
        char *dialog_id, int dialog_id_capacity, char *return_value,
        int return_value_capacity)
{
    dom_element *element;
    dom_html_form_element *form;
    pcore_render *st;
    int result;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return 0;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 0;
    }
    form = (dom_html_form_element *) element;
    st = pcore_get_render((dom_document *) hDoc);
    result = pcore_form_dialog_submission(st, form, NULL, 0, 0, out_info,
            dialog_id, dialog_id_capacity, return_value,
            return_value_capacity);
    dom_node_unref((dom_node *) element);
    return result;
}

PCORE_API HANDLE PCore_MultipartSubmissionAt(HANDLE hDoc, int x, int y)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    pcore_multipart_submission *submission;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return NULL;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* Resolve a submit button's text child to the gadget box. */
    }
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->type != GADGET_SUBMIT) {
        return NULL;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return NULL;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return NULL;
    }
    submission = pcore_multipart_snapshot(st, form, control->node, 0, 1);
    dom_node_unref((dom_node *) form);
    return (HANDLE) submission;
}

PCORE_API HANDLE PCore_MultipartSubmissionForTextInput(HANDLE hDoc,
        unsigned int text_index)
{
    pcore_render *st;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    pcore_multipart_submission *submission;
    unsigned int current;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
            control->type != GADGET_PASSWORD)) {
        return NULL;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return NULL;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return NULL;
    }
    submission = pcore_multipart_snapshot(st, form, NULL, 1, 1);
    dom_node_unref((dom_node *) form);
    return (HANDLE) submission;
}

PCORE_API HANDLE PCore_MultipartSubmissionById(HANDLE hDoc,
        const char *form_id, const char *submitter_id)
{
    dom_element *element;
    dom_node *submitter;
    dom_html_form_element *form;
    pcore_multipart_submission *submission;
    pcore_render *st;
    int submitter_error;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return NULL;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return NULL;
    }
    form = (dom_html_form_element *) element;
    submitter_error = 0;
    submitter = pcore_form_submitter_by_id((dom_document *) hDoc, form,
            submitter_id, &submitter_error);
    if (submitter_error) {
        dom_node_unref((dom_node *) element);
        return NULL;
    }
    st = pcore_get_render((dom_document *) hDoc);
    submission = pcore_multipart_snapshot(st, form, submitter, 0, 1);
    if (submitter != NULL) {
        dom_node_unref(submitter);
    }
    dom_node_unref((dom_node *) element);
    return (HANDLE) submission;
}

PCORE_API HANDLE PCore_MultipartSubmissionNoValidationById(HANDLE hDoc,
        const char *form_id)
{
    dom_element *element;
    dom_html_form_element *form;
    pcore_render *st;
    pcore_multipart_submission *submission;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return NULL;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return NULL;
    }
    form = (dom_html_form_element *) element;
    st = pcore_get_render((dom_document *) hDoc);
    submission = pcore_multipart_snapshot(st, form, NULL, 0, 0);
    dom_node_unref((dom_node *) element);
    return (HANDLE) submission;
}

PCORE_API int PCore_MultipartSubmissionInfo(HANDLE hSubmission,
        PCoreMultipartSubmissionInfo *out_info,
        char *action, int action_capacity)
{
    pcore_multipart_submission *submission;
    size_t action_length;

    submission = (pcore_multipart_submission *) hSubmission;
    if (submission == NULL || submission->action == NULL) {
        return 0;
    }
    action_length = strlen(submission->action);
    if (action_length > INT_MAX) {
        return 0;
    }
    if (out_info != NULL) {
        out_info->action_bytes = (int) action_length;
        out_info->part_count = submission->part_count;
    }
    if (action != NULL && action_capacity > 0) {
        if ((size_t) action_capacity <= action_length) {
            action[0] = '\0';
            return 2;
        }
        memcpy(action, submission->action, action_length + 1);
    }
    return 1;
}

PCORE_API int PCore_MultipartPartInfo(HANDLE hSubmission,
        unsigned int part_index, PCoreMultipartPartInfo *out_info,
        char *name, int name_capacity, char *value, int value_capacity,
        char *path, int path_capacity)
{
    pcore_multipart_submission *submission;
    pcore_multipart_part *part;
    unsigned int current;
    size_t name_length;
    size_t value_length;
    size_t path_length;

    submission = (pcore_multipart_submission *) hSubmission;
    if (submission == NULL) {
        return 0;
    }
    part = submission->first;
    current = 0;
    while (part != NULL && current < part_index) {
        part = part->next;
        current++;
    }
    if (part == NULL) {
        return 0;
    }
    name_length = strlen(part->name);
    value_length = strlen(part->value);
    path_length = strlen(part->path);
    if (name_length > INT_MAX || value_length > INT_MAX ||
            path_length > INT_MAX) {
        return 0;
    }
    if (out_info != NULL) {
        out_info->kind = part->kind;
        out_info->name_bytes = (int) name_length;
        out_info->value_bytes = (int) value_length;
        out_info->path_bytes = (int) path_length;
    }
    if (name != NULL && name_capacity > 0) {
        if ((size_t) name_capacity <= name_length) {
            name[0] = '\0';
            return 2;
        }
        memcpy(name, part->name, name_length + 1);
    }
    if (value != NULL && value_capacity > 0) {
        if ((size_t) value_capacity <= value_length) {
            value[0] = '\0';
            return 2;
        }
        memcpy(value, part->value, value_length + 1);
    }
    if (path != NULL && path_capacity > 0) {
        if ((size_t) path_capacity <= path_length) {
            path[0] = '\0';
            return 2;
        }
        memcpy(path, part->path, path_length + 1);
    }
    return 1;
}

PCORE_API void PCore_FreeMultipartSubmission(HANDLE hSubmission)
{
    pcore_multipart_free((pcore_multipart_submission *) hSubmission);
}

/* FormData(form[, submitter]) needs the same successful-control collector as
 * multipart submission, but it is independent of method/enctype, validation
 * and event dispatch. The optional activated node is already validated by the
 * caller and only affects successful submit-control inclusion. Keep the
 * returned object in the existing private part representation so the two
 * public snapshot families cannot drift apart. */
static pcore_multipart_submission *pcore_form_data_snapshot(
        dom_html_form_element *form, dom_node *activated)
{
    pcore_multipart_submission *submission;

    if (form == NULL) {
        return NULL;
    }
    submission = (pcore_multipart_submission *) calloc(1,
            sizeof(*submission));
    if (submission == NULL) {
        return NULL;
    }
    submission->action = pcore_heap_string("");
    if (submission->action == NULL ||
            !pcore_multipart_build_parts(form, activated, submission)) {
        pcore_multipart_free(submission);
        return NULL;
    }
    return submission;
}

static HANDLE pcore_form_data_by_id(HANDLE hDoc, const char *form_id,
        const char *submitter_id)
{
    dom_element *element;
    dom_node *submitter;
    dom_html_form_element *form;
    pcore_multipart_submission *submission;
    int submitter_error;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return NULL;
    }
    element = pcore_custom_validity_element_by_id((dom_document *) hDoc,
            form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return NULL;
    }
    form = (dom_html_form_element *) element;
    submitter_error = 0;
    submitter = pcore_form_submitter_by_id((dom_document *) hDoc, form,
            submitter_id, &submitter_error);
    if (submitter_error) {
        dom_node_unref((dom_node *) element);
        return NULL;
    }
    submission = pcore_form_data_snapshot(form, submitter);
    if (submitter != NULL) {
        dom_node_unref(submitter);
    }
    dom_node_unref((dom_node *) element);
    return (HANDLE) submission;
}

PCORE_API HANDLE PCore_FormDataById(HANDLE hDoc, const char *form_id)
{
    return pcore_form_data_by_id(hDoc, form_id, NULL);
}

PCORE_API HANDLE PCore_FormDataByIdEx(HANDLE hDoc, const char *form_id,
        const char *submitter_id)
{
    return pcore_form_data_by_id(hDoc, form_id, submitter_id);
}

PCORE_API int PCore_FormDataInfo(HANDLE hFormData,
        PCoreFormDataInfo *out_info)
{
    pcore_multipart_submission *submission;

    submission = (pcore_multipart_submission *) hFormData;
    if (submission == NULL || submission->action == NULL) {
        return 0;
    }
    if (out_info != NULL) {
        out_info->entry_count = submission->part_count;
    }
    return 1;
}

PCORE_API int PCore_FormDataEntryInfo(HANDLE hFormData,
        unsigned int entry_index, PCoreFormDataEntryInfo *out_info,
        char *name, int name_capacity, char *value, int value_capacity)
{
    PCoreMultipartPartInfo multipart_info;
    int result;

    memset(&multipart_info, 0, sizeof(multipart_info));
    result = PCore_MultipartPartInfo(hFormData, entry_index,
            &multipart_info, name, name_capacity, value, value_capacity,
            NULL, 0);
    if (out_info != NULL) {
        out_info->kind = multipart_info.kind;
        out_info->name_bytes = multipart_info.name_bytes;
        out_info->value_bytes = multipart_info.value_bytes;
    }
    return result;
}

PCORE_API void PCore_FreeFormData(HANDLE hFormData)
{
    pcore_multipart_free((pcore_multipart_submission *) hFormData);
}

static int pcore_form_reset_input(dom_html_input_element *input)
{
    dom_string *default_value;
    dom_string *empty;
    bool default_checked;
    char *old_path;
    int is_file;
    int result;

    default_value = NULL;
    empty = NULL;
    default_checked = false;
    old_path = NULL;
    is_file = pcore_attr_value_is((dom_node *) input, "type", "file");
    result = 0;
    if (is_file) {
        if (dom_string_create((const uint8_t *) "", 0, &empty) !=
                DOM_NO_ERR ||
                dom_html_input_element_set_value(input, empty) !=
                        DOM_NO_ERR ||
                corestring_dom___ns_key_file_name_node_data == NULL ||
                dom_node_set_user_data((dom_node *) input,
                    corestring_dom___ns_key_file_name_node_data,
                    NULL, pcore_file_path_handler,
                    (void **) &old_path) != DOM_NO_ERR) {
            result = 1;
        }
    } else if (dom_html_input_element_get_default_value(input,
            &default_value) !=
            DOM_NO_ERR ||
            dom_html_input_element_get_default_checked(input,
                    &default_checked) != DOM_NO_ERR) {
        result = 1;
    } else {
        if (default_value == NULL &&
                dom_string_create((const uint8_t *) "", 0, &empty) !=
                        DOM_NO_ERR) {
            result = 1;
        } else if (dom_html_input_element_set_value(input,
                (default_value != NULL) ? default_value : empty) !=
                        DOM_NO_ERR ||
                dom_html_input_element_set_checked(input,
                        default_checked) != DOM_NO_ERR) {
            result = 1;
        }
    }
    free(old_path);
    if (empty != NULL) {
        dom_string_unref(empty);
    }
    if (default_value != NULL) {
        dom_string_unref(default_value);
    }
    return result;
}

static int pcore_form_reset_textarea(dom_html_text_area_element *textarea)
{
    dom_string *default_value;
    dom_string *empty;
    int result;

    default_value = NULL;
    empty = NULL;
    result = 0;
    if (dom_html_text_area_element_get_default_value(textarea,
            &default_value) != DOM_NO_ERR) {
        result = 1;
    } else {
        if (default_value == NULL &&
                dom_string_create((const uint8_t *) "", 0, &empty) !=
                        DOM_NO_ERR) {
            result = 1;
        } else if (dom_html_text_area_element_set_value(textarea,
                (default_value != NULL) ? default_value : empty) !=
                        DOM_NO_ERR) {
            result = 1;
        }
    }
    if (empty != NULL) {
        dom_string_unref(empty);
    }
    if (default_value != NULL) {
        dom_string_unref(default_value);
    }
    return result;
}

static int pcore_form_reset_select(dom_html_select_element *select)
{
    dom_html_options_collection *options;
    dom_node *node;
    uint32_t count;
    uint32_t index;
    bool multiple;
    bool selected;
    int selected_count;
    int result;

    options = NULL;
    node = NULL;
    count = 0;
    multiple = false;
    selected_count = 0;
    result = 0;
    if (dom_html_select_element_get_multiple(select, &multiple) !=
            DOM_NO_ERR ||
            dom_html_select_element_get_options(select, &options) !=
                    DOM_NO_ERR ||
            options == NULL ||
            dom_html_options_collection_get_length(options, &count) !=
                    DOM_NO_ERR) {
        if (options != NULL) {
            dom_html_options_collection_unref(options);
        }
        return 1;
    }
    for (index = 0; index < count; index++) {
        node = NULL;
        selected = false;
        if (dom_html_options_collection_item(options, index, &node) !=
                DOM_NO_ERR || node == NULL ||
                dom_html_option_element_get_default_selected(
                        (dom_html_option_element *) node,
                        &selected) != DOM_NO_ERR) {
            result = 1;
        } else {
            if (selected && !multiple && selected_count > 0) {
                selected = false;
            }
            if (selected) {
                selected_count++;
            }
            if (dom_html_option_element_set_selected(
                    (dom_html_option_element *) node, selected) !=
                    DOM_NO_ERR) {
                result = 1;
            }
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
        if (result) {
            break;
        }
    }
    if (!result && !multiple && selected_count == 0 && count > 0) {
        node = NULL;
        if (dom_html_options_collection_item(options, 0, &node) !=
                DOM_NO_ERR || node == NULL ||
                dom_html_option_element_set_selected(
                        (dom_html_option_element *) node, true) !=
                        DOM_NO_ERR) {
            result = 1;
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
    }
    dom_html_options_collection_unref(options);
    return result;
}

typedef struct pcore_form_reset_context {
    int error;
} pcore_form_reset_context;

static int pcore_form_reset_visit(dom_node *node, void *pw)
{
    pcore_form_reset_context *context;
    int result;

    context = (pcore_form_reset_context *) pw;
    if (context == NULL || node == NULL) {
        return -1;
    }
    result = 0;
    if (pcore_node_name_is(node, "input")) {
        result = pcore_form_reset_input((dom_html_input_element *) node);
    } else if (pcore_node_name_is(node, "textarea")) {
        result = pcore_form_reset_textarea(
                (dom_html_text_area_element *) node);
    } else if (pcore_node_name_is(node, "select")) {
        result = pcore_form_reset_select((dom_html_select_element *) node);
    } else if (pcore_node_name_is(node, "output")) {
        result = pcore_output_reset(node);
    }
    if (result != 0) {
        context->error = 1;
        return -1;
    }
    return 0;
}

static int pcore_form_reset(dom_html_form_element *form)
{
    dom_document *doc;
    pcore_form_reset_context context;
    int result;

    /* The host dispatches the cancelable reset event before calling this
     * state-only helper. Calling libdom's reset method here would emit a
     * second reset event after the host has already accepted the default. */
    doc = NULL;
    if (form == NULL ||
            dom_node_get_owner_document((dom_node *) form, &doc) !=
                    DOM_NO_ERR || doc == NULL) {
        if (doc != NULL) {
            dom_node_unref((dom_node *) doc);
        }
        return 1;
    }
    context.error = 0;
    result = pcore_form_controls_visit(doc, (dom_element *) form,
            pcore_form_reset_visit, &context);
    if (result == 0 && !context.error) {
        result = pcore_form_outputs_visit(doc, (dom_element *) form,
                pcore_form_reset_visit, &context);
    }
    dom_node_unref((dom_node *) doc);
    return result != 0 || context.error;
}

PCORE_API int PCore_FormResetAt(HANDLE hDoc, int x, int y)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    int result;
    bool effective_disabled;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* Resolve reset button text to the gadget-bearing ancestor. */
    }
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->type != GADGET_RESET) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 2;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 2;
    }
    result = pcore_form_reset(form);
    dom_node_unref((dom_node *) form);
    return result ? 3 : 1;
}

PCORE_API int PCore_FormResetById(HANDLE hDoc, const char *form_id)
{
    dom_element *element;
    int result;

    if (hDoc == NULL || form_id == NULL || form_id[0] == '\0') {
        return 1;
    }
    element = pcore_custom_validity_element_by_id(
            (dom_document *) hDoc, form_id);
    if (element == NULL || !pcore_node_name_is((dom_node *) element,
            "form")) {
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    result = pcore_form_reset((dom_html_form_element *) element);
    dom_node_unref((dom_node *) element);
    return result == 0 ? 0 : 1;
}

static void pcore_dirty_add_box(struct box *box, int *valid,
        int *x0, int *y0, int *x1, int *y1)
{
    int x;
    int y;
    int right;
    int bottom;

    x = 0;
    y = 0;
    box_coords(box, &x, &y);
    right = x + box->width;
    bottom = y + box->height;
    if (!*valid) {
        *x0 = x;
        *y0 = y;
        *x1 = right;
        *y1 = bottom;
        *valid = 1;
        return;
    }
    if (x < *x0) { *x0 = x; }
    if (y < *y0) { *y0 = y; }
    if (right > *x1) { *x1 = right; }
    if (bottom > *y1) { *y1 = bottom; }
}

static int pcore_same_radio_group(struct form_control *left,
        struct form_control *right)
{
    dom_html_form_element *left_form = NULL;
    dom_html_form_element *right_form = NULL;
    int same;

    if (left == NULL || right == NULL || left->name == NULL ||
            right->name == NULL || strcmp(left->name, right->name) != 0) {
        return 0;
    }
    left_form = pcore_control_form(left);
    right_form = pcore_control_form(right);
    same = (left_form == right_form) ? 1 : 0;
    if (left_form != NULL) {
        dom_node_unref((dom_node *) left_form);
    }
    if (right_form != NULL) {
        dom_node_unref((dom_node *) right_form);
    }
    return same;
}

static void pcore_radio_deselect_group(struct box *box,
        struct form_control *radio, int *dirty_valid,
        int *dirty_x0, int *dirty_y0, int *dirty_x1, int *dirty_y1)
{
    struct box *child;
    struct form_control *control;

    if (box == NULL) {
        return;
    }
    control = box->gadget;
    if (control != NULL && control != radio &&
            control->type == GADGET_RADIO && control->selected &&
            pcore_same_radio_group(control, radio)) {
        control->selected = false;
        dom_html_input_element_set_checked(
                (dom_html_input_element *) control->node, false);
        pcore_dirty_add_box(box, dirty_valid, dirty_x0, dirty_y0,
                dirty_x1, dirty_y1);
    }
    for (child = box->children; child != NULL; child = child->next) {
        pcore_radio_deselect_group(child, radio, dirty_valid,
                dirty_x0, dirty_y0, dirty_x1, dirty_y1);
    }
}

static int pcore_disclosure_details_for_summary(dom_node *summary,
        dom_element **out_details);

static dom_node *pcore_interaction_node(struct box *hit, int focus)
{
    struct box *box;
    dom_element *details;
    bool effective_disabled;
    int tabindex;
    int tabindex_present;
    int tabindex_valid;

    for (box = hit; box != NULL; box = box->parent) {
        if (box->gadget != NULL) {
            effective_disabled = box->gadget->disabled;
            if (pcore_node_effectively_disabled(box->gadget->node, NULL,
                    &effective_disabled) != 0 || effective_disabled) {
                return NULL;
            }
            return box->gadget->node;
        }
        if (box->node != NULL &&
                pcore_node_name_is(box->node, "a") &&
                pcore_node_has_attr(box->node, "href")) {
            return box->node;
        }
        if (box->node != NULL &&
                pcore_node_name_is(box->node, "summary")) {
            details = NULL;
            if (pcore_disclosure_details_for_summary(box->node,
                    &details)) {
                if (details != NULL) {
                    dom_node_unref((dom_node *) details);
                }
                return box->node;
            }
        }
        if (focus && box->node != NULL &&
                pcore_node_attr_tabindex(box->node, &tabindex,
                &tabindex_present, &tabindex_valid) && tabindex_present &&
                tabindex_valid) {
            return box->node;
        }
        if (!focus && box->node != NULL &&
                pcore_node_name_is(box->node, "label")) {
            return box->node;
        }
    }
    return NULL;
}

/* :hover applies to the nearest element under the pointer, not only to
 * controls and links. Text/anonymous boxes carry the useful element on an
 * ancestor, so walk the same retained hit chain used by click dispatch. */
static dom_node *pcore_hover_node(struct box *hit)
{
    struct box *box;
    dom_node_type type;

    for (box = hit; box != NULL; box = box->parent) {
        if (box->node != NULL &&
                dom_node_get_node_type(box->node, &type) == DOM_NO_ERR &&
                type == DOM_ELEMENT_NODE) {
            return box->node;
        }
    }
    return NULL;
}

PCORE_API int PCore_InteractionSetAt(HANDLE hDoc, int x, int y,
        unsigned int state_flags)
{
    dom_document *doc;
    pcore_render *st;
    struct box *hit;
    dom_node *target;
    int result;
    int changed;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || state_flags == 0 ||
            (state_flags & ~(PCORE_INTERACTION_FOCUS |
                    PCORE_INTERACTION_ACTIVE |
                    PCORE_INTERACTION_HOVER)) != 0) {
        return -1;
    }
    hit = pcore_hit(st->root_box, x, y);
    changed = 0;
    if ((state_flags & PCORE_INTERACTION_FOCUS) != 0) {
        target = pcore_interaction_node(hit, 1);
        result = pcore_interaction_set_node(doc,
                PCORE_INTERACTION_FOCUS, target);
        if (result < 0) {
            return -1;
        }
        changed |= result;
    }
    if ((state_flags & PCORE_INTERACTION_ACTIVE) != 0) {
        target = pcore_image_map_area_at(st, doc, x, y);
        if (target == NULL) {
            target = pcore_interaction_node(hit, 0);
        }
        result = pcore_interaction_set_node(doc,
                PCORE_INTERACTION_ACTIVE, target);
        if (target != NULL && pcore_node_name_is(target, "area")) {
            dom_node_unref(target);
        }
        if (result < 0) {
            return -1;
        }
        changed |= result;
    }
    if ((state_flags & PCORE_INTERACTION_HOVER) != 0) {
        target = pcore_image_map_area_at(st, doc, x, y);
        if (target == NULL) {
            target = pcore_hover_node(hit);
        }
        result = pcore_interaction_set_node(doc,
                PCORE_INTERACTION_HOVER, target);
        if (target != NULL && pcore_node_name_is(target, "area")) {
            dom_node_unref(target);
        }
        if (result < 0) {
            return -1;
        }
        changed |= result;
    }
    return changed;
}

PCORE_API int PCore_InteractionClear(HANDLE hDoc,
        unsigned int state_flags)
{
    return pcore_interaction_set_node((dom_document *) hDoc,
            state_flags, NULL);
}

PCORE_API int PCore_FormActivateAt(HANDLE hDoc, int x, int y,
        int *dirty_x, int *dirty_y, int *dirty_w, int *dirty_h)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    int dirty_valid;
    int x0;
    int y0;
    int x1;
    int y1;
    bool effective_disabled;

    if (dirty_x != NULL) { *dirty_x = 0; }
    if (dirty_y != NULL) { *dirty_y = 0; }
    if (dirty_w != NULL) { *dirty_w = 0; }
    if (dirty_h != NULL) { *dirty_h = 0; }
    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL && box->gadget == NULL;
            box = box->parent) {
        /* Find the nearest form gadget carried by the hit box or ancestor. */
    }
    if (box == NULL || box->gadget == NULL) {
        return 0;
    }
    control = box->gadget;
    if (control->type != GADGET_CHECKBOX &&
            control->type != GADGET_RADIO) {
        return 0;
    }
    effective_disabled = control->disabled;
    if (pcore_node_effectively_disabled(control->node, NULL,
            &effective_disabled) != 0 || effective_disabled) {
        return 1;
    }

    dirty_valid = 0;
    x0 = 0;
    y0 = 0;
    x1 = 0;
    y1 = 0;
    if (control->type == GADGET_CHECKBOX) {
        control->selected = !control->selected;
        dom_html_input_element_set_checked(
                (dom_html_input_element *) control->node,
                control->selected);
        pcore_dirty_add_box(box, &dirty_valid, &x0, &y0, &x1, &y1);
    } else if (!control->selected) {
        pcore_radio_deselect_group(st->root_box, control, &dirty_valid,
                &x0, &y0, &x1, &y1);
        control->selected = true;
        dom_html_input_element_set_checked(
                (dom_html_input_element *) control->node, true);
        pcore_dirty_add_box(box, &dirty_valid, &x0, &y0, &x1, &y1);
    }

    if (dirty_valid) {
        if (dirty_x != NULL) { *dirty_x = x0; }
        if (dirty_y != NULL) { *dirty_y = y0; }
        if (dirty_w != NULL) { *dirty_w = x1 - x0; }
        if (dirty_h != NULL) { *dirty_h = y1 - y0; }
    }
    return 1;
}

static dom_node *pcore_label_ancestor(dom_node *node)
{
    dom_node *current;
    dom_node *parent;

    current = (node != NULL) ? dom_node_ref(node) : NULL;
    while (current != NULL) {
        if (pcore_node_name_is(current, "label")) {
            return current;
        }
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return NULL;
        }
        dom_node_unref(current);
        current = parent;
    }
    return NULL;
}

static dom_node *pcore_label_first_control(dom_node *node)
{
    dom_node *child;
    dom_node *next;
    dom_node *found;

    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return NULL;
    }
    while (child != NULL) {
        if (pcore_node_name_is(child, "input") ||
                pcore_node_name_is(child, "textarea") ||
                pcore_node_name_is(child, "select") ||
                pcore_node_name_is(child, "button")) {
            return child;
        }
        found = pcore_label_first_control(child);
        if (found != NULL) {
            dom_node_unref(child);
            return found;
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return NULL;
        }
        dom_node_unref(child);
        child = next;
    }
    return NULL;
}

static struct box *pcore_box_for_node(struct box *box, dom_node *node)
{
    struct box *child;
    struct box *found;

    if (box == NULL || node == NULL) {
        return NULL;
    }
    if (box->node == node && box->gadget != NULL) {
        return box;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_box_for_node(child, node);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

/* Unlike pcore_box_for_node(), link lookup also needs ordinary inline boxes
 * that do not carry a form gadget. Keep this traversal private so the public
 * API exposes only the bounded geometry/copy-out contract. */
static struct box *pcore_box_for_any_node(struct box *box, dom_node *node)
{
    struct box *child;
    struct box *found;

    if (box == NULL || node == NULL) {
        return NULL;
    }
    if (box->node == node) {
        return box;
    }
    for (child = box->children; child != NULL; child = child->next) {
        found = pcore_box_for_any_node(child, node);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

/* Return one visual piece of an inline formatting run.  NetSurf keeps the
 * opening inline box and its INLINE_END marker as siblings around the text
 * pieces; the opening box owns the left decoration while the end marker owns
 * the right decoration.  Keeping those edges separate avoids manufacturing a
 * full border box at both ends of every wrapped line. */
static int pcore_layout_piece_geometry(struct box *box, int *x, int *y,
        int *w, int *h)
{
    int ax;
    int ay;
    int width;
    int height;

    if (box == NULL || x == NULL || y == NULL || w == NULL || h == NULL) {
        return 1;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (box->type == BOX_INLINE && box->inline_end != NULL) {
        width = box->width > 0 ? box->width : 0;
        height = box->height > 0 ? box->height : 0;
        *x = ax - box->border[LEFT].width;
        /* The layout pass already positions an inline opening box above its
         * content by padding[TOP], so only the border needs subtracting here.
         * This matches pcore_contenteditable_box_geometry's border-box edge. */
        *y = ay - box->border[TOP].width;
        *w = box->border[LEFT].width + box->padding[LEFT] + width;
        *h = box->border[TOP].width + box->padding[TOP] + height +
                box->padding[BOTTOM] + box->border[BOTTOM].width;
    } else {
        pcore_contenteditable_box_geometry(box, x, y, w, h);
    }
    if (*w < 0 || *h < 0) {
        return 1;
    }
    return 0;
}

static void pcore_layout_fragment_to_css(pcore_render *st, int *x, int *y,
        int *w, int *h)
{
    if (st == NULL || x == NULL || y == NULL || w == NULL || h == NULL ||
            !st->geometry_device_backed || st->geometry_dpi == 96) {
        return;
    }
    *x = FIXTOINT(css_unit_device2css_px(INTTOFIX(*x),
            INTTOFIX(st->geometry_dpi)));
    *y = FIXTOINT(css_unit_device2css_px(INTTOFIX(*y),
            INTTOFIX(st->geometry_dpi)));
    *w = FIXTOINT(css_unit_device2css_px(INTTOFIX(*w),
            INTTOFIX(st->geometry_dpi)));
    *h = FIXTOINT(css_unit_device2css_px(INTTOFIX(*h),
            INTTOFIX(st->geometry_dpi)));
}

static void pcore_layout_fragment_emit(int x, int y, int w, int h,
        int wanted, int *emitted, int *found, int *out_x, int *out_y,
        int *out_w, int *out_h)
{
    if (emitted == NULL || found == NULL || w <= 0 || h <= 0 ||
            *emitted >= (int) PCORE_NODE_LAYOUT_FRAGMENT_MAX) {
        return;
    }
    if (wanted >= 0 && *emitted == wanted) {
        if (out_x != NULL) { *out_x = x; }
        if (out_y != NULL) { *out_y = y; }
        if (out_w != NULL) { *out_w = w; }
        if (out_h != NULL) { *out_h = h; }
        *found = 1;
    }
    *emitted += 1;
}

/* Scan the private box tree once and either count or return one bounded
 * visual fragment.  `wanted` is -1 for count mode.  Inline descendants are
 * deliberately consumed as part of the enclosing inline range; their own
 * opening/end markers contribute decoration edges while text and replaced
 * boxes contribute the measured line extent. */
static int pcore_layout_fragment_scan(pcore_render *st, dom_node *node,
        int wanted, int *out_count, int *out_found, int *out_x, int *out_y,
        int *out_w, int *out_h)
{
    struct box *start;
    struct box *end;
    struct box *cursor;
    struct box *boundary;
    int emitted;
    int found;
    int have_line;
    int line_left;
    int line_right;
    int line_top;
    int line_bottom;
    int piece_x;
    int piece_y;
    int piece_w;
    int piece_h;

    if (out_count != NULL) { *out_count = 0; }
    if (out_found != NULL) { *out_found = 0; }
    if (out_x != NULL) { *out_x = 0; }
    if (out_y != NULL) { *out_y = 0; }
    if (out_w != NULL) { *out_w = 0; }
    if (out_h != NULL) { *out_h = 0; }
    if (st == NULL || st->root_box == NULL || node == NULL) {
        return 1;
    }
    start = pcore_box_for_any_node(st->root_box, node);
    if (start == NULL) {
        return 1;
    }

    /* Block/replaced boxes already represent one visual fragment. */
    end = (start->type == BOX_INLINE) ? start->inline_end : NULL;
    boundary = start;
    if (end != NULL && start->parent != NULL && end->parent == start->parent) {
        while (boundary != NULL && boundary != end) {
            boundary = boundary->next;
        }
    } else {
        boundary = NULL;
    }
    if (end == NULL || start->parent == NULL || boundary == NULL) {
        if (pcore_layout_piece_geometry(start, &piece_x, &piece_y,
                &piece_w, &piece_h) != 0 || piece_w <= 0 || piece_h <= 0) {
            return 0;
        }
        pcore_layout_fragment_to_css(st, &piece_x, &piece_y, &piece_w,
                &piece_h);
        emitted = 0;
        found = 0;
        pcore_layout_fragment_emit(piece_x, piece_y, piece_w, piece_h,
                wanted, &emitted, &found, out_x, out_y, out_w, out_h);
        if (out_count != NULL) { *out_count = emitted; }
        if (out_found != NULL) { *out_found = found; }
        return 0;
    }

    emitted = 0;
    found = 0;
    have_line = 0;
    line_left = 0;
    line_right = 0;
    line_top = 0;
    line_bottom = 0;
    for (cursor = start; cursor != NULL; cursor = cursor->next) {
        if (pcore_layout_piece_geometry(cursor, &piece_x, &piece_y,
                &piece_w, &piece_h) == 0) {
            pcore_layout_fragment_to_css(st, &piece_x, &piece_y,
                    &piece_w, &piece_h);
            if (!have_line) {
                line_left = piece_x;
                line_right = piece_x + piece_w;
                line_top = piece_y;
                line_bottom = piece_y + piece_h;
                have_line = 1;
            } else if (piece_y >= line_bottom ||
                    line_top >= piece_y + piece_h) {
                pcore_layout_fragment_emit(line_left, line_top,
                        line_right - line_left, line_bottom - line_top,
                        wanted, &emitted, &found, out_x, out_y, out_w,
                        out_h);
                line_left = piece_x;
                line_right = piece_x + piece_w;
                line_top = piece_y;
                line_bottom = piece_y + piece_h;
            } else {
                if (piece_x < line_left) { line_left = piece_x; }
                if (piece_x + piece_w > line_right) {
                    line_right = piece_x + piece_w;
                }
                if (piece_y < line_top) { line_top = piece_y; }
                if (piece_y + piece_h > line_bottom) {
                    line_bottom = piece_y + piece_h;
                }
            }
        }
        if (cursor == end) {
            break;
        }
    }
    if (have_line) {
        pcore_layout_fragment_emit(line_left, line_top,
                line_right - line_left, line_bottom - line_top, wanted,
                &emitted, &found, out_x, out_y, out_w, out_h);
    }
    if (out_count != NULL) { *out_count = emitted; }
    if (out_found != NULL) { *out_found = found; }
    return 0;
}

int pcore_box_layout_fragment_count(struct dom_document *doc,
        struct dom_node *node)
{
    pcore_render *st;
    int count;

    st = pcore_get_render((dom_document *) doc);
    count = 0;
    if (pcore_layout_fragment_scan(st, node, -1, &count, NULL, NULL,
            NULL, NULL, NULL) != 0) {
        return -1;
    }
    return count;
}

int pcore_box_layout_fragment_at(struct dom_document *doc,
        struct dom_node *node, unsigned int index, int *x, int *y,
        int *w, int *h)
{
    pcore_render *st;
    int count;
    int found;

    if (x != NULL) { *x = 0; }
    if (y != NULL) { *y = 0; }
    if (w != NULL) { *w = 0; }
    if (h != NULL) { *h = 0; }
    if (index >= PCORE_NODE_LAYOUT_FRAGMENT_MAX) {
        return 1;
    }
    st = pcore_get_render((dom_document *) doc);
    count = 0;
    found = 0;
    if (pcore_layout_fragment_scan(st, node, (int) index, &count, &found,
            x, y, w, h) != 0 || !found) {
        return 1;
    }
    return 0;
}

/* Internal geometry bridge used by the script-facing relation table. Keep
 * the box lookup and border-box arithmetic in the layout owner so other DLLs
 * never need to know about NetSurf's struct box. */
int pcore_box_geometry_for_node(struct dom_document *doc,
        struct dom_node *node, int *x, int *y, int *w, int *h)
{
    int count;
    unsigned int index;
    int fragment_x;
    int fragment_y;
    int fragment_w;
    int fragment_h;
    int right;
    int bottom;
    int union_x;
    int union_y;
    int union_right;
    int union_bottom;

    count = pcore_box_layout_fragment_count(doc, node);
    if (count <= 0) {
        return 1;
    }
    union_x = 0;
    union_y = 0;
    union_right = 0;
    union_bottom = 0;
    for (index = 0; index < (unsigned int) count; index++) {
        if (pcore_box_layout_fragment_at(doc, node, index, &fragment_x,
                &fragment_y, &fragment_w, &fragment_h) != 0) {
            return 1;
        }
        right = fragment_x + fragment_w;
        bottom = fragment_y + fragment_h;
        if (index == 0 || fragment_x < union_x) { union_x = fragment_x; }
        if (index == 0 || fragment_y < union_y) { union_y = fragment_y; }
        if (index == 0 || right > union_right) { union_right = right; }
        if (index == 0 || bottom > union_bottom) {
            union_bottom = bottom;
        }
    }
    if (x != NULL) { *x = union_x; }
    if (y != NULL) { *y = union_y; }
    if (w != NULL) { *w = union_right - union_x; }
    if (h != NULL) { *h = union_bottom - union_y; }
    return 0;
}

/* Keep the first box-metric slice deliberately small and deterministic.  The
 * relation bridge is useful to script consumers that need layout dimensions,
 * but it must not expose anonymous inline/text boxes whose dimensions do not
 * have the block-level offset/client meaning used here. */
static int pcore_box_metrics_supported(const struct box *box)
{
    if (box == NULL) {
        return 0;
    }
    switch (box->type) {
    case BOX_BLOCK:
    case BOX_INLINE_BLOCK:
    case BOX_TABLE:
    case BOX_TABLE_CELL:
    case BOX_FLEX:
    case BOX_INLINE_FLEX:
        return 1;
    default:
        return 0;
    }
}

static int pcore_box_metric_scroll_x(const struct box *box)
{
    unsigned int overflow;

    if (box == NULL || box->style == NULL) {
        return 0;
    }
    overflow = css_computed_overflow_x(box->style);
    return overflow == CSS_OVERFLOW_SCROLL ||
            (overflow == CSS_OVERFLOW_AUTO && box_hscrollbar_present(box));
}

static int pcore_box_metric_scroll_y(const struct box *box)
{
    unsigned int overflow;

    if (box == NULL || box->style == NULL) {
        return 0;
    }
    overflow = css_computed_overflow_y(box->style);
    return overflow == CSS_OVERFLOW_SCROLL ||
            (overflow == CSS_OVERFLOW_AUTO && box_vscrollbar_present(box));
}

/* Redraw normally materializes retained overflow scrollbars lazily. Script
 * setters and relation reads may arrive before the first paint, so create the
 * same bounded scrollbar pair at this product boundary instead of exposing a
 * first-paint ordering dependency to Browser callers. */
static int pcore_box_ensure_overflow_scrollbars(pcore_render *st,
        struct box *box)
{
    int has_x;
    int has_y;

    if (st == NULL || box == NULL || box->style == NULL ||
            box->parent == NULL || box->type == BOX_BR ||
            box->type == BOX_TABLE || box->type == BOX_INLINE) {
        return 0;
    }
    has_x = pcore_box_metric_scroll_x(box);
    has_y = pcore_box_metric_scroll_y(box);
    if (!has_x && !has_y) {
        return 0;
    }
    return box_handle_scrollbars((struct content *) &st->content, box,
            has_x, has_y) ==
            NSERROR_OK ? 0 : 1;
}

static int pcore_box_metric_clamp(long value)
{
    if (value <= 0) {
        return 0;
    }
    if (value > INT_MAX) {
        return INT_MAX;
    }
    return (int) value;
}

static int pcore_layout_dimension_to_css(pcore_render *st, int value)
{
    if (value <= 0 || st == NULL || !st->geometry_device_backed ||
            st->geometry_dpi == 96) {
        return value > 0 ? value : 0;
    }
    return pcore_box_metric_clamp(FIXTOINT(css_unit_device2css_px(
            INTTOFIX(value), INTTOFIX(st->geometry_dpi))));
}

static int pcore_layout_dimension_to_device(pcore_render *st, int value)
{
    if (value <= 0 || st == NULL || !st->geometry_device_backed ||
            st->geometry_dpi == 96) {
        return value > 0 ? value : 0;
    }
    return pcore_box_metric_clamp(FIXTOINT(css_unit_css2device_px(
            INTTOFIX(value), INTTOFIX(st->geometry_dpi))));
}

int pcore_box_layout_client_origin_for_node(struct dom_document *doc,
        struct dom_node *node, int *x, int *y)
{
    pcore_render *st;
    struct box *box;
    int ax;
    int ay;

    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
    if (doc == NULL || node == NULL || x == NULL || y == NULL) {
        return 1;
    }
    st = pcore_get_render(doc);
    if (st == NULL || st->root_box == NULL) {
        return 2;
    }
    box = pcore_box_for_any_node(st->root_box, node);
    if (!pcore_box_metrics_supported(box)) {
        return 2;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    *x = pcore_layout_dimension_to_css(st, ax);
    *y = pcore_layout_dimension_to_css(st, ay);
    return 0;
}

/* Return integer CSS-pixel offset/client/scroll dimensions from the current
 * retained layout.  This mirrors box_handle_scrollbars()'s extent arithmetic
 * while remaining valid before a paint pass has created scrollbar objects. */
int pcore_box_layout_metrics_for_node(struct dom_document *doc,
        struct dom_node *node, int *offset_width, int *offset_height,
        int *client_width, int *client_height, int *scroll_width,
        int *scroll_height)
{
    pcore_render *st;
    struct box *box;
    long visible_width;
    long visible_height;
    long border_width;
    long border_height;
    long full_width;
    long full_height;
    long client_w;
    long client_h;

    if (offset_width != NULL) { *offset_width = 0; }
    if (offset_height != NULL) { *offset_height = 0; }
    if (client_width != NULL) { *client_width = 0; }
    if (client_height != NULL) { *client_height = 0; }
    if (scroll_width != NULL) { *scroll_width = 0; }
    if (scroll_height != NULL) { *scroll_height = 0; }
    if (doc == NULL || node == NULL) {
        return 1;
    }
    st = pcore_get_render(doc);
    if (st == NULL || st->root_box == NULL) {
        return 1;
    }
    box = pcore_box_for_any_node(st->root_box, node);
    if (!pcore_box_metrics_supported(box)) {
        return 1;
    }

    visible_width = (long) box->width + box->padding[LEFT] +
            box->padding[RIGHT];
    visible_height = (long) box->height + box->padding[TOP] +
            box->padding[BOTTOM];
    border_width = visible_width + box->border[LEFT].width +
            box->border[RIGHT].width;
    border_height = visible_height + box->border[TOP].width +
            box->border[BOTTOM].width;
    full_width = visible_width;
    if ((long) box->descendant_x1 - box->border[RIGHT].width >
            visible_width) {
        full_width = (long) box->descendant_x1 + box->padding[RIGHT];
    }
    full_height = visible_height;
    if ((long) box->descendant_y1 - box->border[BOTTOM].width >
            visible_height) {
        full_height = (long) box->descendant_y1 + box->padding[BOTTOM];
    }
    /* NetSurf's retained scrollbar is painted over the edge of the box; its
     * scroll model therefore uses the full padding viewport as visible_size.
     * Keep client dimensions identical to that scrollport so
     * scrollWidth-clientWidth is the actual retained scrollbar range. */
    client_w = visible_width;
    client_h = visible_height;
    if (client_w < 0) { client_w = 0; }
    if (client_h < 0) { client_h = 0; }
    if (full_width < client_w) { full_width = client_w; }
    if (full_height < client_h) { full_height = client_h; }

    if (offset_width != NULL) {
        *offset_width = pcore_layout_dimension_to_css(st,
                pcore_box_metric_clamp(border_width));
    }
    if (offset_height != NULL) {
        *offset_height = pcore_layout_dimension_to_css(st,
                pcore_box_metric_clamp(border_height));
    }
    if (client_width != NULL) {
        *client_width = pcore_layout_dimension_to_css(st,
                pcore_box_metric_clamp(client_w));
    }
    if (client_height != NULL) {
        *client_height = pcore_layout_dimension_to_css(st,
                pcore_box_metric_clamp(client_h));
    }
    if (scroll_width != NULL) {
        *scroll_width = pcore_layout_dimension_to_css(st,
                pcore_box_metric_clamp(full_width));
    }
    if (scroll_height != NULL) {
        *scroll_height = pcore_layout_dimension_to_css(st,
                pcore_box_metric_clamp(full_height));
    }
    return 0;
}

/* Return the retained overflow offsets for one element, or apply a
 * non-negative CSS-pixel request before returning the clamped position. The
 * box tree stores device pixels on high-DPI layouts, so conversion is kept at
 * this private boundary and callers never observe device units. */
int pcore_box_overflow_scroll_for_node(struct dom_document *doc,
        struct dom_node *node, int requested_x, int requested_y,
        int *scroll_x, int *scroll_y)
{
    pcore_render *st;
    struct box *box;
    int value;

    if (scroll_x != NULL) {
        *scroll_x = 0;
    }
    if (scroll_y != NULL) {
        *scroll_y = 0;
    }
    if (doc == NULL || node == NULL || requested_x < -1 ||
            requested_y < -1) {
        return 1;
    }
    st = pcore_get_render(doc);
    if (st == NULL || st->root_box == NULL) {
        return 2;
    }
    box = pcore_box_for_any_node(st->root_box, node);
    if (box == NULL) {
        return 2;
    }
    if (pcore_box_ensure_overflow_scrollbars(st, box) != 0) {
        return 1;
    }
    if (requested_x >= 0 && box->scroll_x != NULL) {
        value = pcore_layout_dimension_to_device(st, requested_x);
        scrollbar_set(box->scroll_x, value, false);
    }
    if (requested_y >= 0 && box->scroll_y != NULL) {
        value = pcore_layout_dimension_to_device(st, requested_y);
        scrollbar_set(box->scroll_y, value, false);
    }
    if (scroll_x != NULL && box->scroll_x != NULL) {
        *scroll_x = pcore_layout_dimension_to_css(st,
                scrollbar_get_offset(box->scroll_x));
    }
    if (scroll_y != NULL && box->scroll_y != NULL) {
        *scroll_y = pcore_layout_dimension_to_css(st,
                scrollbar_get_offset(box->scroll_y));
    }
    return 0;
}

int pcore_box_overflow_axis_available(struct dom_document *doc,
        struct dom_node *node, int axis, int *out_available)
{
    pcore_render *st;
    struct box *box;

    if (out_available != NULL) {
        *out_available = 0;
    }
    if (doc == NULL || node == NULL || out_available == NULL ||
            (axis != 0 && axis != 1)) {
        return 1;
    }
    st = pcore_get_render(doc);
    if (st == NULL || st->root_box == NULL) {
        return 2;
    }
    box = pcore_box_for_any_node(st->root_box, node);
    if (box == NULL) {
        return 2;
    }
    if (pcore_box_ensure_overflow_scrollbars(st, box) != 0) {
        return 1;
    }
    *out_available = (axis == 0) ? (box->scroll_x != NULL) :
            (box->scroll_y != NULL);
    return 0;
}

PCORE_API int PCore_NodeOverflowScrollToById(HANDLE hDoc,
        const char *element_id, int scroll_x, int scroll_y,
        int *out_x, int *out_y)
{
    dom_document *doc;
    dom_element *element;
    int result;

    if (out_x != NULL) {
        *out_x = 0;
    }
    if (out_y != NULL) {
        *out_y = 0;
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0' ||
            out_x == NULL || out_y == NULL || scroll_x < 0 ||
            scroll_y < 0) {
        return 1;
    }
    doc = (dom_document *) hDoc;
    element = pcore_box_element_by_id(doc, element_id);
    if (element == NULL) {
        return 2;
    }
    result = pcore_box_overflow_scroll_for_node(doc,
            (dom_node *) element, scroll_x, scroll_y, out_x, out_y);
    dom_node_unref((dom_node *) element);
    return result;
}

/* Resolve the first direct summary trigger of a details element. The
 * returned parent is retained by dom_node_get_parent_node and belongs to the
 * caller. Later summary elements are intentionally not activation targets;
 * this keeps the bounded implementation aligned with the HTML trigger rule
 * without exposing DOM nodes through the public ABI. */
static int pcore_disclosure_details_for_summary(dom_node *summary,
        dom_element **out_details)
{
    dom_node *parent;
    dom_node *child;
    dom_node *next;
    int first_summary;

    if (out_details != NULL) {
        *out_details = NULL;
    }
    if (summary == NULL || out_details == NULL ||
            !pcore_node_name_is(summary, "summary")) {
        return 0;
    }
    parent = NULL;
    if (dom_node_get_parent_node(summary, &parent) != DOM_NO_ERR ||
            parent == NULL || !pcore_node_name_is(parent, "details")) {
        if (parent != NULL) {
            dom_node_unref(parent);
        }
        return 0;
    }
    child = NULL;
    if (dom_node_get_first_child(parent, &child) != DOM_NO_ERR) {
        dom_node_unref(parent);
        return 0;
    }
    first_summary = 0;
    while (child != NULL) {
        if (pcore_node_name_is(child, "summary")) {
            first_summary = (child == summary) ? 1 : 0;
            dom_node_unref(child);
            child = NULL;
            break;
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            child = NULL;
            break;
        }
        dom_node_unref(child);
        child = next;
    }
    if (!first_summary) {
        dom_node_unref(parent);
        return 0;
    }
    *out_details = (dom_element *) parent;
    return 1;
}

static int pcore_disclosure_read_open(dom_element *details, int *out_open)
{
    dom_string *name;
    bool present;

    if (out_open != NULL) {
        *out_open = 0;
    }
    if (details == NULL) {
        return 0;
    }
    name = NULL;
    if (dom_string_create((const uint8_t *) "open", 4, &name) !=
            DOM_NO_ERR || name == NULL) {
        return 0;
    }
    present = false;
    if (dom_element_has_attribute(details, name, &present) != DOM_NO_ERR) {
        dom_string_unref(name);
        return 0;
    }
    dom_string_unref(name);
    if (out_open != NULL) {
        *out_open = present ? 1 : 0;
    }
    return 1;
}

static int pcore_disclosure_toggle_details(dom_element *details,
        int *out_open)
{
    dom_string *name;
    dom_string *value;
    bool present;
    dom_exception err;

    if (out_open != NULL) {
        *out_open = 0;
    }
    if (details == NULL) {
        return -1;
    }
    name = NULL;
    value = NULL;
    present = false;
    if (dom_string_create((const uint8_t *) "open", 4, &name) !=
            DOM_NO_ERR || name == NULL ||
            dom_element_has_attribute(details, name, &present) !=
            DOM_NO_ERR) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return -1;
    }
    if (present) {
        err = dom_element_remove_attribute(details, name);
    } else {
        if (dom_string_create((const uint8_t *) "", 0, &value) !=
                DOM_NO_ERR || value == NULL) {
            dom_string_unref(name);
            return -1;
        }
        err = dom_element_set_attribute(details, name, value);
    }
    if (value != NULL) {
        dom_string_unref(value);
    }
    dom_string_unref(name);
    if (err != DOM_NO_ERR) {
        return -1;
    }
    if (out_open != NULL) {
        *out_open = present ? 0 : 1;
    }
    return 1;
}

static int pcore_disclosure_summary_box_info(pcore_render *st,
        dom_node *summary, struct box *summary_box, int *x, int *y,
        int *w, int *h, int *open, dom_element **out_details)
{
    dom_element *details;
    int is_open;
    int ax;
    int ay;

    if (out_details != NULL) {
        *out_details = NULL;
    }
    if (st == NULL || summary == NULL || summary_box == NULL ||
            summary_box->width <= 0 || summary_box->height <= 0 ||
            out_details == NULL) {
        return 0;
    }
    details = NULL;
    if (!pcore_disclosure_details_for_summary(summary, &details) ||
            details == NULL || !pcore_disclosure_read_open(details,
            &is_open)) {
        if (details != NULL) {
            dom_node_unref((dom_node *) details);
        }
        return 0;
    }
    ax = 0;
    ay = 0;
    box_coords(summary_box, &ax, &ay);
    if (x != NULL) { *x = ax; }
    if (y != NULL) { *y = ay; }
    if (w != NULL) { *w = summary_box->width; }
    if (h != NULL) { *h = summary_box->height; }
    if (open != NULL) { *open = is_open; }
    *out_details = details;
    return 1;
}

static dom_element *pcore_box_element_by_id(dom_document *doc,
        const char *element_id)
{
    dom_string *id;
    dom_element *element;

    if (doc == NULL || element_id == NULL || element_id[0] == '\0') {
        return NULL;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id,
            strlen(element_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &element) !=
            DOM_NO_ERR) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return NULL;
    }
    dom_string_unref(id);
    return element;
}

static int pcore_disclosure_info_at_internal(pcore_render *st, int x,
        int y, int *summary_x, int *summary_y, int *summary_w,
        int *summary_h, int *open, dom_element **out_details)
{
    struct box *hit;
    struct box *box;
    dom_element *details;

    if (out_details != NULL) {
        *out_details = NULL;
    }
    if (st == NULL || out_details == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    for (box = hit; box != NULL; box = box->parent) {
        if (box->node == NULL || !pcore_node_name_is(box->node,
                "summary")) {
            continue;
        }
        details = NULL;
        if (pcore_disclosure_summary_box_info(st, box->node, box,
                summary_x, summary_y, summary_w, summary_h, open,
                &details)) {
            *out_details = details;
            return 1;
        }
    }
    return 0;
}

PCORE_API int PCore_DisclosureInfoById(HANDLE hDoc,
        const char *summary_id, int *x, int *y, int *w, int *h,
        int *open)
{
    dom_document *doc;
    pcore_render *st;
    dom_element *summary;
    dom_element *details;
    struct box *box;
    int result;

    if (x != NULL) { *x = 0; }
    if (y != NULL) { *y = 0; }
    if (w != NULL) { *w = 0; }
    if (h != NULL) { *h = 0; }
    if (open != NULL) { *open = 0; }
    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || summary_id == NULL || summary_id[0] == '\0') {
        return 1;
    }
    summary = pcore_box_element_by_id(doc, summary_id);
    if (summary == NULL) {
        return 1;
    }
    box = pcore_box_for_any_node(st->root_box, (dom_node *) summary);
    details = NULL;
    result = pcore_disclosure_summary_box_info(st, (dom_node *) summary,
            box, x, y, w, h, open, &details);
    if (details != NULL) {
        dom_node_unref((dom_node *) details);
    }
    dom_node_unref((dom_node *) summary);
    return result ? 0 : 1;
}

PCORE_API int PCore_DisclosureInfoAt(HANDLE hDoc, int x, int y,
        int *summary_x, int *summary_y, int *summary_w, int *summary_h,
        int *open)
{
    pcore_render *st;
    dom_element *details;
    int result;

    if (summary_x != NULL) { *summary_x = 0; }
    if (summary_y != NULL) { *summary_y = 0; }
    if (summary_w != NULL) { *summary_w = 0; }
    if (summary_h != NULL) { *summary_h = 0; }
    if (open != NULL) { *open = 0; }
    st = pcore_get_render((dom_document *) hDoc);
    details = NULL;
    result = pcore_disclosure_info_at_internal(st, x, y, summary_x,
            summary_y, summary_w, summary_h, open, &details);
    if (details != NULL) {
        dom_node_unref((dom_node *) details);
    }
    return result;
}

PCORE_API int PCore_DisclosureToggleById(HANDLE hDoc,
        const char *summary_id, int *open_after)
{
    dom_document *doc;
    pcore_render *st;
    dom_element *summary;
    dom_element *details;
    struct box *box;
    int result;

    if (open_after != NULL) { *open_after = 0; }
    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || summary_id == NULL || summary_id[0] == '\0') {
        return 0;
    }
    summary = pcore_box_element_by_id(doc, summary_id);
    if (summary == NULL) {
        return 0;
    }
    box = pcore_box_for_any_node(st->root_box, (dom_node *) summary);
    details = NULL;
    result = pcore_disclosure_summary_box_info(st, (dom_node *) summary,
            box, NULL, NULL, NULL, NULL, NULL, &details);
    if (result) {
        result = pcore_disclosure_toggle_details(details, open_after);
    }
    if (details != NULL) {
        dom_node_unref((dom_node *) details);
    }
    dom_node_unref((dom_node *) summary);
    return result;
}

PCORE_API int PCore_DisclosureToggleAt(HANDLE hDoc, int x, int y,
        int *open_after)
{
    pcore_render *st;
    dom_element *details;
    int result;

    if (open_after != NULL) { *open_after = 0; }
    st = pcore_get_render((dom_document *) hDoc);
    details = NULL;
    result = pcore_disclosure_info_at_internal(st, x, y, NULL, NULL,
            NULL, NULL, NULL, &details);
    if (result) {
        result = pcore_disclosure_toggle_details(details, open_after);
    }
    if (details != NULL) {
        dom_node_unref((dom_node *) details);
    }
    return result;
}

PCORE_API int PCore_LabelTargetAt(HANDLE hDoc, int x, int y,
        int *target_x, int *target_y, int *target_kind)
{
    dom_document *doc;
    pcore_render *st;
    struct box *hit;
    struct box *target_box;
    struct form_control *control;
    dom_node *label;
    dom_node *target;
    dom_string *html_for;
    int ax;
    int ay;
    int kind;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL) {
        return 0;
    }
    hit = pcore_hit(st->root_box, x, y);
    label = (hit != NULL) ? pcore_label_ancestor(hit->node) : NULL;
    if (label == NULL) {
        return 0;
    }
    target = NULL;
    html_for = NULL;
    if (dom_html_label_element_get_html_for(
            (dom_html_label_element *) label, &html_for) == DOM_NO_ERR &&
            html_for != NULL && dom_string_byte_length(html_for) > 0) {
        dom_document_get_element_by_id(doc, html_for,
                (dom_element **) &target);
    } else {
        target = pcore_label_first_control(label);
    }
    if (html_for != NULL) {
        dom_string_unref(html_for);
    }
    dom_node_unref(label);
    target_box = (target != NULL) ?
            pcore_box_for_node(st->root_box, target) : NULL;
    if (target != NULL) {
        dom_node_unref(target);
    }
    control = (target_box != NULL) ? target_box->gadget : NULL;
    if (control == NULL) {
        return 0;
    }
    if (control->type == GADGET_CHECKBOX) {
        kind = 1;
    } else if (control->type == GADGET_RADIO) {
        kind = 2;
    } else if (control->type == GADGET_TEXTBOX) {
        kind = 3;
    } else if (control->type == GADGET_PASSWORD) {
        kind = 4;
    } else if (control->type == GADGET_TEXTAREA) {
        kind = 5;
    } else if (control->type == GADGET_SELECT) {
        kind = 6;
    } else if (control->type == GADGET_SUBMIT) {
        kind = 7;
    } else if (control->type == GADGET_RESET) {
        kind = 8;
    } else if (control->type == GADGET_BUTTON) {
        kind = 9;
    } else if (control->type == GADGET_FILE) {
        kind = 10;
    } else {
        return 0;
    }
    ax = 0;
    ay = 0;
    box_coords(target_box, &ax, &ay);
    if (target_x != NULL) {
        *target_x = ax + target_box->width / 2;
    }
    if (target_y != NULL) {
        *target_y = ay + target_box->height / 2;
    }
    if (target_kind != NULL) {
        *target_kind = kind;
    }
    return 1;
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
    {
        dom_node *area = pcore_image_map_area_at(st, doc, x, y);
        if (area != NULL) {
            rc = pcore_link_copy_href_truncated((dom_element *) area,
                    pcore_href_name, out_href, cap);
            dom_node_unref(area);
            return rc;
        }
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

/* Copy one link attribute without exposing a libdom string to the caller.
 * Required attributes fail when absent or empty; optional attributes become
 * an empty string. Capacities include the trailing NUL. */
static int pcore_link_copy_attribute(dom_element *element,
        dom_string *attribute, char *out, int cap, int required)
{
    dom_string *value;
    const char *data;
    int length;

    if (out == NULL || cap <= 0 || element == NULL || attribute == NULL) {
        return 1;
    }
    out[0] = '\0';
    value = NULL;
    if (dom_element_get_attribute(element, attribute, &value) != DOM_NO_ERR) {
        return 1;
    }
    if (value == NULL) {
        return required ? 1 : 0;
    }
    data = dom_string_data(value);
    length = (int) dom_string_byte_length(value);
    if (data == NULL || length < 0 || length >= cap) {
        dom_string_unref(value);
        return 1;
    }
    if (length > 0) {
        memcpy(out, data, (size_t) length);
    }
    out[length] = '\0';
    dom_string_unref(value);
    return required && length == 0 ? 1 : 0;
}

/* The legacy PCore_LinkAt contract permits truncating href values, unlike the
 * strict Ex/ById snapshots.  Keep that behaviour identical for <a> and
 * mapped <area> links. */
static int pcore_link_copy_href_truncated(dom_element *element,
        dom_string *attribute, char *out, int cap)
{
    dom_string *value;
    const char *data;
    int length;

    if (element == NULL || attribute == NULL || out == NULL || cap <= 0) {
        return 0;
    }
    value = NULL;
    if (dom_element_get_attribute(element, attribute, &value) != DOM_NO_ERR ||
            value == NULL) {
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 0;
    }
    data = dom_string_data(value);
    length = (int) dom_string_byte_length(value);
    if (data == NULL || length <= 0) {
        dom_string_unref(value);
        return 0;
    }
    if (length > cap - 1) {
        length = cap - 1;
    }
    memcpy(out, data, (size_t) length);
    out[length] = '\0';
    dom_string_unref(value);
    return 1;
}

PCORE_API int PCore_LinkAtEx(HANDLE hDoc, int x, int y,
        char *out_href, int href_cap, char *out_target, int target_cap,
        char *out_rel, int rel_cap)
{
    dom_document *doc;
    pcore_render *st;
    struct box *hit;
    struct box *b;
    dom_string *name;
    int is_anchor;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (out_href != NULL && href_cap > 0) {
        out_href[0] = '\0';
    }
    if (out_target != NULL && target_cap > 0) {
        out_target[0] = '\0';
    }
    if (out_rel != NULL && rel_cap > 0) {
        out_rel[0] = '\0';
    }
    if (st == NULL || out_href == NULL || href_cap <= 0 ||
            out_target == NULL || target_cap <= 0 || out_rel == NULL ||
            rel_cap <= 0 || pcore_ensure_link_strings() != 0) {
        return 1;
    }
    {
        dom_node *area = pcore_image_map_area_at(st, doc, x, y);
        if (area != NULL) {
            if (pcore_link_copy_attribute((dom_element *) area,
                    pcore_href_name, out_href, href_cap, 1) != 0 ||
                    pcore_link_copy_attribute((dom_element *) area,
                    pcore_target_name, out_target, target_cap, 0) != 0 ||
                    pcore_link_copy_attribute((dom_element *) area,
                    pcore_rel_name, out_rel, rel_cap, 0) != 0) {
                dom_node_unref(area);
                return 1;
            }
            dom_node_unref(area);
            return 0;
        }
    }
    hit = pcore_hit(st->root_box, x, y);
    if (hit == NULL) {
        return 1;
    }
    for (b = hit; b != NULL; b = b->parent) {
        name = NULL;
        is_anchor = 0;
        if (b->node != NULL &&
                dom_node_get_node_name(b->node, &name) == DOM_NO_ERR &&
                name != NULL) {
            is_anchor = dom_string_caseless_isequal(name, pcore_a_name);
            dom_string_unref(name);
        }
        if (is_anchor) {
            if (pcore_link_copy_attribute((dom_element *) b->node,
                    pcore_href_name, out_href, href_cap, 1) != 0 ||
                    pcore_link_copy_attribute((dom_element *) b->node,
                    pcore_target_name, out_target, target_cap, 0) != 0 ||
                    pcore_link_copy_attribute((dom_element *) b->node,
                    pcore_rel_name, out_rel, rel_cap, 0) != 0) {
                return 1;
            }
            return 0;
        }
    }
    return 1;
}

PCORE_API int PCore_LinkInfoById(HANDLE hDoc, const char *element_id,
        int *x, int *y, int *w, int *h, char *out_href, int cap)
{
    dom_document *doc;
    pcore_render *st;
    dom_string *id;
    dom_element *element;
    dom_string *name;
    dom_string *href;
    struct box *box;
    const char *name_data;
    const char *href_data;
    int href_len;
    int is_anchor;
    int is_area;
    int ax;
    int ay;
    int geometry_w;
    int geometry_h;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || element_id == NULL || element_id[0] == '\0' ||
            out_href == NULL || cap <= 0) {
        return 1;
    }
    if (pcore_ensure_link_strings() != 0) {
        return 1;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id,
            strlen(element_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR ||
            element == NULL) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    dom_string_unref(id);
    name = NULL;
    if (dom_node_get_node_name((dom_node *) element, &name) != DOM_NO_ERR ||
            name == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    name_data = dom_string_data(name);
    is_anchor = name_data != NULL &&
            dom_string_caseless_isequal(name, pcore_a_name);
    is_area = name_data != NULL &&
            pcore_node_name_is((dom_node *) element, "area");
    if (!is_anchor && !is_area) {
        dom_string_unref(name);
        dom_node_unref((dom_node *) element);
        return 1;
    }
    dom_string_unref(name);
    href = NULL;
    if (dom_element_get_attribute(element, pcore_href_name, &href) !=
            DOM_NO_ERR || href == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    href_data = dom_string_data(href);
    href_len = (int) dom_string_byte_length(href);
    if (href_data == NULL || href_len <= 0 || href_len >= cap) {
        dom_string_unref(href);
        dom_node_unref((dom_node *) element);
        return 1;
    }
    box = NULL;
    ax = 0;
    ay = 0;
    geometry_w = 0;
    geometry_h = 0;
    if (is_area) {
        if (pcore_image_map_area_geometry(st, doc,
                (dom_node *) element, &ax, &ay, &geometry_w,
                &geometry_h) != 0) {
            dom_string_unref(href);
            dom_node_unref((dom_node *) element);
            return 1;
        }
    } else {
        box = pcore_box_for_any_node(st->root_box, (dom_node *) element);
        if (box == NULL) {
            dom_string_unref(href);
            dom_node_unref((dom_node *) element);
            return 1;
        }
        box_coords(box, &ax, &ay);
        geometry_w = box->width;
        geometry_h = box->height;
    }
    if (x != NULL) {
        *x = ax;
    }
    if (y != NULL) {
        *y = ay;
    }
    if (w != NULL) {
        *w = geometry_w;
    }
    if (h != NULL) {
        *h = geometry_h;
    }
    memcpy(out_href, href_data, (size_t) href_len);
    out_href[href_len] = '\0';
    dom_string_unref(href);
    dom_node_unref((dom_node *) element);
    return 0;
}

PCORE_API int PCore_LinkInfoByIdEx(HANDLE hDoc, const char *element_id,
        int *x, int *y, int *w, int *h, char *out_href, int href_cap,
        char *out_target, int target_cap, char *out_rel, int rel_cap)
{
    int bytes;
    int status;

    if (out_target != NULL && target_cap > 0) {
        out_target[0] = '\0';
    }
    if (out_rel != NULL && rel_cap > 0) {
        out_rel[0] = '\0';
    }
    if (out_href == NULL || href_cap <= 0 || out_target == NULL ||
            target_cap <= 0 || out_rel == NULL || rel_cap <= 0) {
        return 1;
    }
    if (PCore_LinkInfoById(hDoc, element_id, x, y, w, h, out_href,
            href_cap) != 0) {
        return 1;
    }
    bytes = 0;
    status = PCore_NodeAttributeById(hDoc, element_id, "target",
            out_target, target_cap, &bytes);
    if (status == 2) {
        out_target[0] = '\0';
    } else if (status != 0 || bytes < 0 || bytes >= target_cap) {
        return 1;
    }
    bytes = 0;
    status = PCore_NodeAttributeById(hDoc, element_id, "rel",
            out_rel, rel_cap, &bytes);
    if (status == 2) {
        out_rel[0] = '\0';
    } else if (status != 0 || bytes < 0 || bytes >= rel_cap) {
        return 1;
    }
    return 0;
}

PCORE_API int PCore_FragmentInfoById(HANDLE hDoc, const char *fragment_id,
        int *x, int *y, int *w, int *h)
{
    dom_document *doc;
    pcore_render *st;
    dom_string *id;
    dom_element *element;
    struct box *box;
    int ax;
    int ay;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || fragment_id == NULL || fragment_id[0] == '\0') {
        return 1;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) fragment_id,
            strlen(fragment_id), &id) != DOM_NO_ERR || id == NULL ||
            dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR ||
            element == NULL) {
        if (id != NULL) {
            dom_string_unref(id);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return 1;
    }
    dom_string_unref(id);
    box = pcore_box_for_any_node(st->root_box, (dom_node *) element);
    if (box == NULL || box->width <= 0 || box->height <= 0) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (x != NULL) {
        *x = ax;
    }
    if (y != NULL) {
        *y = ay;
    }
    if (w != NULL) {
        *w = box->width;
    }
    if (h != NULL) {
        *h = box->height;
    }
    dom_node_unref((dom_node *) element);
    return 0;
}

/* Copy only the geometry needed by the fragment bridge.  The node reference
 * remains owned by the caller; the box tree borrows it for the document's
 * layout lifetime. */
static int pcore_fragment_info_for_node(struct box *root_box,
        dom_node *node, int *x, int *y, int *w, int *h)
{
    struct box *box;
    int ax;
    int ay;

    if (root_box == NULL || node == NULL) {
        return 1;
    }
    box = pcore_box_for_any_node(root_box, node);
    if (box == NULL || box->width <= 0 || box->height <= 0) {
        return 1;
    }
    ax = 0;
    ay = 0;
    box_coords(box, &ax, &ay);
    if (x != NULL) {
        *x = ax;
    }
    if (y != NULL) {
        *y = ay;
    }
    if (w != NULL) {
        *w = box->width;
    }
    if (h != NULL) {
        *h = box->height;
    }
    return 0;
}

/* libdom's HTML anchors collection is deliberately used here instead of a
 * private DOM walk: it matches the legacy HTML definition of an anchor with
 * a name attribute and keeps the public Core ABI independent of node layout. */
static dom_element *pcore_fragment_anchor_by_name(dom_document *doc,
        const char *fragment_token)
{
    dom_html_collection *anchors;
    dom_string *wanted;
    dom_string *value;
    dom_node *node;
    dom_element *match;
    uint32_t count;
    uint32_t index;

    anchors = NULL;
    wanted = NULL;
    value = NULL;
    node = NULL;
    match = NULL;
    count = 0;
    if (doc == NULL || fragment_token == NULL ||
            dom_string_create((const uint8_t *) fragment_token,
            strlen(fragment_token), &wanted) != DOM_NO_ERR || wanted == NULL) {
        return NULL;
    }
    if (dom_html_document_get_anchors((dom_html_document *) doc,
            &anchors) != DOM_NO_ERR || anchors == NULL ||
            dom_html_collection_get_length(anchors, &count) != DOM_NO_ERR) {
        if (anchors != NULL) {
            dom_html_collection_unref(anchors);
        }
        dom_string_unref(wanted);
        return NULL;
    }
    for (index = 0; index < count; index++) {
        node = NULL;
        value = NULL;
        if (dom_html_collection_item(anchors, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            break;
        }
        if (dom_element_get_attribute((dom_element *) node,
                pcore_name_attr, &value) == DOM_NO_ERR && value != NULL &&
                dom_string_isequal(value, wanted)) {
            match = (dom_element *) node;
        }
        if (value != NULL) {
            dom_string_unref(value);
        }
        if (match != NULL) {
            break;
        }
        dom_node_unref(node);
        node = NULL;
    }
    if (node != NULL && match == NULL) {
        dom_node_unref(node);
    }
    dom_html_collection_unref(anchors);
    dom_string_unref(wanted);
    return match;
}

PCORE_API int PCore_FragmentInfoByToken(HANDLE hDoc,
        const char *fragment_token, int *x, int *y, int *w, int *h)
{
    dom_document *doc;
    pcore_render *st;
    dom_string *id;
    dom_element *element;
    int result;

    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || fragment_token == NULL || fragment_token[0] == '\0' ||
            pcore_ensure_link_strings() != 0) {
        return 1;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) fragment_token,
            strlen(fragment_token), &id) != DOM_NO_ERR || id == NULL) {
        return 1;
    }
    (void) dom_document_get_element_by_id(doc, id, &element);
    dom_string_unref(id);
    if (element != NULL) {
        /* A real id wins even when its box is not usable; do not silently
         * select a legacy name anchor behind it. */
        result = pcore_fragment_info_for_node(st->root_box,
                (dom_node *) element, x, y, w, h);
        dom_node_unref((dom_node *) element);
        return result;
    }
    element = pcore_fragment_anchor_by_name(doc, fragment_token);
    if (element == NULL) {
        return 1;
    }
    result = pcore_fragment_info_for_node(st->root_box,
            (dom_node *) element, x, y, w, h);
    dom_node_unref((dom_node *) element);
    return result;
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

static int pcore_overflow_copy_element_id(struct box *box, char *out_id,
        int out_capacity, int *out_bytes)
{
    dom_string *name;
    dom_string *value;
    dom_node_type node_type;
    const char *data;
    size_t length;
    size_t copy_length;
    int result;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (out_id != NULL && out_capacity > 0) {
        out_id[0] = '\0';
    }
    if (box == NULL || box->node == NULL || out_capacity < 0 ||
            (out_id == NULL && out_capacity > 0)) {
        return 1;
    }
    if (dom_node_get_node_type(box->node, &node_type) != DOM_NO_ERR ||
            node_type != DOM_ELEMENT_NODE) {
        return 2;
    }
    name = NULL;
    value = NULL;
    result = 1;
    if (dom_string_create((const uint8_t *) "id", 2, &name) ==
            DOM_NO_ERR && name != NULL &&
            dom_element_get_attribute((dom_element *) box->node, name,
            &value) == DOM_NO_ERR && value != NULL) {
        data = dom_string_data(value);
        length = dom_string_byte_length(value);
        if ((data != NULL || length == 0) && length <= (size_t) INT_MAX) {
            if (out_bytes != NULL) {
                *out_bytes = (int) length;
            }
            if (length == 0) {
                result = 2;
            } else {
                copy_length = length;
                if (out_id != NULL && out_capacity > 0) {
                    if (copy_length > (size_t) (out_capacity - 1)) {
                        copy_length = (size_t) (out_capacity - 1);
                    }
                    if (copy_length > 0) {
                        memcpy(out_id, data, copy_length);
                    }
                    out_id[copy_length] = '\0';
                }
                result = 0;
            }
        }
    }
    if (value != NULL) {
        dom_string_unref(value);
    }
    if (name != NULL) {
        dom_string_unref(name);
    }
    return result;
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
        st->overflow_target_box = NULL;
        scrollbar = pcore_scrollbar_at(st->root_box, x, y,
                &origin_x, &origin_y, &st->overflow_dirty_x,
                &st->overflow_dirty_y, &st->overflow_dirty_w,
                &st->overflow_dirty_h);
        if (scrollbar == NULL) {
            return 0;
        }
        st->overflow_dirty_valid = 1;
        st->active_scrollbar = scrollbar;
        st->overflow_target_box = pcore_scrollbar_owner(scrollbar);
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

PCORE_API int PCore_OverflowScrollSnapshot(HANDLE hDoc, char *element_id,
        int element_capacity, int *element_bytes, int *scroll_x,
        int *scroll_y)
{
    dom_document *doc;
    pcore_render *st;
    int result;

    if (element_bytes != NULL) {
        *element_bytes = 0;
    }
    if (scroll_x != NULL) {
        *scroll_x = 0;
    }
    if (scroll_y != NULL) {
        *scroll_y = 0;
    }
    if (element_capacity < 0 || (element_id == NULL && element_capacity > 0) ||
            element_bytes == NULL || scroll_x == NULL || scroll_y == NULL ||
            hDoc == NULL) {
        return 1;
    }
    doc = (dom_document *) hDoc;
    st = pcore_get_render(doc);
    if (st == NULL || st->overflow_target_box == NULL) {
        return 2;
    }
    result = pcore_overflow_copy_element_id(st->overflow_target_box,
            element_id, element_capacity, element_bytes);
    if (result != 0) {
        return result;
    }
    result = pcore_box_overflow_scroll_for_node(doc,
            st->overflow_target_box->node, -1, -1, scroll_x, scroll_y);
    return result;
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

/* Compose a modal presentation after the normal document has been painted.
 * WM6's GDI path has no reliable alpha compositor, so the public contract
 * uses one deterministic solid backdrop colour and redraws the addressed
 * dialog above it. The host supplies the Browser-owned active modal id; Core
 * supplies the document geometry and redraw ordering. */
static int pcore_paint_modal_overlay(pcore_render *st, dom_document *doc,
        HDC hdc, int scroll_x, int scroll_y, const char *dialog_id)
{
    dom_string *id_name;
    dom_element *element;
    struct box *dialog_box;
    RECT viewport_rect;
    RECT paint_rect;
    RECT dialog_rect;
    RECT dialog_clip;
    HBRUSH backdrop;
    int id_length;
    int ax;
    int ay;
    int status;
    struct redraw_context rc;
    struct content_redraw_data data;
    struct rect clip;
    struct { HDC hdc; } pv;

    if (st == NULL || doc == NULL || hdc == NULL || dialog_id == NULL ||
            dialog_id[0] == '\0') {
        return PCORE_MODAL_PAINT_NONE;
    }
    id_length = 0;
    while (id_length < PCORE_MODAL_DIALOG_ID_MAX &&
            dialog_id[id_length] != '\0') {
        id_length++;
    }
    SetRect(&viewport_rect, 0, 0, st->vw, st->vh);
    /* Use the full logical viewport here. GDI still applies the HDC's
     * invalid-region clip during FillRect/html_redraw, while GetClipBox can
     * report a transient or implementation-specific region on WM6 memory
     * and window DCs. Using that region as the composition bounds could leave
     * the visible modal backdrop partially unpainted. */
    paint_rect = viewport_rect;
    backdrop = CreateSolidBrush((COLORREF) 0x00c0c0c0UL);
    if (backdrop != NULL) {
        FillRect(hdc, &paint_rect, backdrop);
        DeleteObject(backdrop);
    } else {
        FillRect(hdc, &paint_rect,
                (HBRUSH) GetStockObject(GRAY_BRUSH));
    }
    status = PCORE_MODAL_PAINT_BACKDROP_ONLY;
    id_name = NULL;
    element = NULL;
    dialog_box = NULL;
    if (id_length <= 0 || id_length >= PCORE_MODAL_DIALOG_ID_MAX ||
            dom_string_create((const uint8_t *) dialog_id,
            (size_t) id_length, &id_name) != DOM_NO_ERR || id_name == NULL ||
            dom_document_get_element_by_id(doc, id_name, &element) !=
            DOM_NO_ERR || element == NULL ||
            !pcore_node_name_is((dom_node *) element, "dialog") ||
            !pcore_node_has_attr((dom_node *) element, "open")) {
        if (id_name != NULL) {
            dom_string_unref(id_name);
        }
        if (element != NULL) {
            dom_node_unref((dom_node *) element);
        }
        return status;
    }
    dom_string_unref(id_name);
    id_name = NULL;
    dialog_box = pcore_box_for_any_node(st->root_box,
            (dom_node *) element);
    if (dialog_box == NULL || dialog_box->width <= 0 ||
            dialog_box->height <= 0) {
        dom_node_unref((dom_node *) element);
        return status;
    }
    ax = 0;
    ay = 0;
    box_coords(dialog_box, &ax, &ay);
    SetRect(&dialog_rect, ax - scroll_x, ay - scroll_y,
            ax - scroll_x + dialog_box->width,
            ay - scroll_y + dialog_box->height);
    if (!IntersectRect(&dialog_clip, &paint_rect, &dialog_rect)) {
        dom_node_unref((dom_node *) element);
        return status;
    }
    pv.hdc = hdc;
    memset(&rc, 0, sizeof(rc));
    rc.background_images = true;
    rc.plot = &pcore_gdi_plotters;
    rc.priv = &pv;
    memset(&data, 0, sizeof(data));
    data.x = -scroll_x;
    data.y = -scroll_y;
    data.width = st->vw;
    data.height = st->vh;
    data.background_colour = 0x00ffffff;
    data.scale = 1.0f;
    clip.x0 = dialog_clip.left;
    clip.y0 = dialog_clip.top;
    clip.x1 = dialog_clip.right;
    clip.y1 = dialog_clip.bottom;
    html_redraw((struct content *) &st->content, &data, &clip, &rc);
    status = PCORE_MODAL_PAINT_APPLIED;
    dom_node_unref((dom_node *) element);
    return status;
}

PCORE_API int PCore_PaintDocumentWithModal(HANDLE hDoc, HDC hdc,
        int scroll_x, int scroll_y, const char *dialog_id)
{
    pcore_render *st;
    int saved_dc;
    int status;

    st = pcore_get_render((dom_document *) hDoc);
    if (st == NULL || hdc == NULL) {
        return -1;
    }
    /* NetSurf's GDI plotter installs a clip for each redraw operation and
     * leaves the last one selected. Restore the caller's DC state before the
     * composition pass; otherwise the backdrop can inherit a child-box clip
     * and leave the rest of the viewport untouched. */
    saved_dc = SaveDC(hdc);
    PCore_PaintDocument(hDoc, hdc, scroll_x, scroll_y);
    if (saved_dc > 0) {
        RestoreDC(hdc, saved_dc);
    }
    if (dialog_id == NULL || dialog_id[0] == '\0') {
        return PCORE_MODAL_PAINT_NONE;
    }
    saved_dc = SaveDC(hdc);
    status = pcore_paint_modal_overlay(st, (dom_document *) hDoc, hdc,
            scroll_x, scroll_y, dialog_id);
    if (saved_dc > 0) {
        RestoreDC(hdc, saved_dc);
    }
    return status;
}
