/*
 * positron_app/app_controls.c - private WM6 native form-control adapter.
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "app_controls.h"

#define APP_CONTROLS_MAX             PBROWSER_SCRIPT_NATIVE_EDIT_MAX_TARGETS
#define APP_CONTROLS_TEXT_CAP        32768
#define APP_CONTROLS_OPTION_TEXT_CAP 4096
#define APP_CONTROLS_ID_BASE         1000
#define APP_CONTROLS_KIND_TEXT       1
#define APP_CONTROLS_KIND_SELECT     2
#define APP_CONTROLS_KIND_TOGGLE     3
#define APP_CONTROLS_FORM_BUTTON     9
#define APP_CONTROLS_NO_SELECT_INDEX  0xffffffffUL

typedef struct AppControlsItem AppControlsItem;

struct AppControlsItem {
    HWND hwnd;
    int kind;
    unsigned int text_index;
    unsigned int select_index;
    unsigned int toggle_index;
    unsigned int form_index;
    unsigned long target_token;
    WNDPROC original_proc;
    int multiline;
    int password;
    int read_only;
    int disabled;
    int toggle_kind;
    int selected;
    int toggle_space_pending;
    int multiple;
    int option_count;
    unsigned long option_fingerprint;
    int dropdown_active;
    int select_candidate_index;
    int select_candidate_count;
    int x;
    int y;
    int width;
    int height;
};

struct AppControlsContext {
    HWND parent;
    HINSTANCE instance;
    void *pw;
    AppControlsChangedFn changed;
    HANDLE document;
    AppScriptContext *script;
    AppControlsItem items[APP_CONTROLS_MAX];
    unsigned int count;
    unsigned int button_focus_form_index;
    int button_focus_x;
    int button_focus_y;
    int button_focus_active;
    int button_space_pending;
    int syncing;
};

static AppControlsContext *g_app_controls;

static int app_controls_item_geometry(AppControlsContext *context,
        AppControlsItem *item, int *out_x, int *out_y, int *out_width,
        int *out_height)
{
    PCoreTextInputInfo info;
    PCoreSelectInfo select_info;
    int form_kind;
    int selected;
    int disabled;

    if (context == NULL || item == NULL || context->document == NULL) {
        return 0;
    }
    if (item->kind == APP_CONTROLS_KIND_TEXT) {
        memset(&info, 0, sizeof(info));
        if (PCore_TextInputInfo(context->document, item->text_index, &info,
                NULL, 0) != 0) {
            return 0;
        }
        item->x = info.x;
        item->y = info.y;
        item->width = info.width > 0 ? info.width : 1;
        item->height = info.height > 0 ? info.height : 1;
    } else if (item->kind == APP_CONTROLS_KIND_SELECT) {
        memset(&select_info, 0, sizeof(select_info));
        if (PCore_SelectInfo(context->document, item->select_index,
                &select_info) != 0) {
            return 0;
        }
        item->x = select_info.x;
        item->y = select_info.y;
        item->width = select_info.width > 0 ? select_info.width : 1;
        item->height = select_info.height > 0 ? select_info.height : 1;
    } else if (item->kind == APP_CONTROLS_KIND_TOGGLE) {
        if (PCore_FormControlInfo(context->document, item->form_index,
                &item->x, &item->y, &item->width, &item->height,
                &form_kind, &selected, &disabled) != 0 ||
                (form_kind != PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX &&
                form_kind != PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO)) {
            return 0;
        }
        item->width = item->width > 0 ? item->width : 1;
        item->height = item->height > 0 ? item->height : 1;
        item->toggle_kind = form_kind;
        item->selected = selected ? 1 : 0;
        item->disabled = disabled ? 1 : 0;
    } else {
        return 0;
    }
    if (out_x != NULL) {
        *out_x = item->x;
    }
    if (out_y != NULL) {
        *out_y = item->y;
    }
    if (out_width != NULL) {
        *out_width = item->width;
    }
    if (out_height != NULL) {
        *out_height = item->height;
    }
    return 1;
}

/* Core enumerates all laid-out form controls together, while the native
 * adapter keeps independent text/select/toggle arrays. Resolve the toggle
 * ordinal without exposing any Core internals to the EXE. */
static int app_controls_toggle_info_at(HANDLE document,
        unsigned int toggle_index, unsigned int *out_form_index,
        int *out_x, int *out_y, int *out_width, int *out_height,
        int *out_kind, int *out_selected, int *out_disabled)
{
    unsigned int form_index;
    unsigned int ordinal;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int selected;
    int disabled;

    if (document == NULL) {
        return 1;
    }
    ordinal = 0;
    form_index = 0;
    while (PCore_FormControlInfo(document, form_index, &x, &y, &width,
            &height, &kind, &selected, &disabled) == 0) {
        if (kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX ||
                kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO) {
            if (ordinal == toggle_index) {
                if (out_form_index != NULL) {
                    *out_form_index = form_index;
                }
                if (out_x != NULL) {
                    *out_x = x;
                }
                if (out_y != NULL) {
                    *out_y = y;
                }
                if (out_width != NULL) {
                    *out_width = width;
                }
                if (out_height != NULL) {
                    *out_height = height;
                }
                if (out_kind != NULL) {
                    *out_kind = kind;
                }
                if (out_selected != NULL) {
                    *out_selected = selected ? 1 : 0;
                }
                if (out_disabled != NULL) {
                    *out_disabled = disabled ? 1 : 0;
                }
                return 0;
            }
            ordinal++;
        }
        form_index++;
    }
    return 1;
}

static int app_controls_toggle_count(HANDLE document,
        unsigned int *out_count)
{
    unsigned int form_index;
    unsigned int count;
    int kind;

    if (document == NULL || out_count == NULL) {
        return 1;
    }
    form_index = 0;
    count = 0;
    while (PCore_FormControlInfo(document, form_index, NULL, NULL, NULL,
            NULL, &kind, NULL, NULL) == 0) {
        if (kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX ||
                kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO) {
            count++;
        }
        form_index++;
    }
    *out_count = count;
    return 0;
}

static AppControlsItem *app_controls_find(HWND hwnd)
{
    unsigned int i;

    if (g_app_controls == NULL || hwnd == NULL) {
        return NULL;
    }
    for (i = 0; i < g_app_controls->count; i++) {
        if (g_app_controls->items[i].hwnd == hwnd) {
            return &g_app_controls->items[i];
        }
    }
    return NULL;
}

