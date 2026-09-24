/*
 * positron_app/app_script.c - private Browser ScriptSession adapter.
 */

#include <stdlib.h>
#include <string.h>

#include "app_script.h"
#include "positron_script.h"

#define APP_SCRIPT_EVENT_MAX          64
#define APP_SCRIPT_MAX_SOURCE_BYTES   (64 * 1024)
#define APP_SCRIPT_NO_SELECT_KEY      0xffffffffUL

typedef struct AppScriptEventBinding AppScriptEventBinding;

struct AppScriptEventBinding {
    AppScriptEventBinding *next;
    AppScriptContext *context;
    HANDLE core_listener;
    unsigned int script_listener;
    char element_id[APP_SCRIPT_URL_MAX];
    char event_type[APP_SCRIPT_REL_MAX];
};

struct AppScriptContext {
    HANDLE document;
    HANDLE session;
    char document_url[APP_SCRIPT_URL_MAX];
    AppScriptHostCallbacks callbacks;
    AppScriptEventBinding *events;
    unsigned int next_listener;
    AppScriptPendingNavigation pending_navigation;
    int history_length;
    int history_index;
    int history_can_commit;
    int viewport_width;
    int viewport_height;
    int dpi;
    unsigned int native_select_key_index;
    char native_edit_target_id[APP_SCRIPT_URL_MAX];
};

static void app_script_copy_text(char *target, int capacity,
        const char *source)
{
    int length;

    if (target == NULL || capacity <= 0) {
        return;
    }
    target[0] = '\0';
    if (source == NULL || capacity == 1) {
        return;
    }
    length = (int) strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(target, source, (size_t) length);
    target[length] = '\0';
}

static int app_script_copy_bounded(char *target, int capacity,
        const char *source)
{
    if (target == NULL || capacity <= 0 || source == NULL ||
            (int) strlen(source) >= capacity) {
        return 1;
    }
    app_script_copy_text(target, capacity, source);
    return 0;
}

static int app_script_mutation_result(AppScriptContext *context, int result)
{
    if (result == 0) {
        if (context != NULL && context->callbacks.mutation != NULL) {
            context->callbacks.mutation(context->callbacks.pw, context);
        }
        return 1;
    }
    if (result == 2) {
        return 0;
    }
    return -1;
}

static int app_script_has_element(void *pw, const char *id)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            id[0] == '\0') {
        return -1;
    }
    return PCore_NodeExistsById(context->document, id);
}

static int app_script_get_text(void *pw, const char *id, char *out_text,
        int out_capacity, int *out_len)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_len == NULL || out_capacity < 0 ||
            (out_text == NULL && out_capacity != 0) ||
            (out_text != NULL && out_capacity <= 0)) {
        return -1;
    }
    return PCore_NodeTextContentById(context->document, id, out_text,
            out_capacity, out_len) == 0 ? 0 : -1;
}

static int app_script_get_content_editable(void *pw, const char *id,
        int *out_editable)
{
    AppScriptContext *context;
    PCoreContentEditableInfo info;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_editable == NULL) {
        return -1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (PCore_ContentEditableInfoById(context->document, id, &info) != 0) {
        return -1;
    }
    *out_editable = info.editable ? 1 : 0;
    return *out_editable;
}

static int app_script_get_content_editable_selection(void *pw,
        const char *id, int *out_start, int *out_end, int *out_direction)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || id == NULL || out_start == NULL ||
            out_end == NULL || out_direction == NULL) {
        return -1;
    }
    if (context->callbacks.get_contenteditable_selection == NULL) {
        return 0;
    }
    return context->callbacks.get_contenteditable_selection(
            context->callbacks.pw, context, id, out_start, out_end,
            out_direction);
}

static int app_script_set_content_editable_selection(void *pw,
        const char *id, int start, int end, int direction)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || id == NULL || start < 0 || end < 0 ||
            direction < PBROWSER_SCRIPT_CONTENT_SELECTION_NONE ||
            direction > PBROWSER_SCRIPT_CONTENT_SELECTION_BACKWARD) {
        return -1;
    }
    if (context->callbacks.set_contenteditable_selection == NULL) {
        return 0;
    }
    return context->callbacks.set_contenteditable_selection(
            context->callbacks.pw, context, id, start, end, direction);
}

static int app_script_set_content_editable_text(void *pw, const char *id,
        const char *text)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            text == NULL) {
        return -1;
    }
    result = PCore_ContentEditableSetTextById(context->document, id, text);
    if (result == 0) {
        return app_script_mutation_result(context, 0);
    }
    if (result == 1 || result == 2) {
        return 0;
    }
    return -1;
}

static int app_script_get_document_title(void *pw, char *out_text,
        int out_capacity, int *out_len)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || out_len == NULL ||
            out_capacity < 0 ||
            (out_text == NULL && out_capacity != 0) ||
            (out_text != NULL && out_capacity <= 0)) {
        return -1;
    }
    return PCore_DocumentTitle(context->document, out_text, out_capacity,
            out_len) == 0 ? 0 : -1;
}

static int app_script_get_relation(void *pw, const char *id,
        unsigned int relation, unsigned int index, char *out_value,
        int out_capacity, int *out_bytes, int *out_number)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_bytes == NULL || out_number == NULL || out_capacity < 0 ||
            (out_value == NULL && out_capacity != 0) ||
            (out_value != NULL && out_capacity <= 0)) {
        return -1;
    }
    return PCore_NodeRelationById(context->document, id, relation, index,
            out_value, out_capacity, out_bytes, out_number);
}

static int app_script_set_text(void *pw, const char *id, const char *text)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            text == NULL) {
        return -1;
    }
    result = PCore_NodeSetTextContentById(context->document, id, text);
    return app_script_mutation_result(context, result);
}

static int app_script_set_document_title(void *pw, const char *text)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || text == NULL) {
        return -1;
    }
    result = PCore_DocumentSetTitle(context->document, text);
    return app_script_mutation_result(context, result);
}

static int app_script_get_value(void *pw, const char *id, char *out_value,
        int out_capacity, int *out_len)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_len == NULL || out_capacity < 0 ||
            (out_value == NULL && out_capacity != 0) ||
            (out_value != NULL && out_capacity <= 0)) {
        return -1;
    }
    return PCore_NodeValueById(context->document, id, out_value,
            out_capacity, out_len) == 0 ? 0 : -1;
}

static int app_script_set_value(void *pw, const char *id, const char *value)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            value == NULL) {
        return -1;
    }
    result = PCore_NodeSetValueById(context->document, id, value);
    return app_script_mutation_result(context, result);
}

static int app_script_get_default_value(void *pw, const char *id,
        char *out_value, int out_capacity, int *out_len)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_len == NULL || out_capacity < 0 ||
            (out_value == NULL && out_capacity != 0) ||
            (out_value != NULL && out_capacity <= 0)) {
        return -1;
    }
    return PCore_NodeDefaultValueById(context->document, id, out_value,
            out_capacity, out_len) == 0 ? 0 : -1;
}

static int app_script_set_default_value(void *pw, const char *id,
        const char *value)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            value == NULL) {
        return -1;
    }
    result = PCore_NodeSetDefaultValueById(context->document, id, value);
    return app_script_mutation_result(context, result);
}

