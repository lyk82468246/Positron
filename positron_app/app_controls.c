/*
 * positron_app/app_controls.c - private WM6 native text-control adapter.
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "app_controls.h"

#define APP_CONTROLS_MAX             PBROWSER_SCRIPT_NATIVE_EDIT_MAX_TARGETS
#define APP_CONTROLS_TEXT_CAP        32768
#define APP_CONTROLS_ID_BASE         1000

typedef struct AppControlsItem AppControlsItem;

struct AppControlsItem {
    HWND hwnd;
    unsigned int text_index;
    unsigned long target_token;
    WNDPROC original_proc;
    int multiline;
    int password;
    int read_only;
    int disabled;
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
    int syncing;
};

static AppControlsContext *g_app_controls;

static int app_controls_item_geometry(AppControlsContext *context,
        AppControlsItem *item, int *out_x, int *out_y, int *out_width,
        int *out_height)
{
    PCoreTextInputInfo info;

    if (context == NULL || item == NULL || context->document == NULL) {
        return 0;
    }
    memset(&info, 0, sizeof(info));
    if (PCore_TextInputInfo(context->document, item->text_index, &info,
            NULL, 0) != 0) {
        return 0;
    }
    item->x = info.x;
    item->y = info.y;
    item->width = info.width > 0 ? info.width : 1;
    item->height = info.height > 0 ? info.height : 1;
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

    if (source == NULL || target == NULL || target_capacity <= 1) {
        return 0;
    }
    target[0] = L'\0';
    chars = MultiByteToWideChar(CP_UTF8, 0, source, -1, target,
            target_capacity - 1);
    if (chars <= 0) {
        chars = MultiByteToWideChar(CP_ACP, 0, source, -1, target,
                target_capacity - 1);
    }
    if (chars <= 0) {
        return 0;
    }
    target[chars] = L'\0';
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
        if (context->script != NULL) {
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

static int app_controls_rebuild_item(AppControlsContext *context,
        AppControlsItem *item, unsigned int index, int scroll_x, int scroll_y)
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
            (HMENU) (APP_CONTROLS_ID_BASE + (int) index),
            context->instance, NULL);
    free(wide_value);
    free(edit_value);
    if (hwnd == NULL) {
        return 1;
    }
    memset(item, 0, sizeof(*item));
    item->hwnd = hwnd;
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
    }
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

    if (context == NULL || document == NULL) {
        return 1;
    }
    AppControls_ClearPage(context);
    context->document = document;
    context->script = script;
    index = 0;
    while (index < APP_CONTROLS_MAX && PCore_TextInputInfo(document,
            index, NULL, NULL, 0) == 0) {
        if (app_controls_rebuild_item(context, &context->items[index],
                index, scroll_x, scroll_y) != 0) {
            AppControls_ClearPage(context);
            return 1;
        }
        index++;
        context->count = index;
    }
    AppControls_Reposition(context, document, scroll_x, scroll_y);
    return 0;
}

void AppControls_Reposition(AppControlsContext *context, HANDLE document,
        int scroll_x, int scroll_y)
{
    RECT client;
    unsigned int i;
    int x;
    int y;
    int width;
    int height;

    if (context == NULL || document == NULL || context->parent == NULL) {
        return;
    }
    context->document = document;
    GetClientRect(context->parent, &client);
    for (i = 0; i < context->count; i++) {
        if (context->items[i].hwnd == NULL ||
                !app_controls_item_geometry(context, &context->items[i],
                &x, &y, &width, &height)) {
            continue;
        }
        x -= scroll_x;
        y -= scroll_y;
        MoveWindow(context->items[i].hwnd, x, y, width, height, TRUE);
        if (x + width <= client.left || x >= client.right ||
                y + height <= client.top || y >= client.bottom) {
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

    if (context == NULL || lparam == 0 || HIWORD(wparam) != EN_CHANGE) {
        return 0;
    }
    item = app_controls_find((HWND) lparam);
    if (item == NULL || context->syncing || context->document == NULL) {
        return item != NULL ? 1 : 0;
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
