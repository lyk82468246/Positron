/*
 * pcore_select.c - CSS selection / computed style for positron_core.
 *
 * Engine layer 2 (after parse): apply a parsed stylesheet to the DOM and read
 * back computed styles via libcss's selection machinery. This file provides:
 *
 *   - a libdom-backed css_select_handler (the ~35 callbacks libcss uses to
 *     interrogate the document tree), and
 *   - PCore_ComputeColor(), the first public probe of the layer.
 *
 * The handler is modelled on NetSurf's content/handlers/css/select.c, but is
 * self-contained: it talks to libdom through core primitives only (no NetSurf
 * "corestring" table, no html-content structures). Selectors that need more
 * machinery than this first cut provides - dynamic pseudo-classes (:visited,
 * ...) - are stubbed to "no match"; :hover is backed by the document
 * interaction state; type / class / id / attribute /
 * static pseudo-classes / descendant / sibling structure all work.
 *
 * C89 only.
 */

#include <windows.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>   /* malloc / free (box geometry) */

#include <dom/dom.h>
#include <dom/core/namednodemap.h>
#include <dom/html/html_button_element.h>
#include <dom/html/html_input_element.h>
#include <dom/html/html_option_element.h>
#include <dom/html/html_options_collection.h>
#include <dom/html/html_select_element.h>
#include <dom/html/html_text_area_element.h>

#include <libcss/libcss.h>
#include <libcss/fpmath.h>
#include <libwapcaplet/libwapcaplet.h>

#include "positron_core.h"
#include "positron_image.h"
#include "pcore_internal.h"

/* Client data threaded through css_select_style into the handler. */
typedef struct pcore_select_pw {
    lwc_string *universal;   /* interned "*", for node_has_name */
    dom_node *focus_node;     /* borrowed from document interaction state */
    dom_node *active_node;
    dom_node *hover_node;
} pcore_select_pw;

/* libcss owns the value stored under this key. It must remain attached while
 * descendants are selected because their bloom filters borrow parent data. */
static dom_string *pcore_libcss_node_data_key = NULL;

/* A device viewport is converted to CSS pixels before selection/layout, while
 * layout_document still receives the original device-pixel extent. The flag
 * is consumed by the next PCore_LayoutDocument call. Legacy PCore_SetViewport
 * callers retain the existing explicit-CSS-pixel behaviour. */
int pcore_device_viewport_pending = 0;

/* Defined by the NetSurf HTML shim and used by widgets outside libcss. */
extern css_fixed nscss_screen_dpi;

typedef struct pcore_interaction_state {
    dom_node *focus_node;
    dom_node *active_node;
    dom_node *hover_node;
} pcore_interaction_state;

static dom_string *pcore_interaction_state_key = NULL;

static int pcore_ensure_interaction_state_key(void)
{
    static const char *name = "__pcore_interaction_state__";

    if (pcore_interaction_state_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &pcore_interaction_state_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_interaction_state_handler(dom_node_operation operation,
        dom_string *key, void *data, dom_node *source, dom_node *target)
{
    pcore_interaction_state *state;

    (void) key;
    (void) source;
    (void) target;
    if (operation != DOM_NODE_DELETED || data == NULL) {
        return;
    }
    state = (pcore_interaction_state *) data;
    if (state->focus_node != NULL) {
        dom_node_unref(state->focus_node);
    }
    if (state->active_node != NULL) {
        dom_node_unref(state->active_node);
    }
    if (state->hover_node != NULL) {
        dom_node_unref(state->hover_node);
    }
    free(state);
}

static pcore_interaction_state *pcore_interaction_state_get(
        dom_document *doc, int create)
{
    pcore_interaction_state *state;
    void *data;
    void *old;

    if (doc == NULL || pcore_ensure_interaction_state_key() != 0) {
        return NULL;
    }
    data = NULL;
    if (dom_node_get_user_data((dom_node *) doc,
            pcore_interaction_state_key, &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL || !create) {
        return (pcore_interaction_state *) data;
    }
    state = (pcore_interaction_state *) calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }
    old = NULL;
    if (dom_node_set_user_data((dom_node *) doc,
            pcore_interaction_state_key, state,
            pcore_interaction_state_handler, &old) != DOM_NO_ERR) {
        free(state);
        return NULL;
    }
    if (old != NULL && old != state) {
        pcore_interaction_state_handler(DOM_NODE_DELETED,
                pcore_interaction_state_key, old,
                (dom_node *) doc, NULL);
    }
    return state;
}

static int pcore_interaction_replace_node(dom_node **slot, dom_node *node)
{
    dom_node *old;

    if (*slot == node) {
        return 0;
    }
    old = *slot;
    *slot = (node != NULL) ? dom_node_ref(node) : NULL;
    if (old != NULL) {
        dom_node_unref(old);
    }
    return 1;
}

int pcore_interaction_set_node(dom_document *doc,
        unsigned int state_flags, dom_node *node)
{
    pcore_interaction_state *state;
    int changed;

    if (doc == NULL || state_flags == 0 ||
            (state_flags & ~(PCORE_INTERACTION_FOCUS |
                    PCORE_INTERACTION_ACTIVE |
                    PCORE_INTERACTION_HOVER)) != 0) {
        return -1;
    }
    state = pcore_interaction_state_get(doc, node != NULL);
    if (state == NULL) {
        return (node == NULL) ? 0 : -1;
    }
    changed = 0;
    if ((state_flags & PCORE_INTERACTION_FOCUS) != 0) {
        changed |= pcore_interaction_replace_node(
                &state->focus_node, node);
    }
    if ((state_flags & PCORE_INTERACTION_ACTIVE) != 0) {
        changed |= pcore_interaction_replace_node(
                &state->active_node, node);
    }
    if ((state_flags & PCORE_INTERACTION_HOVER) != 0) {
        changed |= pcore_interaction_replace_node(
                &state->hover_node, node);
    }
    return changed;
}

void pcore_interaction_snapshot(dom_document *doc,
        dom_node **focus_node, dom_node **active_node,
        dom_node **hover_node)
{
    pcore_interaction_state *state;

    state = pcore_interaction_state_get(doc, 0);
    if (focus_node != NULL) {
        *focus_node = (state != NULL) ? state->focus_node : NULL;
    }
    if (active_node != NULL) {
        *active_node = (state != NULL) ? state->active_node : NULL;
    }
    if (hover_node != NULL) {
        *hover_node = (state != NULL) ? state->hover_node : NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Handler callback forward declarations                               */
/* ------------------------------------------------------------------ */

static css_error node_name(void *pw, void *node, css_qname *qname);
static css_error node_classes(void *pw, void *node,
        lwc_string ***classes, uint32_t *n_classes);
static css_error node_id(void *pw, void *node, lwc_string **id);
static css_error named_ancestor_node(void *pw, void *node,
        const css_qname *qname, void **ancestor);
static css_error named_parent_node(void *pw, void *node,
        const css_qname *qname, void **parent);
static css_error named_sibling_node(void *pw, void *node,
        const css_qname *qname, void **sibling);
static css_error named_generic_sibling_node(void *pw, void *node,
        const css_qname *qname, void **sibling);
static css_error parent_node(void *pw, void *node, void **parent);
static css_error sibling_node(void *pw, void *node, void **sibling);
static css_error node_has_name(void *pw, void *node,
        const css_qname *qname, bool *match);
static css_error node_has_class(void *pw, void *node,
        lwc_string *name, bool *match);
static css_error node_has_id(void *pw, void *node,
        lwc_string *name, bool *match);
static css_error node_has_attribute(void *pw, void *node,
        const css_qname *qname, bool *match);
static css_error node_has_attribute_equal(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match);
static css_error node_has_attribute_dashmatch(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match);
static css_error node_has_attribute_includes(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match);
static css_error node_has_attribute_prefix(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match);
static css_error node_has_attribute_suffix(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match);
static css_error node_has_attribute_substring(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match);
static css_error node_is_root(void *pw, void *node, bool *match);
static css_error node_count_siblings(void *pw, void *node,
        bool same_name, bool after, int32_t *count);
static css_error node_is_empty(void *pw, void *node, bool *match);
static css_error node_is_link(void *pw, void *node, bool *match);
static css_error node_is_visited(void *pw, void *node, bool *match);
static css_error node_is_hover(void *pw, void *node, bool *match);
static css_error node_is_active(void *pw, void *node, bool *match);
static css_error node_is_focus(void *pw, void *node, bool *match);
static css_error node_is_enabled(void *pw, void *node, bool *match);
static css_error node_is_disabled(void *pw, void *node, bool *match);
static css_error node_is_checked(void *pw, void *node, bool *match);
static css_error node_is_target(void *pw, void *node, bool *match);
static css_error node_is_lang(void *pw, void *node,
        lwc_string *lang, bool *match);
static css_error node_presentational_hint(void *pw, void *node,
        uint32_t *nhints, css_hint **hints);
static css_error ua_default_for_property(void *pw, uint32_t property,
        css_hint *hint);
static css_error set_libcss_node_data(void *pw, void *node,
        void *libcss_node_data);
static css_error get_libcss_node_data(void *pw, void *node,
        void **libcss_node_data);
static bool pcore_node_name_matches(void *pw, dom_node *node,
        const css_qname *qname);

/* ------------------------------------------------------------------ */
/* Handler vtable + unit context (positional inits = C89-clean)        */
/* ------------------------------------------------------------------ */

static css_select_handler pcore_select_handler = {
    CSS_SELECT_HANDLER_VERSION_1,
    node_name,
    node_classes,
    node_id,
    named_ancestor_node,
    named_parent_node,
    named_sibling_node,
    named_generic_sibling_node,
    parent_node,
    sibling_node,
    node_has_name,
    node_has_class,
    node_has_id,
    node_has_attribute,
    node_has_attribute_equal,
    node_has_attribute_dashmatch,
    node_has_attribute_includes,
    node_has_attribute_prefix,
    node_has_attribute_suffix,
    node_has_attribute_substring,
    node_is_root,
    node_count_siblings,
    node_is_empty,
    node_is_link,
    node_is_visited,
    node_is_hover,
    node_is_active,
    node_is_focus,
    node_is_enabled,
    node_is_disabled,
    node_is_checked,
    node_is_target,
    node_is_lang,
    node_presentational_hint,
    ua_default_for_property,
    set_libcss_node_data,
    get_libcss_node_data
};

/* Unit-conversion context. Defaults to 800x600 @ 96dpi (constant expressions
 * so this is a valid static initialiser); the app overrides viewport + dpi via
 * PCore_SetViewport so styling/layout adapt to the real device. */
static css_unit_ctx pcore_unit_ctx = {
    800 * (1 << CSS_RADIX_POINT),   /* viewport_width    */
    600 * (1 << CSS_RADIX_POINT),   /* viewport_height   */
    16  * (1 << CSS_RADIX_POINT),   /* font_size_default */
    6   * (1 << CSS_RADIX_POINT),   /* font_size_minimum */
    96  * (1 << CSS_RADIX_POINT),   /* device_dpi        */
    NULL,                            /* root_style        */
    NULL,                            /* pw                */
    NULL                             /* measure           */
};

PCORE_API void PCore_SetViewport(int css_width, int css_height, int dpi)
{
    pcore_device_viewport_pending = 0;
    if (css_width > 0) {
        pcore_unit_ctx.viewport_width = css_width * (1 << CSS_RADIX_POINT);
    }
    if (css_height > 0) {
        pcore_unit_ctx.viewport_height = css_height * (1 << CSS_RADIX_POINT);
    }
    if (dpi > 0) {
        pcore_unit_ctx.device_dpi = dpi * (1 << CSS_RADIX_POINT);
        nscss_screen_dpi = dpi * (1 << CSS_RADIX_POINT);
    }
}

PCORE_API void PCore_SetDeviceViewport(int device_width, int device_height,
        int dpi)
{
    int css_width;
    int css_height;

    if (device_width <= 0 || device_height <= 0) {
        return;
    }
    if (dpi <= 0) {
        dpi = 96;
    }
    css_width = MulDiv(device_width, 96, dpi);
    css_height = MulDiv(device_height, 96, dpi);
    if (css_width < 1) {
        css_width = 1;
    }
    if (css_height < 1) {
        css_height = 1;
    }
    pcore_unit_ctx.viewport_width = css_width * (1 << CSS_RADIX_POINT);
    pcore_unit_ctx.viewport_height = css_height * (1 << CSS_RADIX_POINT);
    pcore_unit_ctx.device_dpi = dpi * (1 << CSS_RADIX_POINT);
    nscss_screen_dpi = dpi * (1 << CSS_RADIX_POINT);
    pcore_device_viewport_pending = 1;
}

/* Internal (pcore_internal.h): the engine's unit-conversion context (viewport +
 * dpi), for feeding NetSurf layout's html_content.unit_len_ctx. */
const css_unit_ctx *pcore_get_unit_ctx(void)
{
    return &pcore_unit_ctx;
}

/* libcss keeps CSS media-query dimensions separate from css_unit_ctx. A zero
 * media width makes every max-width query look mobile and every min-width
 * query fail, even when ordinary vw units use the right viewport. Keep both
 * sources in lockstep before selecting a document. */
static void pcore_init_screen_media(css_media *media)
{
    memset(media, 0, sizeof(*media));
    media->type = CSS_MEDIA_SCREEN;
    media->width = pcore_unit_ctx.viewport_width;
    media->height = pcore_unit_ctx.viewport_height;
    media->orientation = (media->width > media->height) ?
            CSS_MEDIA_ORIENTATION_LANDSCAPE : CSS_MEDIA_ORIENTATION_PORTRAIT;
}

/* ------------------------------------------------------------------ */
/* node name / classes / id                                            */
/* ------------------------------------------------------------------ */

static css_error node_name(void *pw, void *node, css_qname *qname)
{
    dom_node *n = (dom_node *) node;
    dom_string *name;
    dom_exception err;

    (void) pw;

    err = dom_node_get_node_name(n, &name);
    if (err != DOM_NO_ERR) {
        return CSS_NOMEM;
    }

    qname->ns = NULL;

    err = dom_string_intern(name, &qname->name);
    if (err != DOM_NO_ERR) {
        dom_string_unref(name);
        return CSS_NOMEM;
    }

    dom_string_unref(name);
    return CSS_OK;
}

static css_error node_classes(void *pw, void *node,
        lwc_string ***classes, uint32_t *n_classes)
{
    dom_node *n = (dom_node *) node;
    dom_exception err;

    (void) pw;

    *classes = NULL;
    *n_classes = 0;

    err = dom_element_get_classes(n, classes, n_classes);
    if (err != DOM_NO_ERR) {
        return CSS_NOMEM;
    }
    return CSS_OK;
}

static css_error node_id(void *pw, void *node, lwc_string **id)
{
    dom_node *n = (dom_node *) node;
    dom_string *attr;
    dom_exception err;

    (void) pw;

    *id = NULL;

    err = dom_html_element_get_id(n, &attr);
    if (err != DOM_NO_ERR) {
        return CSS_NOMEM;
    }

    if (attr != NULL) {
        err = dom_string_intern(attr, id);
        if (err != DOM_NO_ERR) {
            dom_string_unref(attr);
            return CSS_NOMEM;
        }
        dom_string_unref(attr);
    }

    return CSS_OK;
}

/* ------------------------------------------------------------------ */
/* tree navigation (core primitives; returned nodes are borrowed)      */
/* ------------------------------------------------------------------ */

/* Walk up to the first ancestor element whose name matches qname. */
static css_error named_ancestor_node(void *pw, void *node,
        const css_qname *qname, void **ancestor)
{
    dom_node *n = (dom_node *) node;
    dom_exception err;

    *ancestor = NULL;

    /* take our own ref so the unref dance below is balanced */
    n = dom_node_ref(n);

    while (n != NULL) {
        dom_node *parent;
        dom_node_type type;

        err = dom_node_get_parent_node(n, &parent);
        dom_node_unref(n);
        if (err != DOM_NO_ERR) {
            return CSS_OK;
        }
        n = parent;
        if (n == NULL) {
            break;
        }

        err = dom_node_get_node_type(n, &type);
        if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
            continue;
        }

        if (pcore_node_name_matches(pw, n, qname)) {
            dom_node_unref(n);   /* return borrowed */
            *ancestor = n;
            return CSS_OK;
        }
    }

    return CSS_OK;
}

static css_error named_parent_node(void *pw, void *node,
        const css_qname *qname, void **parent)
{
    dom_node *n = (dom_node *) node;
    dom_node *p = NULL;
    dom_node_type type;
    dom_exception err;
    bool match;

    *parent = NULL;

    err = dom_node_get_parent_node(n, &p);
    if (err != DOM_NO_ERR || p == NULL) {
        return CSS_OK;
    }

    err = dom_node_get_node_type(p, &type);
    if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
        dom_node_unref(p);
        return CSS_OK;
    }

    match = pcore_node_name_matches(pw, p, qname);
    dom_node_unref(p);           /* return borrowed */
    if (match) {
        *parent = p;
    }
    return CSS_OK;
}

static css_error pcore_prev_element(dom_node *start, dom_node **out)
{
    dom_node *n;
    dom_node *prev;
    dom_node_type type;
    dom_exception err;

    *out = NULL;
    err = dom_node_get_previous_sibling(start, &n);
    if (err != DOM_NO_ERR) {
        return CSS_OK;
    }

    while (n != NULL) {
        err = dom_node_get_node_type(n, &type);
        if (err != DOM_NO_ERR) {
            dom_node_unref(n);
            return CSS_OK;
        }
        if (type == DOM_ELEMENT_NODE) {
            dom_node_unref(n);       /* return borrowed */
            *out = n;
            return CSS_OK;
        }

        err = dom_node_get_previous_sibling(n, &prev);
        dom_node_unref(n);
        if (err != DOM_NO_ERR) {
            return CSS_OK;
        }
        n = prev;
    }

    return CSS_OK;
}

static bool pcore_node_name_matches(void *pw, dom_node *node,
        const css_qname *qname)
{
    pcore_select_pw *ctx = (pcore_select_pw *) pw;
    dom_string *name;
    dom_node_type type;
    dom_exception err;
    bool match;

    err = dom_node_get_node_type(node, &type);
    if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
        return false;
    }

    match = false;
    if (lwc_string_isequal(qname->name, ctx->universal, &match) ==
            lwc_error_ok && match) {
        return true;
    }

    err = dom_node_get_node_name(node, &name);
    if (err != DOM_NO_ERR || name == NULL) {
        return false;
    }
    match = dom_string_caseless_lwc_isequal(name, qname->name);
    dom_string_unref(name);
    return match;
}

/* Previous element sibling, optionally requiring a matching name. */
static css_error named_sibling_node(void *pw, void *node,
        const css_qname *qname, void **sibling)
{
    dom_node *prev;

    *sibling = NULL;

    if (pcore_prev_element((dom_node *) node, &prev) != CSS_OK) {
        return CSS_OK;
    }
    if (prev != NULL && pcore_node_name_matches(pw, prev, qname)) {
        *sibling = prev;
    }
    return CSS_OK;
}

static css_error named_generic_sibling_node(void *pw, void *node,
        const css_qname *qname, void **sibling)
{
    dom_node *n;
    dom_node *prev;
    dom_exception err;

    *sibling = NULL;

    err = dom_node_get_previous_sibling((dom_node *) node, &n);
    if (err != DOM_NO_ERR) {
        return CSS_OK;
    }
    while (n != NULL) {
        if (pcore_node_name_matches(pw, n, qname)) {
            dom_node_unref(n);       /* return borrowed */
            *sibling = n;
            return CSS_OK;
        }
        err = dom_node_get_previous_sibling(n, &prev);
        dom_node_unref(n);
        if (err != DOM_NO_ERR) {
            return CSS_OK;
        }
        n = prev;
    }

    return CSS_OK;
}

static css_error parent_node(void *pw, void *node, void **parent)
{
    dom_node *n = (dom_node *) node;
    dom_node *p = NULL;
    dom_node_type type;
    dom_exception err;

    (void) pw;
    *parent = NULL;

    err = dom_node_get_parent_node(n, &p);
    if (err != DOM_NO_ERR || p == NULL) {
        return CSS_OK;
    }

    err = dom_node_get_node_type(p, &type);
    dom_node_unref(p);               /* return borrowed */
    if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
        *parent = p;
    }
    return CSS_OK;
}

static css_error sibling_node(void *pw, void *node, void **sibling)
{
    (void) pw;
    return pcore_prev_element((dom_node *) node, (dom_node **) sibling);
}

/* ------------------------------------------------------------------ */
/* name / class / id matching                                          */
/* ------------------------------------------------------------------ */

static css_error node_has_name(void *pw, void *node,
        const css_qname *qname, bool *match)
{
    pcore_select_pw *ctx = (pcore_select_pw *) pw;
    dom_node *n = (dom_node *) node;
    lwc_error lerr;

    /* "*" matches any element */
    lerr = lwc_string_isequal(qname->name, ctx->universal, match);
    if (lerr == lwc_error_ok && *match == false) {
        dom_string *name;
        dom_exception err;

        err = dom_node_get_node_name(n, &name);
        if (err != DOM_NO_ERR) {
            return CSS_OK;
        }
        /* element names are case-insensitive in HTML */
        *match = dom_string_caseless_lwc_isequal(name, qname->name);
        dom_string_unref(name);
    }

    return CSS_OK;
}

static css_error node_has_class(void *pw, void *node,
        lwc_string *name, bool *match)
{
    dom_node *n = (dom_node *) node;

    (void) pw;

    /* libdom does the class-list parsing + comparison */
    if (dom_element_has_class(n, name, match) != DOM_NO_ERR) {
        *match = false;
    }
    return CSS_OK;
}

