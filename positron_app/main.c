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

#include "app_debug.h"
#include "app_host.h"
#include "app_controls.h"
#include "app_script.h"
#include "app_forms.h"
#include "app_resources.h"
#include "app_url_router.h"
#include "app_internal_pages.h"
#include "app_i18n.h"
#include "positron_core.h"
#include "positron_browser.h"
#include "positron_script.h"
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

#define APP_ADDRESS_PADDING      2
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
#define APP_COMMAND_ARG_MAX     16384
#define APP_STARTUP_SCRIPT_MAX_BYTES 8192
#define APP_STARTUP_SELECTOR_MAX_BYTES 512

#define APP_NAV_MAX_RETIRED     4
#define APP_NAV_WORK_DOCUMENT   1
#define APP_NAV_WORK_RESOURCES  2
#define APP_NAV_COMMIT_NONE     0
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
static int g_overflow_pointer;
static HDC g_page_paint_buffer_dc;
static HBITMAP g_page_paint_buffer_bitmap;
static int g_page_paint_buffer_width;
static int g_page_paint_buffer_height;
static int g_scrollbar_settle_depth;
static int g_script_refresh_pending;
static int g_script_refresh_form_reset;
static AppScriptContext *g_script_refresh_context;
static char g_startup_reference[APP_URL_MAX];
static char g_startup_script[APP_STARTUP_SCRIPT_MAX_BYTES + 1];
static char g_startup_click_selector[APP_STARTUP_SELECTOR_MAX_BYTES + 1];
static int g_startup_script_armed;
static int g_startup_click_armed;
static unsigned long g_startup_script_generation;

static int app_relayout(void);
static int app_load_page(HWND hwnd, const char *url, int history_mode,
        int history_target);
static int app_load_page_from(HWND hwnd, const char *url, int history_mode,
        int history_target, AppNavigationSource source);
static void app_page_paint_buffer_release(void);
static int app_load_form_request(HWND hwnd, const AppFormRequest *request);
static void app_update_history_buttons(void);
static void app_controls_changed(void *pw);
static int app_startup_click_form_selector(const char *selector);
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
static int app_navigation_cancel_active(void);
static int app_history_bound(void);
static void app_history_save_scroll(void);
static int app_history_traverse(HWND hwnd, int target_index);
static int app_navigate_fragment(HWND hwnd, const char *url, int replace);

static const char g_app_css[] =
        "body{margin:12px;font-family:sans-serif;font-size:14px;"
        "color:#202020;background:#ffffff}"
        "h1{font-size:22px;color:#173b64;margin:0 0 8px 0}"
        "h2{font-size:17px;color:#28527a;margin:14px 0 6px 0}"
        "p{margin:0 0 8px 0}"
        "a{color:#0000ee;text-decoration:underline}"
        "a:focus{color:#000080;background-color:#cce8ff}"
        "a:hover{color:#800000}"
        "body.internal a{display:block}";

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
    if (g_page_kind != 0) {
        return APP_TEXT_STATUS_READY_INTERNAL;
    }
    return APP_TEXT_STATUS_READY_REMOTE;
}

