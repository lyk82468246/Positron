/*
 * positron_app/main.c - the first independent Positron browser shell.
 *
 * This is an application consumer, not a second browser engine.  The shell
 * owns the WM6 window, native command controls, input routing and the small
 * offline page policy.  Page parsing, styling, layout, painting and bounded
 * focus/link queries cross the public positron_core.dll ABI.  Browser history
 * crosses the public positron_browser.dll ABI.
 *
 * Phase A deliberately keeps the page set offline and bounded.  That makes a
 * missing configuration file and a missing network service non-fatal while
 * still exercising the real Core paint/resize/scroll path in positron.exe.
 */

#include <windows.h>
#include <aygshell.h>
#include <string.h>

#include "positron_core.h"
#include "positron_browser.h"
#include "resource.h"

#ifndef WS_EX_CONTROLPARENT
#define WS_EX_CONTROLPARENT 0x00010000L
#endif

#define APP_URL_MAX             1024
#define APP_WIDE_TEXT_MAX       1024
#define APP_FOCUS_ID_MAX        128
#define APP_FOCUS_MAX           8

#define APP_ADDRESS_HEIGHT       28
#define APP_COMMAND_FALLBACK_HEIGHT 26

#define APP_ID_ADDRESS            100

#define APP_PAGE_WELCOME            1
#define APP_PAGE_CONTROLS           2

#define APP_HISTORY_NEW             1
#define APP_HISTORY_TARGET          2
#define APP_HISTORY_REFRESH         3

#define APP_WM_ADDRESS_GO       (WM_APP + 1)
#define APP_WM_ADDRESS_CANCEL   (WM_APP + 2)

static HWND g_window = NULL;
static HWND g_address = NULL;
static HWND g_menu_bar = NULL;
static WNDPROC g_address_original_proc = NULL;
static SHACTIVATEINFO g_shell_activate;
static HINSTANCE g_instance = NULL;

static HANDLE g_document = NULL;
static HANDLE g_stylesheet = NULL;
static HANDLE g_history = NULL;

static int g_page_kind = APP_PAGE_WELCOME;
static int g_page_width = 1;
static int g_page_height = 1;
static int g_document_width = 1;
static int g_document_height = 1;
static int g_scroll_x = 0;
static int g_scroll_y = 0;
static int g_dpi = 96;
static int g_focus_index = -1;
static int g_focus_count = 0;
static const char *g_focus_ids[APP_FOCUS_MAX];
static char g_focus_id[APP_FOCUS_ID_MAX];
static char g_current_url[APP_URL_MAX];

static const char g_welcome_html[] =
        "<!doctype html><html><head><title>Positron</title></head>"
        "<body><h1>Positron</h1>"
        "<p>This is the first independent Positron browser shell.</p>"
        "<p>The page below is rendered by positron_core.dll.</p>"
        "<p><a id=\"controls\" tabindex=\"0\" "
        "href=\"https://positron.local/controls\">"
        "Open the keyboard and focus page</a></p>"
        "<p><a id=\"reload\" tabindex=\"0\" "
        "href=\"https://positron.local/welcome\">"
        "Open this welcome page again</a></p>"
        "<h2>Phase A checks</h2>"
        "<p>Use the address bar, the WM6 command bar, a stylus or the hardware"
        " arrow keys. The page has a real Core layout and a real scroll range.</p>"
        "<p>Clicking empty page space is intentionally a no-op.</p>"
        "<p>More content is kept below the fold so the device scrollbar can be"
        " exercised without a network connection.</p>"
        "<p>Positron keeps the current document visible while a later page is"
        " being prepared. Network navigation will be added in a later batch.</p>"
        "<p>Public DLL boundaries remain independent of this application shell.</p>"
        "<p>The browser history handle is supplied by positron_browser.dll.</p>"
        "<p>Resize and rotation cause Core layout to run again against the new"
        " page viewport.</p>"
        "<p>Scroll down to the final link, then use the Back softkey or Home in"
        " the Menu to return here.</p>"
        "<p><a id=\"final\" tabindex=\"0\" "
        "href=\"https://positron.local/controls\">Continue to controls</a></p>"
        "</body></html>";