static css_error node_has_id(void *pw, void *node,
        lwc_string *name, bool *match)
{
    dom_node *n = (dom_node *) node;
    dom_string *attr;
    dom_exception err;

    (void) pw;
    *match = false;

    err = dom_html_element_get_id(n, &attr);
    if (err != DOM_NO_ERR) {
        return CSS_OK;
    }
    if (attr != NULL) {
        *match = dom_string_lwc_isequal(attr, name);
        dom_string_unref(attr);
    }
    return CSS_OK;
}

/* ------------------------------------------------------------------ */
/* attribute matching                                                  */
/* ------------------------------------------------------------------ */

static css_error pcore_attr_name(const css_qname *qname, dom_string **name)
{
    dom_exception err;

    err = dom_string_create_interned(
            (const uint8_t *) lwc_string_data(qname->name),
            lwc_string_length(qname->name), name);
    return (err == DOM_NO_ERR) ? CSS_OK : CSS_NOMEM;
}

static css_error pcore_get_attr(dom_node *node, const css_qname *qname,
        dom_string **value)
{
    dom_string *name;
    dom_exception err;
    css_error ret;

    *value = NULL;
    ret = pcore_attr_name(qname, &name);
    if (ret != CSS_OK) {
        return ret;
    }
    err = dom_element_get_attribute(node, name, value);
    dom_string_unref(name);
    if (err != DOM_NO_ERR) {
        *value = NULL;
    }
    return CSS_OK;
}

static css_error node_has_attribute(void *pw, void *node,
        const css_qname *qname, bool *match)
{
    dom_string *name;
    dom_exception err;

    (void) pw;
    *match = false;
    if (pcore_attr_name(qname, &name) != CSS_OK) {
        return CSS_NOMEM;
    }
    err = dom_element_has_attribute(node, name, match);
    dom_string_unref(name);
    if (err != DOM_NO_ERR) {
        *match = false;
    }
    return CSS_OK;
}

static css_error node_has_attribute_equal(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    dom_string *attr;

    (void) pw;
    *match = false;
    if (lwc_string_length(value) == 0) {
        return CSS_OK;
    }
    if (pcore_get_attr(node, qname, &attr) != CSS_OK) {
        return CSS_NOMEM;
    }
    if (attr != NULL) {
        *match = dom_string_caseless_lwc_isequal(attr, value);
        dom_string_unref(attr);
    }
    return CSS_OK;
}

static css_error node_has_attribute_dashmatch(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    dom_string *attr;
    const char *data;
    const char *want;
    size_t len;
    size_t vlen;

    (void) pw;
    *match = false;
    vlen = lwc_string_length(value);
    if (vlen == 0) {
        return CSS_OK;
    }
    if (pcore_get_attr(node, qname, &attr) != CSS_OK) {
        return CSS_NOMEM;
    }
    if (attr != NULL) {
        *match = dom_string_caseless_lwc_isequal(attr, value);
        if (!*match) {
            data = dom_string_data(attr);
            len = dom_string_byte_length(attr);
            want = lwc_string_data(value);
            if (len > vlen && data[vlen] == '-' &&
                    strncasecmp(data, want, vlen) == 0) {
                *match = true;
            }
        }
        dom_string_unref(attr);
    }
    return CSS_OK;
}

static css_error node_has_attribute_includes(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    dom_string *attr;
    const char *start;
    const char *end;
    const char *p;
    size_t vlen;

    (void) pw;
    *match = false;
    vlen = lwc_string_length(value);
    if (vlen == 0) {
        return CSS_OK;
    }
    if (pcore_get_attr(node, qname, &attr) != CSS_OK) {
        return CSS_NOMEM;
    }
    if (attr != NULL) {
        start = dom_string_data(attr);
        end = start + dom_string_byte_length(attr);
        for (p = start; p <= end; p++) {
            if (*p == ' ' || *p == '\0') {
                if ((size_t) (p - start) == vlen &&
                        strncasecmp(start, lwc_string_data(value),
                                vlen) == 0) {
                    *match = true;
                    break;
                }
                start = p + 1;
            }
        }
        dom_string_unref(attr);
    }
    return CSS_OK;
}

static css_error node_has_attribute_prefix(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    dom_string *attr;
    const char *data;
    size_t len;
    size_t vlen;

    (void) pw;
    *match = false;
    vlen = lwc_string_length(value);
    if (vlen == 0) {
        return CSS_OK;
    }
    if (pcore_get_attr(node, qname, &attr) != CSS_OK) {
        return CSS_NOMEM;
    }
    if (attr != NULL) {
        *match = dom_string_caseless_lwc_isequal(attr, value);
        if (!*match) {
            data = dom_string_data(attr);
            len = dom_string_byte_length(attr);
            if (len >= vlen &&
                    strncasecmp(data, lwc_string_data(value), vlen) == 0) {
                *match = true;
            }
        }
        dom_string_unref(attr);
    }
    return CSS_OK;
}

static css_error node_has_attribute_suffix(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    dom_string *attr;
    const char *data;
    const char *start;
    size_t len;
    size_t vlen;

    (void) pw;
    *match = false;
    vlen = lwc_string_length(value);
    if (vlen == 0) {
        return CSS_OK;
    }
    if (pcore_get_attr(node, qname, &attr) != CSS_OK) {
        return CSS_NOMEM;
    }
    if (attr != NULL) {
        *match = dom_string_caseless_lwc_isequal(attr, value);
        if (!*match) {
            data = dom_string_data(attr);
            len = dom_string_byte_length(attr);
            if (len >= vlen) {
                start = data + len - vlen;
                if (strncasecmp(start, lwc_string_data(value), vlen) == 0) {
                    *match = true;
                }
            }
        }
        dom_string_unref(attr);
    }
    return CSS_OK;
}

static css_error node_has_attribute_substring(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    dom_string *attr;
    const char *start;
    const char *last_start;
    const char *want;
    size_t len;
    size_t vlen;

    (void) pw;
    *match = false;
    vlen = lwc_string_length(value);
    if (vlen == 0) {
        return CSS_OK;
    }
    if (pcore_get_attr(node, qname, &attr) != CSS_OK) {
        return CSS_NOMEM;
    }
    if (attr != NULL) {
        *match = dom_string_caseless_lwc_isequal(attr, value);
        if (!*match) {
            start = dom_string_data(attr);
            len = dom_string_byte_length(attr);
            want = lwc_string_data(value);
            if (len >= vlen) {
                last_start = start + len - vlen;
                while (start <= last_start) {
                    if (strncasecmp(start, want, vlen) == 0) {
                        *match = true;
                        break;
                    }
                    start++;
                }
            }
        }
        dom_string_unref(attr);
    }
    return CSS_OK;
}

/* ------------------------------------------------------------------ */
/* structural pseudo-classes                                           */
/* ------------------------------------------------------------------ */

static css_error node_is_root(void *pw, void *node, bool *match)
{
    dom_node *n = (dom_node *) node;
    dom_node *parent;
    dom_node_type type;
    dom_exception err;

    (void) pw;

    err = dom_node_get_parent_node(n, &parent);
    if (err != DOM_NO_ERR) {
        return CSS_NOMEM;
    }

    if (parent != NULL) {
        err = dom_node_get_node_type(parent, &type);
        dom_node_unref(parent);
        if (err != DOM_NO_ERR) {
            return CSS_NOMEM;
        }
        if (type != DOM_DOCUMENT_NODE) {
            *match = false;
            return CSS_OK;
        }
    }

    *match = true;
    return CSS_OK;
}

static int sibling_counts(dom_node *node, bool same_name, dom_string *name)
{
    dom_node_type type;
    dom_exception err;

    if (node == NULL) {
        return 0;
    }
    err = dom_node_get_node_type(node, &type);
    if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
        return 0;
    }
    if (same_name) {
        dom_string *nn = NULL;
        int ret = 0;
        err = dom_node_get_node_name(node, &nn);
        if (err == DOM_NO_ERR && nn != NULL) {
            if (dom_string_caseless_isequal(name, nn)) {
                ret = 1;
            }
            dom_string_unref(nn);
        }
        return ret;
    }
    return 1;
}

static css_error node_count_siblings(void *pw, void *node,
        bool same_name, bool after, int32_t *count)
{
    dom_node *n = (dom_node *) node;
    dom_string *name = NULL;
    dom_exception err;
    int32_t cnt = 0;

    (void) pw;

    if (same_name) {
        err = dom_node_get_node_name(n, &name);
        if (err != DOM_NO_ERR || name == NULL) {
            *count = 0;
            return CSS_OK;
        }
    }

    n = dom_node_ref(n);
    for (;;) {
        dom_node *next;

        if (after) {
            err = dom_node_get_next_sibling(n, &next);
        } else {
            err = dom_node_get_previous_sibling(n, &next);
        }
        dom_node_unref(n);
        if (err != DOM_NO_ERR) {
            break;
        }
        n = next;
        if (n == NULL) {
            break;
        }
        cnt += sibling_counts(n, same_name, name);
    }

    if (name != NULL) {
        dom_string_unref(name);
    }

    *count = cnt;
    return CSS_OK;
}

static css_error node_is_empty(void *pw, void *node, bool *match)
{
    dom_node *n = (dom_node *) node;
    dom_node *child;
    dom_exception err;

    (void) pw;
    *match = true;

    err = dom_node_get_first_child(n, &child);
    if (err != DOM_NO_ERR) {
        return CSS_OK;
    }

    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        err = dom_node_get_node_type(child, &type);
        if (err == DOM_NO_ERR &&
                (type == DOM_ELEMENT_NODE || type == DOM_TEXT_NODE)) {
            *match = false;
            dom_node_unref(child);
            return CSS_OK;
        }

        err = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        if (err != DOM_NO_ERR) {
            return CSS_OK;
        }
        child = next;
    }

    return CSS_OK;
}

/* ------------------------------------------------------------------ */
/* static / dynamic pseudo-classes                                    */
/* ------------------------------------------------------------------ */

static bool pcore_node_name_is(dom_node *node, const char *want)
{
    dom_string *name;
    dom_string *want_name;
    dom_node_type type;
    dom_exception err;
    bool match;

    err = dom_node_get_node_type(node, &type);
    if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
        return false;
    }

    err = dom_node_get_node_name(node, &name);
    if (err != DOM_NO_ERR || name == NULL) {
        return false;
    }
    err = dom_string_create_interned((const uint8_t *) want, strlen(want),
            &want_name);
    if (err != DOM_NO_ERR) {
        dom_string_unref(name);
        return false;
    }
    match = dom_string_caseless_isequal(name, want_name);
    dom_string_unref(want_name);
    dom_string_unref(name);
    return match;
}

static bool pcore_node_has_attr_cstr(dom_node *node, const char *attr_name)
{
    dom_string *name;
    dom_exception err;
    bool has_attr;

    err = dom_string_create_interned((const uint8_t *) attr_name,
            strlen(attr_name), &name);
    if (err != DOM_NO_ERR) {
        return false;
    }
    has_attr = false;
    err = dom_element_has_attribute(node, name, &has_attr);
    dom_string_unref(name);
    return (err == DOM_NO_ERR && has_attr);
}

static css_error node_is_link(void *pw, void *node, bool *match)
{
    dom_node *n = (dom_node *) node;

    (void) pw;
    *match = false;
    if (!pcore_node_has_attr_cstr(n, "href")) {
        return CSS_OK;
    }
    *match = pcore_node_name_is(n, "a") ||
            pcore_node_name_is(n, "area") ||
            pcore_node_name_is(n, "link");
    return CSS_OK;
}
static css_error node_is_visited(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
static css_error node_is_hover(void *pw, void *node, bool *match)
{
    pcore_select_pw *state;

    state = (pcore_select_pw *) pw;
    *match = (state != NULL && state->hover_node == (dom_node *) node);
    return CSS_OK;
}
static css_error node_is_active(void *pw, void *node, bool *match)
{
    pcore_select_pw *state;

    state = (pcore_select_pw *) pw;
    *match = (state != NULL && state->active_node == (dom_node *) node);
    return CSS_OK;
}
static css_error node_is_focus(void *pw, void *node, bool *match)
{
    pcore_select_pw *state;

    state = (pcore_select_pw *) pw;
    *match = (state != NULL && state->focus_node == (dom_node *) node);
    return CSS_OK;
}

static int pcore_node_disabled(dom_node *node, bool *applies,
        bool *disabled)
{
    return pcore_node_effectively_disabled(node, applies, disabled);
}

static css_error node_is_enabled(void *pw, void *node, bool *match)
{
    bool applies;
    bool disabled;

    (void) pw;
    *match = false;
    if (pcore_node_disabled((dom_node *) node, &applies, &disabled) == 0 &&
            applies) {
        *match = !disabled;
    }
    return CSS_OK;
}
static css_error node_is_disabled(void *pw, void *node, bool *match)
{
    bool applies;
    bool disabled;

    (void) pw;
    *match = false;
    if (pcore_node_disabled((dom_node *) node, &applies, &disabled) == 0 &&
            applies) {
        *match = disabled;
    }
    return CSS_OK;
}
static css_error node_is_checked(void *pw, void *node, bool *match)
{
    bool checked;

    (void) pw;
    *match = false;
    checked = false;
    if (pcore_node_name_is((dom_node *) node, "input")) {
        if (dom_html_input_element_get_checked(
                (dom_html_input_element *) node, &checked) == DOM_NO_ERR) {
            *match = checked;
        }
    } else if (pcore_node_name_is((dom_node *) node, "option")) {
        if (dom_html_option_element_get_selected(
                (dom_html_option_element *) node, &checked) == DOM_NO_ERR) {
            *match = checked;
        }
    }
    return CSS_OK;
}
static css_error node_is_target(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }

static css_error node_is_lang(void *pw, void *node,
        lwc_string *lang, bool *match)
{
    dom_node *n;
    dom_string *attr_name;
    size_t want_len;
    dom_exception err;

    (void) pw;
    *match = false;
    want_len = lwc_string_length(lang);
    if (want_len == 0) {
        return CSS_OK;
    }
    err = dom_string_create_interned((const uint8_t *) "lang", 4,
            &attr_name);
    if (err != DOM_NO_ERR) {
        return CSS_NOMEM;
    }

    n = dom_node_ref((dom_node *) node);
    while (n != NULL) {
        dom_node *parent;
        dom_node_type type;

        err = dom_node_get_node_type(n, &type);
        if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
            dom_string *attr = NULL;
            if (dom_element_get_attribute(n, attr_name, &attr) ==
                    DOM_NO_ERR && attr != NULL) {
                const char *data = dom_string_data(attr);
                size_t len = dom_string_byte_length(attr);
                const char *want = lwc_string_data(lang);
                if ((len == want_len ||
                        (len > want_len && data[want_len] == '-')) &&
                        strncasecmp(data, want, want_len) == 0) {
                    *match = true;
                    dom_string_unref(attr);
                    dom_node_unref(n);
                    break;
                }
                dom_string_unref(attr);
            }
        }

        err = dom_node_get_parent_node(n, &parent);
        dom_node_unref(n);
        if (err != DOM_NO_ERR) {
            break;
        }
        n = parent;
    }

    dom_string_unref(attr_name);
    return CSS_OK;
}

/* ------------------------------------------------------------------ */
/* presentational hints + UA defaults + node data                      */
/* ------------------------------------------------------------------ */

static css_error node_presentational_hint(void *pw, void *node,
        uint32_t *nhints, css_hint **hints)
{
    (void) pw; (void) node;
    *nhints = 0;
    *hints = NULL;
    return CSS_OK;
}

static css_error ua_default_for_property(void *pw, uint32_t property,
        css_hint *hint)
{
    (void) pw;

    if (property == CSS_PROP_COLOR) {
        hint->data.color = 0x00000000;
        hint->status = CSS_COLOR_COLOR;
    } else if (property == CSS_PROP_FONT_FAMILY) {
        hint->data.strings = NULL;
        hint->status = CSS_FONT_FAMILY_SANS_SERIF;
    } else if (property == CSS_PROP_QUOTES) {
        hint->data.strings = NULL;
        hint->status = CSS_QUOTES_NONE;
    } else if (property == CSS_PROP_VOICE_FAMILY) {
        hint->data.strings = NULL;
        hint->status = 0;
    } else {
        return CSS_INVALID;
    }

    return CSS_OK;
}

