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
