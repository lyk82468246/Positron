/*
 * pcore_form.c - shared form-owner and form-control traversal primitives.
 *
 * libdom stores a parser-time ancestor form pointer on each control. That is
 * sufficient for its own collection helper, but it cannot reflect a live
 * `form="id"` attribute or a control outside the form subtree. Positron's
 * public form, validation, submission and reset paths therefore share the
 * DOM-derived rules in this file. The visitor observes controls in document
 * order while each node is retained only for the duration of the callback.
 *
 * C89 only.
 */

#include <string.h>
#include <stdlib.h>

#include <dom/dom.h>

#include "pcore_internal.h"

static int pcore_form_node_name_is(dom_node *node, const char *name);

typedef struct pcore_output_state {
    dom_string *default_override;
} pcore_output_state;

static dom_string *pcore_output_state_key = NULL;

static int pcore_output_state_key_ensure(void)
{
    static const char *name = "__pcore_output_state__";

    if (pcore_output_state_key != NULL) {
        return 0;
    }
    if (dom_string_create((const uint8_t *) name, strlen(name),
            &pcore_output_state_key) != DOM_NO_ERR) {
        return 1;
    }
    return 0;
}

static void pcore_output_state_handler(dom_node_operation operation,
        dom_string *key, void *data, dom_node *source, dom_node *target)
{
    pcore_output_state *state;

    (void) key;
    (void) source;
    (void) target;
    if (operation != DOM_NODE_DELETED || data == NULL) {
        return;
    }
    state = (pcore_output_state *) data;
    if (state->default_override != NULL) {
        dom_string_unref(state->default_override);
    }
    free(state);
}

static pcore_output_state *pcore_output_state_get(dom_element *output,
        int create)
{
    pcore_output_state *state;
    void *data;
    void *old;

    if (output == NULL || !pcore_form_node_name_is((dom_node *) output,
            "output") || pcore_output_state_key_ensure() != 0) {
        return NULL;
    }
    data = NULL;
    if (dom_node_get_user_data((dom_node *) output,
            pcore_output_state_key, &data) != DOM_NO_ERR) {
        return NULL;
    }
    if (data != NULL || !create) {
        return (pcore_output_state *) data;
    }
    state = (pcore_output_state *) calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }
    old = NULL;
    if (dom_node_set_user_data((dom_node *) output,
            pcore_output_state_key, state,
            pcore_output_state_handler, &old) != DOM_NO_ERR) {
        free(state);
        return NULL;
    }
    if (old != NULL && old != state) {
        pcore_output_state_handler(DOM_NODE_DELETED,
                pcore_output_state_key, old,
                (dom_node *) output, NULL);
    }
    return state;
}

static int pcore_output_string_copy(dom_string *source,
        dom_string **out_copy)
{
    const char *data;
    size_t length;

    if (out_copy == NULL) {
        return 1;
    }
    *out_copy = NULL;
    data = "";
    length = 0;
    if (source != NULL) {
        data = dom_string_data(source);
        length = dom_string_byte_length(source);
        if (data == NULL && length != 0) {
            return 1;
        }
        if (data == NULL) {
            data = "";
            length = 0;
        }
    }
    if (dom_string_create((const uint8_t *) data, length,
            out_copy) != DOM_NO_ERR || *out_copy == NULL) {
        return 1;
    }
    return 0;
}

static int pcore_output_descendant_text(dom_element *output,
        dom_string **out_value)
{
    dom_string *text;

    if (out_value == NULL || output == NULL ||
            !pcore_form_node_name_is((dom_node *) output, "output")) {
        return 1;
    }
    *out_value = NULL;
    text = NULL;
    if (dom_node_get_text_content((dom_node *) output, &text) != DOM_NO_ERR) {
        return 1;
    }
    if (text == NULL && pcore_output_string_copy(NULL, &text) != 0) {
        return 1;
    }
    *out_value = text;
    return 0;
}

int pcore_output_get_value(dom_element *output, dom_string **out_value)
{
    return pcore_output_descendant_text(output, out_value);
}

int pcore_output_get_default_value(dom_element *output,
        dom_string **out_value)
{
    pcore_output_state *state;

    if (out_value == NULL) {
        return 1;
    }
    *out_value = NULL;
    state = pcore_output_state_get(output, 0);
    if (state != NULL && state->default_override != NULL) {
        return pcore_output_string_copy(state->default_override, out_value);
    }
    return pcore_output_descendant_text(output, out_value);
}

