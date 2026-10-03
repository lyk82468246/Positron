#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "app_address_bar.h"
#include "app_debug.h"

#define APP_ADDRESS_VIEW_CLASS L"PositronAddressView"
#define APP_ADDRESS_TEXT_MAX 4097
#define APP_ADDRESS_ANIMATION_TIMER 1
#define APP_ADDRESS_ANIMATION_MS 160

struct AppAddressBar {
    HWND edit;
    HWND view;
    HFONT font; /* borrowed stock/EDIT font */
    WCHAR url[APP_ADDRESS_TEXT_MAX];
    WCHAR title[APP_ADDRESS_TEXT_MAX];
    int loading;
    int editing;
    int dpi;
    int text_width;
    int text_height;
    DWORD started;
    HDC buffer_dc;
    HBITMAP buffer_bitmap;
    HBITMAP buffer_original;
    int buffer_width;
    int buffer_height;
};

static int app_address_fill(int width, DWORD elapsed)
{
    DWORD position;

    if (width <= 0) return 0;
    position = elapsed % 4000U;
    if (position >= 3600U) return 0;
    if (position >= 3200U) return width;
    return MulDiv(width, (int) position, 3200);
}

static int app_address_offset(int overflow, int dpi, DWORD elapsed)
{
    int speed;
    DWORD duration;
    DWORD position;

    if (overflow <= 0) return 0;
    speed = MulDiv(26, dpi, 96);
    if (speed < 1) speed = 1;
    duration = (DWORD) MulDiv(overflow, 1000, speed);
    if (duration < 1U) duration = 1U;
    position = elapsed % (duration * 2U + 2000U);
    if (position < 1000U) return 0;
    position -= 1000U;
    if (position < duration) return MulDiv(overflow, (int) position, (int) duration);
    position -= duration;
    if (position < 1000U) return overflow;
    position -= 1000U;
    return overflow - MulDiv(overflow, (int) position, (int) duration);
}

static void app_address_release_buffer(AppAddressBar *bar)
{
    if (bar->buffer_dc != NULL) {
        if (bar->buffer_original != NULL)
            SelectObject(bar->buffer_dc, bar->buffer_original);
        DeleteDC(bar->buffer_dc);
    }
    if (bar->buffer_bitmap != NULL) DeleteObject(bar->buffer_bitmap);
    bar->buffer_dc = NULL;
    bar->buffer_bitmap = NULL;
    bar->buffer_original = NULL;
    bar->buffer_width = 0;
    bar->buffer_height = 0;
}

static const WCHAR *app_address_text(AppAddressBar *bar)
{
    return !bar->loading && bar->title[0] ? bar->title : bar->url;
}

static void app_address_measure(AppAddressBar *bar)
{
    HDC dc;
    HFONT old_font;
    SIZE size;
    TEXTMETRIC metrics;
    const WCHAR *text;

    bar->text_width = 0;
    bar->text_height = 0;
    dc = GetDC(bar->view);
    if (dc == NULL) return;
    old_font = (HFONT) SelectObject(dc, bar->font);
    text = app_address_text(bar);
    if (GetTextExtentPoint32W(dc, text, (int) wcslen(text), &size)) {
        bar->text_width = size.cx > 65535 ? 65535 : size.cx;
    }
    if (GetTextMetrics(dc, &metrics)) bar->text_height = metrics.tmHeight;
    SelectObject(dc, old_font);
    ReleaseDC(bar->view, dc);
}

static void app_address_timer(AppAddressBar *bar)
{
    RECT rect;
    int padding;

    GetClientRect(bar->view, &rect);
    padding = MulDiv(2, bar->dpi, 96);
    if (!bar->editing && (bar->loading ||
            bar->text_width > rect.right - padding * 2)) {
        SetTimer(bar->view, APP_ADDRESS_ANIMATION_TIMER,
                APP_ADDRESS_ANIMATION_MS, NULL);
    } else {
        KillTimer(bar->view, APP_ADDRESS_ANIMATION_TIMER);
    }
}

