/*
 * positron_image_demo - independent Windows Mobile image DLL consumer.
 *
 * This program intentionally includes and links only positron_image. It is a
 * small integration example for third-party WM C applications, not a browser
 * or test_host component.
 */

#include <windows.h>

#include "positron_image.h"

#define DEMO_BITMAP_SIZE 16
#define DEMO_BITMAP_BYTES (54 + DEMO_BITMAP_SIZE * DEMO_BITMAP_SIZE * 3)

static unsigned char g_bitmap_data[DEMO_BITMAP_BYTES];

static const char g_svg_data[] =
    "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='60'>"
    "<rect width='40' height='60' fill='#ff0000'/>"
    "<rect x='40' width='40' height='60' fill='#00ff00'/>"
    "<path d='M88 55 C88 10 114 10 114 55' fill='none' "
    "stroke='#0000ff' stroke-width='5'/>"
    "</svg>";

static PIMAGE_BITMAP g_bitmap = NULL;
static PIMAGE_BITMAP g_png_bitmap = NULL;
static PIMAGE_BITMAP g_jpeg_bitmap = NULL;
static PIMAGE_SVG g_svg = NULL;
static int g_draw_error = PIMAGE_OK;

static void demo_put_u16(unsigned char *data, int offset,
        unsigned int value)
{
    data[offset] = (unsigned char) (value & 0xffU);
    data[offset + 1] = (unsigned char) ((value >> 8) & 0xffU);
}

static void demo_put_u32(unsigned char *data, int offset,
        unsigned long value)
{
    data[offset] = (unsigned char) (value & 0xffUL);
    data[offset + 1] = (unsigned char) ((value >> 8) & 0xffUL);
    data[offset + 2] = (unsigned char) ((value >> 16) & 0xffUL);
    data[offset + 3] = (unsigned char) ((value >> 24) & 0xffUL);
}

static void demo_init_bitmap(void)
{
    int file_y;
    int x;

    ZeroMemory(g_bitmap_data, sizeof(g_bitmap_data));
    g_bitmap_data[0] = 'B';
    g_bitmap_data[1] = 'M';
    demo_put_u32(g_bitmap_data, 2, (unsigned long) sizeof(g_bitmap_data));
    demo_put_u32(g_bitmap_data, 10, 54);
    demo_put_u32(g_bitmap_data, 14, 40);
    demo_put_u32(g_bitmap_data, 18, DEMO_BITMAP_SIZE);
    demo_put_u32(g_bitmap_data, 22, DEMO_BITMAP_SIZE);
    demo_put_u16(g_bitmap_data, 26, 1);
    demo_put_u16(g_bitmap_data, 28, 24);
    demo_put_u32(g_bitmap_data, 34,
            DEMO_BITMAP_SIZE * DEMO_BITMAP_SIZE * 3);
    demo_put_u32(g_bitmap_data, 38, 3780);
    demo_put_u32(g_bitmap_data, 42, 3780);
    for (file_y = 0; file_y < DEMO_BITMAP_SIZE; file_y++) {
        int visual_y;

        visual_y = DEMO_BITMAP_SIZE - 1 - file_y;
        for (x = 0; x < DEMO_BITMAP_SIZE; x++) {
            unsigned char red;
            unsigned char green;
            unsigned char blue;
            int offset;

            red = 0;
            green = 0;
            blue = 0;
            if (visual_y < DEMO_BITMAP_SIZE / 2) {
                if (x < DEMO_BITMAP_SIZE / 2) {
                    red = 255;
                } else {
                    green = 255;
                }
            } else if (x < DEMO_BITMAP_SIZE / 2) {
                blue = 255;
            } else {
                red = 255;
                green = 255;
            }
            offset = 54 + (file_y * DEMO_BITMAP_SIZE + x) * 3;
            g_bitmap_data[offset] = blue;
            g_bitmap_data[offset + 1] = green;
            g_bitmap_data[offset + 2] = red;
        }
    }
}

