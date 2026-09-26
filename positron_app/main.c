/*
 * positron_app/main.c - the first independent Positron browser shell.
 *
 * This is an application consumer, not a second browser engine.  The shell
 * owns the WM6 window, native command controls, input routing and navigation
 * policy.  Page parsing, styling, layout, painting and bounded focus/link
 * queries cross the public positron_core.dll ABI.  Browser history and the
 * navigation candidate/resource transaction cross the public
 * positron_browser.dll ABI.
 *
 * The built-in pages remain available when the network is unavailable.  A
 * network page is fetched on a worker and is not made visible until its
 * candidate has passed the Browser commit gate and the Core document has been
 * parsed, styled and laid out.
 */

#include <windows.h>
#include <aygshell.h>
#include <commdlg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_host.h"
#include "app_controls.h"
#include "app_script.h"
#include "app_forms.h"
#include "app_resources.h"
#include "app_i18n.h"
#include "positron_core.h"
#include "positron_browser.h"
#include "positron_http.h"
#include "resource.h"

#ifndef WS_EX_CONTROLPARENT
#define WS_EX_CONTROLPARENT 0x00010000L
#endif

#define APP_URL_MAX             APP_HOST_URL_MAX
#define APP_WIDE_TEXT_MAX       1024
#define APP_FOCUS_ID_MAX        APP_HOST_FOCUS_ID_MAX
#define APP_FOCUS_MAX           APP_HOST_FOCUS_MAX
#define APP_PAGE_CLASS_NAME     L"PositronBrowserPage"

#define APP_ADDRESS_HEIGHT       28
#define APP_ADDRESS_INSET         2
#define APP_COMMAND_FALLBACK_HEIGHT 26

#define APP_ID_ADDRESS            100

#define APP_PAGE_WELCOME            APP_I18N_PAGE_WELCOME
#define APP_PAGE_CONTROLS           APP_I18N_PAGE_CONTROLS
#define APP_URL_WELCOME             "positron://welcome"
#define APP_URL_CONTROLS            "positron://controls"

#define APP_HISTORY_NEW             1
#define APP_HISTORY_TARGET          2
#define APP_HISTORY_REFRESH         3
#define APP_HISTORY_REPLACE         4

#define APP_WM_ADDRESS_GO       (WM_APP + 1)
#define APP_WM_ADDRESS_CANCEL   (WM_APP + 2)
#define APP_WM_NAV_DONE         (WM_APP + 3)
#define APP_WM_SCRIPT_NAVIGATE  (WM_APP + 4)
#define APP_WM_CONTROLS_REFRESH (WM_APP + 5)
#define APP_WM_FILE_PICKER      (WM_APP + 6)
#define APP_CONTROLS_REFRESH_FORM_RESET 1
#define APP_SCRIPT_TIMER_ID     7

#define APP_NAV_MAX_RETIRED     4
#define APP_NAV_HOST_MAX        APP_HOST_NAV_HOST_MAX
#define APP_NAV_PATH_MAX        APP_HOST_NAV_PATH_MAX
#define APP_NAV_WORK_DOCUMENT   1
#define APP_NAV_WORK_RESOURCES  2
#define APP_NAV_COMMIT_SCRIPTS  1
#define APP_NAV_COMMIT_STYLE    2
#define APP_NAV_COMMIT_IMAGES   3
#define APP_NAV_COMMIT_LAYOUT   4

/* Keep the existing UI/navigation helper names stable while the ownership of
 * all host state moves into one private lifecycle object. */
static AppHostContext g_app;
static AppControlsContext *g_controls;
#define g_window                 (g_app.window)
#define g_address                (g_app.address)
#define g_page_window            (g_app.page_window)
#define g_menu_bar               (g_app.menu_bar)
#define g_address_original_proc  (g_app.address_original_proc)
#define g_shell_activate         (g_app.shell_activate)
#define g_instance               (g_app.instance)
#define g_document               (g_app.document)
#define g_stylesheet             (g_app.stylesheet)
#define g_script                 (g_app.script)
#define g_history                (g_app.history)
#define g_core_initialized       (g_app.core_initialized)
#define g_http_initialized       (g_app.http_initialized)
#define g_navigation_request     (g_app.navigation_request)
#define g_retired_navigation     (g_app.retired_navigation)
#define g_navigation_generation  (g_app.navigation_generation)
#define g_navigation_closing     (g_app.navigation_closing)
#define g_page_kind              (g_app.page_kind)
#define g_page_width             (g_app.page_width)
#define g_page_height            (g_app.page_height)
#define g_document_width         (g_app.document_width)
#define g_document_height        (g_app.document_height)
#define g_scroll_x               (g_app.scroll_x)
#define g_scroll_y               (g_app.scroll_y)
#define g_dpi                    (g_app.dpi)
#define g_focus_index            (g_app.focus_index)
#define g_focus_count            (g_app.focus_count)
#define g_focus_ids              (g_app.focus_ids)
#define g_focus_id               (g_app.focus_id)
#define g_current_url            (g_app.current_url)

static int g_file_picker_pending;
static int g_file_picker_active;
static HANDLE g_file_picker_document;
static AppScriptContext *g_file_picker_script;
static unsigned int g_file_picker_index;
static int g_file_picker_x;
static int g_file_picker_y;
static char g_file_picker_pending_id[PBROWSER_SCRIPT_DIALOG_ID_MAX];

static int app_relayout(void);
static int app_load_page(HWND hwnd, const char *url, int history_mode,
        int history_target);
static int app_load_form_request(HWND hwnd, const AppFormRequest *request);
static void app_update_history_buttons(void);
static void app_controls_changed(void *pw);
static void app_handle_form_submit(void *pw, int document_x,
        int document_y, int validation_valid);
static void app_handle_form_enter(void *pw, unsigned int text_index);
static void app_handle_invalid_form(unsigned int text_index);
static void app_handle_invalid_validation(
        PCoreFormValidationInfo *validation);
static int app_script_validate_form_submit(void *pw,
        AppScriptContext *context, HANDLE document,
        const PBrowserScriptFormSubmitInfo *info, int *out_valid);
static int app_script_submit_form(void *pw, AppScriptContext *context,
        HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info,
        AppFormRequest *out_request);
static int app_script_submit_form_direct(void *pw,
        AppScriptContext *context, HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info,
        AppFormRequest *out_request);
static int app_script_build_form_request(void *pw,
        AppScriptContext *context, HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info,
        AppFormRequest *out_request, int validate);
static int app_script_form_navigation(void *pw, AppScriptContext *context);
static int app_script_programmatic_click_target(void *pw,
        AppScriptContext *context, const char *element_id,
        PBrowserScriptProgrammaticClickTargetInfo *out_info);
static int app_script_programmatic_click_validate(void *pw,
        AppScriptContext *context,
        const PBrowserScriptProgrammaticClickInfo *info,
        const PBrowserScriptProgrammaticClickTargetInfo *target,
        int *out_valid);
static int app_script_programmatic_click_default(void *pw,
        AppScriptContext *context,
        const PBrowserScriptProgrammaticClickDefaultInfo *info);
static int app_script_programmatic_click_generic(void *pw,
        AppScriptContext *context,
        const PBrowserScriptProgrammaticClickInfo *info);
static int app_script_programmatic_anchor_target(void *pw,
        AppScriptContext *context, const char *element_id,
        PBrowserScriptProgrammaticAnchorTargetInfo *out_info);
static int app_file_input_at(HANDLE document, int x, int y,
        unsigned int *out_index, int *out_disabled);
static int app_file_input_open(HWND hwnd, HANDLE document,
        AppScriptContext *script, unsigned int file_index, int x, int y,
        int picker_requested);
static int app_file_picker_queue_by_id(HWND hwnd, HANDLE document,
        AppScriptContext *script, const char *element_id);
static int app_file_picker_process(HWND hwnd);
static void app_file_picker_cancel_pending(void);
static void app_forms_adapter_init(AppFormsAdapter *adapter);
static int app_handle_disclosure(HWND hwnd, int x, int y);
static int app_handle_label(HWND hwnd, int x, int y);

static const char g_app_css[] =
        "body{margin:12px;font-family:sans-serif;font-size:14px;"
        "color:#202020;background:#ffffff}"
        "h1{font-size:22px;color:#173b64;margin:0 0 8px 0}"
        "h2{font-size:17px;color:#28527a;margin:14px 0 6px 0}"
        "p{margin:0 0 8px 0}"
        "a{color:#0000ee;text-decoration:underline}"
        "a:focus{color:#000080;background-color:#cce8ff}"
        "a:hover{color:#800000}";

static void app_copy_text(char *target, int target_capacity,
        const char *source)
{
    int length;

    if (target == NULL || target_capacity <= 0) {
        return;
    }
    target[0] = '\0';
    if (source == NULL || target_capacity == 1) {
        return;
    }
    length = (int) strlen(source);
    if (length >= target_capacity) {
        length = target_capacity - 1;
    }
    memcpy(target, source, (size_t) length);
    target[length] = '\0';
}

static void app_utf8_to_wide(const char *source, WCHAR *target,
        int target_capacity)
{
    int chars;

    if (target == NULL || target_capacity <= 0) {
        return;
    }
    target[0] = L'\0';
    if (source == NULL) {
        return;
    }
    chars = MultiByteToWideChar(CP_UTF8, 0, source, -1,
            target, target_capacity - 1);
    if (chars <= 0) {
        chars = MultiByteToWideChar(CP_ACP, 0, source, -1,
                target, target_capacity - 1);
    }
    if (chars > 0) {
        target[chars] = L'\0';
    }
}

static int app_wide_to_utf8(HWND edit, char *target, int target_capacity)
{
    WCHAR source[APP_WIDE_TEXT_MAX];
    int chars;
    int bytes;

    if (edit == NULL || target == NULL || target_capacity <= 1) {
        return 1;
    }
    chars = GetWindowTextW(edit, source,
            sizeof(source) / sizeof(source[0]));
    if (chars < 0 || chars >= (int) (sizeof(source) / sizeof(source[0]))) {
        return 1;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0, source, chars,
            target, target_capacity - 1, NULL, NULL);
    if (bytes <= 0) {
        bytes = WideCharToMultiByte(CP_ACP, 0, source, chars,
                target, target_capacity - 1, NULL, NULL);
    }
    if (bytes <= 0 || bytes >= target_capacity) {
        target[0] = '\0';
        return 1;
    }
    target[bytes] = '\0';
    return 0;
}

static void app_set_status(AppTextId text_id)
{
    WCHAR wide[APP_WIDE_TEXT_MAX];

    if (g_window == NULL) {
        return;
    }
    if (AppI18n_LoadString(text_id, wide,
            sizeof(wide) / sizeof(wide[0])) <= 0) {
        return;
    }
    SetWindowTextW(g_window, wide);
}

static AppTextId app_ready_status(void)
{
    if (g_page_kind == APP_PAGE_WELCOME) {
        return APP_TEXT_STATUS_READY_WELCOME;
    }
    if (g_page_kind == APP_PAGE_CONTROLS) {
        return APP_TEXT_STATUS_READY_CONTROLS;
    }
    return APP_TEXT_STATUS_READY_REMOTE;
}

static void app_restore_page_status(void)
{
    if (g_document != NULL) {
        app_set_status(app_ready_status());
    }
}

static void app_set_address(const char *url)
{
    WCHAR wide[APP_WIDE_TEXT_MAX];

    if (g_address == NULL) {
        return;
    }
    app_utf8_to_wide(url, wide, sizeof(wide) / sizeof(wide[0]));
    SetWindowTextW(g_address, wide);
}

static int app_device_dpi(void)
{
    HDC dc;
    int dpi;

    dpi = 96;
    dc = GetDC(NULL);
    if (dc != NULL) {
        if (GetDeviceCaps(dc, LOGPIXELSX) > 0) {
            dpi = GetDeviceCaps(dc, LOGPIXELSX);
        }
        ReleaseDC(NULL, dc);
    }
    return dpi;
}

static int app_scale_dpi(int logical_pixels)
{
    int scaled;

    if (logical_pixels <= 0) {
        return 0;
    }
    scaled = MulDiv(logical_pixels, (g_dpi > 0) ? g_dpi : 96, 96);
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static int app_address_height(void)
{
    return app_scale_dpi(APP_ADDRESS_HEIGHT);
}

static int app_command_bar_top(HWND hwnd, const RECT *client)
{
    RECT command_bar;
    POINT point;

    if (client == NULL) {
        return 0;
    }
    if (g_menu_bar != NULL && GetWindowRect(g_menu_bar, &command_bar)) {
        point.x = command_bar.left;
        point.y = command_bar.top;
        if (ScreenToClient(hwnd, &point)) {
            if (point.y >= client->top && point.y <= client->bottom) {
                return point.y;
            }
            return client->bottom;
        }
    }
    return client->bottom - app_scale_dpi(APP_COMMAND_FALLBACK_HEIGHT);
}

static void app_page_rect(HWND hwnd, RECT *rect)
{
    RECT client;

    if (rect == NULL) {
        return;
    }
    memset(rect, 0, sizeof(*rect));
    GetClientRect(hwnd, &client);
    rect->left = 0;
    rect->right = client.right;
    rect->top = app_address_height();
    rect->bottom = app_command_bar_top(hwnd, &client);
    if (rect->right < rect->left) {
        rect->right = rect->left;
    }
    if (rect->bottom < rect->top + 1) {
        rect->bottom = rect->top + 1;
    }
}

static void app_reposition_controls(HWND hwnd)
{
    RECT client;
    int address_height;
    int width;
    int address_width;
    int inset;

    GetClientRect(hwnd, &client);
    address_height = app_address_height();
    inset = app_scale_dpi(APP_ADDRESS_INSET);
    width = client.right - client.left;
    if (width < 1) {
        width = 1;
    }
    address_width = width - (inset * 2);
    if (address_width < 1) {
        address_width = 1;
    }
    if (g_address != NULL) {
        if (address_height - (inset * 2) < 1) {
            address_height = (inset * 2) + 1;
        }
        MoveWindow(g_address, inset, inset, address_width,
                address_height - (inset * 2), TRUE);
    }
}

static void app_reposition_page(HWND hwnd)
{
    RECT page;

    if (g_page_window == NULL) {
        return;
    }
    app_page_rect(hwnd, &page);
    MoveWindow(g_page_window, page.left, page.top,
            page.right - page.left, page.bottom - page.top, TRUE);
}

static void app_clamp_scroll(void)
{
    int max_x;
    int max_y;

    max_x = g_document_width - g_page_width;
    max_y = g_document_height - g_page_height;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }
    if (g_scroll_x < 0) {
        g_scroll_x = 0;
    }
    if (g_scroll_y < 0) {
        g_scroll_y = 0;
    }
    if (g_scroll_x > max_x) {
        g_scroll_x = max_x;
    }
    if (g_scroll_y > max_y) {
        g_scroll_y = max_y;
    }
}

static void app_update_scrollbars(HWND hwnd)
{
    SCROLLINFO info;

    if (hwnd == NULL) {
        return;
    }
    app_clamp_scroll();
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = (g_document_width > 0) ? g_document_width - 1 : 0;
    info.nPage = (UINT) ((g_page_width > 0) ? g_page_width : 1);
    info.nPos = g_scroll_x;
    SetScrollInfo(hwnd, SB_HORZ, &info, TRUE);
    info.nMax = (g_document_height > 0) ? g_document_height - 1 : 0;
    info.nPage = (UINT) ((g_page_height > 0) ? g_page_height : 1);
    info.nPos = g_scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
}