static LRESULT app_controls_call_original(AppControlsItem *item,
        HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (item != NULL && item->original_proc != NULL) {
        return CallWindowProc(item->original_proc, hwnd, message,
                wparam, lparam);
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

static int app_controls_utf8_to_wide(const char *source, WCHAR *target,
        int target_capacity)
{
    int chars;

    if (source == NULL || target == NULL || target_capacity <= 0) {
        return 0;
    }
    target[0] = L'\0';
    chars = MultiByteToWideChar(CP_UTF8, 0, source, -1, target,
            target_capacity);
    if (chars <= 0) {
        chars = MultiByteToWideChar(CP_ACP, 0, source, -1, target,
                target_capacity);
    }
    if (chars <= 0) {
        return 0;
    }
    target[target_capacity - 1] = L'\0';
    return chars;
}

static int app_controls_wide_to_utf8(const WCHAR *source, int source_length,
        char **out_value)
{
    char *value;
    int bytes;

    if (source == NULL || source_length < 0 || out_value == NULL) {
        return 1;
    }
    *out_value = NULL;
    bytes = WideCharToMultiByte(CP_UTF8, 0, source, source_length,
            NULL, 0, NULL, NULL);
    if (bytes <= 0) {
        bytes = WideCharToMultiByte(CP_ACP, 0, source, source_length,
                NULL, 0, NULL, NULL);
    }
    if (bytes < 0 || bytes >= APP_CONTROLS_TEXT_CAP) {
        return 1;
    }
    value = (char *) malloc((size_t) bytes + 1);
    if (value == NULL) {
        return 1;
    }
    if (bytes > 0 && WideCharToMultiByte(CP_UTF8, 0, source,
            source_length, value, bytes, NULL, NULL) <= 0) {
        if (WideCharToMultiByte(CP_ACP, 0, source, source_length,
                value, bytes, NULL, NULL) <= 0) {
            free(value);
            return 1;
        }
    }
    value[bytes] = '\0';
    *out_value = value;
    return 0;
}

static void app_controls_normalize_lf(char *value)
{
    int source;
    int target;

    if (value == NULL) {
        return;
    }
    source = 0;
    target = 0;
    while (value[source] != '\0') {
        if (value[source] == '\r') {
            value[target++] = '\n';
            source++;
            if (value[source] == '\n') {
                source++;
            }
        } else {
            value[target++] = value[source++];
        }
    }
    value[target] = '\0';
}

static char *app_controls_edit_value(const char *value, int multiline)
{
    char *result;
    int length;
    int source;
    int target;

    if (value == NULL) {
        return NULL;
    }
    length = (int) strlen(value);
    if (length >= APP_CONTROLS_TEXT_CAP) {
        return NULL;
    }
    result = (char *) malloc((size_t) length * 2 + 1);
    if (result == NULL) {
        return NULL;
    }
    if (!multiline) {
        memcpy(result, value, (size_t) length + 1);
        return result;
    }
    source = 0;
    target = 0;
    while (value[source] != '\0' && target + 2 < APP_CONTROLS_TEXT_CAP) {
        if (value[source] == '\n') {
            result[target++] = '\r';
        }
        result[target++] = value[source++];
    }
    result[target] = '\0';
    return result;
}

static char *app_controls_read_value(AppControlsItem *item)
{
    WCHAR *wide;
    char *value;
    int length;

    if (item == NULL || item->hwnd == NULL) {
        return NULL;
    }
    length = GetWindowTextLengthW(item->hwnd);
    if (length < 0 || length >= APP_CONTROLS_TEXT_CAP) {
        return NULL;
    }
    wide = (WCHAR *) malloc((size_t) (length + 1) * sizeof(WCHAR));
    if (wide == NULL) {
        return NULL;
    }
    if (GetWindowTextW(item->hwnd, wide, length + 1) < 0) {
        free(wide);
        return NULL;
    }
    wide[length] = L'\0';
    if (app_controls_wide_to_utf8(wide, length, &value) != 0) {
        free(wide);
        return NULL;
    }
    free(wide);
    if (item->multiline) {
        app_controls_normalize_lf(value);
    }
    return value;
}

static void app_controls_set_value(AppControlsContext *context,
        AppControlsItem *item, const char *value)
{
    char *edit_value;
    WCHAR *wide_value;
    int wide_capacity;

    if (context == NULL || item == NULL || item->hwnd == NULL ||
            value == NULL) {
        return;
    }
    edit_value = app_controls_edit_value(value, item->multiline);
    if (edit_value == NULL) {
        return;
    }
    wide_capacity = (int) strlen(edit_value) + 1;
    wide_value = (WCHAR *) malloc((size_t) wide_capacity * sizeof(WCHAR));
    if (wide_value != NULL && app_controls_utf8_to_wide(edit_value,
            wide_value, wide_capacity) > 0) {
        context->syncing = 1;
        SetWindowTextW(item->hwnd, wide_value);
        context->syncing = 0;
    }
    free(wide_value);
    free(edit_value);
}

static int app_controls_select_option_label(AppControlsContext *context,
        AppControlsItem *item, unsigned int option_index,
        WCHAR **out_label)
{
    char *label;
    WCHAR *wide_label;
    int label_bytes;
    int label_capacity;

    if (context == NULL || item == NULL || out_label == NULL ||
            context->document == NULL) {
        return 1;
    }
    *out_label = NULL;
    label_bytes = 0;
    if (PCore_SelectOptionInfo(context->document, item->select_index,
            option_index, NULL, 0, NULL, 0, NULL, NULL, &label_bytes,
            NULL) != 0 || label_bytes < 0 ||
            label_bytes >= APP_CONTROLS_OPTION_TEXT_CAP) {
        return 1;
    }
    label_capacity = label_bytes + 1;
    label = (char *) malloc((size_t) label_capacity);
    if (label == NULL) {
        return 1;
    }
    if (PCore_SelectOptionInfo(context->document, item->select_index,
            option_index, label, label_capacity, NULL, 0, NULL, NULL,
            NULL, NULL) != 0) {
        free(label);
        return 1;
    }
    wide_label = (WCHAR *) malloc((strlen(label) + 1) * sizeof(WCHAR));
    if (wide_label == NULL || app_controls_utf8_to_wide(label, wide_label,
            (int) strlen(label) + 1) <= 0) {
        free(label);
        free(wide_label);
        return 1;
    }
    free(label);
    *out_label = wide_label;
    return 0;
}

static void app_controls_hash_bytes(unsigned long *hash, const char *bytes,
        int length)
{
    int i;

    if (hash == NULL || bytes == NULL || length < 0) {
        return;
    }
    for (i = 0; i < length; i++) {
        *hash ^= (unsigned long) ((const unsigned char *) bytes)[i];
        *hash *= 16777619UL;
    }
}

static void app_controls_hash_int(unsigned long *hash, int value)
{
    unsigned long number;
    int i;

    if (hash == NULL) {
        return;
    }
    number = (unsigned long) value;
    for (i = 0; i < 4; i++) {
        *hash ^= number & 0xffUL;
        *hash *= 16777619UL;
        number >>= 8;
    }
}

static int app_controls_select_fingerprint(AppControlsContext *context,
        AppControlsItem *item, unsigned long *out_fingerprint)
{
    PCoreSelectInfo info;
    char *label;
    unsigned int option_index;
    int label_bytes;
    int label_capacity;
    unsigned long hash;

    if (context == NULL || item == NULL || out_fingerprint == NULL ||
            context->document == NULL || item->kind !=
            APP_CONTROLS_KIND_SELECT) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, item->select_index, &info) != 0 ||
            info.option_count < 0 || info.option_count >
            APP_CONTROLS_OPTION_TEXT_CAP) {
        return 1;
    }
    hash = 2166136261UL;
    app_controls_hash_int(&hash, info.option_count);
    app_controls_hash_int(&hash, info.multiple ? 1 : 0);
    for (option_index = 0; option_index < (unsigned int) info.option_count;
            option_index++) {
        label_bytes = 0;
        if (PCore_SelectOptionInfo(context->document, item->select_index,
                option_index, NULL, 0, NULL, 0, NULL, NULL, &label_bytes,
                NULL) != 0 || label_bytes < 0 || label_bytes >=
                APP_CONTROLS_OPTION_TEXT_CAP) {
            return 1;
        }
        label_capacity = label_bytes + 1;
        label = (char *) malloc((size_t) label_capacity);
        if (label == NULL) {
            return 1;
        }
        if (PCore_SelectOptionInfo(context->document, item->select_index,
                option_index, label, label_capacity, NULL, 0, NULL, NULL,
                NULL, NULL) != 0) {
            free(label);
            return 1;
        }
        app_controls_hash_int(&hash, label_bytes);
        app_controls_hash_bytes(&hash, label, label_bytes);
        hash ^= 0xffUL;
        hash *= 16777619UL;
        free(label);
    }
    *out_fingerprint = hash;
    return 0;
}

static int app_controls_select_window_height(const PCoreSelectInfo *info)
{
    int visible_items;
    int height;

    if (info == NULL) {
        return 1;
    }
    visible_items = info->option_count;
    if (visible_items > 6) {
        visible_items = 6;
    }
    if (visible_items < 1) {
        visible_items = 1;
    }
    height = info->height * (visible_items + 1);
    if (height < info->height + 1) {
        height = info->height + 1;
    }
    return height;
}

static int app_controls_select_selected(AppControlsContext *context,
        AppControlsItem *item, unsigned int option_index, int *out_selected)
{
    if (context == NULL || item == NULL || out_selected == NULL ||
            context->document == NULL) {
        return 1;
    }
    return PCore_SelectOptionInfo(context->document, item->select_index,
            option_index, NULL, 0, NULL, 0, out_selected, NULL, NULL,
            NULL) == 0 ? 0 : 1;
}

static void app_controls_sync_select(AppControlsContext *context,
        AppControlsItem *item)
{
    PCoreSelectInfo info;
    unsigned int i;
    int selected;

    if (context == NULL || item == NULL || item->kind !=
            APP_CONTROLS_KIND_SELECT || item->hwnd == NULL ||
            context->document == NULL || context->syncing) {
        return;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, item->select_index, &info) != 0) {
        return;
    }
    if ((info.multiple ? 1 : 0) != item->multiple ||
            info.option_count != item->option_count) {
        return;
    }
    context->syncing = 1;
    if (info.multiple) {
        for (i = 0; i < (unsigned int) info.option_count; i++) {
            selected = 0;
            if (app_controls_select_selected(context, item, i,
                    &selected) == 0) {
                (void) SendMessage(item->hwnd, LB_SETSEL,
                        (WPARAM) (selected ? TRUE : FALSE), (LPARAM) i);
            }
        }
    } else {
        SendMessage(item->hwnd, CB_SETCURSEL,
                (WPARAM) (info.selected_index >= 0 ?
                info.selected_index : -1), 0);
    }
    context->syncing = 0;
    item->multiple = info.multiple ? 1 : 0;
    item->disabled = info.disabled ? 1 : 0;
    item->option_count = info.option_count;
    EnableWindow(item->hwnd, item->disabled ? FALSE : TRUE);
}

static void app_controls_sync_toggle(AppControlsContext *context,
        AppControlsItem *item)
{
    int x;
    int y;
    int width;
    int height;
    int kind;
    int selected;
    int disabled;

    if (context == NULL || item == NULL || item->kind !=
            APP_CONTROLS_KIND_TOGGLE || item->hwnd == NULL ||
            context->document == NULL || context->syncing) {
        return;
    }
    if (PCore_FormControlInfo(context->document, item->form_index, &x, &y,
            &width, &height, &kind, &selected, &disabled) != 0 ||
            kind != item->toggle_kind) {
        return;
    }
    context->syncing = 1;
    SendMessage(item->hwnd, BM_SETCHECK,
            (WPARAM) (selected ? BST_CHECKED : BST_UNCHECKED), 0);
    EnableWindow(item->hwnd, disabled ? FALSE : TRUE);
    context->syncing = 0;
    item->x = x;
    item->y = y;
    item->width = width > 0 ? width : 1;
    item->height = height > 0 ? height : 1;
    item->selected = selected ? 1 : 0;
    item->disabled = disabled ? 1 : 0;
}

static void app_controls_item_point(AppControlsContext *context,
        AppControlsItem *item, int *out_x, int *out_y)
{
    int x;
    int y;

    x = item != NULL ? item->x + item->width / 2 : 0;
    y = item != NULL ? item->y + item->height / 2 : 0;
    if (app_controls_item_geometry(context, item, &x, &y, NULL, NULL)) {
        x += item->width / 2;
        y += item->height / 2;
    }
    if (out_x != NULL) {
        *out_x = x;
    }
    if (out_y != NULL) {
        *out_y = y;
    }
}

static void app_controls_notify(AppControlsContext *context)
{
    if (context != NULL && context->changed != NULL) {
        context->changed(context->pw);
    }
}

