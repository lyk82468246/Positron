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
#include <limits.h>
#include <stdlib.h>   /* malloc / free for the per-document render state */
#include <string.h>

#include <dom/dom.h>
#include <dom/html/html_button_element.h>
#include <dom/html/html_collection.h>
#include <dom/html/html_form_element.h>
#include <dom/html/html_input_element.h>
#include <dom/html/html_label_element.h>
#include <dom/html/html_option_element.h>
#include <dom/html/html_options_collection.h>
#include <dom/html/html_select_element.h>
#include <dom/html/html_text_area_element.h>
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

static struct box *pcore_hit(struct box *box, int px, int py);
static struct box *pcore_box_for_node(struct box *box, dom_node *node);

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
        if (dom_html_button_element_get_disabled(button, &disabled) !=
                DOM_NO_ERR) {
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
        if (dom_html_input_element_get_disabled(input, &disabled) !=
                DOM_NO_ERR) {
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
    if (dom_html_select_element_get_disabled(select, &disabled) !=
            DOM_NO_ERR) {
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
        if (dom_html_text_area_element_get_disabled(textarea,
                &disabled) != DOM_NO_ERR) {
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
    if (dom_html_input_element_get_disabled(input, &disabled) != DOM_NO_ERR) {
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
                    if (gadget_type != 0) {
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
                    int gadget_type = pcore_form_control_type(child);
                    if (gadget_type != 0) {
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
        *disabled = box->gadget->disabled ? 1 : 0;
    }
    return 0;
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
        *disabled = box->gadget->disabled ? 1 : 0;
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
        out_info->disabled = box->gadget->disabled ? 1 : 0;
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

    if (value == NULL || path == NULL) {
        return 1;
    }
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_file_input_at_index(st->root_box, file_index,
                    &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL || control->disabled) {
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
        out_info->disabled = control->disabled ? 1 : 0;
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
    if (control->disabled || read_only) {
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
    out_info->disabled = control->disabled ? 1 : 0;
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
    option_disabled = false;
    if (dom_html_option_element_get_disabled(
            (dom_html_option_element *) option->node,
            &option_disabled) != DOM_NO_ERR) {
        option_disabled = pcore_node_has_attr(option->node,
                "disabled") ? true : false;
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
    option_disabled = false;
    if (dom_html_option_element_get_disabled(
            (dom_html_option_element *) target->node,
            &option_disabled) != DOM_NO_ERR) {
        option_disabled = pcore_node_has_attr(target->node,
                "disabled") ? true : false;
    }
    if (control->disabled || option_disabled) {
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
    if (dom_html_input_element_get_disabled(input, &disabled) !=
            DOM_NO_ERR || disabled) {
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
    if (dom_html_text_area_element_get_disabled(textarea, &disabled) !=
            DOM_NO_ERR || disabled) {
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
    bool selected;
    int append;

    options = NULL;
    name = NULL;
    node = NULL;
    value = NULL;
    count = 0;
    disabled = false;
    if (dom_html_select_element_get_disabled(select, &disabled) !=
            DOM_NO_ERR || disabled) {
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
            if (dom_html_option_element_get_value(
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
    if (dom_html_button_element_get_disabled(button, &disabled) !=
            DOM_NO_ERR || disabled) {
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

static int pcore_form_build_data(dom_html_form_element *form,
        dom_node *activated, pcore_form_buffer *buffer)
{
    dom_html_collection *elements;
    dom_node *node;
    uint32_t count;
    uint32_t index;
    int result;

    elements = NULL;
    node = NULL;
    count = 0;
    if (dom_html_form_element_get_elements(form, &elements) != DOM_NO_ERR ||
            elements == NULL ||
            dom_html_collection_get_length(elements, &count) != DOM_NO_ERR) {
        if (elements != NULL) {
            dom_html_collection_unref(elements);
        }
        return 0;
    }
    result = 1;
    for (index = 0; index < count && result; index++) {
        node = NULL;
        if (dom_html_collection_item(elements, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            result = 0;
        } else if (pcore_node_name_is(node, "input")) {
            result = pcore_form_append_input(buffer,
                    (dom_html_input_element *) node, activated);
        } else if (pcore_node_name_is(node, "textarea")) {
            result = pcore_form_append_textarea(buffer,
                    (dom_html_text_area_element *) node);
        } else if (pcore_node_name_is(node, "select")) {
            result = pcore_form_append_select(buffer,
                    (dom_html_select_element *) node);
        } else if (pcore_node_name_is(node, "button")) {
            result = pcore_form_append_button(buffer,
                    (dom_html_button_element *) node, activated);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
    }
    dom_html_collection_unref(elements);
    return result;
}

static dom_html_form_element *pcore_control_form(struct form_control *control)
{
    dom_html_form_element *form;

    form = NULL;
    if (control == NULL || control->node == NULL) {
        return NULL;
    }
    if (pcore_node_name_is(control->node, "button")) {
        dom_html_button_element_get_form(
                (dom_html_button_element *) control->node, &form);
    } else if (pcore_node_name_is(control->node, "input")) {
        dom_html_input_element_get_form(
                (dom_html_input_element *) control->node, &form);
    } else if (pcore_node_name_is(control->node, "textarea")) {
        dom_html_text_area_element_get_form(
                (dom_html_text_area_element *) control->node, &form);
    } else if (pcore_node_name_is(control->node, "select")) {
        dom_html_select_element_get_form(
                (dom_html_select_element *) control->node, &form);
    }
    return form;
}

static dom_node *pcore_form_first_submit(dom_html_form_element *form,
        int *error)
{
    dom_html_collection *elements;
    dom_node *node;
    uint32_t count;
    uint32_t index;
    bool disabled;
    int is_submit;

    elements = NULL;
    node = NULL;
    count = 0;
    *error = 0;
    if (dom_html_form_element_get_elements(form, &elements) != DOM_NO_ERR ||
            elements == NULL ||
            dom_html_collection_get_length(elements, &count) != DOM_NO_ERR) {
        if (elements != NULL) {
            dom_html_collection_unref(elements);
        }
        *error = 1;
        return NULL;
    }
    for (index = 0; index < count; index++) {
        node = NULL;
        disabled = false;
        is_submit = 0;
        if (dom_html_collection_item(elements, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            *error = 1;
            break;
        }
        if (pcore_node_name_is(node, "input")) {
            if (dom_html_input_element_get_disabled(
                    (dom_html_input_element *) node, &disabled) !=
                    DOM_NO_ERR) {
                *error = 1;
            } else if (!disabled &&
                    pcore_attr_value_is(node, "type", "submit")) {
                is_submit = 1;
            }
        } else if (pcore_node_name_is(node, "button")) {
            if (dom_html_button_element_get_disabled(
                    (dom_html_button_element *) node, &disabled) !=
                    DOM_NO_ERR) {
                *error = 1;
            } else if (!disabled &&
                    !pcore_attr_value_is(node, "type", "reset") &&
                    !pcore_attr_value_is(node, "type", "button")) {
                is_submit = 1;
            }
        }
        if (*error || is_submit) {
            break;
        }
        dom_node_unref(node);
        node = NULL;
    }
    dom_html_collection_unref(elements);
    if (*error && node != NULL) {
        dom_node_unref(node);
        node = NULL;
    }
    return node;
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

static int pcore_radio_group_checked(dom_html_form_element *form,
        dom_html_input_element *radio, int *checked_out)
{
    dom_html_collection *elements;
    dom_node *node;
    dom_string *name;
    dom_string *other_name;
    uint32_t count;
    uint32_t index;
    bool checked;
    int result;

    elements = NULL;
    node = NULL;
    name = NULL;
    other_name = NULL;
    count = 0;
    checked = false;
    *checked_out = 0;
    if (dom_html_input_element_get_name(radio, &name) != DOM_NO_ERR ||
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
    if (dom_html_form_element_get_elements(form, &elements) != DOM_NO_ERR ||
            elements == NULL ||
            dom_html_collection_get_length(elements, &count) != DOM_NO_ERR) {
        if (elements != NULL) {
            dom_html_collection_unref(elements);
        }
        dom_string_unref(name);
        return 0;
    }
    result = 1;
    for (index = 0; index < count && !*checked_out; index++) {
        node = NULL;
        other_name = NULL;
        checked = false;
        if (dom_html_collection_item(elements, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            result = 0;
        } else if (pcore_node_name_is(node, "input") &&
                pcore_attr_value_is(node, "type", "radio")) {
            if (dom_html_input_element_get_name(
                    (dom_html_input_element *) node, &other_name) !=
                            DOM_NO_ERR ||
                    dom_html_input_element_get_checked(
                    (dom_html_input_element *) node, &checked) !=
                            DOM_NO_ERR) {
                result = 0;
            } else if (other_name != NULL &&
                    dom_string_isequal(name, other_name) && checked) {
                *checked_out = 1;
            }
        }
        if (other_name != NULL) {
            dom_string_unref(other_name);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
        if (!result) {
            break;
        }
    }
    dom_html_collection_unref(elements);
    dom_string_unref(name);
    return result;
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

static int pcore_required_control_missing(dom_html_form_element *form,
        dom_node *node, int *kind_out, int *missing_out)
{
    dom_string *value;
    bool disabled;
    bool read_only;
    bool checked;
    int gadget_type;
    int group_checked;

    value = NULL;
    disabled = false;
    read_only = false;
    checked = false;
    *kind_out = 0;
    *missing_out = 0;
    if (!pcore_node_has_attr(node, "required")) {
        return 1;
    }
    gadget_type = pcore_form_control_type(node);
    *kind_out = pcore_public_control_kind(gadget_type);
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
        }
        if (gadget_type == GADGET_CHECKBOX) {
            if (dom_html_input_element_get_checked(
                    (dom_html_input_element *) node, &checked) !=
                            DOM_NO_ERR) {
                return 0;
            }
            *missing_out = checked ? 0 : 1;
            return 1;
        }
        if (gadget_type == GADGET_RADIO) {
            group_checked = 0;
            if (!pcore_radio_group_checked(form,
                    (dom_html_input_element *) node, &group_checked)) {
                return 0;
            }
            *missing_out = group_checked ? 0 : 1;
            return 1;
        }
        if (dom_html_input_element_get_value(
                (dom_html_input_element *) node, &value) != DOM_NO_ERR) {
            return 0;
        }
        *missing_out = (value == NULL ||
                dom_string_byte_length(value) == 0) ? 1 : 0;
        if (value != NULL) {
            dom_string_unref(value);
        }
        return 1;
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
        *missing_out = (value == NULL ||
                dom_string_byte_length(value) == 0) ? 1 : 0;
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
        return pcore_required_select_missing(
                (dom_html_select_element *) node, missing_out);
    }
    return 1;
}

static int pcore_form_validate(pcore_render *st,
        dom_html_form_element *form, dom_node *activated,
        PCoreFormValidationInfo *out_info)
{
    PCoreFormValidationInfo local_info;
    PCoreFormValidationInfo *info;
    dom_html_collection *elements;
    dom_node *node;
    struct box *box;
    uint32_t count;
    uint32_t index;
    int kind;
    int missing;
    int x;
    int y;

    info = (out_info != NULL) ? out_info : &local_info;
    pcore_form_validation_init(info);
    if (st == NULL || form == NULL) {
        return 0;
    }
    if (pcore_node_has_attr((dom_node *) form, "novalidate") ||
            (activated != NULL &&
             pcore_node_has_attr(activated, "formnovalidate"))) {
        return 1;
    }
    elements = NULL;
    node = NULL;
    count = 0;
    if (dom_html_form_element_get_elements(form, &elements) != DOM_NO_ERR ||
            elements == NULL ||
            dom_html_collection_get_length(elements, &count) != DOM_NO_ERR) {
        if (elements != NULL) {
            dom_html_collection_unref(elements);
        }
        return 0;
    }
    for (index = 0; index < count; index++) {
        node = NULL;
        kind = 0;
        missing = 0;
        if (dom_html_collection_item(elements, index, &node) != DOM_NO_ERR ||
                node == NULL ||
                !pcore_required_control_missing(form, node,
                        &kind, &missing)) {
            if (node != NULL) {
                dom_node_unref(node);
            }
            dom_html_collection_unref(elements);
            return 0;
        }
        if (missing) {
            info->valid = 0;
            info->invalid_count++;
            if (info->invalid_count == 1) {
                info->first_control_kind = kind;
                info->first_flags = PCORE_VALIDITY_VALUE_MISSING;
                box = pcore_box_for_node(st->root_box, node);
                if (box != NULL) {
                    x = 0;
                    y = 0;
                    box_coords(box, &x, &y);
                    info->first_x = x;
                    info->first_y = y;
                    info->first_width = box->width;
                    info->first_height = box->height;
                }
            }
        }
        dom_node_unref(node);
    }
    dom_html_collection_unref(elements);
    return 1;
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
    if (control == NULL || control->type != GADGET_SUBMIT ||
            control->disabled) {
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

    pcore_form_validation_init(out_info);
    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
             control->type != GADGET_PASSWORD) ||
            control->disabled) {
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
    if (dom_html_input_element_get_disabled(input, &disabled) !=
            DOM_NO_ERR || disabled) {
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
    if (dom_html_text_area_element_get_disabled(textarea, &disabled) !=
            DOM_NO_ERR || disabled) {
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
    bool selected;
    int result;

    options = NULL;
    name = NULL;
    node = NULL;
    value = NULL;
    count = 0;
    disabled = false;
    if (dom_html_select_element_get_disabled(select, &disabled) !=
            DOM_NO_ERR || disabled) {
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
            if (dom_html_option_element_get_value(
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
    if (dom_html_button_element_get_disabled(button, &disabled) !=
            DOM_NO_ERR || disabled) {
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

static int pcore_multipart_build_parts(dom_html_form_element *form,
        dom_node *activated, pcore_multipart_submission *submission)
{
    dom_html_collection *elements;
    dom_node *node;
    uint32_t count;
    uint32_t index;
    int result;

    elements = NULL;
    node = NULL;
    count = 0;
    if (dom_html_form_element_get_elements(form, &elements) != DOM_NO_ERR ||
            elements == NULL ||
            dom_html_collection_get_length(elements, &count) != DOM_NO_ERR) {
        if (elements != NULL) {
            dom_html_collection_unref(elements);
        }
        return 0;
    }
    result = 1;
    for (index = 0; index < count && result; index++) {
        node = NULL;
        if (dom_html_collection_item(elements, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            result = 0;
        } else if (pcore_node_name_is(node, "input")) {
            result = pcore_multipart_append_input(submission,
                    (dom_html_input_element *) node, activated);
        } else if (pcore_node_name_is(node, "textarea")) {
            result = pcore_multipart_append_textarea(submission,
                    (dom_html_text_area_element *) node);
        } else if (pcore_node_name_is(node, "select")) {
            result = pcore_multipart_append_select(submission,
                    (dom_html_select_element *) node);
        } else if (pcore_node_name_is(node, "button")) {
            result = pcore_multipart_append_button(submission,
                    (dom_html_button_element *) node, activated);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
    }
    dom_html_collection_unref(elements);
    return result;
}

static pcore_multipart_submission *pcore_multipart_snapshot(
        pcore_render *st, dom_html_form_element *form, dom_node *activated,
        int choose_default)
{
    pcore_multipart_submission *submission;
    PCoreFormValidationInfo validation;
    dom_string *action;
    dom_node *default_submit;
    int default_error;

    if (form == NULL ||
            !pcore_attr_value_is((dom_node *) form, "method", "post") ||
            !pcore_attr_value_is((dom_node *) form, "enctype",
                    "multipart/form-data")) {
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
    if (!pcore_form_validate(st, form, activated, &validation) ||
            !validation.valid) {
        if (default_submit != NULL) {
            dom_node_unref(default_submit);
        }
        pcore_multipart_free(submission);
        return NULL;
    }
    if (dom_html_form_element_get_action(form, &action) != DOM_NO_ERR) {
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

static int pcore_form_submission(pcore_render *st,
        dom_html_form_element *form,
        dom_node *activated, int choose_default,
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
    method = 1;
    result = 4;
    if (choose_default) {
        default_error = 0;
        default_submit = pcore_form_first_submit(form, &default_error);
        if (default_error) {
            goto form_submission_done;
        }
        activated = default_submit;
    }
    if (!pcore_form_validate(st, form, activated, &validation)) {
        goto form_submission_done;
    }
    if (!validation.valid) {
        result = 5;
        goto form_submission_done;
    }
    if (dom_html_form_element_get_action(form, &action_string) !=
            DOM_NO_ERR) {
        goto form_submission_done;
    }
    if (pcore_attr_value_is((dom_node *) form, "method", "post")) {
        method = 2;
        if (pcore_attr_value_is((dom_node *) form, "enctype",
                "multipart/form-data")) {
            method = 3;
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
    if (method == 3) {
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
    if (control->disabled || control->type != GADGET_SUBMIT) {
        return 2;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 2;
    }
    result = pcore_form_submission(st, form, control->node, 0, out_info,
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

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
             control->type != GADGET_PASSWORD) ||
            control->disabled) {
        return 0;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return 0;
    }
    result = pcore_form_submission(st, form, NULL, 1, out_info,
            action, action_capacity, body, body_capacity);
    dom_node_unref((dom_node *) form);
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
    if (control == NULL || control->type != GADGET_SUBMIT ||
            control->disabled) {
        return NULL;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return NULL;
    }
    submission = pcore_multipart_snapshot(st, form, control->node, 0);
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

    st = pcore_get_render((dom_document *) hDoc);
    current = 0;
    box = (st != NULL) ?
            pcore_text_input_at(st->root_box, text_index, &current) : NULL;
    control = (box != NULL) ? box->gadget : NULL;
    if (control == NULL ||
            (control->type != GADGET_TEXTBOX &&
             control->type != GADGET_PASSWORD) ||
            control->disabled) {
        return NULL;
    }
    form = pcore_control_form(control);
    if (form == NULL) {
        return NULL;
    }
    submission = pcore_multipart_snapshot(st, form, NULL, 1);
    dom_node_unref((dom_node *) form);
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

static int pcore_form_reset(dom_html_form_element *form)
{
    dom_html_collection *elements;
    dom_node *node;
    uint32_t count;
    uint32_t index;
    int result;

    elements = NULL;
    node = NULL;
    count = 0;
    if (dom_html_form_element_reset(form) != DOM_NO_ERR ||
            dom_html_form_element_get_elements(form, &elements) !=
                    DOM_NO_ERR ||
            elements == NULL ||
            dom_html_collection_get_length(elements, &count) != DOM_NO_ERR) {
        if (elements != NULL) {
            dom_html_collection_unref(elements);
        }
        return 1;
    }
    result = 0;
    for (index = 0; index < count && !result; index++) {
        node = NULL;
        if (dom_html_collection_item(elements, index, &node) != DOM_NO_ERR ||
                node == NULL) {
            result = 1;
        } else if (pcore_node_name_is(node, "input")) {
            result = pcore_form_reset_input(
                    (dom_html_input_element *) node);
        } else if (pcore_node_name_is(node, "textarea")) {
            result = pcore_form_reset_textarea(
                    (dom_html_text_area_element *) node);
        } else if (pcore_node_name_is(node, "select")) {
            result = pcore_form_reset_select(
                    (dom_html_select_element *) node);
        }
        if (node != NULL) {
            dom_node_unref(node);
        }
    }
    dom_html_collection_unref(elements);
    return result;
}

PCORE_API int PCore_FormResetAt(HANDLE hDoc, int x, int y)
{
    pcore_render *st;
    struct box *hit;
    struct box *box;
    struct form_control *control;
    dom_html_form_element *form;
    int result;

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
    if (control->disabled) {
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
    dom_html_input_element_get_form(
            (dom_html_input_element *) left->node, &left_form);
    dom_html_input_element_get_form(
            (dom_html_input_element *) right->node, &right_form);
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
    if (control->disabled) {
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
