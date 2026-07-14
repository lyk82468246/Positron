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
 * machinery than this first cut provides - dynamic pseudo-classes (:hover,
 * :visited, ...) - are stubbed to "no match"; type / class / id / attribute /
 * static pseudo-classes / descendant / sibling structure all work.
 *
 * C89 only.
 */

#include <windows.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>   /* malloc / free (box geometry) */

#include <dom/dom.h>

#include <libcss/libcss.h>
#include <libcss/fpmath.h>
#include <libwapcaplet/libwapcaplet.h>

#include "positron_core.h"
#include "pcore_internal.h"

/* Client data threaded through css_select_style into the handler. */
typedef struct pcore_select_pw {
    lwc_string *universal;   /* interned "*", for node_has_name */
} pcore_select_pw;

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
    if (css_width > 0) {
        pcore_unit_ctx.viewport_width = css_width * (1 << CSS_RADIX_POINT);
    }
    if (css_height > 0) {
        pcore_unit_ctx.viewport_height = css_height * (1 << CSS_RADIX_POINT);
    }
    if (dpi > 0) {
        pcore_unit_ctx.device_dpi = dpi * (1 << CSS_RADIX_POINT);
    }
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

    (void) pw;
    *ancestor = NULL;

    /* take our own ref so the unref dance below is balanced */
    n = dom_node_ref(n);

    while (n != NULL) {
        dom_node *parent;
        dom_node_type type;
        dom_string *name;
        bool match;

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

        err = dom_node_get_node_name(n, &name);
        if (err != DOM_NO_ERR) {
            continue;
        }
        match = dom_string_caseless_lwc_isequal(name, qname->name);
        dom_string_unref(name);
        if (match) {
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
    dom_string *name;
    dom_exception err;
    bool match;

    (void) pw;
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

    err = dom_node_get_node_name(p, &name);
    if (err != DOM_NO_ERR) {
        dom_node_unref(p);
        return CSS_OK;
    }
    match = dom_string_caseless_lwc_isequal(name, qname->name);
    dom_string_unref(name);

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
{ (void) pw; (void) node; *match = false; return CSS_OK; }
static css_error node_is_active(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
static css_error node_is_focus(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
static css_error node_is_enabled(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
static css_error node_is_disabled(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
static css_error node_is_checked(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
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

static css_error set_libcss_node_data(void *pw, void *node,
        void *libcss_node_data)
{
    /* We do not cache per-node data; ensure libcss frees what it handed us. */
    css_libcss_node_data_handler(&pcore_select_handler, CSS_NODE_DELETED,
            pw, node, NULL, libcss_node_data);
    return CSS_OK;
}

static css_error get_libcss_node_data(void *pw, void *node,
        void **libcss_node_data)
{
    (void) pw; (void) node;
    *libcss_node_data = NULL;
    return CSS_OK;
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

    pw.universal = NULL;
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
    "li { display: list-item; }\n"
    "b, strong { font-weight: bold; }\n"
    "i, em { font-style: italic; }\n"
    "a { color: #0000ee; text-decoration: underline; }\n"
    "a { color: #0000ee; text-decoration: underline; }\n"
    "pre { font-family: monospace; white-space: pre; margin-bottom: 1em; }\n"
    "body { margin: 8px; }\n"
    "p, blockquote { margin-top: 1em; margin-bottom: 1em; }\n"
    "h1 { font-size: 2em; }\n"
    "h2 { font-size: 1.5em; }\n"
    "h3 { font-size: 1.17em; }\n";

/* libdom user-data key under which each element's css_computed_style hangs. */
static dom_string *pcore_style_key = NULL;

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
static void pcore_style_subtree(css_select_ctx *ctx, pcore_select_pw *pw,
        const css_media *media, dom_node *node,
        const css_computed_style *parent_style)
{
    css_select_results *results = NULL;
    css_computed_style *node_style = NULL;
    css_computed_style *base;
    dom_node *child;
    void *old = NULL;

    if (css_select_style(ctx, node, &pcore_unit_ctx, media, NULL,
            &pcore_select_handler, pw, &results) != CSS_OK ||
            results == NULL) {
        return;
    }

    base = results->styles[CSS_PSEUDO_ELEMENT_NONE];

    if (parent_style != NULL) {
        /* Compose with the parent to fill inherited properties. */
        if (css_computed_style_compose(parent_style, base, &pcore_unit_ctx,
                &node_style) != CSS_OK) {
            css_select_results_destroy(results);
            return;
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
    dom_node_set_user_data(node, pcore_style_key, node_style,
            pcore_style_ud_handler, &old);
    if (old != NULL && old != node_style) {
        css_computed_style_destroy((css_computed_style *) old);
    }

    /* Recurse into element children, passing our computed style as parent. */
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        return;
    }
    while (child != NULL) {
        dom_node_type type;
        dom_node *next;

        if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                type == DOM_ELEMENT_NODE) {
            pcore_style_subtree(ctx, pw, media, child, node_style);
        }

        if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
            dom_node_unref(child);
            return;
        }
        dom_node_unref(child);
        child = next;
    }
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
    dom_string     *style_name; /* interned "style" */
    dom_string     *link_name;  /* interned "link"  */
    dom_string     *rel_name;   /* interned "rel"   */
    dom_string     *href_name;  /* interned "href"  */
    dom_string     *type_name;  /* interned "type" */
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

static int pcore_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static int pcore_ascii_equal_ci(const char *value, size_t value_len,
        const char *expected)
{
    size_t i;
    size_t expected_len;

    while (value_len > 0 && pcore_ascii_space(*value)) {
        value++;
        value_len--;
    }
    while (value_len > 0 && pcore_ascii_space(value[value_len - 1])) {
        value_len--;
    }
    expected_len = strlen(expected);
    if (value_len != expected_len) {
        return 0;
    }
    for (i = 0; i < value_len; i++) {
        char a;
        char b;

        a = value[i];
        b = expected[i];
        if (a >= 'A' && a <= 'Z') { a = (char) (a + ('a' - 'A')); }
        if (b >= 'A' && b <= 'Z') { b = (char) (b + ('a' - 'A')); }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int pcore_rel_has_token(dom_string *rel, const char *token)
{
    const char *data;
    size_t len;
    size_t start;
    size_t end;

    if (rel == NULL) {
        return 0;
    }
    data = dom_string_data(rel);
    len = dom_string_byte_length(rel);
    start = 0;
    while (start < len) {
        while (start < len && pcore_ascii_space(data[start])) {
            start++;
        }
        end = start;
        while (end < len && !pcore_ascii_space(data[end])) {
            end++;
        }
        if (end > start && pcore_ascii_equal_ci(data + start, end - start,
                token)) {
            return 1;
        }
        start = end;
    }
    return 0;
}

static int pcore_element_css_type(dom_node *node, dom_string *type_name)
{
    dom_string *type;
    int is_css;

    type = NULL;
    if (type_name == NULL || dom_element_get_attribute(node, type_name,
            &type) != DOM_NO_ERR || type == NULL) {
        return 1;
    }
    is_css = dom_string_byte_length(type) == 0 ||
            pcore_ascii_equal_ci(dom_string_data(type),
            dom_string_byte_length(type), "text/css");
    dom_string_unref(type);
    return is_css;
}

static int pcore_element_disabled(dom_node *node, dom_string *disabled_name)
{
    bool disabled;

    disabled = false;
    if (disabled_name != NULL) {
        dom_element_has_attribute(node, disabled_name, &disabled);
    }
    return disabled ? 1 : 0;
}

static const char *pcore_element_media(dom_node *node,
        dom_string *media_name, char *buffer, int capacity)
{
    dom_string *media;
    const char *data;
    size_t len;

    media = NULL;
    if (media_name == NULL || dom_element_get_attribute(node, media_name,
            &media) != DOM_NO_ERR || media == NULL) {
        return NULL;
    }
    data = dom_string_data(media);
    len = dom_string_byte_length(media);
    while (len > 0 && pcore_ascii_space(*data)) {
        data++;
        len--;
    }
    while (len > 0 && pcore_ascii_space(data[len - 1])) {
        len--;
    }
    if (len == 0) {
        dom_string_unref(media);
        return NULL;
    }
    if (len >= (size_t) capacity) {
        dom_string_unref(media);
        return "not all";
    }
    memcpy(buffer, data, len);
    buffer[len] = '\0';
    dom_string_unref(media);
    return buffer;
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
 * blocks (text content) and external <link rel="stylesheet" href> sheets
 * (fetched via the embedder callback, if provided). Parsed sheets are appended
 * to cc->ctx and recorded in cc->sheets[] for the caller to free. Letting a
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
            char media_buffer[1024];
            const char *media;

            media = pcore_element_media(node, cc->media_name, media_buffer,
                    sizeof(media_buffer));
            if (!pcore_element_disabled(node, cc->disabled_name) &&
                    pcore_element_css_type(node, cc->type_name) &&
                    dom_node_get_text_content(node, &css) == DOM_NO_ERR &&
                    css != NULL) {
                pcore_add_author_css(cc, dom_string_data(css),
                        (int) dom_string_byte_length(css),
                        (cc->document_url != NULL) ? cc->document_url :
                        "positron:inline-style", media);
                dom_string_unref(css);
            }
            return;   /* don't recurse into a <style>'s text children */
        }

        if (is_link && (cc->cache != NULL || cc->fetch != NULL)) {
            dom_string *rel = NULL;
            dom_string *href = NULL;
            bool is_sheet = false;

            if (dom_element_get_attribute(node, cc->rel_name, &rel) ==
                    DOM_NO_ERR && rel != NULL) {
                is_sheet = pcore_rel_has_token(rel, "stylesheet") &&
                        !pcore_rel_has_token(rel, "alternate");
                dom_string_unref(rel);
            }
            if (is_sheet && !pcore_element_disabled(node,
                    cc->disabled_name) && pcore_element_css_type(node,
                    cc->type_name) &&
                    dom_element_get_attribute(node, cc->href_name, &href) ==
                            DOM_NO_ERR && href != NULL) {
                const char *hu8 = dom_string_data(href);
                size_t hl = dom_string_byte_length(href);
                if (hu8 != NULL && hl > 0) {
                    char reference[1024];
                    char url[2048];
                    const char *data;
                    char *owned;
                    int len;
                    int cl;
                    char media_buffer[1024];
                    const char *media;

                    cl = (hl < sizeof(reference) - 1) ? (int) hl :
                            (int) sizeof(reference) - 1;
                    memcpy(reference, hu8, cl);
                    reference[cl] = '\0';
                    if (pcore_resolve_css_url(cc, cc->document_url,
                            reference, url, (int) sizeof(url)) == 0 &&
                            pcore_get_stylesheet_bytes(cc, url, &data, &len,
                            &owned) == 0) {
                        media = pcore_element_media(node, cc->media_name,
                                media_buffer, sizeof(media_buffer));
                        pcore_add_author_css(cc, data, len, url, media);
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
    int               rc = 1;

    memset(&cc, 0, sizeof(cc));

    if (doc == NULL) {
        return 1;
    }

    pw.universal = NULL;
    if (lwc_intern_string("*", 1, &pw.universal) != lwc_error_ok) {
        return 1;
    }
    if (pcore_ensure_style_key() != 0) {
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
    dom_string_create((const uint8_t *) "type", 4, &cc.type_name);
    dom_string_create((const uint8_t *) "media", 5, &cc.media_name);
    dom_string_create((const uint8_t *) "disabled", 8, &cc.disabled_name);
    if (cc.style_name != NULL) {
        pcore_collect_resources(&cc, root);
    }

    /* Optional extra author sheet supplied by the caller (may be NULL). */
    if (author != NULL) {
        css_select_ctx_append_sheet(ctx, author, CSS_ORIGIN_AUTHOR, NULL);
    }

    pcore_init_screen_media(&media);

    pcore_style_subtree(ctx, &pw, &media, root, NULL);
    rc = 0;

cleanup:
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
    if (cc.type_name != NULL)  { dom_string_unref(cc.type_name); }
    if (cc.media_name != NULL) { dom_string_unref(cc.media_name); }
    if (cc.disabled_name != NULL) {
        dom_string_unref(cc.disabled_name);
    }
    if (pw.universal != NULL) {
        lwc_string_unref(pw.universal);
    }
    return rc;
}

typedef struct pcore_image_resource {
    struct pcore_image_resource *next;
    char *url;
    char *data;
    int len;
} pcore_image_resource;

typedef struct pcore_image_cache {
    pcore_image_resource *head;
} pcore_image_cache;

static dom_string *pcore_image_cache_key = NULL;

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

        style = pcore_node_computed_style(node);
        if (style != NULL &&
                css_computed_background_image(style, &bg_uri) ==
                CSS_BACKGROUND_IMAGE_IMAGE && bg_uri != NULL) {
            pcore_fetch_image_url(ic, lwc_string_data(bg_uri),
                    lwc_string_length(bg_uri));
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