static int app_script_get_checked(void *pw, const char *id,
        int *out_checked)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_checked == NULL) {
        return -1;
    }
    return PCore_NodeCheckedById(context->document, id, out_checked) == 0 ?
            0 : -1;
}

static int app_script_set_checked(void *pw, const char *id, int checked)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL) {
        return -1;
    }
    result = PCore_NodeSetCheckedById(context->document, id,
            checked ? 1 : 0);
    return app_script_mutation_result(context, result);
}

static int app_script_get_default_checked(void *pw, const char *id,
        int *out_checked)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_checked == NULL) {
        return -1;
    }
    return PCore_NodeDefaultCheckedById(context->document, id,
            out_checked) == 0 ? 0 : -1;
}

static int app_script_set_default_checked(void *pw, const char *id,
        int checked)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL) {
        return -1;
    }
    result = PCore_NodeSetDefaultCheckedById(context->document, id,
            checked ? 1 : 0);
    return app_script_mutation_result(context, result);
}

static int app_script_get_selected_index(void *pw, const char *id,
        int *out_index)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_index == NULL) {
        return -1;
    }
    return PCore_NodeSelectedIndexById(context->document, id, out_index) == 0 ?
            0 : -1;
}

static int app_script_set_selected_index(void *pw, const char *id,
        int index)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL) {
        return -1;
    }
    result = PCore_NodeSetSelectedIndexById(context->document, id, index);
    return app_script_mutation_result(context, result);
}

static int app_script_get_selected(void *pw, const char *id,
        int *out_selected)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_selected == NULL) {
        return -1;
    }
    return PCore_NodeSelectedById(context->document, id, out_selected) == 0 ?
            0 : -1;
}

static int app_script_set_selected(void *pw, const char *id, int selected)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL) {
        return -1;
    }
    result = PCore_NodeSetSelectedById(context->document, id,
            selected ? 1 : 0);
    return app_script_mutation_result(context, result);
}

static int app_script_get_default_selected(void *pw, const char *id,
        int *out_selected)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            out_selected == NULL) {
        return -1;
    }
    return PCore_NodeDefaultSelectedById(context->document, id,
            out_selected) == 0 ? 0 : -1;
}

static int app_script_set_default_selected(void *pw, const char *id,
        int selected)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL) {
        return -1;
    }
    result = PCore_NodeSetDefaultSelectedById(context->document, id,
            selected ? 1 : 0);
    return app_script_mutation_result(context, result);
}

static int app_script_remove_child(void *pw, const char *parent_id,
        const char *child_id)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || parent_id == NULL ||
            child_id == NULL) {
        return -1;
    }
    result = PCore_NodeRemoveChildById(context->document, parent_id,
            child_id);
    return app_script_mutation_result(context, result);
}

static int app_script_set_attribute(void *pw, const char *id,
        const char *name, const char *value)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            name == NULL || value == NULL) {
        return -1;
    }
    result = PCore_NodeSetAttributeById(context->document, id, name, value);
    return app_script_mutation_result(context, result);
}

static int app_script_remove_attribute(void *pw, const char *id,
        const char *name)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            name == NULL) {
        return -1;
    }
    result = PCore_NodeRemoveAttributeById(context->document, id, name);
    return app_script_mutation_result(context, result);
}

static int app_script_get_attribute(void *pw, const char *id,
        const char *name, char *out_value, int out_capacity, int *out_len)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || id == NULL ||
            name == NULL || out_len == NULL || out_capacity < 0 ||
            (out_value == NULL && out_capacity != 0) ||
            (out_value != NULL && out_capacity <= 0)) {
        return -1;
    }
    result = PCore_NodeAttributeById(context->document, id, name,
            out_value, out_capacity, out_len);
    if (result == 0) {
        return 0;
    }
    return result == 2 ? 1 : -1;
}

static int app_script_document_write(void *pw, unsigned int script_index,
        const char *html, int append_newline)
{
    AppScriptContext *context;
    char *fragment;
    int html_length;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || html == NULL) {
        return -1;
    }
    html_length = (int) strlen(html);
    fragment = (char *) malloc((size_t) html_length +
            (append_newline ? 2U : 1U));
    if (fragment == NULL) {
        return -1;
    }
    memcpy(fragment, html, (size_t) html_length);
    if (append_newline) {
        fragment[html_length++] = '\n';
    }
    fragment[html_length] = '\0';
    result = PCore_NodeInsertHTMLAfterScriptByIndex(context->document,
            script_index, fragment);
    free(fragment);
    return app_script_mutation_result(context, result);
}

static const char *app_script_active_element(void *pw)
{
    AppScriptContext *context;
    static char id[PBROWSER_SCRIPT_ACTIVE_ELEMENT_ID_MAX];
    int bytes;

    context = (AppScriptContext *) pw;
    id[0] = '\0';
    if (context == NULL || context->document == NULL) {
        return NULL;
    }
    bytes = 0;
    if (PCore_InteractionFocusElementId(context->document, id,
            sizeof(id), &bytes) != 0 || bytes <= 0) {
        return NULL;
    }
    return id;
}

static const char *app_script_interaction_element(void *pw,
        const char *state)
{
    AppScriptContext *context;
    static char id[PBROWSER_SCRIPT_ACTIVE_ELEMENT_ID_MAX];
    unsigned int state_flag;
    int bytes;

    context = (AppScriptContext *) pw;
    id[0] = '\0';
    if (context == NULL || context->document == NULL || state == NULL) {
        return NULL;
    }
    if (strcmp(state, PBROWSER_SCRIPT_INTERACTION_ACTIVE) == 0) {
        state_flag = PCORE_INTERACTION_ACTIVE;
    } else if (strcmp(state, PBROWSER_SCRIPT_INTERACTION_HOVER) == 0) {
        state_flag = PCORE_INTERACTION_HOVER;
    } else {
        return NULL;
    }
    bytes = 0;
    if (PCore_InteractionStateElementId(context->document, state_flag,
            id, sizeof(id), &bytes) != 0 || bytes <= 0) {
        return NULL;
    }
    return id;
}

static int app_script_focus_request(void *pw,
        const PBrowserScriptFocusRequestInfo *info)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            info->size < sizeof(PBrowserScriptFocusRequestInfo) ||
            info->element_id == NULL || info->element_id[0] == '\0') {
        return -1;
    }
    if (!info->focused) {
        result = PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS);
        if (result < 0) {
            return -1;
        }
    } else if (PCore_InteractionFocusById(context->document,
            info->element_id) < 0) {
        return -1;
    }
    if (context->callbacks.mutation != NULL) {
        context->callbacks.mutation(context->callbacks.pw, context);
    }
    return 0;
}