static void app_scroll_by(HWND hwnd, int dx, int dy)
{
    int old_x;
    int old_y;

    old_x = g_scroll_x;
    old_y = g_scroll_y;
    g_scroll_x += dx;
    g_scroll_y += dy;
    app_clamp_scroll();
    if (old_x != g_scroll_x || old_y != g_scroll_y) {
        app_update_scrollbars(hwnd);
        if (g_controls != NULL) {
            AppControls_Reposition(g_controls, g_document, g_scroll_x,
                    g_scroll_y);
        }
        if (g_script != NULL) {
            (void) AppScript_NotifyScroll(g_script,
                    MulDiv(g_scroll_x, 96, g_dpi > 0 ? g_dpi : 96),
                    MulDiv(g_scroll_y, 96, g_dpi > 0 ? g_dpi : 96));
        }
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void app_set_focus_ids(int page_kind)
{
    g_focus_count = 0;
    g_focus_index = -1;
    g_focus_id[0] = '\0';
    if (page_kind == APP_PAGE_WELCOME) {
        g_focus_ids[g_focus_count++] = "controls";
        g_focus_ids[g_focus_count++] = "reload";
        g_focus_ids[g_focus_count++] = "final";
    } else if (page_kind == APP_PAGE_CONTROLS) {
        g_focus_ids[g_focus_count++] = "welcome";
        g_focus_ids[g_focus_count++] = "welcome2";
        g_focus_ids[g_focus_count++] = "home";
    }
}

static void app_script_schedule_refresh(void *pw, AppScriptContext *context,
        int form_reset)
{
    AppHostContext *host;

    host = (AppHostContext *) pw;
    if (host == NULL || context == NULL || host->script != context ||
            host->document == NULL) {
        return;
    }
    if (app_relayout() != 0) {
        app_set_status(APP_TEXT_STATUS_LAYOUT);
        return;
    }
    if (g_controls != NULL) {
        AppControls_Reposition(g_controls, host->document, g_scroll_x,
                g_scroll_y);
    }
    InvalidateRect(host->page_window, NULL, TRUE);
    if (host->window != NULL) {
        (void) PostMessage(host->window, APP_WM_CONTROLS_REFRESH,
                form_reset ? APP_CONTROLS_REFRESH_FORM_RESET : 0,
                (LPARAM) context);
    }
}

static void app_script_mutated(void *pw, AppScriptContext *context)
{
    app_script_schedule_refresh(pw, context, 0);
}

static void app_script_form_reset_applied(void *pw,
        AppScriptContext *context)
{
    app_script_schedule_refresh(pw, context, 1);
}

static int app_script_contenteditable_selection_get(void *pw,
        AppScriptContext *context, const char *element_id, int *out_start,
        int *out_end, int *out_direction)
{
    AppHostContext *host;

    host = (AppHostContext *) pw;
    if (host == NULL || context == NULL || host->script != context ||
            host->document == NULL ||
            AppScript_Document(context) != host->document ||
            g_controls == NULL) {
        return 0;
    }
    return AppControls_GetContentEditableSelection(g_controls, context,
            element_id, out_start, out_end, out_direction);
}

static int app_script_contenteditable_selection_set(void *pw,
        AppScriptContext *context, const char *element_id, int start,
        int end, int direction)
{
    AppHostContext *host;

    host = (AppHostContext *) pw;
    if (host == NULL || context == NULL || host->script != context ||
            host->document == NULL ||
            AppScript_Document(context) != host->document ||
            g_controls == NULL) {
        return 0;
    }
    return AppControls_SetContentEditableSelection(g_controls, context,
            element_id, start, end, direction);
}

static void app_controls_changed(void *pw)
{
    AppHostContext *host;

    host = (AppHostContext *) pw;
    if (host == NULL || host != &g_app || g_controls == NULL ||
            host->document == NULL) {
        return;
    }
    if (app_relayout() != 0) {
        app_set_status(APP_TEXT_STATUS_LAYOUT);
        return;
    }
    AppControls_Reposition(g_controls, host->document, host->scroll_x,
            host->scroll_y);
    InvalidateRect(host->page_window, NULL, TRUE);
}

static int app_script_scroll(void *pw, AppScriptContext *context,
        const PBrowserScriptScrollInfo *info, int *out_x, int *out_y)
{
    AppHostContext *host;
    int device_x;
    int device_y;
    int result;

    host = (AppHostContext *) pw;
    if (host == NULL || context == NULL || info == NULL || out_x == NULL ||
            out_y == NULL || info->size < sizeof(*info) ||
            info->scroll_x < 0 || info->scroll_y < 0) {
        return -1;
    }
    if (host->script != context || host->document == NULL) {
        *out_x = info->scroll_x;
        *out_y = info->scroll_y;
        return 0;
    }
    if (info->element_id != NULL && info->element_id[0] != '\0') {
        result = PCore_NodeOverflowScrollToById(host->document,
                info->element_id, info->scroll_x, info->scroll_y,
                out_x, out_y);
        if (result == 2) {
            *out_x = 0;
            *out_y = 0;
            return 0;
        }
        if (result != 0) {
            return -1;
        }
        InvalidateRect(host->page_window, NULL, FALSE);
        return 0;
    }
    device_x = MulDiv(info->scroll_x, host->dpi > 0 ? host->dpi : 96,
            96);
    device_y = MulDiv(info->scroll_y, host->dpi > 0 ? host->dpi : 96,
            96);
    host->scroll_x = device_x;
    host->scroll_y = device_y;
    app_clamp_scroll();
    app_update_scrollbars(host->page_window);
    InvalidateRect(host->page_window, NULL, FALSE);
    *out_x = MulDiv(host->scroll_x, 96, host->dpi > 0 ? host->dpi : 96);
    *out_y = MulDiv(host->scroll_y, 96, host->dpi > 0 ? host->dpi : 96);
    return 0;
}

static int app_forms_read_file(void *pw, const char *path,
        char **out_data, int *out_len)
{
    int wide_chars;
    WCHAR *wide_path;
    HANDLE file;
    DWORD high;
    DWORD low;
    DWORD read_count;
    char *data;
    DWORD offset;

    (void) pw;
    if (path == NULL || path[0] == '\0' || out_data == NULL ||
            out_len == NULL) {
        return 1;
    }
    *out_data = NULL;
    *out_len = 0;
    wide_chars = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wide_chars <= 0) {
        wide_chars = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    }
    if (wide_chars <= 0) {
        return 1;
    }
    wide_path = (WCHAR *) malloc((size_t) wide_chars * sizeof(WCHAR));
    if (wide_path == NULL) {
        return 1;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path,
            wide_chars) <= 0 && MultiByteToWideChar(CP_ACP, 0, path, -1,
            wide_path, wide_chars) <= 0) {
        free(wide_path);
        return 1;
    }
    file = CreateFileW(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide_path);
    if (file == INVALID_HANDLE_VALUE) {
        return 1;
    }
    high = 0;
    low = GetFileSize(file, &high);
    if ((low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) ||
            high != 0 || low > APP_FORMS_BODY_MAX_BYTES) {
        CloseHandle(file);
        return 1;
    }
    data = (char *) malloc(low > 0 ? (size_t) low : 1U);
    if (data == NULL) {
        CloseHandle(file);
        return 1;
    }
    offset = 0;
    while (offset < low) {
        read_count = 0;
        if (!ReadFile(file, data + offset, low - offset, &read_count, NULL) ||
                read_count == 0) {
            free(data);
            CloseHandle(file);
            return 1;
        }
        offset += read_count;
    }
    CloseHandle(file);
    *out_data = data;
    *out_len = (int) low;
    return 0;
}

static void app_forms_free_file(void *pw, char *data)
{
    (void) pw;
    free(data);
}

static int app_wide_text_to_utf8(const WCHAR *source, char *target,
        int target_capacity)
{
    int source_length;
    int bytes;

    if (source == NULL || target == NULL || target_capacity <= 1) {
        return 1;
    }
    target[0] = '\0';
    source_length = lstrlenW(source);
    bytes = WideCharToMultiByte(CP_UTF8, 0, source, source_length,
            target, target_capacity - 1, NULL, NULL);
    if (bytes <= 0) {
        bytes = WideCharToMultiByte(CP_ACP, 0, source, source_length,
                target, target_capacity - 1, NULL, NULL);
    }
    if (bytes <= 0 || bytes >= target_capacity) {
        target[0] = '\0';
        return 1;
    }
    target[bytes] = '\0';
    return 0;
}

static int app_script_programmatic_click_target(void *pw,
        AppScriptContext *context, const char *element_id,
        PBrowserScriptProgrammaticClickTargetInfo *out_info)
{
    HANDLE document;
    int core_kind;
    int core_disabled;

    (void) pw;
    document = AppScript_Document(context);
    if (document == NULL || element_id == NULL || element_id[0] == '\0' ||
            out_info == NULL || out_info->size < sizeof(*out_info)) {
        return -1;
    }
    out_info->found = 0;
    out_info->x = 0;
    out_info->y = 0;
    out_info->width = 0;
    out_info->height = 0;
    out_info->kind = 0;
    out_info->disabled = 0;
    if (PCore_DisclosureInfoById(document, element_id, &out_info->x,
            &out_info->y, &out_info->width, &out_info->height, NULL) == 0) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_DISCLOSURE;
        out_info->found = 1;
        return 0;
    }
    core_kind = 0;
    core_disabled = 0;
    if (PCore_FormControlInfoById(document, element_id, &out_info->x,
            &out_info->y, &out_info->width, &out_info->height, &core_kind,
            NULL, &core_disabled) != 0) {
        return 0;
    }
    if (core_kind == 1) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_CHECKBOX;
    } else if (core_kind == 2) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_RADIO;
    } else if (core_kind == 7) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_SUBMIT;
    } else if (core_kind == 8) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_RESET;
    } else if (core_kind == 10) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_FILE;
    } else if (core_kind == 3) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_TEXT;
    } else if (core_kind == 4) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_PASSWORD;
    } else if (core_kind == 5) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_TEXTAREA;
    } else if (core_kind == 6) {
        out_info->kind = PBROWSER_SCRIPT_CLICK_TARGET_SELECT;
    } else {
        return 0;
    }
    out_info->disabled = core_disabled ? 1 : 0;
    out_info->found = 1;
    return 0;
}

static int app_script_programmatic_click_validate(void *pw,
        AppScriptContext *context,
        const PBrowserScriptProgrammaticClickInfo *info,
        const PBrowserScriptProgrammaticClickTargetInfo *target,
        int *out_valid)
{
    HANDLE document;
    PCoreFormValidationInfo validation;

    (void) pw;
    if (info == NULL || target == NULL || out_valid == NULL ||
            info->size < sizeof(*info) || target->size < sizeof(*target)) {
        return -1;
    }
    document = AppScript_Document(context);
    if (document == NULL) {
        return -1;
    }
    *out_valid = 1;
    if (target->kind != PBROWSER_SCRIPT_CLICK_TARGET_SUBMIT) {
        return 0;
    }
    memset(&validation, 0, sizeof(validation));
    if (PCore_FormValidationAt(document,
            target->x + target->width / 2,
            target->y + target->height / 2, &validation) != 1 ||
            !validation.valid) {
        *out_valid = 0;
    }
    return 0;
}

static int app_script_programmatic_click_generic(void *pw,
        AppScriptContext *context,
        const PBrowserScriptProgrammaticClickInfo *info)
{
    HANDLE document;
    int default_allowed;
    int result;

    (void) pw;
    document = AppScript_Document(context);
    if (document == NULL || info == NULL || info->size < sizeof(*info) ||
            info->element_id == NULL || info->element_id[0] == '\0') {
        return -1;
    }
    default_allowed = 1;
    result = PCore_EventDispatchToId(document, info->element_id, "click",
            1, 1, &default_allowed);
    return result < 0 ? -1 : 0;
}

static int app_script_programmatic_anchor_target(void *pw,
        AppScriptContext *context, const char *element_id,
        PBrowserScriptProgrammaticAnchorTargetInfo *out_info)
{
    HANDLE document;

    (void) pw;
    document = AppScript_Document(context);
    if (document == NULL || element_id == NULL || element_id[0] == '\0' ||
            out_info == NULL || out_info->size < sizeof(*out_info) ||
            out_info->href == NULL || out_info->href_capacity <= 0 ||
            out_info->target == NULL || out_info->target_capacity <= 0 ||
            out_info->rel == NULL || out_info->rel_capacity <= 0) {
        return -1;
    }
    out_info->found = 0;
    out_info->x = 0;
    out_info->y = 0;
    out_info->width = 0;
    out_info->height = 0;
    out_info->href[0] = '\0';
    out_info->target[0] = '\0';
    out_info->rel[0] = '\0';
    if (PCore_LinkInfoByIdEx(document, element_id, &out_info->x,
            &out_info->y, &out_info->width, &out_info->height,
            out_info->href, out_info->href_capacity, out_info->target,
            out_info->target_capacity, out_info->rel,
            out_info->rel_capacity) != 0) {
        return 0;
    }
    out_info->found = 1;
    return 0;
}

static int app_file_input_at(HANDLE document, int x, int y,
        unsigned int *out_index, int *out_disabled)
{
    if (document == NULL || out_index == NULL || out_disabled == NULL) {
        return 0;
    }
    *out_index = 0;
    *out_disabled = 0;
    return PCore_FileInputAt(document, x, y, out_index, out_disabled) ?
            1 : 0;
}

static int app_file_picker_system(HWND owner, WCHAR *file_path,
        int file_path_capacity, WCHAR *file_title, int file_title_capacity)
{
    OPENFILENAMEEX picker;
    WCHAR title[APP_WIDE_TEXT_MAX];

    if (file_path == NULL || file_path_capacity <= 0 ||
            file_title == NULL || file_title_capacity <= 0) {
        return -1;
    }
    memset(&picker, 0, sizeof(picker));
    memset(title, 0, sizeof(title));
    if (AppI18n_LoadString(APP_TEXT_FILE_PICKER_TITLE, title,
            sizeof(title) / sizeof(title[0])) <= 0) {
        app_utf8_to_wide("Choose a file", title,
                sizeof(title) / sizeof(title[0]));
    }
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrFilter = L"All files (*.*)\0*.*\0\0";
    picker.lpstrFile = file_path;
    picker.nMaxFile = (DWORD) file_path_capacity;
    picker.lpstrFileTitle = file_title;
    picker.nMaxFileTitle = (DWORD) file_title_capacity;
    picker.lpstrTitle = title;
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    picker.ExFlags = OFN_EXFLAG_NOFILECREATE;
    return GetOpenFileNameEx(&picker) ? 1 : 0;
}

static void app_file_picker_clear(void)
{
    g_file_picker_pending = 0;
    g_file_picker_document = NULL;
    g_file_picker_script = NULL;
    g_file_picker_index = 0;
    g_file_picker_x = 0;
    g_file_picker_y = 0;
    g_file_picker_pending_id[0] = '\0';
}

static void app_file_picker_cancel_pending(void)
{
    int accepted;

    if (g_file_picker_pending && g_file_picker_script != NULL) {
        accepted = 0;
        (void) AppScript_DispatchNativeFilePicker(g_file_picker_script,
                (unsigned long) g_file_picker_index + 1UL,
                g_file_picker_x, g_file_picker_y,
                PBROWSER_SCRIPT_NATIVE_FILE_PICKER_CANCEL, &accepted);
    }
    app_file_picker_clear();
}