static int pcore_ensure_libcss_node_data_key(void)
{
    if (pcore_libcss_node_data_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) "__pcore_libcss_node_data__", 26,
            &pcore_libcss_node_data_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_libcss_node_data_ud_handler(dom_node_operation operation,
        dom_string *key, void *data, struct dom_node *src,
        struct dom_node *dst)
{
    css_node_data_action action;

    (void) key;
    if (data == NULL) {
        return;
    }

    if (operation == DOM_NODE_CLONED) {
        action = CSS_NODE_CLONED;
    } else if (operation == DOM_NODE_RENAMED) {
        action = CSS_NODE_MODIFIED;
    } else if (operation == DOM_NODE_IMPORTED ||
            operation == DOM_NODE_ADOPTED ||
            operation == DOM_NODE_DELETED) {
        action = CSS_NODE_DELETED;
    } else {
        return;
    }

    (void) css_libcss_node_data_handler(&pcore_select_handler, action,
            NULL, src, dst, data);
}

static css_error set_libcss_node_data(void *pw, void *node,
        void *libcss_node_data)
{
    dom_exception err;
    void *old_data;

    (void) pw;
    if (node == NULL || pcore_ensure_libcss_node_data_key() != 0) {
        return CSS_NOMEM;
    }

    /* A non-NULL replacement should not normally occur in one selection
     * transaction. Handle it defensively so an old cache cannot leak. */
    if (libcss_node_data != NULL &&
            dom_node_get_user_data((dom_node *) node,
                    pcore_libcss_node_data_key, &old_data) == DOM_NO_ERR &&
            old_data != NULL && old_data != libcss_node_data) {
        void *detached = NULL;

        err = dom_node_set_user_data((dom_node *) node,
                pcore_libcss_node_data_key, NULL,
                pcore_libcss_node_data_ud_handler, &detached);
        if (err != DOM_NO_ERR) {
            return CSS_NOMEM;
        }
        if (detached != NULL) {
            (void) css_libcss_node_data_handler(&pcore_select_handler,
                    CSS_NODE_DELETED, NULL, node, NULL, detached);
        }
    }

    old_data = NULL;
    err = dom_node_set_user_data((dom_node *) node,
            pcore_libcss_node_data_key, libcss_node_data,
            pcore_libcss_node_data_ud_handler, &old_data);
    if (err != DOM_NO_ERR) {
        return CSS_NOMEM;
    }
    return CSS_OK;
}

static css_error get_libcss_node_data(void *pw, void *node,
        void **libcss_node_data)
{
    (void) pw;
    if (node == NULL || libcss_node_data == NULL ||
            pcore_ensure_libcss_node_data_key() != 0) {
        return CSS_NOMEM;
    }
    if (dom_node_get_user_data((dom_node *) node,
            pcore_libcss_node_data_key, libcss_node_data) != DOM_NO_ERR) {
        return CSS_NOMEM;
    }
    return CSS_OK;
}

/* A style transaction builds a fresh select context and may use different
 * sheets or media dimensions. Drop the previous transaction's libcss cache
 * before walking the same DOM again, then let this pass retain its own data. */
static int pcore_clear_libcss_node_data_subtree(dom_node *node)
{
    void *data = NULL;
    void *detached = NULL;
    dom_node *child;

    if (node == NULL || pcore_ensure_libcss_node_data_key() != 0) {
        return 1;
    }
    if (dom_node_get_user_data(node, pcore_libcss_node_data_key,
            &data) != DOM_NO_ERR) {
        return 1;
    }
    if (data != NULL) {
        if (dom_node_set_user_data(node, pcore_libcss_node_data_key, NULL,
                pcore_libcss_node_data_ud_handler, &detached) != DOM_NO_ERR) {
            return 1;
        }
        if (detached != NULL) {
            (void) css_libcss_node_data_handler(&pcore_select_handler,
                    CSS_NODE_DELETED, NULL, node, NULL, detached);
        }
    }

    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return 1;
    }
    while (child != NULL) {
        dom_node *next;

        if (pcore_clear_libcss_node_data_subtree(child) != 0) {
            dom_node_unref(child);
            return 1;
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        dom_node_unref(child);
        child = next;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* DOM search + public entry point                                     */
/* ------------------------------------------------------------------ */

/* Depth-first search for the first element whose name caselessly equals
 * `want`. Returns an owned ref (caller unrefs) or NULL. `node` is borrowed. */
static dom_node *find_named(dom_node *node, lwc_string *want)
{
    dom_node *child;
    dom_node_type type;
    dom_string *name;
    dom_exception err;
    bool match;

    err = dom_node_get_node_type(node, &type);
    if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
        err = dom_node_get_node_name(node, &name);
        if (err == DOM_NO_ERR && name != NULL) {
            match = dom_string_caseless_lwc_isequal(name, want);
            dom_string_unref(name);
            if (match) {
                return dom_node_ref(node);
            }
        }
    }

    err = dom_node_get_first_child(node, &child);
    if (err != DOM_NO_ERR) {
        return NULL;
    }
    while (child != NULL) {
        dom_node *found;
        dom_node *next;

        found = find_named(child, want);
        if (found != NULL) {
            dom_node_unref(child);
            return found;
        }
        err = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        if (err != DOM_NO_ERR) {
            return NULL;
        }
        child = next;
    }

    return NULL;
}

PCORE_API int PCore_ComputeColor(HANDLE hDoc, HANDLE hSheet,
        const char *tag, unsigned long *out_argb)
{
    dom_document      *doc = (dom_document *) hDoc;
    css_stylesheet    *sheet = (css_stylesheet *) hSheet;
    dom_node          *root = NULL;
    dom_node          *elem = NULL;
    lwc_string        *want = NULL;
    css_select_ctx    *ctx = NULL;
    css_select_results *results = NULL;
    css_media          media;
    pcore_select_pw    pw;
    dom_exception      dexc;
    css_error          cerr;
    css_color          color;
    int                rc = 1;

    if (doc == NULL || sheet == NULL || tag == NULL || out_argb == NULL) {
        return 1;
    }

    memset(&pw, 0, sizeof(pw));
    pcore_interaction_snapshot(doc, &pw.focus_node, &pw.active_node,
            &pw.hover_node);
    if (lwc_intern_string("*", 1, &pw.universal) != lwc_error_ok) {
        return 1;
    }
    if (lwc_intern_string(tag, strlen(tag), &want) != lwc_error_ok) {
        goto cleanup;
    }

    /* locate the target element in the DOM */
    dexc = dom_document_get_document_element(doc, &root);
    if (dexc != DOM_NO_ERR || root == NULL) {
        goto cleanup;
    }
    elem = find_named(root, want);
    if (elem == NULL) {
        goto cleanup;
    }

    /* build a selection context holding the author sheet */
    if (css_select_ctx_create(&ctx) != CSS_OK) {
        goto cleanup;
    }
    if (css_select_ctx_append_sheet(ctx, sheet, CSS_ORIGIN_AUTHOR, NULL)
            != CSS_OK) {
        goto cleanup;
    }

    pcore_init_screen_media(&media);

    cerr = css_select_style(ctx, elem, &pcore_unit_ctx, &media, NULL,
            &pcore_select_handler, &pw, &results);
    if (cerr != CSS_OK || results == NULL) {
        goto cleanup;
    }
    /* The base (non-pseudo) computed style is always produced on success;
     * guard anyway so a surprise NULL surfaces as a clean failure rather
     * than crashing css_computed_color. */
    if (results->styles[CSS_PSEUDO_ELEMENT_NONE] == NULL) {
        goto cleanup;
    }

    (void) css_computed_color(results->styles[CSS_PSEUDO_ELEMENT_NONE],
            &color);
    *out_argb = (unsigned long) color;
    rc = 0;

cleanup:
    if (results != NULL) {
        css_select_results_destroy(results);
    }
    if (ctx != NULL) {
        css_select_ctx_destroy(ctx);
    }
    if (elem != NULL) {
        dom_node_unref(elem);
    }
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (want != NULL) {
        lwc_string_unref(want);
    }
    if (pw.universal != NULL) {
        lwc_string_unref(pw.universal);
    }
    return rc;
}

/* ================================================================== */
/* Whole-document styling (milestone A): UA sheet + top-down compose   */
/* ================================================================== */

/* Minimal UA default stylesheet - enough display/box defaults that layout
 * has something sane to work with. Expanded as later milestones need it. */
static const char PCORE_UA_CSS[] =
    "html, body, div, p, h1, h2, h3, h4, h5, h6, ul, ol, dl, dd, dt,"
    " blockquote, pre, form, fieldset, address, hr,"
    " header, footer, section, article, nav, aside, main, figure"
    " { display: block; }\n"
    "table { display: table; }\n"
    "thead { display: table-header-group; }\n"
    "tbody { display: table-row-group; }\n"
    "tfoot { display: table-footer-group; }\n"
    "tr { display: table-row; }\n"
    "td, th { display: table-cell; }\n"
    "th { font-weight: bold; }\n"
    "head, title, meta, link, style, script, base { display: none; }\n"
    "[hidden] { display: none; }\n"
    "details, dialog { display: block; }\n"
    "summary { display: list-item; }\n"
    "details:not([open]) > :not(summary) { display: none; }\n"
    "dialog:not([open]) { display: none; }\n"
    "li { display: list-item; }\n"
    "ul { padding-left: 40px; margin-top: 1.12em; margin-bottom: 1.12em;"
    " list-style-type: disc; }\n"
    "ol { padding-left: 40px; margin-top: 1.12em; margin-bottom: 1.12em;"
    " list-style-type: decimal; }\n"
    "ul ul { list-style-type: circle; }\n"
    "ul ul ul { list-style-type: square; }\n"
    "ol ul, ul ol, ul ul, ol ol { margin-top: 0; margin-bottom: 0; }\n"
    "b, strong { font-weight: bold; }\n"
    "i, em { font-style: italic; }\n"
    "a { color: #0000ee; text-decoration: underline; }\n"
    "a { color: #0000ee; text-decoration: underline; }\n"
    "input[type=hidden] { display: none; }\n"
    "input[type=button], input[type=reset], input[type=submit], button {"
    " background-color:#d9d9d9; color:#000; text-align:center;"
    " border:2px outset #d9d9d9; padding:1px 0.5em; margin:1px;"
    " line-height:1.33; }\n"
    "input[disabled], button[disabled] {"
    " background-color:#ddd; color:#333; }\n"
    "input[type=checkbox], input[type=radio] {"
    " background-color: transparent; border: none; padding: 0 0.1em;"
    " margin-right: 0.2em; }\n"
    "pre { font-family: monospace; white-space: pre; margin-bottom: 1em; }\n"
    "pre[wrap] { white-space: pre-wrap; }\n"
    "body { margin: 8px; }\n"
    "p, blockquote { margin-top: 1em; margin-bottom: 1em; }\n"
    "h1 { font-size: 2em; }\n"
    "h2 { font-size: 1.5em; }\n"
    "h3 { font-size: 1.17em; }\n";

/* libdom user-data key under which each element's css_computed_style hangs. */
static dom_string *pcore_style_key = NULL;
static dom_string *pcore_default_style_key = NULL;

static int pcore_ensure_style_key(void)
{
    if (pcore_style_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) "__pcore_style__", 15,
            &pcore_style_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static int pcore_ensure_default_style_key(void)
{
    if (pcore_default_style_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) "__pcore_default_style__", 23,
            &pcore_default_style_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

/* Internal (pcore_internal.h): read back the computed style attached to a node
 * by PCore_StyleDocument. Used by the box-tree builder in pcore_box.c. */
css_computed_style *pcore_node_computed_style(struct dom_node *node)
{
    void *sd = NULL;

    if (pcore_style_key == NULL || node == NULL) {
        return NULL;
    }
    if (dom_node_get_user_data(node, pcore_style_key, &sd) != DOM_NO_ERR) {
        return NULL;
    }
    return (css_computed_style *) sd;
}

css_computed_style *pcore_document_default_style(struct dom_document *doc)
{
    void *sd = NULL;

    if (pcore_default_style_key == NULL || doc == NULL) {
        return NULL;
    }
    if (dom_node_get_user_data((dom_node *) doc, pcore_default_style_key,
            &sd) != DOM_NO_ERR) {
        return NULL;
    }
    return (css_computed_style *) sd;
}

/* Free the attached computed style when libdom deletes the node (e.g. when the
 * document is destroyed), so the per-node styles do not leak. */
static void pcore_style_ud_handler(dom_node_operation op, dom_string *key,
        void *data, struct dom_node *src, struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        css_computed_style_destroy((css_computed_style *) data);
    }
}

/* Style `node` (an element) and its element descendants top-down, composing
 * each selected style with `parent_style` to resolve inheritance. The computed
 * style is attached to the node via user-data. `node` is borrowed. */
static int pcore_style_subtree(css_select_ctx *ctx, pcore_select_pw *pw,
        const css_media *media, dom_node *node,
        const css_computed_style *parent_style, dom_string *style_name,
        const char *document_url, PCoreResolveUrlFn resolve,
        void *resolve_pw)
{
    css_select_results *results = NULL;
    css_computed_style *node_style = NULL;
    css_stylesheet *inline_style = NULL;
    css_computed_style *base;
    css_error done;
    dom_string *style_value = NULL;
    dom_node *child;
    void *old = NULL;
    dom_exception dom_err;

    if (style_name != NULL) {
        dom_err = dom_element_get_attribute(node, style_name, &style_value);
        if (dom_err != DOM_NO_ERR) {
            return 1;
        }
    }
    if (style_value != NULL) {
        inline_style = pcore_parse_inline_css_internal(
                (const char *) dom_string_data(style_value),
                (unsigned int) dom_string_byte_length(style_value),
                document_url, resolve, resolve_pw, &done);
        dom_string_unref(style_value);
        if (inline_style != NULL && done != CSS_OK) {
            css_stylesheet_destroy(inline_style);
            inline_style = NULL;
        }
        if (inline_style == NULL) {
            return 1;
        }
    }

    if (css_select_style(ctx, node, &pcore_unit_ctx, media, inline_style,
            &pcore_select_handler, pw, &results) != CSS_OK ||
            results == NULL) {
        if (inline_style != NULL) {
            css_stylesheet_destroy(inline_style);
        }
        return 1;
    }
    if (inline_style != NULL) {
        css_stylesheet_destroy(inline_style);
    }

    base = results->styles[CSS_PSEUDO_ELEMENT_NONE];

    if (parent_style != NULL) {
        /* Compose with the parent to fill inherited properties. */
        if (css_computed_style_compose(parent_style, base, &pcore_unit_ctx,
                &node_style) != CSS_OK) {
            css_select_results_destroy(results);
            return 1;
        }
        /* `base` is freed by results_destroy below. */
    } else {
        /* Root element: no parent. Keep the base style and detach it so
         * results_destroy does not free it. */
        node_style = base;
        results->styles[CSS_PSEUDO_ELEMENT_NONE] = NULL;
    }

    css_select_results_destroy(results);

    /* Attach to the node (handler frees it on node deletion). Re-styling
     * replaces user-data without invoking that handler, so free the previous
     * computed style here instead of leaking it on every viewport change. */
    if (dom_node_set_user_data(node, pcore_style_key, node_style,
            pcore_style_ud_handler, &old) != DOM_NO_ERR) {
        css_computed_style_destroy(node_style);
        return 1;
    }
    if (old != NULL && old != node_style) {
        css_computed_style_destroy((css_computed_style *) old);
    }

    /* Recurse into element children, passing our computed style as parent. */
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return 1;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                type == DOM_ELEMENT_NODE) {
            if (pcore_style_subtree(ctx, pw, media, child, node_style,
                    style_name, document_url, resolve, resolve_pw) != 0) {
                dom_node_unref(child);
                return 1;
            }
        }

        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        dom_node_unref(child);
        child = next;
    }
    return 0;
}

/* External CSS is fetched once while navigating, then retained as raw bytes
 * on its document. That lets a WM_SIZE restyle re-evaluate @media without
 * running the transport again. The parsed stylesheet remains per-style-pass:
 * css_select_ctx owns it only until all computed styles have been produced. */
#define PCORE_STYLESHEET_CACHE_MAX 32
#define PCORE_STYLESHEET_CACHE_ENTRY_MAX (256 * 1024)
#define PCORE_STYLESHEET_CACHE_BYTES_MAX (512 * 1024)

typedef struct pcore_stylesheet_resource {
    struct pcore_stylesheet_resource *next;
    char *url;
    char *data;
    int len;
} pcore_stylesheet_resource;

typedef struct pcore_stylesheet_cache {
    pcore_stylesheet_resource *head;
    int count;
    int bytes;
} pcore_stylesheet_cache;

#define PCORE_STYLESHEET_REFERENCE_MAX 1024
#define PCORE_STYLESHEET_URL_MAX 2048

typedef struct pcore_collect_scratch {
    char reference[PCORE_STYLESHEET_REFERENCE_MAX];
    char url[PCORE_STYLESHEET_URL_MAX];
} pcore_collect_scratch;

static dom_string *pcore_stylesheet_cache_key = NULL;

static int pcore_ensure_stylesheet_cache_key(void)
{
    static const char *name = "__pcore_stylesheet_cache__";

    if (pcore_stylesheet_cache_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &pcore_stylesheet_cache_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_stylesheet_cache_free(pcore_stylesheet_cache *cache)
{
    pcore_stylesheet_resource *entry;

    if (cache == NULL) {
        return;
    }
    entry = cache->head;
    while (entry != NULL) {
        pcore_stylesheet_resource *next = entry->next;

        free(entry->url);
        free(entry->data);
        free(entry);
        entry = next;
    }
    free(cache);
}

static void pcore_stylesheet_cache_ud_handler(dom_node_operation op,
        dom_string *key, void *data, struct dom_node *src,
        struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_stylesheet_cache_free((pcore_stylesheet_cache *) data);
    }
}

static pcore_stylesheet_cache *pcore_stylesheet_cache_get(
        dom_document *doc, int create)
{
    void *data = NULL;
    void *old = NULL;
    pcore_stylesheet_cache *cache;

    if (doc == NULL || pcore_ensure_stylesheet_cache_key() != 0) {
        return NULL;
    }
    if (dom_node_get_user_data((struct dom_node *) doc,
            pcore_stylesheet_cache_key, &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL) {
        return (pcore_stylesheet_cache *) data;
    }
    if (!create) {
        return NULL;
    }
    cache = (pcore_stylesheet_cache *) malloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }
    cache->head = NULL;
    cache->count = 0;
    cache->bytes = 0;
    if (dom_node_set_user_data((struct dom_node *) doc,
            pcore_stylesheet_cache_key, cache,
            pcore_stylesheet_cache_ud_handler, &old) != DOM_NO_ERR) {
        free(cache);
        return NULL;
    }
    if (old != NULL && old != cache) {
        pcore_stylesheet_cache_free((pcore_stylesheet_cache *) old);
    }
    return cache;
}

static pcore_stylesheet_resource *pcore_stylesheet_cache_find(
        pcore_stylesheet_cache *cache, const char *url)
{
    pcore_stylesheet_resource *entry;

    if (cache == NULL || url == NULL) {
        return NULL;
    }
    for (entry = cache->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->url, url) == 0) {
            return entry;
        }
    }
    return NULL;
}

static int pcore_stylesheet_cache_store(pcore_stylesheet_cache *cache,
        const char *url, const char *data, int len)
{
    pcore_stylesheet_resource *entry;
    size_t url_len;

    if (cache == NULL || url == NULL || data == NULL || len <= 0 ||
            cache->count >= PCORE_STYLESHEET_CACHE_MAX ||
            len > PCORE_STYLESHEET_CACHE_ENTRY_MAX ||
            cache->bytes > PCORE_STYLESHEET_CACHE_BYTES_MAX - len) {
        return 1;
    }
    entry = (pcore_stylesheet_resource *) malloc(sizeof(*entry));
    if (entry == NULL) {
        return 1;
    }
    entry->next = NULL;
    entry->url = NULL;
    entry->data = NULL;
    entry->len = 0;
    url_len = strlen(url);
    entry->url = (char *) malloc(url_len + 1);
    entry->data = (char *) malloc((size_t) len);
    if (entry->url == NULL || entry->data == NULL) {
        free(entry->url);
        free(entry->data);
        free(entry);
        return 1;
    }
    memcpy(entry->url, url, url_len + 1);
    memcpy(entry->data, data, (size_t) len);
    entry->len = len;
    entry->next = cache->head;
    cache->head = entry;
    cache->count++;
    cache->bytes += len;
    return 0;
}

/* Shared state for the resource-collection DFS below. */
typedef struct pcore_collect_ctx {
    css_select_ctx *ctx;
    HANDLE         *sheets;     /* parsed sheet handles, freed by caller */
    int            *n;
    int             max;
    PCoreFetchFn    fetch;      /* embedder fetch for external <link> CSS */
    PCoreFreeFn     freefn;
    PCoreResolveUrlFn resolve;
    void           *pw;
    const char     *document_url;
    const char     *url_stack[16];
    pcore_stylesheet_cache *cache; /* per-document external CSS bytes */
    pcore_collect_scratch *scratch; /* one buffer set, not one per DFS frame */
    dom_string     *style_name; /* interned "style" */
    dom_string     *link_name;  /* interned "link"  */
    dom_string     *rel_name;   /* interned "rel"   */
    dom_string     *href_name;  /* interned "href"  */
    dom_string     *media_name; /* interned "media" */
    dom_string     *disabled_name; /* interned "disabled" */
} pcore_collect_ctx;

#define PCORE_IMPORT_DEPTH_MAX 16

static int pcore_resolve_css_url(pcore_collect_ctx *cc, const char *base,
        const char *reference, char *out, int capacity)
{
    size_t len;

    if (cc->resolve != NULL && base != NULL &&
            cc->resolve(cc->pw, base, reference, out, capacity) == 0 &&
            out[0] != '\0') {
        return 0;
    }
    len = strlen(reference);
    if (len >= (size_t) capacity) {
        return 1;
    }
    memcpy(out, reference, len + 1);
    return 0;
}

/* Return cached bytes when possible. A successful uncached body is copied to
 * the document cache; when the cache budget is exhausted, `owned` keeps the
 * embedder buffer alive until the caller finishes parsing it. */
static int pcore_get_stylesheet_bytes(pcore_collect_ctx *cc, const char *url,
        const char **out_data, int *out_len, char **owned)
{
    pcore_stylesheet_resource *cached;
    char *data;
    int len;

    *out_data = NULL;
    *out_len = 0;
    *owned = NULL;
    cached = pcore_stylesheet_cache_find(cc->cache, url);
    if (cached != NULL) {
        *out_data = cached->data;
        *out_len = cached->len;
        return 0;
    }
    if (cc->fetch == NULL) {
        return 1;
    }
    data = NULL;
    len = 0;
    if (cc->fetch(cc->pw, url, &data, &len) != 0 || data == NULL ||
            len <= 0) {
        if (data != NULL && cc->freefn != NULL) {
            cc->freefn(cc->pw, data);
        }
        return 1;
    }
    if (pcore_stylesheet_cache_store(cc->cache, url, data, len) == 0) {
        cached = pcore_stylesheet_cache_find(cc->cache, url);
        if (cc->freefn != NULL) {
            cc->freefn(cc->pw, data);
        }
        if (cached == NULL) {
            return 1;
        }
        *out_data = cached->data;
        *out_len = cached->len;
    } else {
        *out_data = data;
        *out_len = len;
        *owned = data;
    }
    return 0;
}

static css_stylesheet *pcore_record_css_sheet(pcore_collect_ctx *cc,
        const char *data, int len, const char *url, css_error *done)
{
    css_stylesheet *sheet;

    if (*cc->n >= cc->max || data == NULL || len <= 0) {
        return NULL;
    }
    sheet = pcore_parse_css_internal(data, (unsigned int) len, url,
            cc->resolve, cc->pw, done);
    if (sheet != NULL) {
        cc->sheets[(*cc->n)++] = (HANDLE) sheet;
    }
    return sheet;
}

static css_stylesheet *pcore_empty_import(pcore_collect_ctx *cc,
        const char *url)
{
    static const char empty_css[] = " ";
    css_error done = CSS_INVALID;
    css_stylesheet *sheet;

    sheet = pcore_record_css_sheet(cc, empty_css, 1, url, &done);
    return (done == CSS_OK) ? sheet : NULL;
}

static css_stylesheet *pcore_parse_css_tree(pcore_collect_ctx *cc,
        const char *data, int len, const char *url, int depth)
{
    css_stylesheet *sheet;
    css_stylesheet *child;
    css_error done;
    css_error err;
    lwc_string *pending;
    const char *import_data;
    char *owned;
    int import_len;
    int cycle;
    int i;

    sheet = pcore_record_css_sheet(cc, data, len, url, &done);
    if (sheet == NULL || done == CSS_OK) {
        return sheet;
    }
    if (done != CSS_IMPORTS_PENDING) {
        return NULL;
    }
    cc->url_stack[depth] = url;
    while (css_stylesheet_next_pending_import(sheet, &pending) == CSS_OK) {
        const char *import_url;

        import_url = lwc_string_data(pending);
        child = NULL;
        cycle = depth + 1 >= PCORE_IMPORT_DEPTH_MAX;
        for (i = 0; !cycle && i <= depth; i++) {
            if (cc->url_stack[i] != NULL &&
                    strcmp(cc->url_stack[i], import_url) == 0) {
                cycle = 1;
            }
        }
        if (!cycle && pcore_get_stylesheet_bytes(cc, import_url,
                &import_data, &import_len, &owned) == 0) {
            child = pcore_parse_css_tree(cc, import_data, import_len,
                    import_url, depth + 1);
            if (owned != NULL && cc->freefn != NULL) {
                cc->freefn(cc->pw, owned);
            }
        }
        if (child == NULL) {
            child = pcore_empty_import(cc, import_url);
        }
        if (child == NULL) {
            lwc_string_unref(pending);
            cc->url_stack[depth] = NULL;
            return NULL;
        }
        err = css_stylesheet_register_import(sheet, child);
        lwc_string_unref(pending);
        if (err != CSS_OK) {
            cc->url_stack[depth] = NULL;
            return NULL;
        }
    }
    cc->url_stack[depth] = NULL;
    return sheet;
}

/* The HTML rel attribute is an ASCII-whitespace separated token list, not a
 * single case-sensitive string. Keep stylesheet selection conservative:
 * recognize the implemented stylesheet token, but leave alternate sheets
 * disabled until an explicit alternate-sheet selection policy exists. */