static unsigned int app_script_event_callback(void *pw,
        const PCoreEventInfo *event_info)
{
    AppScriptEventBinding *binding;
    PBrowserScriptEventInfo browser_event;
    unsigned int action;

    binding = (AppScriptEventBinding *) pw;
    if (binding == NULL || binding->context == NULL ||
            binding->context->session == NULL || event_info == NULL) {
        return PCORE_EVENT_ACTION_NONE;
    }
    memset(&browser_event, 0, sizeof(browser_event));
    browser_event.size = sizeof(browser_event);
    browser_event.phase = event_info->phase;
    browser_event.bubbles = event_info->bubbles;
    browser_event.cancelable = event_info->cancelable;
    browser_event.trusted = event_info->trusted;
    browser_event.default_prevented = event_info->default_prevented;
    browser_event.key = event_info->key;
    browser_event.key_code = event_info->key_code;
    browser_event.char_code = event_info->char_code;
    browser_event.repeat = event_info->repeat;
    browser_event.shift = event_info->shift;
    browser_event.ctrl = event_info->ctrl;
    browser_event.alt = event_info->alt;
    browser_event.input_type = event_info->input_type;
    browser_event.data = event_info->data;
    browser_event.is_composing = event_info->is_composing;
    browser_event.target_id = event_info->target_id;
    browser_event.current_target_id = event_info->current_target_id;
    action = PBrowser_ScriptSessionDispatchEvent(
            binding->context->session, binding->script_listener,
            binding->event_type, &browser_event);
    return (action & PBROWSER_SCRIPT_EVENT_ACTION_PREVENT_DEFAULT) != 0 ?
            PCORE_EVENT_ACTION_PREVENT_DEFAULT : PCORE_EVENT_ACTION_NONE;
}

static unsigned int app_script_add_event_listener(void *pw,
        const char *element_id, const char *event_type, int capture)
{
    AppScriptContext *context;
    AppScriptEventBinding *binding;
    HANDLE listener;
    unsigned int script_listener;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL ||
            context->session == NULL || element_id == NULL ||
            element_id[0] == '\0' || event_type == NULL ||
            event_type[0] == '\0') {
        return 0;
    }
    binding = (AppScriptEventBinding *) calloc(1, sizeof(*binding));
    if (binding == NULL ||
            app_script_copy_bounded(binding->element_id,
            sizeof(binding->element_id), element_id) != 0 ||
            app_script_copy_bounded(binding->event_type,
            sizeof(binding->event_type), event_type) != 0) {
        free(binding);
        return 0;
    }
    binding->context = context;
    script_listener = context->next_listener++;
    if (script_listener == 0) {
        script_listener = context->next_listener++;
    }
    binding->script_listener = script_listener;
    listener = PCore_EventListenerAdd(context->document, binding->element_id,
            binding->event_type, capture, app_script_event_callback, binding);
    if (listener == NULL) {
        free(binding);
        return 0;
    }
    binding->core_listener = listener;
    binding->next = context->events;
    context->events = binding;
    return script_listener;
}

static int app_script_remove_event_listener(void *pw,
        unsigned int script_listener)
{
    AppScriptContext *context;
    AppScriptEventBinding *binding;
    AppScriptEventBinding *previous;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL ||
            script_listener == 0) {
        return 0;
    }
    previous = NULL;
    binding = context->events;
    while (binding != NULL && binding->script_listener != script_listener) {
        previous = binding;
        binding = binding->next;
    }
    if (binding == NULL) {
        return 0;
    }
    if (previous == NULL) {
        context->events = binding->next;
    } else {
        previous->next = binding->next;
    }
    PCore_EventListenerRemove(context->document, binding->core_listener);
    free(binding);
    return 1;
}

static int app_script_navigate(void *pw,
        const PBrowserScriptNavigationInfo *info, int *out_value)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || info == NULL || out_value == NULL ||
            info->size < sizeof(*info)) {
        return -1;
    }
    if (info->kind == PBROWSER_SCRIPT_NAVIGATION_REPLACE_STATE ||
            info->kind == PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE) {
        return context->callbacks.navigate == NULL ? 0 :
                context->callbacks.navigate(context->callbacks.pw,
                context, info, out_value);
    }
    if (AppScript_QueueNavigation(context, info) != 0) {
        return 0;
    }
    if (context->callbacks.navigate != NULL) {
        result = context->callbacks.navigate(context->callbacks.pw, context,
                info, out_value);
        if (result <= 0) {
            AppScript_ClearPendingNavigation(context);
        }
        return result;
    }
    return 1;
}

static int app_script_scroll(void *pw,
        const PBrowserScriptScrollInfo *info, int *out_x, int *out_y)
{
    AppScriptContext *context;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->callbacks.scroll == NULL) {
        if (info == NULL || out_x == NULL || out_y == NULL) {
            return -1;
        }
        *out_x = info->scroll_x;
        *out_y = info->scroll_y;
        return 0;
    }
    return context->callbacks.scroll(context->callbacks.pw, context, info,
            out_x, out_y);
}

static int app_script_input_dispatch(void *pw,
        const PBrowserScriptInputEventInfo *info, int *out_default_allowed)
{
    AppScriptContext *context;
    PCoreInputEventDataEx data;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            info->size < sizeof(*info) || info->event_type == NULL ||
            info->event_type[0] == '\0' || info->input_type == NULL ||
            info->data == NULL || out_default_allowed == NULL) {
        return -1;
    }
    memset(&data, 0, sizeof(data));
    data.struct_size = sizeof(data);
    data.input_type = info->input_type;
    data.data = info->data;
    data.is_composing = info->is_composing ? 1 : 0;
    if (context->native_edit_target_id[0] != '\0') {
        result = PCore_EventDispatchInputExToId(context->document,
                context->native_edit_target_id, info->event_type,
                info->bubbles ? 1 : 0, info->cancelable ? 1 : 0, &data,
                out_default_allowed);
    } else {
        result = PCore_EventDispatchInputExAt(context->document, info->x,
                info->y, info->event_type, info->bubbles ? 1 : 0,
                info->cancelable ? 1 : 0, &data, out_default_allowed);
    }
    return result < 0 ? -1 : 0;
}

static int app_script_edit_dispatch(void *pw,
        const PBrowserScriptEditEventInfo *info)
{
    AppScriptContext *context;
    int default_allowed;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            info->size < sizeof(*info) || info->event_type == NULL ||
            info->event_type[0] == '\0') {
        return -1;
    }
    default_allowed = 1;
    result = PCore_EventDispatchAt(context->document, info->x, info->y,
            info->event_type, info->bubbles ? 1 : 0,
            info->cancelable ? 1 : 0, &default_allowed);
    return result < 0 ? -1 : 0;
}

static int app_script_key_dispatch(void *pw,
        const PBrowserScriptKeyEventInfo *info, int *out_default_allowed)
{
    AppScriptContext *context;
    PCoreKeyEventDataEx data;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            out_default_allowed == NULL || info->size < sizeof(*info) ||
            info->event_type == NULL || info->event_type[0] == '\0' ||
            info->key == NULL || info->key[0] == '\0') {
        return -1;
    }
    memset(&data, 0, sizeof(data));
    data.struct_size = sizeof(data);
    data.key = info->key;
    data.key_code = info->key_code;
    data.char_code = info->char_code;
    data.repeat = info->repeat ? 1 : 0;
    data.shift = info->shift ? 1 : 0;
    data.ctrl = info->ctrl ? 1 : 0;
    data.alt = info->alt ? 1 : 0;
    data.is_composing = info->is_composing ? 1 : 0;
    *out_default_allowed = 1;
    if (context->native_select_key_index != APP_SCRIPT_NO_SELECT_KEY) {
        result = PCore_EventDispatchKeyExToSelectIndex(context->document,
                context->native_select_key_index, info->event_type,
                info->bubbles ? 1 : 0, info->cancelable ? 1 : 0, &data,
                out_default_allowed);
    } else {
        result = PCore_EventDispatchKeyExAt(context->document, info->x,
                info->y, info->event_type, info->bubbles ? 1 : 0,
                info->cancelable ? 1 : 0, &data, out_default_allowed);
    }
    return result < 0 ? -1 : 0;
}