int pcore_output_set_value(dom_element *output, const char *value)
{
    pcore_output_state *state;
    dom_string *new_value;
    dom_string *default_value;
    dom_string *new_override;
    dom_exception err;

    if (output == NULL || value == NULL ||
            !pcore_form_node_name_is((dom_node *) output, "output")) {
        return 1;
    }
    state = pcore_output_state_get(output, 1);
    if (state == NULL) {
        return 1;
    }
    new_value = NULL;
    if (dom_string_create((const uint8_t *) value, strlen(value),
            &new_value) != DOM_NO_ERR || new_value == NULL) {
        return 1;
    }
    default_value = NULL;
    new_override = NULL;
    if (state->default_override == NULL) {
        if (pcore_output_descendant_text(output, &default_value) != 0 ||
                pcore_output_string_copy(default_value,
                &new_override) != 0) {
            if (default_value != NULL) {
                dom_string_unref(default_value);
            }
            dom_string_unref(new_value);
            return 1;
        }
    }
    err = dom_node_set_text_content((dom_node *) output, new_value);
    if (default_value != NULL) {
        dom_string_unref(default_value);
    }
    dom_string_unref(new_value);
    if (err != DOM_NO_ERR) {
        if (new_override != NULL) {
            dom_string_unref(new_override);
        }
        return 1;
    }
    if (new_override != NULL) {
        state->default_override = new_override;
    }
    return 0;
}

int pcore_output_set_default_value(dom_element *output, const char *value)
{
    pcore_output_state *state;
    dom_string *new_value;
    dom_string *old_value;
    dom_exception err;

    if (output == NULL || value == NULL ||
            !pcore_form_node_name_is((dom_node *) output, "output")) {
        return 1;
    }
    state = pcore_output_state_get(output, 1);
    if (state == NULL || dom_string_create((const uint8_t *) value,
            strlen(value), &new_value) != DOM_NO_ERR || new_value == NULL) {
        return 1;
    }
    if (state->default_override == NULL) {
        err = dom_node_set_text_content((dom_node *) output, new_value);
        dom_string_unref(new_value);
        return err == DOM_NO_ERR ? 0 : 1;
    }
    old_value = state->default_override;
    state->default_override = new_value;
    dom_string_unref(old_value);
    return 0;
}

int pcore_output_reset(dom_node *output)
{
    pcore_output_state *state;
    dom_exception err;

    if (output == NULL || !pcore_form_node_name_is(output, "output")) {
        return 1;
    }
    state = pcore_output_state_get((dom_element *) output, 0);
    if (state == NULL || state->default_override == NULL) {
        return 0;
    }
    err = dom_node_set_text_content(output, state->default_override);
    if (err != DOM_NO_ERR) {
        return 1;
    }
    dom_string_unref(state->default_override);
    state->default_override = NULL;
    return 0;
}

static int pcore_form_node_name_is(dom_node *node, const char *name)
{
    dom_string *actual;
    dom_string *expected;
    int same;

    actual = NULL;
    expected = NULL;
    same = 0;
    if (node == NULL || name == NULL ||
            dom_node_get_node_name(node, &actual) != DOM_NO_ERR ||
            actual == NULL ||
            dom_string_create((const uint8_t *) name, strlen(name),
                    &expected) != DOM_NO_ERR || expected == NULL) {
        if (actual != NULL) {
            dom_string_unref(actual);
        }
        if (expected != NULL) {
            dom_string_unref(expected);
        }
        return 0;
    }
    same = dom_string_caseless_isequal(actual, expected) ? 1 : 0;
    dom_string_unref(expected);
    dom_string_unref(actual);
    return same;
}

static int pcore_form_control_is(dom_node *node)
{
    return pcore_form_node_name_is(node, "input") ||
            pcore_form_node_name_is(node, "select") ||
            pcore_form_node_name_is(node, "textarea") ||
            pcore_form_node_name_is(node, "button");
}

static int pcore_form_associated_is(dom_node *node)
{
    return pcore_form_control_is(node) ||
            pcore_form_node_name_is(node, "object") ||
            pcore_form_node_name_is(node, "output");
}

static int pcore_form_attribute_value(dom_node *node, const char *name,
        dom_string **out_value)
{
    dom_string *attribute;
    int result;

    if (out_value == NULL) {
        return 1;
    }
    *out_value = NULL;
    attribute = NULL;
    if (node == NULL || name == NULL ||
            dom_string_create((const uint8_t *) name, strlen(name),
                    &attribute) != DOM_NO_ERR || attribute == NULL) {
        if (attribute != NULL) {
            dom_string_unref(attribute);
        }
        return 1;
    }
    result = dom_element_get_attribute((dom_element *) node, attribute,
            out_value) == DOM_NO_ERR ? 0 : 1;
    dom_string_unref(attribute);
    return result;
}

