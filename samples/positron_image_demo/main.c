/*
 * positron_image_demo - independent Windows Mobile image DLL consumer.
 *
 * This program intentionally includes and links only positron_image. It is a
 * small integration example for third-party WM C applications, not a browser
 * or test_host component.
 */

#include <windows.h>

#include "positron_image.h"

static const unsigned char g_bitmap_data[] = {
    0x42, 0x4d, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xc4, 0x0e,
    0x00, 0x00, 0xc4, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0xff, 0x00, 0xff, 0x00, 0x00, 0x00
};

static const char g_svg_data[] =
    "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='60'>"
    "<rect width='40' height='60' fill='#ff0000'/>"
    "<rect x='40' width='40' height='60' fill='#00ff00'/>"
    "<path d='M88 55 C88 10 114 10 114 55' fill='none' "
    "stroke='#0000ff' stroke-width='5'/>"
    "</svg>";

static PIMAGE_BITMAP g_bitmap = NULL;
static PIMAGE_SVG g_svg = NULL;
static int g_draw_error = PIMAGE_OK;

static void demo_text(HDC hdc, int x, int y, const WCHAR *text)
{
    ExtTextOutW(hdc, x, y, 0, NULL, text, (UINT) lstrlenW(text), NULL);
}

static LRESULT CALLBACK demo_window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc;
            RECT client;
            int margin;
            int gap;
            int image_y;
            int item_w;
            int item_h;
            int result;

            hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &client);
            FillRect(hdc, &client, (HBRUSH) GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            demo_text(hdc, 12, 10, L"positron_image ABI 1.0");
            demo_text(hdc, 12, 29,
                    L"Independent bitmap + SVG consumer");

            margin = 12;
            gap = 10;
            image_y = 55;
            item_w = (client.right - margin * 2 - gap) / 2;
            if (item_w > 140) {
                item_w = 140;
            }
            item_h = item_w;
            if (item_h > 100) {
                item_h = 100;
            }
            if (item_w < 24 || item_h < 24) {
                g_draw_error = PIMAGE_ERROR_DRAW;
            } else {
                result = PImage_DrawBitmap(g_bitmap, hdc, margin, image_y,
                        item_w, item_h);
                if (result == PIMAGE_OK) {
                    result = PImage_DrawSvg(g_svg, hdc,
                            margin + item_w + gap, image_y,
                            item_w, item_h);
                }
                if (result != PIMAGE_OK) {
                    g_draw_error = result;
                }
            }
            if (g_draw_error != PIMAGE_OK) {
                SetTextColor(hdc, RGB(192, 0, 0));
                demo_text(hdc, 12, image_y + item_h + 8,
                        L"PImage draw failed");
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static int demo_fail(const WCHAR *message)
{
    MessageBoxW(NULL, message, L"positron_image demo",
            MB_OK | MB_ICONERROR);
    return 1;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPWSTR command_line, int show_command)
{
    WNDCLASSW wc;
    HWND window;
    MSG message;
    int width;
    int height;
    int result;
    int exit_code;
    unsigned long abi_version;

    (void) previous;
    (void) command_line;
    (void) show_command;

    abi_version = PImage_GetAbiVersion();
    if (PIMAGE_ABI_VERSION_GET_MAJOR(abi_version) !=
            PIMAGE_ABI_VERSION_MAJOR ||
            PIMAGE_ABI_VERSION_GET_MINOR(abi_version) <
            PIMAGE_ABI_VERSION_MINOR) {
        return demo_fail(L"Unsupported positron_image ABI version");
    }
    result = PImage_CreateBitmapFromMemory((const char *) g_bitmap_data,
            (int) sizeof(g_bitmap_data), &g_bitmap);
    if (result != PIMAGE_OK) {
        return demo_fail(L"Could not create retained bitmap");
    }
    width = 0;
    height = 0;
    result = PImage_BitmapGetInfo(g_bitmap, &width, &height);
    if (result != PIMAGE_OK || width != 2 || height != 2) {
        PImage_FreeBitmap(g_bitmap);
        g_bitmap = NULL;
        return demo_fail(L"Retained bitmap dimensions are incorrect");
    }
    result = PImage_CreateSvgFromMemory(g_svg_data,
            (int) sizeof(g_svg_data) - 1, 120, 60, &g_svg);
    if (result != PIMAGE_OK) {
        PImage_FreeBitmap(g_bitmap);
        g_bitmap = NULL;
        return demo_fail(L"Could not create retained SVG");
    }
    width = 0;
    height = 0;
    result = PImage_SvgGetInfo(g_svg, &width, &height, NULL);
    if (result != PIMAGE_OK || width != 120 || height != 60) {
        PImage_FreeSvg(g_svg);
        PImage_FreeBitmap(g_bitmap);
        g_svg = NULL;
        g_bitmap = NULL;
        return demo_fail(L"Retained SVG dimensions are incorrect");
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = demo_window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"PositronImageDemoWindow";
    if (RegisterClassW(&wc) == 0) {
        PImage_FreeSvg(g_svg);
        PImage_FreeBitmap(g_bitmap);
        g_svg = NULL;
        g_bitmap = NULL;
        return demo_fail(L"Could not register demo window");
    }

    window = CreateWindowW(wc.lpszClassName, L"Positron image API",
            WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, instance, NULL);
    if (window == NULL) {
        PImage_FreeSvg(g_svg);
        PImage_FreeBitmap(g_bitmap);
        g_svg = NULL;
        g_bitmap = NULL;
        return demo_fail(L"Could not create demo window");
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetForegroundWindow(window);

    exit_code = 0;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_draw_error != PIMAGE_OK) {
        exit_code = demo_fail(L"Bitmap or SVG drawing failed");
    }
    PImage_FreeSvg(g_svg);
    PImage_FreeBitmap(g_bitmap);
    g_svg = NULL;
    g_bitmap = NULL;
    return exit_code;
}