static int pcore_rel_has_token(dom_string *rel, const char *token)
{
    const char *data;
    size_t length;
    size_t start;
    size_t end;
    size_t token_length;
    size_t i;

    if (rel == NULL || token == NULL) {
        return 0;
    }
    data = dom_string_data(rel);
    length = dom_string_byte_length(rel);
    token_length = strlen(token);
    start = 0;
    while (start < length) {
        while (start < length && (data[start] == ' ' || data[start] == '\t' ||
                data[start] == '\r' || data[start] == '\n' ||
                data[start] == '\f')) {
            start++;
        }
        end = start;
        while (end < length && data[end] != ' ' && data[end] != '\t' &&
                data[end] != '\r' && data[end] != '\n' && data[end] != '\f') {
            end++;
        }
        if (end - start == token_length) {
            for (i = 0; i < token_length; i++) {
                char actual = data[start + i];
                char expected = token[i];

                if (actual >= 'A' && actual <= 'Z') {
                    actual = (char) (actual + ('a' - 'A'));
                }
                if (expected >= 'A' && expected <= 'Z') {
                    expected = (char) (expected + ('a' - 'A'));
                }
                if (actual != expected) {
                    break;
                }
            }
            if (i == token_length) {
                return 1;
            }
        }
        start = end;
    }
    return 0;
}

/* Parse CSS and its native libcss @import tree, append only the root sheet to
 * the select context, and retain every child handle for pass-end cleanup. */
static void pcore_add_author_css(pcore_collect_ctx *cc, const char *data,
        int len, const char *url, const char *media)
{
    css_stylesheet *sheet;

    sheet = pcore_parse_css_tree(cc, data, len, url, 0);
    if (sheet == NULL) {
        return;
    }
    css_select_ctx_append_sheet(cc->ctx, sheet, CSS_ORIGIN_AUTHOR, media);
}

/* DFS: collect author CSS from the page in document order - inline <style>
 * blocks and external <link rel="stylesheet" href> sheets (fetched via the
 * embedder callback, if provided). The element's optional media attribute is
 * passed to libcss when each sheet is attached. Parsed sheets are appended to
 * cc->ctx and recorded in cc->sheets[] for the caller to free. Letting a
 * fetched page carry both its inline and linked CSS is what makes real pages
 * look styled rather than bare. */
static void pcore_collect_resources(pcore_collect_ctx *cc, dom_node *node)
{
    dom_node *child;
    dom_node_type type;
    dom_string *name;
    dom_exception err;

    err = dom_node_get_node_type(node, &type);
    if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
        bool is_style = false;
        bool is_link = false;

        err = dom_node_get_node_name(node, &name);
        if (err == DOM_NO_ERR && name != NULL) {
            is_style = dom_string_caseless_isequal(name, cc->style_name);
            if (!is_style && cc->link_name != NULL) {
                is_link = dom_string_caseless_isequal(name, cc->link_name);
            }
            dom_string_unref(name);
        }

        if (is_style) {
            dom_string *css = NULL;
            dom_string *media = NULL;
            if (dom_node_get_text_content(node, &css) == DOM_NO_ERR &&
                    css != NULL) {
                const char *media_value = NULL;

                if (cc->media_name != NULL &&
                        dom_element_get_attribute(node, cc->media_name,
                        &media) == DOM_NO_ERR && media != NULL) {
                    media_value = dom_string_data(media);
                }
                pcore_add_author_css(cc, dom_string_data(css),
                        (int) dom_string_byte_length(css),
                        (cc->document_url != NULL) ? cc->document_url :
                        "positron:inline-style", media_value);
                if (media != NULL) {
                    dom_string_unref(media);
                }
                dom_string_unref(css);
            }
            return;   /* don't recurse into a <style>'s text children */
        }

        if (is_link && (cc->cache != NULL || cc->fetch != NULL)) {
            dom_string *rel = NULL;
            dom_string *href = NULL;
            dom_string *media = NULL;
            bool is_sheet = false;
            bool is_disabled = false;

            if (cc->disabled_name != NULL &&
                    dom_element_has_attribute(node, cc->disabled_name,
                    &is_disabled) != DOM_NO_ERR) {
                is_disabled = false;
            }
            if (is_disabled) {
                return;   /* disabled stylesheet links are not fetched */
            }

            if (dom_element_get_attribute(node, cc->rel_name, &rel) ==
                    DOM_NO_ERR && rel != NULL) {
                is_sheet = pcore_rel_has_token(rel, "stylesheet") &&
                        !pcore_rel_has_token(rel, "alternate");
                dom_string_unref(rel);
            }
            if (is_sheet &&
                    dom_element_get_attribute(node, cc->href_name, &href) ==
                            DOM_NO_ERR && href != NULL) {
                const char *hu8 = dom_string_data(href);
                size_t hl = dom_string_byte_length(href);
                if (hu8 != NULL && hl > 0 && cc->scratch != NULL) {
                    const char *data;
                    char *owned;
                    int len;
                    int cl;

                    cl = (hl < sizeof(cc->scratch->reference) - 1) ?
                            (int) hl :
                            (int) sizeof(cc->scratch->reference) - 1;
                    memcpy(cc->scratch->reference, hu8, cl);
                    cc->scratch->reference[cl] = '\0';
                    if (pcore_resolve_css_url(cc, cc->document_url,
                            cc->scratch->reference, cc->scratch->url,
                            (int) sizeof(cc->scratch->url)) == 0 &&
                            pcore_get_stylesheet_bytes(cc, cc->scratch->url,
                            &data, &len,
                            &owned) == 0) {
                        const char *media_value = NULL;

                        if (cc->media_name != NULL &&
                                dom_element_get_attribute(node,
                                cc->media_name, &media) == DOM_NO_ERR &&
                                media != NULL) {
                            media_value = dom_string_data(media);
                        }
                        pcore_add_author_css(cc, data, len, cc->scratch->url,
                                media_value);
                        if (media != NULL) {
                            dom_string_unref(media);
                        }
                        if (owned != NULL && cc->freefn != NULL) {
                            cc->freefn(cc->pw, owned);
                        }
                    }
                }
                dom_string_unref(href);
            }
            return;   /* <link> has no element children */
        }
    }

    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        dom_node *next;

        pcore_collect_resources(cc, child);
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

PCORE_API int PCore_StyleDocument(HANDLE hDoc, HANDLE hSheet)
{
    return PCore_StyleDocumentEx(hDoc, hSheet, NULL, NULL, NULL);
}

PCORE_API int PCore_StyleDocumentEx(HANDLE hDoc, HANDLE hSheet,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw_fetch)
{
    return PCore_StyleDocumentEx2(hDoc, hSheet, NULL, NULL, fetch,
            freefn, pw_fetch);
}

PCORE_API int PCore_StyleDocumentEx2(HANDLE hDoc, HANDLE hSheet,
        const char *document_url, PCoreResolveUrlFn resolve,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw_fetch)
{
    dom_document     *doc = (dom_document *) hDoc;
    css_stylesheet   *author = (css_stylesheet *) hSheet;
    HANDLE            hUA = NULL;
    css_select_ctx   *ctx = NULL;
    dom_node         *root = NULL;
    HANDLE            page_sheets[64];
    int               n_page = 0;
    int               i;
    css_media         media;
    pcore_select_pw   pw;
    pcore_collect_ctx cc;
    css_computed_style *default_style = NULL;
    dom_string        *inline_style_name = NULL;
    void             *old_default = NULL;
    int               rc = 1;

    memset(&cc, 0, sizeof(cc));

    if (doc == NULL) {
        return 1;
    }

    memset(&pw, 0, sizeof(pw));
    pcore_interaction_snapshot(doc, &pw.focus_node, &pw.active_node,
            &pw.hover_node);
    if (lwc_intern_string("*", 1, &pw.universal) != lwc_error_ok) {
        return 1;
    }
    if (pcore_ensure_style_key() != 0 ||
            pcore_ensure_libcss_node_data_key() != 0 ||
            pcore_ensure_default_style_key() != 0) {
        goto cleanup;
    }

    /* UA default sheet, parsed via the existing CSS entry point. */
    hUA = PCore_ParseCSS(PCORE_UA_CSS, 0, "positron:ua-default.css");
    if (hUA == NULL) {
        goto cleanup;
    }

    if (css_select_ctx_create(&ctx) != CSS_OK) {
        goto cleanup;
    }
    if (css_select_ctx_append_sheet(ctx, (css_stylesheet *) hUA,
            CSS_ORIGIN_UA, NULL) != CSS_OK) {
        goto cleanup;
    }

    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }
    if (pcore_clear_libcss_node_data_subtree(root) != 0) {
        goto cleanup;
    }

    /* Apply the page's own inline <style> and external <link> sheets. */
    cc.ctx = ctx;
    cc.sheets = page_sheets;
    cc.n = &n_page;
    cc.max = 64;
    cc.fetch = fetch;
    cc.freefn = freefn;
    cc.resolve = resolve;
    cc.pw = pw_fetch;
    cc.document_url = document_url;
    cc.cache = pcore_stylesheet_cache_get(doc, 1);
    dom_string_create((const uint8_t *) "style", 5, &cc.style_name);
    dom_string_create((const uint8_t *) "link", 4, &cc.link_name);
    dom_string_create((const uint8_t *) "rel", 3, &cc.rel_name);
    dom_string_create((const uint8_t *) "href", 4, &cc.href_name);
    dom_string_create((const uint8_t *) "media", 5, &cc.media_name);
    dom_string_create((const uint8_t *) "disabled", 8, &cc.disabled_name);
    dom_string_create_interned((const uint8_t *) "style", 5,
            &inline_style_name);
    cc.scratch = (pcore_collect_scratch *) malloc(sizeof(*cc.scratch));
    if (cc.style_name == NULL || cc.link_name == NULL ||
            cc.rel_name == NULL || cc.href_name == NULL ||
            cc.media_name == NULL || cc.disabled_name == NULL ||
            inline_style_name == NULL || cc.scratch == NULL) {
        goto cleanup;
    }
    pcore_collect_resources(&cc, root);

    /* Optional extra author sheet supplied by the caller (may be NULL). */
    if (author != NULL) {
        css_select_ctx_append_sheet(ctx, author, CSS_ORIGIN_AUTHOR, NULL);
    }

    pcore_init_screen_media(&media);

    if (pcore_style_subtree(ctx, &pw, &media, root, NULL,
            inline_style_name, document_url, resolve, pw_fetch) != 0) {
        goto cleanup;
    }
    if (css_select_default_style(ctx, &pcore_select_handler, &pw,
            &default_style) != CSS_OK || default_style == NULL) {
        goto cleanup;
    }
    if (dom_node_set_user_data((dom_node *) doc, pcore_default_style_key,
            default_style, pcore_style_ud_handler, &old_default) !=
            DOM_NO_ERR) {
        goto cleanup;
    }
    if (old_default != NULL && old_default != default_style) {
        css_computed_style_destroy((css_computed_style *) old_default);
    }
    default_style = NULL;
    rc = 0;

cleanup:
    if (default_style != NULL) {
        css_computed_style_destroy(default_style);
    }
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (ctx != NULL) {
        css_select_ctx_destroy(ctx);   /* destroy before freeing its sheets */
    }
    for (i = 0; i < n_page; i++) {
        PCore_FreeStylesheet(page_sheets[i]);
    }
    if (hUA != NULL) {
        PCore_FreeStylesheet(hUA);
    }
    if (cc.style_name != NULL) { dom_string_unref(cc.style_name); }
    if (cc.link_name != NULL)  { dom_string_unref(cc.link_name); }
    if (cc.rel_name != NULL)   { dom_string_unref(cc.rel_name); }
    if (cc.href_name != NULL)  { dom_string_unref(cc.href_name); }
    if (cc.media_name != NULL) { dom_string_unref(cc.media_name); }
    if (cc.disabled_name != NULL) { dom_string_unref(cc.disabled_name); }
    free(cc.scratch);
    if (inline_style_name != NULL) { dom_string_unref(inline_style_name); }
    if (pw.universal != NULL) {
        lwc_string_unref(pw.universal);
    }
    return rc;
}

typedef struct pcore_shared_svg {
    struct pcore_shared_svg *next;
    char *url;
    int len;
    unsigned long hash_a;
    unsigned long hash_b;
    unsigned int users;
    void *svg;
    int width;
    int height;
} pcore_shared_svg;

typedef struct pcore_image_resource {
    struct pcore_image_resource *next;
    char *url;
    char *data;
    int len;
    int retained_attempted;
    void *native_image;
    void *svg;
    pcore_shared_svg *shared_svg;
    int width;
    int height;
    PCoreImageDecodeStats decode_stats;
} pcore_image_resource;

typedef struct pcore_image_cache {
    pcore_image_resource *head;
} pcore_image_cache;

static dom_string *pcore_image_cache_key = NULL;
static pcore_shared_svg *pcore_shared_svg_head = NULL;

/* Script bytes are retained only by the owning document. This keeps the
 * future JavaScript consumer independent from transport and avoids a global
 * cache on a device with a small process budget. */
#define PCORE_SCRIPT_CACHE_MAX 32
#define PCORE_SCRIPT_CACHE_ENTRY_MAX (512 * 1024)
#define PCORE_SCRIPT_CACHE_BYTES_MAX (2 * 1024 * 1024)

typedef struct pcore_script_resource {
    struct pcore_script_resource *next;
    char *url;
    char *data;
    int len;
} pcore_script_resource;

typedef struct pcore_script_cache {
    pcore_script_resource *head;
    pcore_script_resource *tail;
    int count;
    int bytes;
} pcore_script_cache;

static dom_string *pcore_script_cache_key = NULL;

static unsigned long pcore_image_hash_a(const char *data, int len)
{
    unsigned long hash = 2166136261UL;
    int i;

    for (i = 0; i < len; i++) {
        hash ^= (unsigned char) data[i];
        hash *= 16777619UL;
    }
    return hash;
}

static unsigned long pcore_image_hash_b(const char *data, int len)
{
    unsigned long hash = 5381UL;
    int i;

    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) ^ (unsigned char) data[i];
    }
    return hash;
}

static pcore_shared_svg *pcore_shared_svg_find(const char *url,
        const char *data, int len)
{
    pcore_shared_svg *entry;
    unsigned long hash_a;
    unsigned long hash_b;

    if (url == NULL || data == NULL || len <= 0) {
        return NULL;
    }
    hash_a = pcore_image_hash_a(data, len);
    hash_b = pcore_image_hash_b(data, len);
    for (entry = pcore_shared_svg_head; entry != NULL;
            entry = entry->next) {
        if (entry->len == len && entry->hash_a == hash_a &&
                entry->hash_b == hash_b && strcmp(entry->url, url) == 0) {
            entry->users++;
            return entry;
        }
    }
    return NULL;
}

static pcore_shared_svg *pcore_shared_svg_store(const char *url,
        const char *data, int len, void *svg, int width, int height)
{
    pcore_shared_svg *entry;
    size_t url_len;

    if (url == NULL || data == NULL || len <= 0 || svg == NULL ||
            width <= 0 || height <= 0) {
        return NULL;
    }
    entry = (pcore_shared_svg *) malloc(sizeof(*entry));
    if (entry == NULL) {
        return NULL;
    }
    url_len = strlen(url);
    entry->url = (char *) malloc(url_len + 1);
    if (entry->url == NULL) {
        free(entry);
        return NULL;
    }
    memcpy(entry->url, url, url_len + 1);
    entry->len = len;
    entry->hash_a = pcore_image_hash_a(data, len);
    entry->hash_b = pcore_image_hash_b(data, len);
    entry->users = 1;
    entry->svg = svg;
    entry->width = width;
    entry->height = height;
    entry->next = pcore_shared_svg_head;
    pcore_shared_svg_head = entry;
    return entry;
}

static void pcore_shared_svg_release(pcore_shared_svg *entry)
{
    pcore_shared_svg **link;

    if (entry == NULL || entry->users == 0) {
        return;
    }
    entry->users--;
    if (entry->users != 0) {
        return;
    }
    link = &pcore_shared_svg_head;
    while (*link != NULL && *link != entry) {
        link = &(*link)->next;
    }
    if (*link == entry) {
        *link = entry->next;
    }
    PImage_FreeSvg((PIMAGE_SVG) entry->svg);
    free(entry->url);
    free(entry);
}

void pcore_image_shared_shutdown(void)
{
    pcore_shared_svg *entry;

    entry = pcore_shared_svg_head;
    pcore_shared_svg_head = NULL;
    while (entry != NULL) {
        pcore_shared_svg *next = entry->next;

        PImage_FreeSvg((PIMAGE_SVG) entry->svg);
        free(entry->url);
        free(entry);
        entry = next;
    }
}

static int pcore_ensure_image_cache_key(void)
{
    static const char *name = "__pcore_image_cache__";

    if (pcore_image_cache_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &pcore_image_cache_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_image_cache_free(pcore_image_cache *cache)
{
    pcore_image_resource *entry;

    if (cache == NULL) {
        return;
    }
    entry = cache->head;
    while (entry != NULL) {
        pcore_image_resource *next = entry->next;

        if (entry->native_image != NULL) {
            PImage_FreeBitmap((PIMAGE_BITMAP) entry->native_image);
        }
        if (entry->shared_svg != NULL) {
            pcore_shared_svg_release(entry->shared_svg);
        } else if (entry->svg != NULL) {
            PImage_FreeSvg((PIMAGE_SVG) entry->svg);
        }
        free(entry->url);
        free(entry->data);
        free(entry);
        entry = next;
    }
    free(cache);
}

static void pcore_image_cache_ud_handler(dom_node_operation op,
        dom_string *key, void *data, struct dom_node *src,
        struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_image_cache_free((pcore_image_cache *) data);
    }
}

static pcore_image_cache *pcore_image_cache_get(dom_document *doc, int create)
{
    void *data = NULL;
    void *old = NULL;
    pcore_image_cache *cache;

    if (doc == NULL || pcore_ensure_image_cache_key() != 0) {
        return NULL;
    }
    if (dom_node_get_user_data((struct dom_node *) doc,
            pcore_image_cache_key, &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL) {
        return (pcore_image_cache *) data;
    }
    if (!create) {
        return NULL;
    }

    cache = (pcore_image_cache *) malloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }
    cache->head = NULL;
    if (dom_node_set_user_data((struct dom_node *) doc,
            pcore_image_cache_key, cache, pcore_image_cache_ud_handler,
            &old) != DOM_NO_ERR) {
        free(cache);
        return NULL;
    }
    if (old != NULL && old != cache) {
        pcore_image_cache_free((pcore_image_cache *) old);
    }
    return cache;
}

static pcore_image_resource *pcore_image_cache_find(
        pcore_image_cache *cache, const char *url)
{
    pcore_image_resource *entry;

    if (cache == NULL || url == NULL) {
        return NULL;
    }
    for (entry = cache->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->url, url) == 0) {
            return entry;
        }
    }
    return NULL;
}

static int pcore_image_cache_store(pcore_image_cache *cache,
        const char *url, const char *data, int len)
{
    pcore_image_resource *entry;
    size_t url_len;

    if (cache == NULL || url == NULL || data == NULL || len <= 0) {
        return 1;
    }
    entry = (pcore_image_resource *) malloc(sizeof(*entry));
    if (entry == NULL) {
        return 1;
    }
    entry->next = NULL;
    entry->url = NULL;
    entry->data = NULL;
    entry->len = 0;
    entry->retained_attempted = 0;
    entry->native_image = NULL;
    entry->svg = NULL;
    entry->shared_svg = NULL;
    entry->width = 0;
    entry->height = 0;
    memset(&entry->decode_stats, 0, sizeof(entry->decode_stats));

    url_len = strlen(url);
    entry->url = (char *) malloc(url_len + 1);
    entry->data = (char *) malloc((size_t) len);
    if (entry->url == NULL || entry->data == NULL) {
        free(entry->url);
        free(entry->data);
        free(entry);
        return 1;
    }
    memcpy(entry->url, url, url_len + 1);
    memcpy(entry->data, data, (size_t) len);
    entry->len = len;
    entry->next = cache->head;
    cache->head = entry;
    return 0;
}

static int pcore_ensure_script_cache_key(void)
{
    static const char *name = "__pcore_script_cache__";

    if (pcore_script_cache_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &pcore_script_cache_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_script_cache_free(pcore_script_cache *cache)
{
    pcore_script_resource *entry;

    if (cache == NULL) {
        return;
    }
    entry = cache->head;
    while (entry != NULL) {
        pcore_script_resource *next = entry->next;

        free(entry->url);
        free(entry->data);
        free(entry);
        entry = next;
    }
    free(cache);
}

static void pcore_script_cache_ud_handler(dom_node_operation op,
        dom_string *key, void *data, struct dom_node *src,
        struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_script_cache_free((pcore_script_cache *) data);
    }
}

static pcore_script_cache *pcore_script_cache_get(dom_document *doc,
        int create)
{
    void *data = NULL;
    void *old = NULL;
    pcore_script_cache *cache;

    if (doc == NULL || pcore_ensure_script_cache_key() != 0) {
        return NULL;
    }
    if (dom_node_get_user_data((struct dom_node *) doc,
            pcore_script_cache_key, &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL) {
        return (pcore_script_cache *) data;
    }
    if (!create) {
        return NULL;
    }

    cache = (pcore_script_cache *) malloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;
    cache->bytes = 0;
    if (dom_node_set_user_data((struct dom_node *) doc,
            pcore_script_cache_key, cache,
            pcore_script_cache_ud_handler, &old) != DOM_NO_ERR) {
        free(cache);
        return NULL;
    }
    if (old != NULL && old != cache) {
        pcore_script_cache_free((pcore_script_cache *) old);
    }
    return cache;
}

static pcore_script_resource *pcore_script_cache_find(
        pcore_script_cache *cache, const char *url)
{
    pcore_script_resource *entry;

    if (cache == NULL || url == NULL) {
        return NULL;
    }
    for (entry = cache->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->url, url) == 0) {
            return entry;
        }
    }
    return NULL;
}