static void app_controls_dispatch_focus(AppControlsContext *context,
        AppControlsItem *item, int focused)
{
    int x;
    int y;
    int allowed;

    if (context == NULL || item == NULL || context->document == NULL) {
        return;
    }
    app_controls_item_point(context, item, &x, &y);
    if (focused) {
        (void) PCore_InteractionSetAt(context->document, x, y,
                PCORE_INTERACTION_FOCUS);
        allowed = 1;
        (void) PCore_EventDispatchAt(context->document, x, y, "focus",
                0, 0, &allowed);
        allowed = 1;
        (void) PCore_EventDispatchAt(context->document, x, y, "focusin",
                1, 0, &allowed);
    } else {
        if (context->script != NULL && item->kind ==
                APP_CONTROLS_KIND_TEXT) {
            (void) AppScript_DispatchNativeEditBlur(context->script,
                    item->target_token, x, y);
        }
        (void) PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS);
        allowed = 1;
        (void) PCore_EventDispatchAt(context->document, x, y, "blur",
                0, 0, &allowed);
        allowed = 1;
        (void) PCore_EventDispatchAt(context->document, x, y, "focusout",
                1, 0, &allowed);
    }
    app_controls_notify(context);
}

static int app_controls_button_focus_event(AppControlsContext *context,
        int x, int y, const char *event_type, int bubbles)
{
    int allowed;
    int result;

    if (context == NULL || context->document == NULL || event_type == NULL) {
        return 1;
    }
    if (context->script != NULL) {
        return AppScript_DispatchFocusEvent(context->script, x, y,
                event_type, bubbles, 0);
    }
    allowed = 1;
    result = PCore_EventDispatchAt(context->document, x, y, event_type,
            bubbles, 0, &allowed);
    return result < 0 ? 1 : 0;
}

static void app_controls_button_focus_forget(AppControlsContext *context,
        int clear_core)
{
    if (context == NULL) {
        return;
    }
    if (clear_core && context->button_focus_active &&
            context->document != NULL) {
        (void) PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS);
    }
    context->button_focus_active = 0;
    context->button_space_pending = 0;
    context->button_focus_form_index = 0;
    context->button_focus_x = 0;
    context->button_focus_y = 0;
}

void AppControls_ClearButtonFocus(AppControlsContext *context)
{
    int x;
    int y;

    if (context == NULL || !context->button_focus_active) {
        if (context != NULL) {
            context->button_space_pending = 0;
        }
        return;
    }
    x = context->button_focus_x;
    y = context->button_focus_y;
    context->button_focus_active = 0;
    context->button_space_pending = 0;
    (void) app_controls_button_focus_event(context, x, y, "blur", 0);
    (void) app_controls_button_focus_event(context, x, y, "focusout", 1);
    if (context->document != NULL) {
        (void) PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS);
    }
    context->button_focus_form_index = 0;
    context->button_focus_x = 0;
    context->button_focus_y = 0;
    app_controls_notify(context);
}

static int app_controls_button_focus(AppControlsContext *context,
        unsigned int form_index)
{
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;

    if (context == NULL || context->document == NULL) {
        return 1;
    }
    if (context->button_focus_active &&
            context->button_focus_form_index == form_index) {
        if (PCore_FormControlInfo(context->document, form_index, &x, &y,
                &width, &height, &kind, NULL, &disabled) == 0 &&
                kind == APP_CONTROLS_FORM_BUTTON && !disabled &&
                width > 0 && height > 0) {
            context->button_focus_x = x + width / 2;
            context->button_focus_y = y + height / 2;
            return 0;
        }
        app_controls_button_focus_forget(context, 1);
    }
    AppControls_ClearButtonFocus(context);
    if (PCore_FormControlInfo(context->document, form_index, &x, &y,
            &width, &height, &kind, NULL, &disabled) != 0 ||
            kind != APP_CONTROLS_FORM_BUTTON || disabled || width <= 0 ||
            height <= 0) {
        return 1;
    }
    x += width / 2;
    y += height / 2;
    if (PCore_InteractionSetAt(context->document, x, y,
            PCORE_INTERACTION_FOCUS) < 0) {
        return 1;
    }
    context->button_focus_active = 1;
    context->button_focus_form_index = form_index;
    context->button_focus_x = x;
    context->button_focus_y = y;
    if (app_controls_button_focus_event(context, x, y, "focus", 0) != 0 ||
            app_controls_button_focus_event(context, x, y, "focusin", 1) !=
            0) {
        app_controls_button_focus_forget(context, 1);
        return 1;
    }
    app_controls_notify(context);
    return 0;
}

static int app_controls_before_input(AppControlsContext *context,
        AppControlsItem *item, const char *input_type, const char *data)
{
    int x;
    int y;
    int allowed;

    if (context == NULL || item == NULL || input_type == NULL ||
            data == NULL) {
        return 0;
    }
    if (context->script == NULL) {
        return 1;
    }
    app_controls_item_point(context, item, &x, &y);
    allowed = 0;
    if (AppScript_DispatchNativeEditBeforeInput(context->script,
            item->target_token, x, y, input_type, data, 1, 0,
            &allowed) != 0) {
        return 0;
    }
    return allowed ? 1 : 0;
}

static int app_controls_before_input_char(AppControlsItem *item,
        WCHAR character, const char **out_type, const char **out_data,
        char *data, int data_capacity)
{
    int bytes;

    if (item == NULL || out_type == NULL || out_data == NULL ||
            data == NULL || data_capacity < 2) {
        return 0;
    }
    if (character == (WCHAR) VK_BACK) {
        *out_type = "deleteContentBackward";
        *out_data = "";
        return 1;
    }
    if (character == (WCHAR) VK_RETURN && item->multiline) {
        *out_type = "insertLineBreak";
        *out_data = "\n";
        return 1;
    }
    if (character < 0x20) {
        return 0;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0, &character, 1, data,
            data_capacity - 1, NULL, NULL);
    if (bytes <= 0) {
        bytes = WideCharToMultiByte(CP_ACP, 0, &character, 1, data,
                data_capacity - 1, NULL, NULL);
    }
    if (bytes <= 0 || bytes >= data_capacity) {
        return 0;
    }
    data[bytes] = '\0';
    *out_type = "insertText";
    *out_data = data;
    return 1;
}

static LRESULT CALLBACK app_controls_edit_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    AppControlsItem *item;
    const char *input_type;
    const char *input_data;
    char data[16];

    item = app_controls_find(hwnd);
    if (item == NULL || g_app_controls == NULL) {
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
    if (message == WM_SETFOCUS) {
        LRESULT result;

        result = app_controls_call_original(item, hwnd, message,
                wparam, lparam);
        app_controls_dispatch_focus(g_app_controls, item, 1);
        return result;
    }
    if (message == WM_KILLFOCUS) {
        LRESULT result;

        result = app_controls_call_original(item, hwnd, message,
                wparam, lparam);
        app_controls_dispatch_focus(g_app_controls, item, 0);
        return result;
    }
    if (message == WM_CHAR) {
        input_type = NULL;
        input_data = NULL;
        if (app_controls_before_input_char(item, (WCHAR) wparam,
                &input_type, &input_data, data, sizeof(data)) &&
                !app_controls_before_input(g_app_controls, item,
                input_type, input_data)) {
            return 0;
        }
    } else if (message == WM_KEYDOWN && wparam == VK_DELETE) {
        if (!app_controls_before_input(g_app_controls, item,
                "deleteContentForward", "")) {
            return 0;
        }
    } else if (message == WM_PASTE) {
        if (!app_controls_before_input(g_app_controls, item,
                "insertFromPaste", "")) {
            return 0;
        }
    } else if (message == WM_CUT || message == WM_CLEAR) {
        if (!app_controls_before_input(g_app_controls, item,
                "deleteByCut", "")) {
            return 0;
        }
    }
    return app_controls_call_original(item, hwnd, message, wparam, lparam);
}

static const char *app_controls_toggle_key_name(WPARAM wparam)
{
    if (wparam == VK_SPACE) {
        return "Space";
    }
    if (wparam == VK_RETURN) {
        return "Enter";
    }
    return "Unidentified";
}

static int app_controls_toggle_key(AppControlsContext *context,
        AppControlsItem *item, const char *event_type, WPARAM wparam,
        LPARAM lparam)
{
    int x;
    int y;
    int allowed;

    if (context == NULL || item == NULL || event_type == NULL) {
        return 0;
    }
    if (context->script == NULL) {
        return 1;
    }
    app_controls_item_point(context, item, &x, &y);
    allowed = 0;
    if (AppScript_DispatchKeyEvent(context->script, x, y, event_type,
            app_controls_toggle_key_name(wparam), (unsigned int) wparam, 0,
            ((lparam & 0x40000000L) != 0) ? 1 : 0,
            GetKeyState(VK_SHIFT) < 0 ? 1 : 0,
            GetKeyState(VK_CONTROL) < 0 ? 1 : 0,
            GetKeyState(VK_MENU) < 0 ? 1 : 0, 0, &allowed) != 0) {
        return 0;
    }
    return allowed ? 1 : 0;
}

static int app_controls_toggle_focus(AppControlsContext *context,
        AppControlsItem *item, int focused)
{
    int x;
    int y;

    if (context == NULL || item == NULL || context->document == NULL) {
        return -1;
    }
    app_controls_item_point(context, item, &x, &y);
    if (context->script == NULL) {
        app_controls_dispatch_focus(context, item, focused);
        return 1;
    }
    if (focused) {
        if (PCore_InteractionSetAt(context->document, x, y,
                PCORE_INTERACTION_FOCUS) < 0 ||
                AppScript_DispatchFocusEvent(context->script, x, y,
                "focus", 0, 0) != 0 ||
                AppScript_DispatchFocusEvent(context->script, x, y,
                "focusin", 1, 0) != 0) {
            return -1;
        }
    } else {
        (void) AppScript_DispatchFocusEvent(context->script, x, y,
                "blur", 0, 0);
        (void) AppScript_DispatchFocusEvent(context->script, x, y,
                "focusout", 1, 0);
        if (PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS) < 0) {
            return -1;
        }
    }
    return 1;
}