static void app_address_changed(AppAddressBar *bar)
{
    bar->started = GetTickCount();
    app_address_measure(bar);
    app_address_timer(bar);
    InvalidateRect(bar->view, NULL, FALSE);
}

static void app_address_draw(AppAddressBar *bar, HDC dc, const RECT *rect)
{
    RECT clip;
    RECT filled;
    HFONT old_font;
    const WCHAR *text;
    int padding;
    int y;
    int offset;
    int saved;
    DWORD elapsed;

    text = app_address_text(bar);
    padding = MulDiv(2, bar->dpi, 96);
    if (padding < 1) padding = 1;
    clip = *rect;
    clip.left += padding;
    clip.right -= padding;
    elapsed = GetTickCount() - bar->started;
    offset = bar->loading ? 0 : app_address_offset(
            bar->text_width - (clip.right - clip.left), bar->dpi, elapsed);
    y = (rect->bottom - rect->top - bar->text_height) / 2 + rect->top;
    FillRect(dc, rect, GetSysColorBrush(COLOR_WINDOW));
    old_font = (HFONT) SelectObject(dc, bar->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    ExtTextOutW(dc, padding - offset, y, ETO_CLIPPED, &clip,
            text, (UINT) wcslen(text), NULL);
    if (bar->loading) {
        filled = *rect;
        filled.right = rect->left + app_address_fill(
                rect->right - rect->left, elapsed);
        if (filled.right > filled.left) {
            saved = SaveDC(dc);
            if (saved != 0) {
                FillRect(dc, &filled, GetSysColorBrush(COLOR_HIGHLIGHT));
                IntersectClipRect(dc, filled.left, filled.top,
                        filled.right, filled.bottom);
                SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                ExtTextOutW(dc, padding, y, ETO_CLIPPED, &clip,
                        text, (UINT) wcslen(text), NULL);
                RestoreDC(dc, saved);
            }
        }
    }
    SelectObject(dc, old_font);
}

static void app_address_paint(AppAddressBar *bar, HWND hwnd)
{
    PAINTSTRUCT paint;
    RECT rect;
    HDC dc;
    int width;
    int height;

    dc = BeginPaint(hwnd, &paint);
    GetClientRect(hwnd, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    if (width > 0 && height > 0) {
        if (width != bar->buffer_width || height != bar->buffer_height) {
            app_address_release_buffer(bar);
            /* Keep this UI bitmap bounded, independent of document extent. */
            if (width <= 2048 && height <= 256) {
                bar->buffer_dc = CreateCompatibleDC(dc);
                bar->buffer_bitmap = CreateCompatibleBitmap(dc, width, height);
                if (bar->buffer_dc != NULL && bar->buffer_bitmap != NULL) {
                    bar->buffer_original = (HBITMAP) SelectObject(
                            bar->buffer_dc, bar->buffer_bitmap);
                    bar->buffer_width = width;
                    bar->buffer_height = height;
                } else {
                    app_address_release_buffer(bar);
                }
            }
        }
        if (bar->buffer_dc != NULL) {
            app_address_draw(bar, bar->buffer_dc, &rect);
            BitBlt(dc, 0, 0, width, height, bar->buffer_dc, 0, 0, SRCCOPY);
        } else {
            app_address_draw(bar, dc, &rect);
        }
    }
    EndPaint(hwnd, &paint);
}

static LRESULT CALLBACK app_address_view_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    AppAddressBar *bar;
    CREATESTRUCT *create;
    POINT point;
    RECT rect;

    bar = (AppAddressBar *) GetWindowLong(hwnd, GWL_USERDATA);
    if (message == WM_CREATE) {
        create = (CREATESTRUCT *) lparam;
        bar = (AppAddressBar *) create->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG) bar);
        return 0;
    }
    if (bar == NULL) return DefWindowProc(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_PAINT: app_address_paint(bar, hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if (wparam == APP_ADDRESS_ANIMATION_TIMER && !bar->editing &&
                IsWindowVisible(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: SetCapture(hwnd); return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            point.x = (short) LOWORD(lparam);
            point.y = (short) HIWORD(lparam);
            GetClientRect(hwnd, &rect);
            if (PtInRect(&rect, point)) AppAddressBar_BeginEdit(bar);
        }
        return 0;
    case WM_CANCELMODE:
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, APP_ADDRESS_ANIMATION_TIMER);
        app_address_release_buffer(bar);
        bar->view = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

AppAddressBar *AppAddressBar_Create(HINSTANCE instance, HWND parent, int id)
{
    AppAddressBar *bar;
    WNDCLASS cls;

    memset(&cls, 0, sizeof(cls));
    cls.lpfnWndProc = app_address_view_proc;
    cls.hInstance = instance;
    cls.lpszClassName = APP_ADDRESS_VIEW_CLASS;
    if (!RegisterClass(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return NULL;
    bar = (AppAddressBar *) calloc(1, sizeof(*bar));
    if (bar == NULL) return NULL;
    bar->dpi = 96;
    bar->font = (HFONT) GetStockObject(SYSTEM_FONT);
    bar->edit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER |
            WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL, 0, 0, 1, 1,
            parent, (HMENU) id, instance, NULL);
    if (bar->edit != NULL) {
        SendMessage(bar->edit, WM_SETFONT, (WPARAM) bar->font, FALSE);
        bar->view = CreateWindowW(APP_ADDRESS_VIEW_CLASS, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 0, 1, 1,
                parent, NULL, instance, bar);
    }
    if (bar->edit == NULL || bar->view == NULL) {
        AppAddressBar_Destroy(bar);
        return NULL;
    }
    app_address_changed(bar);
    return bar;
}

HWND AppAddressBar_Edit(AppAddressBar *bar)
{
    return bar == NULL ? NULL : bar->edit;
}

HWND AppAddressBar_View(AppAddressBar *bar)
{
    return bar == NULL ? NULL : bar->view;
}

void AppAddressBar_Move(AppAddressBar *bar, int width, int height, int dpi)
{
    if (bar == NULL) return;
    bar->dpi = dpi > 0 ? dpi : 96;
    MoveWindow(bar->edit, 0, 0, width, height, TRUE);
    MoveWindow(bar->view, 0, 0, width, height, TRUE);
    app_address_changed(bar);
}

static void app_address_copy(WCHAR *target, const WCHAR *text)
{
    size_t length;

    if (text == NULL) text = L"";
    length = wcslen(text);
    if (length >= APP_ADDRESS_TEXT_MAX) length = APP_ADDRESS_TEXT_MAX - 1U;
    /* Never split a UTF-16 surrogate pair at the capacity boundary. */
    if (length && text[length - 1U] >= 0xd800 && text[length - 1U] <= 0xdbff) --length;
    memcpy(target, text, length * sizeof(WCHAR));
    target[length] = L'\0';
}

void AppAddressBar_SetUrl(AppAddressBar *bar, const WCHAR *url)
{
    if (bar == NULL) return;
    if (url == NULL) url = L"";
    if (wcscmp(bar->url, url) == 0) return;
    app_address_copy(bar->url, url);
    /* Do not overwrite typing/selection/IME when a pending page commits. */
    if (!bar->editing) SetWindowTextW(bar->edit, bar->url);
    if (bar->loading || !bar->title[0]) app_address_changed(bar);
}

void AppAddressBar_SetTitle(AppAddressBar *bar, const WCHAR *title)
{
    if (bar == NULL) return;
    if (title == NULL) title = L"";
    if (wcscmp(bar->title, title) == 0) return;
    app_address_copy(bar->title, title);
    if (!bar->loading) app_address_changed(bar);
}

void AppAddressBar_SetLoading(AppAddressBar *bar, int loading)
{
    if (bar == NULL || bar->loading == (loading ? 1 : 0)) return;
    bar->loading = loading ? 1 : 0;
    app_address_changed(bar);
}

void AppAddressBar_BeginEdit(AppAddressBar *bar)
{
    if (bar == NULL) return;
    if (!bar->editing) {
        bar->editing = 1;
        KillTimer(bar->view, APP_ADDRESS_ANIMATION_TIMER);
        SetWindowTextW(bar->edit, bar->url);
        ShowWindow(bar->edit, SW_SHOW);
        ShowWindow(bar->view, SW_HIDE);
    }
    SetFocus(bar->edit);
    SendMessage(bar->edit, EM_SETSEL, 0, -1);
}

void AppAddressBar_EndEdit(AppAddressBar *bar)
{
    if (bar == NULL || !bar->editing) return;
    bar->editing = 0;
    SetWindowTextW(bar->edit, bar->url);
    ShowWindow(bar->view, SW_SHOW);
    ShowWindow(bar->edit, SW_HIDE);
    app_address_changed(bar);
}

void AppAddressBar_Destroy(AppAddressBar *bar)
{
    if (bar == NULL) return;
    /* A focused EDIT can synchronously send WM_KILLFOCUS during teardown. */
    bar->editing = 0;
    /* Parent teardown can already have destroyed both child HWNDs. */
    if (bar->view != NULL && IsWindow(bar->view)) DestroyWindow(bar->view);
    if (bar->edit != NULL && IsWindow(bar->edit)) DestroyWindow(bar->edit);
    app_address_release_buffer(bar);
    free(bar);
}

#ifdef _DEBUG
int AppAddressBar_DebugCheck(HINSTANCE instance)
{
    AppAddressBar *bar;
    HWND parent;
    WCHAR text[128];
    int result;

    result = 1;
    parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 320, 100,
            NULL, NULL, instance, NULL);
    if (parent == NULL) goto done;
    bar = AppAddressBar_Create(instance, parent, 1);
    if (bar == NULL) { DestroyWindow(parent); goto done; }
    AppAddressBar_Move(bar, 240, 32, 128);
    AppAddressBar_SetUrl(bar, L"https://example.com/page");
    AppAddressBar_SetTitle(bar, L"A very long page title that must scroll without ever becoming a URL");
    GetWindowTextW(bar->edit, text, 128);
    if (wcscmp(text, L"https://example.com/page") || bar->text_width <= 236 ||
            app_address_offset(100, 96, 1000) != 0 ||
            app_address_offset(100, 96, 2500) <= 0 ||
            app_address_offset(0, 192, 2500) != 0 ||
            app_address_fill(240, 0) != 0 || app_address_fill(240, 1600) != 120 ||
            app_address_fill(240, 3200) != 240 || app_address_fill(240, 3600) != 0) goto release;
    SendMessage(bar->view, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    SendMessage(bar->view, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    if (!bar->editing) goto release;
    SetWindowTextW(bar->edit, L"https://example.com/typing");
    SendMessage(bar->edit, EM_SETSEL, 3, 7);
    AppAddressBar_SetLoading(bar, 1);
    AppAddressBar_SetUrl(bar, L"https://example.com/pending");
    GetWindowTextW(bar->edit, text, 128);
    SendMessage(bar->view, WM_TIMER, APP_ADDRESS_ANIMATION_TIMER, 0);
    if ((DWORD) SendMessage(bar->edit, EM_GETSEL, 0, 0) != MAKELONG(3, 7) ||
            wcscmp(text, L"https://example.com/typing") ||
            app_address_text(bar) != bar->url) goto release;
    AppAddressBar_EndEdit(bar);
    GetWindowTextW(bar->edit, text, 128);
    if (wcscmp(text, L"https://example.com/pending")) goto release;
    AppAddressBar_SetLoading(bar, 0);
    if (app_address_text(bar) != bar->title) goto release;
    AppAddressBar_SetTitle(bar, L"");
    if (app_address_text(bar) != bar->url) goto release;
    /* Real WM_PAINT exercises double-buffer ownership in the hidden fixture. */
    SendMessage(bar->view, WM_PAINT, 0, 0);
    AppAddressBar_Move(bar, 160, 30, 192);
    SendMessage(bar->view, WM_PAINT, 0, 0);
    result = 0;
release:
    AppAddressBar_Destroy(bar);
    DestroyWindow(parent);
done:
    AppDebug_Log(result == 0 ? "positron address-bar selftest OK\r\n" :
            "positron address-bar selftest FAILED\r\n");
    return result;
}
#endif