int pcore_form_control_owner(dom_document *doc, dom_node *control,
        dom_element **out_owner, int *out_has_attribute)
{
    dom_string *form_value;
    dom_element *candidate;
    dom_node *current;
    dom_node *parent;
    dom_node_type type;
    const char *data;
    int result;

    if (out_owner != NULL) {
        *out_owner = NULL;
    }
    if (out_has_attribute != NULL) {
        *out_has_attribute = 0;
    }
    if (doc == NULL || control == NULL || out_owner == NULL ||
            out_has_attribute == NULL || !pcore_form_associated_is(control)) {
        return 1;
    }
    form_value = NULL;
    result = pcore_form_attribute_value(control, "form", &form_value);
    if (result != 0) {
        return 1;
    }
    if (form_value != NULL) {
        *out_has_attribute = 1;
        data = dom_string_data(form_value);
        candidate = NULL;
        if (data != NULL && data[0] != '\0' &&
                dom_document_get_element_by_id(doc, form_value,
                        &candidate) == DOM_NO_ERR && candidate != NULL &&
                pcore_form_node_name_is((dom_node *) candidate, "form")) {
            *out_owner = candidate;
        } else if (candidate != NULL) {
            dom_node_unref((dom_node *) candidate);
        }
        dom_string_unref(form_value);
        return 0;
    }

    current = dom_node_ref(control);
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
                pcore_form_node_name_is(current, "form")) {
            *out_owner = (dom_element *) current;
            return 0;
        }
    }
    return 0;
}

static int pcore_form_walk_controls(dom_node *node, dom_document *doc,
        dom_element *form, pcore_form_control_visit_fn visit, void *pw,
        int *stopped)
{
    dom_node *child;
    dom_node *next;
    dom_element *owner;
    dom_node_type type;
    int has_attribute;
    int callback_result;
    int result;
    int matches;

    if (node == NULL || doc == NULL || form == NULL || visit == NULL ||
            stopped == NULL) {
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
        if (type == DOM_ELEMENT_NODE && pcore_form_control_is(child)) {
            owner = NULL;
            has_attribute = 0;
            result = pcore_form_control_owner(doc, child, &owner,
                    &has_attribute);
            (void) has_attribute;
            if (result != 0) {
                dom_node_unref(child);
                return 1;
            }
            matches = owner == form ? 1 : 0;
            if (owner != NULL) {
                dom_node_unref((dom_node *) owner);
            }
            if (matches) {
                callback_result = visit(child, pw);
                if (callback_result < 0) {
                    dom_node_unref(child);
                    return 1;
                }
                if (callback_result > 0) {
                    *stopped = 1;
                    dom_node_unref(child);
                    return 0;
                }
            }
        }
        if (type == DOM_ELEMENT_NODE && !*stopped) {
            result = pcore_form_walk_controls(child, doc, form, visit,
                    pw, stopped);
            if (result != 0 || *stopped) {
                dom_node_unref(child);
                return result;
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

int pcore_form_controls_visit(dom_document *doc, dom_element *form,
        pcore_form_control_visit_fn visit, void *pw)
{
    dom_element *root;
    int stopped;
    int result;

    if (doc == NULL || form == NULL || visit == NULL ||
            !pcore_form_node_name_is((dom_node *) form, "form")) {
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
    stopped = 0;
    result = pcore_form_walk_controls((dom_node *) root, doc, form, visit,
            pw, &stopped);
    dom_node_unref((dom_node *) root);
    return result;
}

static int pcore_form_walk_outputs(dom_node *node, dom_document *doc,
        dom_element *form, pcore_form_control_visit_fn visit, void *pw,
        int *stopped)
{
    dom_node *child;
    dom_node *next;
    dom_element *owner;
    dom_node_type type;
    int has_attribute;
    int callback_result;
    int result;
    int matches;

    if (node == NULL || doc == NULL || form == NULL || visit == NULL ||
            stopped == NULL) {
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
        if (type == DOM_ELEMENT_NODE &&
                pcore_form_node_name_is(child, "output")) {
            owner = NULL;
            has_attribute = 0;
            result = pcore_form_control_owner(doc, child, &owner,
                    &has_attribute);
            (void) has_attribute;
            if (result != 0) {
                dom_node_unref(child);
                return 1;
            }
            matches = owner == form ? 1 : 0;
            if (owner != NULL) {
                dom_node_unref((dom_node *) owner);
            }
            if (matches) {
                callback_result = visit(child, pw);
                if (callback_result < 0) {
                    dom_node_unref(child);
                    return 1;
                }
                if (callback_result > 0) {
                    *stopped = 1;
                    dom_node_unref(child);
                    return 0;
                }
            }
        }
        if (type == DOM_ELEMENT_NODE && !*stopped) {
            result = pcore_form_walk_outputs(child, doc, form, visit,
                    pw, stopped);
            if (result != 0 || *stopped) {
                dom_node_unref(child);
                return result;
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

int pcore_form_outputs_visit(dom_document *doc, dom_element *form,
        pcore_form_control_visit_fn visit, void *pw)
{
    dom_element *root;
    int stopped;
    int result;

    if (doc == NULL || form == NULL || visit == NULL ||
            !pcore_form_node_name_is((dom_node *) form, "form")) {
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
    stopped = 0;
    result = pcore_form_walk_outputs((dom_node *) root, doc, form, visit,
            pw, &stopped);
    dom_node_unref((dom_node *) root);
    return result;
}