static LRESULT CALLBACK app_controls_toggle_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    AppControlsItem *item;
    LRESULT result;
    int allowed;
    int repeat;

    item = app_controls_find(hwnd);
    if (item == NULL || g_app_controls == NULL) {
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
    if (message == WM_SETFOCUS) {
        result = app_controls_call_original(item, hwnd, message,
                wparam, lparam);
        if (!g_app_controls->syncing) {
            if (app_controls_toggle_focus(g_app_controls, item, 1) > 0) {
                app_controls_notify(g_app_controls);
            }
        }
        return result;
    }
    if (message == WM_KILLFOCUS) {
        result = app_controls_call_original(item, hwnd, message,
                wparam, lparam);
        if (!g_app_controls->syncing) {
            if (app_controls_toggle_focus(g_app_controls, item, 0) > 0) {
                app_controls_notify(g_app_controls);
            }
        }
        return result;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
            (wparam == VK_SPACE || wparam == VK_RETURN)) {
        repeat = (lparam & 0x40000000L) != 0;
        allowed = app_controls_toggle_key(g_app_controls, item, "keydown",
                wparam, lparam);
        if (!allowed) {
            item->toggle_space_pending = 0;
            return 0;
        }
        if (wparam == VK_SPACE) {
            if (!repeat) {
                item->toggle_space_pending = 1;
            }
        } else if (!repeat) {
            (void) SendMessage(hwnd, BM_CLICK, 0, 0);
        }
        return 0;
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) &&
            (wparam == VK_SPACE || wparam == VK_RETURN)) {
        allowed = app_controls_toggle_key(g_app_controls, item, "keyup",
                wparam, lparam);
        if (wparam == VK_SPACE && item->toggle_space_pending) {
            item->toggle_space_pending = 0;
            if (allowed) {
                (void) SendMessage(hwnd, BM_CLICK, 0, 0);
            }
        }
        return 0;
    }
    return app_controls_call_original(item, hwnd, message, wparam, lparam);
}

static int app_controls_select_native_state(AppControlsItem *item,
        int *out_selected_index, int *out_selected_count)
{
    int selected_index;
    int selected_count;
    int i;

    if (item == NULL || item->hwnd == NULL || out_selected_index == NULL ||
            out_selected_count == NULL) {
        return 1;
    }
    selected_index = -1;
    selected_count = 0;
    if (item->multiple) {
        selected_count = (int) SendMessage(item->hwnd, LB_GETSELCOUNT,
                0, 0);
        if (selected_count < 0) {
            return 1;
        }
        if (selected_count == 1) {
            for (i = 0; i < item->option_count; i++) {
                if (SendMessage(item->hwnd, LB_GETSEL,
                        (WPARAM) i, 0) > 0) {
                    selected_index = i;
                    break;
                }
            }
        }
    } else {
        selected_index = (int) SendMessage(item->hwnd, CB_GETCURSEL, 0, 0);
        if (selected_index == CB_ERR) {
            selected_index = -1;
        } else {
            selected_count = 1;
        }
    }
    *out_selected_index = selected_index;
    *out_selected_count = selected_count;
    return 0;
}

static int app_controls_select_dispatch_commit(AppControlsContext *context,
        AppControlsItem *item)
{
    PCoreSelectInfo info;
    int x;
    int y;
    int result;

    if (context == NULL || item == NULL || context->document == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, item->select_index,
            &info) != 0) {
        return 1;
    }
    app_controls_item_point(context, item, &x, &y);
    if (context->script != NULL) {
        result = AppScript_DispatchNativeSelectCommit(context->script,
                item->target_token, x, y, info.multiple ? 1 : 0,
                info.selected_index, info.selected_count);
    } else {
        result = PCore_EventDispatchAt(context->document, x, y,
                "input", 1, 0, NULL) < 0 ? 1 : 0;
        if (result == 0 && PCore_EventDispatchAt(context->document, x, y,
                "change", 1, 0, NULL) < 0) {
            result = 1;
        }
    }
    return result;
}

static void app_controls_select_restore_core(AppControlsContext *context,
        AppControlsItem *item, const unsigned char *selected,
        int selected_count)
{
    int i;
    int current;

    if (context == NULL || item == NULL || selected == NULL ||
            selected_count < 0) {
        return;
    }
    for (i = 0; i < selected_count; i++) {
        current = 0;
        if (app_controls_select_selected(context, item, (unsigned int) i,
                &current) == 0 && current != (selected[i] ? 1 : 0)) {
            (void) PCore_SelectSetOptionSelected(context->document,
                    item->select_index, (unsigned int) i,
                    selected[i] ? 1 : 0);
        }
    }
}

static int app_controls_select_commit_single(AppControlsContext *context,
        AppControlsItem *item, int selected_index)
{
    PCoreSelectInfo info;
    int previous_index;
    int result;
    int i;

    if (context == NULL || item == NULL || context->document == NULL ||
            selected_index < 0 || selected_index >= item->option_count) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, item->select_index, &info) != 0) {
        return 1;
    }
    previous_index = info.selected_index;
    if (previous_index == selected_index) {
        return 0;
    }
    result = PCore_SelectSetOptionSelected(context->document,
            item->select_index, (unsigned int) selected_index, 1);
    if (result != 0 || app_controls_select_dispatch_commit(context, item) !=
            0) {
        if (previous_index >= 0) {
            (void) PCore_SelectSetOptionSelected(context->document,
                    item->select_index, (unsigned int) previous_index, 1);
        } else {
            for (i = 0; i < item->option_count; i++) {
                (void) PCore_SelectSetOptionSelected(context->document,
                        item->select_index, (unsigned int) i, 0);
            }
        }
        app_controls_sync_select(context, item);
        return 1;
    }
    app_controls_notify(context);
    return 0;
}

static int app_controls_select_commit_multiple(AppControlsContext *context,
        AppControlsItem *item)
{
    PCoreSelectInfo info;
    unsigned char *previous;
    int i;
    int native_selected;
    int core_selected;
    int changed;
    int result;

    if (context == NULL || item == NULL || context->document == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, item->select_index, &info) !=
            0 || info.option_count < 0 || info.option_count >
            APP_CONTROLS_OPTION_TEXT_CAP) {
        return 1;
    }
    previous = (unsigned char *) calloc((size_t) info.option_count + 1,
            sizeof(unsigned char));
    if (previous == NULL && info.option_count > 0) {
        return 1;
    }
    changed = 0;
    for (i = 0; i < info.option_count; i++) {
        core_selected = 0;
        if (app_controls_select_selected(context, item, (unsigned int) i,
                &core_selected) != 0) {
            free(previous);
            return 1;
        }
        previous[i] = (unsigned char) (core_selected ? 1 : 0);
    }
    for (i = 0; i < info.option_count; i++) {
        native_selected = SendMessage(item->hwnd, LB_GETSEL,
                (WPARAM) i, 0) > 0 ? 1 : 0;
        core_selected = previous[i] ? 1 : 0;
        if (native_selected != core_selected) {
            result = PCore_SelectSetOptionSelected(context->document,
                    item->select_index, (unsigned int) i,
                    native_selected);
            if (result != 0) {
                app_controls_select_restore_core(context, item, previous,
                        info.option_count);
                app_controls_sync_select(context, item);
                free(previous);
                return 1;
            }
            changed = 1;
        }
    }
    if (changed && app_controls_select_dispatch_commit(context, item) != 0) {
        app_controls_select_restore_core(context, item, previous,
                info.option_count);
        app_controls_sync_select(context, item);
        free(previous);
        return 1;
    }
    free(previous);
    if (changed) {
        app_controls_notify(context);
    }
    return 0;
}

static int app_controls_select_interaction(AppControlsContext *context,
        AppControlsItem *item, int phase, int *out_should_commit)
{
    PCoreSelectInfo info;
    int selected_index;
    int selected_count;
    int x;
    int y;

    if (out_should_commit != NULL) {
        *out_should_commit = 0;
    }
    if (context == NULL || item == NULL || out_should_commit == NULL ||
            context->document == NULL) {
        return -1;
    }
    if (context->script == NULL) {
        return 0;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, item->select_index, &info) !=
            0 || app_controls_select_native_state(item, &selected_index,
            &selected_count) != 0) {
        return -1;
    }
    if (phase == PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_BEGIN) {
        selected_index = info.selected_index;
        selected_count = info.selected_count;
    }
    app_controls_item_point(context, item, &x, &y);
    if (AppScript_DispatchNativeSelectInteraction(context->script,
            item->target_token, x, y, 0, selected_index, selected_count,
            phase, out_should_commit) != 0) {
        return -1;
    }
    return 1;
}

static int app_controls_select_focus(AppControlsContext *context,
        AppControlsItem *item, int focused)
{
    int x;
    int y;
    int result;

    if (context == NULL || item == NULL || context->document == NULL) {
        return -1;
    }
    if (context->script == NULL) {
        return 0;
    }
    app_controls_item_point(context, item, &x, &y);
    if (focused) {
        result = PCore_InteractionSetAt(context->document, x, y,
                PCORE_INTERACTION_FOCUS);
        if (result < 0) {
            return -1;
        }
    }
    result = AppScript_DispatchNativeSelectFocus(context->script,
            item->target_token, x, y, focused);
    if (!focused) {
        if (PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS) < 0) {
            return -1;
        }
    } else if (result != 0) {
        (void) PCore_InteractionClear(context->document,
                PCORE_INTERACTION_FOCUS);
    }
    return result == 0 ? 1 : -1;
}

static const char *app_controls_select_key_name(WPARAM wparam)
{
    switch (wparam) {
    case VK_BACK:
        return "Backspace";
    case VK_TAB:
        return "Tab";
    case VK_RETURN:
        return "Enter";
    case VK_ESCAPE:
        return "Escape";
    case VK_LEFT:
        return "ArrowLeft";
    case VK_RIGHT:
        return "ArrowRight";
    case VK_UP:
        return "ArrowUp";
    case VK_DOWN:
        return "ArrowDown";
    case VK_HOME:
        return "Home";
    case VK_END:
        return "End";
    case VK_PRIOR:
        return "PageUp";
    case VK_NEXT:
        return "PageDown";
    case VK_SPACE:
        return "Space";
    default:
        return "Unidentified";
    }
}