static int app_script_focus_dispatch(void *pw,
        const PBrowserScriptFocusEventInfo *info)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            info->size < sizeof(*info) || info->event_type == NULL ||
            info->event_type[0] == '\0') {
        return -1;
    }
    result = PCore_EventDispatchAt(context->document, info->x, info->y,
            info->event_type, info->bubbles ? 1 : 0,
            info->cancelable ? 1 : 0, NULL);
    return result < 0 ? -1 : 0;
}

static int app_script_native_select_dispatch(void *pw,
        const PBrowserScriptNativeSelectEventInfo *info)
{
    AppScriptContext *context;
    int default_allowed;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            info->size < sizeof(*info) || info->event_type == NULL ||
            info->event_type[0] == '\0') {
        return -1;
    }
    default_allowed = 1;
    result = PCore_EventDispatchAt(context->document, info->x, info->y,
            info->event_type, info->bubbles ? 1 : 0,
            info->cancelable ? 1 : 0, &default_allowed);
    return result < 0 ? -1 : 0;
}

static int app_script_click_dispatch(void *pw,
        const PBrowserScriptClickEventInfo *info, int *out_default_allowed)
{
    AppScriptContext *context;
    int result;

    context = (AppScriptContext *) pw;
    if (context == NULL || context->document == NULL || info == NULL ||
            info->size < sizeof(*info) || info->event_type == NULL ||
            strcmp(info->event_type, "click") != 0 ||
            out_default_allowed == NULL) {
        return -1;
    }
    *out_default_allowed = 1;
    result = PCore_EventDispatchAt(context->document, info->x, info->y,
            info->event_type, info->bubbles ? 1 : 0,
            info->cancelable ? 1 : 0, out_default_allowed);
    return result < 0 ? -1 : 0;
}