static void app_restore_page_status(void)
{
    if (g_document != NULL) {
        char title[APP_HOST_TITLE_MAX];
        WCHAR wide[APP_HOST_TITLE_MAX];

        if (g_page_kind > APP_PAGE_CONTROLS &&
                PCore_DocumentTitle(g_document, title, sizeof(title), NULL) == 0 &&
                title[0] != '\0') {
            app_utf8_to_wide(title, wide, APP_HOST_TITLE_MAX);
            SetWindowTextW(g_window, wide);
        } else {
            app_set_status(app_ready_status());
        }
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

/* Capture the UI belonging to the last committed page.  A second navigation
 * can begin while the first candidate is still loading, so reading the
 * visible controls directly would capture "Loading" and the first pending
 * URL.  In that case inherit the first request's snapshot instead. */
static void app_navigation_capture_ui(AppNavigationRequest *request)
{
    AppNavigationRequest *active;
    int length;

    if (request == NULL) {
        return;
    }
    request->ui_snapshot_valid = 0;
    active = g_navigation_request;
    if (active != NULL && active->ui_snapshot_valid) {
        app_copy_text(request->committed_url,
                sizeof(request->committed_url), active->committed_url);
        memcpy(request->committed_caption, active->committed_caption,
                sizeof(request->committed_caption));
        request->ui_snapshot_valid = 1;
        return;
    }
    app_copy_text(request->committed_url,
            sizeof(request->committed_url), g_current_url);
    memset(request->committed_caption, 0,
            sizeof(request->committed_caption));
    if (g_window != NULL) {
        length = GetWindowTextW(g_window, request->committed_caption,
                APP_HOST_TITLE_MAX);
        if (length < 0) {
            request->committed_caption[0] = L'\0';
        }
    }
    request->committed_caption[APP_HOST_TITLE_MAX - 1] = L'\0';
    request->ui_snapshot_valid = 1;
}

static void app_navigation_restore_ui(AppNavigationRequest *request)
{
    if (request == NULL || !request->ui_snapshot_valid) {
        return;
    }
    app_set_address(request->committed_url);
    if (g_window != NULL) {
        SetWindowTextW(g_window, request->committed_caption);
    }
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

static int app_address_font_height(HWND address)
{
    HDC dc;
    HFONT font;
    HFONT old_font;
    TEXTMETRIC metrics;
    int height;

    if (address == NULL) {
        return 0;
    }
    font = (HFONT) SendMessage(address, WM_GETFONT, 0, 0);
    if (font == NULL) {
        font = (HFONT) GetStockObject(SYSTEM_FONT);
    }
    dc = GetDC(address);
    if (dc == NULL) {
        return 0;
    }
    old_font = (HFONT) SelectObject(dc, font);
    memset(&metrics, 0, sizeof(metrics));
    if (!GetTextMetrics(dc, &metrics)) {
        SelectObject(dc, old_font);
        ReleaseDC(address, dc);
        return 0;
    }
    height = metrics.tmHeight;
    SelectObject(dc, old_font);
    ReleaseDC(address, dc);
    return height;
}

static int app_address_outer_height(HWND address, int client_height)
{
    RECT rect;
    LONG style;
    LONG ex_style;
    int outer_height;

    if (client_height < 1) {
        client_height = 1;
    }
    memset(&rect, 0, sizeof(rect));
    rect.right = 1;
    rect.bottom = client_height;
    style = WS_CHILD | WS_BORDER;
    ex_style = 0;
    if (address != NULL) {
        style = GetWindowLong(address, GWL_STYLE);
        ex_style = GetWindowLong(address, GWL_EXSTYLE);
        if (style == 0) {
            style = WS_CHILD | WS_BORDER;
        }
    }
    if (!AdjustWindowRectEx(&rect, (DWORD) style, FALSE,
            (DWORD) ex_style)) {
        return client_height;
    }
    outer_height = rect.bottom - rect.top;
    if (outer_height < client_height) {
        outer_height = client_height;
    }
    return outer_height;
}

static int app_address_height(void)
{
    int font_height;
    int padding;
    int client_height;

    font_height = app_address_font_height(g_address);
    if (font_height < 1) {
        /* This is only a pre-control/failure fallback.  The normal path uses
         * the physical height reported for the EDIT's actual font. */
        font_height = app_scale_dpi(16);
    }
    padding = app_scale_dpi(APP_ADDRESS_PADDING);
    /* TEXTMETRIC describes the line inside the EDIT client area.  Convert
     * that client size to the actual outer child-window size so WS_BORDER
     * does not consume the bottom of the native line box.  The EDIT keeps
     * its own formatting rectangle and therefore performs its normal
     * vertical centering; the host never shifts the text. */
    client_height = font_height + (padding * 2);
    return app_address_outer_height(g_address, client_height);
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

    GetClientRect(hwnd, &client);
    address_height = app_address_height();
    width = client.right - client.left;
    if (width < 1) {
        width = 1;
    }
    if (g_address != NULL) {
        if (address_height < 1) {
            address_height = 1;
        }
        /* The address EDIT is a child of the same top-level window as the
         * command bar and page.  Keep its outer rectangle flush with the
         * client edge; WS_BORDER already supplies the control's own border.
         * An EXE-side inset leaves stale pixels behind after rotation and
         * makes the page origin disagree with the address control's bottom. */
        MoveWindow(g_address, 0, 0, width, address_height, TRUE);
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

/* Configure native page scrollbars from the current document extent.  A
 * scrollbar consumes client space, so adding one can make the other one
 * necessary; settle both styles before publishing the final page size.  This
 * follows the reference host's WM6 path and keeps an incidental small width
 * overrun from becoming a permanent horizontal scrollbar. */
static int app_update_scrollbars(HWND hwnd)
{
    SCROLLINFO info;
    RECT client;
    int client_width;
    int client_height;
    int needed_horz;
    int needed_vert;
    int pass;
    int size_changed;
    LONG style;
    LONG next_style;

    if (hwnd == NULL) {
        return 0;
    }
    size_changed = 0;
    g_scrollbar_settle_depth++;
    for (pass = 0; pass < 3; pass++) {
        if (!GetClientRect(hwnd, &client)) {
            break;
        }
        client_width = client.right - client.left;
        client_height = client.bottom - client.top;
        needed_horz = g_document_width > client_width;
        needed_vert = g_document_height > client_height;
        style = GetWindowLong(hwnd, GWL_STYLE);
        next_style = style;
        if (needed_horz) {
            next_style |= WS_HSCROLL;
        } else {
            next_style &= ~WS_HSCROLL;
        }
        if (needed_vert) {
            next_style |= WS_VSCROLL;
        } else {
            next_style &= ~WS_VSCROLL;
        }
        if (next_style == style) {
            break;
        }
        SetWindowLong(hwnd, GWL_STYLE, next_style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (!GetClientRect(hwnd, &client)) {
        g_scrollbar_settle_depth--;
        return 0;
    }
    client_width = client.right - client.left;
    client_height = client.bottom - client.top;
    if (client_width < 1) {
        client_width = 1;
    }
    if (client_height < 1) {
        client_height = 1;
    }
    if (g_page_width != client_width || g_page_height != client_height) {
        size_changed = 1;
    }
    g_page_width = client_width;
    g_page_height = client_height;
    app_clamp_scroll();
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = (g_document_width > 0) ? g_document_width - 1 : 0;
    info.nPage = (UINT) client_width;
    info.nPos = g_scroll_x;
    SetScrollInfo(hwnd, SB_HORZ, &info, TRUE);
    info.nMax = (g_document_height > 0) ? g_document_height - 1 : 0;
    info.nPage = (UINT) client_height;
    info.nPos = g_scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
    g_scrollbar_settle_depth--;
    return size_changed;
}

/* Keep the scrollbar range/page stable during a drag or keyboard scroll.
 * Reapplying the full SCROLLINFO causes the native scrollbar and its parent
 * to redraw on every thumb-track message.  The range is owned by layout;
 * scrolling only changes the current positions. */
static void app_set_scrollbar_positions(HWND hwnd)
{
    if (hwnd == NULL) {
        return;
    }
    SetScrollPos(hwnd, SB_HORZ, g_scroll_x, TRUE);
    SetScrollPos(hwnd, SB_VERT, g_scroll_y, TRUE);
}

/* Move an already laid-out page without changing its document geometry.
 * This mirrors the test_host viewport path: shift retained pixels, invalidate
 * only the newly exposed strip, then synchronously paint that strip.  Layout
 * and native-control reconciliation remain on the resize/content paths. */
static int app_scroll_to_position(HWND hwnd, int target_x, int target_y)
{
    RECT client;
    RECT scroll_rect;
    int old_x;
    int old_y;
    int applied_x;
    int applied_y;

    if (hwnd == NULL || !IsWindow(hwnd) ||
            !GetClientRect(hwnd, &client)) {
        return 0;
    }
    old_x = g_scroll_x;
    old_y = g_scroll_y;
    g_scroll_x = target_x;
    g_scroll_y = target_y;
    app_clamp_scroll();
    applied_x = g_scroll_x - old_x;
    applied_y = g_scroll_y - old_y;
    if (applied_x == 0 && applied_y == 0) {
        return 0;
    }
    app_set_scrollbar_positions(hwnd);
    if (g_controls != NULL) {
        AppControls_RepositionForScroll(g_controls, g_document, g_scroll_x,
                g_scroll_y);
    }
    scroll_rect = client;
    ScrollWindowEx(hwnd, -applied_x, -applied_y, &scroll_rect, &scroll_rect,
            NULL, NULL, SW_INVALIDATE);
    UpdateWindow(hwnd);
    return 1;
}

static void app_scroll_by(HWND hwnd, int dx, int dy)
{
    if (app_scroll_to_position(hwnd, g_scroll_x + dx,
            g_scroll_y + dy)) {
        if (g_script != NULL) {
            (void) AppScript_NotifyScroll(g_script,
                    MulDiv(g_scroll_x, 96, g_dpi > 0 ? g_dpi : 96),
                    MulDiv(g_scroll_y, 96, g_dpi > 0 ? g_dpi : 96));
        }
    }
}

/* These are platform viewport adapters. Browser owns entry identity/state;
 * Core owns fragment lookup. Never infer same-document identity from a URL. */
static int app_history_bound(void)
{
    return g_document != NULL && g_app.history_document_id != 0 &&
            g_app.history_document_id == PBrowser_HistoryEntryDocumentId(
            g_history, PBrowser_HistoryIndex(g_history));
}

static void app_history_save_scroll(void)
{
    if (app_history_bound()) {
        (void) PBrowser_HistorySetEntryScroll(g_history,
                PBrowser_HistoryIndex(g_history), g_scroll_x, g_scroll_y);
    }
}

static void app_history_bind_page(void)
{
    g_app.history_document_id = PBrowser_HistoryEntryDocumentId(g_history,
            PBrowser_HistoryIndex(g_history));
}

static void app_history_notify_scroll(void)
{
    if (g_script != NULL) {
        (void) AppScript_NotifyScroll(g_script,
                MulDiv(g_scroll_x, 96, g_dpi > 0 ? g_dpi : 96),
                MulDiv(g_scroll_y, 96, g_dpi > 0 ? g_dpi : 96));
    }
}

static int app_scroll_to_fragment(const char *url)
{
    const char *fragment;
    int x;
    int y;

    fragment = url != NULL ? strchr(url, '#') : NULL;
    if (g_document == NULL || fragment == NULL) {
        return 0;
    }
    if (strchr(fragment + 1, '%') != NULL) {
        return 0;
    }
    x = 0;
    y = 0;
    /* The Core API accepts decoded UTF-8 tokens, not URL percent decoding.
     * Do not add a second URL decoder to the application. */
    if (fragment[1] != '\0' &&
            PCore_FragmentInfoByToken(g_document, fragment + 1, &x, &y,
            NULL, NULL) != 0) {
        return 0;
    }
    (void) app_scroll_to_position(g_page_window, app_scale_dpi(x),
            app_scale_dpi(y));
    app_history_notify_scroll();
    return 1;
}

static void app_history_restore_scroll(int index)
{
    int mode;
    int x;
    int y;

    if (AppScript_GetScrollRestoration(g_script, &mode) != 0 ||
            mode == PBROWSER_SCROLL_RESTORATION_MANUAL) {
        return;
    }
    x = 0;
    y = 0;
    (void) PBrowser_HistoryEntryScroll(g_history, index, &x, &y);
    (void) app_scroll_to_position(g_page_window, x, y);
    app_history_notify_scroll();
}

/* Core owns nested overflow geometry and scrollbar state.  The EXE only
 * translates the retained dirty rectangle into the page child window so a
 * table drag does not force a full page repaint. */
static void app_invalidate_overflow(HWND hwnd)
{
    RECT client;
    RECT dirty;
    int x;
    int y;
    int w;
    int h;

    if (g_document == NULL || !PCore_OverflowDirtyRect(g_document,
            &x, &y, &w, &h)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    GetClientRect(hwnd, &client);
    dirty.left = x - g_scroll_x - 1;
    dirty.top = y - g_scroll_y - 1;
    dirty.right = x + w - g_scroll_x + 1;
    dirty.bottom = y + h - g_scroll_y + 1;
    if (dirty.left < client.left) {
        dirty.left = client.left;
    }
    if (dirty.top < client.top) {
        dirty.top = client.top;
    }
    if (dirty.right > client.right) {
        dirty.right = client.right;
    }
    if (dirty.bottom > client.bottom) {
        dirty.bottom = client.bottom;
    }
    if (dirty.left < dirty.right && dirty.top < dirty.bottom) {
        InvalidateRect(hwnd, &dirty, FALSE);
    }
}

/* Keep Browser's element scroll state in step with Core when a retained
 * overflow scrollbar is moved by native pointer input. */
static void app_sync_overflow_scroll(void)
{
    char element_id[256];
    int element_bytes;
    int scroll_x;
    int scroll_y;

    if (g_document == NULL || g_script == NULL) {
        return;
    }
    memset(element_id, 0, sizeof(element_id));
    element_bytes = 0;
    scroll_x = 0;
    scroll_y = 0;
    if (PCore_OverflowScrollSnapshot(g_document, element_id,
            sizeof(element_id), &element_bytes, &scroll_x, &scroll_y) != 0 ||
            element_bytes <= 0 || element_bytes >= (int) sizeof(element_id) ||
            element_id[0] == '\0') {
        return;
    }
    (void) AppScript_NotifyElementScroll(g_script, element_id,
            scroll_x, scroll_y);
}

#ifdef _DEBUG
static void app_script_debug_refresh(const char *event, int result,
        int form_reset)
{
    char message[256];

    _snprintf(message, sizeof(message) - 1,
            "positron script-refresh event=%s result=%d form_reset=%d\r\n",
            event != NULL ? event : "unknown", result,
            form_reset ? 1 : 0);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}

static void app_debug_pointer_down(int x, int y, int document_x,
        int document_y, int scroll_x, int scroll_y)
{
    char message[224];

    _snprintf(message, sizeof(message) - 1,
            "positron pointer-down x=%d y=%d document_x=%d document_y=%d "
            "scroll_x=%d scroll_y=%d\r\n", x, y, document_x, document_y,
            scroll_x, scroll_y);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}
#endif

static void app_set_focus_ids(int page_kind)
{
    g_focus_count = 0;
    g_focus_index = -1;
    g_focus_id[0] = '\0';
    if (page_kind > APP_PAGE_CONTROLS) {
        int i;

        g_focus_count = AppInternalPages_FocusIds(g_document,
                g_app.internal_focus_ids);
        for (i = 0; i < g_focus_count; i++) {
            g_focus_ids[i] = g_app.internal_focus_ids[i];
        }
    } else if (page_kind == APP_PAGE_WELCOME) {
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
    if (g_script_refresh_pending && g_script_refresh_context == context) {
        if (form_reset) {
            g_script_refresh_form_reset = 1;
        }
#ifdef _DEBUG
        app_script_debug_refresh("coalesced", 0, g_script_refresh_form_reset);
#endif
        return;
    }
    g_script_refresh_pending = 1;
    g_script_refresh_form_reset = form_reset ? 1 : 0;
    g_script_refresh_context = context;
    if (host->window == NULL || !PostMessage(host->window,
            APP_WM_CONTROLS_REFRESH,
            g_script_refresh_form_reset ? APP_CONTROLS_REFRESH_FORM_RESET : 0,
            (LPARAM) context)) {
#ifdef _DEBUG
        app_script_debug_refresh("post-failed", 0, form_reset);
#endif
        g_script_refresh_pending = 0;
        g_script_refresh_form_reset = 0;
        g_script_refresh_context = NULL;
    } else {
#ifdef _DEBUG
        app_script_debug_refresh("posted", 0, form_reset);
#endif
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
    (void) app_scroll_to_position(host->page_window, device_x, device_y);
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

static int app_wide_buffer_to_utf8(const WCHAR *source, int source_chars,
        char *target, int target_capacity)
{
    int bytes;

    if (source == NULL || source_chars < 0 || target == NULL ||
            target_capacity <= 1) {
        return 1;
    }
    target[0] = '\0';
    if (source_chars == 0) {
        return 0;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0, source, source_chars,
            target, target_capacity - 1, NULL, NULL);
    if (bytes <= 0) {
        bytes = WideCharToMultiByte(CP_ACP, 0, source, source_chars,
                target, target_capacity - 1, NULL, NULL);
    }
    if (bytes <= 0 || bytes >= target_capacity) {
        target[0] = '\0';
        return 1;
    }
    target[bytes] = '\0';
    return 0;
}

static int app_command_space(WCHAR value)
{
    return value == L' ' || value == L'\t' || value == L'\r' ||
            value == L'\n' || value == L'\f' || value == L'\v';
}

/* Read one bounded Windows command-line argument without depending on
 * CommandLineToArgvW, which is not present on every WM6 image.  Backslash-
 * quote is accepted so a JavaScript string can be passed inside a quoted
 * --eval value. */
static int app_command_line_next_arg(const WCHAR **cursor, WCHAR *output,
        int output_capacity)
{
    const WCHAR *current;
    int length;
    int quoted;

    if (cursor == NULL || *cursor == NULL || output == NULL ||
            output_capacity <= 1) {
        return -1;
    }
    current = *cursor;
    while (app_command_space(*current)) {
        current++;
    }
    if (*current == L'\0') {
        *cursor = current;
        output[0] = L'\0';
        return 0;
    }
    length = 0;
    quoted = 0;
    while (*current != L'\0') {
        if (*current == L'\\' && current[1] == L'"') {
            if (length + 1 >= output_capacity) {
                return -1;
            }
            output[length++] = L'"';
            current += 2;
            continue;
        }
        if (*current == L'"') {
            quoted = !quoted;
            current++;
            continue;
        }
        if (!quoted && app_command_space(*current)) {
            break;
        }
        if (length + 1 >= output_capacity) {
            return -1;
        }
        output[length++] = *current++;
    }
    if (quoted) {
        return -1;
    }
    output[length] = L'\0';
    while (app_command_space(*current)) {
        current++;
    }
    *cursor = current;
    return 1;
}

static int app_command_ascii_equal(const WCHAR *value, const char *ascii)
{
    int index;
    WCHAR wide;
    char character;

    if (value == NULL || ascii == NULL) {
        return 0;
    }
    index = 0;
    while (ascii[index] != '\0') {
        wide = value[index];
        character = ascii[index];
        if (wide >= L'A' && wide <= L'Z') {
            wide = (WCHAR) (wide + (L'a' - L'A'));
        }
        if (character >= 'A' && character <= 'Z') {
            character = (char) (character + ('a' - 'A'));
        }
        if (wide != (WCHAR) (unsigned char) character) {
            return 0;
        }
        index++;
    }
    return value[index] == L'\0';
}

static int app_command_ascii_prefix(const WCHAR *value, const char *ascii)
{
    int index;
    WCHAR wide;
    char character;

    if (value == NULL || ascii == NULL) {
        return 0;
    }
    index = 0;
    while (ascii[index] != '\0') {
        wide = value[index];
        character = ascii[index];
        if (wide == L'\0') {
            return 0;
        }
        if (wide >= L'A' && wide <= L'Z') {
            wide = (WCHAR) (wide + (L'a' - L'A'));
        }
        if (character >= 'A' && character <= 'Z') {
            character = (char) (character + ('a' - 'A'));
        }
        if (wide != (WCHAR) (unsigned char) character) {
            return 0;
        }
        index++;
    }
    return 1;
}

static int app_command_copy_value(const WCHAR *argument, const char *prefix,
        WCHAR *output, int output_capacity)
{
    int prefix_length;
    int length;

    if (argument == NULL || prefix == NULL || output == NULL ||
            output_capacity <= 1 || !app_command_ascii_prefix(argument,
            prefix)) {
        return 0;
    }
    prefix_length = (int) strlen(prefix);
    if (argument[prefix_length] != L'=') {
        return 0;
    }
    length = lstrlenW(argument + prefix_length + 1);
    if (length <= 0 || length >= output_capacity) {
        return -1;
    }
    memcpy(output, argument + prefix_length + 1,
            (size_t) (length + 1) * sizeof(WCHAR));
    return 1;
}

/* Build the private --click convenience source without allowing the selector
 * to escape the bounded JavaScript string literal.  The selector remains
 * page-provided input; this helper only quotes it and does not broaden the
 * document facade or create a second automation language. */
static int app_build_startup_click_script(const char *selector,
        char *output, int output_capacity)
{
    static const char prefix[] =
            "var e=document.querySelector('";
    static const char suffix[] =
            "');if(!e){throw new Error('startup click target not found');}"
            "e.click();";
    size_t selector_length;
    size_t prefix_length;
    size_t suffix_length;
    size_t i;
    int position;
    unsigned char byte;

    if (selector == NULL || output == NULL || output_capacity <= 1) {
        return 1;
    }
    output[0] = '\0';
    selector_length = strlen(selector);
    if (selector_length == 0 ||
            selector_length > APP_STARTUP_SELECTOR_MAX_BYTES) {
        return 1;
    }
    prefix_length = strlen(prefix);
    suffix_length = strlen(suffix);
    if (prefix_length + suffix_length + selector_length * 2U + 1U >=
            (size_t) output_capacity) {
        return 1;
    }
    position = 0;
    memcpy(output + position, prefix, prefix_length);
    position += (int) prefix_length;
    for (i = 0; i < selector_length; i++) {
        byte = (unsigned char) selector[i];
        if (byte < 0x20U || byte == 0x7fU) {
            output[0] = '\0';
            return 1;
        }
        if (byte == '\\' || byte == '\'') {
            output[position++] = '\\';
        }
        output[position++] = (char) byte;
    }
    memcpy(output + position, suffix, suffix_length);
    position += (int) suffix_length;
    output[position] = '\0';
    return 0;
}

static int app_parse_startup_arguments(LPWSTR command_line,
        char *out_reference, int reference_capacity, int *out_has_reference,
        char *out_script, int script_capacity, int *out_has_script,
        char *out_click_selector, int click_selector_capacity,
        int *out_has_click)
{
    static WCHAR argument[APP_COMMAND_ARG_MAX];
    static WCHAR value[APP_COMMAND_ARG_MAX];
    static char selector[APP_STARTUP_SELECTOR_MAX_BYTES + 1];
    const WCHAR *cursor;
    int result;
    int value_result;
    int has_reference;
    int has_script;

    if (out_reference == NULL || reference_capacity <= 1 ||
            out_has_reference == NULL || out_script == NULL ||
            script_capacity <= 1 || out_has_script == NULL ||
            out_click_selector == NULL || click_selector_capacity <= 1 ||
            out_has_click == NULL) {
        return 1;
    }
    out_reference[0] = '\0';
    out_script[0] = '\0';
    out_click_selector[0] = '\0';
    *out_has_reference = 0;
    *out_has_script = 0;
    *out_has_click = 0;
    has_reference = 0;
    has_script = 0;
    cursor = command_line != NULL ? command_line : L"";
    for (;;) {
        result = app_command_line_next_arg(&cursor, argument,
                sizeof(argument) / sizeof(argument[0]));
        if (result < 0) {
            return 1;
        }
        if (result == 0) {
            break;
        }
        value_result = app_command_copy_value(argument, "--url", value,
                sizeof(value) / sizeof(value[0]));
        if (value_result == 0) {
            value_result = app_command_copy_value(argument, "-u", value,
                    sizeof(value) / sizeof(value[0]));
        }
        if (value_result == 0) {
            value_result = app_command_copy_value(argument, "/url", value,
                    sizeof(value) / sizeof(value[0]));
        }
        if (value_result != 0 || app_command_ascii_equal(argument, "--url") ||
                app_command_ascii_equal(argument, "-u") ||
                app_command_ascii_equal(argument, "/url")) {
            if (value_result < 0 || has_reference) {
                return 1;
            }
            if (value_result == 0) {
                result = app_command_line_next_arg(&cursor, value,
                        sizeof(value) / sizeof(value[0]));
                if (result != 1) {
                    return 1;
                }
            }
            if (app_wide_buffer_to_utf8(value, lstrlenW(value),
                    out_reference, reference_capacity) != 0 ||
                    out_reference[0] == '\0') {
                return 1;
            }
            has_reference = 1;
            continue;
        }
        value_result = app_command_copy_value(argument, "--eval", value,
                sizeof(value) / sizeof(value[0]));
        if (value_result == 0) {
            value_result = app_command_copy_value(argument, "-e", value,
                    sizeof(value) / sizeof(value[0]));
        }
        if (value_result == 0) {
            value_result = app_command_copy_value(argument, "/eval", value,
                    sizeof(value) / sizeof(value[0]));
        }
        if (value_result != 0 || app_command_ascii_equal(argument, "--eval") ||
                app_command_ascii_equal(argument, "-e") ||
                app_command_ascii_equal(argument, "/eval")) {
            if (value_result < 0 || has_script) {
                return 1;
            }
            if (value_result == 0) {
                result = app_command_line_next_arg(&cursor, value,
                        sizeof(value) / sizeof(value[0]));
                if (result != 1) {
                    return 1;
                }
            }
            if (app_wide_buffer_to_utf8(value, lstrlenW(value), out_script,
                    script_capacity) != 0 || out_script[0] == '\0') {
                return 1;
            }
            has_script = 1;
            continue;
        }
        value_result = app_command_copy_value(argument, "--click", value,
                sizeof(value) / sizeof(value[0]));
        if (value_result == 0) {
            value_result = app_command_copy_value(argument, "-c", value,
                    sizeof(value) / sizeof(value[0]));
        }
        if (value_result == 0) {
            value_result = app_command_copy_value(argument, "/click", value,
                    sizeof(value) / sizeof(value[0]));
        }
        if (value_result != 0 || app_command_ascii_equal(argument, "--click") ||
                app_command_ascii_equal(argument, "-c") ||
                app_command_ascii_equal(argument, "/click")) {
            if (value_result < 0 || has_script) {
                return 1;
            }
            if (value_result == 0) {
                result = app_command_line_next_arg(&cursor, value,
                        sizeof(value) / sizeof(value[0]));
                if (result != 1) {
                    return 1;
                }
            }
            if (app_wide_buffer_to_utf8(value, lstrlenW(value), selector,
                    sizeof(selector)) != 0 ||
                    app_build_startup_click_script(selector, out_script,
                    script_capacity) != 0) {
                return 1;
            }
            if (strlen(selector) >= (size_t) click_selector_capacity) {
                return 1;
            }
            memcpy(out_click_selector, selector, strlen(selector) + 1U);
            *out_has_click = 1;
            has_script = 1;
            continue;
        }
        if (argument[0] == L'-' || argument[0] == L'/') {
            return 1;
        }
        if (has_reference || app_wide_buffer_to_utf8(argument,
                lstrlenW(argument), out_reference, reference_capacity) != 0 ||
                out_reference[0] == '\0') {
            return 1;
        }
        has_reference = 1;
    }
    *out_has_reference = has_reference;
    *out_has_script = has_script;
    return 0;
}

/* The Browser DOM facade intentionally exposes persistent wrappers by id.
 * Startup automation also needs to exercise the same native button path as a
 * user tap for common id-less controls (for example Bootstrap's
 * .navbar-toggler).  Keep this adapter application-private and bounded: it
 * matches one simple selector against the Core form-control snapshot, then
 * lets AppControls dispatch the normal trusted click transaction.  Complex
 * selectors and non-form elements still use the --eval fallback. */
static int app_startup_selector_name_char(unsigned char value)
{
    return (value >= (unsigned char) 'a' && value <= (unsigned char) 'z') ||
            (value >= (unsigned char) 'A' && value <= (unsigned char) 'Z') ||
            (value >= (unsigned char) '0' && value <= (unsigned char) '9') ||
            value == (unsigned char) '_' || value == (unsigned char) '-';
}

static int app_startup_form_attribute(HANDLE document, unsigned int index,
        const char *name, char *value, int capacity)
{
    int bytes;
    int result;

    if (value == NULL || capacity <= 1 || document == NULL || name == NULL) {
        return 0;
    }
    value[0] = '\0';
    bytes = 0;
    result = PCore_FormControlAttributeByIndex(document, index, name, value,
            capacity, &bytes);
    if (result != 0 || bytes < 0 || bytes >= capacity) {
        value[0] = '\0';
        return 0;
    }
    value[bytes] = '\0';
    return 1;
}

static int app_startup_class_contains(const char *classes, const char *wanted)
{
    const char *cursor;
    const char *start;
    size_t wanted_len;
    size_t length;

    if (classes == NULL || wanted == NULL || wanted[0] == '\0') {
        return 0;
    }
    wanted_len = strlen(wanted);
    cursor = classes;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
                *cursor == '\n' || *cursor == '\f') {
            cursor++;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
                *cursor != '\r' && *cursor != '\n' && *cursor != '\f') {
            cursor++;
        }
        length = (size_t) (cursor - start);
        if (length == wanted_len && memcmp(start, wanted, wanted_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int app_startup_click_form_selector(const char *selector)
{
    HANDLE document;
    const char *cursor;
    char tag[16];
    char id[128];
    char classes[4][64];
    char attribute_names[4][64];
    char attribute_values[4][160];
    int attribute_has_value[4];
    int tag_len;
    int id_len;
    int class_count;
    int attribute_count;
    int selector_len;
    int i;
    int start;
    int quote;
    int found;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;
    char actual[256];
    int class_match;
    int attribute_match;
    int value_start;
    int value_length;

    document = g_document;
    if (document == NULL || selector == NULL || selector[0] == '\0' ||
            g_controls == NULL) {
        return 0;
    }
    selector_len = (int) strlen(selector);
    if (selector_len <= 0 || selector_len > APP_STARTUP_SELECTOR_MAX_BYTES) {
        return 0;
    }
    memset(tag, 0, sizeof(tag));
    memset(id, 0, sizeof(id));
    memset(classes, 0, sizeof(classes));
    memset(attribute_names, 0, sizeof(attribute_names));
    memset(attribute_values, 0, sizeof(attribute_values));
    memset(attribute_has_value, 0, sizeof(attribute_has_value));
    tag_len = 0;
    id_len = 0;
    class_count = 0;
    attribute_count = 0;
    cursor = selector;
    while (*cursor != '\0' && *cursor != '#' && *cursor != '.' &&
            *cursor != '[') {
        if (!app_startup_selector_name_char((unsigned char) *cursor) ||
                tag_len >= (int) sizeof(tag) - 1) {
            return 0;
        }
        tag[tag_len++] = *cursor++;
    }
    tag[tag_len] = '\0';
    while (*cursor != '\0') {
        if (*cursor == '#') {
            cursor++;
            start = 0;
            while (app_startup_selector_name_char((unsigned char) cursor[start])) {
                start++;
            }
            if (start <= 0 || id_len != 0 || start >= (int) sizeof(id)) {
                return 0;
            }
            memcpy(id, cursor, (size_t) start);
            id[start] = '\0';
            id_len = start;
            cursor += start;
        } else if (*cursor == '.') {
            cursor++;
            start = 0;
            while (app_startup_selector_name_char((unsigned char) cursor[start])) {
                start++;
            }
            if (start <= 0 || class_count >= 4 ||
                    start >= (int) sizeof(classes[0])) {
                return 0;
            }
            memcpy(classes[class_count], cursor, (size_t) start);
            classes[class_count][start] = '\0';
            class_count++;
            cursor += start;
        } else if (*cursor == '[') {
            cursor++;
            if (attribute_count >= 4) {
                return 0;
            }
            start = 0;
            while (app_startup_selector_name_char((unsigned char) cursor[start])) {
                start++;
            }
            if (start <= 0 || start >= (int) sizeof(attribute_names[0])) {
                return 0;
            }
            memcpy(attribute_names[attribute_count], cursor, (size_t) start);
            attribute_names[attribute_count][start] = '\0';
            cursor += start;
            if (*cursor == ']') {
                cursor++;
                attribute_count++;
                continue;
            }
            if (*cursor != '=') {
                return 0;
            }
            cursor++;
            quote = 0;
            if (*cursor == '\'' || *cursor == '"') {
                quote = (int) (unsigned char) *cursor++;
            }
            value_start = 0;
            while (cursor[value_start] != '\0' &&
                    ((quote != 0 && (int) (unsigned char) cursor[value_start] !=
                    quote) || (quote == 0 && cursor[value_start] != ']'))) {
                value_start++;
            }
            value_length = value_start;
            if (value_length <= 0 || value_length >=
                    (int) sizeof(attribute_values[0])) {
                return 0;
            }
            memcpy(attribute_values[attribute_count], cursor,
                    (size_t) value_length);
            attribute_values[attribute_count][value_length] = '\0';
            cursor += value_length;
            if (quote != 0) {
                if (*cursor != (char) quote) {
                    return 0;
                }
                cursor++;
            }
            if (*cursor != ']') {
                return 0;
            }
            cursor++;
            attribute_has_value[attribute_count] = 1;
            attribute_count++;
        } else {
            return 0;
        }
    }
    if (tag_len == 0 && id_len == 0 && class_count == 0 &&
            attribute_count == 0) {
        return 0;
    }
    found = 0;
    for (i = 0; i < 64; i++) {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        kind = 0;
        disabled = 0;
        if (PCore_FormControlInfo(document, (unsigned int) i, &x, &y,
                &width, &height, &kind, NULL, &disabled) != 0) {
            break;
        }
        if (disabled || width <= 0 || height <= 0 ||
                (kind != 7 && kind != 8 && kind != 9)) {
            continue;
        }
        if (tag_len != 0 && _stricmp(tag, "button") != 0) {
            continue;
        }
        if (id_len != 0 && (!app_startup_form_attribute(document,
                (unsigned int) i, "id", actual, sizeof(actual)) ||
                strcmp(actual, id) != 0)) {
            continue;
        }
        class_match = 1;
        if (class_count > 0) {
            if (!app_startup_form_attribute(document, (unsigned int) i,
                    "class", actual, sizeof(actual))) {
                class_match = 0;
            } else {
                for (start = 0; start < class_count; start++) {
                    if (!app_startup_class_contains(actual, classes[start])) {
                        class_match = 0;
                        break;
                    }
                }
            }
        }
        if (!class_match) {
            continue;
        }
        attribute_match = 1;
        for (start = 0; start < attribute_count; start++) {
            if (!app_startup_form_attribute(document, (unsigned int) i,
                    attribute_names[start], actual, sizeof(actual))) {
                attribute_match = 0;
                break;
            }
            if (attribute_has_value[start] && strcmp(actual,
                    attribute_values[start]) != 0) {
                attribute_match = 0;
                break;
            }
        }
        if (!attribute_match) {
            continue;
        }
        if (AppControls_HandleButtonPointer(g_controls,
                x + width / 2, y + height / 2)) {
            found = 1;
            break;
        }
    }
    return found;
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
                !app_history_bound() ||
                info->url == NULL || info->state_json == NULL ||
                info->url[0] == '\0' || info->state_json[0] == '\0' ||
                PBrowser_HistorySameOriginUrl(host->current_url,
                info->url) != 1) {
            return 0;
        }
        if (info->kind == PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE) {
            app_history_save_scroll();
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
        (void) AppScript_SetDocumentUrl(context, host->current_url);
        app_history_save_scroll();
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
    if (g_page_kind > APP_PAGE_CONTROLS) {
        int x;
        int y;

        info.x = app_scale_dpi(info.x);
        info.y = app_scale_dpi(info.y);
        info.width = app_scale_dpi(info.width);
        info.height = app_scale_dpi(info.height);
        x = g_scroll_x;
        y = g_scroll_y;
        if (info.y < y) y = info.y;
        else if (info.y + info.height > y + g_page_height)
            y = info.y + info.height - g_page_height;
        if (info.x < x) x = info.x;
        else if (info.x + info.width > x + g_page_width)
            x = info.x + info.width - g_page_width;
        (void) app_scroll_to_position(hwnd, x, y);
    }
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

static int app_page_kind(const char *url)
{
    AppInternalRoute route;

    return AppInternalPages_Resolve(url, APP_NAV_SOURCE_DOCUMENT,
            &route) == 0 ? route.page_kind : 0;
}

static int app_canonicalize_url(const char *base_url, const char *reference,
        char *output, int output_capacity)
{
    AppUrlSchemeKind reference_scheme;
    AppInternalRoute route;
    int length;

    if (reference == NULL || output == NULL || output_capacity <= 1) {
        return 1;
    }
    reference_scheme = AppUrlRouter_ClassifyScheme(reference);
    if (reference_scheme == APP_URL_SCHEME_POSITRON) {
        if (AppInternalPages_Resolve(reference, APP_NAV_SOURCE_DOCUMENT,
                &route) != 0) return 1;
        length = (int) strlen(route.url);
        if (length >= output_capacity) return 1;
        memcpy(output, route.url, (size_t) length + 1);
        return 0;
    }
    if (reference_scheme == APP_URL_SCHEME_OTHER) {
        return 1;
    }
    return AppUrlRouter_ResolveNetworkReference(base_url, reference, output,
            output_capacity);
}

static int app_resolve_app_url(const char *base_url, const char *reference,
        char *output, int output_capacity)
{
    char local[APP_URL_MAX];
    const char *fragment;
    size_t base_length;

    if (reference == NULL || output == NULL || output_capacity <= 1)
        return 1;
    if (reference[0] == '#' &&
            AppUrlRouter_ClassifyScheme(base_url) == APP_URL_SCHEME_POSITRON) {
        fragment = strchr(base_url, '#');
        base_length = fragment != NULL ? (size_t) (fragment - base_url) :
                strlen(base_url);
        if (base_length + strlen(reference) >= sizeof(local)) return 1;
        memcpy(local, base_url, base_length);
        strcpy(local + base_length, reference);
        return app_canonicalize_url(base_url, local, output, output_capacity);
    }
    return app_canonicalize_url(base_url, reference, output, output_capacity);
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
    AppInternalPageData data;

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
    memset(&data, 0, sizeof(data));
    data.history = g_history;
    data.dpi = g_dpi;
    data.viewport_width = g_page_width;
    data.viewport_height = g_page_height;
    if (AppInternalPages_Build(page_kind, &data, &html, &html_length) != 0) {
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
    int pass;
    int old_page_width;
    int old_page_height;
#ifdef _DEBUG
    DWORD pass_started;
    DWORD style_finished;
    DWORD layout_finished;
    char timing_message[256];
#endif

    if (g_document == NULL) {
        return 0;
    }
    for (pass = 0; pass < 3; pass++) {
#ifdef _DEBUG
        pass_started = GetTickCount();
#endif
        old_page_width = g_page_width;
        old_page_height = g_page_height;
        PCore_SetDeviceViewport(g_page_width, g_page_height, g_dpi);
        if (g_page_kind == 0) {
            if (g_current_url[0] == '\0' ||
                    PCore_StyleDocumentEx2(g_document, NULL, g_current_url,
                    AppResources_Resolve, NULL, NULL, NULL) != 0) {
                return 1;
            }
#ifdef _DEBUG
            style_finished = GetTickCount();
#endif
            if (PCore_LayoutDocument(g_document, g_page_width,
                    g_page_height) != 0) {
                return 1;
            }
        } else if (g_stylesheet == NULL ||
                app_style_and_layout(g_document, g_stylesheet) != 0) {
            return 1;
        }
#ifdef _DEBUG
        layout_finished = GetTickCount();
        if (g_page_kind != 0) {
            style_finished = layout_finished;
        }
#endif
        g_document_width = PCore_DocumentWidth(g_document);
        g_document_height = PCore_DocumentHeight(g_document);
        if (g_document_width < g_page_width) {
            g_document_width = g_page_width;
        }
        if (g_document_height < g_page_height) {
            g_document_height = g_page_height;
        }
        app_clamp_scroll();
        (void) app_update_scrollbars(g_page_window);
#ifdef _DEBUG
        _snprintf(timing_message, sizeof(timing_message) - 1,
                "positron relayout pass=%d network=%d style_ms=%lu "
                "layout_ms=%lu viewport_ms=%lu size=%dx%d extent=%dx%d\r\n",
                pass, g_page_kind == 0,
                (unsigned long) (style_finished - pass_started),
                (unsigned long) (layout_finished - style_finished),
                (unsigned long) (GetTickCount() - layout_finished),
                g_page_width, g_page_height, g_document_width,
                g_document_height);
        timing_message[sizeof(timing_message) - 1] = '\0';
        AppDebug_Log(timing_message);
#endif
        if (old_page_width == g_page_width &&
                old_page_height == g_page_height) {
            break;
        }
    }
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
    int restore_x;
    int restore_y;

    restore_x = 0;
    restore_y = 0;
    if (history_mode == APP_HISTORY_REFRESH) {
        restore_x = g_scroll_x;
        restore_y = g_scroll_y;
    } else if (history_mode == APP_HISTORY_TARGET) {
        (void) PBrowser_HistoryEntryScroll(g_history, history_target,
                &restore_x, &restore_y);
    }
    if (app_build_page(url, &new_document, &new_stylesheet,
            &new_page_kind) != 0) {
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
            app_set_address(g_current_url);
            return 0;
        }
    }
    app_history_save_scroll();
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
    } else if (history_mode == APP_HISTORY_REFRESH) {
        history_rc = PBrowser_HistoryCommitTargetDocument(g_history,
                PBrowser_HistoryIndex(g_history));
    }
    if (history_rc != PBROWSER_OK) {
        PCore_FreeStylesheet(new_stylesheet);
        PCore_FreeDocument(new_document);
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
        app_set_address(g_current_url);
        return 0;
    }
    app_set_address(g_current_url);
    app_history_bind_page();
    if (app_relayout() != 0) {
        app_set_status(APP_TEXT_STATUS_LAYOUT);
    }
    app_set_focus_ids(g_page_kind);
    if (g_controls != NULL) {
        (void) AppControls_Rebuild(g_controls, g_document, g_script,
                g_scroll_x, g_scroll_y);
    }
    app_update_history_buttons();
    app_restore_page_status();
    (void) app_scroll_to_position(g_page_window, restore_x, restore_y);
    if (history_mode != APP_HISTORY_TARGET &&
            history_mode != APP_HISTORY_REFRESH) {
        (void) app_scroll_to_fragment(url);
    }
#ifdef _DEBUG
    {
        char message[1280];

        _snprintf(message, sizeof(message) - 1,
                "positron internal-page commit url=%s kind=%d history=%d "
                "focus=%d scroll=%d,%d script=%d\r\n",
                g_current_url, g_page_kind, PBrowser_HistoryCount(g_history),
                g_focus_count, g_scroll_x, g_scroll_y, g_script != NULL);
        message[sizeof(message) - 1] = '\0';
        AppDebug_Log(message);
    }
#endif
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

#ifdef _DEBUG
static const char *app_navigation_debug_stage(int stage)
{
    switch (stage) {
    case APP_NAV_COMMIT_SCRIPTS:
        return "scripts";
    case APP_NAV_COMMIT_STYLE:
        return "style";
    case APP_NAV_COMMIT_IMAGES:
        return "images";
    case APP_NAV_COMMIT_LAYOUT:
        return "layout";
    default:
        return "none";
    }
}

static void app_navigation_debug_log_url(const char *event,
        const char *url, const char *detail)
{
    char message[768];

    _snprintf(message, sizeof(message) - 1,
            "positron nav event=%s requested=%.*s visible=%.*s detail=%s\r\n",
            event != NULL ? event : "unknown",
            240, url != NULL ? url : "",
            240, g_current_url,
            detail != NULL ? detail : "");
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}

static void app_navigation_debug_log_request(AppNavigationRequest *request,
        const char *event, const char *detail)
{
    char message[1024];

    if (request == NULL) {
        return;
    }
    _snprintf(message, sizeof(message) - 1,
            "positron nav event=%s gen=%lu requested=%.*s visible=%.*s "
            "stage=%s worker=%d status=%d failure=%d current=%d detail=%s\r\n",
            event != NULL ? event : "unknown", request->generation,
            240, request->url, 240, g_current_url,
            app_navigation_debug_stage(request->commit_stage),
            request->worker_succeeded, request->worker_status_code,
            request->worker_failure_class,
            request == g_navigation_request ? 1 : 0,
            detail != NULL ? detail : "");
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}

static void app_navigation_debug_log_script_count(
        AppNavigationRequest *request, int count)
{
    char message[768];

    if (request == NULL) {
        return;
    }
    _snprintf(message, sizeof(message) - 1,
            "positron script-scan gen=%lu page=%.*s count=%d\r\n",
            request->generation, 600, request->url, count);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}

static void app_navigation_image_resource_counts(
        AppNavigationRequest *request, int *out_queued, int *out_ready,
        int *out_failed, int *out_pending)
{
    AppNavigationResource *resource;
    PBrowserNavigationResourceInfo info;

    if (out_queued != NULL) { *out_queued = 0; }
    if (out_ready != NULL) { *out_ready = 0; }
    if (out_failed != NULL) { *out_failed = 0; }
    if (out_pending != NULL) { *out_pending = 0; }
    if (request == NULL) {
        return;
    }
    for (resource = request->resources; resource != NULL;
            resource = resource->next) {
        if (AppResources_GetInfo(request, resource, &info) != 0 ||
                (info.role_mask & PBROWSER_NAVIGATION_RESOURCE_ROLE_IMAGE) ==
                0) {
            continue;
        }
        if (out_queued != NULL) { (*out_queued)++; }
        if (info.state == PBROWSER_NAVIGATION_RESOURCE_READY) {
            if (out_ready != NULL) { (*out_ready)++; }
        } else if (info.state == PBROWSER_NAVIGATION_RESOURCE_FAILED) {
            if (out_failed != NULL) { (*out_failed)++; }
        } else if (info.state == PBROWSER_NAVIGATION_RESOURCE_PENDING) {
            if (out_pending != NULL) { (*out_pending)++; }
        }
    }
}

/* Keep the EXE-only resource transaction diagnosable without changing the
 * public ABI or making telemetry part of the visible WM6 UI. The image scan
 * counters come from Core; the resource counters come from Browser; the box
 * and SVG counters are the closest public boundary for decoded/layout work. */
static void app_navigation_trace_image_state(AppNavigationRequest *request)
{
    PBrowserNavigationResourceStats resource_stats;
    PCoreBoxStats box_stats;
    PCoreImageDecodeStats image_stats;
    char message[640];
    int queued;
    int ready;
    int failed;
    int pending;
    int resource_ok;
    int box_ok;
    int image_ok;

    if (request == NULL) {
        return;
    }
    queued = 0;
    ready = 0;
    failed = 0;
    pending = 0;
    app_navigation_image_resource_counts(request, &queued, &ready,
            &failed, &pending);
    memset(&resource_stats, 0, sizeof(resource_stats));
    resource_stats.size = sizeof(resource_stats);
    resource_ok = request->resource_transaction != NULL &&
            PBrowser_NavigationResourceGetStats(
            request->resource_transaction, &resource_stats) == PBROWSER_OK;
    memset(&box_stats, 0, sizeof(box_stats));
    box_ok = request->document_candidate != NULL &&
            PCore_GetBoxStats(request->document_candidate, &box_stats) == 0;
    memset(&image_stats, 0, sizeof(image_stats));
    image_ok = request->document_candidate != NULL &&
            PCore_GetImageDecodeStats(request->document_candidate,
            &image_stats) == 0;
    _snprintf(message, sizeof(message) - 1,
            "positron image-state gen=%lu url=%.*s "
            "scan=%d/%d resources=%d/%d/%d/%d "
            "browser=%d/%d/%d box=%d/%u/%u/%u svg=%d/%u\r\n",
            request->generation, 240, request->url,
            request->image_scan_found,
            request->image_scan_fetched, queued, ready, failed, pending,
            resource_ok ? resource_stats.resources_ready : -1,
            resource_ok ? resource_stats.resources_fetched : -1,
            resource_ok ? resource_stats.resource_fallback_images : -1,
            box_ok ? 1 : 0,
            box_ok ? box_stats.image_calls : 0,
            box_ok ? box_stats.image_reuses : 0,
            box_ok ? box_stats.image_markup_first : 0,
            image_ok ? 1 : 0, image_ok ? image_stats.svg_creates : 0);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}

/* Keep the final application's Core form snapshot diagnosable without
 * changing the public ABI or treating a test-host fixture as product state.
 * Ordinary HTML buttons are Core-painted, so their presence and geometry are
 * the boundary needed to distinguish a missing visual box from an input that
 * never reached the page window. */
static void app_navigation_trace_controls(AppNavigationRequest *request)
{
    unsigned int index;
    unsigned int form_count;
    unsigned int button_count;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int first_x;
    int first_y;
    int first_width;
    int first_height;
    int first_kind;
    int first_found;
    char message[384];

    if (request == NULL || request->document_candidate == NULL) {
        return;
    }
    form_count = 0;
    button_count = 0;
    first_x = 0;
    first_y = 0;
    first_width = 0;
    first_height = 0;
    first_kind = 0;
    first_found = 0;
    for (index = 0; index < 64; index++) {
        if (PCore_FormControlInfo(request->document_candidate, index, &x, &y,
                &width, &height, &kind, NULL, NULL) != 0) {
            break;
        }
        form_count++;
        if (kind >= 7 && kind <= 9) {
            button_count++;
            if (!first_found) {
                first_x = x;
                first_y = y;
                first_width = width;
                first_height = height;
                first_kind = kind;
                first_found = 1;
            }
        }
    }
    _snprintf(message, sizeof(message) - 1,
            "positron controls gen=%lu forms=%u buttons=%u first=%d,%d,%dx%d/k%d\r\n",
            request->generation, form_count, button_count, first_x, first_y,
            first_width, first_height, first_kind);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}
#endif

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
    int retry;
    int transport_failure;
    int is_main_request;
    int retry_allowed;
    char content_type_header[APP_HOST_CONTENT_TYPE_MAX + 32];
    char final_url[PHTTP_URL_MAX];
    char resolved_url[PHTTP_URL_MAX];
    const char *fetch_url;
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
    /* PCore_FetchImageResources reports the selected image source directly;
     * unlike script/style discovery it has no resolver callback parameter.
     * TEST13 resolves that source against the document's effective URL before
     * calling HTTP. Do the same in the EXE so `/img/logo.svg` does not reach
     * PHttp_GetUrlEx as an origin-less URL. */
    resolved_url[0] = '\0';
    if (AppResources_Resolve(request, request->url, reference,
            resolved_url, sizeof(resolved_url)) != 0) {
        request->worker_failure_class = PBROWSER_NAVIGATION_FAILURE_RESOLVE;
        (void) app_navigation_resource_fail(request, index,
                PBROWSER_NAVIGATION_FAILURE_RESOLVE);
        return 0;
    }
    fetch_url = resolved_url;
    if (app_navigation_is_cancelled(request)) {
        (void) PBrowser_NavigationResourceCancelAll(
                request->resource_transaction);
        return 1;
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
            response = PHttp_PostUrlEx(fetch_url, headers, request->body,
                    request->body_bytes, NULL, NULL);
        } else {
            response = PHttp_GetUrlEx(fetch_url, NULL, NULL, NULL);
        }
        if (app_navigation_is_cancelled(request)) {
            PHttp_FreeResponse(response);
            response = NULL;
            (void) PBrowser_NavigationResourceCancelAll(
                    request->resource_transaction);
            return 1;
        }
        transport_failure = response == NULL || response->status_code == 0 ||
                (response != NULL && response->error_msg[0] != '\0');
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
            final_url[0] = '\0';
            if (PHttp_ResponseGetFinalUrl(response, final_url,
                    sizeof(final_url)) != 0) {
                request->worker_failure_class =
                        PBROWSER_NAVIGATION_FAILURE_TRANSPORT;
                (void) app_navigation_resource_fail(request, index,
                        PBROWSER_NAVIGATION_FAILURE_TRANSPORT);
            } else if (strlen(final_url) >= APP_HOST_URL_MAX) {
                request->worker_failure_class =
                        PBROWSER_NAVIGATION_FAILURE_BUDGET;
                (void) app_navigation_resource_fail(request, index,
                        PBROWSER_NAVIGATION_FAILURE_BUDGET);
            } else if (AppResources_SetEffectiveUrl(request, index,
                    final_url) != 0) {
                request->worker_failure_class =
                        PBROWSER_NAVIGATION_FAILURE_MEMORY;
                (void) app_navigation_resource_fail(request, index,
                        PBROWSER_NAVIGATION_FAILURE_MEMORY);
            } else if (PBrowser_NavigationResourceSetData(
                    request->resource_transaction, index, response->body,
                    response->body_len) == PBROWSER_OK) {
                if (is_main_request) {
                    memcpy(request->url, final_url, strlen(final_url) + 1);
                }
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
    request->document_candidate = document;
    /* The embedded application stylesheet belongs only to welcome/controls.
     * Network pages supply their own inline/link author sheets to Core. */
    request->stylesheet_candidate = NULL;
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
    int current;

    current = request == g_navigation_request;
#ifdef _DEBUG
    app_navigation_debug_log_request(request,
            committed ? "finish-commit" : "finish-rollback",
            committed ? "candidate-committed" : "candidate-failed");
#endif
    if (current) {
        g_navigation_request = NULL;
    }
    if (!committed) {
        if (current) {
            app_navigation_restore_ui(request);
        }
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
            int script_count;

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
                request->worker_resume_stage = APP_NAV_COMMIT_SCRIPTS;
                if (app_navigation_start_worker(request) != 0) {
                    return -1;
                }
                return 0;
            }
            script_count = PCore_GetScriptCount(request->document_candidate);
#ifdef _DEBUG
            app_navigation_debug_log_script_count(request, script_count);
#endif
            if (script_count > 0 || (g_startup_script_armed &&
                    request->generation == g_startup_script_generation)) {
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
#ifdef _DEBUG
                        app_navigation_debug_log_request(request,
                                "script-fail", "session-execute");
#endif
                        AppScript_Destroy(request->script_candidate);
                        request->script_candidate = NULL;
                    }
#ifdef _DEBUG
                } else {
                    app_navigation_debug_log_request(request, "script-fail",
                            "script-create");
#endif
                }
            }
            request->commit_stage = APP_NAV_COMMIT_STYLE;
            continue;
        }
        if (request->commit_stage == APP_NAV_COMMIT_STYLE) {
            /* Media queries are evaluated while Core selects computed styles,
             * not when the later layout pass starts.  Install the physical
             * device viewport before collecting/selecting the candidate page;
             * otherwise a freshly navigated WM6 page is styled with the
             * previous/default viewport and responsive assets (for example
             * IANA's 128px header SVG) remain on the desktop branch. */
            PCore_SetDeviceViewport(g_page_width, g_page_height, g_dpi);
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_REQUIRED;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET;
            result = PCore_StyleDocumentEx2(request->document_candidate,
                    NULL, request->url,
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
                request->worker_resume_stage = APP_NAV_COMMIT_STYLE;
                if (app_navigation_start_worker(request) != 0) {
                    return -1;
                }
                return 0;
            }
            request->commit_stage = APP_NAV_COMMIT_IMAGES;
            continue;
        }
        if (request->commit_stage == APP_NAV_COMMIT_IMAGES) {
            request->image_scan_found = 0;
            request->image_scan_fetched = 0;
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_IMAGE;
            (void) PCore_FetchImageResourcesEx(request->document_candidate,
                    AppResources_FetchImage, AppResources_Free, request,
                    &request->image_scan_found,
                    &request->image_scan_fetched);
            request->resource_policy =
                    PBROWSER_NAVIGATION_RESOURCE_OPTIONAL;
            request->resource_role_mask =
                    PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE;
            if (app_navigation_pending_count(request) > 0) {
                request->worker_stage = APP_NAV_WORK_RESOURCES;
                request->worker_resume_stage = APP_NAV_COMMIT_STYLE;
                request->commit_stage = APP_NAV_COMMIT_STYLE;
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
#ifdef _DEBUG
        app_navigation_trace_controls(request);
        app_navigation_trace_image_state(request);
#endif
        if (g_script != NULL) {
            int prevented;

            prevented = 0;
            if (AppScript_BeforeUnload(g_script, &prevented) != 0 ||
                    prevented) {
                return -1;
            }
        }
        app_history_save_scroll();
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
        } else if (request->history_mode == APP_HISTORY_REFRESH) {
            history_rc = PBrowser_HistoryCommitTargetDocument(g_history,
                    PBrowser_HistoryIndex(g_history));
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
        if (request->method == PCORE_FORM_METHOD_GET) {
            app_history_bind_page();
        }
        app_set_focus_ids(g_page_kind);
        app_set_address(g_current_url);
        if (app_relayout() != 0) {
            app_set_status(APP_TEXT_STATUS_LAYOUT);
        }
        if (g_controls != NULL) {
            (void) AppControls_Rebuild(g_controls, g_document, g_script,
                    g_scroll_x, g_scroll_y);
        }
        app_update_history_buttons();
        app_set_status(app_ready_status());
        if (request->method == PCORE_FORM_METHOD_GET &&
                (request->history_mode == APP_HISTORY_TARGET ||
                request->history_mode == APP_HISTORY_REFRESH)) {
            app_history_restore_scroll(PBrowser_HistoryIndex(g_history));
        }
        if (g_script != NULL) {
            (void) AppScript_SetVisibility(g_script,
                    IsWindowVisible(g_window) ? 0 : 1);
            (void) AppScript_SetFocus(g_script,
                    GetForegroundWindow() == g_window ? 1 : 0);
            (void) AppScript_PageLifecycleComplete(g_script);
            if (g_startup_script_armed && request->generation ==
                    g_startup_script_generation) {
                int startup_result;
                int native_click;
#ifdef _DEBUG
                char startup_detail[64];
#endif

                native_click = 0;
                if (g_startup_click_armed) {
                    native_click = app_startup_click_form_selector(
                            g_startup_click_selector);
                }
                if (native_click) {
                    startup_result = PSCRIPT_OK;
                    g_startup_click_armed = 0;
#ifdef _DEBUG
                    app_navigation_debug_log_request(request, "startup-script",
                            "native-button");
#endif
                } else {
                    startup_result = AppScript_Evaluate(g_script,
                            g_startup_script, (int) strlen(g_startup_script));
                    g_startup_click_armed = 0;
                }
                g_startup_script_armed = 0;
#ifdef _DEBUG
                if (!native_click) {
                    _snprintf(startup_detail, sizeof(startup_detail) - 1,
                            "result=%d", startup_result);
                    startup_detail[sizeof(startup_detail) - 1] = '\0';
                    app_navigation_debug_log_request(request, "startup-script",
                            startup_result == PSCRIPT_OK ? "executed" :
                            startup_detail);
                }
#endif
            }
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
#ifdef _DEBUG
        app_navigation_debug_log_request(request, "finish-stale",
                "retired-request");
#endif
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
        request->worker_resume_stage = APP_NAV_COMMIT_NONE;
    } else if (request->worker_stage == APP_NAV_WORK_RESOURCES) {
        if (request->worker_resume_stage != APP_NAV_COMMIT_NONE) {
            request->commit_stage = request->worker_resume_stage;
        } else {
            request->commit_stage = APP_NAV_COMMIT_STYLE;
        }
        request->worker_resume_stage = APP_NAV_COMMIT_NONE;
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
    char canonical[APP_HOST_URL_MAX];
    LONG generation;
    int index;

    if (!g_http_initialized || url == NULL || url[0] == '\0' ||
            (method != PCORE_FORM_METHOD_GET && method !=
            PCORE_FORM_METHOD_POST && method != PCORE_FORM_METHOD_MULTIPART) ||
            body_bytes < 0 || body_bytes > APP_FORMS_BODY_MAX_BYTES ||
            (body_bytes > 0 && body == NULL) || (content_type != NULL &&
            (int) strlen(content_type) >= APP_HOST_CONTENT_TYPE_MAX)) {
#ifdef _DEBUG
        app_navigation_debug_log_url("reject", url, "invalid-arguments");
#endif
        app_restore_page_status();
        return 0;
    }
    app_file_picker_cancel_pending();
    if (g_navigation_request != NULL &&
            app_navigation_retired_count() >= APP_NAV_MAX_RETIRED) {
#ifdef _DEBUG
        app_navigation_debug_log_url("reject", url, "retired-limit");
#endif
        app_restore_page_status();
        return 0;
    }
    request = (AppNavigationRequest *) malloc(sizeof(*request));
    if (request == NULL) {
#ifdef _DEBUG
        app_navigation_debug_log_url("reject", url, "request-allocation");
#endif
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
#ifdef _DEBUG
            app_navigation_debug_log_url("reject", url, "body-allocation");
#endif
            free(request);
            app_restore_page_status();
            return 0;
        }
        memcpy(request->body, body, (size_t) body_bytes);
        request->body_bytes = body_bytes;
    }
    app_copy_text(request->content_type,
            sizeof(request->content_type), content_type);
    if (AppUrlRouter_ResolveNetworkReference(NULL, url, canonical,
            sizeof(canonical)) != 0) {
#ifdef _DEBUG
        app_navigation_debug_log_url("reject", url, "network-url-router");
#endif
        free(request);
        app_restore_page_status();
        return 0;
    }
    app_copy_text(request->url, sizeof(request->url), canonical);
    request->resource_transaction = PBrowser_NavigationResourceCreate();
    if (request->resource_transaction == NULL ||
            AppResources_Register(request, request->url,
            PBROWSER_NAVIGATION_RESOURCE_REQUIRED,
            PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE, &index) != 0) {
#ifdef _DEBUG
        app_navigation_debug_log_url("reject", request->url,
                "resource-registration");
#endif
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
#ifdef _DEBUG
        app_navigation_debug_log_request(request, "reject",
                "candidate-allocation");
#endif
        app_navigation_request_destroy(request);
        app_restore_page_status();
        return 0;
    }
    app_navigation_capture_ui(request);
    if (g_navigation_request != NULL && app_navigation_cancel_active() != 0) {
        app_navigation_request_destroy(request);
        app_restore_page_status();
        return 0;
    }
    g_navigation_request = request;
#ifdef _DEBUG
    app_navigation_debug_log_request(request, "start", "worker-queued");
#endif
    app_set_status(APP_TEXT_STATUS_LOADING);
    app_set_address(request->url);
    if (app_navigation_start_worker(request) != 0) {
        app_navigation_finish(request, 0);
        return 0;
    }
    return 1;
}

/* Return zero only when a full document navigation is needed. A negative
 * result rejects this operation without falling through to a network reload. */
static int app_history_traverse(HWND hwnd, int target_index)
{
    const char *entry;
    char url[APP_URL_MAX];
    char state[PBROWSER_HISTORY_STATE_MAX];
    char old_url[APP_URL_MAX];
    int old_index;

    if (!app_history_bound() || !PBrowser_HistoryIsSameDocumentTarget(
            g_history, target_index)) {
        return 0;
    }
    entry = PBrowser_HistoryEntryUrl(g_history, target_index);
    if (entry == NULL || strlen(entry) >= sizeof(url)) return -1;
    app_copy_text(url, sizeof(url), entry);
    entry = PBrowser_HistoryEntryState(g_history, target_index);
    if (entry == NULL || strlen(entry) >= sizeof(state)) return -1;
    app_copy_text(state, sizeof(state), entry);
    app_copy_text(old_url, sizeof(old_url), g_current_url);
    old_index = PBrowser_HistoryIndex(g_history);
    if (app_navigation_cancel_active() != 0) return -1;
    app_history_save_scroll();
    if (PBrowser_HistoryCommitTarget(g_history, target_index) != PBROWSER_OK)
        return -1;
    app_copy_text(g_current_url, sizeof(g_current_url), url);
    if (g_script != NULL) {
        (void) AppScript_SetDocumentUrl(g_script, url);
        if (AppScript_DispatchHistoryTraversal(g_script, state, url) != 0) {
            /* Do not overwrite a pushState performed by an event handler. */
            if (PBrowser_HistoryIndex(g_history) == target_index &&
                    strcmp(g_current_url, url) == 0) {
                (void) PBrowser_HistoryCommitTarget(g_history, old_index);
                app_copy_text(g_current_url, sizeof(g_current_url), old_url);
                (void) AppScript_SetDocumentUrl(g_script, old_url);
            }
            app_set_address(g_current_url);
            app_update_history_buttons();
            app_restore_page_status();
            return -1;
        }
    }
    app_set_address(g_current_url);
    app_update_history_buttons();
    app_restore_page_status();
    /* Follow the reference host: fragments reveal their live target; other
     * entries use the saved viewport unless Browser requests manual mode. */
    if (strchr(g_current_url, '#') != NULL) {
        (void) app_scroll_to_fragment(g_current_url);
    } else {
        app_history_restore_scroll(PBrowser_HistoryIndex(g_history));
    }
#ifdef _DEBUG
    AppDebug_Log("positron history same-document traversal OK\r\n");
#endif
    (void) hwnd;
    return 1;
}

static int app_navigate_fragment(HWND hwnd, const char *url, int replace)
{
    char target[APP_URL_MAX];
    const char *hash;
    size_t base_length;
    int result;

    if (!app_history_bound() || url == NULL) return 0;
    if (url[0] == '#') {
        /* The anchor callback borrows a fragment-only href. Retain it as
         * document-location metadata, never send it through transport. */
        hash = strchr(g_current_url, '#');
        base_length = hash != NULL ? (size_t) (hash - g_current_url) :
                strlen(g_current_url);
        if (base_length + strlen(url) >= sizeof(target)) return -1;
        memcpy(target, g_current_url, base_length);
        strcpy(target + base_length, url);
    } else {
        if (strchr(url, '#') == NULL ||
                !PBrowser_HistorySameBaseUrl(g_current_url, url)) return 0;
        if (strlen(url) >= sizeof(target)) return -1;
        app_copy_text(target, sizeof(target), url);
    }
    if (app_navigation_cancel_active() != 0) return -1;
    if (strcmp(g_current_url, target) != 0) {
        app_history_save_scroll();
        result = replace ? PBrowser_HistoryReplaceState(g_history,
                target, "null") : PBrowser_HistoryPushState(g_history,
                target, "null");
        if (result != PBROWSER_OK) return -1;
        app_copy_text(g_current_url, sizeof(g_current_url), target);
        if (g_script != NULL) {
            (void) AppScript_SetDocumentUrl(g_script, target);
            (void) AppScript_DispatchHashNavigation(g_script, target,
                    PBrowser_HistoryCount(g_history));
        }
    }
    app_set_address(g_current_url);
    app_update_history_buttons();
    app_restore_page_status();
    (void) app_scroll_to_fragment(g_current_url);
    (void) hwnd;
    return 1;
}

static int app_load_page_from(HWND hwnd, const char *url, int history_mode,
        int history_target, AppNavigationSource source)
{
    char canonical[APP_URL_MAX];
    AppInternalRoute route;
    int same_document;

    if (url == NULL || url[0] == '\0') return 0;
    if (history_mode == APP_HISTORY_TARGET) {
        same_document = app_history_traverse(hwnd, history_target);
        if (same_document != 0) return same_document > 0;
    }
    if (AppUrlRouter_ClassifyScheme(url) == APP_URL_SCHEME_POSITRON) {
        if (AppInternalPages_Resolve(url, source, &route) != 0) {
            app_set_address(g_current_url);
            return 0;
        }
        if (route.kind == APP_INTERNAL_COMMAND) {
            return PostMessage(hwnd, WM_CLOSE, 0, 0) ? 1 : 0;
        }
        if (history_mode == APP_HISTORY_NEW ||
                history_mode == APP_HISTORY_REPLACE) {
            same_document = app_navigate_fragment(hwnd, route.url,
                    history_mode == APP_HISTORY_REPLACE);
            if (same_document != 0) return same_document > 0;
        }
        if (g_navigation_request != NULL &&
                app_navigation_cancel_active() != 0) {
            app_set_address(g_current_url);
            return 0;
        }
        return app_load_local_page(hwnd, route.url, history_mode,
                history_target);
    }
    if (history_mode == APP_HISTORY_NEW ||
            history_mode == APP_HISTORY_REPLACE) {
        same_document = app_navigate_fragment(hwnd, url,
                history_mode == APP_HISTORY_REPLACE);
        if (same_document != 0) return same_document > 0;
    }
    if (app_canonicalize_url(g_current_url, url, canonical,
            sizeof(canonical)) != 0) {
#ifdef _DEBUG
        app_navigation_debug_log_url("reject", url, "canonicalize");
#endif
        app_set_address(g_current_url);
        return 0;
    }
    return app_navigation_start(hwnd, canonical, PCORE_FORM_METHOD_GET,
            NULL, 0, NULL, history_mode, history_target);
}

#ifdef _DEBUG
static int app_history_debug_eval(const char *source)
{
    return AppScript_Evaluate(g_script, source, (int) strlen(source));
}

/* Exercise the real EXE adapters with an independent page/history and a
 * hidden native viewport. No network, live history or Release fixture. */
static int app_history_debug_check(void)
{
    static const char html[] =
            "<html><body style='margin:0'><div style='height:700px'>top</div>"
            "<h2 id='chapter'>chapter</h2>"
            "<a name='legacy' style='display:block;height:24px'>legacy</a>"
            "<div style='height:700px;width:600px'>tail</div></body></html>";
    AppHostContext *saved;
    AppScriptHostCallbacks callbacks;
    HANDLE document;
    AppScriptContext *script;
    HWND viewport;
    int result;
    int x;
    int y;
    int chapter_x;
    int chapter_y;
    int phase;
    char message[192];

    saved = (AppHostContext *) malloc(sizeof(*saved));
    if (saved == NULL) return 1;
    memcpy(saved, &g_app, sizeof(*saved));
    AppHostContext_Init(&g_app);
    g_instance = saved->instance;
    g_dpi = 96;
    result = 1;
    phase = 0;
    viewport = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 120, 80,
            NULL, NULL, g_instance, NULL);
    g_page_window = viewport;
    g_page_width = 120;
    g_page_height = 80;
    g_history = PBrowser_HistoryCreate();
    g_document = PCore_ParseHTML(html, (unsigned int) strlen(html));
    if (viewport == NULL || g_history == NULL || g_document == NULL)
        goto done;
    PCore_SetDeviceViewport(120, 80, 96);
    if (PCore_StyleDocumentEx2(g_document, NULL, "https://example.com/",
            AppResources_Resolve, NULL, NULL, NULL) != 0 ||
            PCore_LayoutDocument(g_document, 120, 80) != 0) goto done;
    g_document_width = PCore_DocumentWidth(g_document);
    g_document_height = PCore_DocumentHeight(g_document);
    if (PCore_FragmentInfoByToken(g_document, "chapter", &chapter_x,
            &chapter_y, NULL, NULL) != 0 || chapter_y < 100 ||
            PCore_FragmentInfoByToken(g_document, "legacy", &x, &y,
            NULL, NULL) != 0 ||
            PBrowser_HistoryCommitNavigation(g_history,
            "https://example.com/", PBROWSER_HISTORY_METHOD_GET,
            PBROWSER_HISTORY_TARGET_NEW) != PBROWSER_OK) goto done;
    app_copy_text(g_current_url, sizeof(g_current_url),
            "https://example.com/");
    app_history_bind_page();
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.size = sizeof(callbacks);
    callbacks.pw = &g_app;
    callbacks.navigate = app_script_navigate;
    callbacks.scroll = app_script_scroll;
    g_script = AppScript_Create(g_document, g_current_url, 1, 0, 1,
            "null", 120, 80, 96, &callbacks);
    if (g_script == NULL) goto done;
    phase = 1;
    document = g_document;
    script = g_script;
    if (app_history_debug_eval(
            "var kept=17,pops=0,hashes=0,unloads=0;"
            "addEventListener('popstate',function(e){pops++;});"
            "addEventListener('beforeunload',function(e){unloads++;});"
            "addEventListener('hashchange',function(e){hashes++;});")
            != PSCRIPT_OK) goto done;
    (void) app_scroll_to_position(viewport, 32, 41);
    phase = 2;
    if (app_history_debug_eval(
            "history.pushState({n:1},'', '/other');") != PSCRIPT_OK ||
            PBrowser_HistoryIndex(g_history) != 1) goto done;
    (void) app_scroll_to_position(viewport, 5, 250);
    phase = 3;
    if (app_history_traverse(NULL, 0) != 1 || g_document != document ||
            g_script != script || g_scroll_x != 32 || g_scroll_y != 41 ||
            app_history_debug_eval(
            "if(kept!==17||pops!==1||history.state!==null||"
            "location.href!=='https://example.com/'||pageYOffset!==41)"
            "throw Error('back');") != PSCRIPT_OK) goto done;
    phase = 4;
    if (app_history_traverse(NULL, 1) != 1 || g_scroll_x != 5 ||
            g_scroll_y != 250 || app_history_debug_eval(
            "if(pops!==2||history.state.n!==1)throw Error('forward');"
            "history.scrollRestoration='manual';") != PSCRIPT_OK ||
            app_history_traverse(NULL, 0) != 1 || g_scroll_y != 250)
        goto done;
    phase = 5;
    if (app_history_debug_eval(
            "history.scrollRestoration='auto';") != PSCRIPT_OK ||
            app_history_traverse(NULL, 1) != 1 ||
            app_navigate_fragment(NULL, "#chapter", 0) != 1 ||
            g_document != document || g_script != script ||
            g_scroll_x != chapter_x || g_scroll_y != chapter_y ||
            PBrowser_HistoryCount(g_history) != 3 ||
            app_history_debug_eval(
            "if(hashes!==1||location.hash!=='#chapter'||pops!==4||unloads!==0)"
            "throw Error('fragment');") != PSCRIPT_OK) goto done;
    phase = 6;
    if (app_navigate_fragment(NULL, "#chapter", 0) != 1 ||
            PBrowser_HistoryCount(g_history) != 3 ||
            app_navigate_fragment(NULL, "#missing", 1) != 1 ||
            g_scroll_y != chapter_y ||
            app_navigate_fragment(NULL, "#", 1) != 1 ||
            g_scroll_x != 0 || g_scroll_y != 0) goto done;
    phase = 7;
    if (PCore_FragmentInfoByToken(g_document, "legacy", &x, &y,
            NULL, NULL) != 0 ||
            app_navigate_fragment(NULL, "#legacy", 1) != 1 ||
            g_scroll_y != y ||
            app_navigate_fragment(NULL, "https://other.example/#chapter", 0)
            != 0) goto done;
    phase = 8;
    /* A displayed non-history page must neither reuse old same-document
     * entries nor overwrite their viewport. */
    g_app.history_document_id = 0;
    if (app_history_traverse(NULL, 1) != 0 ||
            app_navigate_fragment(NULL, "#chapter", 0) != 0) goto done;
    /* A reloaded document receives a fresh Browser identity but preserves
     * the entry's saved viewport. Old pushState siblings must reload. */
    app_history_bind_page();
    (void) app_scroll_to_position(viewport, 19, 123);
    app_history_save_scroll();
    if (PBrowser_HistoryCommitTargetDocument(g_history,
            PBrowser_HistoryIndex(g_history)) != PBROWSER_OK) goto done;
    app_history_bind_page();
    (void) app_scroll_to_position(viewport, 0, 0);
    app_history_restore_scroll(PBrowser_HistoryIndex(g_history));
    if (g_scroll_x != 19 || g_scroll_y != 123 ||
            app_history_traverse(NULL, 1) != 0) goto done;
    result = 0;
done:
    if (result != 0) {
        _snprintf(message, sizeof(message) - 1,
                "positron history check phase=%d scroll=%d,%d extent=%d,%d "
                "index=%d count=%d\r\n", phase, g_scroll_x, g_scroll_y,
                g_document_width, g_document_height,
                PBrowser_HistoryIndex(g_history), PBrowser_HistoryCount(g_history));
        message[sizeof(message) - 1] = '\0';
        AppDebug_Log(message);
    }
    AppHostContext_ReleasePage(&g_app);
    PBrowser_HistoryDestroy(g_history);
    if (viewport != NULL) DestroyWindow(viewport);
    memcpy(&g_app, saved, sizeof(g_app));
    free(saved);
    PCore_SetDeviceViewport(g_page_width > 0 ? g_page_width : 1,
            g_page_height > 0 ? g_page_height : 1, g_dpi);
    AppDebug_Log(result == 0 ? "positron history selftest OK\r\n" :
            "positron history selftest FAILED\r\n");
    return result;
}
#endif

static int app_load_page(HWND hwnd, const char *url, int history_mode,
        int history_target)
{
    return app_load_page_from(hwnd, url, history_mode, history_target,
            APP_NAV_SOURCE_DOCUMENT);
}

static int app_load_form_request(HWND hwnd, const AppFormRequest *request)
{
    char canonical[APP_URL_MAX];

    if (request == NULL || !request->valid || request->target_url[0] == '\0' ||
            request->method == PCORE_FORM_METHOD_DIALOG) {
        return 0;
    }
    if (request->method == PCORE_FORM_METHOD_GET) {
        return app_load_page_from(hwnd, request->target_url,
                APP_HISTORY_NEW, -1, APP_NAV_SOURCE_FORM);
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
        app_copy_text(output, output_capacity, APP_URL_NEWTAB);
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
        if (AppUrlRouter_ClassifyScheme(reference) ==
                APP_URL_SCHEME_POSITRON) {
            AppInternalRoute route;

            if (AppInternalPages_Resolve(reference, APP_NAV_SOURCE_ADDRESS,
                    &route) != 0) return 1;
            app_copy_text(output, output_capacity, route.url);
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
        app_set_address(g_current_url);
        return;
    }
    app_load_page_from(hwnd, url, APP_HISTORY_NEW, -1,
            APP_NAV_SOURCE_ADDRESS);
}

static void app_go_home(HWND hwnd)
{
    (void) app_load_page_from(hwnd, APP_URL_NEWTAB,
            APP_HISTORY_NEW, -1, APP_NAV_SOURCE_MENU);
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
    (void) app_load_page_from(hwnd, url, APP_HISTORY_TARGET, index,
            APP_NAV_SOURCE_HISTORY);
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
    (void) app_load_page_from(hwnd, url, APP_HISTORY_TARGET, index,
            APP_NAV_SOURCE_HISTORY);
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
            app_load_page_from(hwnd, target, APP_HISTORY_TARGET,
                    target_index, APP_NAV_SOURCE_HISTORY);
        }
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_RELOAD) {
        app_refresh(hwnd);
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FRAGMENT ||
            navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FRAGMENT_REPLACE) {
        (void) app_navigate_fragment(hwnd, navigation.url,
                navigation.kind == PBROWSER_SCRIPT_NAVIGATION_FRAGMENT_REPLACE);
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_REPLACE) {
        if (navigation.url[0] != '\0') {
            app_load_page_from(hwnd, navigation.url, APP_HISTORY_REPLACE,
                    -1, APP_NAV_SOURCE_SCRIPT);
        }
        return;
    }
    if (navigation.kind == PBROWSER_SCRIPT_NAVIGATION_ASSIGN &&
            navigation.url[0] != '\0') {
        app_load_page_from(hwnd, navigation.url, APP_HISTORY_NEW, -1,
                APP_NAV_SOURCE_SCRIPT);
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

/* Keep the page's default paint target out of the visible window while a
 * retained dirty region is composed.  WM6's GDI can expose the intermediate
 * white clear when a page is painted directly into the window DC; a bounded
 * compatible bitmap lets the user see one completed frame instead. */
static void app_page_paint_buffer_release(void)
{
    if (g_page_paint_buffer_dc != NULL) {
        DeleteDC(g_page_paint_buffer_dc);
        g_page_paint_buffer_dc = NULL;
    }
    if (g_page_paint_buffer_bitmap != NULL) {
        DeleteObject(g_page_paint_buffer_bitmap);
        g_page_paint_buffer_bitmap = NULL;
    }
    g_page_paint_buffer_width = 0;
    g_page_paint_buffer_height = 0;
}

static int app_page_paint_buffer_prepare(HDC target, int width, int height)
{
    HDC buffer_dc;
    HBITMAP buffer_bitmap;

    if (target == NULL || width <= 0 || height <= 0) {
        return 1;
    }
    if (g_page_paint_buffer_dc != NULL &&
            g_page_paint_buffer_bitmap != NULL &&
            g_page_paint_buffer_width >= width &&
            g_page_paint_buffer_height >= height) {
        return 0;
    }
    app_page_paint_buffer_release();
    buffer_dc = CreateCompatibleDC(target);
    if (buffer_dc == NULL) {
        return 1;
    }
    buffer_bitmap = CreateCompatibleBitmap(target, width, height);
    if (buffer_bitmap == NULL) {
        DeleteDC(buffer_dc);
        return 1;
    }
    if (SelectObject(buffer_dc, buffer_bitmap) == NULL) {
        DeleteObject(buffer_bitmap);
        DeleteDC(buffer_dc);
        return 1;
    }
    g_page_paint_buffer_dc = buffer_dc;
    g_page_paint_buffer_bitmap = buffer_bitmap;
    g_page_paint_buffer_width = width;
    g_page_paint_buffer_height = height;
    return 0;
}

static void app_paint_page_contents(HDC dc, const RECT *client,
        const RECT *clear)
{
    RECT focus_rect;
    PCoreFocusTargetInfo focus_info;
    int saved;

    saved = SaveDC(dc);
    IntersectClipRect(dc, client->left, client->top,
            client->right, client->bottom);
    IntersectClipRect(dc, clear->left, clear->top,
            clear->right, clear->bottom);
    FillRect(dc, clear, (HBRUSH) GetStockObject(WHITE_BRUSH));
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

static void app_paint_page(HWND hwnd, HDC dc, const RECT *paint_rect)
{
    RECT client;
    RECT clear;
    int width;
    int height;

    GetClientRect(hwnd, &client);
    if (paint_rect != NULL) {
        clear = *paint_rect;
    } else {
        clear = client;
    }
    if (clear.left < client.left) {
        clear.left = client.left;
    }
    if (clear.top < client.top) {
        clear.top = client.top;
    }
    if (clear.right > client.right) {
        clear.right = client.right;
    }
    if (clear.bottom > client.bottom) {
        clear.bottom = client.bottom;
    }
    width = clear.right - clear.left;
    height = clear.bottom - clear.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (app_page_paint_buffer_prepare(dc, width, height) != 0) {
        app_paint_page_contents(dc, &client, &clear);
        return;
    }
    SetViewportOrgEx(g_page_paint_buffer_dc, -clear.left, -clear.top,
            NULL);
    app_paint_page_contents(g_page_paint_buffer_dc, &client, &clear);
    SetViewportOrgEx(g_page_paint_buffer_dc, 0, 0, NULL);
    BitBlt(dc, clear.left, clear.top, width, height,
            g_page_paint_buffer_dc, 0, 0, SRCCOPY);
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
            if (g_document != NULL && g_scrollbar_settle_depth == 0 &&
                    app_relayout() != 0) {
                app_set_status(APP_TEXT_STATUS_LAYOUT);
            }
            if (g_scrollbar_settle_depth > 0) {
                return 0;
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

#ifdef _DEBUG
            DWORD paint_started;

            paint_started = GetTickCount();
#endif
            dc = BeginPaint(hwnd, &paint);
            app_paint_page(hwnd, dc, &paint.rcPaint);
            EndPaint(hwnd, &paint);
#ifdef _DEBUG
            AppDebug_LogElapsed("page-paint", paint_started);
#endif
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
#ifdef _DEBUG
            app_debug_pointer_down(x, y, document_x, document_y, g_scroll_x,
                    g_scroll_y);
#endif
            if (g_document != NULL && PCore_OverflowPointer(g_document,
                    PCORE_POINTER_DOWN, document_x, document_y)) {
                g_overflow_pointer = 1;
                SetCapture(hwnd);
                app_sync_overflow_scroll();
                app_invalidate_overflow(hwnd);
                return 0;
            }
            if (g_document != NULL && PCore_InteractionSetAt(g_document,
                    document_x, document_y, PCORE_INTERACTION_FOCUS |
                    PCORE_INTERACTION_ACTIVE) > 0) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
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
    case WM_MOUSEMOVE:
        if (g_overflow_pointer && g_document != NULL &&
                (wparam & MK_LBUTTON) != 0) {
            int x;
            int y;

            x = (int) (short) LOWORD(lparam);
            y = (int) (short) HIWORD(lparam);
            (void) PCore_OverflowPointer(g_document, PCORE_POINTER_MOVE,
                    x + g_scroll_x, y + g_scroll_y);
            app_sync_overflow_scroll();
            app_invalidate_overflow(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_overflow_pointer) {
            int x;
            int y;

            x = (int) (short) LOWORD(lparam);
            y = (int) (short) HIWORD(lparam);
            if (g_document != NULL) {
                (void) PCore_OverflowPointer(g_document, PCORE_POINTER_UP,
                        x + g_scroll_x, y + g_scroll_y);
                app_sync_overflow_scroll();
            }
            g_overflow_pointer = 0;
            ReleaseCapture();
            app_invalidate_overflow(hwnd);
            return 0;
        }
        if (g_document != NULL && PCore_InteractionClear(g_document,
                PCORE_INTERACTION_ACTIVE) > 0) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
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
        case APP_CMD_HISTORY:
            (void) app_load_page_from(hwnd, "positron://history",
                    APP_HISTORY_NEW, -1, APP_NAV_SOURCE_MENU);
            return 0;
        case APP_CMD_DOWNLOADS:
            (void) app_load_page_from(hwnd, "positron://downloads",
                    APP_HISTORY_NEW, -1, APP_NAV_SOURCE_MENU);
            return 0;
        case APP_CMD_SETTINGS:
            (void) app_load_page_from(hwnd, "positron://settings",
                    APP_HISTORY_NEW, -1, APP_NAV_SOURCE_MENU);
            return 0;
        case APP_CMD_ABOUT:
            (void) app_load_page_from(hwnd, "positron://about",
                    APP_HISTORY_NEW, -1, APP_NAV_SOURCE_MENU);
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
        {
            int form_reset;

            if (!g_script_refresh_pending ||
                    lparam != (LPARAM) g_script_refresh_context) {
                return 0;
            }
            form_reset = g_script_refresh_form_reset ||
                    (wparam == APP_CONTROLS_REFRESH_FORM_RESET);
            g_script_refresh_pending = 0;
            g_script_refresh_form_reset = 0;
            g_script_refresh_context = NULL;
            if (lparam == (LPARAM) g_script && g_document != NULL &&
                    g_controls != NULL) {
                int refresh_result;
#ifdef _DEBUG
                DWORD refresh_started;

                refresh_started = GetTickCount();
#endif

                AppControls_PrepareReconcile(g_controls);
                refresh_result = app_relayout();
                if (refresh_result != 0 ||
                        (form_reset ?
                        AppControls_ReconcileAfterFormReset(g_controls,
                        g_document, g_script, g_scroll_x, g_scroll_y) :
                        AppControls_Reconcile(g_controls, g_document,
                        g_script, g_scroll_x, g_scroll_y)) != 0) {
#ifdef _DEBUG
                    app_script_debug_refresh("failed", refresh_result,
                            form_reset);
#endif
                    app_set_status(APP_TEXT_STATUS_LAYOUT);
                } else {
#ifdef _DEBUG
                    app_script_debug_refresh("complete", 0, form_reset);
#endif
                    InvalidateRect(g_page_window, NULL, FALSE);
                }
#ifdef _DEBUG
                AppDebug_LogElapsed("controls-refresh", refresh_started);
#endif
            }
        }
        return 0;
    case WM_TIMER:
        if (wparam == APP_SCRIPT_TIMER_ID && g_script != NULL) {
#ifdef _DEBUG
            DWORD checkpoint_started;
            DWORD checkpoint_elapsed;
            int checkpoint_result;
            char checkpoint_message[160];

            checkpoint_started = GetTickCount();
            checkpoint_result = AppScript_RunTaskCheckpoint(g_script,
                    checkpoint_started);
            checkpoint_elapsed = GetTickCount() - checkpoint_started;
            if (checkpoint_result != 0 || checkpoint_elapsed >= 250UL) {
                _snprintf(checkpoint_message,
                        sizeof(checkpoint_message) - 1,
                        "positron script-checkpoint result=%d elapsed_ms=%lu\r\n",
                        checkpoint_result,
                        (unsigned long) checkpoint_elapsed);
                checkpoint_message[sizeof(checkpoint_message) - 1] = '\0';
                AppDebug_Log(checkpoint_message);
            }
#else
            (void) AppScript_RunTaskCheckpoint(g_script, GetTickCount());
#endif
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
        g_script_refresh_pending = 0;
        g_script_refresh_form_reset = 0;
        g_script_refresh_context = NULL;
        app_file_picker_cancel_pending();
        if (g_controls != NULL) {
            AppControls_Destroy(g_controls);
            g_controls = NULL;
        }
        app_page_paint_buffer_release();
        AppHostContext_Shutdown(&g_app);
        AppDebug_EndSession();
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
    SendMessage(address, WM_SETFONT, (WPARAM) GetStockObject(SYSTEM_FONT),
            TRUE);
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
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL |
            WS_CLIPCHILDREN,
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

static void app_show_startup_usage(void)
{
    MessageBoxW(NULL,
            L"Usage: positron.exe [URL]\n"
            L"       positron.exe --url URL --click \"CSS selector\"\n"
            L"       positron.exe --url URL --eval \"JavaScript\"",
            L"Positron", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPWSTR command_line, int show_command)
{
    WNDCLASSW window_class;
    HWND hwnd;
    MSG message;
    char initial_url[APP_URL_MAX];
    int startup_has_reference;
    int startup_has_script;
    int startup_has_click;
    int startup_invalid;
    int result;

    (void) previous;
    startup_has_reference = 0;
    startup_has_script = 0;
    startup_has_click = 0;
    if (app_parse_startup_arguments(command_line, g_startup_reference,
            sizeof(g_startup_reference), &startup_has_reference,
            g_startup_script, sizeof(g_startup_script),
            &startup_has_script, g_startup_click_selector,
            sizeof(g_startup_click_selector), &startup_has_click) != 0 ||
            (startup_has_script &&
            !startup_has_reference)) {
        app_show_startup_usage();
        return 2;
    }
    g_startup_script_armed = 0;
    g_startup_click_armed = 0;
    g_startup_script_generation = 0;
    AppHostContext_Init(&g_app);
    AppDebug_BeginSession();
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
#ifdef _DEBUG
    if (AppInternalPages_DebugCheck(g_app_css) != 0) {
        AppHostContext_Shutdown(&g_app);
        return 1;
    }
#endif
    AppHostContext_SetHistory(&g_app, PBrowser_HistoryCreate());
    if (g_history == NULL) {
        AppHostContext_Shutdown(&g_app);
        app_show_error(APP_TEXT_ERROR_HISTORY_INIT);
        return 1;
    }
#ifdef _DEBUG
    if (app_history_debug_check() != 0) {
        AppHostContext_Shutdown(&g_app);
        return 1;
    }
#endif
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
    app_copy_text(initial_url, sizeof(initial_url), APP_URL_NEWTAB);
    startup_invalid = 0;
    if (startup_has_reference &&
            app_normalize_address(g_startup_reference, initial_url,
            sizeof(initial_url)) != 0) {
        startup_invalid = 1;
    }
    if (!startup_invalid && AppUrlRouter_ClassifyScheme(initial_url) ==
            APP_URL_SCHEME_POSITRON) {
        AppInternalRoute route;

        if (AppInternalPages_Resolve(initial_url, APP_NAV_SOURCE_STARTUP,
                &route) != 0) startup_invalid = 1;
    }
    if (startup_invalid) {
        app_copy_text(initial_url, sizeof(initial_url), APP_URL_NEWTAB);
    }
    if (!startup_invalid && startup_has_script &&
            app_page_kind(initial_url) != 0) {
        startup_invalid = 1;
    }
    result = app_load_page_from(hwnd, initial_url,
            APP_HISTORY_NEW, -1, APP_NAV_SOURCE_STARTUP) ? 0 : 1;
    if (result != 0) {
        DestroyWindow(hwnd);
        return result;
    }
    if (!startup_invalid && startup_has_script) {
        g_startup_script_armed = 1;
        g_startup_click_armed = startup_has_click ? 1 : 0;
        g_startup_script_generation = (unsigned long) g_navigation_generation;
    }
    ShowWindow(hwnd, show_command == 0 ? SW_SHOW : show_command);
    UpdateWindow(hwnd);
    SetFocus(g_address);
    SendMessage(g_address, EM_SETSEL, 0, -1);
    if (startup_invalid) {
        app_set_status(APP_TEXT_STATUS_ADDRESS_INVALID);
    }
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
