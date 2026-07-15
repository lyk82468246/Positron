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
#define DEMO_BGR_STRIDE (DEMO_BITMAP_SIZE * 3 + 4)
#define DEMO_BGRA_STRIDE (DEMO_BITMAP_SIZE * 4 + 8)
#define DEMO_BGR_REQUIRED \
    ((DEMO_BITMAP_SIZE - 1) * DEMO_BGR_STRIDE + DEMO_BITMAP_SIZE * 3)

static unsigned char g_bgr_data[DEMO_BGR_STRIDE * DEMO_BITMAP_SIZE];
static unsigned char g_bgra_data[DEMO_BGRA_STRIDE * DEMO_BITMAP_SIZE];

static const char g_svg_data[] =
    "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='60'>"
    "<rect width='40' height='60' fill='#ff0000'/>"
    "<rect x='40' width='40' height='60' fill='#00ff00'/>"
    "<path d='M88 55 C88 10 114 10 114 55' fill='none' "
    "stroke='#0000ff' stroke-width='5'/>"
    "</svg>";

static PIMAGE_BITMAP g_bgr_bitmap = NULL;
static PIMAGE_BITMAP g_bgra_bitmap = NULL;
static PIMAGE_BITMAP g_png_bitmap = NULL;
static PIMAGE_BITMAP g_png_alpha_bitmap = NULL;
static PIMAGE_BITMAP g_jpeg_bitmap = NULL;
static PIMAGE_SVG g_svg = NULL;
static int g_draw_error = PIMAGE_OK;

static void demo_pixel_color(int x, int y, unsigned char *red,
        unsigned char *green, unsigned char *blue)
{
    *red = 0;
    *green = 0;
    *blue = 0;
    if (y < DEMO_BITMAP_SIZE / 2) {
        if (x < DEMO_BITMAP_SIZE / 2) {
            *red = 255;
        } else {
            *green = 255;
        }
    } else if (x < DEMO_BITMAP_SIZE / 2) {
        *blue = 255;
    } else {
        *red = 255;
        *green = 255;
    }
}

static void demo_init_pixels(void)
{
    int y;
    int x;

    ZeroMemory(g_bgr_data, sizeof(g_bgr_data));
    ZeroMemory(g_bgra_data, sizeof(g_bgra_data));
    for (y = 0; y < DEMO_BITMAP_SIZE; y++) {
        for (x = 0; x < DEMO_BITMAP_SIZE; x++) {
            unsigned char red;
            unsigned char green;
            unsigned char blue;
            int bgr_offset;
            int bgra_offset;

            demo_pixel_color(x, y, &red, &green, &blue);
            bgr_offset = y * DEMO_BGR_STRIDE + x * 3;
            g_bgr_data[bgr_offset] = blue;
            g_bgr_data[bgr_offset + 1] = green;
            g_bgr_data[bgr_offset + 2] = red;
            bgra_offset = y * DEMO_BGRA_STRIDE + x * 4;
            g_bgra_data[bgra_offset] = blue;
            g_bgra_data[bgra_offset + 1] = green;
            g_bgra_data[bgra_offset + 2] = red;
            g_bgra_data[bgra_offset + 3] = 128;
        }
    }
}

