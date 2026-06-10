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

/* 800x600 viewport, 16/6 pt fonts, 96 dpi - constant expressions so this can
 * be a static initialiser (INTTOFIX() is a function call, not allowed here). */
static const css_unit_ctx pcore_unit_ctx = {
    800 * (1 << CSS_RADIX_POINT),   /* viewport_width    */
    600 * (1 << CSS_RADIX_POINT),   /* viewport_height   */
    16  * (1 << CSS_RADIX_POINT),   /* font_size_default */
    6   * (1 << CSS_RADIX_POINT),   /* font_size_minimum */
    96  * (1 << CSS_RADIX_POINT),   /* device_dpi        */
    NULL,                            /* root_style        */
    NULL,                            /* pw                */
    NULL                             /* measure           */
};

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

PCORE_API int PCore_StyleDocument(HANDLE hDoc, HANDLE hSheet)
{
    dom_document   *doc = (dom_document *) hDoc;
    css_stylesheet *author = (css_stylesheet *) hSheet;
    HANDLE          hUA = NULL;
    css_select_ctx *ctx = NULL;
    dom_node       *root = NULL;
    css_media       media;
    pcore_select_pw pw;
    int             rc = 1;

    if (doc == NULL || author == NULL) {
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
    if (css_select_ctx_append_sheet(ctx, author,
            CSS_ORIGIN_AUTHOR, NULL) != CSS_OK) {
        goto cleanup;
    }

    memset(&media, 0, sizeof(media));
    media.type = CSS_MEDIA_SCREEN;

    if (dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
            root == NULL) {
        goto cleanup;
    }

    pcore_style_subtree(ctx, &pw, &media, root, NULL);
    rc = 0;

cleanup:
    if (root != NULL) {
        dom_node_unref(root);
    }
    if (ctx != NULL) {
        css_select_ctx_destroy(ctx);
    }
    if (hUA != NULL) {
        PCore_FreeStylesheet(hUA);
    }
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

/* Content-box geometry attached to each laid-out element (integer CSS px). */
typedef struct pcore_box {
    int x;
    int y;
    int w;
    int h;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
} pcore_box;

static dom_string *pcore_box_key = NULL;

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
        free(data);
    }
}

/* Resolve a CSS length (value+unit) to integer CSS px for the given style. */
static int pcore_len_px(const css_computed_style *style,
        css_fixed len, css_unit unit)
{
    css_fixed px = css_unit_len2css_px(style, &pcore_unit_ctx, len, unit);
    return (int) FIXTOINT(px);
}

/* Create a font of `px` pixel height (Tahoma; available on WM). WinCE coredll
 * has no CreateFontW, so build it via CreateFontIndirectW + LOGFONTW. */
static HFONT pcore_make_font(int px)
{
    LOGFONTW lf;

    if (px < 1) {
        px = 12;
    }
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = (BYTE) (DEFAULT_PITCH | FF_DONTCARE);
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    return CreateFontIndirectW(&lf);
}

/* Greedy word-wrap `text` (wlen UTF-16 units) to lines of at most `maxw` px,
 * using whatever font is selected in `hdc`. Returns the line count. When
 * `draw` is non-zero, also draws each line at (x, y + line*lh). A single word
 * wider than maxw overflows on its own line (no mid-word breaking yet). */
static int pcore_text_lines(HDC hdc, const WCHAR *text, int wlen, int maxw,
        int draw, int x, int y, int lh)
{
    int line_start = 0;
    int i = 0;
    int count = 0;
    SIZE sz;

    if (maxw < 1) {
        maxw = 1;
    }

    while (i < wlen) {
        int word_end = i;

        while (word_end < wlen && text[word_end] != L' ') {
            word_end++;   /* word body */
        }
        while (word_end < wlen && text[word_end] == L' ') {
            word_end++;   /* trailing spaces */
        }

        if (GetTextExtentPoint32W(hdc, text + line_start,
                word_end - line_start, &sz) && sz.cx > maxw &&
                i > line_start) {
            /* This word overflows the current line: emit [line_start, i). */
            int len = i - line_start;
            while (len > 0 && text[line_start + len - 1] == L' ') {
                len--;   /* trim trailing spaces from the drawn line */
            }
            if (draw && len > 0) {
                ExtTextOutW(hdc, x, y + count * lh, 0, NULL,
                        text + line_start, len, NULL);
            }
            count++;
            line_start = i;   /* start a new line at this word; re-loop */
        } else {
            i = word_end;     /* word fits (or is alone): consume it */
        }
    }

    /* Final line. */
    {
        int len = wlen - line_start;
        while (len > 0 && text[line_start + len - 1] == L' ') {
            len--;
        }
        if (draw && len > 0) {
            ExtTextOutW(hdc, x, y + count * lh, 0, NULL,
                    text + line_start, len, NULL);
        }
        count++;
    }

    return count;
}