static int app_file_input_open(HWND hwnd, HANDLE document,
        AppScriptContext *script, unsigned int file_index, int x, int y,
        int picker_requested)
{
    PCoreFileInputInfo core_info;
    WCHAR file_path[MAX_PATH];
    WCHAR file_title[MAX_PATH];
    char *path;
    char *value;
    int disabled;
    int selection_started;
    int picker_opened;
    int accepted;
    int picker_result;
    int result;

    if (document == NULL || hwnd == NULL ||
            PCore_FileInputInfo(document, file_index, &core_info, NULL, 0,
            NULL, 0) != 0) {
        return 1;
    }
    disabled = core_info.disabled ? 1 : 0;
    if (disabled) {
        return 1;
    }
    selection_started = 0;
    picker_opened = picker_requested ? 1 : 0;
    if (script != NULL && script == g_script && document == g_document) {
        if (AppScript_DispatchNativeFileSelection(script,
                (unsigned long) file_index + 1UL, x, y,
                PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_BEGIN) != 0) {
            app_set_status(APP_TEXT_STATUS_FILE_FAILED);
            return 1;
        }
        selection_started = 1;
        if (!picker_requested) {
            accepted = 0;
            if (AppScript_DispatchNativeFilePicker(script,
                    (unsigned long) file_index + 1UL, x, y,
                    PBROWSER_SCRIPT_NATIVE_FILE_PICKER_REQUEST,
                    &accepted) != 0 || !accepted) {
                (void) AppScript_DispatchNativeFileSelection(script,
                        (unsigned long) file_index + 1UL, x, y,
                        PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_CANCEL);
                return 1;
            }
            picker_opened = 1;
        }
        if (picker_opened) {
            accepted = 0;
            if (AppScript_DispatchNativeFilePicker(script,
                    (unsigned long) file_index + 1UL, x, y,
                    PBROWSER_SCRIPT_NATIVE_FILE_PICKER_OPEN,
                    &accepted) != 0 || !accepted) {
                (void) AppScript_DispatchNativeFilePicker(script,
                        (unsigned long) file_index + 1UL, x, y,
                        PBROWSER_SCRIPT_NATIVE_FILE_PICKER_CANCEL,
                        &accepted);
                (void) AppScript_DispatchNativeFileSelection(script,
                        (unsigned long) file_index + 1UL, x, y,
                        PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_CANCEL);
                return 1;
            }
        }
    }
    memset(file_path, 0, sizeof(file_path));
    memset(file_title, 0, sizeof(file_title));
    g_file_picker_active = 1;
    picker_result = app_file_picker_system(hwnd, file_path, MAX_PATH,
            file_title, MAX_PATH);
    g_file_picker_active = 0;
    if (picker_result < 0) {
        app_set_status(APP_TEXT_STATUS_FILE_FAILED);
        result = 1;
    } else if (picker_result == 0) {
        result = 0;
    } else {
        if (file_title[0] == L'\0') {
            memcpy(file_title, file_path, sizeof(file_title));
            file_title[MAX_PATH - 1] = L'\0';
        }
        path = (char *) malloc(APP_FORMS_URL_MAX);
        value = (char *) malloc(APP_FORMS_URL_MAX);
        if (path == NULL || value == NULL ||
                app_wide_text_to_utf8(file_path, path, APP_FORMS_URL_MAX) !=
                0 || app_wide_text_to_utf8(file_title, value,
                APP_FORMS_URL_MAX) != 0 ||
                PCore_FileInputSetPath(document, file_index, value, path) !=
                0) {
            app_set_status(APP_TEXT_STATUS_FILE_FAILED);
            result = 1;
        } else {
            result = 0;
        }
        free(path);
        free(value);
    }
    if (result != 0 || picker_result == 0) {
        if (selection_started) {
            (void) AppScript_DispatchNativeFileSelection(script,
                    (unsigned long) file_index + 1UL, x, y,
                    PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_CANCEL);
        }
    } else if (selection_started) {
        if (AppScript_DispatchNativeFileSelection(script,
                (unsigned long) file_index + 1UL, x, y,
                PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_COMMIT) != 0) {
            app_set_status(APP_TEXT_STATUS_FILE_FAILED);
        }
    } else {
        (void) PCore_EventDispatchAt(document, x, y, "input", 1, 0, NULL);
        (void) PCore_EventDispatchAt(document, x, y, "change", 1, 0, NULL);
    }
    if (picker_opened && script != NULL && script == g_script) {
        accepted = 0;
        (void) AppScript_DispatchNativeFilePicker(script,
                (unsigned long) file_index + 1UL, x, y,
                PBROWSER_SCRIPT_NATIVE_FILE_PICKER_CLOSE, &accepted);
    }
    if (result == 0 && g_script != NULL && g_document == document) {
        app_script_schedule_refresh(&g_app, g_script, 0);
    }
    return 1;
}

static int app_file_picker_queue_by_id(HWND hwnd, HANDLE document,
        AppScriptContext *script, const char *element_id)
{
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;
    unsigned int file_index;
    int accepted;

    if (hwnd == NULL || document == NULL || script == NULL ||
            script != g_script || document != g_document || element_id == NULL ||
            element_id[0] == '\0' || g_file_picker_pending ||
            g_file_picker_active) {
        return 0;
    }
    if (PCore_FormControlInfoById(document, element_id, &x, &y, &width,
            &height, &kind, NULL, &disabled) != 0 || kind != 10 || disabled ||
            width <= 0 || height <= 0 || !app_file_input_at(document,
            x + width / 2, y + height / 2, &file_index, &disabled) ||
            disabled) {
        return 0;
    }
    accepted = 0;
    if (AppScript_DispatchNativeFilePicker(script,
            (unsigned long) file_index + 1UL, x + width / 2,
            y + height / 2, PBROWSER_SCRIPT_NATIVE_FILE_PICKER_REQUEST,
            &accepted) != 0 || !accepted) {
        return 0;
    }
    g_file_picker_pending = 1;
    g_file_picker_document = document;
    g_file_picker_script = script;
    g_file_picker_index = file_index;
    g_file_picker_x = x + width / 2;
    g_file_picker_y = y + height / 2;
    app_copy_text(g_file_picker_pending_id,
            sizeof(g_file_picker_pending_id), element_id);
    if (!PostMessage(hwnd, APP_WM_FILE_PICKER, 0, 0)) {
        app_file_picker_cancel_pending();
        return -1;
    }
    return 0;
}

static int app_file_picker_process(HWND hwnd)
{
    HANDLE document;
    AppScriptContext *script;
    unsigned int file_index;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;
    if (!g_file_picker_pending) {
        return 0;
    }
    document = g_file_picker_document;
    script = g_file_picker_script;
    file_index = g_file_picker_index;
    x = g_file_picker_x;
    y = g_file_picker_y;
    if (document != g_document || script != g_script || hwnd != g_window) {
        app_file_picker_cancel_pending();
        return 0;
    }
    if (g_file_picker_pending_id[0] != '\0') {
        if (PCore_FormControlInfoById(document, g_file_picker_pending_id,
                &x, &y, &width, &height, &kind, NULL, &disabled) != 0 ||
                kind != 10 || disabled || width <= 0 || height <= 0 ||
                !app_file_input_at(document, x + width / 2, y + height / 2,
                &file_index, &disabled) || disabled) {
            app_file_picker_cancel_pending();
            return 0;
        }
        x += width / 2;
        y += height / 2;
    }
    app_file_picker_clear();
    return app_file_input_open(hwnd, document, script, file_index, x, y, 1);
}

static int app_script_programmatic_click_default(void *pw,
        AppScriptContext *context,
        const PBrowserScriptProgrammaticClickDefaultInfo *info)
{
    AppHostContext *host;
    HANDLE document;
    AppFormsAdapter adapter;
    AppFormRequest request;
    int center_x;
    int center_y;
    int native_kind;
    int dirty_x;
    int dirty_y;
    int dirty_width;
    int dirty_height;
    int result;

    host = (AppHostContext *) pw;
    document = AppScript_Document(context);
    if (host == NULL || context == NULL || info == NULL ||
            info->size < sizeof(*info) || document == NULL) {
        return -1;
    }
    /* A candidate session may evaluate script before it becomes the visible
     * page. It may mutate its own Core document, but must not open WM UI or
     * schedule navigation from the candidate worker path. */
    if (host != &g_app || host->script != context ||
            host->document != document) {
        if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_TOGGLE) {
            dirty_x = 0;
            dirty_y = 0;
            dirty_width = 0;
            dirty_height = 0;
            (void) PCore_FormActivateAt(document,
                    info->x + info->width / 2, info->y + info->height / 2,
                    &dirty_x, &dirty_y, &dirty_width, &dirty_height);
        } else if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_DISCLOSURE) {
            (void) PCore_DisclosureToggleById(document, info->element_id,
                    NULL);
        }
        return 0;
    }
    center_x = info->x + info->width / 2;
    center_y = info->y + info->height / 2;
    if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_TOGGLE) {
        dirty_x = 0;
        dirty_y = 0;
        dirty_width = 0;
        dirty_height = 0;
        result = PCore_FormActivateAt(document, center_x, center_y,
                &dirty_x, &dirty_y, &dirty_width, &dirty_height);
        if (result < 0) {
            return -1;
        }
        app_script_schedule_refresh(host, context, 0);
        return 0;
    }
    if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_DISCLOSURE) {
        if (PCore_DisclosureToggleById(document, info->element_id,
                NULL) < 0) {
            return -1;
        }
        app_script_schedule_refresh(host, context, 0);
        return 0;
    }
    if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_FILE) {
        return app_file_picker_queue_by_id(host->window, document, context,
                info->element_id);
    }
    if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_FOCUS) {
        native_kind = 0;
        if (info->kind == PBROWSER_SCRIPT_CLICK_TARGET_TEXT) {
            native_kind = 3;
        } else if (info->kind == PBROWSER_SCRIPT_CLICK_TARGET_PASSWORD) {
            native_kind = 4;
        } else if (info->kind == PBROWSER_SCRIPT_CLICK_TARGET_TEXTAREA) {
            native_kind = 5;
        } else if (info->kind == PBROWSER_SCRIPT_CLICK_TARGET_SELECT) {
            native_kind = 6;
        }
        if (native_kind == 0 || g_controls == NULL ||
                !AppControls_FocusFormControlAt(g_controls, native_kind,
                center_x, center_y)) {
            return 0;
        }
        return 0;
    }
    if (info->action == PBROWSER_SCRIPT_CLICK_DEFAULT_RESET) {
        result = PCore_FormResetAt(document, center_x, center_y);
        if (result == 1) {
            app_script_schedule_refresh(host, context, 1);
        }
        return result < 0 ? -1 : 0;
    }
    if (info->action != PBROWSER_SCRIPT_CLICK_DEFAULT_SUBMIT) {
        return -1;
    }
    app_forms_adapter_init(&adapter);
    AppForms_InitRequest(&request);
    result = AppForms_BuildAt(document, AppScript_DocumentUrl(context),
            center_x, center_y, &adapter, &request);
    if (result != APP_FORMS_RESULT_OK || !request.valid) {
        AppForms_ClearRequest(&request);
        if (result == APP_FORMS_RESULT_INVALID) {
            app_set_status(APP_TEXT_STATUS_FORM_INVALID);
        } else if (result == APP_FORMS_RESULT_UNSUPPORTED) {
            app_set_status(APP_TEXT_STATUS_FORM_UNSUPPORTED);
        } else {
            app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        }
        return 0;
    }
    if (AppScript_QueueFormNavigation(context, &request) != 0) {
        AppForms_ClearRequest(&request);
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        return 0;
    }
    if (app_script_form_navigation(host, context) != 0) {
        AppScript_ClearPendingNavigation(context);
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
    }
    return 0;
}

static int app_script_navigate(void *pw, AppScriptContext *context,
        const PBrowserScriptNavigationInfo *info, int *out_value)
{
    AppHostContext *host;
    int history_result;

    host = (AppHostContext *) pw;
    if (host == NULL || context == NULL || info == NULL ||
            out_value == NULL || info->size < sizeof(*info)) {
        return -1;
    }
    *out_value = 0;
    if (info->kind == PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE ||
            info->kind == PBROWSER_SCRIPT_NAVIGATION_REPLACE_STATE) {
        if (host->script != context || host->history == NULL ||
                info->url == NULL || info->state_json == NULL ||
                info->url[0] == '\0' || info->state_json[0] == '\0' ||
                PBrowser_HistorySameOriginUrl(host->current_url,
                info->url) != 1) {
            return 0;
        }
        if (info->kind == PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE) {
            history_result = PBrowser_HistoryPushState(host->history,
                    info->url, info->state_json);
        } else {
            history_result = PBrowser_HistoryReplaceState(host->history,
                    info->url, info->state_json);
        }
        if (history_result != PBROWSER_OK) {
            return 0;
        }
        app_copy_text(host->current_url, sizeof(host->current_url),
                info->url);
        app_set_address(host->current_url);
        app_update_history_buttons();
        if (info->kind == PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE) {
            *out_value = PBrowser_HistoryCount(host->history);
        }
        return 1;
    }
    if (info->kind != PBROWSER_SCRIPT_NAVIGATION_BACK &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_FORWARD &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_GO &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_ASSIGN &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_RELOAD &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_REPLACE &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_FRAGMENT &&
            info->kind != PBROWSER_SCRIPT_NAVIGATION_FRAGMENT_REPLACE) {
        return 0;
    }
    if (info->target_kind == PBROWSER_SCRIPT_NAVIGATION_TARGET_BLANK ||
            info->target_kind == PBROWSER_SCRIPT_NAVIGATION_TARGET_NAMED) {
        return 0;
    }
    if (AppScript_QueueNavigation(context, info) != 0) {
        return 0;
    }
    if (host->script == context && host->window != NULL) {
        PostMessage(host->window, APP_WM_SCRIPT_NAVIGATE, 0,
                (LPARAM) context);
    }
    return 1;
}