static int app_controls_select_key(AppControlsContext *context,
        AppControlsItem *item, const char *event_type, WPARAM wparam,
        LPARAM lparam)
{
    int x;
    int y;
    int default_allowed;

    if (context == NULL || item == NULL || event_type == NULL) {
        return 1;
    }
    if (context->script == NULL) {
        return 1;
    }
    app_controls_item_point(context, item, &x, &y);
    default_allowed = 1;
    if (AppScript_DispatchNativeSelectKey(context->script,
            item->target_token, x, y, event_type,
            app_controls_select_key_name(wparam), (unsigned int) wparam, 0,
            ((lparam & 0x40000000L) != 0) ? 1 : 0,
            GetKeyState(VK_SHIFT) < 0 ? 1 : 0,
            GetKeyState(VK_CONTROL) < 0 ? 1 : 0,
            GetKeyState(VK_MENU) < 0 ? 1 : 0, 0, &default_allowed) != 0) {
        return 1;
    }
    return default_allowed ? 1 : 0;
}

static void app_controls_select_sync_after_key(AppControlsContext *context,
        AppControlsItem *item, int before)
{
    LRESULT after;

    if (context == NULL || item == NULL || item->multiple ||
            item->dropdown_active || before < 0 || item->hwnd == NULL ||
            SendMessage(item->hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0) {
        return;
    }
    after = SendMessage(item->hwnd, CB_GETCURSEL, 0, 0);
    if (after == CB_ERR || after == before) {
        return;
    }
    (void) app_controls_select_commit_single(context, item, (int) after);
}

static LRESULT CALLBACK app_controls_select_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    AppControlsItem *item;
    LRESULT result;
    int key_message;
    int key_before;

    item = app_controls_find(hwnd);
    if (item == NULL || g_app_controls == NULL) {
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
    key_message = message == WM_KEYDOWN || message == WM_KEYUP ||
            message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    key_before = -1;
    if (key_message && !item->multiple) {
        key_before = (int) SendMessage(hwnd, CB_GETCURSEL, 0, 0);
    }
    if (message == WM_SETFOCUS) {
        result = app_controls_call_original(item, hwnd, message,
                wparam, lparam);
        if (g_app_controls->syncing) {
            return result;
        }
        if (g_app_controls->script == NULL) {
            app_controls_dispatch_focus(g_app_controls, item, 1);
        } else if (app_controls_select_focus(g_app_controls, item, 1) > 0) {
            app_controls_notify(g_app_controls);
        }
        return result;
    }
    if (message == WM_KILLFOCUS) {
        result = app_controls_call_original(item, hwnd, message,
                wparam, lparam);
        if (g_app_controls->syncing) {
            return result;
        }
        if (g_app_controls->script == NULL) {
            app_controls_dispatch_focus(g_app_controls, item, 0);
        } else if (app_controls_select_focus(g_app_controls, item, 0) > 0) {
            app_controls_notify(g_app_controls);
        }
        return result;
    }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (!app_controls_select_key(g_app_controls, item, "keydown",
                wparam, lparam)) {
            return 0;
        }
    } else if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        if (!app_controls_select_key(g_app_controls, item, "keyup",
                wparam, lparam)) {
            return 0;
        }
    }
    result = app_controls_call_original(item, hwnd, message, wparam, lparam);
    if (key_message) {
        app_controls_select_sync_after_key(g_app_controls, item, key_before);
    }
    return result;
}

static void app_controls_sync_all_toggles(AppControlsContext *context)
{
    unsigned int i;

    if (context == NULL) {
        return;
    }
    for (i = 0; i < context->count; i++) {
        if (context->items[i].kind == APP_CONTROLS_KIND_TOGGLE) {
            app_controls_sync_toggle(context, &context->items[i]);
        }
    }
}

static int app_controls_toggle_activate(AppControlsContext *context,
        AppControlsItem *item)
{
    int x;
    int y;
    int width;
    int height;
    int kind;
    int selected_before;
    int selected_after;
    int disabled;
    int dirty_x;
    int dirty_y;
    int dirty_width;
    int dirty_height;
    int default_allowed;
    int product_started;
    int consumed;
    int changed;

    if (context == NULL || item == NULL || context->document == NULL ||
            item->kind != APP_CONTROLS_KIND_TOGGLE) {
        return 0;
    }
    if (PCore_FormControlInfo(context->document, item->form_index, &x, &y,
            &width, &height, &kind, &selected_before, &disabled) != 0 ||
            (kind != PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX &&
            kind != PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO)) {
        return 1;
    }
    x += width / 2;
    y += height / 2;
    if (disabled) {
        app_controls_sync_toggle(context, item);
        return 1;
    }
    product_started = 0;
    if (context->script != NULL) {
        default_allowed = 1;
        if (AppScript_DispatchNativeToggle(context->script,
                item->target_token, x, y,
                PBROWSER_SCRIPT_NATIVE_TOGGLE_CLICK, kind, 0,
                selected_before ? 1 : 0, selected_before ? 1 : 0,
                &default_allowed) != 0 || !default_allowed) {
            app_controls_sync_toggle(context, item);
            return 1;
        }
        product_started = 1;
    }
    consumed = PCore_FormActivateAt(context->document, x, y, &dirty_x,
            &dirty_y, &dirty_width, &dirty_height);
    if (!consumed || PCore_FormControlInfo(context->document,
            item->form_index, NULL, NULL, NULL, NULL, NULL,
            &selected_after, NULL) != 0) {
        if (product_started) {
            (void) AppScript_DispatchNativeToggle(context->script,
                    item->target_token, x, y,
                    PBROWSER_SCRIPT_NATIVE_TOGGLE_CANCEL, kind, 0,
                    selected_before ? 1 : 0, selected_before ? 1 : 0,
                    &default_allowed);
        }
        app_controls_sync_toggle(context, item);
        return 1;
    }
    changed = selected_before != (selected_after ? 1 : 0);
    app_controls_sync_all_toggles(context);
    if (product_started) {
        (void) AppScript_DispatchNativeToggle(context->script,
                item->target_token, x, y,
                PBROWSER_SCRIPT_NATIVE_TOGGLE_COMMIT, kind, 0,
                selected_before ? 1 : 0, selected_after ? 1 : 0,
                &default_allowed);
    } else if (changed) {
        (void) PCore_EventDispatchAt(context->document, x, y, "input",
                1, 0, NULL);
        (void) PCore_EventDispatchAt(context->document, x, y, "change",
                1, 0, NULL);
    }
    if (changed) {
        app_controls_notify(context);
    }
    return 1;
}

static int app_controls_button_activate(AppControlsContext *context,
        unsigned int form_index, int x, int y)
{
    unsigned long target_token;
    int left;
    int top;
    int width;
    int height;
    int kind;
    int disabled;
    int default_allowed;

    if (context == NULL || context->document == NULL) {
        return 1;
    }
    if (PCore_FormControlInfo(context->document, form_index, &left, &top,
            &width, &height, &kind, NULL, &disabled) != 0 ||
            kind != APP_CONTROLS_FORM_BUTTON || disabled || width <= 0 ||
            height <= 0 || x < left || x >= left + width || y < top ||
            y >= top + height) {
        return 1;
    }
    target_token = (unsigned long) form_index + 1UL;
    if (context->script == NULL) {
        default_allowed = 1;
        (void) PCore_EventDispatchAt(context->document, x, y, "click",
                1, 1, &default_allowed);
        return 1;
    }
    default_allowed = 1;
    if (AppScript_DispatchNativeButton(context->script, target_token, x, y,
            PBROWSER_SCRIPT_NATIVE_BUTTON_CLICK,
            PBROWSER_SCRIPT_NATIVE_BUTTON_BUTTON, 0, 0,
            &default_allowed) != 0 || !default_allowed) {
        (void) AppScript_DispatchNativeButton(context->script, target_token,
                x, y, PBROWSER_SCRIPT_NATIVE_BUTTON_CANCEL,
                PBROWSER_SCRIPT_NATIVE_BUTTON_BUTTON, 0, 0,
                &default_allowed);
        AppScript_ResetNativeButtonState(context->script);
        return 1;
    }
    if (AppScript_DispatchNativeButton(context->script, target_token, x, y,
            PBROWSER_SCRIPT_NATIVE_BUTTON_COMMIT,
            PBROWSER_SCRIPT_NATIVE_BUTTON_BUTTON, 0, 0,
            &default_allowed) != 0) {
        (void) AppScript_DispatchNativeButton(context->script, target_token,
                x, y, PBROWSER_SCRIPT_NATIVE_BUTTON_CANCEL,
                PBROWSER_SCRIPT_NATIVE_BUTTON_BUTTON, 0, 0,
                &default_allowed);
    }
    AppScript_ResetNativeButtonState(context->script);
    return 1;
}

int AppControls_HandleButtonPointer(AppControlsContext *context,
        int document_x, int document_y)
{
    unsigned int form_index;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;

    if (context == NULL || context->document == NULL) {
        return 0;
    }
    form_index = 0;
    while (PCore_FormControlInfo(context->document, form_index, &x, &y,
            &width, &height, &kind, NULL, &disabled) == 0) {
        if (kind == APP_CONTROLS_FORM_BUTTON && width > 0 && height > 0 &&
                document_x >= x && document_x < x + width &&
                document_y >= y && document_y < y + height) {
            if (disabled) {
                return 1;
            }
            if (app_controls_button_focus(context, form_index) != 0) {
                return 1;
            }
            (void) app_controls_button_activate(context, form_index,
                    document_x, document_y);
            return 1;
        }
        form_index++;
    }
    return 0;
}