static int app_script_register_callbacks(AppScriptContext *context)
{
    PBrowserScriptDomReadCallbacksEx dom_read;
    PBrowserScriptDomRelationCallbacks relation;
    PBrowserScriptDomWriteCallbacksEx13 write;
    PBrowserScriptContentEditableCallbacks content_editable;
    PBrowserScriptContentEditableSelectionCallbacks content_selection;
    PBrowserScriptDocumentWriteCallbacks document_write;
    PBrowserScriptDomMutationCallbacks mutation;
    PBrowserScriptDomAttributeCallbacks attribute;
    PBrowserScriptDomValueCallbacks value;
    PBrowserScriptDomCheckedCallbacks checked;
    PBrowserScriptFormCallbacks form;
    PBrowserScriptOptionCallbacks option;
    PBrowserScriptEventCallbacks event_callbacks;
    PBrowserScriptNavigationCallbacks navigation;
    PBrowserScriptScrollCallbacks scroll;
    PBrowserScriptActiveElementCallbacks active;
    PBrowserScriptInteractionCallbacks interaction;
    PBrowserScriptFocusRequestCallbacks focus_request;
    PBrowserScriptInputCallbacks input_callbacks;
    PBrowserScriptKeyCallbacks key_callbacks;
    PBrowserScriptFocusCallbacks focus_callbacks;
    PBrowserScriptEditCallbacks edit_callbacks;
    PBrowserScriptNativeEditCallbacksEx native_edit_callbacks;
    PBrowserScriptNativeSelectCallbacksEx native_select_callbacks;
    PBrowserScriptClickCallbacks click_callbacks;

    memset(&dom_read, 0, sizeof(dom_read));
    dom_read.size = sizeof(dom_read);
    dom_read.pw = context;
    dom_read.has_element = app_script_has_element;
    dom_read.get_text = app_script_get_text;
    dom_read.get_document_title = app_script_get_document_title;
    memset(&relation, 0, sizeof(relation));
    relation.size = sizeof(relation);
    relation.pw = context;
    relation.get_relation = app_script_get_relation;
    memset(&write, 0, sizeof(write));
    write.size = sizeof(write);
    write.pw = context;
    write.set_text = app_script_set_text;
    write.set_document_title = app_script_set_document_title;
    memset(&content_editable, 0, sizeof(content_editable));
    content_editable.size = sizeof(content_editable);
    content_editable.pw = context;
    content_editable.get_editable = app_script_get_content_editable;
    content_editable.set_text = app_script_set_content_editable_text;
    memset(&content_selection, 0, sizeof(content_selection));
    content_selection.size = sizeof(content_selection);
    content_selection.pw = context;
    content_selection.get_selection =
            app_script_get_content_editable_selection;
    content_selection.set_selection =
            app_script_set_content_editable_selection;
    memset(&document_write, 0, sizeof(document_write));
    document_write.size = sizeof(document_write);
    document_write.pw = context;
    document_write.write = app_script_document_write;
    memset(&mutation, 0, sizeof(mutation));
    mutation.size = sizeof(mutation);
    mutation.pw = context;
    mutation.remove_child = app_script_remove_child;
    memset(&attribute, 0, sizeof(attribute));
    attribute.size = sizeof(attribute);
    attribute.pw = context;
    attribute.get_attribute = app_script_get_attribute;
    attribute.set_attribute = app_script_set_attribute;
    attribute.remove_attribute = app_script_remove_attribute;
    memset(&value, 0, sizeof(value));
    value.size = sizeof(value);
    value.pw = context;
    value.get_value = app_script_get_value;
    value.set_value = app_script_set_value;
    memset(&checked, 0, sizeof(checked));
    checked.size = sizeof(checked);
    checked.pw = context;
    checked.get_checked = app_script_get_checked;
    checked.set_checked = app_script_set_checked;
    memset(&form, 0, sizeof(form));
    form.size = sizeof(form);
    form.pw = context;
    form.get_default_value = app_script_get_default_value;
    form.set_default_value = app_script_set_default_value;
    form.get_default_checked = app_script_get_default_checked;
    form.set_default_checked = app_script_set_default_checked;
    form.get_selected_index = app_script_get_selected_index;
    form.set_selected_index = app_script_set_selected_index;
    memset(&option, 0, sizeof(option));
    option.size = sizeof(option);
    option.pw = context;
    option.get_selected = app_script_get_selected;
    option.set_selected = app_script_set_selected;
    option.get_default_selected = app_script_get_default_selected;
    option.set_default_selected = app_script_set_default_selected;
    memset(&event_callbacks, 0, sizeof(event_callbacks));
    event_callbacks.size = sizeof(event_callbacks);
    event_callbacks.pw = context;
    event_callbacks.add_listener = app_script_add_event_listener;
    event_callbacks.remove_listener = app_script_remove_event_listener;
    memset(&navigation, 0, sizeof(navigation));
    navigation.size = sizeof(navigation);
    navigation.pw = context;
    navigation.navigate = app_script_navigate;
    memset(&scroll, 0, sizeof(scroll));
    scroll.size = sizeof(scroll);
    scroll.pw = context;
    scroll.scroll = app_script_scroll;
    memset(&active, 0, sizeof(active));
    active.size = sizeof(active);
    active.pw = context;
    active.get_active_element = app_script_active_element;
    memset(&interaction, 0, sizeof(interaction));
    interaction.size = sizeof(interaction);
    interaction.pw = context;
    interaction.get_interaction_element = app_script_interaction_element;
    memset(&focus_request, 0, sizeof(focus_request));
    focus_request.size = sizeof(focus_request);
    focus_request.pw = context;
    focus_request.request_focus = app_script_focus_request;
    memset(&input_callbacks, 0, sizeof(input_callbacks));
    input_callbacks.size = sizeof(input_callbacks);
    input_callbacks.pw = context;
    input_callbacks.dispatch_input = app_script_input_dispatch;
    memset(&key_callbacks, 0, sizeof(key_callbacks));
    key_callbacks.size = sizeof(key_callbacks);
    key_callbacks.pw = context;
    key_callbacks.dispatch_key = app_script_key_dispatch;
    memset(&focus_callbacks, 0, sizeof(focus_callbacks));
    focus_callbacks.size = sizeof(focus_callbacks);
    focus_callbacks.pw = context;
    focus_callbacks.dispatch_focus = app_script_focus_dispatch;
    memset(&edit_callbacks, 0, sizeof(edit_callbacks));
    edit_callbacks.size = sizeof(edit_callbacks);
    edit_callbacks.pw = context;
    edit_callbacks.dispatch_edit = app_script_edit_dispatch;
    memset(&native_edit_callbacks, 0, sizeof(native_edit_callbacks));
    native_edit_callbacks.size = sizeof(native_edit_callbacks);
    native_edit_callbacks.pw = context;
    native_edit_callbacks.dispatch_input = app_script_input_dispatch;
    native_edit_callbacks.dispatch_change = app_script_edit_dispatch;
    memset(&native_select_callbacks, 0, sizeof(native_select_callbacks));
    native_select_callbacks.size = sizeof(native_select_callbacks);
    native_select_callbacks.pw = context;
    native_select_callbacks.dispatch_select =
            app_script_native_select_dispatch;
    memset(&click_callbacks, 0, sizeof(click_callbacks));
    click_callbacks.size = sizeof(click_callbacks);
    click_callbacks.pw = context;
    click_callbacks.dispatch_click = app_script_click_dispatch;
    if (PBrowser_ScriptSessionRegisterDomReadCallbacksEx(context->session,
            &dom_read) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDomRelationCallbacks(
            context->session, &relation) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDomWriteCallbacksEx13(
            context->session,
            &write) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterContentEditableCallbacks(
            context->session, &content_editable) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterContentEditableSelectionCallbacks(
            context->session, &content_selection) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDocumentWriteCallbacks(
            context->session, &document_write) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDomMutationCallbacks(
            context->session, &mutation) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDomAttributeCallbacks(
            context->session, &attribute) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDomValueCallbacks(context->session,
            &value) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterDomCheckedCallbacks(
            context->session, &checked) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterFormCallbacks(context->session,
            &form) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterOptionCallbacks(context->session,
            &option) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterEventCallbacks(context->session,
            &event_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterNavigationCallbacks(
            context->session, &navigation) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterScrollCallbacks(context->session,
            &scroll) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterActiveElementCallbacks(
            context->session, &active) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterInteractionElementCallbacks(
            context->session, &interaction) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterFocusRequestCallbacks(
            context->session, &focus_request) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterInputCallbacks(context->session,
            &input_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterKeyCallbacks(context->session,
            &key_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterFocusCallbacks(context->session,
            &focus_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterEditCallbacks(context->session,
            &edit_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterNativeEditCallbacksEx(
            context->session, &native_edit_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx(
            context->session, &native_select_callbacks) != PSCRIPT_OK ||
            PBrowser_ScriptSessionRegisterClickCallbacks(context->session,
            &click_callbacks) != PSCRIPT_OK) {
        return 1;
    }
    return 0;
}

AppScriptContext *AppScript_Create(HANDLE document,
        const char *document_url, int history_length, int history_index,
        int history_can_commit, const char *history_state_json,
        int viewport_width, int viewport_height, int dpi,
        const AppScriptHostCallbacks *callbacks)
{
    AppScriptContext *context;
    double viewport_css_width;
    double viewport_css_height;
    double device_pixel_ratio;

    if (document == NULL || document_url == NULL || callbacks == NULL ||
            callbacks->size < sizeof(AppScriptHostCallbacks)) {
        return NULL;
    }
    context = (AppScriptContext *) calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->document = document;
    app_script_copy_text(context->document_url,
            sizeof(context->document_url), document_url);
    memcpy(&context->callbacks, callbacks, sizeof(context->callbacks));
    context->next_listener = 1;
    context->native_select_key_index = APP_SCRIPT_NO_SELECT_KEY;
    context->history_length = history_length < 1 ? 1 : history_length;
    context->history_index = history_index;
    if (context->history_index < 0 ||
            context->history_index >= context->history_length) {
        context->history_index = context->history_length - 1;
    }
    context->history_can_commit = history_can_commit ? 1 : 0;
    context->viewport_width = viewport_width > 0 ? viewport_width : 1;
    context->viewport_height = viewport_height > 0 ? viewport_height : 1;
    context->dpi = dpi > 0 ? dpi : 96;
    context->session = PBrowser_ScriptSessionCreate(
            PSCRIPT_DEFAULT_BUDGET_MS * 4UL);
    if (context->session == NULL) {
        free(context);
        return NULL;
    }
    viewport_css_width = (double) context->viewport_width * 96.0 /
            (double) context->dpi;
    viewport_css_height = (double) context->viewport_height * 96.0 /
            (double) context->dpi;
    device_pixel_ratio = (double) context->dpi / 96.0;
    if (PBrowser_ScriptSessionSetGlobalString(context->session,
            "__pcoreWindowName", "") != PSCRIPT_OK ||
            PBrowser_ScriptSessionSetGlobalString(context->session,
            "__pcoreDocumentUrl", context->document_url) != PSCRIPT_OK ||
            PBrowser_ScriptSessionSetGlobalNumber(context->session,
            "__pcoreHistoryLength", (double) context->history_length) !=
            PSCRIPT_OK ||
            PBrowser_ScriptSessionSetGlobalJson(context->session,
            "__pcoreHistoryState", history_state_json != NULL ?
            history_state_json : "null") != PSCRIPT_OK ||
            PBrowser_ScriptSessionSetGlobalNumber(context->session,
            "__pcoreViewportWidth", viewport_css_width) != PSCRIPT_OK ||
            PBrowser_ScriptSessionSetGlobalNumber(context->session,
            "__pcoreViewportHeight", viewport_css_height) != PSCRIPT_OK ||
            PBrowser_ScriptSessionSetGlobalNumber(context->session,
            "__pcoreDevicePixelRatio", device_pixel_ratio) != PSCRIPT_OK ||
            app_script_register_callbacks(context) != 0 ||
            PBrowser_ScriptSessionEvaluateBootstrap(context->session) !=
            PSCRIPT_OK) {
        AppScript_Destroy(context);
        return NULL;
    }
    return context;
}

static int app_script_type_equal(const char *type, int length,
        const char *expected)
{
    int i;
    char left;
    char right;

    if (type == NULL || expected == NULL ||
            (int) strlen(expected) != length) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        left = type[i];
        right = expected[i];
        if (left >= 'A' && left <= 'Z') {
            left = (char) (left + ('a' - 'A'));
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

static int app_script_type_supported(const char *type)
{
    const char *start;
    const char *end;
    int length;

    if (type == NULL) {
        return 1;
    }
    start = type;
    while (*start == ' ' || *start == '\t' || *start == '\r' ||
            *start == '\n') {
        start++;
    }
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
            end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    length = (int) (end - start);
    return length == 0 || app_script_type_equal(start, length,
            "text/javascript") || app_script_type_equal(start, length,
            "application/javascript") || app_script_type_equal(start,
            length, "text/ecmascript") || app_script_type_equal(start,
            length, "application/ecmascript");
}

int AppScript_Execute(AppScriptContext *context, int allow_external,
        PCoreResolveUrlFn resolve, void *resolve_pw, int *out_executed,
        int *out_ignored, int *out_errors)
{
    PCoreScriptInfo info;
    const char *data;
    char *source;
    char *type;
    int count;
    int executed;
    int ignored;
    int errors;
    int i;
    int result;

    if (out_executed != NULL) {
        *out_executed = 0;
    }
    if (out_ignored != NULL) {
        *out_ignored = 0;
    }
    if (out_errors != NULL) {
        *out_errors = 0;
    }
    if (context == NULL || context->document == NULL ||
            context->session == NULL) {
        return 1;
    }
    count = PCore_GetScriptCount(context->document);
    if (count < 0) {
        return 1;
    }
    executed = 0;
    ignored = 0;
    errors = 0;
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        data = NULL;
        if (PCore_GetScript(context->document, (unsigned int) i,
                context->document_url, resolve, resolve_pw, &info, NULL, 0,
                NULL, 0, NULL, 0, &data) != 0 || info.source_bytes < 0 ||
                info.type_bytes < 0 || info.data_bytes < 0) {
            errors++;
            continue;
        }
        if (info.kind == 2 && (!allow_external || !info.available)) {
            ignored++;
            continue;
        }
        if (info.kind == 1 && info.source_bytes > APP_SCRIPT_MAX_SOURCE_BYTES) {
            ignored++;
            continue;
        }
        if (info.kind == 2 && info.data_bytes > APP_SCRIPT_MAX_SOURCE_BYTES) {
            ignored++;
            continue;
        }
        source = info.kind == 1 ? (char *) malloc(
                (size_t) info.source_bytes + 1U) : NULL;
        type = (char *) malloc((size_t) info.type_bytes + 1U);
        if ((info.kind == 1 && source == NULL) || type == NULL ||
                PCore_GetScript(context->document, (unsigned int) i,
                context->document_url, resolve, resolve_pw, &info, source,
                info.source_bytes + 1, NULL, 0, type, info.type_bytes + 1,
                &data) != 0) {
            free(source);
            free(type);
            errors++;
            continue;
        }
        if (!app_script_type_supported(type) ||
                (info.kind == 1 && source == NULL) ||
                (info.kind == 2 && data == NULL)) {
            ignored++;
        } else {
            result = PBrowser_ScriptSessionSetCurrentScriptIndex(
                    context->session, i);
            if (result == PSCRIPT_OK) {
                result = PBrowser_ScriptSessionEvaluate(context->session,
                        info.kind == 1 ? source : data,
                        info.kind == 1 ? info.source_bytes : info.data_bytes);
            }
            if (result != PSCRIPT_OK) {
                errors++;
            } else {
                executed++;
            }
        }
        free(source);
        free(type);
    }
    (void) PBrowser_ScriptSessionSetCurrentScriptIndex(context->session, -1);
    if (out_executed != NULL) {
        *out_executed = executed;
    }
    if (out_ignored != NULL) {
        *out_ignored = ignored;
    }
    if (out_errors != NULL) {
        *out_errors = errors;
    }
    return 0;
}

void AppScript_Destroy(AppScriptContext *context)
{
    AppScriptEventBinding *binding;
    AppScriptEventBinding *next;

    if (context == NULL) {
        return;
    }
    binding = context->events;
    while (binding != NULL) {
        next = binding->next;
        if (context->document != NULL && binding->core_listener != NULL) {
            PCore_EventListenerRemove(context->document,
                    binding->core_listener);
        }
        free(binding);
        binding = next;
    }
    context->events = NULL;
    if (context->session != NULL) {
        PBrowser_ScriptSessionDestroy(context->session);
        context->session = NULL;
    }
    free(context);
}

int AppScript_BeforeUnload(AppScriptContext *context, int *out_prevented)
{
    if (context == NULL || context->session == NULL ||
            out_prevented == NULL) {
        return 1;
    }
    return PBrowser_ScriptSessionDispatchBeforeUnload(context->session,
            out_prevented) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_PageLifecycleComplete(AppScriptContext *context)
{
    if (context == NULL || context->session == NULL) {
        return 0;
    }
    return PBrowser_ScriptSessionDispatchPageLifecycle(context->session,
            "complete") == PSCRIPT_OK ? 0 : 1;
}

int AppScript_PageTeardown(AppScriptContext *context)
{
    if (context == NULL || context->session == NULL) {
        return 0;
    }
    return PBrowser_ScriptSessionDispatchPageTeardown(context->session) ==
            PSCRIPT_OK ? 0 : 1;
}

int AppScript_RunTaskCheckpoint(AppScriptContext *context,
        unsigned long now_ms)
{
    if (context == NULL || context->session == NULL) {
        return 0;
    }
    return PBrowser_ScriptSessionRunTaskCheckpoint(context->session, now_ms,
            now_ms, now_ms + 1UL, 8UL, PBROWSER_SCRIPT_PUMP_ALL) ==
            PSCRIPT_OK ? 0 : 1;
}

int AppScript_SetVisibility(AppScriptContext *context, int hidden)
{
    if (context == NULL || context->session == NULL) {
        return 0;
    }
    return PBrowser_ScriptSessionDispatchVisibility(context->session,
            hidden ? 1 : 0) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_SetFocus(AppScriptContext *context, int focused)
{
    if (context == NULL || context->session == NULL) {
        return 0;
    }
    return PBrowser_ScriptSessionDispatchWindowFocus(context->session,
            focused ? 1 : 0) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_NotifyScroll(AppScriptContext *context, int scroll_x,
        int scroll_y)
{
    if (context == NULL || context->session == NULL) {
        return 0;
    }
    return PBrowser_ScriptSessionNotifyScroll(context->session, scroll_x,
            scroll_y) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_NotifyResize(AppScriptContext *context, int viewport_width,
        int viewport_height, int dpi)
{
    double css_width;
    double css_height;
    double ratio;

    if (context == NULL || context->session == NULL || viewport_width <= 0 ||
            viewport_height <= 0 || dpi <= 0) {
        return 0;
    }
    css_width = (double) viewport_width * 96.0 / (double) dpi;
    css_height = (double) viewport_height * 96.0 / (double) dpi;
    ratio = (double) dpi / 96.0;
    context->viewport_width = viewport_width;
    context->viewport_height = viewport_height;
    context->dpi = dpi;
    return PBrowser_ScriptSessionNotifyResize(context->session, css_width,
            css_height, ratio) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_DispatchHashNavigation(AppScriptContext *context,
        const char *url, int history_length)
{
    if (context == NULL || context->session == NULL || url == NULL ||
            url[0] == '\0' || history_length < 1) {
        return 1;
    }
    return PBrowser_ScriptSessionDispatchHashNavigation(context->session,
            url, history_length) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_DispatchKeyEvent(AppScriptContext *context, int x, int y,
        const char *event_type, const char *key, unsigned int key_code,
        unsigned int char_code, int repeat, int shift, int ctrl, int alt,
        int is_composing, int *out_default_allowed)
{
    PBrowserScriptKeyEventInfo info;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    if (context == NULL || context->session == NULL ||
            event_type == NULL || event_type[0] == '\0' || key == NULL ||
            key[0] == '\0' || out_default_allowed == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.x = x;
    info.y = y;
    info.event_type = event_type;
    info.key = key;
    info.key_code = key_code;
    info.char_code = char_code;
    info.repeat = repeat ? 1 : 0;
    info.shift = shift ? 1 : 0;
    info.ctrl = ctrl ? 1 : 0;
    info.alt = alt ? 1 : 0;
    info.is_composing = is_composing ? 1 : 0;
    return PBrowser_ScriptSessionDispatchKeyEvent(context->session, &info,
            out_default_allowed) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_DispatchFocusEvent(AppScriptContext *context, int x, int y,
        const char *event_type, int bubbles, int cancelable)
{
    PBrowserScriptFocusEventInfo info;

    if (context == NULL || context->session == NULL || event_type == NULL ||
            event_type[0] == '\0') {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.x = x;
    info.y = y;
    info.event_type = event_type;
    info.bubbles = bubbles ? 1 : 0;
    info.cancelable = cancelable ? 1 : 0;
    return PBrowser_ScriptSessionDispatchFocusEvent(context->session, &info)
            == PSCRIPT_OK ? 0 : 1;
}

static int app_script_native_edit_target_push(AppScriptContext *context,
        const char *target_id, char *previous_id, int previous_capacity)
{
    if (context == NULL || previous_id == NULL || previous_capacity <= 0) {
        return 1;
    }
    app_script_copy_text(previous_id, previous_capacity,
            context->native_edit_target_id);
    if (target_id == NULL || target_id[0] == '\0') {
        context->native_edit_target_id[0] = '\0';
        return 0;
    }
    if (app_script_copy_bounded(context->native_edit_target_id,
            sizeof(context->native_edit_target_id), target_id) != 0) {
        return 1;
    }
    return 0;
}

static void app_script_native_edit_target_pop(AppScriptContext *context,
        const char *previous_id)
{
    if (context != NULL && previous_id != NULL) {
        app_script_copy_text(context->native_edit_target_id,
                sizeof(context->native_edit_target_id), previous_id);
    }
}

int AppScript_DispatchNativeEditBeforeInput(AppScriptContext *context,
        unsigned long target_token, const char *target_id, int x, int y,
        const char *input_type, const char *data, int cancelable,
        int is_composing, int *out_default_allowed)
{
    PBrowserScriptNativeEditInputInfo info;
    char previous_id[APP_SCRIPT_URL_MAX];
    int result;

    if (context == NULL || context->session == NULL || target_token == 0 ||
            input_type == NULL || data == NULL ||
            out_default_allowed == NULL ||
            app_script_native_edit_target_push(context, target_id,
            previous_id, sizeof(previous_id)) != 0) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.input_type = input_type;
    info.data = data;
    info.cancelable = cancelable ? 1 : 0;
    info.is_composing = is_composing ? 1 : 0;
    result = PBrowser_ScriptSessionDispatchNativeEditBeforeInput(
            context->session, &info, out_default_allowed) == PSCRIPT_OK ?
            0 : 1;
    app_script_native_edit_target_pop(context, previous_id);
    return result;
}

int AppScript_DispatchNativeEditInput(AppScriptContext *context,
        unsigned long target_token, const char *target_id, int x, int y,
        const char *input_type, const char *data)
{
    PBrowserScriptNativeEditInputInfo info;
    char previous_id[APP_SCRIPT_URL_MAX];
    int result;

    if (context == NULL || context->session == NULL || target_token == 0 ||
            app_script_native_edit_target_push(context, target_id,
            previous_id, sizeof(previous_id)) != 0) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.input_type = input_type;
    info.data = data;
    result = PBrowser_ScriptSessionDispatchNativeEditInput(context->session,
            &info) == PSCRIPT_OK ? 0 : 1;
    app_script_native_edit_target_pop(context, previous_id);
    return result;
}

int AppScript_DispatchNativeEditBlur(AppScriptContext *context,
        unsigned long target_token, int x, int y)
{
    PBrowserScriptNativeEditInputInfo info;

    if (context == NULL || context->session == NULL || target_token == 0) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    return PBrowser_ScriptSessionDispatchNativeEditBlur(context->session,
            &info) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_NotifyNativeContentEditableSelection(AppScriptContext *context,
        const char *element_id, int start, int end, int direction,
        int trusted)
{
    if (context == NULL || context->session == NULL || element_id == NULL ||
            element_id[0] == '\0' || start < 0 || end < start ||
            direction < PBROWSER_SCRIPT_CONTENT_SELECTION_NONE ||
            direction > PBROWSER_SCRIPT_CONTENT_SELECTION_BACKWARD) {
        return 1;
    }
    return PBrowser_ScriptSessionNotifyContentEditableSelection(
            context->session, element_id, start, end, direction,
            trusted ? 1 : 0, NULL) == PSCRIPT_OK ? 0 : 1;
}

void AppScript_ResetNativeEditState(AppScriptContext *context)
{
    if (context != NULL && context->session != NULL) {
        (void) PBrowser_ScriptSessionResetNativeEditState(context->session);
    }
}

int AppScript_DispatchNativeSelectCommit(AppScriptContext *context,
        unsigned long target_token, int x, int y, int multiple,
        int selected_index, int selected_count)
{
    PBrowserScriptNativeSelectCommitInfo info;

    if (context == NULL || context->session == NULL || target_token == 0) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.multiple = multiple ? 1 : 0;
    info.selected_index = selected_index;
    info.selected_count = selected_count;
    return PBrowser_ScriptSessionDispatchNativeSelectCommit(
            context->session, &info) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_DispatchNativeSelectInteraction(AppScriptContext *context,
        unsigned long target_token, int x, int y, int multiple,
        int selected_index, int selected_count, int phase,
        int *out_should_commit)
{
    PBrowserScriptNativeSelectInteractionInfo info;

    if (out_should_commit != NULL) {
        *out_should_commit = 0;
    }
    if (context == NULL || context->session == NULL || target_token == 0 ||
            out_should_commit == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.multiple = multiple ? 1 : 0;
    info.selected_index = selected_index;
    info.selected_count = selected_count;
    info.phase = phase;
    return PBrowser_ScriptSessionDispatchNativeSelectInteraction(
            context->session, &info, out_should_commit) == PSCRIPT_OK ?
            0 : 1;
}

int AppScript_DispatchNativeSelectFocus(AppScriptContext *context,
        unsigned long target_token, int x, int y, int focused)
{
    PBrowserScriptNativeSelectFocusInfo info;

    if (context == NULL || context->session == NULL || target_token == 0 ||
            (focused != 0 && focused != 1)) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.focused = focused;
    return PBrowser_ScriptSessionDispatchNativeSelectFocus(
            context->session, &info) == PSCRIPT_OK ? 0 : 1;
}

int AppScript_DispatchNativeSelectKey(AppScriptContext *context,
        unsigned long target_token, int x, int y, const char *event_type,
        const char *key, unsigned int key_code, unsigned int char_code,
        int repeat, int shift, int ctrl, int alt, int is_composing,
        int *out_default_allowed)
{
    PBrowserScriptNativeSelectKeyInfo info;
    unsigned int previous_index;
    int default_allowed;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    if (context == NULL || context->session == NULL || target_token == 0 ||
            event_type == NULL || event_type[0] == '\0' || key == NULL ||
            key[0] == '\0' || out_default_allowed == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.event_type = event_type;
    info.key = key;
    info.key_code = key_code;
    info.char_code = char_code;
    info.repeat = repeat ? 1 : 0;
    info.shift = shift ? 1 : 0;
    info.ctrl = ctrl ? 1 : 0;
    info.alt = alt ? 1 : 0;
    info.is_composing = is_composing ? 1 : 0;
    default_allowed = 1;
    previous_index = context->native_select_key_index;
    context->native_select_key_index = (unsigned int) (target_token - 1UL);
    if (PBrowser_ScriptSessionDispatchNativeSelectKey(context->session,
            &info, &default_allowed) != PSCRIPT_OK) {
        context->native_select_key_index = previous_index;
        return 1;
    }
    context->native_select_key_index = previous_index;
    *out_default_allowed = default_allowed ? 1 : 0;
    return 0;
}

void AppScript_ResetNativeSelectState(AppScriptContext *context)
{
    if (context != NULL && context->session != NULL) {
        (void) PBrowser_ScriptSessionResetNativeSelectState(context->session);
    }
}

int AppScript_DispatchNativeToggle(AppScriptContext *context,
        unsigned long target_token, int x, int y, int phase, int kind,
        int disabled, int selected_before, int selected_after,
        int *out_default_allowed)
{
    PBrowserScriptNativeToggleInfo info;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    if (context == NULL || context->session == NULL || target_token == 0 ||
            out_default_allowed == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.phase = phase;
    info.kind = kind;
    info.disabled = disabled ? 1 : 0;
    info.selected_before = selected_before ? 1 : 0;
    info.selected_after = selected_after ? 1 : 0;
    return PBrowser_ScriptSessionDispatchNativeToggle(context->session, &info,
            out_default_allowed) == PSCRIPT_OK ? 0 : 1;
}

void AppScript_ResetNativeToggleState(AppScriptContext *context)
{
    if (context != NULL && context->session != NULL) {
        (void) PBrowser_ScriptSessionResetNativeToggleState(context->session);
    }
}

int AppScript_DispatchNativeButton(AppScriptContext *context,
        unsigned long target_token, int x, int y, int phase, int kind,
        int disabled, int validation_valid, int *out_default_allowed)
{
    PBrowserScriptNativeButtonInfo info;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    if (context == NULL || context->session == NULL || target_token == 0 ||
            out_default_allowed == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.target_token = target_token;
    info.x = x;
    info.y = y;
    info.phase = phase;
    info.kind = kind;
    info.disabled = disabled ? 1 : 0;
    info.validation_valid = validation_valid ? 1 : 0;
    return PBrowser_ScriptSessionDispatchNativeButton(context->session,
            &info, out_default_allowed) == PSCRIPT_OK ? 0 : 1;
}

void AppScript_ResetNativeButtonState(AppScriptContext *context)
{
    if (context != NULL && context->session != NULL) {
        (void) PBrowser_ScriptSessionResetNativeButtonState(context->session);
    }
}

HANDLE AppScript_Document(AppScriptContext *context)
{
    return context == NULL ? NULL : context->document;
}

int AppScript_QueueNavigation(AppScriptContext *context,
        const PBrowserScriptNavigationInfo *info)
{
    if (context == NULL || info == NULL || info->size < sizeof(*info) ||
            ((info->url == NULL || info->url[0] == '\0') &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_BACK &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_FORWARD &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_GO) ||
            (info->url != NULL && strlen(info->url) >=
            sizeof(context->pending_navigation.url)) ||
            (info->state_json != NULL && strlen(info->state_json) >=
            sizeof(context->pending_navigation.state_json)) ||
            (info->target != NULL && strlen(info->target) >=
            sizeof(context->pending_navigation.target)) ||
            (info->rel != NULL && strlen(info->rel) >=
            sizeof(context->pending_navigation.rel)) ||
            (info->context_name != NULL && strlen(info->context_name) >=
            sizeof(context->pending_navigation.context_name))) {
        return 1;
    }
    memset(&context->pending_navigation, 0,
            sizeof(context->pending_navigation));
    context->pending_navigation.valid = 1;
    context->pending_navigation.kind = info->kind;
    context->pending_navigation.delta = info->delta;
    context->pending_navigation.target_kind = info->target_kind;
    app_script_copy_text(context->pending_navigation.url,
            sizeof(context->pending_navigation.url),
            info->url != NULL ? info->url : "");
    app_script_copy_text(context->pending_navigation.state_json,
            sizeof(context->pending_navigation.state_json),
            info->state_json != NULL ? info->state_json : "null");
    app_script_copy_text(context->pending_navigation.target,
            sizeof(context->pending_navigation.target),
            info->target != NULL ? info->target : "");
    app_script_copy_text(context->pending_navigation.rel,
            sizeof(context->pending_navigation.rel),
            info->rel != NULL ? info->rel : "");
    app_script_copy_text(context->pending_navigation.context_name,
            sizeof(context->pending_navigation.context_name),
            info->context_name != NULL ? info->context_name : "");
    return 0;
}

void AppScript_ClearPendingNavigation(AppScriptContext *context)
{
    if (context != NULL) {
        memset(&context->pending_navigation, 0,
                sizeof(context->pending_navigation));
    }
}

int AppScript_TakeNavigation(AppScriptContext *context,
        AppScriptPendingNavigation *out_navigation)
{
    if (context == NULL || out_navigation == NULL) {
        return 1;
    }
    if (!context->pending_navigation.valid) {
        memset(out_navigation, 0, sizeof(*out_navigation));
        return 1;
    }
    memcpy(out_navigation, &context->pending_navigation,
            sizeof(*out_navigation));
    memset(&context->pending_navigation, 0,
            sizeof(context->pending_navigation));
    return 0;
}

int AppScript_HasPendingNavigation(AppScriptContext *context)
{
    return context != NULL && context->pending_navigation.valid;
}