static int pcore_script_cache_store(pcore_script_cache *cache,
        const char *url, const char *data, int len)
{
    pcore_script_resource *entry;
    size_t url_len;

    if (cache == NULL || url == NULL || data == NULL || len <= 0 ||
            cache->count >= PCORE_SCRIPT_CACHE_MAX ||
            len > PCORE_SCRIPT_CACHE_ENTRY_MAX ||
            cache->bytes > PCORE_SCRIPT_CACHE_BYTES_MAX - len) {
        return 1;
    }
    entry = (pcore_script_resource *) malloc(sizeof(*entry));
    if (entry == NULL) {
        return 1;
    }
    entry->next = NULL;
    entry->url = NULL;
    entry->data = NULL;
    entry->len = 0;
    url_len = strlen(url);
    entry->url = (char *) malloc(url_len + 1);
    entry->data = (char *) malloc((size_t) len);
    if (entry->url == NULL || entry->data == NULL) {
        free(entry->url);
        free(entry->data);
        free(entry);
        return 1;
    }
    memcpy(entry->url, url, url_len + 1);
    memcpy(entry->data, data, (size_t) len);
    entry->len = len;
    if (cache->tail != NULL) {
        cache->tail->next = entry;
    } else {
        cache->head = entry;
    }
    cache->tail = entry;
    cache->count++;
    cache->bytes += len;
    return 0;
}

static pcore_script_resource *pcore_script_cache_at(
        pcore_script_cache *cache, unsigned int index)
{
    pcore_script_resource *entry;
    unsigned int i;

    if (cache == NULL) {
        return NULL;
    }
    entry = cache->head;
    for (i = 0; entry != NULL && i < index; i++) {
        entry = entry->next;
    }
    return entry;
}

int pcore_image_resource_get(dom_document *doc, const char *url,
        const char **out_data, int *out_len)
{
    pcore_image_cache *cache;
    pcore_image_resource *entry;

    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    cache = pcore_image_cache_get(doc, 0);
    entry = pcore_image_cache_find(cache, url);
    if (entry == NULL) {
        return 1;
    }
    if (out_data != NULL) {
        *out_data = entry->data;
    }
    if (out_len != NULL) {
        *out_len = entry->len;
    }
    return 0;
}

int pcore_image_resource_retained_get(dom_document *doc,
        const char *url, int *out_attempted, void **out_native_image,
        void **out_svg, int *out_width, int *out_height)
{
    pcore_image_cache *cache;
    pcore_image_resource *entry;

    if (out_attempted != NULL) {
        *out_attempted = 0;
    }
    if (out_native_image != NULL) {
        *out_native_image = NULL;
    }
    if (out_svg != NULL) {
        *out_svg = NULL;
    }
    if (out_width != NULL) {
        *out_width = 0;
    }
    if (out_height != NULL) {
        *out_height = 0;
    }
    cache = pcore_image_cache_get(doc, 0);
    entry = pcore_image_cache_find(cache, url);
    if (entry == NULL) {
        return 1;
    }
    if (!entry->retained_attempted) {
        entry->shared_svg = pcore_shared_svg_find(entry->url,
                entry->data, entry->len);
        if (entry->shared_svg != NULL) {
            entry->retained_attempted = 1;
            entry->svg = entry->shared_svg->svg;
            entry->width = entry->shared_svg->width;
            entry->height = entry->shared_svg->height;
        }
    }
    if (out_attempted != NULL) {
        *out_attempted = entry->retained_attempted;
    }
    if (out_native_image != NULL) {
        *out_native_image = entry->native_image;
    }
    if (out_svg != NULL) {
        *out_svg = entry->svg;
    }
    if (out_width != NULL) {
        *out_width = entry->width;
    }
    if (out_height != NULL) {
        *out_height = entry->height;
    }
    return 0;
}

int pcore_image_resource_retained_store(dom_document *doc,
        const char *url, void *native_image, void *svg,
        int width, int height, const PCoreImageDecodeStats *decode_stats)
{
    pcore_image_cache *cache;
    pcore_image_resource *entry;

    cache = pcore_image_cache_get(doc, 0);
    entry = pcore_image_cache_find(cache, url);
    if (entry == NULL || entry->retained_attempted) {
        return 1;
    }
    entry->retained_attempted = 1;
    entry->native_image = native_image;
    entry->svg = svg;
    entry->width = width;
    entry->height = height;
    if (svg != NULL) {
        entry->shared_svg = pcore_shared_svg_store(entry->url,
                entry->data, entry->len, svg, width, height);
    }
    if (decode_stats != NULL) {
        entry->decode_stats = *decode_stats;
    }
    return 0;
}

PCORE_API int PCore_GetImageDecodeStats(HANDLE hDoc,
        PCoreImageDecodeStats *out_stats)
{
    pcore_image_cache *cache;
    pcore_image_resource *entry;

    if (hDoc == NULL || out_stats == NULL) {
        return 1;
    }
    memset(out_stats, 0, sizeof(*out_stats));
    cache = pcore_image_cache_get((dom_document *) hDoc, 0);
    if (cache == NULL) {
        return 1;
    }
    for (entry = cache->head; entry != NULL; entry = entry->next) {
        out_stats->svg_total_ms += entry->decode_stats.svg_total_ms;
        out_stats->svg_setup_ms += entry->decode_stats.svg_setup_ms;
        out_stats->svg_parse_ms += entry->decode_stats.svg_parse_ms;
        out_stats->svg_raster_ms += entry->decode_stats.svg_raster_ms;
        out_stats->svg_creates += entry->decode_stats.svg_creates;
    }
    return 0;
}

typedef struct pcore_image_fetch_ctx {
    PCoreFetchFn fetch;
    PCoreFreeFn  freefn;
    void        *pw;
    pcore_image_cache *cache;
    int          found;
    int          fetched;
    dom_string  *img_name;
    dom_string  *src_name;
} pcore_image_fetch_ctx;

static void pcore_fetch_image_url(pcore_image_fetch_ctx *ic,
        const char *url_data, size_t url_len)
{
    char *url;
    char *data = NULL;
    int len = 0;
    pcore_image_resource *cached;

    if (ic == NULL || url_data == NULL || url_len == 0) {
        return;
    }
    ic->found++;
    url = (char *) malloc(url_len + 1);
    if (url == NULL) {
        return;
    }
    memcpy(url, url_data, url_len);
    url[url_len] = '\0';
    cached = pcore_image_cache_find(ic->cache, url);
    if (cached != NULL) {
        ic->fetched++;
    } else if (ic->fetch != NULL &&
            ic->fetch(ic->pw, url, &data, &len) == 0 &&
            data != NULL && len > 0) {
        if (pcore_image_cache_store(ic->cache, url, data, len) == 0) {
            ic->fetched++;
        }
    }
    if (data != NULL && ic->freefn != NULL) {
        ic->freefn(ic->pw, data);
    }
    free(url);
}

static void pcore_fetch_images_walk(pcore_image_fetch_ctx *ic, dom_node *node)
{
    dom_node_type type;
    dom_exception err;
    dom_node *child;

    err = dom_node_get_node_type(node, &type);
    if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
        dom_string *name = NULL;
        bool is_img = false;
        css_computed_style *style;
        lwc_string *bg_uri = NULL;
        lwc_string *list_uri = NULL;

        style = pcore_node_computed_style(node);
        if (style != NULL &&
                css_computed_background_image(style, &bg_uri) ==
                CSS_BACKGROUND_IMAGE_IMAGE && bg_uri != NULL) {
            pcore_fetch_image_url(ic, lwc_string_data(bg_uri),
                    lwc_string_length(bg_uri));
        }
        if (style != NULL &&
                css_computed_display(style, false) ==
                        CSS_DISPLAY_LIST_ITEM &&
                css_computed_list_style_image(style, &list_uri) ==
                CSS_LIST_STYLE_IMAGE_URI && list_uri != NULL) {
            pcore_fetch_image_url(ic, lwc_string_data(list_uri),
                    lwc_string_length(list_uri));
        }

        err = dom_node_get_node_name(node, &name);
        if (err == DOM_NO_ERR && name != NULL) {
            is_img = dom_string_caseless_isequal(name, ic->img_name);
            dom_string_unref(name);
        }
        if (is_img) {
            dom_string *src = NULL;
            if (dom_element_get_attribute(node, ic->src_name, &src) ==
                    DOM_NO_ERR && src != NULL) {
                const char *su8 = dom_string_data(src);
                size_t sl = dom_string_byte_length(src);

                if (su8 != NULL && sl > 0) {
                    pcore_fetch_image_url(ic, su8, sl);
                }
                dom_string_unref(src);
            }
            return;   /* <img> is void; no useful children to scan. */
        }
    }

    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        dom_node *next;

        pcore_fetch_images_walk(ic, child);
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

PCORE_API int PCore_FetchImageResources(HANDLE hDoc, PCoreFetchFn fetch,
        PCoreFreeFn freefn, void *pw_fetch, int *out_found, int *out_fetched)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node *root = NULL;
    pcore_image_fetch_ctx ic;
    int rc = 1;

    if (out_found != NULL) {
        *out_found = 0;
    }
    if (out_fetched != NULL) {
        *out_fetched = 0;
    }
    if (doc == NULL) {
        return 1;
    }

    ic.fetch = fetch;
    ic.freefn = freefn;
    ic.pw = pw_fetch;
    ic.cache = pcore_image_cache_get(doc, 1);
    ic.found = 0;
    ic.fetched = 0;
    ic.img_name = NULL;
    ic.src_name = NULL;

    if (ic.cache == NULL) {
        goto cleanup;
    }
    dom_string_create((const uint8_t *) "img", 3, &ic.img_name);
    dom_string_create((const uint8_t *) "src", 3, &ic.src_name);
    if (ic.img_name == NULL || ic.src_name == NULL) {
        goto cleanup;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }

    pcore_fetch_images_walk(&ic, root);
    if (out_found != NULL) {
        *out_found = ic.found;
    }
    if (out_fetched != NULL) {
        *out_fetched = ic.fetched;
    }
    rc = 0;

cleanup:
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (ic.img_name != NULL) {
        dom_string_unref(ic.img_name);
    }
    if (ic.src_name != NULL) {
        dom_string_unref(ic.src_name);
    }
    return rc;
}

typedef struct pcore_script_fetch_ctx {
    PCoreFetchFn fetch;
    PCoreFreeFn freefn;
    PCoreResolveUrlFn resolve;
    void *pw;
    const char *document_url;
    pcore_script_cache *cache;
    int found;
    int fetched;
    dom_string *script_name;
    dom_string *src_name;
} pcore_script_fetch_ctx;

static void pcore_fetch_script_url(pcore_script_fetch_ctx *sc,
        const char *reference_data, size_t reference_len)
{
    char reference[1024];
    char url[2048];
    char *data;
    const char *resolved;
    int len;
    int copy_len;
    pcore_script_resource *cached;

    if (sc == NULL || reference_data == NULL || reference_len == 0) {
        return;
    }
    sc->found++;
    if (reference_len >= sizeof(reference)) {
        return;
    }
    copy_len = (int) reference_len;
    memcpy(reference, reference_data, (size_t) copy_len);
    reference[copy_len] = '\0';
    url[0] = '\0';
    resolved = reference;
    if (sc->resolve != NULL && sc->document_url != NULL) {
        if (sc->resolve(sc->pw, sc->document_url, reference,
                url, (int) sizeof(url)) != 0) {
            return;
        }
        resolved = url;
    }
    if (resolved == NULL || resolved[0] == '\0') {
        return;
    }
    cached = pcore_script_cache_find(sc->cache, resolved);
    if (cached != NULL) {
        sc->fetched++;
        return;
    }
    data = NULL;
    len = 0;
    if (sc->fetch != NULL &&
            sc->fetch(sc->pw, resolved, &data, &len) == 0 &&
            data != NULL && len > 0) {
        if (pcore_script_cache_store(sc->cache, resolved, data, len) == 0) {
            sc->fetched++;
        }
    }
    if (data != NULL && sc->freefn != NULL) {
        sc->freefn(sc->pw, data);
    }
}

static void pcore_fetch_scripts_walk(pcore_script_fetch_ctx *sc,
        dom_node *node)
{
    dom_node_type type;
    dom_exception err;
    dom_node *child;
    dom_node *next;
    dom_string *name;
    dom_string *src;
    const char *src_data;
    size_t src_len;
    bool is_script;

    name = NULL;
    src = NULL;
    is_script = false;
    err = dom_node_get_node_type(node, &type);
    if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
        err = dom_node_get_node_name(node, &name);
        if (err == DOM_NO_ERR && name != NULL) {
            is_script = dom_string_caseless_isequal(name,
                    sc->script_name);
            dom_string_unref(name);
            name = NULL;
        }
        if (is_script) {
            if (dom_element_get_attribute(node, sc->src_name, &src) ==
                    DOM_NO_ERR && src != NULL) {
                src_data = dom_string_data(src);
                src_len = dom_string_byte_length(src);
                if (src_data != NULL && src_len > 0) {
                    pcore_fetch_script_url(sc, src_data, src_len);
                }
                dom_string_unref(src);
            }
            return;   /* Inline script text is not a resource. */
        }
    }

    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        pcore_fetch_scripts_walk(sc, child);
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

PCORE_API int PCore_FetchScriptResources(HANDLE hDoc, PCoreFetchFn fetch,
        PCoreFreeFn freefn, void *pw_fetch, int *out_found, int *out_fetched)
{
    return PCore_FetchScriptResourcesEx(hDoc, NULL, NULL, fetch, freefn,
            pw_fetch, out_found, out_fetched);
}

PCORE_API int PCore_FetchScriptResourcesEx(HANDLE hDoc,
        const char *document_url, PCoreResolveUrlFn resolve,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw_fetch,
        int *out_found, int *out_fetched)
{
    dom_document *doc;
    dom_node *root;
    pcore_script_fetch_ctx sc;
    int rc;

    if (out_found != NULL) {
        *out_found = 0;
    }
    if (out_fetched != NULL) {
        *out_fetched = 0;
    }
    doc = (dom_document *) hDoc;
    if (doc == NULL) {
        return 1;
    }

    memset(&sc, 0, sizeof(sc));
    sc.fetch = fetch;
    sc.freefn = freefn;
    sc.resolve = resolve;
    sc.pw = pw_fetch;
    sc.document_url = document_url;
    sc.cache = pcore_script_cache_get(doc, 1);
    sc.script_name = NULL;
    sc.src_name = NULL;
    root = NULL;
    rc = 1;
    if (sc.cache == NULL) {
        goto cleanup;
    }
    dom_string_create((const uint8_t *) "script", 6, &sc.script_name);
    dom_string_create((const uint8_t *) "src", 3, &sc.src_name);
    if (sc.script_name == NULL || sc.src_name == NULL) {
        goto cleanup;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }
    pcore_fetch_scripts_walk(&sc, root);
    if (out_found != NULL) {
        *out_found = sc.found;
    }
    if (out_fetched != NULL) {
        *out_fetched = sc.fetched;
    }
    rc = 0;

cleanup:
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (sc.script_name != NULL) {
        dom_string_unref(sc.script_name);
    }
    if (sc.src_name != NULL) {
        dom_string_unref(sc.src_name);
    }
    return rc;
}

PCORE_API int PCore_GetScriptResourceCount(HANDLE hDoc)
{
    pcore_script_cache *cache;

    if (hDoc == NULL) {
        return -1;
    }
    cache = pcore_script_cache_get((dom_document *) hDoc, 0);
    return (cache != NULL) ? cache->count : 0;
}

PCORE_API int PCore_GetScriptResource(HANDLE hDoc, unsigned int index,
        PCoreScriptResourceInfo *out_info, char *url, int url_capacity,
        const char **out_data)
{
    pcore_script_cache *cache;
    pcore_script_resource *entry;
    size_t url_len;
    int copy_len;

    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (hDoc == NULL) {
        return 1;
    }
    cache = pcore_script_cache_get((dom_document *) hDoc, 0);
    entry = pcore_script_cache_at(cache, index);
    if (entry == NULL) {
        return 1;
    }
    url_len = strlen(entry->url);
    if (url != NULL && url_capacity > 0) {
        copy_len = (url_len < (size_t) (url_capacity - 1)) ?
                (int) url_len : url_capacity - 1;
        memcpy(url, entry->url, (size_t) copy_len);
        url[copy_len] = '\0';
    }
    if (out_info != NULL) {
        out_info->available = (entry->data != NULL && entry->len > 0);
        out_info->url_bytes = (int) url_len;
        out_info->data_bytes = entry->len;
    }
    if (out_data != NULL) {
        *out_data = entry->data;
    }
    return 0;
}

typedef struct pcore_inline_script_scan {
    unsigned int target;
    unsigned int count;
    int found;
    dom_string *script_name;
    dom_string *src_name;
    dom_string *type_name;
    dom_string *source;
    dom_string *mime_type;
} pcore_inline_script_scan;