int AppControls_HandleButtonKey(AppControlsContext *context, UINT message,
        WPARAM key, LPARAM flags)
{
    const char *key_name;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;
    int allowed;
    int repeat;

    if (context == NULL || !context->button_focus_active ||
            (message != WM_KEYDOWN && message != WM_KEYUP) ||
            (key != VK_SPACE && key != VK_RETURN)) {
        return 0;
    }
    if (context->document == NULL || PCore_FormControlInfo(
            context->document, context->button_focus_form_index, &x, &y,
            &width, &height, &kind, NULL, &disabled) != 0 ||
            kind != APP_CONTROLS_FORM_BUTTON || disabled || width <= 0 ||
            height <= 0) {
        app_controls_button_focus_forget(context, 1);
        return 1;
    }
    x += width / 2;
    y += height / 2;
    context->button_focus_x = x;
    context->button_focus_y = y;
    key_name = key == VK_SPACE ? "Space" : "Enter";
    repeat = (flags & 0x40000000L) != 0;
    allowed = 1;
    if (context->script != NULL && AppScript_DispatchKeyEvent(
            context->script, x, y,
            message == WM_KEYDOWN ? "keydown" : "keyup", key_name,
            (unsigned int) key, 0, repeat,
            GetKeyState(VK_SHIFT) < 0 ? 1 : 0,
            GetKeyState(VK_CONTROL) < 0 ? 1 : 0,
            GetKeyState(VK_MENU) < 0 ? 1 : 0, 0, &allowed) != 0) {
        allowed = 0;
    }
    if (message == WM_KEYDOWN) {
        if (!allowed) {
            context->button_space_pending = 0;
            return 1;
        }
        if (key == VK_SPACE) {
            if (!repeat) {
                context->button_space_pending = 1;
            }
        } else if (!repeat) {
            (void) app_controls_button_activate(context,
                    context->button_focus_form_index, x, y);
        }
        return 1;
    }
    if (key == VK_SPACE && context->button_space_pending) {
        context->button_space_pending = 0;
        if (allowed) {
            (void) app_controls_button_activate(context,
                    context->button_focus_form_index, x, y);
        }
    }
    return 1;
}

void AppControls_PrepareReconcile(AppControlsContext *context)
{
    if (context == NULL) {
        return;
    }
    if (context->script != NULL) {
        AppScript_ResetNativeButtonState(context->script);
    }
    app_controls_button_focus_forget(context, 1);
}

static int app_controls_rebuild_item(AppControlsContext *context,
        AppControlsItem *item, unsigned int index, int control_id,
        int scroll_x, int scroll_y)
{
    PCoreTextInputInfo info;
    WCHAR *wide_value;
    char *value;
    char *edit_value;
    DWORD style;
    HWND hwnd;
    WNDPROC original_proc;
    int multiline;
    int value_capacity;
    int wide_capacity;

    memset(&info, 0, sizeof(info));
    if (PCore_TextInputInfo(context->document, index, &info, NULL, 0) !=
            0 || PCore_TextInputIsMultiline(context->document, index,
            &multiline) != 0 || info.value_bytes < 0 ||
            info.value_bytes >= APP_CONTROLS_TEXT_CAP) {
        return 1;
    }
    value_capacity = info.value_bytes + 1;
    value = (char *) malloc((size_t) value_capacity);
    if (value == NULL || PCore_TextInputInfo(context->document, index,
            NULL, value, value_capacity) != 0) {
        free(value);
        return 1;
    }
    edit_value = app_controls_edit_value(value, multiline);
    free(value);
    if (edit_value == NULL) {
        return 1;
    }
    wide_capacity = (int) strlen(edit_value) + 1;
    wide_value = (WCHAR *) malloc((size_t) wide_capacity * sizeof(WCHAR));
    if (wide_value == NULL || app_controls_utf8_to_wide(edit_value,
            wide_value, wide_capacity) <= 0) {
        free(wide_value);
        free(edit_value);
        return 1;
    }
    style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_LEFT;
    if (multiline) {
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
                WS_VSCROLL;
    } else {
        style |= ES_AUTOHSCROLL;
    }
    if (info.password) {
        style |= ES_PASSWORD;
    }
    hwnd = CreateWindowW(L"EDIT", wide_value, style,
            info.x - scroll_x, info.y - scroll_y,
            info.width > 0 ? info.width : 1,
            info.height > 0 ? info.height : 1, context->parent,
            (HMENU) (APP_CONTROLS_ID_BASE + control_id),
            context->instance, NULL);
    free(wide_value);
    free(edit_value);
    if (hwnd == NULL) {
        return 1;
    }
    memset(item, 0, sizeof(*item));
    item->hwnd = hwnd;
    item->kind = APP_CONTROLS_KIND_TEXT;
    item->text_index = index;
    item->target_token = (unsigned long) index + 1UL;
    item->multiline = multiline ? 1 : 0;
    item->password = info.password ? 1 : 0;
    item->read_only = info.read_only ? 1 : 0;
    item->disabled = info.disabled ? 1 : 0;
    item->x = info.x;
    item->y = info.y;
    item->width = info.width > 0 ? info.width : 1;
    item->height = info.height > 0 ? info.height : 1;
    original_proc = (WNDPROC) SetWindowLong(hwnd, GWL_WNDPROC,
            (LONG) app_controls_edit_proc);
    item->original_proc = original_proc;
    if (original_proc == NULL) {
        DestroyWindow(hwnd);
        memset(item, 0, sizeof(*item));
        return 1;
    }
    SendMessage(hwnd, WM_SETFONT, (WPARAM) GetStockObject(SYSTEM_FONT),
            TRUE);
    if (info.max_length >= 0) {
        SendMessage(hwnd, EM_LIMITTEXT, (WPARAM) info.max_length, 0);
    }
    if (item->read_only) {
        SendMessage(hwnd, EM_SETREADONLY, TRUE, 0);
    }
    EnableWindow(hwnd, item->disabled ? FALSE : TRUE);
    return 0;
}

static int app_controls_rebuild_select_item(AppControlsContext *context,
        AppControlsItem *item, unsigned int index, int control_id,
        int scroll_x, int scroll_y)
{
    PCoreSelectInfo info;
    WCHAR *wide_label;
    const WCHAR *class_name;
    DWORD style;
    HWND hwnd;
    WNDPROC original_proc;
    LRESULT add_result;
    unsigned int option_index;
    int selected;

    memset(&info, 0, sizeof(info));
    if (PCore_SelectInfo(context->document, index, &info) != 0 ||
            info.option_count < 0 || info.option_count >
            APP_CONTROLS_OPTION_TEXT_CAP) {
        return 1;
    }
    if (info.multiple) {
        class_name = L"LISTBOX";
        style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                WS_BORDER | LBS_NOTIFY | LBS_MULTIPLESEL |
                LBS_NOINTEGRALHEIGHT;
    } else {
        class_name = L"COMBOBOX";
        style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                CBS_DROPDOWNLIST;
    }
    hwnd = CreateWindowW(class_name, L"", style,
            info.x - scroll_x, info.y - scroll_y,
            info.width > 0 ? info.width : 1,
            info.multiple ? (info.height > 0 ? info.height : 1) :
            app_controls_select_window_height(&info), context->parent,
            (HMENU) (APP_CONTROLS_ID_BASE + control_id),
            context->instance, NULL);
    if (hwnd == NULL) {
        return 1;
    }
    memset(item, 0, sizeof(*item));
    item->hwnd = hwnd;
    item->kind = APP_CONTROLS_KIND_SELECT;
    item->select_index = index;
    item->target_token = (unsigned long) index + 1UL;
    item->multiple = info.multiple ? 1 : 0;
    item->disabled = info.disabled ? 1 : 0;
    item->option_count = info.option_count;
    item->x = info.x;
    item->y = info.y;
    item->width = info.width > 0 ? info.width : 1;
    item->height = info.height > 0 ? info.height : 1;
    original_proc = (WNDPROC) SetWindowLong(hwnd, GWL_WNDPROC,
            (LONG) app_controls_select_proc);
    item->original_proc = original_proc;
    if (original_proc == NULL) {
        DestroyWindow(hwnd);
        memset(item, 0, sizeof(*item));
        return 1;
    }
    SendMessage(hwnd, WM_SETFONT, (WPARAM) GetStockObject(SYSTEM_FONT),
            TRUE);
    for (option_index = 0; option_index < (unsigned int) info.option_count;
            option_index++) {
        wide_label = NULL;
        if (app_controls_select_option_label(context, item, option_index,
                &wide_label) != 0) {
            SetWindowLong(hwnd, GWL_WNDPROC, (LONG) original_proc);
            DestroyWindow(hwnd);
            memset(item, 0, sizeof(*item));
            return 1;
        }
        add_result = SendMessage(hwnd, info.multiple ? LB_ADDSTRING :
                CB_ADDSTRING, 0, (LPARAM) wide_label);
        free(wide_label);
        if (info.multiple) {
            if (add_result == LB_ERR || add_result == LB_ERRSPACE) {
                SetWindowLong(hwnd, GWL_WNDPROC, (LONG) original_proc);
                DestroyWindow(hwnd);
                memset(item, 0, sizeof(*item));
                return 1;
            }
            selected = 0;
            if (app_controls_select_selected(context, item, option_index,
                    &selected) != 0) {
                SetWindowLong(hwnd, GWL_WNDPROC, (LONG) original_proc);
                DestroyWindow(hwnd);
                memset(item, 0, sizeof(*item));
                return 1;
            }
            SendMessage(hwnd, LB_SETSEL, (WPARAM) (selected ? TRUE : FALSE),
                    (LPARAM) add_result);
        }
    }
    if (!info.multiple) {
        SendMessage(hwnd, CB_SETCURSEL,
                (WPARAM) (info.selected_index >= 0 ?
                info.selected_index : -1), 0);
    }
    EnableWindow(hwnd, item->disabled ? FALSE : TRUE);
    if (app_controls_select_fingerprint(context, item,
            &item->option_fingerprint) != 0) {
        SetWindowLong(hwnd, GWL_WNDPROC, (LONG) original_proc);
        DestroyWindow(hwnd);
        memset(item, 0, sizeof(*item));
        return 1;
    }
    return 0;
}