static int app_focus_set(HWND hwnd, int index)
{
    PCoreFocusTargetInfo info;

    if (g_document == NULL || index < 0 || index >= g_focus_count) {
        return 0;
    }
    if (PCore_FocusTargetInfoById(g_document, g_focus_ids[index],
            &info) != 0) {
        return 0;
    }
    AppControls_ClearButtonFocus(g_controls);
    if (PCore_InteractionFocusById(g_document, g_focus_ids[index]) < 0) {
        return 0;
    }
    g_focus_index = index;
    app_copy_text(g_focus_id, sizeof(g_focus_id), g_focus_ids[index]);
    SetFocus(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    return 1;
}

static void app_focus_move(HWND hwnd, int direction)
{
    int i;
    int next;

    if (g_focus_count <= 0) {
        return;
    }
    if (g_focus_index < 0) {
        next = (direction > 0) ? 0 : g_focus_count - 1;
    } else {
        next = g_focus_index;
    }
    for (i = 0; i < g_focus_count; i++) {
        if (g_focus_index >= 0 || i > 0) {
            next += direction;
        }
        if (next < 0) {
            next = g_focus_count - 1;
        }
        if (next >= g_focus_count) {
            next = 0;
        }
        if (app_focus_set(hwnd, next)) {
            return;
        }
    }
}

static int app_focus_at(int x, int y)
{
    char href[APP_URL_MAX];
    int i;
    int left;
    int top;
    int width;
    int height;

    if (g_document == NULL) {
        return -1;
    }
    for (i = 0; i < g_focus_count; i++) {
        if (PCore_LinkInfoById(g_document, g_focus_ids[i], &left, &top,
                &width, &height, href, sizeof(href)) == 0 &&
                x >= left && x < left + width &&
                y >= top && y < top + height) {
            return i;
        }
    }
    return -1;
}

static int app_activate_focus(HWND hwnd)
{
    char href[APP_URL_MAX];
    int left;
    int top;
    int width;
    int height;
    HWND target;

    if (g_document == NULL || g_focus_index < 0 ||
            g_focus_index >= g_focus_count) {
        return 0;
    }
    if (PCore_LinkInfoById(g_document, g_focus_ids[g_focus_index],
            &left, &top, &width, &height, href, sizeof(href)) != 0) {
        return 0;
    }
    target = (g_window != NULL) ? g_window : GetParent(hwnd);
    return (int) SendMessage(target, APP_WM_ADDRESS_GO, 1,
            (LPARAM) href);
}

static int app_ascii_prefix_equal(const char *value, const char *prefix)
{
    unsigned char value_char;
    unsigned char prefix_char;

    if (value == NULL || prefix == NULL) {
        return 0;
    }
    while (*prefix != '\0') {
        value_char = (unsigned char) *value++;
        if (value_char == '\0') {
            return 0;
        }
        prefix_char = (unsigned char) *prefix++;
        if (value_char >= 'A' && value_char <= 'Z') {
            value_char = (unsigned char) (value_char + ('a' - 'A'));
        }
        if (prefix_char >= 'A' && prefix_char <= 'Z') {
            prefix_char = (unsigned char) (prefix_char + ('a' - 'A'));
        }
        if (value_char != prefix_char) {
            return 0;
        }
    }
    return 1;
}

static int app_url_matches_local_page(const char *url,
        const char *page_url)
{
    int page_length;
    int url_length;

    if (url == NULL || page_url == NULL) {
        return 0;
    }
    page_length = (int) strlen(page_url);
    url_length = (int) strlen(url);
    return url_length >= page_length &&
            app_ascii_prefix_equal(url, page_url) &&
            (url[page_length] == '\0' || url[page_length] == '?' ||
            url[page_length] == '#');
}

static int app_page_kind(const char *url)
{
    if (app_url_matches_local_page(url, APP_URL_WELCOME)) {
        return APP_PAGE_WELCOME;
    }
    if (app_url_matches_local_page(url, APP_URL_CONTROLS)) {
        return APP_PAGE_CONTROLS;
    }
    return 0;
}

static int app_format_url(const char *host, const char *path, int port,
        char *output, int output_capacity)
{
    const char *scheme;
    int default_port;
    int length;

    if (host == NULL || host[0] == '\0' || path == NULL ||
            output == NULL || output_capacity <= 1) {
        return 1;
    }
    scheme = (port == 80) ? "http" : "https";
    default_port = (port == 80 || port == 443);
    if (default_port) {
        length = _snprintf(output, output_capacity - 1, "%s://%s%s",
                scheme, host, path);
    } else {
        length = _snprintf(output, output_capacity - 1,
                "%s://%s:%d%s", scheme, host, port, path);
    }
    output[output_capacity - 1] = '\0';
    return (length < 0 || length >= output_capacity - 1) ? 1 : 0;
}

static int app_canonicalize_url(const char *base_url, const char *reference,
        char *output, int output_capacity)
{
    char base_host[APP_NAV_HOST_MAX];
    char base_path[APP_NAV_PATH_MAX];
    char host[APP_NAV_HOST_MAX];
    char path[APP_NAV_PATH_MAX];
    int base_port;
    int port;
    int absolute_network_reference;

    if (reference == NULL || output == NULL || output_capacity <= 1) {
        return 1;
    }
    base_host[0] = '\0';
    base_path[0] = '\0';
    base_port = 443;
    absolute_network_reference =
            app_ascii_prefix_equal(reference, "http://") ||
            app_ascii_prefix_equal(reference, "https://") ||
            (reference[0] == '/' && reference[1] == '/');
    if (!absolute_network_reference && base_url != NULL &&
            base_url[0] != '\0' &&
            PHttp_ResolveReference(NULL, 443, NULL, base_url,
            base_host, sizeof(base_host), base_path, sizeof(base_path),
            &base_port) != 0) {
        return 1;
    }
    port = 0;
    if (PHttp_ResolveReference(
            (base_host[0] != '\0') ? base_host : NULL,
            (base_host[0] != '\0') ? base_port : 443,
            (base_host[0] != '\0') ? base_path : NULL,
            reference, host, sizeof(host), path, sizeof(path), &port) != 0) {
        return 1;
    }
    return app_format_url(host, path, port, output, output_capacity);
}

static int app_resolve_app_url(const char *base_url, const char *reference,
        char *output, int output_capacity)
{
    int length;

    if (reference == NULL || output == NULL || output_capacity <= 1) {
        return 1;
    }
    if (app_page_kind(reference) != 0) {
        length = (int) strlen(reference);
        if (length >= output_capacity) {
            return 1;
        }
        memcpy(output, reference, (size_t) length + 1);
        return 0;
    }
    return app_canonicalize_url(base_url, reference, output,
            output_capacity);
}

static int app_forms_resolve_url(void *pw, const char *base_url,
        const char *reference, char *output, int output_capacity)
{
    (void) pw;
    return app_resolve_app_url(base_url, reference, output, output_capacity);
}

static void app_forms_adapter_init(AppFormsAdapter *adapter)
{
    if (adapter == NULL) {
        return;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->size = sizeof(*adapter);
    adapter->resolve_url = app_forms_resolve_url;
    adapter->read_file = app_forms_read_file;
    adapter->free_file = app_forms_free_file;
}

static int app_style_and_layout(HANDLE document, HANDLE stylesheet)
{
    if (document == NULL || stylesheet == NULL || g_page_width <= 0 ||
            g_page_height <= 0) {
        return 1;
    }
    PCore_SetDeviceViewport(g_page_width, g_page_height, g_dpi);
    if (PCore_StyleDocument(document, stylesheet) != 0) {
        return 1;
    }
    if (PCore_LayoutDocument(document, g_page_width, g_page_height) != 0) {
        return 1;
    }
    return 0;
}

static int app_build_page(const char *url, HANDLE *out_document,
        HANDLE *out_stylesheet, int *out_page_kind)
{
    HANDLE document;
    HANDLE stylesheet;
    char *html;
    unsigned int html_length;
    int page_kind;

    if (out_document == NULL || out_stylesheet == NULL ||
            out_page_kind == NULL) {
        return 1;
    }
    *out_document = NULL;
    *out_stylesheet = NULL;
    *out_page_kind = 0;
    page_kind = app_page_kind(url);
    if (page_kind == 0) {
        return 1;
    }
    html = NULL;
    html_length = 0;
    if (AppI18n_LoadPage(page_kind, &html, &html_length) != 0) {
        return 1;
    }
    document = PCore_ParseHTML(html, html_length);
    AppI18n_FreePage(html);
    if (document == NULL) {
        return 1;
    }
    stylesheet = PCore_ParseCSS(g_app_css, 0,
            "https://positron.invalid/app.css");
    if (stylesheet == NULL || app_style_and_layout(document, stylesheet) != 0) {
        PCore_FreeStylesheet(stylesheet);
        PCore_FreeDocument(document);
        return 1;
    }
    *out_document = document;
    *out_stylesheet = stylesheet;
    *out_page_kind = page_kind;
    return 0;
}

static int app_relayout(void)
{
    if (g_document == NULL || g_stylesheet == NULL) {
        return 0;
    }
    if (app_style_and_layout(g_document, g_stylesheet) != 0) {
        return 1;
    }
    g_document_width = PCore_DocumentWidth(g_document);
    g_document_height = PCore_DocumentHeight(g_document);
    if (g_document_width < g_page_width) {
        g_document_width = g_page_width;
    }
    if (g_document_height < g_page_height) {
        g_document_height = g_page_height;
    }
    app_clamp_scroll();
    app_update_scrollbars(g_page_window);
    if (g_controls != NULL) {
        AppControls_Reposition(g_controls, g_document, g_scroll_x,
                g_scroll_y);
    }
    return 0;
}

static int app_handle_disclosure(HWND hwnd, int x, int y)
{
    int default_allowed;
    int result;

    if (g_document == NULL || PCore_DisclosureInfoAt(g_document, x, y,
            NULL, NULL, NULL, NULL, NULL) != 1) {
        return 0;
    }
    default_allowed = 1;
    if (g_script != NULL) {
        if (AppScript_DispatchClickEvent(g_script, x, y,
                &default_allowed) != 0) {
            return 1;
        }
    } else {
        result = PCore_EventDispatchAt(g_document, x, y, "click", 1, 1,
                &default_allowed);
        if (result < 0) {
            return 1;
        }
    }
    if (!default_allowed) {
        return 1;
    }
    result = PCore_DisclosureToggleAt(g_document, x, y, NULL);
    if (result > 0) {
        if (app_relayout() != 0) {
            app_set_status(APP_TEXT_STATUS_LAYOUT);
        } else {
            InvalidateRect(hwnd, NULL, TRUE);
        }
    }
    return 1;
}

static int app_handle_label(HWND hwnd, int x, int y)
{
    int target_x;
    int target_y;
    int target_kind;
    int default_allowed;
    unsigned int file_index;
    int file_disabled;

    if (g_document == NULL || PCore_LabelTargetAt(g_document, x, y,
            &target_x, &target_y, &target_kind) != 1) {
        return 0;
    }
    if (PCore_InteractionSetAt(g_document, target_x, target_y,
            PCORE_INTERACTION_FOCUS) > 0) {
        InvalidateRect(hwnd, NULL, FALSE);
    }
    AppControls_ClearButtonFocus(g_controls);
    default_allowed = 1;
    if (g_script != NULL) {
        if (AppScript_DispatchClickEvent(g_script, x, y,
                &default_allowed) != 0) {
            return 1;
        }
    } else {
        if (PCore_EventDispatchAt(g_document, x, y, "click", 1, 1,
                &default_allowed) < 0) {
            return 1;
        }
    }
    if (!default_allowed) {
        return 1;
    }
    if (target_kind >= 7 && target_kind <= 9) {
        (void) AppControls_HandleButtonPointer(g_controls, target_x,
                target_y);
        return 1;
    }
    if (target_kind == 1 || target_kind == 2) {
        (void) AppControls_HandleTogglePointer(g_controls, target_x,
                target_y);
        return 1;
    }
    if (target_kind == 10) {
        file_index = 0;
        file_disabled = 0;
        if (!app_file_input_at(g_document, target_x, target_y,
                &file_index, &file_disabled) || file_disabled) {
            return 1;
        }
        default_allowed = 1;
        if (g_script != NULL) {
            if (AppScript_DispatchClickEvent(g_script, target_x, target_y,
                    &default_allowed) != 0) {
                return 1;
            }
        } else {
            if (PCore_EventDispatchAt(g_document, target_x, target_y,
                    "click", 1, 1, &default_allowed) < 0) {
                return 1;
            }
        }
        if (default_allowed && !g_file_picker_pending) {
            (void) app_file_input_open(hwnd, g_document, g_script,
                    file_index, target_x, target_y, 0);
        }
        return 1;
    }
    if (target_kind >= 3 && target_kind <= 6) {
        default_allowed = 1;
        if (g_script != NULL) {
            if (AppScript_DispatchClickEvent(g_script, target_x, target_y,
                    &default_allowed) != 0) {
                return 1;
            }
        } else {
            if (PCore_EventDispatchAt(g_document, target_x, target_y,
                    "click", 1, 1, &default_allowed) < 0) {
                return 1;
            }
        }
        if (default_allowed) {
            (void) AppControls_FocusFormControlAt(g_controls, target_kind,
                    target_x, target_y);
        }
    }
    return 1;
}

static int app_create_menu_bar(HWND hwnd)
{
    SHMENUBARINFO menu_info;
    SHINITDLGINFO shell_init;

    memset(&menu_info, 0, sizeof(menu_info));
    menu_info.cbSize = sizeof(menu_info);
    menu_info.hwndParent = hwnd;
    menu_info.nToolBarId = AppI18n_MenuBarResource();
    menu_info.hInstRes = (g_instance != NULL) ? g_instance :
            GetModuleHandle(NULL);
    if (menu_info.nToolBarId == 0) {
        AppHostContext_SetMenuBar(&g_app, NULL);
        return 1;
    }
    if (!SHCreateMenuBar(&menu_info) || menu_info.hwndMB == NULL) {
        AppHostContext_SetMenuBar(&g_app, NULL);
        return 1;
    }
    AppHostContext_SetMenuBar(&g_app, menu_info.hwndMB);
    memset(&shell_init, 0, sizeof(shell_init));
    shell_init.dwMask = SHIDIM_FLAGS;
    shell_init.dwFlags = SHIDIF_SIZEDLGFULLSCREEN | SHIDIF_SIPDOWN;
    shell_init.hDlg = hwnd;
    if (!SHInitDialog(&shell_init)) {
        CommandBar_Destroy(menu_info.hwndMB);
        AppHostContext_SetMenuBar(&g_app, NULL);
        return 1;
    }
    return 0;
}

static void app_update_history_buttons(void)
{
    HMENU menu;
    int index;
    int can_go_back;
    int can_go_forward;
    const char *target;

    if (g_menu_bar == NULL) {
        return;
    }
    menu = (HMENU) SendMessage(g_menu_bar, SHCMBM_GETSUBMENU, 0,
            (LPARAM) APP_CMD_MENU);
    if (menu == NULL) {
        return;
    }
    target = (g_history == NULL) ? NULL :
            PBrowser_HistoryBackTarget(g_history, &index);
    can_go_back = (target != NULL);
    EnableMenuItem(menu, APP_CMD_BACK, MF_BYCOMMAND |
            (can_go_back ? MF_ENABLED : MF_GRAYED));
    target = (g_history == NULL) ? NULL :
            PBrowser_HistoryForwardTarget(g_history, &index);
    can_go_forward = (target != NULL);
    EnableMenuItem(menu, APP_CMD_FORWARD, MF_BYCOMMAND |
            (can_go_forward ? MF_ENABLED : MF_GRAYED));
    SendMessage(g_menu_bar, TB_ENABLEBUTTON, (WPARAM) APP_CMD_BACK,
            MAKELONG(can_go_back, 0));
}

static int app_load_local_page(HWND hwnd, const char *url, int history_mode,
        int history_target)
{
    HANDLE new_document;
    HANDLE new_stylesheet;
    int new_page_kind;
    int history_rc;

    if (app_build_page(url, &new_document, &new_stylesheet,
            &new_page_kind) != 0) {
        app_restore_page_status();
        app_set_address(g_current_url);
        return 0;
    }
    if (g_script != NULL) {
        int prevented;

        prevented = 0;
        if (AppScript_BeforeUnload(g_script, &prevented) != 0 ||
                prevented) {
            PCore_FreeStylesheet(new_stylesheet);
            PCore_FreeDocument(new_document);
            app_restore_page_status();
            app_set_address(g_current_url);
            return 0;
        }
    }
    history_rc = PBROWSER_OK;
    if (history_mode == APP_HISTORY_NEW) {
        history_rc = PBrowser_HistoryCommitNavigation(g_history, url,
                PBROWSER_HISTORY_METHOD_GET, PBROWSER_HISTORY_TARGET_NEW);
    } else if (history_mode == APP_HISTORY_TARGET) {
        history_rc = PBrowser_HistoryCommitTargetDocument(g_history,
                history_target);
    } else if (history_mode == APP_HISTORY_REPLACE) {
        history_rc = PBrowser_HistoryCommitNavigation(g_history, url,
                PBROWSER_HISTORY_METHOD_GET,
                PBROWSER_HISTORY_TARGET_REPLACE_CURRENT);
    }
    if (history_rc != PBROWSER_OK) {
        PCore_FreeStylesheet(new_stylesheet);
        PCore_FreeDocument(new_document);
        app_restore_page_status();
        app_set_address(g_current_url);
        return 0;
    }
    if (g_script != NULL) {
        (void) AppScript_PageTeardown(g_script);
    }
    if (g_controls != NULL) {
        AppControls_ClearPage(g_controls);
    }
    if (AppHostContext_ReplacePage(&g_app, new_document, new_stylesheet,
            new_page_kind, url) != 0) {
        PCore_FreeStylesheet(new_stylesheet);
        PCore_FreeDocument(new_document);
        app_restore_page_status();
        app_set_address(g_current_url);
        return 0;
    }
    app_set_focus_ids(g_page_kind);
    app_set_address(g_current_url);
    g_document_width = PCore_DocumentWidth(g_document);
    g_document_height = PCore_DocumentHeight(g_document);
    if (g_document_width < g_page_width) {
        g_document_width = g_page_width;
    }
    if (g_document_height < g_page_height) {
        g_document_height = g_page_height;
    }
    app_update_scrollbars(g_page_window);
    if (g_controls != NULL) {
        (void) AppControls_Rebuild(g_controls, g_document, g_script,
                g_scroll_x, g_scroll_y);
    }
    app_update_history_buttons();
    app_set_status(app_ready_status());
    if (g_page_window != NULL) {
        InvalidateRect(g_page_window, NULL, TRUE);
    } else {
        InvalidateRect(hwnd, NULL, TRUE);
    }
    return 1;
}

static int app_navigation_retired_count(void)
{
    AppNavigationRequest *request;
    int count;

    count = 0;
    for (request = g_retired_navigation; request != NULL;
            request = request->retired_next) {
        count++;
    }
    return count;
}

static int app_navigation_is_cancelled(AppNavigationRequest *request)
{
    PBrowserNavigationCandidateInfo info;

    if (request == NULL || request->candidate == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (PBrowser_NavigationCandidateGetInfo(request->candidate,
            request->generation, &info) != PBROWSER_OK) {
        return 1;
    }
    return (info.cancel_requested || info.retired) ? 1 : 0;
}

static int app_navigation_can_apply(AppNavigationRequest *request)
{
    if (request == NULL || request->candidate == NULL ||
            request->resource_transaction == NULL) {
        return 0;
    }
    return PBrowser_NavigationCandidateCanApply(request->candidate,
            (unsigned long) g_navigation_generation) == 1;
}

static int app_navigation_commit_ready(AppNavigationRequest *request)
{
    PBrowserNavigationCommitInfo info;

    if (!app_navigation_can_apply(request)) {
        return 0;
    }
    if (PBrowser_NavigationResourceCommitGate(
            request->resource_transaction) != PBROWSER_OK) {
        return 0;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (PBrowser_NavigationCommitGetInfo(request->candidate,
            request->resource_transaction,
            (unsigned long) g_navigation_generation, &info) != PBROWSER_OK) {
        return 0;
    }
    return info.decision == PBROWSER_NAVIGATION_COMMIT_READY &&
            info.can_commit;
}

static void app_navigation_request_destroy(AppNavigationRequest *request)
{
    if (request == NULL) {
        return;
    }
    if (request->worker_thread != NULL) {
        WaitForSingleObject(request->worker_thread, INFINITE);
        CloseHandle(request->worker_thread);
        request->worker_thread = NULL;
    }
    if (request->script_candidate != NULL) {
        AppScript_Destroy(request->script_candidate);
        request->script_candidate = NULL;
    }
    if (request->document_candidate != NULL) {
        PCore_FreeDocument(request->document_candidate);
        request->document_candidate = NULL;
    }
    if (request->stylesheet_candidate != NULL) {
        PCore_FreeStylesheet(request->stylesheet_candidate);
        request->stylesheet_candidate = NULL;
    }
    AppResources_DestroyRequest(request);
    if (request->resource_transaction != NULL) {
        PBrowser_NavigationResourceDestroy(request->resource_transaction);
        request->resource_transaction = NULL;
    }
    if (request->candidate != NULL) {
        PBrowser_NavigationCandidateDestroy(request->candidate);
        request->candidate = NULL;
    }
    free(request->body);
    request->body = NULL;
    request->body_bytes = 0;
    free(request);
}

static int app_navigation_cancel_active(void)
{
    AppNavigationRequest *request;

    request = g_navigation_request;
    if (request == NULL) {
        return 0;
    }
    if (app_navigation_retired_count() >= APP_NAV_MAX_RETIRED) {
        return 1;
    }
    (void) PBrowser_NavigationCandidateRequestCancel(request->candidate);
    (void) PBrowser_NavigationCandidateRetire(request->candidate);
    request->retired_next = g_retired_navigation;
    g_retired_navigation = request;
    g_navigation_request = NULL;
    return 0;
}

static void app_navigation_cancel_all(void)
{
    AppNavigationRequest *request;

    if (g_navigation_request != NULL) {
        (void) PBrowser_NavigationCandidateRequestCancel(
                g_navigation_request->candidate);
        (void) PBrowser_NavigationCandidateRetire(
                g_navigation_request->candidate);
        g_navigation_request->retired_next = g_retired_navigation;
        g_retired_navigation = g_navigation_request;
        g_navigation_request = NULL;
    }
    for (request = g_retired_navigation; request != NULL;
            request = request->retired_next) {
        (void) PBrowser_NavigationCandidateRequestCancel(
                request->candidate);
        (void) PBrowser_NavigationCandidateRetire(request->candidate);
    }
}

static int app_navigation_resource_fail(AppNavigationRequest *request,
        int index, int failure_class)
{
    if (request == NULL || request->resource_transaction == NULL ||
            index < 0) {
        return 1;
    }
    return PBrowser_NavigationResourceFail(request->resource_transaction,
            index, failure_class) == PBROWSER_OK ? 0 : 1;
}

static int app_navigation_fetch_resource(AppNavigationRequest *request,
        int index, const char *reference)
{
    PBrowserNavigationResourceInfo info;
    PHttpResponse *response;
    char host[APP_NAV_HOST_MAX];
    char path[APP_NAV_PATH_MAX];
    int port;
    int retry;
    int transport_failure;
    int is_main_request;
    int retry_allowed;
    char content_type_header[APP_HOST_CONTENT_TYPE_MAX + 32];
    const char *headers[2];

    if (request == NULL || request->resource_transaction == NULL ||
            reference == NULL) {
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (PBrowser_NavigationResourceGet(request->resource_transaction, index,
            &info) != PBROWSER_OK) {
        return 1;
    }
    if (info.state != PBROWSER_NAVIGATION_RESOURCE_PENDING ||
            info.attempted) {
        return 0;
    }
    if (app_navigation_is_cancelled(request)) {
        (void) PBrowser_NavigationResourceCancelAll(
                request->resource_transaction);
        return 1;
    }
    host[0] = '\0';
    path[0] = '\0';
    port = 443;
    if (AppResources_ResolveTransport(request, reference, host, sizeof(host),
            path, sizeof(path), &port) != 0) {
        (void) app_navigation_resource_fail(request, index,
                PBROWSER_NAVIGATION_FAILURE_RESOLVE);
        return 0;
    }
    response = NULL;
    retry = 0;
    is_main_request = index == request->resource_index;
    retry_allowed = !is_main_request || request->method ==
            PCORE_FORM_METHOD_GET;
    for (;;) {
        if (app_navigation_is_cancelled(request)) {
            (void) PBrowser_NavigationResourceCancelAll(
                    request->resource_transaction);
            return 1;
        }
        if (PBrowser_NavigationResourceBeginAttempt(
                request->resource_transaction, index) != PBROWSER_OK) {
            (void) app_navigation_resource_fail(request, index,
                    PBROWSER_NAVIGATION_FAILURE_MEMORY);
            return 0;
        }
        if (is_main_request && request->method != PCORE_FORM_METHOD_GET) {
            if (request->content_type[0] != '\0') {
                _snprintf(content_type_header,
                        sizeof(content_type_header) - 1,
                        "Content-Type: %s", request->content_type);
                content_type_header[sizeof(content_type_header) - 1] = '\0';
                headers[0] = content_type_header;
            } else {
                headers[0] = "Content-Type: application/x-www-form-urlencoded";
            }
            headers[1] = NULL;
            response = PHttp_PostEx(host, port, path, headers, request->body,
                    request->body_bytes, NULL, NULL);
        } else {
            response = PHttp_GetEx(host, port, path, NULL, NULL, NULL);
        }
        if (app_navigation_is_cancelled(request)) {
            PHttp_FreeResponse(response);
            response = NULL;
            (void) PBrowser_NavigationResourceCancelAll(
                    request->resource_transaction);
            return 1;
        }
        transport_failure = response == NULL || response->status_code == 0;
        if (transport_failure && retry_allowed &&
                PBrowser_NavigationResourceShouldRetry(
                request->resource_transaction, index, 1,
                &retry) == PBROWSER_OK && retry) {
            PHttp_FreeResponse(response);
            response = NULL;
            continue;
        }
        if (index == request->resource_index && response != NULL) {
            request->worker_status_code = response->status_code;
        }
        if (response != NULL && response->status_code >= 200 &&
                response->status_code < 300 && response->body != NULL &&
                response->body_len > 0 &&
                response->body_len <= (int)
                PBROWSER_NAVIGATION_RESOURCE_BYTES_MAX) {
            if (PBrowser_NavigationResourceSetData(
                    request->resource_transaction, index, response->body,
                    response->body_len) == PBROWSER_OK) {
                PHttp_FreeResponse(response);
                return 0;
            }
            request->worker_failure_class =
                    PBROWSER_NAVIGATION_FAILURE_MEMORY;
            (void) app_navigation_resource_fail(request, index,
                    PBROWSER_NAVIGATION_FAILURE_MEMORY);
        } else if (transport_failure) {
            request->worker_failure_class =
                    PBROWSER_NAVIGATION_FAILURE_TRANSPORT;
            (void) app_navigation_resource_fail(request, index,
                    PBROWSER_NAVIGATION_FAILURE_TRANSPORT);
        } else if (response != NULL && response->body_len >
                (int) PBROWSER_NAVIGATION_RESOURCE_BYTES_MAX) {
            request->worker_failure_class =
                    PBROWSER_NAVIGATION_FAILURE_BUDGET;
            (void) app_navigation_resource_fail(request, index,
                    PBROWSER_NAVIGATION_FAILURE_BUDGET);
        } else {
            request->worker_failure_class =
                    PBROWSER_NAVIGATION_FAILURE_HTTP;
            (void) app_navigation_resource_fail(request, index,
                    PBROWSER_NAVIGATION_FAILURE_HTTP);
        }
        PHttp_FreeResponse(response);
        response = NULL;
        return 0;
    }
}

static DWORD WINAPI app_navigation_worker(LPVOID parameter)
{
    AppNavigationRequest *request;
    AppNavigationResource *resource;
    PBrowserNavigationResourceInfo info;

    request = (AppNavigationRequest *) parameter;
    if (request == NULL) {
        return 0;
    }
    request->worker_succeeded = 0;
    request->worker_failure_class = PBROWSER_NAVIGATION_FAILURE_TRANSPORT;
    request->worker_status_code = 0;
    if (!g_http_initialized || app_navigation_is_cancelled(request)) {
        goto done;
    }
    if (request->worker_stage == APP_NAV_WORK_DOCUMENT) {
        if (app_navigation_fetch_resource(request, request->resource_index,
                request->url) != 0) {
            goto done;
        }
    } else {
        for (;;) {
            if (app_navigation_is_cancelled(request)) {
                (void) PBrowser_NavigationResourceCancelAll(
                        request->resource_transaction);
                goto done;
            }
            if (AppResources_FindPending(request, &resource) != 0) {
                break;
            }
            if (app_navigation_fetch_resource(request, resource->index,
                    resource->url) != 0) {
                goto done;
            }
        }
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (PBrowser_NavigationResourceGet(request->resource_transaction,
            request->resource_index, &info) == PBROWSER_OK &&
            info.state == PBROWSER_NAVIGATION_RESOURCE_READY &&
            info.data_bytes > 0) {
        request->worker_succeeded = 1;
    }
done:
    if (!PostMessage(request->hwnd, APP_WM_NAV_DONE, 0,
            (LPARAM) request)) {
        /* The UI keeps the window alive until this message is processed. */
    }
    return 0;
}

static int app_navigation_parse_document(AppNavigationRequest *request)
{
    PBrowserNavigationResourceInfo info;
    HANDLE document;
    HANDLE stylesheet;
    char *bytes;
    int copied;

    if (request == NULL || request->resource_transaction == NULL) {
        return 1;
    }
    request->document_candidate = NULL;
    request->stylesheet_candidate = NULL;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (PBrowser_NavigationResourceGet(request->resource_transaction,
            request->resource_index, &info) != PBROWSER_OK ||
            info.state != PBROWSER_NAVIGATION_RESOURCE_READY ||
            info.data_bytes <= 0) {
        return 1;
    }
    bytes = (char *) malloc((size_t) info.data_bytes + 1U);
    if (bytes == NULL) {
        return 1;
    }
    copied = 0;
    if (PBrowser_NavigationResourceCopyData(
            request->resource_transaction, request->resource_index, bytes,
            info.data_bytes + 1, &copied) != PBROWSER_OK ||
            copied != info.data_bytes) {
        free(bytes);
        return 1;
    }
    bytes[copied] = '\0';
    document = PCore_ParseHTML(bytes, (unsigned int) copied);
    free(bytes);
    if (document == NULL) {
        return 1;
    }
    stylesheet = PCore_ParseCSS(g_app_css, 0, request->url);
    if (stylesheet == NULL) {
        PCore_FreeDocument(document);
        return 1;
    }
    request->document_candidate = document;
    request->stylesheet_candidate = stylesheet;
    return 0;
}

static int app_navigation_pending_count(AppNavigationRequest *request)
{
    return AppResources_PendingCount(request);
}

static int app_navigation_start_worker(AppNavigationRequest *request)
{
    if (request == NULL || request->worker_thread != NULL) {
        return 1;
    }
    request->worker_thread = CreateThread(NULL, 0, app_navigation_worker,
            request, 0, NULL);
    return request->worker_thread == NULL ? 1 : 0;
}

static void app_navigation_finish(AppNavigationRequest *request,
        int committed)
{
    if (request == g_navigation_request) {
        g_navigation_request = NULL;
    }
    if (!committed) {
        app_restore_page_status();
        app_set_address(g_current_url);
        if (request->candidate != NULL) {
            (void) PBrowser_NavigationCandidateMarkFailed(
                    request->candidate);
        }
    }
    app_navigation_request_destroy(request);
    if (g_navigation_closing && g_navigation_request == NULL &&
            g_retired_navigation == NULL) {
        DestroyWindow(g_window);
    }
}

static void app_navigation_remove_retired(AppNavigationRequest *request)
{
    AppNavigationRequest *current;
    AppNavigationRequest *previous;

    previous = NULL;
    current = g_retired_navigation;
    while (current != NULL && current != request) {
        previous = current;
        current = current->retired_next;
    }
    if (current == NULL) {
        return;
    }
    if (previous == NULL) {
        g_retired_navigation = current->retired_next;
    } else {
        previous->retired_next = current->retired_next;
    }
    current->retired_next = NULL;
}

static int app_navigation_advance(HWND hwnd, AppNavigationRequest *request)
{
    int result;
    int history_rc;

    if (request == NULL) {
        return -1;
    }
    for (;;) {
        if (!app_navigation_can_apply(request)) {
            return -1;
        }
        if (request->commit_stage == APP_NAV_COMMIT_SCRIPTS) {
            AppScriptHostCallbacks script_callbacks;
            const char *history_state;
            int history_length;
            int history_index;
            int script_errors;

            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_SCRIPT;
            result = PCore_FetchScriptResourcesEx(request->document_candidate,
                    request->url, AppResources_Resolve, AppResources_Fetch,
                    AppResources_Free, request, NULL, NULL);
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE;
            if (result != 0) {
                /* Script discovery is optional; the document may still
                 * commit when Core reports a non-fatal scan limitation. */
            }
            if (app_navigation_pending_count(request) > 0) {
                request->worker_stage = APP_NAV_WORK_RESOURCES;
                if (app_navigation_start_worker(request) != 0) {
                    return -1;
                }
                return 0;
            }
            if (PCore_GetScriptCount(request->document_candidate) > 0) {
                if (request->history_mode == APP_HISTORY_NEW) {
                    history_length = PBrowser_HistoryNavigationLength(
                            g_history, request->url,
                            PBROWSER_HISTORY_METHOD_GET,
                            PBROWSER_HISTORY_TARGET_NEW);
                    history_index = PBrowser_HistoryNavigationIndex(
                            g_history, request->url,
                            PBROWSER_HISTORY_METHOD_GET,
                            PBROWSER_HISTORY_TARGET_NEW);
                    history_state = PBrowser_HistoryNavigationState(
                            g_history, request->url,
                            PBROWSER_HISTORY_METHOD_GET,
                            PBROWSER_HISTORY_TARGET_NEW);
                } else if (request->history_mode == APP_HISTORY_TARGET) {
                    history_length = PBrowser_HistoryNavigationLength(
                            g_history, request->url,
                            PBROWSER_HISTORY_METHOD_GET,
                            request->history_target);
                    history_index = PBrowser_HistoryNavigationIndex(
                            g_history, request->url,
                            PBROWSER_HISTORY_METHOD_GET,
                            request->history_target);
                    history_state = PBrowser_HistoryNavigationState(
                            g_history, request->url,
                            PBROWSER_HISTORY_METHOD_GET,
                            request->history_target);
                } else {
                    history_length = PBrowser_HistoryCount(g_history);
                    history_index = PBrowser_HistoryIndex(g_history);
                    history_state = PBrowser_HistoryCurrentState(g_history);
                }
                memset(&script_callbacks, 0, sizeof(script_callbacks));
                script_callbacks.size = sizeof(script_callbacks);
                script_callbacks.pw = &g_app;
                script_callbacks.navigate = app_script_navigate;
                script_callbacks.scroll = app_script_scroll;
                script_callbacks.mutation = app_script_mutated;
                script_callbacks.form_reset_applied =
                        app_script_form_reset_applied;
                script_callbacks.get_contenteditable_selection =
                        app_script_contenteditable_selection_get;
                script_callbacks.set_contenteditable_selection =
                        app_script_contenteditable_selection_set;
                script_callbacks.validate_form_submit =
                        app_script_validate_form_submit;
                script_callbacks.submit_form = app_script_submit_form;
                script_callbacks.submit_form_direct =
                        app_script_submit_form_direct;
                script_callbacks.form_navigation =
                        app_script_form_navigation;
                script_callbacks.get_programmatic_click_target =
                        app_script_programmatic_click_target;
                script_callbacks.validate_programmatic_click =
                        app_script_programmatic_click_validate;
                script_callbacks.programmatic_click_default =
                        app_script_programmatic_click_default;
                script_callbacks.programmatic_click_generic =
                        app_script_programmatic_click_generic;
                script_callbacks.get_programmatic_anchor_target =
                        app_script_programmatic_anchor_target;
                request->script_candidate = AppScript_Create(
                        request->document_candidate, request->url,
                        history_length, history_index, 1, history_state,
                        g_page_width, g_page_height, g_dpi,
                        &script_callbacks);
                if (request->script_candidate != NULL) {
                    script_errors = 0;
                    if (AppScript_Execute(request->script_candidate, 1,
                            AppResources_Resolve, request, NULL, NULL,
                            &script_errors) != 0) {
                        AppScript_Destroy(request->script_candidate);
                        request->script_candidate = NULL;
                    }
                }
            }
            request->commit_stage = APP_NAV_COMMIT_STYLE;
            continue;
        }
        if (request->commit_stage == APP_NAV_COMMIT_STYLE) {
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_REQUIRED;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET;
            result = PCore_StyleDocumentEx2(request->document_candidate,
                    request->stylesheet_candidate, request->url,
                    AppResources_Resolve, AppResources_Fetch,
                    AppResources_Free, request);
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE;
            if (result != 0 || request->resource_registration_failed) {
                return -1;
            }
            if (app_navigation_pending_count(request) > 0) {
                request->worker_stage = APP_NAV_WORK_RESOURCES;
                if (app_navigation_start_worker(request) != 0) {
                    return -1;
                }
                return 0;
            }
            request->commit_stage = APP_NAV_COMMIT_IMAGES;
            continue;
        }
        if (request->commit_stage == APP_NAV_COMMIT_IMAGES) {
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_IMAGE;
            (void) PCore_FetchImageResources(request->document_candidate,
                    AppResources_Fetch, AppResources_Free, request,
                    NULL, NULL);
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE;
            if (app_navigation_pending_count(request) > 0) {
                request->worker_stage = APP_NAV_WORK_RESOURCES;
                if (app_navigation_start_worker(request) != 0) {
                    return -1;
                }
                return 0;
            }
            if (!app_navigation_commit_ready(request)) {
                return -1;
            }
            request->commit_stage = APP_NAV_COMMIT_LAYOUT;
            continue;
        }
        if (request->commit_stage != APP_NAV_COMMIT_LAYOUT ||
                request->document_candidate == NULL ||
                request->stylesheet_candidate == NULL ||
                !app_navigation_commit_ready(request)) {
            return -1;
        }
        PCore_SetDeviceViewport(g_page_width, g_page_height, g_dpi);
        if (PCore_LayoutDocument(request->document_candidate, g_page_width,
                g_page_height) != 0) {
            return -1;
        }
        (void) PBrowser_NavigationResourceObserveFallbacks(
                request->resource_transaction);
        if (g_script != NULL) {
            int prevented;

            prevented = 0;
            if (AppScript_BeforeUnload(g_script, &prevented) != 0 ||
                    prevented) {
                return -1;
            }
        }
        if (request->method != PCORE_FORM_METHOD_GET) {
            /* The current Browser history ABI records GET document entries;
             * POST/multipart still replace the visible document but do not
             * invent a replayable history entry. */
            history_rc = PBROWSER_OK;
        } else if (request->history_mode == APP_HISTORY_NEW) {
            history_rc = PBrowser_HistoryCommitNavigation(g_history,
                    request->url, PBROWSER_HISTORY_METHOD_GET,
                    PBROWSER_HISTORY_TARGET_NEW);
        } else if (request->history_mode == APP_HISTORY_TARGET) {
            history_rc = PBrowser_HistoryCommitTargetDocument(g_history,
                    request->history_target);
        } else if (request->history_mode == APP_HISTORY_REPLACE) {
            history_rc = PBrowser_HistoryCommitNavigation(g_history,
                    request->url, PBROWSER_HISTORY_METHOD_GET,
                    PBROWSER_HISTORY_TARGET_REPLACE_CURRENT);
        } else {
            history_rc = PBROWSER_OK;
        }
        if (history_rc != PBROWSER_OK ||
                PBrowser_NavigationCandidateMarkCommitted(request->candidate,
                (unsigned long) g_navigation_generation) != PBROWSER_OK) {
            return -1;
        }
        if (g_script != NULL) {
            (void) AppScript_PageTeardown(g_script);
        }
        if (g_controls != NULL) {
            AppControls_ClearPage(g_controls);
        }
        if (AppHostContext_ReplacePageWithScript(&g_app,
                request->document_candidate, request->stylesheet_candidate,
                request->script_candidate, 0, request->url) != 0) {
            return -1;
        }
        request->document_candidate = NULL;
        request->stylesheet_candidate = NULL;
        request->script_candidate = NULL;
        app_set_focus_ids(g_page_kind);
        app_set_address(g_current_url);
        g_document_width = PCore_DocumentWidth(g_document);
        g_document_height = PCore_DocumentHeight(g_document);
        if (g_document_width < g_page_width) {
            g_document_width = g_page_width;
        }
        if (g_document_height < g_page_height) {
            g_document_height = g_page_height;
        }
        app_update_scrollbars(g_page_window);
        if (g_controls != NULL) {
            (void) AppControls_Rebuild(g_controls, g_document, g_script,
                    g_scroll_x, g_scroll_y);
        }
        app_update_history_buttons();
        app_set_status(app_ready_status());
        if (g_script != NULL) {
            (void) AppScript_SetVisibility(g_script,
                    IsWindowVisible(g_window) ? 0 : 1);
            (void) AppScript_SetFocus(g_script,
                    GetForegroundWindow() == g_window ? 1 : 0);
            (void) AppScript_PageLifecycleComplete(g_script);
            if (AppScript_HasPendingNavigation(g_script)) {
                PostMessage(g_window, APP_WM_SCRIPT_NAVIGATE, 0,
                        (LPARAM) g_script);
            }
        }
        InvalidateRect(g_page_window, NULL, TRUE);
        return 1;
    }
}

static void app_navigation_handle_done(HWND hwnd,
        AppNavigationRequest *request)
{
    int advance_result;

    if (request == NULL) {
        return;
    }
    if (request->worker_thread != NULL) {
        WaitForSingleObject(request->worker_thread, INFINITE);
        CloseHandle(request->worker_thread);
        request->worker_thread = NULL;
    }
    if (request != g_navigation_request) {
        app_navigation_remove_retired(request);
        app_navigation_request_destroy(request);
        if (g_navigation_closing && g_retired_navigation == NULL &&
                g_navigation_request == NULL) {
            DestroyWindow(hwnd);
        }
        return;
    }
    if (!request->worker_succeeded || !app_navigation_can_apply(request)) {
        app_navigation_finish(request, 0);
        return;
    }
    if (request->worker_stage == APP_NAV_WORK_DOCUMENT) {
        if (app_navigation_parse_document(request) != 0) {
            app_navigation_finish(request, 0);
            return;
        }
        request->worker_stage = APP_NAV_WORK_RESOURCES;
    }
    advance_result = app_navigation_advance(hwnd, request);
    if (advance_result < 0) {
        app_navigation_finish(request, 0);
    } else if (advance_result > 0) {
        app_navigation_finish(request, 1);
    }
}

static int app_navigation_start(HWND hwnd, const char *url, int method,
        const void *body, int body_bytes, const char *content_type,
        int history_mode, int history_target)
{
    AppNavigationRequest *request;
    LONG generation;
    int index;

    if (!g_http_initialized || url == NULL || url[0] == '\0' ||
            (method != PCORE_FORM_METHOD_GET && method !=
            PCORE_FORM_METHOD_POST && method != PCORE_FORM_METHOD_MULTIPART) ||
            body_bytes < 0 || body_bytes > APP_FORMS_BODY_MAX_BYTES ||
            (body_bytes > 0 && body == NULL) || (content_type != NULL &&
            (int) strlen(content_type) >= APP_HOST_CONTENT_TYPE_MAX)) {
        app_restore_page_status();
        return 0;
    }
    app_file_picker_cancel_pending();
    if (g_navigation_request != NULL &&
            app_navigation_retired_count() >= APP_NAV_MAX_RETIRED) {
        app_restore_page_status();
        return 0;
    }
    request = (AppNavigationRequest *) malloc(sizeof(*request));
    if (request == NULL) {
        app_restore_page_status();
        return 0;
    }
    memset(request, 0, sizeof(*request));
    request->hwnd = hwnd;
    request->method = method;
    request->history_mode = history_mode;
    request->history_target = history_target;
    if (body_bytes > 0) {
        request->body = (char *) malloc((size_t) body_bytes);
        if (request->body == NULL) {
            free(request);
            app_restore_page_status();
            return 0;
        }
        memcpy(request->body, body, (size_t) body_bytes);
        request->body_bytes = body_bytes;
    }
    app_copy_text(request->content_type,
            sizeof(request->content_type), content_type);
    app_copy_text(request->url, sizeof(request->url), url);
    if (PHttp_ResolveReference(NULL, 443, NULL, request->url,
            request->host, sizeof(request->host), request->path,
            sizeof(request->path), &request->port) != 0) {
        free(request);
        app_restore_page_status();
        return 0;
    }
    request->resource_transaction = PBrowser_NavigationResourceCreate();
    if (request->resource_transaction == NULL ||
            AppResources_Register(request, request->url,
            PBROWSER_NAVIGATION_RESOURCE_REQUIRED,
            PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE, &index) != 0) {
        app_navigation_request_destroy(request);
        app_restore_page_status();
        return 0;
    }
    request->resource_index = index;
    request->worker_stage = APP_NAV_WORK_DOCUMENT;
    request->commit_stage = APP_NAV_COMMIT_SCRIPTS;
    generation = InterlockedIncrement(&g_navigation_generation);
    if (generation <= 0) {
        generation = InterlockedIncrement(&g_navigation_generation);
    }
    request->generation = (unsigned long) generation;
    request->candidate = PBrowser_NavigationCandidateCreate(
            request->generation);
    if (request->candidate == NULL) {
        app_navigation_request_destroy(request);
        app_restore_page_status();
        return 0;
    }
    if (g_navigation_request != NULL && app_navigation_cancel_active() != 0) {
        app_navigation_request_destroy(request);
        app_restore_page_status();
        return 0;
    }
    g_navigation_request = request;
    app_set_status(APP_TEXT_STATUS_LOADING);
    app_set_address(request->url);
    if (app_navigation_start_worker(request) != 0) {
        g_navigation_request = NULL;
        app_navigation_finish(request, 0);
        return 0;
    }
    return 1;
}

static int app_load_page(HWND hwnd, const char *url, int history_mode,
        int history_target)
{
    char canonical[APP_URL_MAX];

    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    if (app_page_kind(url) != 0) {
        if (g_navigation_request != NULL &&
                app_navigation_cancel_active() != 0) {
            app_restore_page_status();
            return 0;
        }
        return app_load_local_page(hwnd, url, history_mode, history_target);
    }
    if (app_canonicalize_url(g_current_url, url, canonical,
            sizeof(canonical)) != 0) {
        app_restore_page_status();
        return 0;
    }
    return app_navigation_start(hwnd, canonical, PCORE_FORM_METHOD_GET,
            NULL, 0, NULL, history_mode, history_target);
}

static int app_load_form_request(HWND hwnd, const AppFormRequest *request)
{
    char canonical[APP_URL_MAX];

    if (request == NULL || !request->valid || request->target_url[0] == '\0' ||
            request->method == PCORE_FORM_METHOD_DIALOG) {
        return 0;
    }
    if (request->method == PCORE_FORM_METHOD_GET) {
        return app_load_page(hwnd, request->target_url, APP_HISTORY_NEW, -1);
    }
    if (app_page_kind(request->target_url) != 0 ||
            app_canonicalize_url(g_current_url, request->target_url,
            canonical, sizeof(canonical)) != 0) {
        app_restore_page_status();
        return 0;
    }
    return app_navigation_start(hwnd, canonical, request->method,
            request->body, request->body_bytes, request->content_type,
            APP_HISTORY_NEW, -1);
}

static void app_apply_form_request(int result, AppFormRequest *request)
{
    int closed;

    if (request == NULL) {
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        return;
    }
    if (result == APP_FORMS_RESULT_INVALID) {
        app_set_status(APP_TEXT_STATUS_FORM_INVALID);
        AppForms_ClearRequest(request);
        return;
    }
    if (result == APP_FORMS_RESULT_UNSUPPORTED) {
        app_set_status(APP_TEXT_STATUS_FORM_UNSUPPORTED);
        AppForms_ClearRequest(request);
        return;
    }
    if (result != APP_FORMS_RESULT_OK || !request->valid) {
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        AppForms_ClearRequest(request);
        return;
    }
    if (request->method == PCORE_FORM_METHOD_DIALOG) {
        closed = 0;
        if (g_script == NULL || AppScript_CloseDialog(g_script,
                request->dialog_id, request->return_value, &closed) != 0 ||
                !closed) {
            app_set_status(APP_TEXT_STATUS_FORM_UNSUPPORTED);
        } else {
            app_script_schedule_refresh(&g_app, g_script, 0);
        }
        AppForms_ClearRequest(request);
        return;
    }
    if (!app_load_form_request(g_window, request)) {
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
    }
    AppForms_ClearRequest(request);
}

static void app_handle_form_submit(void *pw, int document_x,
        int document_y, int validation_valid)
{
    AppFormsAdapter adapter;
    AppFormRequest request;
    int result;

    if (pw != &g_app || g_document == NULL) {
        return;
    }
    if (!validation_valid) {
        PCoreFormValidationInfo validation;

        memset(&validation, 0, sizeof(validation));
        if (PCore_FormValidationAt(g_document, document_x, document_y,
                &validation) == 1) {
            app_handle_invalid_validation(&validation);
        } else {
            app_set_status(APP_TEXT_STATUS_FORM_INVALID);
        }
        return;
    }
    app_forms_adapter_init(&adapter);
    AppForms_InitRequest(&request);
    result = AppForms_BuildAt(g_document, g_current_url, document_x,
            document_y, &adapter, &request);
    app_apply_form_request(result, &request);
}

static void app_handle_form_enter(void *pw, unsigned int text_index)
{
    AppFormsAdapter adapter;
    AppFormRequest request;
    PCoreFormSubmissionInfo submission;
    PCoreTextInputInfo text_info;
    char action_probe[1];
    char body_probe[1];
    int result;
    int default_allowed;
    int event_result;
    int event_x;
    int event_y;

    if (pw != &g_app || g_document == NULL) {
        return;
    }
    memset(&submission, 0, sizeof(submission));
    action_probe[0] = '\0';
    body_probe[0] = '\0';
    result = PCore_FormSubmissionForTextInput(g_document, text_index,
            &submission, action_probe, sizeof(action_probe), body_probe,
            sizeof(body_probe));
    if (result == 0 || result == 2) {
        return;
    }
    if (result == 5) {
        app_handle_invalid_form(text_index);
        return;
    }
    if (result != 1 && result != 3 && result != 4 && result != 6) {
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        return;
    }
    memset(&text_info, 0, sizeof(text_info));
    if (PCore_TextInputInfo(g_document, text_index, &text_info, NULL, 0) != 0 ||
            text_info.width <= 0 || text_info.height <= 0) {
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        return;
    }
    event_x = text_info.x + text_info.width / 2;
    event_y = text_info.y + text_info.height / 2;
    default_allowed = 1;
    if (g_script != NULL) {
        event_result = AppScript_DispatchFormEvent(g_script, event_x,
                event_y, "submit", &default_allowed);
        if (event_result != 0) {
            app_set_status(APP_TEXT_STATUS_FORM_FAILED);
            return;
        }
    } else {
        event_result = PCore_EventDispatchAt(g_document, event_x, event_y,
                "submit", 1, 1, &default_allowed);
        if (event_result < 0) {
            app_set_status(APP_TEXT_STATUS_FORM_FAILED);
            return;
        }
    }
    if (!default_allowed) {
        return;
    }
    app_forms_adapter_init(&adapter);
    AppForms_InitRequest(&request);
    result = AppForms_BuildForTextInput(g_document, g_current_url,
            text_index, &adapter, &request);
    app_apply_form_request(result, &request);
}

static void app_handle_invalid_form(unsigned int text_index)
{
    PCoreFormValidationInfo validation;
    int result;

    if (g_document == NULL) {
        return;
    }
    memset(&validation, 0, sizeof(validation));
    result = PCore_FormValidationForTextInput(g_document, text_index,
            &validation);
    if (!result || validation.valid || validation.invalid_count <= 0) {
        app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        return;
    }
    app_handle_invalid_validation(&validation);
}

static void app_handle_invalid_validation(
        PCoreFormValidationInfo *validation)
{
    int result;
    int event_result;
    int default_allowed;
    int target_scroll_x;
    int target_scroll_y;
    int left;
    int right;
    int top;
    int bottom;
    int center_x;
    int center_y;

    if (g_document == NULL || validation == NULL || validation->valid ||
            validation->invalid_count <= 0) {
        app_set_status(APP_TEXT_STATUS_FORM_INVALID);
        return;
    }
    center_x = validation->first_x + validation->first_width / 2;
    center_y = validation->first_y + validation->first_height / 2;
    default_allowed = 1;
    if (g_script != NULL) {
        event_result = AppScript_DispatchInvalidEvent(g_script, center_x,
                center_y, &default_allowed);
        if (event_result != 0) {
            app_set_status(APP_TEXT_STATUS_FORM_FAILED);
            return;
        }
    } else {
        result = PCore_EventDispatchAt(g_document, center_x, center_y,
                "invalid", 0, 1, &default_allowed);
        if (result < 0) {
            app_set_status(APP_TEXT_STATUS_FORM_FAILED);
            return;
        }
    }
    if (!default_allowed) {
        return;
    }

    if (validation->first_width > 0 && validation->first_height > 0) {
        left = validation->first_x;
        right = left + validation->first_width;
        target_scroll_x = g_scroll_x;
        if (left < g_scroll_x + 8) {
            target_scroll_x = left - 8;
        } else if (right > g_scroll_x + g_page_width - 8) {
            target_scroll_x = right - g_page_width + 8;
        }
        top = validation->first_y;
        bottom = top + validation->first_height;
        target_scroll_y = g_scroll_y;
        if (top < g_scroll_y + 8) {
            target_scroll_y = top - 8;
        } else if (bottom > g_scroll_y + g_page_height - 8) {
            target_scroll_y = bottom - g_page_height + 8;
        }
        app_scroll_by(g_page_window, target_scroll_x - g_scroll_x,
                target_scroll_y - g_scroll_y);
        if (g_controls != NULL) {
            (void) AppControls_FocusFormControlAt(g_controls,
                    validation->first_control_kind, center_x, center_y);
        }
        InvalidateRect(g_page_window, NULL, FALSE);
    }
    MessageBeep(MB_ICONEXCLAMATION);
    app_set_status(APP_TEXT_STATUS_FORM_INVALID);
}

static int app_script_validate_form_submit(void *pw,
        AppScriptContext *context, HANDLE document,
        const PBrowserScriptFormSubmitInfo *info, int *out_valid)
{
    AppHostContext *host;
    PCoreFormValidationInfo validation;

    host = (AppHostContext *) pw;
    if (out_valid == NULL) {
        return -1;
    }
    *out_valid = -1;
    if (host != &g_app || context == NULL || document == NULL ||
            AppScript_Document(context) != document || info == NULL ||
            info->size < sizeof(*info) || info->form_id == NULL ||
            info->form_id[0] == '\0') {
        return 0;
    }
    memset(&validation, 0, sizeof(validation));
    if (PCore_FormValidationSubmitById(document, info->form_id,
            info->submitter_id, &validation) != 0) {
        return 0;
    }
    *out_valid = validation.valid ? 1 : 0;
    if (!*out_valid && host->script == context) {
        app_set_status(APP_TEXT_STATUS_FORM_INVALID);
    }
    return 0;
}

static int app_script_submit_form(void *pw, AppScriptContext *context,
        HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info,
        AppFormRequest *out_request)
{
    return app_script_build_form_request(pw, context, document, document_url,
            info, out_request, 1);
}

static int app_script_submit_form_direct(void *pw,
        AppScriptContext *context, HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info,
        AppFormRequest *out_request)
{
    if (info == NULL || info->submitter_id == NULL ||
            info->submitter_id[0] != '\0') {
        return -1;
    }
    return app_script_build_form_request(pw, context, document, document_url,
            info, out_request, 0);
}

static int app_script_build_form_request(void *pw,
        AppScriptContext *context, HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info,
        AppFormRequest *out_request, int validate)
{
    AppHostContext *host;
    AppFormsAdapter adapter;
    int result;

    host = (AppHostContext *) pw;
    if (host != &g_app || context == NULL || document == NULL ||
            AppScript_Document(context) != document || document_url == NULL ||
            document_url[0] == '\0' || info == NULL ||
            info->size < sizeof(*info) || info->form_id == NULL ||
            info->form_id[0] == '\0' || out_request == NULL) {
        return -1;
    }
    app_forms_adapter_init(&adapter);
    AppForms_InitRequest(out_request);
    result = AppForms_BuildById(document, document_url, info->form_id,
            info->submitter_id, validate, &adapter, out_request);
    if (result == APP_FORMS_RESULT_OK) {
        return 1;
    }
    AppForms_ClearRequest(out_request);
    if (host->script == context) {
        if (result == APP_FORMS_RESULT_INVALID) {
            app_set_status(APP_TEXT_STATUS_FORM_INVALID);
        } else if (result == APP_FORMS_RESULT_UNSUPPORTED) {
            app_set_status(APP_TEXT_STATUS_FORM_UNSUPPORTED);
        } else if (result != APP_FORMS_RESULT_NONE) {
            app_set_status(APP_TEXT_STATUS_FORM_FAILED);
        }
    }
    return result == APP_FORMS_RESULT_NONE ||
            result == APP_FORMS_RESULT_INVALID ||
            result == APP_FORMS_RESULT_UNSUPPORTED ? 0 : -1;
}

static int app_script_form_navigation(void *pw, AppScriptContext *context)
{
    AppHostContext *host;

    host = (AppHostContext *) pw;
    if (host == NULL || host != &g_app || context == NULL ||
            host->script != context || host->window == NULL) {
        return 1;
    }
    return PostMessage(host->window, APP_WM_SCRIPT_NAVIGATE, 0,
            (LPARAM) context) ? 1 : 0;
}

static int app_normalize_address(const char *input, char *output,
        int output_capacity)
{
    const char *start;
    const char *end;
    int length;

    if (input == NULL || output == NULL || output_capacity <= 1) {
        return 1;
    }
    start = input;
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
    if (length == 0) {
        app_copy_text(output, output_capacity, APP_URL_WELCOME);
        return 0;
    }
    if (length == 7 && strncmp(start, "welcome", 7) == 0) {
        app_copy_text(output, output_capacity, APP_URL_WELCOME);
        return 0;
    }
    if (length == 8 && strncmp(start, "controls", 8) == 0) {
        app_copy_text(output, output_capacity, APP_URL_CONTROLS);
        return 0;
    }
    if (length >= output_capacity) {
        return 1;
    }
    {
        char reference[APP_URL_MAX];

        memcpy(reference, start, (size_t) length);
        reference[length] = '\0';
        if (app_page_kind(reference) != 0) {
            app_copy_text(output, output_capacity, reference);
            return 0;
        }
        return app_canonicalize_url(NULL, reference, output,
                output_capacity);
    }
}

static void app_go_from_address(HWND hwnd)
{
    char input[APP_URL_MAX];
    char url[APP_URL_MAX];

    if (app_wide_to_utf8(g_address, input, sizeof(input)) != 0 ||
            app_normalize_address(input, url, sizeof(url)) != 0) {
        app_restore_page_status();
        app_set_address(g_current_url);
        return;
    }
    app_load_page(hwnd, url, APP_HISTORY_NEW, -1);
}

static void app_go_home(HWND hwnd)
{
    (void) app_load_page(hwnd, APP_URL_WELCOME,
            APP_HISTORY_NEW, -1);
}

static void app_go_back(HWND hwnd)
{
    const char *target;
    char url[APP_URL_MAX];
    int index;

    target = PBrowser_HistoryBackTarget(g_history, &index);
    if (target == NULL || strlen(target) >= sizeof(url)) {
        app_set_status(APP_TEXT_STATUS_NO_BACK);
        return;
    }
    app_copy_text(url, sizeof(url), target);
    (void) app_load_page(hwnd, url, APP_HISTORY_TARGET, index);
}

static void app_go_forward(HWND hwnd)
{
    const char *target;
    char url[APP_URL_MAX];
    int index;

    target = PBrowser_HistoryForwardTarget(g_history, &index);
    if (target == NULL || strlen(target) >= sizeof(url)) {
        app_set_status(APP_TEXT_STATUS_NO_FORWARD);
        return;
    }
    app_copy_text(url, sizeof(url), target);
    (void) app_load_page(hwnd, url, APP_HISTORY_TARGET, index);
}

static void app_refresh(HWND hwnd)
{
    if (g_current_url[0] != '\0') {
        (void) app_load_page(hwnd, g_current_url,
                APP_HISTORY_REFRESH, -1);
    }
}

static void app_handle_script_navigation(HWND hwnd,
        AppScriptContext *context)
{
    AppScriptPendingNavigation navigation;
    const char *target;
    int target_index;
    int history_result;

    if (context == NULL || context != g_script ||
            AppScript_TakeNavigation(context, &navigation) != 0) {
        return;
    }
    if (navigation.form_valid) {
        app_apply_form_request(APP_FORMS_RESULT_OK, &navigation.form);
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_BACK) {
        app_go_back(hwnd);
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FORWARD) {
        app_go_forward(hwnd);
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_GO) {
        target = PBrowser_HistoryGoTarget(g_history, navigation.delta,
                &target_index);
        if (target != NULL) {
            app_load_page(hwnd, target, APP_HISTORY_TARGET, target_index);
        }
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_RELOAD) {
        app_refresh(hwnd);
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FRAGMENT ||
            navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FRAGMENT_REPLACE) {
        if (navigation.url[0] == '\0' ||
                !PBrowser_HistorySameBaseUrl(g_current_url,
                navigation.url)) {
            return;
        }
        if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FRAGMENT) {
            history_result = PBrowser_HistoryPushState(g_history,
                    navigation.url, "null");
        } else {
            history_result = PBrowser_HistoryReplaceState(g_history,
                    navigation.url, "null");
        }
        if (history_result == PBROWSER_OK) {
            app_copy_text(g_current_url, sizeof(g_current_url),
                    navigation.url);
            app_set_address(g_current_url);
            app_update_history_buttons();
            (void) AppScript_DispatchHashNavigation(g_script,
                    g_current_url, PBrowser_HistoryCount(g_history));
        }
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_REPLACE) {
        if (navigation.url[0] != '\0') {
            app_load_page(hwnd, navigation.url, APP_HISTORY_REPLACE, -1);
        }
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_ASSIGN &&
            navigation.url[0] != '\0') {
        app_load_page(hwnd, navigation.url, APP_HISTORY_NEW, -1);
    }
}

static LRESULT CALLBACK app_address_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    HWND parent;

    parent = GetParent(hwnd);
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        PostMessage(parent, APP_WM_ADDRESS_GO, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        PostMessage(parent, APP_WM_ADDRESS_CANCEL, 0, 0);
        return 0;
    }
    if (g_address_original_proc != NULL) {
        return CallWindowProc(g_address_original_proc, hwnd, message,
                wparam, lparam);
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

static void app_paint_page(HWND hwnd, HDC dc)
{
    RECT client;
    RECT clear;
    RECT focus_rect;
    PCoreFocusTargetInfo focus_info;
    int saved;

    GetClientRect(hwnd, &client);
    saved = SaveDC(dc);
    IntersectClipRect(dc, client.left, client.top,
            client.right, client.bottom);
    clear.left = 0;
    clear.top = 0;
    clear.right = client.right - client.left;
    clear.bottom = client.bottom - client.top;
    FillRect(dc, &clear, (HBRUSH) GetStockObject(WHITE_BRUSH));
    if (g_document != NULL) {
        PCore_PaintDocument(g_document, dc, g_scroll_x, g_scroll_y);
        if (g_focus_index >= 0 && g_focus_id[0] != '\0' &&
                PCore_FocusTargetInfoById(g_document, g_focus_id,
                &focus_info) == 0) {
            focus_rect.left = focus_info.x - g_scroll_x - 2;
            focus_rect.top = focus_info.y - g_scroll_y - 2;
            focus_rect.right = focus_info.x + focus_info.width -
                    g_scroll_x + 2;
            focus_rect.bottom = focus_info.y + focus_info.height -
                    g_scroll_y + 2;
            {
                HPEN pen;
                HGDIOBJ old_pen;
                HGDIOBJ old_brush;

                pen = CreatePen(PS_SOLID, 1, RGB(0, 64, 128));
                if (pen != NULL) {
                    old_pen = SelectObject(dc, pen);
                    old_brush = SelectObject(dc,
                            GetStockObject(HOLLOW_BRUSH));
                    Rectangle(dc, focus_rect.left, focus_rect.top,
                            focus_rect.right, focus_rect.bottom);
                    SelectObject(dc, old_brush);
                    SelectObject(dc, old_pen);
                    DeleteObject(pen);
                }
            }
        }
    }
    RestoreDC(dc, saved);
}

static LRESULT CALLBACK app_page_window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    if (message == WM_COMMAND && g_controls != NULL &&
            AppControls_HandleCommand(g_controls, wparam, lparam)) {
        return 0;
    }
    switch (message) {
    case WM_SIZE:
        {
            RECT client;

            GetClientRect(hwnd, &client);
            g_page_width = client.right - client.left;
            g_page_height = client.bottom - client.top;
            if (g_page_width < 1) {
                g_page_width = 1;
            }
            if (g_page_height < 1) {
                g_page_height = 1;
            }
            if (g_document != NULL && app_relayout() != 0) {
                app_set_status(APP_TEXT_STATUS_LAYOUT);
            }
            if (g_controls != NULL && g_document != NULL) {
                AppControls_Reposition(g_controls, g_document, g_scroll_x,
                        g_scroll_y);
            }
            if (g_script != NULL) {
                (void) AppScript_NotifyResize(g_script, g_page_width,
                        g_page_height, g_dpi);
            }
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC dc;

            dc = BeginPaint(hwnd, &paint);
            app_paint_page(hwnd, dc);
            EndPaint(hwnd, &paint);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KILLFOCUS:
        AppControls_ClearButtonFocus(g_controls);
        break;
    case WM_LBUTTONDOWN:
        {
            int x;
            int y;
            int document_x;
            int document_y;
            int focus_index;
            int default_allowed;
            unsigned int file_index;
            int file_disabled;
            char href[APP_URL_MAX];
            char link_target[APP_SCRIPT_TARGET_MAX];
            char link_rel[APP_SCRIPT_REL_MAX];
            int link_found;
            int navigated;

            x = (int) (short) LOWORD(lparam);
            y = (int) (short) HIWORD(lparam);
            SetFocus(hwnd);
            document_x = x + g_scroll_x;
            document_y = y + g_scroll_y;
            if (AppControls_HandleButtonPointer(g_controls, document_x,
                    document_y)) {
                return 0;
            }
            file_index = 0;
            file_disabled = 0;
            if (g_document != NULL && app_file_input_at(g_document,
                    document_x, document_y, &file_index, &file_disabled)) {
                if (file_disabled) {
                    return 0;
                }
                default_allowed = 1;
                if (g_script != NULL) {
                    if (AppScript_DispatchClickEvent(g_script, document_x,
                            document_y, &default_allowed) != 0) {
                        return 0;
                    }
                } else {
                    (void) PCore_EventDispatchAt(g_document, document_x,
                            document_y, "click", 1, 1, &default_allowed);
                }
                if (default_allowed && !g_file_picker_pending) {
                    (void) app_file_input_open(hwnd, g_document, g_script,
                            file_index, document_x, document_y, 0);
                }
                return 0;
            }
            if (g_document != NULL && PCore_DisclosureInfoAt(g_document,
                    document_x, document_y, NULL, NULL, NULL, NULL,
                    NULL) == 1) {
                (void) app_handle_disclosure(hwnd, document_x, document_y);
                return 0;
            }
            if (g_document != NULL && app_handle_label(hwnd, document_x,
                    document_y)) {
                return 0;
            }
            href[0] = '\0';
            link_target[0] = '\0';
            link_rel[0] = '\0';
            link_found = g_document != NULL && PCore_LinkAtEx(g_document,
                    document_x, document_y, href, sizeof(href),
                    link_target, sizeof(link_target), link_rel,
                    sizeof(link_rel)) == 0;
            if (link_found && g_script != NULL) {
                navigated = 0;
                (void) AppScript_DispatchAnchorClick(g_script, document_x,
                        document_y, href, link_target, link_rel,
                        &navigated);
                return 0;
            }
            AppControls_ClearButtonFocus(g_controls);
            focus_index = app_focus_at(document_x, document_y);
            if (focus_index >= 0) {
                (void) app_focus_set(hwnd, focus_index);
            }
            default_allowed = 1;
            if (g_document != NULL) {
                if (g_script != NULL) {
                    if (AppScript_DispatchClickEvent(g_script, document_x,
                            document_y, &default_allowed) != 0) {
                        return 0;
                    }
                } else {
                    (void) PCore_EventDispatchAt(g_document, document_x,
                            document_y, "click", 1, 1, &default_allowed);
                }
            }
            if (default_allowed && link_found) {
                (void) app_load_page(g_window, href, APP_HISTORY_NEW, -1);
            }
        }
        return 0;
    case WM_VSCROLL:
        {
            int amount;

            amount = 16;
            if (LOWORD(wparam) == SB_PAGEUP ||
                    LOWORD(wparam) == SB_PAGEDOWN) {
                amount = g_page_height;
            }
            if (LOWORD(wparam) == SB_LINEUP ||
                    LOWORD(wparam) == SB_PAGEUP) {
                amount = -amount;
            }
            if (LOWORD(wparam) == SB_THUMBPOSITION ||
                    LOWORD(wparam) == SB_THUMBTRACK) {
                app_scroll_by(hwnd, 0,
                        (int) HIWORD(wparam) - g_scroll_y);
            } else {
                app_scroll_by(hwnd, 0, amount);
            }
        }
        return 0;
    case WM_HSCROLL:
        {
            int amount;

            amount = 16;
            if (LOWORD(wparam) == SB_PAGELEFT ||
                    LOWORD(wparam) == SB_PAGERIGHT) {
                amount = g_page_width;
            }
            if (LOWORD(wparam) == SB_LINELEFT ||
                    LOWORD(wparam) == SB_PAGELEFT) {
                amount = -amount;
            }
            if (LOWORD(wparam) == SB_THUMBPOSITION ||
                    LOWORD(wparam) == SB_THUMBTRACK) {
                app_scroll_by(hwnd,
                        (int) HIWORD(wparam) - g_scroll_x, 0);
            } else {
                app_scroll_by(hwnd, amount, 0);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (GetFocus() != hwnd) {
            break;
        }
        if (AppControls_HandleButtonKey(g_controls, WM_KEYDOWN, wparam,
                lparam)) {
            return 0;
        }
        if (wparam == VK_UP) {
            if (g_focus_index >= 0 && g_focus_count > 0) {
                app_focus_move(hwnd, -1);
            } else {
                app_scroll_by(hwnd, 0, -16);
            }
            return 0;
        }
        if (wparam == VK_DOWN) {
            if (g_focus_count > 0) {
                app_focus_move(hwnd, 1);
            } else {
                app_scroll_by(hwnd, 0, 16);
            }
            return 0;
        }
        if (wparam == VK_LEFT) {
            app_scroll_by(hwnd, -16, 0);
            return 0;
        }
        if (wparam == VK_RIGHT) {
            app_scroll_by(hwnd, 16, 0);
            return 0;
        }
        if (wparam == VK_PRIOR) {
            app_scroll_by(hwnd, 0, -g_page_height);
            return 0;
        }
        if (wparam == VK_NEXT) {
            app_scroll_by(hwnd, 0, g_page_height);
            return 0;
        }
        if (wparam == VK_RETURN) {
            (void) app_activate_focus(hwnd);
            return 0;
        }
        if (wparam == VK_F5) {
            app_refresh(g_window);
            return 0;
        }
        if (wparam == VK_TAB) {
            SetFocus(g_address);
            return 0;
        }
        return 0;
    case WM_KEYUP:
        if (GetFocus() == hwnd && AppControls_HandleButtonKey(g_controls,
                WM_KEYUP, wparam, lparam)) {
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK app_window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        memset(&g_shell_activate, 0, sizeof(g_shell_activate));
        g_shell_activate.cbSize = sizeof(g_shell_activate);
        if (app_create_menu_bar(hwnd) != 0) {
            return -1;
        }
        SetTimer(hwnd, APP_SCRIPT_TIMER_ID, 250, NULL);
        return 0;
    case WM_SIZE:
        app_reposition_controls(hwnd);
        app_reposition_page(hwnd);
        return 0;
    case WM_ACTIVATE:
        SHHandleWMActivate(hwnd, wparam, lparam, &g_shell_activate, FALSE);
        if (g_script != NULL) {
            (void) AppScript_SetFocus(g_script,
                    LOWORD(wparam) != WA_INACTIVE ? 1 : 0);
        }
        return 0;
    case WM_SHOWWINDOW:
        if (g_script != NULL) {
            (void) AppScript_SetVisibility(g_script, wparam ? 0 : 1);
        }
        return 0;
    case WM_SETTINGCHANGE:
        SHHandleWMSettingChange(hwnd, wparam, lparam, &g_shell_activate);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case APP_CMD_BACK:
            app_go_back(hwnd);
            return 0;
        case APP_CMD_FORWARD:
            app_go_forward(hwnd);
            return 0;
        case APP_CMD_HOME:
            app_go_home(hwnd);
            return 0;
        case APP_CMD_ADDRESS:
            SetFocus(g_address);
            SendMessage(g_address, EM_SETSEL, 0, -1);
            return 0;
        case APP_CMD_REFRESH:
            app_refresh(hwnd);
            return 0;
        case APP_CMD_EXIT:
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case APP_WM_ADDRESS_GO:
        if (lparam != 0) {
            (void) app_load_page(hwnd, (const char *) lparam,
                    APP_HISTORY_NEW, -1);
        } else {
            app_go_from_address(hwnd);
        }
        return 0;
    case APP_WM_ADDRESS_CANCEL:
        app_set_address(g_current_url);
        app_set_status(APP_TEXT_STATUS_ADDRESS_CANCELLED);
        SetFocus((g_page_window != NULL) ? g_page_window : hwnd);
        return 0;
    case APP_WM_NAV_DONE:
        app_navigation_handle_done(hwnd,
                (AppNavigationRequest *) lparam);
        return 0;
    case APP_WM_SCRIPT_NAVIGATE:
        app_handle_script_navigation(hwnd, (AppScriptContext *) lparam);
        return 0;
    case APP_WM_FILE_PICKER:
        (void) app_file_picker_process(hwnd);
        return 0;
    case APP_WM_CONTROLS_REFRESH:
        if (lparam == (LPARAM) g_script && g_document != NULL &&
                g_controls != NULL) {
            AppControls_PrepareReconcile(g_controls);
            if (app_relayout() != 0 ||
                    ((wparam == APP_CONTROLS_REFRESH_FORM_RESET) ?
                    AppControls_ReconcileAfterFormReset(g_controls,
                    g_document, g_script, g_scroll_x, g_scroll_y) :
                    AppControls_Reconcile(g_controls, g_document,
                    g_script, g_scroll_x, g_scroll_y)) != 0) {
                app_set_status(APP_TEXT_STATUS_LAYOUT);
            } else {
                InvalidateRect(g_page_window, NULL, TRUE);
            }
        }
        return 0;
    case WM_TIMER:
        if (wparam == APP_SCRIPT_TIMER_ID && g_script != NULL) {
            (void) AppScript_RunTaskCheckpoint(g_script, GetTickCount());
        }
        return 0;
    case WM_CLOSE:
        if (g_navigation_request != NULL || g_retired_navigation != NULL) {
            g_navigation_closing = 1;
            app_navigation_cancel_all();
            if (g_retired_navigation != NULL) {
                EnableWindow(hwnd, FALSE);
                return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        /* Navigation has already drained before WM_DESTROY.  The private
         * host context is the single owner of the remaining page, history,
         * command bar and DLL shutdown sequence. */
        KillTimer(hwnd, APP_SCRIPT_TIMER_ID);
        app_file_picker_cancel_pending();
        if (g_controls != NULL) {
            AppControls_Destroy(g_controls);
            g_controls = NULL;
        }
        AppHostContext_Shutdown(&g_app);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

static int app_create_controls(HWND hwnd)
{
    HWND address;
    WNDPROC original_proc;

    address = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
            ES_LEFT | ES_AUTOHSCROLL,
            0, 0, 1, 1, hwnd, (HMENU) APP_ID_ADDRESS,
            g_instance, NULL);
    if (address == NULL) {
        return 1;
    }
    original_proc = (WNDPROC) SetWindowLong(address,
            GWL_WNDPROC, (LONG) app_address_proc);
    AppHostContext_SetAddress(&g_app, address, original_proc);
    return (original_proc == NULL) ? 1 : 0;
}

static int app_create_page_window(HWND hwnd)
{
    HWND page_window;

    /* Match the WM6 SDK PViewCE pattern: the parent owns the command bar,
     * while the same top-level window's content child owns native scrolling. */
    page_window = CreateWindowExW(0, APP_PAGE_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL,
            0, 0, 1, 1, hwnd, NULL, g_instance, NULL);
    AppHostContext_SetPageWindow(&g_app, page_window);
    return (page_window == NULL) ? 1 : 0;
}

static int app_register_page_class(void)
{
    WNDCLASSW page_class;

    memset(&page_class, 0, sizeof(page_class));
    page_class.style = CS_HREDRAW | CS_VREDRAW;
    page_class.lpfnWndProc = app_page_window_proc;
    page_class.hInstance = g_instance;
    page_class.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    page_class.lpszClassName = APP_PAGE_CLASS_NAME;
    return (RegisterClassW(&page_class) == 0) ? 1 : 0;
}

static void app_show_error(AppTextId text_id)
{
    WCHAR text[APP_WIDE_TEXT_MAX];

    if (AppI18n_LoadString(text_id, text,
            sizeof(text) / sizeof(text[0])) <= 0) {
        lstrcpyW(text, L"Positron");
    }
    MessageBoxW(NULL, text, L"Positron", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPWSTR command_line, int show_command)
{
    WNDCLASSW window_class;
    HWND hwnd;
    MSG message;
    int result;

    (void) previous;
    (void) command_line;
    AppHostContext_Init(&g_app);
    AppHostContext_SetInstance(&g_app, instance);
    if (AppI18n_Init(g_instance) != 0) {
        MessageBoxW(NULL, L"Positron", L"Positron",
                MB_OK | MB_ICONERROR);
        return 1;
    }
    g_dpi = app_device_dpi();
    if (PCore_Init() != 0) {
        app_show_error(APP_TEXT_ERROR_CORE_INIT);
        return 1;
    }
    AppHostContext_SetCoreInitialized(&g_app, 1);
    AppHostContext_SetHistory(&g_app, PBrowser_HistoryCreate());
    if (g_history == NULL) {
        AppHostContext_Shutdown(&g_app);
        app_show_error(APP_TEXT_ERROR_HISTORY_INIT);
        return 1;
    }
    AppHostContext_SetHttpInitialized(&g_app, PHttp_Init() ? 1 : 0);
    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = app_window_proc;
    window_class.hInstance = g_instance;
    window_class.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    window_class.lpszClassName = L"PositronBrowserWindow";
    if (app_register_page_class() != 0) {
        AppHostContext_Shutdown(&g_app);
        app_show_error(APP_TEXT_ERROR_CLASS_REGISTER);
        return 1;
    }
    if (RegisterClassW(&window_class) == 0) {
        AppHostContext_Shutdown(&g_app);
        app_show_error(APP_TEXT_ERROR_CLASS_REGISTER);
        return 1;
    }
    hwnd = CreateWindowExW(WS_EX_CONTROLPARENT,
            L"PositronBrowserWindow", L"Positron",
            WS_OVERLAPPED | WS_SYSMENU | WS_CLIPCHILDREN |
            WS_CLIPSIBLINGS,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL,
            g_instance, NULL);
    if (hwnd == NULL) {
        AppHostContext_Shutdown(&g_app);
        app_show_error(APP_TEXT_ERROR_WINDOW_CREATE);
        return 1;
    }
    AppHostContext_SetWindow(&g_app, hwnd);
    if (app_create_controls(hwnd) != 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    if (app_create_page_window(hwnd) != 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    g_controls = AppControls_Create(g_page_window, g_instance, &g_app,
            app_controls_changed, app_handle_form_submit,
            app_handle_form_enter);
    if (g_controls == NULL) {
        DestroyWindow(hwnd);
        return 1;
    }
    app_reposition_controls(hwnd);
    app_reposition_page(hwnd);
    result = app_load_page(hwnd, APP_URL_WELCOME,
            APP_HISTORY_NEW, -1) ? 0 : 1;
    if (result != 0) {
        DestroyWindow(hwnd);
        return result;
    }
    ShowWindow(hwnd, show_command == 0 ? SW_SHOW : show_command);
    UpdateWindow(hwnd);
    SetFocus(g_address);
    SendMessage(g_address, EM_SETSEL, 0, -1);
    while (GetMessage(&message, NULL, 0, 0) > 0) {
        if ((g_menu_bar == NULL ||
                !IsCommandBarMessage(g_menu_bar, &message)) &&
                !IsDialogMessage(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }
    return (int) message.wParam;
}