static const char g_controls_html[] =
        "<!doctype html><html><head><title>Keyboard and focus</title></head>"
        "<body><h1>Keyboard and focus</h1>"
        "<p>Tab moves through native application controls. When the page owns"
        " focus, Up and Down visit these links and Enter activates one.</p>"
        "<p>Left and Right scroll horizontally. Page Up and Page Down scroll"
        " vertically. Escape never turns into an accidental history Back.</p>"
        "<p><a id=\"welcome\" tabindex=\"0\" "
        "href=\"https://positron.local/welcome\">Back to the welcome page</a></p>"
        "<p><a id=\"welcome2\" tabindex=\"0\" "
        "href=\"https://positron.local/welcome\">Return using this second link</a></p>"
        "<h2>Input contract</h2>"
        "<p>The address bar uses a native EDIT control. Enter submits it and"
        " Escape restores the last committed address. Backspace remains the"
        " EDIT control's deletion key.</p>"
        "<p>Native text fields, SELECT controls, SIP and file picking will be"
        " connected to this shell in the next input batch.</p>"
        "<p><a id=\"home\" tabindex=\"0\" "
        "href=\"https://positron.local/welcome\">Home</a></p>"
        "</body></html>";

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

static void app_set_status(const char *status)
{
    WCHAR wide[APP_WIDE_TEXT_MAX];

    if (g_window == NULL) {
        return;
    }
    app_utf8_to_wide(status, wide, sizeof(wide) / sizeof(wide[0]));
    SetWindowTextW(g_window, wide);
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
    return client->bottom - APP_COMMAND_FALLBACK_HEIGHT;
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
    rect->top = APP_ADDRESS_HEIGHT;
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
    int width;
    int address_width;

    GetClientRect(hwnd, &client);
    width = client.right - client.left;
    if (width < 1) {
        width = 1;
    }
    address_width = width - 4;
    if (address_width < 1) {
        address_width = 1;
    }
    if (g_address != NULL) {
        MoveWindow(g_address, 2, 2, address_width,
                APP_ADDRESS_HEIGHT - 4, TRUE);
    }
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
    } else {
        g_focus_ids[g_focus_count++] = "welcome";
        g_focus_ids[g_focus_count++] = "welcome2";
        g_focus_ids[g_focus_count++] = "home";
    }
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

    if (g_document == NULL || g_focus_index < 0 ||
            g_focus_index >= g_focus_count) {
        return 0;
    }
    if (PCore_LinkInfoById(g_document, g_focus_ids[g_focus_index],
            &left, &top, &width, &height, href, sizeof(href)) != 0) {
        return 0;
    }
    return (int) SendMessage(hwnd, APP_WM_ADDRESS_GO, 1,
            (LPARAM) href);
}

static int app_page_kind(const char *url)
{
    if (strcmp(url, "https://positron.local/welcome") == 0) {
        return APP_PAGE_WELCOME;
    }
    if (strcmp(url, "https://positron.local/controls") == 0) {
        return APP_PAGE_CONTROLS;
    }
    return 0;
}

static const char *app_page_html(int page_kind)
{
    return (page_kind == APP_PAGE_CONTROLS) ?
            g_controls_html : g_welcome_html;
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
    document = PCore_ParseHTML(app_page_html(page_kind), 0);
    if (document == NULL) {
        return 1;
    }
    stylesheet = PCore_ParseCSS(g_app_css, 0,
            "https://positron.local/app.css");
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
    app_update_scrollbars(g_window);
    return 0;
}

