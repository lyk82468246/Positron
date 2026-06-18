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
 * machinery than this first cut provides - attribute-value matches and the
 * dynamic pseudo-classes (:hover, :link, ...) - are stubbed to "no match";
 * type / class / id / descendant / sibling structure all work.
 *
 * C89 only.
 */

#include <windows.h>
#include <string.h>
#include <stdlib.h>   /* malloc / free (box geometry) */

#include <dom/dom.h>

#include <libcss/libcss.h>
#include <libcss/fpmath.h>
#include <libwapcaplet/libwapcaplet.h>

#include "positron_core.h"

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

/* Previous element sibling, optionally requiring a matching name. */
/* adjacent / general sibling lookup.
 *
 * TEMP STUB: report "no previous sibling". These callbacks walk previous
 * siblings via dom_node_get_previous_sibling - the prime suspect for the
 * css_select_style crash, since get_sharable_node_data calls
 * named_generic_sibling_node first. dom_node_get_parent_node is known good
 * (it runs at css_select_style entry) but get_previous_sibling has never been
 * exercised. Returning NULL makes libcss skip style sharing (an optimisation)
 * and disables adjacent/general-sibling combinators until the libdom call is
 * verified safe. */
static css_error named_sibling_node(void *pw, void *node,
        const css_qname *qname, void **sibling)
{
    (void) pw;
    (void) node;
    (void) qname;
    *sibling = NULL;
    return CSS_OK;
}