static int app_controls_rebuild_toggle_item(AppControlsContext *context,
        AppControlsItem *item, unsigned int toggle_index,
        unsigned int form_index, int control_id, int scroll_x, int scroll_y)
{
    DWORD style;
    HWND hwnd;
    WNDPROC original_proc;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int selected;
    int disabled;

    if (context == NULL || item == NULL || context->document == NULL ||
            app_controls_toggle_info_at(context->document, toggle_index,
            NULL, &x, &y, &width, &height, &kind, &selected,
            &disabled) != 0 || (kind !=
            PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX && kind !=
            PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO)) {
        return 1;
    }
    style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_NOTIFY;
    style |= kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO ?
            BS_RADIOBUTTON : BS_CHECKBOX;
    hwnd = CreateWindowW(L"BUTTON", L"", style,
            x - scroll_x, y - scroll_y, width > 0 ? width : 1,
            height > 0 ? height : 1, context->parent,
            (HMENU) (APP_CONTROLS_ID_BASE + control_id),
            context->instance, NULL);
    if (hwnd == NULL) {
        return 1;
    }
    memset(item, 0, sizeof(*item));
    item->hwnd = hwnd;
    item->kind = APP_CONTROLS_KIND_TOGGLE;
    item->toggle_index = toggle_index;
    item->form_index = form_index;
    item->target_token = (unsigned long) toggle_index + 1UL;
    item->toggle_kind = kind;
    item->selected = selected ? 1 : 0;
    item->disabled = disabled ? 1 : 0;
    item->x = x;
    item->y = y;
    item->width = width > 0 ? width : 1;
    item->height = height > 0 ? height : 1;
    original_proc = (WNDPROC) SetWindowLong(hwnd, GWL_WNDPROC,
            (LONG) app_controls_toggle_proc);
    item->original_proc = original_proc;
    if (original_proc == NULL) {
        DestroyWindow(hwnd);
        memset(item, 0, sizeof(*item));
        return 1;
    }
    SendMessage(hwnd, WM_SETFONT, (WPARAM) GetStockObject(SYSTEM_FONT),
            TRUE);
    SendMessage(hwnd, BM_SETCHECK,
            (WPARAM) (item->selected ? BST_CHECKED : BST_UNCHECKED), 0);
    EnableWindow(hwnd, item->disabled ? FALSE : TRUE);
    return 0;
}

AppControlsContext *AppControls_Create(HWND parent, HINSTANCE instance,
        void *pw, AppControlsChangedFn changed)
{
    AppControlsContext *context;

    if (parent == NULL) {
        return NULL;
    }
    context = (AppControlsContext *) calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->parent = parent;
    context->instance = instance != NULL ? instance : GetModuleHandle(NULL);
    context->pw = pw;
    context->changed = changed;
    g_app_controls = context;
    return context;
}

void AppControls_ClearPage(AppControlsContext *context)
{
    unsigned int i;

    if (context == NULL) {
        return;
    }
    if (context->script != NULL) {
        AppScript_ResetNativeEditState(context->script);
        AppScript_ResetNativeSelectState(context->script);
        AppScript_ResetNativeToggleState(context->script);
        AppScript_ResetNativeButtonState(context->script);
    }
    app_controls_button_focus_forget(context, 1);
    /* Detaching the document before DestroyWindow prevents the native
     * child's WM_KILLFOCUS path from dispatching a stale blur while the old
     * page is being torn down. */
    context->document = NULL;
    context->script = NULL;
    context->syncing = 1;
    for (i = 0; i < context->count; i++) {
        if (context->items[i].hwnd != NULL) {
            if (context->items[i].original_proc != NULL) {
                SetWindowLong(context->items[i].hwnd, GWL_WNDPROC,
                        (LONG) context->items[i].original_proc);
            }
            DestroyWindow(context->items[i].hwnd);
        }
    }
    context->syncing = 0;
    memset(context->items, 0, sizeof(context->items));
    context->count = 0;
}

void AppControls_Destroy(AppControlsContext *context)
{
    if (context == NULL) {
        return;
    }
    AppControls_ClearPage(context);
    if (g_app_controls == context) {
        g_app_controls = NULL;
    }
    free(context);
}

int AppControls_Rebuild(AppControlsContext *context, HANDLE document,
        AppScriptContext *script, int scroll_x, int scroll_y)
{
    unsigned int index;
    unsigned int form_index;
    unsigned int toggle_index;
    int form_kind;

    if (context == NULL || document == NULL) {
        return 1;
    }
    AppControls_ClearPage(context);
    context->document = document;
    context->script = script;
    index = 0;
    while (context->count < APP_CONTROLS_MAX &&
            PCore_TextInputInfo(document, index, NULL, NULL, 0) == 0) {
        if (app_controls_rebuild_item(context, &context->items[index],
                index, (int) context->count, scroll_x, scroll_y) != 0) {
            AppControls_ClearPage(context);
            return 1;
        }
        index++;
        context->count++;
    }
    index = 0;
    while (context->count < APP_CONTROLS_MAX &&
            PCore_SelectInfo(document, index, NULL) == 0) {
        if (app_controls_rebuild_select_item(context,
                &context->items[context->count], index,
                (int) context->count, scroll_x, scroll_y) != 0) {
            AppControls_ClearPage(context);
            return 1;
        }
        index++;
        context->count++;
    }
    form_index = 0;
    toggle_index = 0;
    while (PCore_FormControlInfo(document, form_index, NULL, NULL, NULL,
            NULL, &form_kind, NULL, NULL) == 0) {
        if (form_kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX ||
                form_kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO) {
            if (toggle_index >= PBROWSER_SCRIPT_NATIVE_TOGGLE_MAX_TARGETS ||
                    context->count >= APP_CONTROLS_MAX ||
                    app_controls_rebuild_toggle_item(context,
                    &context->items[context->count], toggle_index,
                    form_index, (int) context->count, scroll_x,
                    scroll_y) != 0) {
                AppControls_ClearPage(context);
                return 1;
            }
            toggle_index++;
            context->count++;
        }
        form_index++;
    }
    AppControls_Reposition(context, document, scroll_x, scroll_y);
    return 0;
}

static int app_controls_rebuild_selects(AppControlsContext *context,
        HANDLE document, AppScriptContext *script, int scroll_x, int scroll_y,
        unsigned int text_count, unsigned int select_count)
{
    HWND old_focus;
    unsigned int focus_index;
    unsigned int i;
    unsigned int select_index;
    AppControlsItem *item;

    if (context == NULL || document == NULL ||
            text_count + select_count > context->count) {
        return 1;
    }
    old_focus = GetFocus();
    focus_index = APP_CONTROLS_NO_SELECT_INDEX;
    for (i = 0; i < select_count; i++) {
        item = &context->items[text_count + i];
        if (item->kind == APP_CONTROLS_KIND_SELECT &&
                item->hwnd == old_focus) {
            focus_index = item->select_index;
            break;
        }
    }
    if (script != NULL) {
        AppScript_ResetNativeSelectState(script);
    }
    context->syncing = 1;
    for (i = 0; i < select_count; i++) {
        item = &context->items[text_count + i];
        select_index = item->select_index;
        if (item->hwnd != NULL) {
            if (item->original_proc != NULL) {
                SetWindowLong(item->hwnd, GWL_WNDPROC,
                        (LONG) item->original_proc);
            }
            DestroyWindow(item->hwnd);
        }
        item->hwnd = NULL;
        item->original_proc = NULL;
        item->kind = APP_CONTROLS_KIND_SELECT;
        item->select_index = select_index;
    }
    for (i = 0; i < select_count; i++) {
        item = &context->items[text_count + i];
        if (app_controls_rebuild_select_item(context, item,
                item->select_index, (int) (text_count + i), scroll_x,
                scroll_y) != 0) {
            context->syncing = 0;
            return 1;
        }
    }
    context->syncing = 0;
    AppControls_Reposition(context, document, scroll_x, scroll_y);
    if (focus_index != APP_CONTROLS_NO_SELECT_INDEX) {
        for (i = 0; i < select_count; i++) {
            item = &context->items[text_count + i];
            if (item->select_index == focus_index && item->hwnd != NULL &&
                    IsWindowEnabled(item->hwnd)) {
                SetFocus(item->hwnd);
                break;
            }
        }
    }
    return 0;
}