static int app_create_menu_bar(HWND hwnd)
{
    SHMENUBARINFO menu_info;

    memset(&menu_info, 0, sizeof(menu_info));
    menu_info.cbSize = sizeof(menu_info);
    menu_info.hwndParent = hwnd;
    menu_info.nToolBarId = IDR_APP_MENUBAR;
    menu_info.hInstRes = (g_instance != NULL) ? g_instance :
            GetModuleHandle(NULL);
    if (!SHCreateMenuBar(&menu_info) || menu_info.hwndMB == NULL) {
        g_menu_bar = NULL;
        return 1;
    }
    g_menu_bar = menu_info.hwndMB;
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

static int app_load_page(HWND hwnd, const char *url, int history_mode,
        int history_target)
{
    HANDLE new_document;
    HANDLE new_stylesheet;
    HANDLE old_document;
    HANDLE old_stylesheet;
    int new_page_kind;
    int history_rc;

    if (app_build_page(url, &new_document, &new_stylesheet,
            &new_page_kind) != 0) {
        app_set_status("This address is not available in the Phase A offline shell.");
        app_set_address(g_current_url);
        return 0;
    }
    history_rc = PBROWSER_OK;
    if (history_mode == APP_HISTORY_NEW) {
        history_rc = PBrowser_HistoryCommitNavigation(g_history, url,
                PBROWSER_HISTORY_METHOD_GET, PBROWSER_HISTORY_TARGET_NEW);
    } else if (history_mode == APP_HISTORY_TARGET) {
        history_rc = PBrowser_HistoryCommitTargetDocument(g_history,
                history_target);
    }
    if (history_rc != PBROWSER_OK) {
        PCore_FreeStylesheet(new_stylesheet);
        PCore_FreeDocument(new_document);
        app_set_status("The browser history rejected this navigation.");
        app_set_address(g_current_url);
        return 0;
    }
    old_document = g_document;
    old_stylesheet = g_stylesheet;
    g_document = new_document;
    g_stylesheet = new_stylesheet;
    g_page_kind = new_page_kind;
    app_copy_text(g_current_url, sizeof(g_current_url), url);
    g_scroll_x = 0;
    g_scroll_y = 0;
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
    app_update_scrollbars(hwnd);
    app_update_history_buttons();
    app_set_status((g_page_kind == APP_PAGE_CONTROLS) ?
            "Ready - keyboard and focus page" :
            "Ready - offline welcome page");
    if (old_stylesheet != NULL) {
        PCore_FreeStylesheet(old_stylesheet);
    }
    if (old_document != NULL) {
        PCore_FreeDocument(old_document);
    }
    InvalidateRect(hwnd, NULL, TRUE);
    return 1;
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
        app_copy_text(output, output_capacity,
                "https://positron.local/welcome");
        return 0;
    }
    if (length == 7 && strncmp(start, "welcome", 7) == 0) {
        app_copy_text(output, output_capacity,
                "https://positron.local/welcome");
        return 0;
    }
    if (length == 8 && strncmp(start, "controls", 8) == 0) {
        app_copy_text(output, output_capacity,
                "https://positron.local/controls");
        return 0;
    }
    if (length == 18 && strncmp(start, "positron://welcome", 18) == 0) {
        app_copy_text(output, output_capacity,
                "https://positron.local/welcome");
        return 0;
    }
    if (length == 19 && strncmp(start, "positron://controls", 19) == 0) {
        app_copy_text(output, output_capacity,
                "https://positron.local/controls");
        return 0;
    }
    if (length >= output_capacity) {
        return 1;
    }
    memcpy(output, start, (size_t) length);
    output[length] = '\0';
    return (app_page_kind(output) == 0) ? 1 : 0;
}

static void app_go_from_address(HWND hwnd)
{
    char input[APP_URL_MAX];
    char url[APP_URL_MAX];

    if (app_wide_to_utf8(g_address, input, sizeof(input)) != 0 ||
            app_normalize_address(input, url, sizeof(url)) != 0) {
        app_set_status("Enter welcome or controls for an offline page.");
        app_set_address(g_current_url);
        return;
    }
    app_load_page(hwnd, url, APP_HISTORY_NEW, -1);
}

static void app_go_home(HWND hwnd)
{
    (void) app_load_page(hwnd, "https://positron.local/welcome",
            APP_HISTORY_NEW, -1);
}