static void pcore_inline_script_walk(pcore_inline_script_scan *scan,
        dom_node *node)
{
    dom_node_type node_type;
    dom_node *child;
    dom_node *next;
    dom_string *name;
    dom_string *src;
    dom_string *body;
    bool is_script;

    if (scan == NULL || node == NULL || scan->found) {
        return;
    }
    name = NULL;
    src = NULL;
    body = NULL;
    is_script = false;
    if (dom_node_get_node_type(node, &node_type) == DOM_NO_ERR &&
            node_type == DOM_ELEMENT_NODE &&
            dom_node_get_node_name(node, &name) == DOM_NO_ERR &&
            name != NULL) {
        is_script = dom_string_caseless_isequal(name, scan->script_name);
        dom_string_unref(name);
        name = NULL;
    }
    if (is_script) {
        if (dom_element_get_attribute(node, scan->src_name, &src) ==
                DOM_NO_ERR && src != NULL) {
            if (dom_string_byte_length(src) > 0) {
                dom_string_unref(src);
                return;
            }
            dom_string_unref(src);
            src = NULL;
        }
        if (dom_node_get_text_content(node, &body) == DOM_NO_ERR &&
                body != NULL && dom_string_byte_length(body) > 0) {
            if (scan->count == scan->target) {
                scan->source = body;
                body = NULL;
                if (dom_element_get_attribute(node, scan->type_name,
                        &scan->mime_type) != DOM_NO_ERR) {
                    scan->mime_type = NULL;
                }
                scan->found = 1;
            }
            scan->count++;
        }
        if (body != NULL) {
            dom_string_unref(body);
        }
        return;
    }

    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        pcore_inline_script_walk(scan, child);
        if (scan->found) {
            dom_node_unref(child);
            return;
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

static int pcore_inline_script_scan_document(dom_document *doc,
        unsigned int target, pcore_inline_script_scan *scan)
{
    dom_node *root;
    int rc;

    if (doc == NULL || scan == NULL) {
        return 1;
    }
    memset(scan, 0, sizeof(*scan));
    scan->target = target;
    root = NULL;
    rc = 1;
    dom_string_create((const uint8_t *) "script", 6, &scan->script_name);
    dom_string_create((const uint8_t *) "src", 3, &scan->src_name);
    dom_string_create((const uint8_t *) "type", 4, &scan->type_name);
    if (scan->script_name == NULL || scan->src_name == NULL ||
            scan->type_name == NULL) {
        goto cleanup;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }
    pcore_inline_script_walk(scan, root);
    rc = 0;

cleanup:
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (scan->script_name != NULL) {
        dom_string_unref(scan->script_name);
        scan->script_name = NULL;
    }
    if (scan->src_name != NULL) {
        dom_string_unref(scan->src_name);
        scan->src_name = NULL;
    }
    if (scan->type_name != NULL) {
        dom_string_unref(scan->type_name);
        scan->type_name = NULL;
    }
    return rc;
}

static void pcore_copy_dom_string(dom_string *value, char *buffer,
        int capacity, int *out_bytes)
{
    const char *data;
    size_t length;
    size_t copy_length;

    data = NULL;
    length = 0;
    if (value != NULL) {
        data = dom_string_data(value);
        length = dom_string_byte_length(value);
    }
    if (out_bytes != NULL) {
        *out_bytes = (int) length;
    }
    if (buffer == NULL || capacity <= 0) {
        return;
    }
    copy_length = length;
    if (copy_length > (size_t) (capacity - 1)) {
        copy_length = (size_t) (capacity - 1);
    }
    if (copy_length > 0 && data != NULL) {
        memcpy(buffer, data, copy_length);
    }
    buffer[copy_length] = '\0';
}

PCORE_API int PCore_GetInlineScriptCount(HANDLE hDoc)
{
    pcore_inline_script_scan scan;

    if (pcore_inline_script_scan_document((dom_document *) hDoc,
            (unsigned int) -1, &scan) != 0) {
        return -1;
    }
    return (int) scan.count;
}

PCORE_API int PCore_GetInlineScript(HANDLE hDoc, unsigned int index,
        PCoreInlineScriptInfo *out_info, char *source, int source_capacity,
        char *type, int type_capacity)
{
    pcore_inline_script_scan scan;
    int source_bytes;
    int type_bytes;

    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (source != NULL && source_capacity > 0) {
        source[0] = '\0';
    }
    if (type != NULL && type_capacity > 0) {
        type[0] = '\0';
    }
    if (pcore_inline_script_scan_document((dom_document *) hDoc,
            index, &scan) != 0 || !scan.found || scan.source == NULL) {
        return 1;
    }
    source_bytes = 0;
    type_bytes = 0;
    pcore_copy_dom_string(scan.source, source, source_capacity,
            &source_bytes);
    pcore_copy_dom_string(scan.mime_type, type, type_capacity, &type_bytes);
    if (out_info != NULL) {
        out_info->source_bytes = source_bytes;
        out_info->type_bytes = type_bytes;
    }
    dom_string_unref(scan.source);
    if (scan.mime_type != NULL) {
        dom_string_unref(scan.mime_type);
    }
    return 0;
}

typedef struct pcore_script_sequence_scan {
    unsigned int target;
    unsigned int count;
    int found;
    int kind;
    dom_string *script_name;
    dom_string *src_name;
    dom_string *type_name;
    dom_string *source;
    dom_string *reference;
    dom_string *mime_type;
} pcore_script_sequence_scan;

static void pcore_script_sequence_walk(
        pcore_script_sequence_scan *scan, dom_node *node)
{
    dom_node_type node_type;
    dom_node *child;
    dom_node *next;
    dom_string *name;
    dom_string *src;
    dom_string *body;
    dom_string *selected;
    int selected_kind;
    bool is_script;

    if (scan == NULL || node == NULL || scan->found) {
        return;
    }
    name = NULL;
    src = NULL;
    body = NULL;
    selected = NULL;
    selected_kind = 0;
    is_script = false;
    if (dom_node_get_node_type(node, &node_type) == DOM_NO_ERR &&
            node_type == DOM_ELEMENT_NODE &&
            dom_node_get_node_name(node, &name) == DOM_NO_ERR &&
            name != NULL) {
        is_script = dom_string_caseless_isequal(name,
                scan->script_name);
        dom_string_unref(name);
        name = NULL;
    }
    if (is_script) {
        if (dom_element_get_attribute(node, scan->src_name, &src) ==
                DOM_NO_ERR && src != NULL &&
                dom_string_byte_length(src) > 0) {
            selected = src;
            src = NULL;
            selected_kind = 2;
        } else if (dom_node_get_text_content(node, &body) == DOM_NO_ERR &&
                body != NULL && dom_string_byte_length(body) > 0) {
            selected = body;
            body = NULL;
            selected_kind = 1;
        }
        if (selected != NULL) {
            if (scan->count == scan->target) {
                scan->kind = selected_kind;
                if (selected_kind == 1) {
                    scan->source = selected;
                } else {
                    scan->reference = selected;
                }
                selected = NULL;
                if (dom_element_get_attribute(node, scan->type_name,
                        &scan->mime_type) != DOM_NO_ERR) {
                    scan->mime_type = NULL;
                }
                scan->found = 1;
            }
            scan->count++;
        }
        if (selected != NULL) {
            dom_string_unref(selected);
        }
        if (src != NULL) {
            dom_string_unref(src);
        }
        if (body != NULL) {
            dom_string_unref(body);
        }
        return;
    }

    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        pcore_script_sequence_walk(scan, child);
        if (scan->found) {
            dom_node_unref(child);
            return;
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
}

static int pcore_script_sequence_scan_document(dom_document *doc,
        unsigned int target, pcore_script_sequence_scan *scan)
{
    dom_node *root;
    int rc;

    if (doc == NULL || scan == NULL) {
        return 1;
    }
    memset(scan, 0, sizeof(*scan));
    scan->target = target;
    root = NULL;
    rc = 1;
    dom_string_create((const uint8_t *) "script", 6, &scan->script_name);
    dom_string_create((const uint8_t *) "src", 3, &scan->src_name);
    dom_string_create((const uint8_t *) "type", 4, &scan->type_name);
    if (scan->script_name == NULL || scan->src_name == NULL ||
            scan->type_name == NULL) {
        goto cleanup;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }
    pcore_script_sequence_walk(scan, root);
    rc = 0;

cleanup:
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (scan->script_name != NULL) {
        dom_string_unref(scan->script_name);
        scan->script_name = NULL;
    }
    if (scan->src_name != NULL) {
        dom_string_unref(scan->src_name);
        scan->src_name = NULL;
    }
    if (scan->type_name != NULL) {
        dom_string_unref(scan->type_name);
        scan->type_name = NULL;
    }
    return rc;
}

static void pcore_script_sequence_release(
        pcore_script_sequence_scan *scan)
{
    if (scan == NULL) {
        return;
    }
    if (scan->source != NULL) {
        dom_string_unref(scan->source);
        scan->source = NULL;
    }
    if (scan->reference != NULL) {
        dom_string_unref(scan->reference);
        scan->reference = NULL;
    }
    if (scan->mime_type != NULL) {
        dom_string_unref(scan->mime_type);
        scan->mime_type = NULL;
    }
}

PCORE_API int PCore_GetScriptCount(HANDLE hDoc)
{
    pcore_script_sequence_scan scan;

    if (pcore_script_sequence_scan_document((dom_document *) hDoc,
            (unsigned int) -1, &scan) != 0) {
        return -1;
    }
    return (int) scan.count;
}

PCORE_API int PCore_GetScript(HANDLE hDoc, unsigned int index,
        const char *document_url, PCoreResolveUrlFn resolve, void *pw,
        PCoreScriptInfo *out_info, char *source, int source_capacity,
        char *url, int url_capacity, char *type, int type_capacity,
        const char **out_data)
{
    pcore_script_sequence_scan scan;
    pcore_script_cache *cache;
    pcore_script_resource *entry;
    const char *reference_data;
    const char *resolved_url;
    char *reference;
    char resolved[2048];
    size_t reference_len;
    int source_bytes;
    int url_bytes;
    int type_bytes;

    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (source != NULL && source_capacity > 0) {
        source[0] = '\0';
    }
    if (url != NULL && url_capacity > 0) {
        url[0] = '\0';
    }
    if (type != NULL && type_capacity > 0) {
        type[0] = '\0';
    }
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (pcore_script_sequence_scan_document((dom_document *) hDoc,
            index, &scan) != 0 || !scan.found) {
        return 1;
    }
    source_bytes = 0;
    url_bytes = 0;
    type_bytes = 0;
    cache = NULL;
    entry = NULL;
    reference = NULL;
    if (scan.kind == 1) {
        pcore_copy_dom_string(scan.source, source, source_capacity,
                &source_bytes);
    } else if (scan.reference != NULL) {
        reference_data = dom_string_data(scan.reference);
        reference_len = dom_string_byte_length(scan.reference);
        reference = (char *) malloc(reference_len + 1);
        if (reference == NULL) {
            pcore_script_sequence_release(&scan);
            return 1;
        }
        memcpy(reference, reference_data, reference_len);
        reference[reference_len] = '\0';
        resolved[0] = '\0';
        resolved_url = reference;
        if (resolve != NULL && document_url != NULL) {
            if (resolve(pw, document_url, reference, resolved,
                    (int) sizeof(resolved)) == 0) {
                resolved_url = resolved;
            }
        }
        url_bytes = (int) strlen(resolved_url);
        if (url != NULL && url_capacity > 0) {
            if (url_capacity - 1 < url_bytes) {
                memcpy(url, resolved_url, (size_t) (url_capacity - 1));
                url[url_capacity - 1] = '\0';
            } else {
                memcpy(url, resolved_url, (size_t) url_bytes);
                url[url_bytes] = '\0';
            }
        }
        cache = pcore_script_cache_get((dom_document *) hDoc, 0);
        entry = pcore_script_cache_find(cache, resolved_url);
        if (entry != NULL) {
            if (out_data != NULL) {
                *out_data = entry->data;
            }
        }
        free(reference);
    }
    pcore_copy_dom_string(scan.mime_type, type, type_capacity,
            &type_bytes);
    if (out_info != NULL) {
        out_info->kind = scan.kind;
        out_info->available = (entry != NULL && entry->data != NULL &&
                entry->len > 0);
        out_info->source_bytes = source_bytes;
        out_info->url_bytes = url_bytes;
        out_info->type_bytes = type_bytes;
        out_info->data_bytes = (entry != NULL) ? entry->len : 0;
    }
    pcore_script_sequence_release(&scan);
    return 0;
}

static dom_element *pcore_document_structural_element(dom_document *doc,
        const char *element_id);

static int pcore_contenteditable_utf8_valid(const char *text)
{
    const unsigned char *p;
    unsigned int cp;
    int need;
    int i;

    if (text == NULL) {
        return 0;
    }
    p = (const unsigned char *) text;
    while (*p != 0) {
        if (*p < 0x80) {
            p++;
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
            return 0;
        }
        for (i = 1; i <= need; i++) {
            if (p[i] == 0 || (p[i] & 0xc0) != 0x80) {
                return 0;
            }
            cp = (cp << 6) | (p[i] & 0x3f);
        }
        if ((need == 2 && cp < 0x800) ||
                (need == 3 && cp < 0x10000) ||
                (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
            return 0;
        }
        p += need + 1;
    }
    return 1;
}

static dom_element *pcore_element_by_id(dom_document *doc,
        const char *element_id)
{
    dom_string *id;
    dom_element *element;

    if (doc == NULL || element_id == NULL || element_id[0] == '\0') {
        return NULL;
    }
    id = NULL;
    element = NULL;
    if (dom_string_create((const uint8_t *) element_id, strlen(element_id),
            &id) != DOM_NO_ERR || id == NULL) {
        return NULL;
    }
    if (dom_document_get_element_by_id(doc, id, &element) != DOM_NO_ERR) {
        element = NULL;
    }
    dom_string_unref(id);
    if (element == NULL) {
        element = pcore_document_structural_element(doc, element_id);
    }
    return element;
}

static int pcore_element_name_is(dom_element *element, const char *name)
{
    dom_string *actual;
    dom_string *expected;
    int same;

    actual = NULL;
    expected = NULL;
    same = 0;
    if (element == NULL || name == NULL ||
            dom_node_get_node_name((dom_node *) element, &actual) !=
                    DOM_NO_ERR || actual == NULL ||
            dom_string_create((const uint8_t *) name, strlen(name),
                    &expected) != DOM_NO_ERR || expected == NULL) {
        if (actual != NULL) { dom_string_unref(actual); }
        if (expected != NULL) { dom_string_unref(expected); }
        return 0;
    }
    same = dom_string_caseless_isequal(actual, expected) ? 1 : 0;
    dom_string_unref(expected);
    dom_string_unref(actual);
    return same;
}

/* Return a retained element for one of the reserved document structure
 * tokens. Real id lookup runs first in pcore_element_by_id(), so an unusual
 * page that happens to use one of these strings as an id keeps its normal
 * getElementById identity. */
static dom_element *pcore_document_structural_element(dom_document *doc,
        const char *element_id)
{
    dom_element *root;
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    const char *wanted;

    if (doc == NULL || element_id == NULL) {
        return NULL;
    }
    if (strcmp(element_id, PCORE_DOCUMENT_ELEMENT_TOKEN) == 0) {
        root = NULL;
        if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR) {
            return NULL;
        }
        return root;
    }
    if (strcmp(element_id, PCORE_DOCUMENT_HEAD_TOKEN) == 0) {
        wanted = "head";
    } else if (strcmp(element_id, PCORE_DOCUMENT_BODY_TOKEN) == 0) {
        wanted = "body";
    } else {
        return NULL;
    }
    root = NULL;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        return NULL;
    }
    child = NULL;
    if (dom_node_get_first_child((dom_node *) root, &child) != DOM_NO_ERR) {
        dom_node_unref((dom_node *) root);
        return NULL;
    }
    while (child != NULL) {
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            dom_node_unref((dom_node *) root);
            return NULL;
        }
        if (type == DOM_ELEMENT_NODE &&
                pcore_element_name_is((dom_element *) child, wanted)) {
            dom_node_unref((dom_node *) root);
            return (dom_element *) child;
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            dom_node_unref((dom_node *) root);
            return NULL;
        }
        dom_node_unref(child);
        child = next;
    }
    dom_node_unref((dom_node *) root);
    return NULL;
}

static const char *pcore_document_structural_token(dom_node *node)
{
    dom_node *parent;
    dom_node_type type;
    const char *token;

    if (node == NULL) {
        return NULL;
    }
    parent = NULL;
    if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR ||
            parent == NULL || dom_node_get_node_type(parent, &type) !=
            DOM_NO_ERR) {
        if (parent != NULL) {
            dom_node_unref(parent);
        }
        return NULL;
    }
    token = NULL;
    if (type == DOM_DOCUMENT_NODE) {
        token = PCORE_DOCUMENT_ELEMENT_TOKEN;
    } else if (type == DOM_ELEMENT_NODE &&
            pcore_element_name_is((dom_element *) parent, "html")) {
        if (pcore_element_name_is((dom_element *) node, "head")) {
            token = PCORE_DOCUMENT_HEAD_TOKEN;
        } else if (pcore_element_name_is((dom_element *) node, "body")) {
            token = PCORE_DOCUMENT_BODY_TOKEN;
        }
    }
    dom_node_unref(parent);
    return token;
}

static int pcore_copy_document_structural_token(dom_node *node, char *value,
        int value_capacity, int *out_bytes)
{
    const char *token;
    size_t length;
    size_t copy_length;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (value != NULL && value_capacity > 0) {
        value[0] = '\0';
    }
    token = pcore_document_structural_token(node);
    if (token == NULL) {
        return 2;
    }
    length = strlen(token);
    if (out_bytes != NULL) {
        *out_bytes = (int) length;
    }
    if (value == NULL || value_capacity <= 0) {
        return 0;
    }
    copy_length = length;
    if (copy_length > (size_t) (value_capacity - 1)) {
        copy_length = (size_t) (value_capacity - 1);
    }
    if (copy_length > 0) {
        memcpy(value, token, copy_length);
    }
    value[copy_length] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* bounded script-facing DOM relationships                            */

static int pcore_relation_copy_element_id(dom_node *node, char *value,
        int value_capacity, int *out_bytes)
{
    dom_node_type type;
    dom_namednodemap *attributes;
    dom_string *id_name;
    dom_string *attribute_name;
    dom_string *attribute_text;
    dom_node *attribute;
    dom_ulong length;
    dom_ulong i;
    int found;
    dom_exception err;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (value != NULL && value_capacity > 0) {
        value[0] = '\0';
    }
    if (node == NULL || dom_node_get_node_type(node, &type) != DOM_NO_ERR ||
            type != DOM_ELEMENT_NODE) {
        return 1;
    }
    attributes = NULL;
    id_name = NULL;
    attribute_name = NULL;
    attribute = NULL;
    attribute_text = NULL;
    if (dom_string_create((const uint8_t *) "id", 2, &id_name) !=
            DOM_NO_ERR || id_name == NULL ||
            dom_node_get_attributes(node, &attributes) != DOM_NO_ERR ||
            attributes == NULL || dom_namednodemap_get_length(attributes,
            &length) != DOM_NO_ERR) {
        if (id_name != NULL) {
            dom_string_unref(id_name);
        }
        if (attributes != NULL) {
            dom_namednodemap_unref(attributes);
        }
        return 1;
    }
    err = DOM_NO_ERR;
    found = 0;
    for (i = 0; i < length; i++) {
        attribute = NULL;
        attribute_name = NULL;
        if (dom_namednodemap_item(attributes, i, &attribute) != DOM_NO_ERR ||
                attribute == NULL ||
                dom_node_get_node_name(attribute, &attribute_name) !=
                DOM_NO_ERR || attribute_name == NULL) {
            err = DOM_INVALID_STATE_ERR;
            if (attribute_name != NULL) {
                dom_string_unref(attribute_name);
            }
            if (attribute != NULL) {
                dom_node_unref(attribute);
            }
            break;
        }
        if (dom_string_isequal(attribute_name, id_name)) {
            err = dom_node_get_text_content(attribute, &attribute_text);
            found = 1;
            dom_string_unref(attribute_name);
            attribute_name = NULL;
            dom_node_unref(attribute);
            attribute = NULL;
            break;
        }
        dom_string_unref(attribute_name);
        attribute_name = NULL;
        dom_node_unref(attribute);
        attribute = NULL;
    }
    if (attribute_name != NULL) {
        dom_string_unref(attribute_name);
    }
    if (attribute != NULL) {
        dom_node_unref(attribute);
    }
    dom_string_unref(id_name);
    dom_namednodemap_unref(attributes);
    if (err != DOM_NO_ERR) {
        if (attribute_text != NULL) {
            dom_string_unref(attribute_text);
        }
        return 1;
    }
    if (!found || attribute_text == NULL ||
            dom_string_byte_length(attribute_text) == 0) {
        if (attribute_text != NULL) {
            dom_string_unref(attribute_text);
        }
        return pcore_copy_document_structural_token(node, value,
                value_capacity, out_bytes);
    }
    pcore_copy_dom_string(attribute_text, value, value_capacity, out_bytes);
    dom_string_unref(attribute_text);
    return 0;
}

static int pcore_relation_copy_node_name(dom_node *node, char *value,
        int value_capacity, int *out_bytes)
{
    dom_node_type type;
    dom_string *name;
    dom_exception err;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (value != NULL && value_capacity > 0) {
        value[0] = '\0';
    }
    if (node == NULL || dom_node_get_node_type(node, &type) != DOM_NO_ERR ||
            type != DOM_ELEMENT_NODE) {
        return 1;
    }
    name = NULL;
    err = dom_node_get_node_name(node, &name);
    if (err != DOM_NO_ERR || name == NULL || dom_string_byte_length(name) == 0) {
        if (name != NULL) {
            dom_string_unref(name);
        }
        return 1;
    }
    pcore_copy_dom_string(name, value, value_capacity, out_bytes);
    dom_string_unref(name);
    return 0;
}

static int pcore_relation_child_at(dom_node *node, unsigned int wanted,
        char *value, int value_capacity, int *out_bytes)
{
    dom_nodelist *children;
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    uint32_t length;
    unsigned int index;
    int bytes;
    int err;

    children = NULL;
    if (dom_node_get_child_nodes(node, &children) != DOM_NO_ERR ||
            children == NULL || dom_nodelist_get_length(children,
            &length) != DOM_NO_ERR) {
        if (children != NULL) {
            dom_nodelist_unref(children);
        }
        return 1;
    }
    child = NULL;
    if (length > 0 && (dom_nodelist_item(children, 0, &child) !=
            DOM_NO_ERR || child == NULL)) {
        dom_nodelist_unref(children);
        return 1;
    }
    dom_nodelist_unref(children);
    index = 0;
    while (child != NULL) {
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE) {
            bytes = 0;
            err = pcore_relation_copy_element_id(child, NULL, 0, &bytes);
            if (err == 0 && wanted == index) {
                err = pcore_relation_copy_element_id(child, value,
                        value_capacity, out_bytes);
                dom_node_unref(child);
                return err;
            } else if (err == 1) {
                dom_node_unref(child);
                return 1;
            }
            if (err == 0) {
                index++;
            }
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        dom_node_unref(child);
        child = next;
    }
    return 2;
}

static int pcore_relation_child_count(dom_node *node, int *out_count)
{
    dom_nodelist *children;
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    uint32_t length;
    int count;
    int bytes;
    int err;

    if (out_count == NULL) {
        return 1;
    }
    *out_count = 0;
    children = NULL;
    if (dom_node_get_child_nodes(node, &children) != DOM_NO_ERR ||
            children == NULL || dom_nodelist_get_length(children,
            &length) != DOM_NO_ERR) {
        if (children != NULL) {
            dom_nodelist_unref(children);
        }
        return 1;
    }
    child = NULL;
    if (length > 0 && (dom_nodelist_item(children, 0, &child) != DOM_NO_ERR ||
            child == NULL)) {
        dom_nodelist_unref(children);
        return 1;
    }
    dom_nodelist_unref(children);
    count = 0;
    while (child != NULL) {
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE) {
            bytes = 0;
            err = pcore_relation_copy_element_id(child, NULL, 0, &bytes);
            if (err == 0) {
                count++;
            } else if (err == 1) {
                dom_node_unref(child);
                return 1;
            }
        }
        next = NULL;
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        dom_node_unref(child);
        child = next;
    }
    *out_count = count;
    return 0;
}

static int pcore_relation_sibling(dom_node *node, int previous,
        char *value, int value_capacity, int *out_bytes)
{
    dom_node *sibling;
    dom_node *next;
    dom_node_type type;
    int bytes;
    int err;

    sibling = NULL;
    if ((previous ? dom_node_get_previous_sibling(node, &sibling) :
            dom_node_get_next_sibling(node, &sibling)) != DOM_NO_ERR) {
        return 1;
    }
    while (sibling != NULL) {
        next = NULL;
        if (dom_node_get_node_type(sibling, &type) != DOM_NO_ERR) {
            dom_node_unref(sibling);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE) {
            bytes = 0;
            err = pcore_relation_copy_element_id(sibling, NULL, 0,
                    &bytes);
            if (err == 0) {
                err = pcore_relation_copy_element_id(sibling, value,
                        value_capacity, out_bytes);
                dom_node_unref(sibling);
                return err;
            } else if (err == 1) {
                dom_node_unref(sibling);
                return 1;
            }
        }
        if ((previous ? dom_node_get_previous_sibling(sibling, &next) :
                dom_node_get_next_sibling(sibling, &next)) != DOM_NO_ERR) {
            dom_node_unref(sibling);
            return 1;
        }
        dom_node_unref(sibling);
        sibling = next;
    }
    return 2;
}

static int pcore_relation_parent(dom_node *node, char *value,
        int value_capacity, int *out_bytes)
{
    dom_node *parent;
    dom_node *next;
    dom_node_type type;
    int bytes;
    int err;

    parent = NULL;
    if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR) {
        return 1;
    }
    while (parent != NULL) {
        next = NULL;
        if (dom_node_get_node_type(parent, &type) != DOM_NO_ERR) {
            dom_node_unref(parent);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE) {
            bytes = 0;
            err = pcore_relation_copy_element_id(parent, NULL, 0, &bytes);
            if (err == 0) {
                err = pcore_relation_copy_element_id(parent, value,
                        value_capacity, out_bytes);
                dom_node_unref(parent);
                return err;
            } else if (err == 1) {
                dom_node_unref(parent);
                return 1;
            }
        }
        if (dom_node_get_parent_node(parent, &next) != DOM_NO_ERR) {
            dom_node_unref(parent);
            return 1;
        }
        dom_node_unref(parent);
        parent = next;
    }
    return 2;
}