static void demo_free_images(void)
{
    PImage_FreeSvg(g_svg);
    PImage_FreeBitmap(g_jpeg_bitmap);
    PImage_FreeBitmap(g_png_alpha_bitmap);
    PImage_FreeBitmap(g_png_bitmap);
    PImage_FreeBitmap(g_bgra_bitmap);
    PImage_FreeBitmap(g_bgr_bitmap);
    g_svg = NULL;
    g_jpeg_bitmap = NULL;
    g_png_alpha_bitmap = NULL;
    g_png_bitmap = NULL;
    g_bgra_bitmap = NULL;
    g_bgr_bitmap = NULL;
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

static int demo_draw_bitmap_item(HDC hdc, PIMAGE_BITMAP bitmap,
        int x, int y, int width, int height, const WCHAR *label)
{
    int size;
    int draw_x;

    demo_text(hdc, x, y, label);
    size = width;
    if (size > height - 16) {
        size = height - 16;
    }
    if (size > 64) {
        size = 64;
    }
    if (size < 16) {
        return PIMAGE_ERROR_DRAW;
    }
    draw_x = x + (width - size) / 2;
    return PImage_DrawBitmap(bitmap, hdc, draw_x, y + 16, size, size);
}

static int demo_draw_svg_item(HDC hdc, int x, int y, int width,
        int height, const WCHAR *label)
{
    int draw_width;
    int draw_height;
    int draw_x;

    demo_text(hdc, x, y, label);
    draw_width = width;
    if (draw_width > 96) {
        draw_width = 96;
    }
    draw_height = draw_width / 2;
    if (draw_height > height - 16) {
        draw_height = height - 16;
        draw_width = draw_height * 2;
    }
    if (draw_width < 24 || draw_height < 12) {
        return PIMAGE_ERROR_DRAW;
    }
    draw_x = x + (width - draw_width) / 2;
    return PImage_DrawSvg(g_svg, hdc, draw_x, y + 16,
            draw_width, draw_height);
}

static int demo_make_png_roundtrip(PIMAGE_BITMAP source,
        PIMAGE_BITMAP *out_bitmap)
{
    unsigned char *encoded;
    int encoded_len;
    int result;

    *out_bitmap = NULL;
    encoded = NULL;
    encoded_len = 0;
    result = PImage_EncodeBitmap(source, PIMAGE_ENCODE_PNG,
            &encoded, &encoded_len);
    if (result != PIMAGE_OK || encoded_len < 8 || encoded == NULL ||
            encoded[0] != 0x89 || encoded[1] != 'P' ||
            encoded[2] != 'N' || encoded[3] != 'G') {
        PImage_FreeBuffer(encoded);
        return 0;
    }
    result = PImage_CreateBitmapFromMemory((const char *) encoded,
            encoded_len, out_bitmap);
    PImage_FreeBuffer(encoded);
    return result == PIMAGE_OK && demo_bitmap_size_ok(*out_bitmap);
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
            int top;
            int columns;
            int rows;
            int slot_width;
            int slot_height;
            int x;
            int y;
            int result;

            hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &client);
            FillRect(hdc, &client, (HBRUSH) GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            demo_text(hdc, 8, 6, L"positron_image ABI 1.3");
            demo_text(hdc, 8, 23, L"Raw pixels + codec round-trips");

            margin = 8;
            gap = 5;
            top = 43;
            columns = client.right > client.bottom ? 3 : 2;
            rows = columns == 3 ? 2 : 3;
            slot_width = (client.right - margin * 2 -
                    gap * (columns - 1)) / columns;
            slot_height = (client.bottom - top - margin -
                    gap * (rows - 1)) / rows;
            if (slot_width < 24 || slot_height < 32) {
                g_draw_error = PIMAGE_ERROR_DRAW;
            } else {
                x = margin;
                y = top;
                result = demo_draw_bitmap_item(hdc, g_bgr_bitmap,
                        x, y, slot_width, slot_height, L"Raw BGR24");
                if (result == PIMAGE_OK) {
                    x = margin + slot_width + gap;
                    result = demo_draw_bitmap_item(hdc, g_bgra_bitmap,
                            x, y, slot_width, slot_height, L"Raw BGRA alpha");
                }
                if (result == PIMAGE_OK) {
                    x = columns == 3 ? margin +
                            (slot_width + gap) * 2 : margin;
                    y = columns == 3 ? top : top + slot_height + gap;
                    result = demo_draw_bitmap_item(hdc, g_png_bitmap,
                            x, y, slot_width, slot_height, L"PNG RGB");
                }
                if (result == PIMAGE_OK) {
                    x = columns == 3 ? margin :
                            margin + slot_width + gap;
                    y = top + slot_height + gap;
                    result = demo_draw_bitmap_item(hdc,
                            g_png_alpha_bitmap, x, y,
                            slot_width, slot_height, L"PNG alpha");
                }
                if (result == PIMAGE_OK) {
                    x = columns == 3 ? margin + slot_width + gap : margin;
                    y = columns == 3 ? top + slot_height + gap :
                            top + (slot_height + gap) * 2;
                    result = demo_draw_bitmap_item(hdc, g_jpeg_bitmap,
                            x, y, slot_width, slot_height, L"JPEG 4:4:4");
                }
                if (result == PIMAGE_OK) {
                    x = columns == 3 ? margin +
                            (slot_width + gap) * 2 :
                            margin + slot_width + gap;
                    y = columns == 3 ? top + slot_height + gap :
                            top + (slot_height + gap) * 2;
                    result = demo_draw_svg_item(hdc, x, y,
                            slot_width, slot_height, L"Retained SVG");
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

    demo_init_pixels();
    abi_version = PImage_GetAbiVersion();
    if (PIMAGE_ABI_VERSION_GET_MAJOR(abi_version) !=
            PIMAGE_ABI_VERSION_MAJOR ||
            PIMAGE_ABI_VERSION_GET_MINOR(abi_version) <
            PIMAGE_ABI_VERSION_MINOR) {
        return demo_fail(L"Unsupported positron_image ABI version");
    }
    result = PImage_CreateBitmapFromPixels(g_bgr_data,
            DEMO_BGR_REQUIRED - 1, DEMO_BITMAP_SIZE, DEMO_BITMAP_SIZE,
            DEMO_BGR_STRIDE, PIMAGE_PIXEL_BGR24, &g_bgr_bitmap);
    if (result != PIMAGE_ERROR_ARGUMENT || g_bgr_bitmap != NULL) {
        demo_free_images();
        return demo_fail(L"Raw pixel length validation failed");
    }
    result = PImage_CreateBitmapFromPixels(g_bgr_data,
            (int) sizeof(g_bgr_data), DEMO_BITMAP_SIZE, DEMO_BITMAP_SIZE,
            DEMO_BGR_STRIDE, PIMAGE_PIXEL_BGR24, &g_bgr_bitmap);
    if (result != PIMAGE_OK || !demo_bitmap_size_ok(g_bgr_bitmap)) {
        demo_free_images();
        return demo_fail(L"Could not create BGR24 bitmap");
    }
    ZeroMemory(g_bgr_data, sizeof(g_bgr_data));
    result = PImage_CreateBitmapFromPixels(g_bgra_data,
            (int) sizeof(g_bgra_data), DEMO_BITMAP_SIZE, DEMO_BITMAP_SIZE,
            DEMO_BGRA_STRIDE, PIMAGE_PIXEL_BGRA32, &g_bgra_bitmap);
    if (result != PIMAGE_OK || !demo_bitmap_size_ok(g_bgra_bitmap)) {
        demo_free_images();
        return demo_fail(L"Could not create BGRA32 bitmap");
    }
    ZeroMemory(g_bgra_data, sizeof(g_bgra_data));

    if (!demo_make_png_roundtrip(g_bgr_bitmap, &g_png_bitmap)) {
        demo_free_images();
        return demo_fail(L"Native RGB PNG round-trip failed");
    }
    if (!demo_make_png_roundtrip(g_bgra_bitmap, &g_png_alpha_bitmap)) {
        demo_free_images();
        return demo_fail(L"Native alpha PNG round-trip failed");
    }

    encoded = NULL;
    encoded_len = 0;
    result = PImage_EncodeBitmapEx(g_bgr_bitmap, PIMAGE_ENCODE_JPEG, 100,
            &encoded, &encoded_len);
    if (result != PIMAGE_OK || encoded_len < 4 || encoded == NULL ||
            encoded[0] != 0xff ||
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