static void app_go_back(HWND hwnd)
{
    const char *target;
    char url[APP_URL_MAX];
    int index;

    target = PBrowser_HistoryBackTarget(g_history, &index);
    if (target == NULL || strlen(target) >= sizeof(url)) {
        app_set_status("There is no previous page.");
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
        app_set_status("There is no next page.");
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
    RECT page;
    RECT clear;
    RECT focus_rect;
    PCoreFocusTargetInfo focus_info;
    int saved;

    app_page_rect(hwnd, &page);
    saved = SaveDC(dc);
    SetViewportOrgEx(dc, page.left, page.top, NULL);
    IntersectClipRect(dc, 0, 0, page.right - page.left,
            page.bottom - page.top);
    clear.left = 0;
    clear.top = 0;
    clear.right = page.right - page.left;
    clear.bottom = page.bottom - page.top;
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
        return 0;
    case WM_SIZE:
        app_reposition_controls(hwnd);
        {
            RECT page;
            app_page_rect(hwnd, &page);
            g_page_width = page.right - page.left;
            g_page_height = page.bottom - page.top;
            if (g_page_width < 1) {
                g_page_width = 1;
            }
            if (g_page_height < 1) {
                g_page_height = 1;
            }
            if (g_document != NULL && app_relayout() != 0) {
                app_set_status("The page could not be laid out after resize.");
            }
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_ACTIVATE:
        SHHandleWMActivate(hwnd, wparam, lparam, &g_shell_activate, FALSE);
        return 0;
    case WM_SETTINGCHANGE:
        SHHandleWMSettingChange(hwnd, wparam, lparam, &g_shell_activate);
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
        app_set_status("Address edit cancelled.");
        SetFocus(hwnd);
        return 0;
    case WM_LBUTTONDOWN:
        {
            RECT page;
            int x;
            int y;
            int document_x;
            int document_y;
            int focus_index;
            char href[APP_URL_MAX];

            x = (int) (short) LOWORD(lparam);
            y = (int) (short) HIWORD(lparam);
            app_page_rect(hwnd, &page);
            if (x < page.left || x >= page.right || y < page.top ||
                    y >= page.bottom) {
                break;
            }
            SetFocus(hwnd);
            document_x = x - page.left + g_scroll_x;
            document_y = y - page.top + g_scroll_y;
            focus_index = app_focus_at(document_x, document_y);
            if (focus_index >= 0) {
                (void) app_focus_set(hwnd, focus_index);
            }
            href[0] = '\0';
            if (g_document != NULL && PCore_LinkAt(g_document,
                    document_x, document_y, href, sizeof(href)) == 1) {
                (void) app_load_page(hwnd, href, APP_HISTORY_NEW, -1);
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
                app_scroll_by(hwnd, 0, (int) HIWORD(wparam) - g_scroll_y);
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
                app_scroll_by(hwnd, (int) HIWORD(wparam) - g_scroll_x, 0);
            } else {
                app_scroll_by(hwnd, amount, 0);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (GetFocus() != hwnd) {
            break;
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
            app_refresh(hwnd);
            return 0;
        }
        if (wparam == VK_TAB) {
            SetFocus(g_address);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_stylesheet != NULL) {
            PCore_FreeStylesheet(g_stylesheet);
            g_stylesheet = NULL;
        }
        if (g_document != NULL) {
            PCore_FreeDocument(g_document);
            g_document = NULL;
        }
        if (g_history != NULL) {
            PBrowser_HistoryDestroy(g_history);
            g_history = NULL;
        }
        if (g_menu_bar != NULL) {
            CommandBar_Destroy(g_menu_bar);
            g_menu_bar = NULL;
        }
        PCore_Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

static int app_create_controls(HWND hwnd)
{
    g_address = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
            ES_LEFT | ES_AUTOHSCROLL,
            0, 0, 1, 1, hwnd, (HMENU) APP_ID_ADDRESS,
            g_instance, NULL);
    if (g_address == NULL) {
        return 1;
    }
    g_address_original_proc = (WNDPROC) SetWindowLong(g_address,
            GWL_WNDPROC, (LONG) app_address_proc);
    return (g_address_original_proc == NULL) ? 1 : 0;
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
    g_instance = instance;
    g_dpi = app_device_dpi();
    if (PCore_Init() != 0) {
        MessageBoxW(NULL, L"The Positron rendering core could not start.",
                L"Positron", MB_OK | MB_ICONERROR);
        return 1;
    }
    g_history = PBrowser_HistoryCreate();
    if (g_history == NULL) {
        PCore_Shutdown();
        MessageBoxW(NULL, L"The browser history service could not start.",
                L"Positron", MB_OK | MB_ICONERROR);
        return 1;
    }
    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = app_window_proc;
    window_class.hInstance = g_instance;
    window_class.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    window_class.lpszClassName = L"PositronBrowserWindow";
    if (RegisterClassW(&window_class) == 0) {
        PBrowser_HistoryDestroy(g_history);
        g_history = NULL;
        PCore_Shutdown();
        MessageBoxW(NULL, L"The Positron window class could not register.",
                L"Positron", MB_OK | MB_ICONERROR);
        return 1;
    }
    hwnd = CreateWindowExW(WS_EX_CONTROLPARENT,
            L"PositronBrowserWindow", L"Positron",
            WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN |
            WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL,
            0, 0, GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN), NULL, NULL,
            g_instance, NULL);
    if (hwnd == NULL) {
        PBrowser_HistoryDestroy(g_history);
        g_history = NULL;
        PCore_Shutdown();
        MessageBoxW(NULL, L"The Positron window could not be created.",
                L"Positron", MB_OK | MB_ICONERROR);
        return 1;
    }
    g_window = hwnd;
    if (app_create_controls(hwnd) != 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    app_reposition_controls(hwnd);
    {
        RECT page;
        app_page_rect(hwnd, &page);
        g_page_width = page.right - page.left;
        g_page_height = page.bottom - page.top;
        if (g_page_width < 1) {
            g_page_width = 1;
        }
        if (g_page_height < 1) {
            g_page_height = 1;
        }
    }
    result = app_load_page(hwnd, "https://positron.local/welcome",
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