static int pcore_relation_form_owner_is(dom_node *node,
        dom_element *wanted_form)
{
    dom_node *current;
    dom_node *parent;
    dom_node_type type;
    int is_form;

    current = dom_node_ref(node);
    while (current != NULL) {
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return 0;
        }
        dom_node_unref(current);
        current = parent;
        if (current == NULL) {
            break;
        }
        if (dom_node_get_node_type(current, &type) != DOM_NO_ERR) {
            dom_node_unref(current);
            return 0;
        }
        is_form = type == DOM_ELEMENT_NODE &&
                pcore_element_name_is((dom_element *) current, "form");
        if (is_form) {
            if (current == (dom_node *) wanted_form) {
                dom_node_unref(current);
                return 1;
            }
            dom_node_unref(current);
            return 0;
        }
    }
    return 0;
}

static int pcore_relation_is_control(dom_element *element)
{
    return pcore_element_name_is(element, "input") ||
            pcore_element_name_is(element, "select") ||
            pcore_element_name_is(element, "textarea") ||
            pcore_element_name_is(element, "button");
}

static int pcore_relation_walk_controls(dom_node *node, dom_element *form,
        unsigned int wanted, unsigned int *count, char *value,
        int value_capacity, int *out_bytes, int *found)
{
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    int bytes;
    int err;

    if (count == NULL || found == NULL) {
        return 1;
    }
    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return 1;
    }
    while (child != NULL) {
        next = NULL;
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE) {
            if (pcore_relation_is_control((dom_element *) child) &&
                    pcore_relation_form_owner_is(child, form)) {
                bytes = 0;
                err = pcore_relation_copy_element_id(child, NULL, 0,
                        &bytes);
                if (err == 0) {
                    if (wanted == (unsigned int) -1 ||
                            *count == wanted) {
                        if (wanted != (unsigned int) -1) {
                            err = pcore_relation_copy_element_id(child, value,
                                    value_capacity, out_bytes);
                            if (err != 0) {
                                dom_node_unref(child);
                                return err;
                            }
                            *found = 1;
                            dom_node_unref(child);
                            return 0;
                        }
                    }
                    (*count)++;
                } else if (err == 1) {
                    dom_node_unref(child);
                    return 1;
                }
            }
            err = pcore_relation_walk_controls(child, form, wanted, count,
                    value, value_capacity, out_bytes, found);
            if (err != 0 || *found) {
                dom_node_unref(child);
                return err;
            }
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        dom_node_unref(child);
        child = next;
    }
    return 0;
}

static int pcore_relation_form_owner(dom_node *node, char *value,
        int value_capacity, int *out_bytes)
{
    dom_node *current;
    dom_node *parent;
    dom_node_type type;
    int err;

    current = dom_node_ref(node);
    while (current != NULL) {
        parent = NULL;
        if (dom_node_get_parent_node(current, &parent) != DOM_NO_ERR) {
            dom_node_unref(current);
            return 1;
        }
        dom_node_unref(current);
        current = parent;
        if (current == NULL) {
            break;
        }
        if (dom_node_get_node_type(current, &type) != DOM_NO_ERR) {
            dom_node_unref(current);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE &&
                pcore_element_name_is((dom_element *) current, "form")) {
            err = pcore_relation_copy_element_id(current, value,
                    value_capacity, out_bytes);
            dom_node_unref(current);
            return err == 0 ? 0 : (err == 2 ? 2 : 1);
        }
    }
    return 2;
}

/* The browser DOM exposes a deliberately bounded label/control association
 * slice.  It is computed from the parsed document on each relation query so
 * attribute changes are observed without introducing a second live DOM cache.
 * Only the labelable controls already represented by the core form bridge are
 * included (input except type=hidden, select, textarea and button). */
static int pcore_relation_attribute_value(dom_element *element,
        const char *name, dom_string **out_value)
{
    dom_string *dom_name;
    dom_exception err;

    if (out_value != NULL) {
        *out_value = NULL;
    }
    if (element == NULL || name == NULL || out_value == NULL) {
        return 1;
    }
    dom_name = NULL;
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &dom_name) != DOM_NO_ERR || dom_name == NULL) {
        return 1;
    }
    err = dom_element_get_attribute(element, dom_name, out_value);
    dom_string_unref(dom_name);
    if (err != DOM_NO_ERR) {
        if (*out_value != NULL) {
            dom_string_unref(*out_value);
            *out_value = NULL;
        }
        return 1;
    }
    return (*out_value == NULL) ? 2 : 0;
}

static int pcore_relation_same_element_id(dom_element *first,
        dom_element *second)
{
    dom_string *first_id;
    dom_string *second_id;
    int same;

    first_id = NULL;
    second_id = NULL;
    if (pcore_relation_attribute_value(first, "id", &first_id) != 0 ||
            pcore_relation_attribute_value(second, "id", &second_id) !=
            0) {
        if (first_id != NULL) {
            dom_string_unref(first_id);
        }
        if (second_id != NULL) {
            dom_string_unref(second_id);
        }
        return 0;
    }
    same = dom_string_isequal(first_id, second_id) ? 1 : 0;
    dom_string_unref(first_id);
    dom_string_unref(second_id);
    return same;
}

static int pcore_relation_is_labelable(dom_element *element)
{
    dom_string *type;
    const char *data;
    int result;

    if (element == NULL) {
        return 0;
    }
    if (pcore_element_name_is(element, "input")) {
        type = NULL;
        result = pcore_relation_attribute_value(element, "type", &type);
        if (result == 1) {
            return 0;
        }
        if (type != NULL) {
            data = (const char *) dom_string_data(type);
            if (data != NULL && strcasecmp(data, "hidden") == 0) {
                dom_string_unref(type);
                return 0;
            }
            dom_string_unref(type);
        }
        return 1;
    }
    return pcore_element_name_is(element, "select") ||
            pcore_element_name_is(element, "textarea") ||
            pcore_element_name_is(element, "button");
}

/* Find the first labelable descendant in tree order. */
static dom_element *pcore_relation_find_labelable_descendant(dom_node *node)
{
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    dom_element *found;

    if (node == NULL) {
        return NULL;
    }
    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return NULL;
    }
    while (child != NULL) {
        next = NULL;
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            return NULL;
        }
        if (type == DOM_ELEMENT_NODE) {
            if (pcore_relation_is_labelable((dom_element *) child)) {
                return (dom_element *) child;
            }
            found = pcore_relation_find_labelable_descendant(child);
            if (found != NULL) {
                dom_node_unref(child);
                return found;
            }
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return NULL;
        }
        dom_node_unref(child);
        child = next;
    }
    return NULL;
}

/* Return a retained labelable control associated with one label. */
static dom_element *pcore_relation_label_control_element(
        dom_document *doc, dom_element *label)
{
    dom_string *for_value;
    const char *for_data;
    dom_element *target;
    dom_element *nested;

    if (doc == NULL || label == NULL ||
            !pcore_element_name_is(label, "label")) {
        return NULL;
    }
    for_value = NULL;
    if (pcore_relation_attribute_value(label, "for", &for_value) == 0) {
        for_data = (const char *) dom_string_data(for_value);
        if (for_data != NULL && for_data[0] != '\0') {
            target = pcore_element_by_id(doc, for_data);
            dom_string_unref(for_value);
            if (target != NULL && pcore_relation_is_labelable(target)) {
                return target;
            }
            if (target != NULL) {
                dom_node_unref((dom_node *) target);
            }
            return NULL;
        }
        dom_string_unref(for_value);
    }

    nested = pcore_relation_find_labelable_descendant((dom_node *) label);
    return nested;
}

static int pcore_relation_label_control(dom_document *doc,
        dom_element *label, char *value, int value_capacity,
        int *out_bytes)
{
    dom_element *target;
    int result;

    target = pcore_relation_label_control_element(doc, label);
    if (target == NULL) {
        return 2;
    }
    result = pcore_relation_copy_element_id((dom_node *) target, value,
            value_capacity,
            out_bytes);
    dom_node_unref((dom_node *) target);
    return result;
}

/* Walk labels in document order.  Only labels with an addressable id are
 * returned to the browser wrapper; an id-less label still participates in
 * label.control resolution but cannot be represented by the ID-based ABI. */
static int pcore_relation_walk_labels(dom_node *node, dom_document *doc,
        dom_element *control, unsigned int wanted, unsigned int *count,
        char *value, int value_capacity, int *out_bytes, int *found)
{
    dom_node *child;
    dom_node *next;
    dom_node_type type;
    dom_element *associated;
    int id_result;
    int err;
    int same;

    if (node == NULL || doc == NULL || control == NULL || count == NULL ||
            found == NULL) {
        return 1;
    }
    child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return 1;
    }
    while (child != NULL) {
        next = NULL;
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        if (type == DOM_ELEMENT_NODE) {
            if (pcore_element_name_is((dom_element *) child, "label")) {
                associated = pcore_relation_label_control_element(doc,
                        (dom_element *) child);
                if (associated != NULL) {
                    same = pcore_relation_same_element_id(associated,
                            control);
                    if (same) {
                        id_result = pcore_relation_copy_element_id(child,
                                NULL, 0, NULL);
                        if (id_result == 0) {
                            if (wanted == (unsigned int) -1 ||
                                    *count == wanted) {
                                if (wanted != (unsigned int) -1) {
                                    id_result =
                                            pcore_relation_copy_element_id(
                                            child, value,
                                            value_capacity, out_bytes);
                                    if (id_result != 0) {
                                        dom_node_unref((dom_node *) associated);
                                        dom_node_unref(child);
                                        return id_result;
                                    }
                                    *found = 1;
                                    dom_node_unref((dom_node *) associated);
                                    dom_node_unref(child);
                                    return 0;
                                }
                            }
                            (*count)++;
                        } else if (id_result == 1) {
                            dom_node_unref((dom_node *) associated);
                            dom_node_unref(child);
                            return 1;
                        }
                    }
                    dom_node_unref((dom_node *) associated);
                }
            }
            err = pcore_relation_walk_labels(child, doc, control, wanted,
                    count, value, value_capacity, out_bytes, found);
            if (err != 0 || *found) {
                dom_node_unref(child);
                return err;
            }
        }
        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        dom_node_unref(child);
        child = next;
    }
    return 0;
}

static int pcore_relation_control_labels(dom_document *doc,
        dom_element *control, unsigned int wanted, char *value,
        int value_capacity, int *out_bytes, int *out_count)
{
    dom_element *root;
    unsigned int count;
    int found;
    int err;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (doc == NULL || control == NULL ||
            !pcore_relation_is_labelable(control)) {
        return 2;
    }
    root = NULL;
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        return 1;
    }
    count = 0;
    found = 0;
    err = pcore_relation_walk_labels((dom_node *) root, doc, control,
            wanted, &count, value, value_capacity, out_bytes, &found);
    dom_node_unref((dom_node *) root);
    if (err != 0) {
        return err;
    }
    if (wanted != (unsigned int) -1 && !found) {
        return 2;
    }
    if (out_count != NULL) {
        *out_count = (int) count;
    }
    return 0;
}

static int pcore_relation_attribute_count(dom_element *element,
        int *out_count)
{
    dom_namednodemap *attributes;
    dom_ulong length;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (element == NULL || out_count == NULL) {
        return 1;
    }
    attributes = NULL;
    length = 0;
    if (dom_node_get_attributes((dom_node *) element, &attributes) !=
            DOM_NO_ERR) {
        return 1;
    }
    if (attributes == NULL) {
        return 0;
    }
    if (dom_namednodemap_get_length(attributes, &length) != DOM_NO_ERR) {
        dom_namednodemap_unref(attributes);
        return 1;
    }
    dom_namednodemap_unref(attributes);
    if (length > (dom_ulong) 2147483647UL) {
        return 1;
    }
    *out_count = (int) length;
    return 0;
}

static int pcore_relation_attribute_field(dom_element *element,
        unsigned int index, int want_value, char *value,
        int value_capacity, int *out_bytes)
{
    dom_namednodemap *attributes;
    dom_node *attribute;
    dom_string *text;
    dom_ulong length;
    dom_exception err;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (value != NULL && value_capacity > 0) {
        value[0] = '\0';
    }
    if (element == NULL) {
        return 1;
    }
    attributes = NULL;
    attribute = NULL;
    text = NULL;
    length = 0;
    if (dom_node_get_attributes((dom_node *) element, &attributes) !=
            DOM_NO_ERR || attributes == NULL ||
            dom_namednodemap_get_length(attributes, &length) != DOM_NO_ERR) {
        if (attributes != NULL) {
            dom_namednodemap_unref(attributes);
        }
        return 1;
    }
    if ((dom_ulong) index >= length ||
            dom_namednodemap_item(attributes, (dom_ulong) index,
            &attribute) != DOM_NO_ERR || attribute == NULL) {
        dom_namednodemap_unref(attributes);
        return 2;
    }
    if (want_value) {
        err = dom_node_get_text_content(attribute, &text);
    } else {
        err = dom_node_get_node_name(attribute, &text);
    }
    if (err != DOM_NO_ERR || text == NULL) {
        if (text != NULL) {
            dom_string_unref(text);
        }
        dom_node_unref(attribute);
        dom_namednodemap_unref(attributes);
        return 1;
    }
    pcore_copy_dom_string(text, value, value_capacity, out_bytes);
    dom_string_unref(text);
    dom_node_unref(attribute);
    dom_namednodemap_unref(attributes);
    return 0;
}

/* Return one direct child without filtering out text, comment, or
 * id-less-element nodes. The caller owns the returned reference. */
static int pcore_relation_child_node_at(dom_node *node, unsigned int index,
        dom_node **out_node)
{
    dom_nodelist *children;
    dom_ulong length;
    dom_node *child;

    if (out_node != NULL) {
        *out_node = NULL;
    }
    if (node == NULL || out_node == NULL) {
        return 1;
    }
    children = NULL;
    length = 0;
    child = NULL;
    if (dom_node_get_child_nodes(node, &children) != DOM_NO_ERR ||
            children == NULL || dom_nodelist_get_length(children,
            &length) != DOM_NO_ERR) {
        if (children != NULL) {
            dom_nodelist_unref(children);
        }
        return 1;
    }
    if ((dom_ulong) index >= length || dom_nodelist_item(children,
            (dom_ulong) index, &child) != DOM_NO_ERR || child == NULL) {
        dom_nodelist_unref(children);
        return 2;
    }
    dom_nodelist_unref(children);
    *out_node = child;
    return 0;
}

static int pcore_relation_child_node_count(dom_node *node, int *out_count)
{
    dom_nodelist *children;
    dom_ulong length;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (node == NULL || out_count == NULL) {
        return 1;
    }
    children = NULL;
    length = 0;
    if (dom_node_get_child_nodes(node, &children) != DOM_NO_ERR ||
            children == NULL || dom_nodelist_get_length(children,
            &length) != DOM_NO_ERR) {
        if (children != NULL) {
            dom_nodelist_unref(children);
        }
        return 1;
    }
    dom_nodelist_unref(children);
    if (length > (dom_ulong) 2147483647UL) {
        return 1;
    }
    *out_count = (int) length;
    return 0;
}

static int pcore_relation_child_node_type(dom_node *node,
        unsigned int index, int *out_type)
{
    dom_node *child;
    dom_node_type type;
    int err;

    if (out_type != NULL) {
        *out_type = 0;
    }
    child = NULL;
    err = pcore_relation_child_node_at(node, index, &child);
    if (err != 0) {
        return err;
    }
    if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
        dom_node_unref(child);
        return 1;
    }
    dom_node_unref(child);
    if (out_type != NULL) {
        *out_type = (int) type;
    }
    return 0;
}

/* field: 0 = nodeName, 1 = nodeValue, 2 = element id, 3 = textContent. */
static int pcore_relation_child_node_field(dom_node *node,
        unsigned int index, int field, char *value, int value_capacity,
        int *out_bytes)
{
    dom_node *child;
    dom_node_type type;
    dom_string *text;
    dom_exception err;
    int result;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (value != NULL && value_capacity > 0) {
        value[0] = '\0';
    }
    if (field < 0 || field > 3) {
        return 1;
    }
    child = NULL;
    result = pcore_relation_child_node_at(node, index, &child);
    if (result != 0) {
        return result;
    }
    if (field == 2) {
        if (dom_node_get_node_type(child, &type) != DOM_NO_ERR) {
            dom_node_unref(child);
            return 1;
        }
        if (type != DOM_ELEMENT_NODE) {
            dom_node_unref(child);
            return 2;
        }
        result = pcore_relation_copy_element_id(child, value,
                value_capacity, out_bytes);
        dom_node_unref(child);
        return result;
    }
    text = NULL;
    if (field == 0) {
        err = dom_node_get_node_name(child, &text);
    } else if (field == 1) {
        err = dom_node_get_node_value(child, &text);
    } else {
        err = dom_node_get_text_content(child, &text);
    }
    if (err != DOM_NO_ERR || text == NULL) {
        if (text != NULL) {
            dom_string_unref(text);
        }
        dom_node_unref(child);
        return (err == DOM_NO_ERR) ? 2 : 1;
    }
    pcore_copy_dom_string(text, value, value_capacity, out_bytes);
    dom_string_unref(text);
    dom_node_unref(child);
    return 0;
}