static void demo_free_images(void)
{
    PImage_FreeSvg(g_svg);
    PImage_FreeBitmap(g_jpeg_bitmap);
    PImage_FreeBitmap(g_png_bitmap);
    PImage_FreeBitmap(g_bitmap);
    g_svg = NULL;
    g_jpeg_bitmap = NULL;
    g_png_bitmap = NULL;
    g_bitmap = NULL;
}

static int demo_bitmap_size_ok(PIMAGE_BITMAP bitmap)
{
    int width;
    int height;

    width = 0;
    height = 0;
    return PImage_BitmapGetInfo(bitmap, &width, &height) == PIMAGE_OK &&
            width == DEMO_BITMAP_SIZE && height == DEMO_BITMAP_SIZE;
}

static int demo_jpeg_is_444(const unsigned char *data, int len)
{
    int offset;

    if (data == NULL || len < 4 || data[0] != 0xff || data[1] != 0xd8) {
        return 0;
    }
    offset = 2;
    while (offset + 4 <= len) {
        int marker;
        int segment_len;

        if (data[offset] != 0xff) {
            return 0;
        }
        while (offset < len && data[offset] == 0xff) {
            offset++;
        }
        if (offset >= len) {
            return 0;
        }
        marker = data[offset++];
        if (marker == 0xd9 || marker == 0xda) {
            return 0;
        }
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            continue;
        }
        if (offset + 2 > len) {
            return 0;
        }
        segment_len = ((int) data[offset] << 8) | data[offset + 1];
        if (segment_len < 2 || offset + segment_len > len) {
            return 0;
        }
        if (marker == 0xc0) {
            int components;

            if (segment_len < 17) {
                return 0;
            }
            components = data[offset + 7];
            return components == 3 && data[offset + 9] == 0x11 &&
                    data[offset + 12] == 0x11 &&
                    data[offset + 15] == 0x11;
        }
        offset += segment_len;
    }
    return 0;
}

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
            int row_height;
            int row1_y;
            int row2_y;
            int item_w;
            int bitmap_size;
            int left_x;
            int right_x;
            int result;

            hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &client);
            FillRect(hdc, &client, (HBRUSH) GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            demo_text(hdc, 12, 10, L"positron_image ABI 1.2");
            demo_text(hdc, 12, 29,
                    L"Native PNG/JPEG memory round-trip");

            margin = 12;
            gap = 10;
            item_w = (client.right - margin * 2 - gap) / 2;
            if (item_w > 140) {
                item_w = 140;
            }
            row_height = (client.bottom - 90) / 2;
            if (row_height > 80) {
                row_height = 80;
            }
            bitmap_size = item_w;
            if (bitmap_size > row_height) {
                bitmap_size = row_height;
            }
            left_x = margin + (item_w - bitmap_size) / 2;
            right_x = margin + item_w + gap + (item_w - bitmap_size) / 2;
            row1_y = 65;
            row2_y = row1_y + row_height + 20;
            demo_text(hdc, margin, 49, L"Source BMP");
            demo_text(hdc, margin + item_w + gap, 49, L"PNG round-trip");
            demo_text(hdc, margin, row2_y - 16, L"JPEG 4:4:4");
            demo_text(hdc, margin + item_w + gap, row2_y - 16,
                    L"Retained SVG");
            if (item_w < 24 || row_height < 24 || bitmap_size < 24) {
                g_draw_error = PIMAGE_ERROR_DRAW;
            } else {
                result = PImage_DrawBitmap(g_bitmap, hdc, left_x, row1_y,
                        bitmap_size, bitmap_size);
                if (result == PIMAGE_OK) {
                    result = PImage_DrawBitmap(g_png_bitmap, hdc, right_x,
                            row1_y, bitmap_size, bitmap_size);
                }
                if (result == PIMAGE_OK) {
                    result = PImage_DrawBitmap(g_jpeg_bitmap, hdc, left_x,
                            row2_y, bitmap_size, bitmap_size);
                }
                if (result == PIMAGE_OK) {
                    result = PImage_DrawSvg(g_svg, hdc,
                            margin + item_w + gap, row2_y,
                            item_w, row_height);
                }
                if (result != PIMAGE_OK) {
                    g_draw_error = result;
                }
            }
            if (g_draw_error != PIMAGE_OK) {
                SetTextColor(hdc, RGB(192, 0, 0));
                demo_text(hdc, 12, client.bottom - 18,
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
    unsigned char *encoded;
    int encoded_len;

    (void) previous;
    (void) command_line;
    (void) show_command;

    demo_init_bitmap();
    abi_version = PImage_GetAbiVersion();
    if (PIMAGE_ABI_VERSION_GET_MAJOR(abi_version) !=
            PIMAGE_ABI_VERSION_MAJOR ||
            PIMAGE_ABI_VERSION_GET_MINOR(abi_version) <
            PIMAGE_ABI_VERSION_MINOR) {
        return demo_fail(L"Unsupported positron_image ABI version");
    }
    result = PImage_CreateBitmapFromMemory((const char *) g_bitmap_data,
            (int) sizeof(g_bitmap_data), &g_bitmap);
    if (result != PIMAGE_OK || !demo_bitmap_size_ok(g_bitmap)) {
        demo_free_images();
        return demo_fail(L"Could not create source bitmap");
    }

    encoded = NULL;
    encoded_len = 0;
    result = PImage_EncodeBitmap(g_bitmap, PIMAGE_ENCODE_PNG,
            &encoded, &encoded_len);
    if (result != PIMAGE_OK || encoded_len < 8 || encoded[0] != 0x89 ||
            encoded[1] != 'P' || encoded[2] != 'N' || encoded[3] != 'G') {
        PImage_FreeBuffer(encoded);
        demo_free_images();
        return demo_fail(L"Native PNG memory encoding failed");
    }
    result = PImage_CreateBitmapFromMemory((const char *) encoded,
            encoded_len, &g_png_bitmap);
    PImage_FreeBuffer(encoded);
    encoded = NULL;
    if (result != PIMAGE_OK || !demo_bitmap_size_ok(g_png_bitmap)) {
        demo_free_images();
        return demo_fail(L"Could not decode encoded PNG");
    }

    encoded_len = 0;
    result = PImage_EncodeBitmapEx(g_bitmap, PIMAGE_ENCODE_JPEG, 100,
            &encoded, &encoded_len);
    if (result != PIMAGE_OK || encoded_len < 4 || encoded[0] != 0xff ||
            encoded[1] != 0xd8 || encoded[encoded_len - 2] != 0xff ||
            encoded[encoded_len - 1] != 0xd9) {
        PImage_FreeBuffer(encoded);
        demo_free_images();
        return demo_fail(L"Native JPEG memory encoding failed");
    }
    if (!demo_jpeg_is_444(encoded, encoded_len)) {
        PImage_FreeBuffer(encoded);
        demo_free_images();
        return demo_fail(L"JPEG output is not 4:4:4");
    }
    result = PImage_CreateBitmapFromMemory((const char *) encoded,
            encoded_len, &g_jpeg_bitmap);
    PImage_FreeBuffer(encoded);
    encoded = NULL;
    if (result != PIMAGE_OK || !demo_bitmap_size_ok(g_jpeg_bitmap)) {
        demo_free_images();
        return demo_fail(L"Could not decode encoded JPEG");
    }

    result = PImage_CreateSvgFromMemory(g_svg_data,
            (int) sizeof(g_svg_data) - 1, 120, 60, &g_svg);
    if (result != PIMAGE_OK) {
        demo_free_images();
        return demo_fail(L"Could not create retained SVG");
    }
    width = 0;
    height = 0;
    result = PImage_SvgGetInfo(g_svg, &width, &height, NULL);
    if (result != PIMAGE_OK || width != 120 || height != 60) {
        demo_free_images();
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
        demo_free_images();
        return demo_fail(L"Could not register demo window");
    }

    window = CreateWindowW(wc.lpszClassName, L"Positron image API",
            WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, instance, NULL);
    if (window == NULL) {
        demo_free_images();
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
    demo_free_images();
    return exit_code;
}