/* Lay out element `node` as a block box inside a containing block whose
 * content edge is at `cb_x` with content width `cb_w`, starting at vertical
 * cursor *cursor_y. Stores the box on the node and advances *cursor_y past it
 * (including bottom margin). `is_root` picks display computation. */
static void pcore_layout_block(dom_node *node, int cb_x, int cb_w,
        int *cursor_y, int is_root, HDC mdc)
{
    void *sd = NULL;
    css_computed_style *style;
    pcore_box *box;
    css_fixed len;
    css_unit unit;
    uint8_t disp;
    int mt, mr, mb, ml;
    int content_x, content_y, content_w;
    int child_y;
    int had_block_child;
    dom_node *child;
    void *old = NULL;

    if (dom_node_get_user_data(node, pcore_style_key, &sd) != DOM_NO_ERR ||
            sd == NULL) {
        return;
    }
    style = (css_computed_style *) sd;

    disp = css_computed_display(style, is_root ? true : false);
    if (disp == CSS_DISPLAY_NONE) {
        return;   /* not laid out; contributes nothing to the flow */
    }

    /* Margins (auto unsupported in this first cut -> 0). */
    mt = (css_computed_margin_top(style, &len, &unit) == CSS_MARGIN_SET)
            ? pcore_len_px(style, len, unit) : 0;
    mr = (css_computed_margin_right(style, &len, &unit) == CSS_MARGIN_SET)
            ? pcore_len_px(style, len, unit) : 0;
    mb = (css_computed_margin_bottom(style, &len, &unit) == CSS_MARGIN_SET)
            ? pcore_len_px(style, len, unit) : 0;
    ml = (css_computed_margin_left(style, &len, &unit) == CSS_MARGIN_SET)
            ? pcore_len_px(style, len, unit) : 0;

    /* Width: explicit value, else fill the containing block. */
    if (css_computed_width(style, &len, &unit) == CSS_WIDTH_SET) {
        content_w = pcore_len_px(style, len, unit);
    } else {
        content_w = cb_w - ml - mr;
    }
    if (content_w < 0) {
        content_w = 0;
    }

    content_x = cb_x + ml;
    content_y = *cursor_y + mt;

    box = (pcore_box *) malloc(sizeof(pcore_box));
    if (box == NULL) {
        return;
    }
    box->x = content_x;
    box->y = content_y;
    box->w = content_w;
    box->h = 0;
    box->margin_top = mt;
    box->margin_right = mr;
    box->margin_bottom = mb;
    box->margin_left = ml;
    dom_node_set_user_data(node, pcore_box_key, box,
            pcore_box_ud_handler, &old);

    /* Lay out block children inside our content box. */
    child_y = content_y;
    had_block_child = 0;
    if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
        while (child != NULL) {
            dom_node_type type;
            dom_node *next;

            if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
                    type == DOM_ELEMENT_NODE) {
                int before = child_y;
                pcore_layout_block(child, content_x, content_w, &child_y,
                        0, mdc);
                if (child_y != before) {
                    had_block_child = 1;
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

    if (had_block_child) {
        box->h = child_y - content_y;
    } else {
        /* Leaf block: wrap its text content to the content width and set the
         * height from the resulting line count (real text measurement). */
        int fs = 16;
        int lh;
        int n_lines = 1;
        dom_string *text = NULL;

        if (css_computed_font_size(style, &len, &unit) ==
                CSS_FONT_SIZE_DIMENSION) {
            fs = pcore_len_px(style, len, unit);
        }
        lh = (fs * 12) / 10;

        if (dom_node_get_text_content(node, &text) == DOM_NO_ERR &&
                text != NULL) {
            const char *utf8 = dom_string_data(text);
            size_t blen = dom_string_byte_length(text);

            if (utf8 != NULL && blen > 0) {
                WCHAR wbuf[1024];
                int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8,
                        (int) blen, wbuf, 1023);
                if (wlen > 0) {
                    HFONT f = pcore_make_font(fs);
                    HFONT old = (HFONT) SelectObject(mdc, f);
                    TEXTMETRICW tm;
                    if (GetTextMetricsW(mdc, &tm)) {
                        lh = tm.tmHeight + tm.tmExternalLeading;
                    }
                    n_lines = pcore_text_lines(mdc, wbuf, wlen, content_w,
                            0, 0, 0, lh);
                    SelectObject(mdc, old);
                    DeleteObject(f);
                }
            }
            dom_string_unref(text);
        }

        if (n_lines < 1) {
            n_lines = 1;
        }
        box->h = n_lines * lh;
    }

    *cursor_y = content_y + box->h + mb;
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
        pcore_layout_block(root, 0, viewport_w, &cursor_y, 1, mdc);
        if (mdc != NULL) {
            DeleteDC(mdc);
        }
    }

    dom_node_unref(root);
    return 0;
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

/* Paint `node` (an element with a box) and its element descendants. Leaf
 * blocks draw their text content; containers recurse. `sx`/`sy` scroll. */
static void pcore_paint_node(HDC hdc, dom_node *node, int sx, int sy)
{
    void *sd = NULL;
    void *bd = NULL;
    css_computed_style *style;
    pcore_box *box;
    css_color col;
    dom_node *child;
    int had_block_child;

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

    /* Background colour (skip transparent: alpha 0). */
    if (css_computed_background_color(style, &col) ==
            CSS_BACKGROUND_COLOR_COLOR && ((col >> 24) & 0xFF) != 0) {
        RECT r;
        HBRUSH br;

        r.left = box->x - sx;
        r.top = box->y - sy;
        r.right = box->x + box->w - sx;
        r.bottom = box->y + box->h - sy;
        br = CreateSolidBrush(pcore_argb_to_colorref(col));
        if (br != NULL) {
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
    }

    /* Paint element children; remember whether any were laid out. */
    had_block_child = 0;
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
                had_block_child = 1;
            }

            if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
                dom_node_unref(child);
                break;
            }
            dom_node_unref(child);
            child = next;
        }
    }

    /* Leaf block: wrap + draw its text content in its computed colour/font. */
    if (had_block_child == 0) {
        dom_string *text = NULL;

        if (dom_node_get_text_content(node, &text) == DOM_NO_ERR &&
                text != NULL) {
            const char *utf8 = dom_string_data(text);
            size_t blen = dom_string_byte_length(text);

            if (utf8 != NULL && blen > 0) {
                WCHAR wbuf[1024];
                int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int) blen,
                        wbuf, 1023);
                if (wlen > 0) {
                    css_fixed flen;
                    css_unit funit;
                    int fs = 16;
                    int lh;
                    HFONT f, old;
                    TEXTMETRICW tm;

                    if (css_computed_font_size(style, &flen, &funit) ==
                            CSS_FONT_SIZE_DIMENSION) {
                        fs = pcore_len_px(style, flen, funit);
                    }
                    f = pcore_make_font(fs);
                    old = (HFONT) SelectObject(hdc, f);
                    lh = (fs * 12) / 10;
                    if (GetTextMetricsW(hdc, &tm)) {
                        lh = tm.tmHeight + tm.tmExternalLeading;
                    }
                    (void) css_computed_color(style, &col);
                    SetTextColor(hdc, pcore_argb_to_colorref(col));
                    SetBkMode(hdc, TRANSPARENT);
                    pcore_text_lines(hdc, wbuf, wlen, box->w, 1,
                            box->x - sx, box->y - sy, lh);
                    SelectObject(hdc, old);
                    DeleteObject(f);
                }
            }
            dom_string_unref(text);
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