PCORE_API int PCore_NodeRelationById(HANDLE hDoc, const char *element_id,
        unsigned int relation, unsigned int index, char *out_value,
        int value_capacity, int *out_bytes, int *out_number)
{
    dom_element *element;
    int count;
    int found;
    int err;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (out_number != NULL) {
        *out_number = 0;
    }
    if (out_value != NULL && value_capacity > 0) {
        out_value[0] = '\0';
    }
    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0' ||
            (out_value == NULL && value_capacity > 0) || value_capacity < 0) {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 2;
    }
    err = 1;
    count = 0;
    found = 0;
    switch (relation) {
    case PCORE_NODE_RELATION_PARENT_ELEMENT:
        err = pcore_relation_parent((dom_node *) element, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_FIRST_CHILD:
        err = pcore_relation_child_at((dom_node *) element, index, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_LAST_CHILD:
        if (pcore_relation_child_count((dom_node *) element, &count) == 0 &&
                count > 0) {
            err = pcore_relation_child_at((dom_node *) element,
                    (unsigned int) (count - 1), out_value, value_capacity,
                    out_bytes);
        } else {
            err = count == 0 ? 2 : 1;
        }
        break;
    case PCORE_NODE_RELATION_PREVIOUS_SIBLING:
        err = pcore_relation_sibling((dom_node *) element, 1, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_NEXT_SIBLING:
        err = pcore_relation_sibling((dom_node *) element, 0, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_CHILD_COUNT:
        err = pcore_relation_child_count((dom_node *) element, &count);
        if (err == 0 && out_number != NULL) {
            *out_number = count;
        }
        break;
    case PCORE_NODE_RELATION_TAG_NAME:
        err = pcore_relation_copy_node_name((dom_node *) element, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_FORM_OWNER:
        err = pcore_relation_form_owner((dom_node *) element, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_LABEL_CONTROL:
        err = pcore_relation_label_control((dom_document *) hDoc, element,
                out_value, value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_CONTROL_LABEL_COUNT:
        err = pcore_relation_control_labels((dom_document *) hDoc, element,
                (unsigned int) -1, NULL, 0, NULL, &count);
        if (err == 0 && out_number != NULL) {
            *out_number = count;
        }
        break;
    case PCORE_NODE_RELATION_CONTROL_LABEL_AT:
        err = pcore_relation_control_labels((dom_document *) hDoc, element,
                index, out_value, value_capacity, out_bytes, NULL);
        break;
    case PCORE_NODE_RELATION_FORM_CONTROL_COUNT:
        if (!pcore_element_name_is(element, "form")) {
            err = 0;
            count = 0;
        } else {
            err = pcore_relation_walk_controls((dom_node *) element, element,
                    (unsigned int) -1, &count, NULL, 0, NULL, &found);
        }
        if (err == 0 && out_number != NULL) {
            *out_number = count;
        }
        break;
    case PCORE_NODE_RELATION_FORM_CONTROL_AT:
        if (!pcore_element_name_is(element, "form")) {
            err = 2;
        } else {
            err = pcore_relation_walk_controls((dom_node *) element, element,
                    index, &count, out_value, value_capacity, out_bytes,
                    &found);
            if (err == 0 && !found) {
                err = 2;
            }
        }
        break;
    case PCORE_NODE_RELATION_ATTRIBUTE_COUNT:
        err = pcore_relation_attribute_count(element, &count);
        if (err == 0 && out_number != NULL) {
            *out_number = count;
        }
        break;
    case PCORE_NODE_RELATION_ATTRIBUTE_NAME_AT:
        err = pcore_relation_attribute_field(element, index, 0, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_ATTRIBUTE_VALUE_AT:
        err = pcore_relation_attribute_field(element, index, 1, out_value,
                value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_CHILD_NODE_COUNT:
        err = pcore_relation_child_node_count((dom_node *) element, &count);
        if (err == 0 && out_number != NULL) {
            *out_number = count;
        }
        break;
    case PCORE_NODE_RELATION_CHILD_NODE_TYPE_AT:
        err = pcore_relation_child_node_type((dom_node *) element, index,
                &count);
        if (err == 0 && out_number != NULL) {
            *out_number = count;
        }
        break;
    case PCORE_NODE_RELATION_CHILD_NODE_NAME_AT:
        err = pcore_relation_child_node_field((dom_node *) element, index,
                0, out_value, value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_CHILD_NODE_VALUE_AT:
        err = pcore_relation_child_node_field((dom_node *) element, index,
                1, out_value, value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_CHILD_NODE_ID_AT:
        err = pcore_relation_child_node_field((dom_node *) element, index,
                2, out_value, value_capacity, out_bytes);
        break;
    case PCORE_NODE_RELATION_CHILD_NODE_TEXT_AT:
        err = pcore_relation_child_node_field((dom_node *) element, index,
                3, out_value, value_capacity, out_bytes);
        break;
    default:
        err = 1;
        break;
    }
    dom_node_unref((dom_node *) element);
    return err;
}

/* libdom 0.4.2 stores input.value in the value attribute. Freeze the parsed
 * default before the first live-value write so reset keeps browser semantics. */
static int pcore_dom_input_preserve_default(
        dom_html_input_element *input)
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
    if (current_value != NULL) { dom_string_unref(current_value); }
    if (empty != NULL) { dom_string_unref(empty); }
    return (error == DOM_NO_ERR) ? 1 : 0;
}

PCORE_API int PCore_NodeExistsById(HANDLE hDoc, const char *element_id)
{
    dom_element *element;

    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 0;
    }
    dom_node_unref((dom_node *) element);
    return 1;
}

PCORE_API int PCore_NodeTextContentById(HANDLE hDoc, const char *element_id,
        char *text, int text_capacity, int *out_bytes)
{
    dom_element *element;
    dom_string *content;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (text != NULL && text_capacity > 0) {
        text[0] = '\0';
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    content = NULL;
    if (dom_node_get_text_content((dom_node *) element, &content) !=
            DOM_NO_ERR || content == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    pcore_copy_dom_string(content, text, text_capacity, out_bytes);
    dom_string_unref(content);
    dom_node_unref((dom_node *) element);
    return 0;
}

/* Resolve the enumerated contenteditable attribute while retaining the
 * browser's inheritance rule. The input node is owned by the caller and is
 * consumed by this helper; every parent returned by libdom is released before
 * the result is returned. Unknown values inherit from the parent. */
static int pcore_contenteditable_mode_take(dom_node *node, int *out_mode)
{
    dom_string *name;
    dom_string *value;
    dom_node *parent;
    dom_node_type node_type;
    const char *data;
    const char *token;
    size_t length;
    size_t start;
    size_t end;
    size_t token_length;
    size_t i;
    int mode;
    int recognized;

    if (out_mode != NULL) {
        *out_mode = PCORE_CONTENTEDITABLE_MODE_NONE;
    }
    if (node == NULL || out_mode == NULL) {
        if (node != NULL) {
            dom_node_unref(node);
        }
        return 1;
    }
    name = NULL;
    if (dom_string_create((const uint8_t *) "contenteditable", 15,
            &name) != DOM_NO_ERR || name == NULL) {
        dom_node_unref(node);
        return 1;
    }
    mode = PCORE_CONTENTEDITABLE_MODE_NONE;
    while (node != NULL) {
        if (dom_node_get_node_type(node, &node_type) != DOM_NO_ERR) {
            dom_string_unref(name);
            dom_node_unref(node);
            return 1;
        }
        if (node_type != DOM_ELEMENT_NODE) {
            dom_node_unref(node);
            break;
        }
        value = NULL;
        if (dom_element_get_attribute((dom_element *) node, name, &value) !=
                DOM_NO_ERR) {
            dom_string_unref(name);
            dom_node_unref(node);
            return 1;
        }
        recognized = 0;
        if (value != NULL) {
            data = dom_string_data(value);
            length = dom_string_byte_length(value);
            if (data == NULL && length != 0) {
                dom_string_unref(value);
                dom_string_unref(name);
                dom_node_unref(node);
                return 1;
            }
            start = 0;
            end = length;
            while (start < end && (data[start] == ' ' ||
                    data[start] == '\t' || data[start] == '\r' ||
                    data[start] == '\n' || data[start] == '\f')) {
                start++;
            }
            while (end > start && (data[end - 1] == ' ' ||
                    data[end - 1] == '\t' || data[end - 1] == '\r' ||
                    data[end - 1] == '\n' || data[end - 1] == '\f')) {
                end--;
            }
            token = NULL;
            if (start == end) {
                recognized = 1;
                mode = PCORE_CONTENTEDITABLE_MODE_TEXT;
            } else if (end - start == 4) {
                token = "true";
            } else if (end - start == 5) {
                token = "false";
            } else if (end - start == 14) {
                token = "plaintext-only";
            }
            if (!recognized && token != NULL) {
                token_length = strlen(token);
                if (token_length == end - start) {
                    recognized = 1;
                    for (i = 0; i < token_length; i++) {
                        char a;
                        char b;

                        a = data[start + i];
                        b = token[i];
                        if (a >= 'A' && a <= 'Z') {
                            a = (char) (a + ('a' - 'A'));
                        }
                        if (a != b) {
                            recognized = 0;
                            break;
                        }
                    }
                    if (recognized) {
                        mode = (token[0] == 'f') ?
                                PCORE_CONTENTEDITABLE_MODE_NONE :
                                (token[0] == 'p' ?
                                PCORE_CONTENTEDITABLE_MODE_PLAINTEXT_ONLY :
                                PCORE_CONTENTEDITABLE_MODE_TEXT);
                    }
                }
            }
            dom_string_unref(value);
        }
        if (recognized) {
            dom_string_unref(name);
            dom_node_unref(node);
            *out_mode = mode;
            return 0;
        }
        parent = NULL;
        if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR) {
            dom_string_unref(name);
            dom_node_unref(node);
            return 1;
        }
        dom_node_unref(node);
        node = parent;
    }
    dom_string_unref(name);
    *out_mode = PCORE_CONTENTEDITABLE_MODE_NONE;
    return 0;
}

PCORE_API int PCore_ContentEditableInfoById(HANDLE hDoc,
        const char *element_id, PCoreContentEditableInfo *out_info)
{
    dom_element *element;
    dom_string *content;
    int mode;
    size_t bytes;

    if (out_info == NULL || out_info->size < sizeof(*out_info) ||
            hDoc == NULL || element_id == NULL || element_id[0] == '\0') {
        return 1;
    }
    out_info->editable = 0;
    out_info->mode = PCORE_CONTENTEDITABLE_MODE_NONE;
    out_info->text_bytes = 0;
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    content = NULL;
    if (dom_node_get_text_content((dom_node *) element, &content) !=
            DOM_NO_ERR || content == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    bytes = dom_string_byte_length(content);
    dom_string_unref(content);
    if (bytes > (size_t) INT_MAX ||
            pcore_contenteditable_mode_take((dom_node *) element, &mode) !=
            0) {
        return 1;
    }
    out_info->mode = mode;
    out_info->editable = mode != PCORE_CONTENTEDITABLE_MODE_NONE ? 1 : 0;
    out_info->text_bytes = (int) bytes;
    return 0;
}

PCORE_API int PCore_ContentEditableSetTextById(HANDLE hDoc,
        const char *element_id, const char *text)
{
    dom_element *element;
    dom_string *content;
    int mode;
    size_t bytes;
    dom_exception error;

    if (hDoc == NULL || element_id == NULL || element_id[0] == '\0' ||
            text == NULL) {
        return 1;
    }
    bytes = strlen(text);
    if (bytes > PCORE_CONTENTEDITABLE_TEXT_MAX_BYTES) {
        return 3;
    }
    /* The core is UTF-8 at its public boundary. Keep malformed input from
     * entering the DOM even though libdom itself stores byte strings. */
    if (!pcore_contenteditable_utf8_valid(text)) {
        return 3;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    if (pcore_contenteditable_mode_take((dom_node *) element, &mode) != 0) {
        return 1;
    }
    if (mode == PCORE_CONTENTEDITABLE_MODE_NONE) {
        return 2;
    }
    content = NULL;
    if (dom_string_create((const uint8_t *) text, bytes, &content) !=
            DOM_NO_ERR || content == NULL) {
        return 1;
    }
    error = dom_node_set_text_content((dom_node *) element, content);
    dom_string_unref(content);
    return (error == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeSetTextContentById(HANDLE hDoc,
        const char *element_id, const char *text)
{
    dom_element *element;
    dom_string *content;
    dom_exception err;

    if (text == NULL) {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    content = NULL;
    if (dom_string_create((const uint8_t *) text, strlen(text), &content) !=
            DOM_NO_ERR || content == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    err = dom_node_set_text_content((dom_node *) element, content);
    dom_string_unref(content);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeAttributeById(HANDLE hDoc, const char *element_id,
        const char *name, char *value, int value_capacity, int *out_bytes)
{
    dom_element *element;
    dom_string *dom_name;
    dom_string *dom_value;
    dom_exception err;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (value != NULL && value_capacity > 0) {
        value[0] = '\0';
    }
    if (name == NULL || name[0] == '\0') {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    dom_name = NULL;
    dom_value = NULL;
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &dom_name) != DOM_NO_ERR || dom_name == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    err = dom_element_get_attribute(element, dom_name, &dom_value);
    dom_string_unref(dom_name);
    if (err != DOM_NO_ERR) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    if (dom_value == NULL) {
        dom_node_unref((dom_node *) element);
        return 2;
    }
    pcore_copy_dom_string(dom_value, value, value_capacity, out_bytes);
    dom_string_unref(dom_value);
    dom_node_unref((dom_node *) element);
    return 0;
}

PCORE_API int PCore_NodeSetAttributeById(HANDLE hDoc,
        const char *element_id, const char *name, const char *value)
{
    dom_element *element;
    dom_string *dom_name;
    dom_string *dom_value;
    dom_exception err;

    if (name == NULL || name[0] == '\0' || value == NULL) {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    dom_name = NULL;
    dom_value = NULL;
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &dom_name) != DOM_NO_ERR || dom_name == NULL ||
            dom_string_create((const uint8_t *) value, strlen(value),
            &dom_value) != DOM_NO_ERR || dom_value == NULL) {
        if (dom_name != NULL) { dom_string_unref(dom_name); }
        if (dom_value != NULL) { dom_string_unref(dom_value); }
        dom_node_unref((dom_node *) element);
        return 1;
    }
    err = dom_element_set_attribute(element, dom_name, dom_value);
    dom_string_unref(dom_value);
    dom_string_unref(dom_name);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeRemoveAttributeById(HANDLE hDoc,
        const char *element_id, const char *name)
{
    dom_element *element;
    dom_string *dom_name;
    dom_exception err;

    if (name == NULL || name[0] == '\0') {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    dom_name = NULL;
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &dom_name) != DOM_NO_ERR || dom_name == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    err = dom_element_remove_attribute(element, dom_name);
    dom_string_unref(dom_name);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeValueById(HANDLE hDoc, const char *element_id,
        char *value, int value_capacity, int *out_bytes)
{
    dom_element *element;
    dom_string *dom_value;
    dom_exception err;

    if (out_bytes != NULL) { *out_bytes = 0; }
    if (value != NULL && value_capacity > 0) { value[0] = '\0'; }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    dom_value = NULL;
    if (pcore_element_name_is(element, "input")) {
        err = dom_html_input_element_get_value(
                (dom_html_input_element *) element, &dom_value);
    } else if (pcore_element_name_is(element, "textarea")) {
        err = dom_html_text_area_element_get_value(
                (dom_html_text_area_element *) element, &dom_value);
    } else if (pcore_element_name_is(element, "select")) {
        err = dom_html_select_element_get_value(
                (dom_html_select_element *) element, &dom_value);
    } else {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    if (err != DOM_NO_ERR) {
        if (dom_value != NULL) { dom_string_unref(dom_value); }
        dom_node_unref((dom_node *) element);
        return 1;
    }
    pcore_copy_dom_string(dom_value, value, value_capacity, out_bytes);
    if (dom_value != NULL) { dom_string_unref(dom_value); }
    dom_node_unref((dom_node *) element);
    return 0;
}

PCORE_API int PCore_NodeSetValueById(HANDLE hDoc, const char *element_id,
        const char *value)
{
    dom_element *element;
    dom_string *dom_value;
    dom_exception err;
    char *normalised;
    size_t source;
    size_t target;
    size_t length;

    if (value == NULL) {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    normalised = NULL;
    length = strlen(value);
    if (pcore_element_name_is(element, "textarea")) {
        normalised = (char *) malloc(length + 1);
        if (normalised == NULL) {
            dom_node_unref((dom_node *) element);
            return 1;
        }
        source = 0;
        target = 0;
        while (source < length) {
            if (value[source] == '\r') {
                normalised[target++] = '\n';
                source++;
                if (source < length && value[source] == '\n') {
                    source++;
                }
            } else {
                normalised[target++] = value[source++];
            }
        }
        normalised[target] = '\0';
        value = normalised;
        length = target;
    }
    dom_value = NULL;
    if (dom_string_create((const uint8_t *) value, length,
            &dom_value) != DOM_NO_ERR || dom_value == NULL) {
        free(normalised);
        dom_node_unref((dom_node *) element);
        return 1;
    }
    if (pcore_element_name_is(element, "input")) {
        if (!pcore_dom_input_preserve_default(
                (dom_html_input_element *) element)) {
            err = DOM_NO_MEM_ERR;
        } else {
            err = dom_html_input_element_set_value(
                    (dom_html_input_element *) element, dom_value);
        }
    } else if (pcore_element_name_is(element, "textarea")) {
        err = dom_html_text_area_element_set_value(
                (dom_html_text_area_element *) element, dom_value);
    } else if (pcore_element_name_is(element, "select")) {
        err = dom_html_select_element_set_value(
                (dom_html_select_element *) element, dom_value);
    } else {
        err = DOM_NOT_SUPPORTED_ERR;
    }
    dom_string_unref(dom_value);
    free(normalised);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeDefaultValueById(HANDLE hDoc,
        const char *element_id, char *value, int value_capacity,
        int *out_bytes)
{
    dom_element *element;
    dom_string *dom_value;
    dom_exception err;

    if (out_bytes != NULL) { *out_bytes = 0; }
    if (value != NULL && value_capacity > 0) { value[0] = '\0'; }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    dom_value = NULL;
    if (pcore_element_name_is(element, "input")) {
        err = dom_html_input_element_get_default_value(
                (dom_html_input_element *) element, &dom_value);
    } else if (pcore_element_name_is(element, "textarea")) {
        err = dom_html_text_area_element_get_default_value(
                (dom_html_text_area_element *) element, &dom_value);
    } else {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    if (err != DOM_NO_ERR) {
        if (dom_value != NULL) { dom_string_unref(dom_value); }
        dom_node_unref((dom_node *) element);
        return 1;
    }
    pcore_copy_dom_string(dom_value, value, value_capacity, out_bytes);
    if (dom_value != NULL) { dom_string_unref(dom_value); }
    dom_node_unref((dom_node *) element);
    return 0;
}

PCORE_API int PCore_NodeSetDefaultValueById(HANDLE hDoc,
        const char *element_id, const char *value)
{
    dom_element *element;
    dom_string *dom_value;
    dom_exception err;

    if (value == NULL) {
        return 1;
    }
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL) {
        return 1;
    }
    dom_value = NULL;
    if (dom_string_create((const uint8_t *) value, strlen(value),
            &dom_value) != DOM_NO_ERR || dom_value == NULL) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    if (pcore_element_name_is(element, "input")) {
        err = dom_html_input_element_set_default_value(
                (dom_html_input_element *) element, dom_value);
    } else if (pcore_element_name_is(element, "textarea")) {
        err = dom_html_text_area_element_set_default_value(
                (dom_html_text_area_element *) element, dom_value);
    } else {
        err = DOM_NOT_SUPPORTED_ERR;
    }
    dom_string_unref(dom_value);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeCheckedById(HANDLE hDoc, const char *element_id,
        int *out_checked)
{
    dom_element *element;
    bool checked;
    dom_exception err;

    if (out_checked == NULL) {
        return 1;
    }
    *out_checked = 0;
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_element_name_is(element, "input")) {
        if (element != NULL) { dom_node_unref((dom_node *) element); }
        return 1;
    }
    checked = false;
    err = dom_html_input_element_get_checked(
            (dom_html_input_element *) element, &checked);
    dom_node_unref((dom_node *) element);
    if (err != DOM_NO_ERR) {
        return 1;
    }
    *out_checked = checked ? 1 : 0;
    return 0;
}

PCORE_API int PCore_NodeSetCheckedById(HANDLE hDoc,
        const char *element_id, int checked)
{
    dom_element *element;
    dom_html_input_element *input;
    bool default_checked;
    dom_exception err;

    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_element_name_is(element, "input")) {
        if (element != NULL) { dom_node_unref((dom_node *) element); }
        return 1;
    }
    input = (dom_html_input_element *) element;
    default_checked = false;
    if (dom_html_input_element_get_default_checked(input,
            &default_checked) != DOM_NO_ERR ||
            dom_html_input_element_set_default_checked(input,
                    default_checked) != DOM_NO_ERR) {
        dom_node_unref((dom_node *) element);
        return 1;
    }
    err = dom_html_input_element_set_checked(input,
            checked ? true : false);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeDefaultCheckedById(HANDLE hDoc,
        const char *element_id, int *out_checked)
{
    dom_element *element;
    bool checked;
    dom_exception err;

    if (out_checked == NULL) {
        return 1;
    }
    *out_checked = 0;
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_element_name_is(element, "input")) {
        if (element != NULL) { dom_node_unref((dom_node *) element); }
        return 1;
    }
    checked = false;
    err = dom_html_input_element_get_default_checked(
            (dom_html_input_element *) element, &checked);
    dom_node_unref((dom_node *) element);
    if (err != DOM_NO_ERR) {
        return 1;
    }
    *out_checked = checked ? 1 : 0;
    return 0;
}

PCORE_API int PCore_NodeSetDefaultCheckedById(HANDLE hDoc,
        const char *element_id, int checked)
{
    dom_element *element;
    dom_exception err;

    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_element_name_is(element, "input")) {
        if (element != NULL) { dom_node_unref((dom_node *) element); }
        return 1;
    }
    err = dom_html_input_element_set_default_checked(
            (dom_html_input_element *) element, checked ? true : false);
    dom_node_unref((dom_node *) element);
    return (err == DOM_NO_ERR) ? 0 : 1;
}

PCORE_API int PCore_NodeSelectedIndexById(HANDLE hDoc,
        const char *element_id, int *out_index)
{
    dom_element *element;
    int32_t index;
    dom_exception err;

    if (out_index == NULL) {
        return 1;
    }
    *out_index = -1;
    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_element_name_is(element, "select")) {
        if (element != NULL) { dom_node_unref((dom_node *) element); }
        return 1;
    }
    index = -1;
    err = dom_html_select_element_get_selected_index(
            (dom_html_select_element *) element, &index);
    dom_node_unref((dom_node *) element);
    if (err != DOM_NO_ERR) {
        return 1;
    }
    *out_index = (int) index;
    return 0;
}

static int pcore_select_set_selected_index_dom(
        dom_html_select_element *select, int index)
{
    dom_html_options_collection *options;
    dom_node *node;
    dom_exception err;
    uint32_t length;
    uint32_t i;
    int ok;

    options = NULL;
    if (dom_html_select_element_get_options(select, &options) !=
            DOM_NO_ERR || options == NULL) {
        return 1;
    }
    length = 0;
    if (dom_html_options_collection_get_length(options, &length) !=
            DOM_NO_ERR || (index < -1 ||
            (index >= 0 && (uint32_t) index >= length))) {
        dom_html_options_collection_unref(options);
        return 1;
    }
    ok = 1;
    for (i = 0; i < length; i++) {
        node = NULL;
        err = dom_html_options_collection_item(options, i, &node);
        if (err != DOM_NO_ERR || node == NULL) {
            ok = 0;
            break;
        }
        err = dom_html_option_element_set_selected(
                (dom_html_option_element *) node,
                index >= 0 && i == (uint32_t) index);
        dom_node_unref(node);
        if (err != DOM_NO_ERR) {
            ok = 0;
            break;
        }
    }
    dom_html_options_collection_unref(options);
    return ok ? 0 : 1;
}

PCORE_API int PCore_NodeSetSelectedIndexById(HANDLE hDoc,
        const char *element_id, int index)
{
    dom_element *element;
    int result;

    element = pcore_element_by_id((dom_document *) hDoc, element_id);
    if (element == NULL || !pcore_element_name_is(element, "select")) {
        if (element != NULL) { dom_node_unref((dom_node *) element); }
        return 1;
    }
    result = pcore_select_set_selected_index_dom(
            (dom_html_select_element *) element, index);
    dom_node_unref((dom_node *) element);
    return result;
}

PCORE_API int PCore_NodeComputedColor(HANDLE hDoc, const char *tag,
        unsigned long *out_argb)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;
    dom_node     *elem = NULL;
    lwc_string   *want = NULL;
    void         *style = NULL;
    css_color     color;
    int           rc = 1;

    if (doc == NULL || tag == NULL || out_argb == NULL) {
        return 1;
    }
    if (pcore_style_key == NULL) {
        return 1;   /* PCore_StyleDocument has not run */
    }
    if (lwc_intern_string(tag, strlen(tag), &want) != lwc_error_ok) {
        return 1;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }
    elem = find_named(root, want);
    if (elem == NULL) {
        goto cleanup;
    }
    if (dom_node_get_user_data(elem, pcore_style_key, &style) != DOM_NO_ERR ||
            style == NULL) {
        goto cleanup;
    }

    (void) css_computed_color((css_computed_style *) style, &color);
    *out_argb = (unsigned long) color;
    rc = 0;

cleanup:
    if (elem != NULL) {
        dom_node_unref(elem);
    }
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (want != NULL) {
        lwc_string_unref(want);
    }
    return rc;
}