int AppControls_Reconcile(AppControlsContext *context, HANDLE document,
        AppScriptContext *script, int scroll_x, int scroll_y)
{
    PCoreTextInputInfo text_info;
    PCoreSelectInfo select_info;
    AppControlsItem *item;
    unsigned long fingerprint;
    unsigned int text_count;
    unsigned int select_count;
    unsigned int toggle_count;
    unsigned int toggle_offset;
    unsigned int toggle_index;
    unsigned int form_index;
    unsigned int i;
    int multiline;
    int toggle_kind;
    int rebuild_selects;

    if (context == NULL || document == NULL) {
        return 1;
    }
    context->document = document;
    context->script = script;
    text_count = 0;
    while (text_count < APP_CONTROLS_MAX &&
            PCore_TextInputInfo(document, text_count, NULL, NULL, 0) == 0) {
        text_count++;
    }
    if (text_count == APP_CONTROLS_MAX &&
            PCore_TextInputInfo(document, text_count, NULL, NULL, 0) == 0) {
        return 1;
    }
    select_count = 0;
    while (select_count < APP_CONTROLS_MAX &&
            PCore_SelectInfo(document, select_count, NULL) == 0) {
        select_count++;
    }
    if (select_count == APP_CONTROLS_MAX &&
            PCore_SelectInfo(document, select_count, NULL) == 0) {
        return 1;
    }
    if (app_controls_toggle_count(document, &toggle_count) != 0) {
        return 1;
    }
    if (text_count + select_count + toggle_count != context->count ||
            text_count + select_count + toggle_count > APP_CONTROLS_MAX ||
            toggle_count > PBROWSER_SCRIPT_NATIVE_TOGGLE_MAX_TARGETS) {
        return AppControls_Rebuild(context, document, script, scroll_x,
                scroll_y);
    }
    for (i = 0; i < text_count; i++) {
        item = &context->items[i];
        memset(&text_info, 0, sizeof(text_info));
        if (item->kind != APP_CONTROLS_KIND_TEXT || item->text_index != i ||
                PCore_TextInputInfo(document, i, &text_info, NULL, 0) != 0 ||
                PCore_TextInputIsMultiline(document, i, &multiline) != 0 ||
                (multiline ? 1 : 0) != item->multiline ||
                (text_info.password ? 1 : 0) != item->password ||
                (text_info.read_only ? 1 : 0) != item->read_only) {
            return AppControls_Rebuild(context, document, script, scroll_x,
                    scroll_y);
        }
    }
    rebuild_selects = 0;
    for (i = 0; i < select_count; i++) {
        item = &context->items[text_count + i];
        memset(&select_info, 0, sizeof(select_info));
        if (item->kind != APP_CONTROLS_KIND_SELECT ||
                item->select_index != i || PCore_SelectInfo(document, i,
                &select_info) != 0) {
            return AppControls_Rebuild(context, document, script, scroll_x,
                    scroll_y);
        }
        if ((select_info.multiple ? 1 : 0) != item->multiple ||
                select_info.option_count != item->option_count) {
            rebuild_selects = 1;
        }
        if (app_controls_select_fingerprint(context, item,
                &fingerprint) != 0) {
            return AppControls_Rebuild(context, document, script, scroll_x,
                    scroll_y);
        }
        if (fingerprint != item->option_fingerprint) {
            rebuild_selects = 1;
        }
    }
    if (rebuild_selects && app_controls_rebuild_selects(context, document,
            script, scroll_x, scroll_y, text_count, select_count) != 0) {
        return AppControls_Rebuild(context, document, script, scroll_x,
                scroll_y);
    }
    toggle_offset = text_count + select_count;
    toggle_index = 0;
    form_index = 0;
    while (PCore_FormControlInfo(document, form_index, NULL, NULL, NULL,
            NULL, &toggle_kind, NULL, NULL) == 0) {
        if (toggle_kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX ||
                toggle_kind == PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO) {
            item = &context->items[toggle_offset + toggle_index];
            if (item->kind != APP_CONTROLS_KIND_TOGGLE ||
                    item->toggle_index != toggle_index ||
                    item->form_index != form_index ||
                    item->toggle_kind != toggle_kind) {
                return AppControls_Rebuild(context, document, script,
                        scroll_x, scroll_y);
            }
            toggle_index++;
        }
        form_index++;
    }
    AppControls_Reposition(context, document, scroll_x, scroll_y);
    return 0;
}

void AppControls_Reposition(AppControlsContext *context, HANDLE document,
        int scroll_x, int scroll_y)
{
    RECT client;
    int form_kind;
    int button_x;
    int button_y;
    int button_width;
    int button_height;
    PCoreSelectInfo select_info;
    unsigned int i;
    int x;
    int y;
    int width;
    int height;
    int visible_height;

    if (context == NULL || document == NULL || context->parent == NULL) {
        return;
    }
    context->document = document;
    GetClientRect(context->parent, &client);
    if (context->button_focus_active) {
        if (PCore_FormControlInfo(document,
                context->button_focus_form_index, &button_x, &button_y,
                &button_width, &button_height, &form_kind, NULL, NULL) == 0 &&
                form_kind == APP_CONTROLS_FORM_BUTTON && button_width > 0 &&
                button_height > 0) {
            context->button_focus_x = button_x + button_width / 2;
            context->button_focus_y = button_y + button_height / 2;
        } else {
            app_controls_button_focus_forget(context, 1);
        }
    }
    for (i = 0; i < context->count; i++) {
        if (context->items[i].kind == APP_CONTROLS_KIND_SELECT) {
            app_controls_sync_select(context, &context->items[i]);
        } else if (context->items[i].kind == APP_CONTROLS_KIND_TOGGLE) {
            app_controls_sync_toggle(context, &context->items[i]);
        }
        if (context->items[i].hwnd == NULL ||
                !app_controls_item_geometry(context, &context->items[i],
                &x, &y, &width, &height)) {
            continue;
        }
        visible_height = height;
        x -= scroll_x;
        y -= scroll_y;
        if (context->items[i].kind == APP_CONTROLS_KIND_SELECT &&
                !context->items[i].multiple) {
            memset(&select_info, 0, sizeof(select_info));
            if (PCore_SelectInfo(document, context->items[i].select_index,
                    &select_info) == 0) {
                height = app_controls_select_window_height(&select_info);
            }
        }
        MoveWindow(context->items[i].hwnd, x, y, width, height, TRUE);
        if (x + width <= client.left || x >= client.right ||
                y + visible_height <= client.top || y >= client.bottom) {
            ShowWindow(context->items[i].hwnd, SW_HIDE);
        } else {
            ShowWindow(context->items[i].hwnd, SW_SHOW);
        }
    }
}

int AppControls_HandleCommand(AppControlsContext *context, WPARAM wparam,
        LPARAM lparam)
{
    AppControlsItem *item;
    char *value;
    int x;
    int y;
    int result;
    int selected_index;
    int selected_count;
    int should_commit;
    int interaction_result;

    if (context == NULL || lparam == 0) {
        return 0;
    }
    item = app_controls_find((HWND) lparam);
    if (item == NULL) {
        return 0;
    }
    if (context->syncing || context->document == NULL) {
        return 1;
    }
    if (item->kind == APP_CONTROLS_KIND_TOGGLE) {
        if (HIWORD(wparam) == BN_CLICKED) {
            return app_controls_toggle_activate(context, item);
        }
        return 1;
    }
    if (item->kind == APP_CONTROLS_KIND_SELECT) {
        if (!item->multiple && HIWORD(wparam) == CBN_DROPDOWN) {
            item->dropdown_active = 0;
            item->select_candidate_index = -1;
            item->select_candidate_count = 0;
            interaction_result = app_controls_select_interaction(context,
                    item, PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_BEGIN,
                    &should_commit);
            if (interaction_result > 0) {
                item->dropdown_active = 1;
            }
            return 1;
        }
        if (!item->multiple && HIWORD(wparam) == CBN_SELCHANGE) {
            if (app_controls_select_native_state(item, &selected_index,
                    &selected_count) != 0) {
                return 1;
            }
            if (item->dropdown_active) {
                interaction_result = app_controls_select_interaction(context,
                        item,
                        PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_CANDIDATE,
                        &should_commit);
                if (interaction_result > 0) {
                    item->select_candidate_index = selected_index;
                    item->select_candidate_count = selected_count;
                    return 1;
                }
                item->dropdown_active = 0;
                app_controls_sync_select(context, item);
                return 1;
            }
            (void) app_controls_select_commit_single(context, item,
                    selected_index);
            return 1;
        }
        if (!item->multiple && HIWORD(wparam) == CBN_SELENDOK) {
            if (item->dropdown_active) {
                should_commit = 0;
                interaction_result = app_controls_select_interaction(context,
                        item,
                        PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_END_OK,
                        &should_commit);
                item->dropdown_active = 0;
                if (interaction_result > 0 && should_commit &&
                        item->select_candidate_index >= 0) {
                    (void) app_controls_select_commit_single(context, item,
                            item->select_candidate_index);
                } else if (interaction_result < 0) {
                    app_controls_sync_select(context, item);
                }
            }
            return 1;
        }
        if (!item->multiple && HIWORD(wparam) == CBN_SELENDCANCEL) {
            if (item->dropdown_active) {
                should_commit = 0;
                (void) app_controls_select_interaction(context, item,
                        PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_END_CANCEL,
                        &should_commit);
                item->dropdown_active = 0;
                app_controls_sync_select(context, item);
            }
            return 1;
        }
        if (!item->multiple && HIWORD(wparam) == CBN_CLOSEUP) {
            return 1;
        }
        if (item->multiple && HIWORD(wparam) == LBN_SELCHANGE) {
            (void) app_controls_select_commit_multiple(context, item);
            return 1;
        }
        return 1;
    }
    if (item->kind != APP_CONTROLS_KIND_TEXT || HIWORD(wparam) != EN_CHANGE) {
        return 1;
    }
    value = app_controls_read_value(item);
    if (value == NULL) {
        return 1;
    }
    app_controls_item_point(context, item, &x, &y);
    result = PCore_TextInputSetValue(context->document, item->text_index,
            value);
    if (result != 0) {
        PCoreTextInputInfo info;
        char *core_value;

        memset(&info, 0, sizeof(info));
        core_value = NULL;
        if (PCore_TextInputInfo(context->document, item->text_index,
                &info, NULL, 0) == 0 && info.value_bytes >= 0 &&
                info.value_bytes < APP_CONTROLS_TEXT_CAP) {
            core_value = (char *) malloc((size_t) info.value_bytes + 1);
            if (core_value != NULL && PCore_TextInputInfo(context->document,
                    item->text_index, NULL, core_value,
                    info.value_bytes + 1) == 0) {
                app_controls_set_value(context, item, core_value);
            }
            free(core_value);
        }
    } else {
        if (context->script != NULL) {
            (void) AppScript_DispatchNativeEditInput(context->script,
                    item->target_token, x, y, NULL, NULL);
        }
        app_controls_notify(context);
    }
    free(value);
    return 1;
}