static css_error named_generic_sibling_node(void *pw, void *node,
        const css_qname *qname, void **sibling)
{
    (void) pw;
    (void) node;
    (void) qname;
    *sibling = NULL;
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
    (void) node;
    /* TEMP STUB: no previous sibling (see named_sibling_node note). */
    *sibling = NULL;
    return CSS_OK;
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
/* attribute matching - first cut stubs to "no match"                  */
/* (attribute-value selectors come with a later milestone)             */
/* ------------------------------------------------------------------ */

static css_error node_has_attribute(void *pw, void *node,
        const css_qname *qname, bool *match)
{
    (void) pw; (void) node; (void) qname;
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_equal(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    (void) pw; (void) node; (void) qname; (void) value;
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_dashmatch(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    (void) pw; (void) node; (void) qname; (void) value;
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_includes(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    (void) pw; (void) node; (void) qname; (void) value;
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_prefix(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    (void) pw; (void) node; (void) qname; (void) value;
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_suffix(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    (void) pw; (void) node; (void) qname; (void) value;
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_substring(void *pw, void *node,
        const css_qname *qname, lwc_string *value, bool *match)
{
    (void) pw; (void) node; (void) qname; (void) value;
    *match = false;
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
/* dynamic / state pseudo-classes - not modelled yet, all false        */
/* ------------------------------------------------------------------ */

static css_error node_is_link(void *pw, void *node, bool *match)
{ (void) pw; (void) node; *match = false; return CSS_OK; }
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
{ (void) pw; (void) node; (void) lang; *match = false; return CSS_OK; }

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

    memset(&media, 0, sizeof(media));
    media.type = CSS_MEDIA_SCREEN;

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
    " blockquote, pre, table, tr, form, fieldset, address, hr,"
    " header, footer, section, article, nav, aside, main, figure"
    " { display: block; }\n"
    "head, title, meta, link, style, script, base { display: none; }\n"
    "li { display: list-item; }\n"
    "b, strong { font-weight: bold; }\n"
    "i, em { font-style: italic; }\n"
    "a { color: #0000ee; text-decoration: underline; }\n"
    "a { color: #0000ee; text-decoration: underline; }\n"
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

    /* Attach to the node (handler frees it on node deletion). */
    dom_node_set_user_data(node, pcore_style_key, node_style,
            pcore_style_ud_handler, &old);

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

/* Shared state for the resource-collection DFS below. */
typedef struct pcore_collect_ctx {
    css_select_ctx *ctx;
    HANDLE         *sheets;     /* parsed sheet handles, freed by caller */
    int            *n;
    int             max;
    PCoreFetchFn    fetch;      /* embedder fetch for external <link> CSS */
    PCoreFreeFn     freefn;
    void           *pw;
    dom_string     *style_name; /* interned "style" */
    dom_string     *link_name;  /* interned "link"  */
    dom_string     *rel_name;   /* interned "rel"   */
    dom_string     *href_name;  /* interned "href"  */
    dom_string     *css_value;  /* interned "stylesheet" (for rel match) */
} pcore_collect_ctx;

/* Parse `data`/`len` CSS, append to the select context as an author sheet, and
 * record the handle for later cleanup. `url` is informational (base for any
 * @import - not yet followed). */
static void pcore_add_author_css(pcore_collect_ctx *cc, const char *data,
        int len, const char *url)
{
    HANDLE hs;

    if (*cc->n >= cc->max || data == NULL || len <= 0) {
        return;
    }
    hs = PCore_ParseCSS(data, len, url);
    if (hs == NULL) {
        return;
    }
    if (css_select_ctx_append_sheet(cc->ctx, (css_stylesheet *) hs,
            CSS_ORIGIN_AUTHOR, NULL) == CSS_OK) {
        cc->sheets[(*cc->n)++] = hs;
    } else {
        PCore_FreeStylesheet(hs);
    }
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
            if (dom_node_get_text_content(node, &css) == DOM_NO_ERR &&
                    css != NULL) {
                pcore_add_author_css(cc, dom_string_data(css),
                        (int) dom_string_byte_length(css),
                        "positron:inline-style");
                dom_string_unref(css);
            }
            return;   /* don't recurse into a <style>'s text children */
        }

        if (is_link && cc->fetch != NULL) {
            dom_string *rel = NULL;
            dom_string *href = NULL;
            bool is_sheet = false;

            if (dom_element_get_attribute(node, cc->rel_name, &rel) ==
                    DOM_NO_ERR && rel != NULL) {
                is_sheet = dom_string_caseless_isequal(rel, cc->css_value);
                dom_string_unref(rel);
            }
            if (is_sheet &&
                    dom_element_get_attribute(node, cc->href_name, &href) ==
                            DOM_NO_ERR && href != NULL) {
                const char *hu8 = dom_string_data(href);
                size_t hl = dom_string_byte_length(href);
                if (hu8 != NULL && hl > 0) {
                    char  url[1024];
                    char *data = NULL;
                    int   len = 0;
                    int   cl = (hl < sizeof(url) - 1)
                            ? (int) hl : (int) sizeof(url) - 1;
                    memcpy(url, hu8, cl);
                    url[cl] = '\0';
                    if (cc->fetch(cc->pw, url, &data, &len) == 0 &&
                            data != NULL) {
                        pcore_add_author_css(cc, data, len, url);
                        if (cc->freefn != NULL) {
                            cc->freefn(cc->pw, data);
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
    dom_document     *doc = (dom_document *) hDoc;
    css_stylesheet   *author = (css_stylesheet *) hSheet;
    HANDLE            hUA = NULL;
    css_select_ctx   *ctx = NULL;
    dom_node         *root = NULL;
    HANDLE            page_sheets[32];
    int               n_page = 0;
    int               i;
    css_media         media;
    pcore_select_pw   pw;
    pcore_collect_ctx cc;
    int               rc = 1;

    cc.style_name = NULL;
    cc.link_name = NULL;
    cc.rel_name = NULL;
    cc.href_name = NULL;
    cc.css_value = NULL;

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
    cc.max = 32;
    cc.fetch = fetch;
    cc.freefn = freefn;
    cc.pw = pw_fetch;
    dom_string_create((const uint8_t *) "style", 5, &cc.style_name);
    dom_string_create((const uint8_t *) "link", 4, &cc.link_name);
    dom_string_create((const uint8_t *) "rel", 3, &cc.rel_name);
    dom_string_create((const uint8_t *) "href", 4, &cc.href_name);
    dom_string_create((const uint8_t *) "stylesheet", 10, &cc.css_value);
    if (cc.style_name != NULL) {
        pcore_collect_resources(&cc, root);
    }

    /* Optional extra author sheet supplied by the caller (may be NULL). */
    if (author != NULL) {
        css_select_ctx_append_sheet(ctx, author, CSS_ORIGIN_AUTHOR, NULL);
    }

    memset(&media, 0, sizeof(media));
    media.type = CSS_MEDIA_SCREEN;

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
    if (cc.css_value != NULL)  { dom_string_unref(cc.css_value); }
    if (pw.universal != NULL) {
        lwc_string_unref(pw.universal);
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

/* ================================================================== */
/* Block layout (milestone B): normal-flow block boxes                 */
/* ================================================================== */

/* A positioned run of inline text, produced by inline layout and consumed by
 * paint / hit-testing. Coordinates are absolute page CSS px (paint subtracts
 * the scroll offset). `text` is an owned UTF-16 buffer of `len` units (not NUL-
 * terminated; drawn with an explicit count). A run coalesces consecutive words
 * of the same style and (if any) the same link, so `href` and the underline
 * span the whole run. `href` is an owned UTF-8 string, or NULL for non-links. */
typedef struct pcore_frag {
    int x;
    int y;
    int w;
    int h;               /* line height: the run's vertical hit / paint extent */
    WCHAR *text;
    int len;
    css_color color;
    int font_px;
    int bold;
    int italic;
    int underline;
    char *href;          /* owned UTF-8 link target, or NULL */
} pcore_frag;

/* Content-box geometry attached to each laid-out element (integer CSS px).
 * Padding / border are stored alongside so paint can derive the padding and
 * border boxes; indices are [0]=top [1]=right [2]=bottom [3]=left. A block that
 * establishes an inline formatting context (no block-level element children)
 * also carries its laid-out inline fragments. */
typedef struct pcore_box {
    int x;
    int y;
    int w;
    int h;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
    int pad[4];          /* padding widths */
    int bord[4];         /* border widths (0 if style none/hidden) */
    css_color bcol[4];   /* border colours (ARGB) */
    pcore_frag *frags;   /* inline text fragments (NULL if a block container) */
    int n_frags;
} pcore_box;

/* Free a box and any inline fragments it owns. */
static void pcore_free_box(pcore_box *box)
{
    int i;

    if (box == NULL) {
        return;
    }
    if (box->frags != NULL) {
        for (i = 0; i < box->n_frags; i++) {
            if (box->frags[i].text != NULL) {
                free(box->frags[i].text);
            }
            if (box->frags[i].href != NULL) {
                free(box->frags[i].href);
            }
        }
        free(box->frags);
    }
    free(box);
}

static dom_string *pcore_box_key = NULL;
static int pcore_doc_height = 0;   /* total height of the most recent layout */

static int pcore_ensure_box_key(void)
{
    if (pcore_box_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) "__pcore_box__", 13,
            &pcore_box_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_box_ud_handler(dom_node_operation op, dom_string *key,
        void *data, struct dom_node *src, struct dom_node *dst)
{
    (void) key;
    (void) src;
    (void) dst;
    if (op == DOM_NODE_DELETED && data != NULL) {
        pcore_free_box((pcore_box *) data);
    }
}

/* Resolve a CSS length (value+unit) to integer CSS px for the given style. */
static int pcore_len_px(const css_computed_style *style,
        css_fixed len, css_unit unit)
{
    css_fixed px = css_unit_len2css_px(style, &pcore_unit_ctx, len, unit);
    return (int) FIXTOINT(px);
}

/* Padding width (px) for side 0=top 1=right 2=bottom 3=left. */
static int pcore_padding_px(const css_computed_style *s, int side)
{
    css_fixed len;
    css_unit unit;
    uint8_t t;

    switch (side) {
    case 0:  t = css_computed_padding_top(s, &len, &unit);    break;
    case 1:  t = css_computed_padding_right(s, &len, &unit);  break;
    case 2:  t = css_computed_padding_bottom(s, &len, &unit); break;
    default: t = css_computed_padding_left(s, &len, &unit);   break;
    }
    return (t == CSS_PADDING_SET) ? pcore_len_px(s, len, unit) : 0;
}

/* Effective border width (px) for a side, honouring border-style. */
static int pcore_border_w(const css_computed_style *s, int side)
{
    css_fixed len;
    css_unit unit;
    uint8_t st, wt;

    switch (side) {
    case 0:  st = css_computed_border_top_style(s);
             wt = css_computed_border_top_width(s, &len, &unit);    break;
    case 1:  st = css_computed_border_right_style(s);
             wt = css_computed_border_right_width(s, &len, &unit);  break;
    case 2:  st = css_computed_border_bottom_style(s);
             wt = css_computed_border_bottom_width(s, &len, &unit); break;
    default: st = css_computed_border_left_style(s);
             wt = css_computed_border_left_width(s, &len, &unit);   break;
    }
    if (st == CSS_BORDER_STYLE_NONE || st == CSS_BORDER_STYLE_HIDDEN) {
        return 0;
    }
    if (wt == CSS_BORDER_WIDTH_WIDTH) {
        return pcore_len_px(s, len, unit);
    }
    if (wt == CSS_BORDER_WIDTH_THIN) {
        return 1;
    }
    return 2;   /* medium / thick */
}

/* Border colour (ARGB) for a side. */
static css_color pcore_border_color(const css_computed_style *s, int side)
{
    css_color c = 0;

    switch (side) {
    case 0:  css_computed_border_top_color(s, &c);    break;
    case 1:  css_computed_border_right_color(s, &c);  break;
    case 2:  css_computed_border_bottom_color(s, &c); break;
    default: css_computed_border_left_color(s, &c);   break;
    }
    return c;
}

/* Create a font of `px` pixel height (Tahoma; available on WM), with optional
 * bold / italic / underline. WinCE coredll has no CreateFontW, so build it via
 * CreateFontIndirectW + LOGFONTW. */
static HFONT pcore_make_font_ex(int px, int bold, int italic, int underline)
{
    LOGFONTW lf;

    if (px < 1) {
        px = 12;
    }
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE) (italic ? 1 : 0);
    lf.lfUnderline = (BYTE) (underline ? 1 : 0);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = (BYTE) (DEFAULT_PITCH | FF_DONTCARE);
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    return CreateFontIndirectW(&lf);
}

/* ------------------------------------------------------------------ */
/* Inline formatting: lay inline content (text + a/span/b/i ...) onto    */
/* line boxes, wrapping at word boundaries, each run in its own font.     */
/* ------------------------------------------------------------------ */

#define PCORE_WBUF_CAP   16384   /* UTF-16 units of inline text per block */
#define PCORE_WORD_CAP    4096   /* words per block                       */
#define PCORE_FC_CAP        24   /* distinct fonts cached per pass         */
#define PCORE_HREF_CAP      64   /* distinct link targets per block        */

/* One whitespace-delimited word, indexing into the shared wbuf, tagged with
 * the computed style of the inline element it came from (for font + colour)
 * and the index of the enclosing link target (-1 if none). */
typedef struct pcore_word {
    int start;
    int len;
    int has_space;       /* a collapsible space follows this word */
    css_computed_style *style;
    int href;            /* index into pcore_il.hrefs, or -1 */
} pcore_word;

/* Word-collection state threaded through the inline tree walk. */
typedef struct pcore_il {
    WCHAR      *wbuf;
    int         wpos;
    pcore_word *words;
    int         nwords;
    int         pending_space;          /* whitespace seen since last word */
    char       *hrefs[PCORE_HREF_CAP];  /* owned UTF-8 link targets         */
    int         n_hrefs;
    dom_string *a_name;                 /* interned "a"                     */
    dom_string *href_name;              /* interned "href"                  */
} pcore_il;

/* A cached font + its metrics, keyed by (px,bold,italic,underline). */
typedef struct pcore_fc {
    int   px, bold, italic, underline;
    HFONT font;
    int   lh;        /* line height (px) */
    int   ascent;    /* baseline from top (px) */
    int   sp_w;      /* width of a space (px) */
} pcore_fc;

/* Per-word layout result (pass 1), consumed by run-coalescing (pass 2). */
typedef struct pcore_wl {
    int line;
    int x;           /* absolute x of the word on its line */
    int word_w;
    int ascent;
    int px, bold, italic, underline;
    css_color color;
    int href;
} pcore_wl;

/* Resolve the inline run attributes (font size/weight/style/decoration and
 * colour) from a computed style. NULL style -> sensible defaults. */
static void pcore_inline_attrs(css_computed_style *s, int *px, int *bold,
        int *italic, int *underline, css_color *color)
{
    css_fixed len;
    css_unit unit;
    uint8_t w, st, td;
    int fs = 16;

    if (s != NULL && css_computed_font_size(s, &len, &unit) ==
            CSS_FONT_SIZE_DIMENSION) {
        fs = pcore_len_px(s, len, unit);
    }
    if (fs < 1) {
        fs = 1;
    }
    *px = fs;

    w = (s != NULL) ? css_computed_font_weight(s) : CSS_FONT_WEIGHT_NORMAL;
    *bold = (w == CSS_FONT_WEIGHT_BOLD || w == CSS_FONT_WEIGHT_BOLDER ||
             w == CSS_FONT_WEIGHT_600 || w == CSS_FONT_WEIGHT_700 ||
             w == CSS_FONT_WEIGHT_800 || w == CSS_FONT_WEIGHT_900) ? 1 : 0;

    st = (s != NULL) ? css_computed_font_style(s) : CSS_FONT_STYLE_NORMAL;
    *italic = (st == CSS_FONT_STYLE_ITALIC || st == CSS_FONT_STYLE_OBLIQUE)
            ? 1 : 0;

    td = (s != NULL) ? css_computed_text_decoration(s)
                     : CSS_TEXT_DECORATION_NONE;
    *underline = (td & CSS_TEXT_DECORATION_UNDERLINE) ? 1 : 0;

    *color = 0;
    if (s != NULL) {
        css_computed_color(s, color);
    }
}

/* Find or create a cached font for the given attributes; measures its metrics
 * (line height, ascent, space width) on first use. `dc` is used only for
 * measuring. Returns the cache index, or -1 on failure / cache full. */
static int pcore_fc_get(HDC dc, pcore_fc *cache, int *n,
        int px, int bold, int italic, int underline)
{
    int i;
    HFONT f, old;
    TEXTMETRICW tm;
    SIZE sz;

    for (i = 0; i < *n; i++) {
        if (cache[i].px == px && cache[i].bold == bold &&
                cache[i].italic == italic && cache[i].underline == underline) {
            return i;
        }
    }
    if (*n >= PCORE_FC_CAP) {
        return -1;
    }
    f = pcore_make_font_ex(px, bold, italic, underline);
    if (f == NULL) {
        return -1;
    }

    i = *n;
    cache[i].px = px;
    cache[i].bold = bold;
    cache[i].italic = italic;
    cache[i].underline = underline;
    cache[i].font = f;
    cache[i].lh = (px * 12) / 10;
    cache[i].ascent = px;
    cache[i].sp_w = px / 3 + 1;

    old = (HFONT) SelectObject(dc, f);
    if (GetTextMetricsW(dc, &tm)) {
        cache[i].lh = tm.tmHeight + tm.tmExternalLeading;
        cache[i].ascent = tm.tmAscent;
    }
    if (GetTextExtentPoint32W(dc, L" ", 1, &sz)) {
        cache[i].sp_w = sz.cx;
    }
    SelectObject(dc, old);

    (*n)++;
    return i;
}

/* Intern a link target (UTF-8, NUL-terminated copy) into the per-block pool,
 * de-duplicating by string value. Returns its index, or -1 on failure/full. */
static int pcore_il_href(pcore_il *il, const char *u8, size_t len)
{
    int i;
    char *copy;

    if (u8 == NULL || len == 0) {
        return -1;
    }
    for (i = 0; i < il->n_hrefs; i++) {
        if (strncmp(il->hrefs[i], u8, len) == 0 &&
                il->hrefs[i][len] == '\0') {
            return i;
        }
    }
    if (il->n_hrefs >= PCORE_HREF_CAP) {
        return -1;
    }
    copy = (char *) malloc(len + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, u8, len);
    copy[len] = '\0';
    il->hrefs[il->n_hrefs] = copy;
    return il->n_hrefs++;
}

/* Append a text node's UTF-16 text to the word list, splitting on whitespace
 * and collapsing runs of whitespace to single inter-word spaces. Each word is
 * tagged with the governing style and link index. */
static void pcore_il_add_text(pcore_il *il, const WCHAR *s, int len,
        css_computed_style *style, int href)
{
    int i = 0;

    while (i < len) {
        WCHAR c = s[i];
        int ws = (c == L' ' || c == L'\t' || c == L'\n' ||
                  c == L'\r' || c == L'\f');
        if (ws) {
            il->pending_space = 1;
            i++;
            continue;
        }

        /* Start of a word. */
        if (il->nwords >= PCORE_WORD_CAP) {
            return;
        }
        if (il->pending_space && il->nwords > 0) {
            il->words[il->nwords - 1].has_space = 1;
        }
        il->pending_space = 0;

        {
            int start = il->wpos;
            int wlen = 0;

            while (i < len) {
                WCHAR cc = s[i];
                if (cc == L' ' || cc == L'\t' || cc == L'\n' ||
                        cc == L'\r' || cc == L'\f') {
                    break;
                }
                if (il->wpos >= PCORE_WBUF_CAP - 1) {
                    break;
                }
                il->wbuf[il->wpos++] = cc;
                wlen++;
                i++;
            }
            if (wlen > 0) {
                il->words[il->nwords].start = start;
                il->words[il->nwords].len = wlen;
                il->words[il->nwords].has_space = 0;
                il->words[il->nwords].style = style;
                il->words[il->nwords].href = href;
                il->nwords++;
            }
        }
    }
}

/* Walk the inline subtree of `node` in document order, gathering text runs.
 * `cur` is the computed style governing text that appears directly here, and
 * `href` the index of the enclosing <a> target (-1 if none). Inline element
 * children switch to their own computed style; an <a> with an href starts a
 * link scope for its descendants. display:none descendants are skipped. */
static void pcore_il_walk(pcore_il *il, dom_node *node,
        css_computed_style *cur, int href)
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
                dom_string *txt = NULL;
                if (dom_node_get_text_content(child, &txt) == DOM_NO_ERR &&
                        txt != NULL) {
                    const char *u8 = dom_string_data(txt);
                    size_t bl = dom_string_byte_length(txt);
                    if (u8 != NULL && bl > 0) {
                        WCHAR tmp[1024];
                        int wl = MultiByteToWideChar(CP_UTF8, 0, u8,
                                (int) bl, tmp, 1024);
                        if (wl > 0) {
                            pcore_il_add_text(il, tmp, wl, cur, href);
                        }
                    }
                    dom_string_unref(txt);
                }
            } else if (type == DOM_ELEMENT_NODE) {
                void *csd = NULL;
                css_computed_style *cs = cur;
                uint8_t cdisp = CSS_DISPLAY_INLINE;
                int child_href = href;

                if (dom_node_get_user_data(child, pcore_style_key, &csd) ==
                        DOM_NO_ERR && csd != NULL) {
                    cs = (css_computed_style *) csd;
                    cdisp = css_computed_display(cs, false);
                }

                /* An <a href="..."> opens a link scope for its descendants. */
                if (il->a_name != NULL) {
                    dom_string *nm = NULL;
                    if (dom_node_get_node_name(child, &nm) == DOM_NO_ERR &&
                            nm != NULL) {
                        if (dom_string_caseless_isequal(nm, il->a_name)) {
                            dom_string *hv = NULL;
                            if (dom_element_get_attribute(child,
                                    il->href_name, &hv) == DOM_NO_ERR &&
                                    hv != NULL) {
                                const char *hu8 = dom_string_data(hv);
                                size_t hl = dom_string_byte_length(hv);
                                int hi = pcore_il_href(il, hu8, hl);
                                if (hi >= 0) {
                                    child_href = hi;
                                }
                                dom_string_unref(hv);
                            }
                        }
                        dom_string_unref(nm);
                    }
                }

                if (cdisp != CSS_DISPLAY_NONE) {
                    pcore_il_walk(il, child, cs, child_href);
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

/* Establish an inline formatting context for `node`: gather its inline content
 * and lay it out into line boxes within [content_x, content_x+content_w),
 * starting at content_y. Consecutive words sharing a style and link coalesce
 * into one fragment (continuous underline, one hit-rect per link). Stores the
 * fragments on `box` and returns the total inline height (px). `mdc` measures. */
static int pcore_layout_inline(dom_node *node, css_computed_style *style,
        int content_x, int content_y, int content_w, HDC mdc, pcore_box *box)
{
    pcore_il    il;
    pcore_fc    cache[PCORE_FC_CAP];
    int         nfc = 0;
    pcore_wl   *wl = NULL;
    int        *linetop = NULL;
    int        *linelh = NULL;
    int        *lineasc = NULL;
    pcore_frag *frags = NULL;
    int         nfrags = 0;
    int         nlines = 0;
    int         pen_x = 0;
    int         line = 0;
    int         total_h = 0;
    int         i;

    box->frags = NULL;
    box->n_frags = 0;

    if (content_w < 1) {
        content_w = 1;
    }

    il.wbuf = (WCHAR *) malloc(sizeof(WCHAR) * PCORE_WBUF_CAP);
    il.words = (pcore_word *) malloc(sizeof(pcore_word) * PCORE_WORD_CAP);
    if (il.wbuf == NULL || il.words == NULL) {
        if (il.wbuf != NULL) free(il.wbuf);
        if (il.words != NULL) free(il.words);
        return 0;
    }
    il.wpos = 0;
    il.nwords = 0;
    il.pending_space = 0;
    il.n_hrefs = 0;
    il.a_name = NULL;
    il.href_name = NULL;
    dom_string_create((const uint8_t *) "a", 1, &il.a_name);
    dom_string_create((const uint8_t *) "href", 4, &il.href_name);

    pcore_il_walk(&il, node, style, -1);

    if (il.nwords == 0) {
        goto done;
    }

    wl = (pcore_wl *) malloc(sizeof(pcore_wl) * il.nwords);
    linetop = (int *) malloc(sizeof(int) * (il.nwords + 1));
    linelh = (int *) malloc(sizeof(int) * (il.nwords + 1));
    lineasc = (int *) malloc(sizeof(int) * (il.nwords + 1));
    frags = (pcore_frag *) malloc(sizeof(pcore_frag) * il.nwords);
    if (wl == NULL || linetop == NULL || linelh == NULL ||
            lineasc == NULL || frags == NULL) {
        if (frags != NULL) { free(frags); frags = NULL; }
        goto done;
    }

    /* Pass 1: measure + wrap; record each word's line, x, metrics. */
    linelh[0] = 0;
    lineasc[0] = 0;
    for (i = 0; i < il.nwords; i++) {
        pcore_word *wd = &il.words[i];
        int px, bold, italic, underline;
        css_color color;
        int fc, word_w, sp_w, lh, asc;
        HFONT sel;
        SIZE sz;

        pcore_inline_attrs(wd->style, &px, &bold, &italic, &underline, &color);
        fc = pcore_fc_get(mdc, cache, &nfc, px, bold, italic, underline);
        if (fc < 0) {
            lh = (px * 12) / 10;
            asc = px;
            sp_w = px / 3 + 1;
            sel = NULL;
        } else {
            lh = cache[fc].lh;
            asc = cache[fc].ascent;
            sp_w = cache[fc].sp_w;
            sel = cache[fc].font;
        }

        word_w = px * wd->len;   /* fallback estimate */
        if (sel != NULL) {
            HFONT old = (HFONT) SelectObject(mdc, sel);
            if (GetTextExtentPoint32W(mdc, il.wbuf + wd->start, wd->len, &sz)) {
                word_w = sz.cx;
            }
            SelectObject(mdc, old);
        }

        /* Wrap onto a new line if this word does not fit and the line is
         * non-empty. */
        if (pen_x > 0 && pen_x + word_w > content_w) {
            line++;
            pen_x = 0;
            linelh[line] = 0;
            lineasc[line] = 0;
        }

        wl[i].line = line;
        wl[i].x = content_x + pen_x;
        wl[i].word_w = word_w;
        wl[i].ascent = asc;
        wl[i].px = px;
        wl[i].bold = bold;
        wl[i].italic = italic;
        wl[i].underline = underline;
        wl[i].color = color;
        wl[i].href = wd->href;

        if (lh > linelh[line]) {
            linelh[line] = lh;
        }
        if (asc > lineasc[line]) {
            lineasc[line] = asc;
        }
        pen_x += word_w + (wd->has_space ? sp_w : 0);
    }
    nlines = line + 1;

    /* Line tops stack by each line's height. */
    linetop[0] = content_y;
    for (i = 1; i < nlines; i++) {
        linetop[i] = linetop[i - 1] + linelh[i - 1];
    }
    total_h = (linetop[nlines - 1] + linelh[nlines - 1]) - content_y;

    /* Pass 2: coalesce consecutive words sharing line + style + link into a
     * single fragment (continuous underline, one hit-rect per link). */
    i = 0;
    while (i < il.nwords) {
        int j = i + 1;
        int ln = wl[i].line;
        int need, k, pos;
        WCHAR *copy;

        while (j < il.nwords && wl[j].line == ln &&
                wl[j].px == wl[i].px && wl[j].bold == wl[i].bold &&
                wl[j].italic == wl[i].italic &&
                wl[j].underline == wl[i].underline &&
                wl[j].color == wl[i].color && wl[j].href == wl[i].href) {
            j++;
        }

        /* Text length = word chars + one space per interior collapsible gap. */
        need = 0;
        for (k = i; k < j; k++) {
            need += il.words[k].len;
            if (k < j - 1 && il.words[k].has_space) {
                need++;
            }
        }
        copy = (WCHAR *) malloc(sizeof(WCHAR) * (need > 0 ? need : 1));
        pos = 0;
        if (copy != NULL) {
            for (k = i; k < j; k++) {
                memcpy(copy + pos, il.wbuf + il.words[k].start,
                        sizeof(WCHAR) * il.words[k].len);
                pos += il.words[k].len;
                if (k < j - 1 && il.words[k].has_space) {
                    copy[pos++] = L' ';
                }
            }
        }

        frags[nfrags].x = wl[i].x;
        frags[nfrags].y = linetop[ln] + (lineasc[ln] - wl[i].ascent);
        frags[nfrags].w = (wl[j - 1].x + wl[j - 1].word_w) - wl[i].x;
        frags[nfrags].h = linelh[ln];
        frags[nfrags].text = copy;
        frags[nfrags].len = pos;
        frags[nfrags].color = wl[i].color;
        frags[nfrags].font_px = wl[i].px;
        frags[nfrags].bold = wl[i].bold;
        frags[nfrags].italic = wl[i].italic;
        frags[nfrags].underline = wl[i].underline;
        frags[nfrags].href = NULL;
        if (wl[i].href >= 0 && wl[i].href < il.n_hrefs) {
            const char *h = il.hrefs[wl[i].href];
            size_t hl = strlen(h);
            char *hc = (char *) malloc(hl + 1);
            if (hc != NULL) {
                memcpy(hc, h, hl + 1);
                frags[nfrags].href = hc;
            }
        }
        nfrags++;
        i = j;
    }

    box->frags = frags;
    box->n_frags = nfrags;
    frags = NULL;   /* ownership transferred to box */

done:
    /* Release the cache fonts (paint rebuilds its own), the href pool, the
     * interned names and the scratch buffers. */
    for (i = 0; i < nfc; i++) {
        DeleteObject(cache[i].font);
    }
    for (i = 0; i < il.n_hrefs; i++) {
        free(il.hrefs[i]);
    }
    if (il.a_name != NULL) {
        dom_string_unref(il.a_name);
    }
    if (il.href_name != NULL) {
        dom_string_unref(il.href_name);
    }
    if (frags != NULL) {
        free(frags);
    }
    if (wl != NULL) free(wl);
    if (linetop != NULL) free(linetop);
    if (linelh != NULL) free(linelh);
    if (lineasc != NULL) free(lineasc);
    free(il.wbuf);
    free(il.words);
    return total_h;
}

/* Lay out element `node` as a block box inside a containing block whose
 * content edge is at `cb_x` with content width `cb_w`, starting at vertical
 * cursor *cursor_y. Stores the box on the node and advances *cursor_y past it
 * (including bottom margin). `is_root` picks display computation. */
static int pcore_layout_block(dom_node *node, int cb_x, int cb_w,
        int *cursor_y, int is_root, HDC mdc, int prev_mb)
{
    void *sd = NULL;
    css_computed_style *style;
    pcore_box *box;
    css_fixed len;
    css_unit unit;
    uint8_t disp;
    int mt, mr, mb, ml;
    uint8_t mlt, mrt;   /* margin left/right types (for auto centering) */
    int width_set;
    int pad[4];
    int bord[4];
    int k;
    int content_x, content_y, content_w;
    int child_y;
    int had_block_child;
    dom_node *child;
    void *old = NULL;

    if (dom_node_get_user_data(node, pcore_style_key, &sd) != DOM_NO_ERR ||
            sd == NULL) {
        return 0;
    }
    style = (css_computed_style *) sd;

    disp = css_computed_display(style, is_root ? true : false);
    if (disp == CSS_DISPLAY_NONE) {
        return 0;   /* not laid out; contributes nothing to the flow */
    }

    /* Margins. Left/right may be `auto` (enables centering, handled below). */
    mt = (css_computed_margin_top(style, &len, &unit) == CSS_MARGIN_SET)
            ? pcore_len_px(style, len, unit) : 0;
    mb = (css_computed_margin_bottom(style, &len, &unit) == CSS_MARGIN_SET)
            ? pcore_len_px(style, len, unit) : 0;
    mrt = css_computed_margin_right(style, &len, &unit);
    mr = (mrt == CSS_MARGIN_SET) ? pcore_len_px(style, len, unit) : 0;
    mlt = css_computed_margin_left(style, &len, &unit);
    ml = (mlt == CSS_MARGIN_SET) ? pcore_len_px(style, len, unit) : 0;

    /* Padding + border widths (per side). */
    for (k = 0; k < 4; k++) {
        pad[k] = pcore_padding_px(style, k);
        bord[k] = pcore_border_w(style, k);
    }

    /* Width: explicit content value, else fill the containing block minus this
     * box's own margins, borders and padding. (Fitting over-wide desktop pages
     * to a small screen - viewport meta / fit-to-width - is a deliberate later
     * feature, not a blind clamp here.) */
    width_set = (css_computed_width(style, &len, &unit) == CSS_WIDTH_SET);
    if (width_set) {
        content_w = pcore_len_px(style, len, unit);
    } else {
        content_w = cb_w - ml - mr - bord[1] - bord[3] - pad[1] - pad[3];
    }
    if (content_w < 0) {
        content_w = 0;
    }

    /* margin:auto - a definite-width block with auto left/right margins
     * centers (both auto) or pushes to one side (one auto) in its container. */
    if (width_set) {
        int avail = cb_w - content_w - bord[1] - bord[3] - pad[1] - pad[3];
        if (avail > 0) {
            if (mlt == CSS_MARGIN_AUTO && mrt == CSS_MARGIN_AUTO) {
                ml = avail / 2;
                mr = avail - ml;
            } else if (mlt == CSS_MARGIN_AUTO) {
                ml = avail;
            } else if (mrt == CSS_MARGIN_AUTO) {
                mr = avail;
            }
        }
    }

    content_x = cb_x + ml + bord[3] + pad[3];
    content_y = *cursor_y + mt + bord[0] + pad[0]
            - ((prev_mb < mt) ? prev_mb : mt);   /* collapse with prev sibling */

    box = (pcore_box *) malloc(sizeof(pcore_box));
    if (box == NULL) {
        return 0;
    }
    box->x = content_x;
    box->y = content_y;
    box->w = content_w;
    box->h = 0;
    box->frags = NULL;
    box->n_frags = 0;
    box->margin_top = mt;
    box->margin_right = mr;
    box->margin_bottom = mb;
    box->margin_left = ml;
    for (k = 0; k < 4; k++) {
        box->pad[k] = pad[k];
        box->bord[k] = bord[k];
        box->bcol[k] = pcore_border_color(style, k);
    }
    dom_node_set_user_data(node, pcore_box_key, box,
            pcore_box_ud_handler, &old);
    if (old != NULL) {
        pcore_free_box((pcore_box *) old);   /* release previous layout's box */
    }

    /* Lay out block-level element children inside our content box. Inline
     * children (display:inline / inline-block) are NOT laid out as boxes here;
     * if this block has no block-level children at all it establishes an inline
     * formatting context below and its full inline content (text + inline
     * elements) is laid out onto line boxes. */
    child_y = content_y;
    had_block_child = 0;
    {
        int prev_child_mb = 0;   /* previous child's bottom margin (collapse) */

        if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
            while (child != NULL) {
                dom_node_type type;
                dom_node *next;

                if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                        type == DOM_ELEMENT_NODE) {
                    void *csd = NULL;
                    int is_block = 1;   /* unstyled -> treat as block */

                    if (dom_node_get_user_data(child, pcore_style_key, &csd)
                            == DOM_NO_ERR && csd != NULL) {
                        uint8_t cd = css_computed_display(
                                (css_computed_style *) csd, false);
                        is_block = (cd != CSS_DISPLAY_INLINE &&
                                    cd != CSS_DISPLAY_INLINE_BLOCK &&
                                    cd != CSS_DISPLAY_NONE);
                    }

                    if (is_block) {
                        int before = child_y;
                        int cmb = pcore_layout_block(child, content_x,
                                content_w, &child_y, 0, mdc, prev_child_mb);
                        if (child_y != before) {
                            had_block_child = 1;
                            prev_child_mb = cmb;
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
    }

    if (had_block_child) {
        box->h = child_y - content_y;
    } else {
        /* Inline formatting context: lay this block's inline content (text +
         * inline elements) onto line boxes; the height is the total of those
         * lines. An empty block has height 0. */
        box->h = pcore_layout_inline(node, style, content_x, content_y,
                content_w, mdc, box);
    }

    *cursor_y = content_y + box->h + pad[2] + bord[2] + mb;
    return mb;
}

PCORE_API int PCore_LayoutDocument(HANDLE hDoc, int viewport_w, int viewport_h)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;
    int           cursor_y = 0;

    (void) viewport_h;   /* not used yet (no %/viewport-relative heights) */

    if (doc == NULL || viewport_w <= 0) {
        return 1;
    }
    if (pcore_style_key == NULL) {
        return 1;   /* PCore_StyleDocument must run first */
    }
    if (pcore_ensure_box_key() != 0) {
        return 1;
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        return 1;
    }

    {
        /* A memory DC for text measurement during layout (font metrics). */
        HDC mdc = CreateCompatibleDC(NULL);
        pcore_layout_block(root, 0, viewport_w, &cursor_y, 1, mdc, 0);
        if (mdc != NULL) {
            DeleteDC(mdc);
        }
    }
    pcore_doc_height = cursor_y;   /* total laid-out height */

    dom_node_unref(root);
    return 0;
}

PCORE_API int PCore_DocumentHeight(HANDLE hDoc)
{
    (void) hDoc;
    return pcore_doc_height;
}

PCORE_API int PCore_NodeBox(HANDLE hDoc, const char *tag,
        int *x, int *y, int *w, int *h)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;
    dom_node     *elem = NULL;
    lwc_string   *want = NULL;
    void         *bd = NULL;
    pcore_box    *box;
    int           rc = 1;

    if (doc == NULL || tag == NULL) {
        return 1;
    }
    if (pcore_box_key == NULL) {
        return 1;
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
    if (dom_node_get_user_data(elem, pcore_box_key, &bd) != DOM_NO_ERR ||
            bd == NULL) {
        goto cleanup;
    }
    box = (pcore_box *) bd;
    if (x != NULL) {
        *x = box->x;
    }
    if (y != NULL) {
        *y = box->y;
    }
    if (w != NULL) {
        *w = box->w;
    }
    if (h != NULL) {
        *h = box->h;
    }
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

/* ================================================================== */
/* Painting (milestone C): draw the laid-out box tree to a GDI DC      */
/* ================================================================== */

/* libcss colour 0xAARRGGBB -> GDI COLORREF 0x00BBGGRR. */
static COLORREF pcore_argb_to_colorref(css_color argb)
{
    return RGB((int) ((argb >> 16) & 0xFF),
               (int) ((argb >> 8) & 0xFF),
               (int) (argb & 0xFF));
}

/* Paint `node` (an element with a box) and its element descendants: background
 * + borders, then block-level child boxes (recursion), then this block's inline
 * text fragments if it established an inline formatting context. `sx`/`sy`
 * scroll the page beneath the viewport. */
static void pcore_paint_node(HDC hdc, dom_node *node, int sx, int sy)
{
    void *sd = NULL;
    void *bd = NULL;
    css_computed_style *style;
    pcore_box *box;
    css_color col;
    dom_node *child;

    if (dom_node_get_user_data(node, pcore_box_key, &bd) != DOM_NO_ERR ||
            bd == NULL) {
        return;   /* not laid out (e.g. display:none) */
    }
    box = (pcore_box *) bd;

    if (dom_node_get_user_data(node, pcore_style_key, &sd) != DOM_NO_ERR ||
            sd == NULL) {
        return;
    }
    style = (css_computed_style *) sd;

    /* Border box = content box expanded by padding + border widths. */
    {
        int bx = box->x - box->pad[3] - box->bord[3] - sx;
        int by = box->y - box->pad[0] - box->bord[0] - sy;
        int bw = box->w + box->pad[1] + box->pad[3]
                + box->bord[1] + box->bord[3];
        int bh = box->h + box->pad[0] + box->pad[2]
                + box->bord[0] + box->bord[2];
        RECT r;
        HBRUSH brsh;
        int s;

        /* Background fills the border box (skip transparent: alpha 0). */
        if (css_computed_background_color(style, &col) ==
                CSS_BACKGROUND_COLOR_COLOR && ((col >> 24) & 0xFF) != 0) {
            r.left = bx;
            r.top = by;
            r.right = bx + bw;
            r.bottom = by + bh;
            brsh = CreateSolidBrush(pcore_argb_to_colorref(col));
            if (brsh != NULL) {
                FillRect(hdc, &r, brsh);
                DeleteObject(brsh);
            }
        }

        /* Border edges. */
        for (s = 0; s < 4; s++) {
            if (box->bord[s] <= 0) {
                continue;
            }
            switch (s) {
            case 0:   /* top */
                r.left = bx; r.top = by;
                r.right = bx + bw; r.bottom = by + box->bord[0];
                break;
            case 1:   /* right */
                r.left = bx + bw - box->bord[1]; r.top = by;
                r.right = bx + bw; r.bottom = by + bh;
                break;
            case 2:   /* bottom */
                r.left = bx; r.top = by + bh - box->bord[2];
                r.right = bx + bw; r.bottom = by + bh;
                break;
            default:  /* left */
                r.left = bx; r.top = by;
                r.right = bx + box->bord[3]; r.bottom = by + bh;
                break;
            }
            brsh = CreateSolidBrush(pcore_argb_to_colorref(box->bcol[s]));
            if (brsh != NULL) {
                FillRect(hdc, &r, brsh);
                DeleteObject(brsh);
            }
        }
    }

    /* Paint block-level element children (each has its own box). */
    if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
        while (child != NULL) {
            dom_node_type type;
            dom_node *next;
            void *cbd = NULL;

            if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                    type == DOM_ELEMENT_NODE &&
                    dom_node_get_user_data(child, pcore_box_key, &cbd) ==
                            DOM_NO_ERR && cbd != NULL) {
                pcore_paint_node(hdc, child, sx, sy);
            }

            if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
                dom_node_unref(child);
                break;
            }
            dom_node_unref(child);
            child = next;
        }
    }

    /* Inline formatting context: draw the laid-out text fragments, each in its
     * own font (size / bold / italic / underline) and computed colour. */
    if (box->n_frags > 0 && box->frags != NULL) {
        pcore_fc cache[PCORE_FC_CAP];
        int nfc = 0;
        int i;

        SetBkMode(hdc, TRANSPARENT);
        for (i = 0; i < box->n_frags; i++) {
            pcore_frag *fr = &box->frags[i];
            int fc;
            HFONT old;

            if (fr->text == NULL || fr->len <= 0) {
                continue;
            }
            fc = pcore_fc_get(hdc, cache, &nfc, fr->font_px, fr->bold,
                    fr->italic, fr->underline);
            if (fc < 0) {
                continue;
            }
            old = (HFONT) SelectObject(hdc, cache[fc].font);
            SetTextColor(hdc, pcore_argb_to_colorref(fr->color));
            ExtTextOutW(hdc, fr->x - sx, fr->y - sy, 0, NULL,
                    fr->text, fr->len, NULL);
            SelectObject(hdc, old);
        }
        for (i = 0; i < nfc; i++) {
            DeleteObject(cache[i].font);
        }
    }
}

PCORE_API void PCore_PaintDocument(HANDLE hDoc, HDC hdc,
        int scroll_x, int scroll_y)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;

    if (doc == NULL || hdc == NULL) {
        return;
    }
    if (pcore_box_key == NULL || pcore_style_key == NULL) {
        return;   /* document not styled + laid out */
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        return;
    }

    pcore_paint_node(hdc, root, scroll_x, scroll_y);

    dom_node_unref(root);
}

/* Recursively find the link fragment containing document-space point (x,y).
 * Returns a borrowed pointer into a fragment's href (valid while the document
 * lives), or NULL. `node` is borrowed. */
static const char *pcore_link_at_node(dom_node *node, int x, int y)
{
    void *bd = NULL;
    pcore_box *box;
    dom_node *child;
    const char *found = NULL;

    if (dom_node_get_user_data(node, pcore_box_key, &bd) != DOM_NO_ERR ||
            bd == NULL) {
        return NULL;
    }
    box = (pcore_box *) bd;

    if (box->frags != NULL) {
        int i;
        for (i = 0; i < box->n_frags; i++) {
            pcore_frag *f = &box->frags[i];
            if (f->href != NULL &&
                    x >= f->x && x < f->x + f->w &&
                    y >= f->y && y < f->y + f->h) {
                return f->href;
            }
        }
    }

    if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
        while (child != NULL) {
            dom_node_type type;
            dom_node *next;
            void *cbd = NULL;

            if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                    type == DOM_ELEMENT_NODE &&
                    dom_node_get_user_data(child, pcore_box_key, &cbd) ==
                            DOM_NO_ERR && cbd != NULL) {
                found = pcore_link_at_node(child, x, y);
            }
            if (found != NULL) {
                dom_node_unref(child);
                return found;
            }
            if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
                dom_node_unref(child);
                return NULL;
            }
            dom_node_unref(child);
            child = next;
        }
    }
    return NULL;
}

PCORE_API int PCore_LinkAt(HANDLE hDoc, int x, int y, char *out_href, int cap)
{
    dom_document *doc = (dom_document *) hDoc;
    dom_node     *root = NULL;
    const char   *href;
    int           rc = 0;

    if (doc == NULL || out_href == NULL || cap <= 0) {
        return 0;
    }
    if (pcore_box_key == NULL) {
        return 0;   /* not laid out */
    }
    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        return 0;
    }

    href = pcore_link_at_node(root, x, y);
    if (href != NULL) {
        int n = (int) strlen(href);
        if (n > cap - 1) {
            n = cap - 1;
        }
        memcpy(out_href, href, n);
        out_href[n] = '\0';
        rc = 1;
    }

    dom_node_unref(root);
    return rc;
}
