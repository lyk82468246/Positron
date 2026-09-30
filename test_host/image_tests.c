/*
 * test_host/image_tests.c - offline image-resource callback contracts.
 *
 * This fixture is a consumer-side regression for the versioned Core image
 * fetch entry point.  It deliberately separates a resource that is pending
 * from one that is terminally unavailable, then proves that a later scan can
 * decode the pending SVG without retrying the terminal failure.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "positron_core.h"
#include "positron_image.h"

typedef struct image_pending_retry_fixture {
    int calls;
    int pending_calls;
    int terminal_calls;
    int frees;
} image_pending_retry_fixture;

static const char g_pending_retry_svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" "
        "height=\"60\"><rect width=\"120\" height=\"60\" "
        "fill=\"#2040a0\"/></svg>";

static int image_pending_retry_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    image_pending_retry_fixture *fixture;
    char *copy;
    int len;

    fixture = (image_pending_retry_fixture *) pw;
    if (fixture == NULL || url == NULL || out_data == NULL ||
            out_len == NULL) {
        return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
    }
    *out_data = NULL;
    *out_len = 0;
    fixture->calls++;
    if (strcmp(url, "/img/pending.svg") == 0) {
        fixture->pending_calls++;
        if (fixture->pending_calls == 1) {
            return PCORE_IMAGE_FETCH_PENDING;
        }
        len = (int) sizeof(g_pending_retry_svg) - 1;
        copy = (char *) malloc((size_t) len);
        if (copy == NULL) {
            return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
        }
        memcpy(copy, g_pending_retry_svg, (size_t) len);
        *out_data = copy;
        *out_len = len;
        return PCORE_IMAGE_FETCH_READY;
    }
    if (strcmp(url, "/img/terminal.svg") == 0) {
        fixture->terminal_calls++;
        return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
    }
    return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
}

static void image_pending_retry_free(void *pw, char *data)
{
    image_pending_retry_fixture *fixture;

    fixture = (image_pending_retry_fixture *) pw;
    if (fixture != NULL) {
        fixture->frees++;
    }
    free(data);
}

/* TEST 1313 - asynchronous image pending/retry and terminal failure. */
BOOL test1313_core_image_pending_retry_contract(void)
{
    static const char HTML[] =
            "<!doctype html><html><body>"
            "<img id='pending' src='/img/pending.svg' alt='pending'>"
            "<img id='terminal' src='/img/terminal.svg' alt='terminal'>"
            "</body></html>";
    static const char CSS[] =
            "html,body{margin:0;padding:0;}"
            "img{display:block;width:120px;height:60px;}";
    HANDLE document;
    HANDLE sheet;
    image_pending_retry_fixture fixture;
    PCoreImageDecodeStats image_stats;
    int found;
    int fetched;
    int number;
    int x;
    int y;
    int width;
    int height;
    BOOL ok;

    document = NULL;
    sheet = NULL;
    memset(&fixture, 0, sizeof(fixture));
    memset(&image_stats, 0, sizeof(image_stats));
    found = 0;
    fetched = 0;
    number = 0;
    x = 0;
    y = 0;
    width = 0;
    height = 0;
    ok = FALSE;

    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    if (document == NULL) {
        goto cleanup;
    }
    if (PCore_FetchImageResourcesEx(document, image_pending_retry_fetch,
            image_pending_retry_free, &fixture, &found, &fetched) != 0 ||
            found != 2 || fetched != 0 || fixture.calls != 2 ||
            fixture.pending_calls != 1 || fixture.terminal_calls != 1 ||
            fixture.frees != 0) {
        goto cleanup;
    }
    if (PCore_NodeRelationById(document, "pending",
            PCORE_NODE_RELATION_IMAGE_COMPLETE, 0, NULL, 0, NULL,
            &number) != 0 || number != 0 ||
            PCore_NodeRelationById(document, "terminal",
            PCORE_NODE_RELATION_IMAGE_COMPLETE, 0, NULL, 0, NULL,
            &number) != 0 || number != 1) {
        goto cleanup;
    }

    /* The worker has completed between scans; the pending URL must be
     * requested again while the terminal URL remains cached as failed. */
    if (PCore_FetchImageResourcesEx(document, image_pending_retry_fetch,
            image_pending_retry_free, &fixture, &found, &fetched) != 0 ||
            found != 2 || fetched != 1 || fixture.calls != 3 ||
            fixture.pending_calls != 2 || fixture.terminal_calls != 1 ||
            fixture.frees != 1) {
        goto cleanup;
    }
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "https://positron.local/image-pending.css");
    if (sheet == NULL || PCore_StyleDocument(document, sheet) != 0) {
        goto cleanup;
    }
    PCore_SetViewport(240, 160, 96);
    if (PCore_LayoutDocument(document, 240, 160) != 0 ||
            PCore_NodeBox(document, "img", &x, &y, &width, &height) != 0 ||
            width != 120 || height != 60 ||
            PCore_NodeRelationById(document, "pending",
            PCORE_NODE_RELATION_IMAGE_NATURAL_WIDTH, 0, NULL, 0, NULL,
            &number) != 0 || number != 120 ||
            PCore_NodeRelationById(document, "pending",
            PCORE_NODE_RELATION_IMAGE_COMPLETE, 0, NULL, 0, NULL,
            &number) != 0 || number != 1 ||
            PCore_GetImageDecodeStats(document, &image_stats) != 0 ||
            image_stats.svg_creates == 0) {
        goto cleanup;
    }
    if (PCore_FetchImageResourcesEx(document, image_pending_retry_fetch,
            image_pending_retry_free, &fixture, &found, &fetched) != 0 ||
            found != 2 || fetched != 1 || fixture.calls != 3 ||
            fixture.pending_calls != 2 || fixture.terminal_calls != 1 ||
            fixture.frees != 1) {
        goto cleanup;
    }
    ok = TRUE;

cleanup:
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    return ok;
}

static char g_test1314_failure[256];

const char *test1314_iana_svg_last_error(void)
{
    return g_test1314_failure;
}

static int test1314_color_close(COLORREF actual, COLORREF expected)
{
    return abs((int) GetRValue(actual) - (int) GetRValue(expected)) <= 16 &&
            abs((int) GetGValue(actual) - (int) GetGValue(expected)) <= 16 &&
            abs((int) GetBValue(actual) - (int) GetBValue(expected)) <= 16;
}

/* TEST 1314 - direct positron_image SVG style/class and gradient probe.
 * These are minimized offline fixtures derived from the current IANA
 * homepage/header SVGs (the snapshots are kept locally under tmp/). They
 * retain the constructs that matter for the reported failure: <style> class
 * rules, green/blue logo fills, a gradient, viewBox sizing, the default
 * preserveAspectRatio path, omitted width/height and repeated retained
 * draws. */
BOOL test1314_iana_svg_direct_render(void)
{
    static const char HOMEPAGE_SVG[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "viewBox=\"0 0 90 40\" preserveAspectRatio=\"xMidYMid meet\">"
            "<style>.st0{fill:#11A14E}.st1{fill:#0673BA}"
            ".st2{fill:url(#g1)}</style>"
            "<defs><linearGradient id=\"g1\" x1=\"0\" y1=\"0\" "
            "x2=\"1\" y2=\"0\"><stop offset=\"0\" "
            "style=\"stop-color:#0673BA\"/><stop offset=\"1\" "
            "style=\"stop-color:#11A14E\"/></linearGradient></defs>"
            "<path class=\"st0\" d=\"M0 0h30v40H0z\"/>"
            "<path class=\"st1\" d=\"M30 0h30v40H30z\"/>"
            "<path class=\"st2\" d=\"M60 0h30v40H60z\"/>"
            "</svg>";
    static const char HEADER_SVG[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "viewBox=\"0 0 90 30\" preserveAspectRatio=\"xMidYMid meet\">"
            "<style>.st2{fill:#11A14E}.st3{fill:#0673BA}</style>"
            "<g id=\"Logo\"><path class=\"st2\" "
            "d=\"M0 0h45v30H0z\"/><path class=\"st3\" "
            "d=\"M45 0h45v30H45z\"/></g></svg>";
    static const char BROKEN_SVG[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\"><path";
    PIMAGE_SVG homepage;
    PIMAGE_SVG header;
    PIMAGE_SVG broken;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF green;
    COLORREF blue;
    COLORREF gradient;
    COLORREF header_green;
    COLORREF header_blue;
    int rc_homepage;
    int rc_header;
    int rc_broken;
    int width;
    int height;
    unsigned int shapes;
    BOOL ok;

    strcpy(g_test1314_failure, "not run");
    homepage = NULL;
    header = NULL;
    broken = NULL;
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    rc_homepage = PImage_CreateSvgFromMemory(HOMEPAGE_SVG,
            (int) sizeof(HOMEPAGE_SVG) - 1, 90, 40, &homepage);
    rc_header = PImage_CreateSvgFromMemory(HEADER_SVG,
            (int) sizeof(HEADER_SVG) - 1, 90, 30, &header);
    rc_broken = PImage_CreateSvgFromMemory(BROKEN_SVG,
            (int) sizeof(BROKEN_SVG) - 1, 90, 30, &broken);
    ok = rc_homepage == PIMAGE_OK && homepage != NULL &&
            rc_header == PIMAGE_OK && header != NULL &&
            rc_broken != PIMAGE_OK && broken == NULL;
    if (!ok) {
        _snprintf(g_test1314_failure, sizeof(g_test1314_failure) - 1,
                "create rc=%d/%d/%d handles=%d/%d/%d", rc_homepage,
                rc_header, rc_broken, homepage != NULL, header != NULL,
                broken != NULL);
        g_test1314_failure[sizeof(g_test1314_failure) - 1] = '\0';
    }
    if (ok) {
        if (PImage_SvgGetInfo(homepage, &width, &height, &shapes) !=
                PIMAGE_OK || width != 90 || height != 40 || shapes != 3 ||
                PImage_SvgGetInfo(header, &width, &height, &shapes) !=
                PIMAGE_OK || width != 90 || height != 30 || shapes != 2) {
            ok = FALSE;
            _snprintf(g_test1314_failure, sizeof(g_test1314_failure) - 1,
                    "info width=%d height=%d shapes=%u", width, height,
                    shapes);
            g_test1314_failure[sizeof(g_test1314_failure) - 1] = '\0';
        }
    }
    if (ok) {
        screen_dc = GetDC(NULL);
        memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
        bitmap = (screen_dc != NULL) ?
                CreateCompatibleBitmap(screen_dc, 180, 80) : NULL;
        if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
            ok = FALSE;
            strcpy(g_test1314_failure, "offscreen GDI allocation failed");
        }
    }
    if (ok) {
        old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
        SetRect(&rect, 0, 0, 180, 80);
        FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
        if (PImage_DrawSvg(homepage, memory_dc, 0, 0, 180, 80) !=
                PIMAGE_OK || PImage_DrawSvg(homepage, memory_dc, 0, 0,
                180, 80) != PIMAGE_OK) {
            ok = FALSE;
            strcpy(g_test1314_failure, "homepage draw failed");
        }
        green = GetPixel(memory_dc, 30, 40);
        blue = GetPixel(memory_dc, 90, 40);
        gradient = GetPixel(memory_dc, 150, 40);
        FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
        if (PImage_DrawSvg(header, memory_dc, 0, 0, 180, 60) !=
                PIMAGE_OK) {
            ok = FALSE;
            strcpy(g_test1314_failure, "header draw failed");
        }
        header_green = GetPixel(memory_dc, 45, 30);
        header_blue = GetPixel(memory_dc, 135, 30);
        if (!test1314_color_close(green, RGB(17, 161, 78)) ||
                !test1314_color_close(blue, RGB(6, 115, 186)) ||
                (GetRValue(gradient) == 0 && GetGValue(gradient) == 0 &&
                GetBValue(gradient) == 0) ||
                !test1314_color_close(header_green, RGB(17, 161, 78)) ||
                !test1314_color_close(header_blue, RGB(6, 115, 186))) {
            ok = FALSE;
            _snprintf(g_test1314_failure, sizeof(g_test1314_failure) - 1,
                    "pixels home=%08lx/%08lx/%08lx header=%08lx/%08lx",
                    (unsigned long) green, (unsigned long) blue,
                    (unsigned long) gradient, (unsigned long) header_green,
                    (unsigned long) header_blue);
            g_test1314_failure[sizeof(g_test1314_failure) - 1] = '\0';
        }
        SelectObject(memory_dc, old_bitmap);
        old_bitmap = NULL;
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (homepage != NULL) {
        PImage_FreeSvg(homepage);
    }
    if (header != NULL) {
        PImage_FreeSvg(header);
    }
    if (broken != NULL) {
        PImage_FreeSvg(broken);
    }
    if (!ok) {
        return FALSE;
    }
    strcpy(g_test1314_failure, "ok");
    return TRUE;
}

#define TEST1315_FIXTURE_MAX_BYTES 65536

typedef struct test1315_pixels {
    unsigned long nonwhite;
    unsigned long green;
    unsigned long blue;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
} test1315_pixels;

static char g_test1315_failure[320];

const char *test1315_iana_svg_last_error(void)
{
    return g_test1315_failure;
}

static int test1315_sibling_path(const WCHAR *name, WCHAR path[MAX_PATH])
{
    DWORD path_len;

    path_len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (path_len == 0 || path_len >= MAX_PATH) {
        return 0;
    }
    while (path_len > 0 && path[path_len - 1] != L'\\' &&
            path[path_len - 1] != L'/') {
        path_len--;
    }
    if (path_len == 0 || path_len + (DWORD) lstrlenW(name) >= MAX_PATH) {
        return 0;
    }
    lstrcpyW(path + path_len, name);
    return 1;
}

static int test1315_read_fixture(const WCHAR *name, char **out_data,
        int *out_len)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD size;
    DWORD read_count;
    char *data;

    if (out_data == NULL || out_len == NULL) {
        return 0;
    }
    *out_data = NULL;
    *out_len = 0;
    if (!test1315_sibling_path(name, path)) {
        return 0;
    }
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 ||
            size > TEST1315_FIXTURE_MAX_BYTES) {
        CloseHandle(file);
        return 0;
    }
    data = (char *) malloc((size_t) size + 1);
    if (data == NULL) {
        CloseHandle(file);
        return 0;
    }
    read_count = 0;
    if (!ReadFile(file, data, size, &read_count, NULL) ||
            read_count != size) {
        free(data);
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    data[size] = '\0';
    *out_data = data;
    *out_len = (int) size;
    return 1;
}

static void test1315_pixels_reset(test1315_pixels *pixels)
{
    pixels->nonwhite = 0;
    pixels->green = 0;
    pixels->blue = 0;
    pixels->min_x = 0x7fffffff;
    pixels->min_y = 0x7fffffff;
    pixels->max_x = -1;
    pixels->max_y = -1;
}

static int test1315_draw_metrics(PIMAGE_SVG svg, int width, int height,
        test1315_pixels *pixels)
{
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF color;
    int x;
    int y;
    int red;
    int green;
    int blue;
    int rc;

    if (svg == NULL || pixels == NULL || width <= 0 || height <= 0) {
        return 0;
    }
    test1315_pixels_reset(pixels);
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, width, height) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) {
            DeleteObject(bitmap);
        }
        if (memory_dc != NULL) {
            DeleteDC(memory_dc);
        }
        if (screen_dc != NULL) {
            ReleaseDC(NULL, screen_dc);
        }
        return 0;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, width, height);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, width, height);
    if (rc == PIMAGE_OK) {
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                color = GetPixel(memory_dc, x, y);
                if (color == CLR_INVALID) {
                    continue;
                }
                red = (int) GetRValue(color);
                green = (int) GetGValue(color);
                blue = (int) GetBValue(color);
                if (red < 248 || green < 248 || blue < 248) {
                    pixels->nonwhite++;
                    if (x < pixels->min_x) { pixels->min_x = x; }
                    if (y < pixels->min_y) { pixels->min_y = y; }
                    if (x > pixels->max_x) { pixels->max_x = x; }
                    if (y > pixels->max_y) { pixels->max_y = y; }
                }
                if (green > red + 25 && green > blue + 15) {
                    pixels->green++;
                }
                if (blue > red + 25 && blue > green + 20) {
                    pixels->blue++;
                }
            }
        }
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    return rc == PIMAGE_OK;
}

static int test1315_pixels_same(const test1315_pixels *left,
        const test1315_pixels *right)
{
    return left->nonwhite == right->nonwhite &&
            left->green == right->green && left->blue == right->blue &&
            left->min_x == right->min_x && left->min_y == right->min_y &&
            left->max_x == right->max_x && left->max_y == right->max_y;
}

/* TEST 1315 - exact IANA SVG regression. The files are the complete offline
 * snapshots, not reduced fixtures: XML prolog/DOCTYPE, the 450x175 homepage
 * viewBox, the header's negative viewBox, all class rules and six gradients,
 * the hidden Text_Paths group, repeated parse/draw and release/reparse are
 * intentionally retained. */
BOOL test1315_iana_svg_exact_render(void)
{
    char *homepage_data;
    char *header_data;
    int homepage_len;
    int header_len;
    int homepage_rc;
    int header_rc;
    int homepage_again_rc;
    int header_again_rc;
    PIMAGE_SVG homepage;
    PIMAGE_SVG header;
    PIMAGE_SVG homepage_again;
    PIMAGE_SVG header_again;
    PIMAGE_SVG homepage_default;
    PIMAGE_SVG header_default;
    test1315_pixels homepage_pixels;
    test1315_pixels homepage_repeat;
    test1315_pixels homepage_reparse;
    test1315_pixels homepage_default_pixels;
    test1315_pixels header_default_pixels;
    test1315_pixels header_pixels;
    test1315_pixels header_repeat;
    test1315_pixels header_reparse;
    unsigned int homepage_shapes;
    unsigned int header_shapes;
    int homepage_width;
    int homepage_height;
    int header_width;
    int header_height;
    int homepage_default_rc;
    int header_default_rc;
    int homepage_default_width;
    int homepage_default_height;
    int header_default_width;
    int header_default_height;
    int ok;

    strcpy(g_test1315_failure, "not run");
    homepage_data = NULL;
    header_data = NULL;
    homepage = NULL;
    header = NULL;
    homepage_again = NULL;
    header_again = NULL;
    homepage_default = NULL;
    header_default = NULL;
    homepage_len = 0;
    header_len = 0;
    homepage_shapes = 0;
    header_shapes = 0;
    homepage_width = 0;
    homepage_height = 0;
    header_width = 0;
    header_height = 0;
    homepage_default_rc = PIMAGE_ERROR_ARGUMENT;
    header_default_rc = PIMAGE_ERROR_ARGUMENT;
    homepage_default_width = 0;
    homepage_default_height = 0;
    header_default_width = 0;
    header_default_height = 0;
    ok = test1315_read_fixture(
            L"fixtures\\iana-logo-homepage.svg", &homepage_data,
            &homepage_len) && test1315_read_fixture(
            L"fixtures\\iana-logo-header-notext.svg", &header_data,
            &header_len);
    if (!ok) {
        strcpy(g_test1315_failure, "exact IANA fixture read failed");
    }
    if (ok && (homepage_len < 7000 || header_len < 22000 ||
            memcmp(homepage_data, "<?xml version=\"1.0\"", 19) != 0 ||
            memcmp(header_data, "<?xml version=\"1.0\"", 19) != 0)) {
        ok = 0;
        _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                "fixture sizes=%d/%d or XML prolog missing", homepage_len,
                header_len);
        g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
    }
    if (ok) {
        homepage_rc = PImage_CreateSvgFromMemory(homepage_data,
                homepage_len, 450, 175, &homepage);
        header_rc = PImage_CreateSvgFromMemory(header_data, header_len,
                256, 100, &header);
        ok = homepage_rc == PIMAGE_OK && homepage != NULL &&
                header_rc == PIMAGE_OK && header != NULL;
        if (!ok) {
            _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                    "create rc=%d/%d handles=%d/%d", homepage_rc,
                    header_rc, homepage != NULL, header != NULL);
            g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
        }
    }
    if (ok) {
        ok = PImage_SvgGetInfo(homepage, &homepage_width,
                &homepage_height, &homepage_shapes) == PIMAGE_OK &&
                PImage_SvgGetInfo(header, &header_width, &header_height,
                &header_shapes) == PIMAGE_OK && homepage_width == 450 &&
                homepage_height == 175 && header_width == 256 &&
                header_height == 100 && homepage_shapes == 8 &&
                header_shapes == 8;
        if (!ok) {
            _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                    "info home=%dx%d/%u header=%dx%d/%u",
                    homepage_width, homepage_height, homepage_shapes,
                    header_width, header_height, header_shapes);
            g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
        }
    }
    if (ok) {
        /* Core intentionally passes zero viewport dimensions for retained
         * SVG resources.  The exact assets have no width/height attributes,
         * so the image DLL must use each root viewBox as the natural canvas;
         * otherwise a CSS background repeats an oversized tile. */
        homepage_default_rc = PImage_CreateSvgFromMemory(homepage_data,
                homepage_len, 0, 0, &homepage_default);
        header_default_rc = PImage_CreateSvgFromMemory(header_data,
                header_len, 0, 0, &header_default);
        ok = homepage_default_rc == PIMAGE_OK && homepage_default != NULL &&
                header_default_rc == PIMAGE_OK && header_default != NULL &&
                PImage_SvgGetInfo(homepage_default, &homepage_default_width,
                &homepage_default_height, NULL) == PIMAGE_OK &&
                PImage_SvgGetInfo(header_default, &header_default_width,
                &header_default_height, NULL) == PIMAGE_OK &&
                homepage_default_width == 450 &&
                homepage_default_height == 175 &&
                header_default_width == 128 && header_default_height == 50;
        if (ok) {
            ok = test1315_draw_metrics(homepage_default, 450, 175,
                    &homepage_default_pixels) &&
                    test1315_draw_metrics(header_default, 128, 50,
                    &header_default_pixels) &&
                    homepage_default_pixels.nonwhite > 400 &&
                    homepage_default_pixels.green > 20 &&
                    homepage_default_pixels.blue > 20 &&
                    homepage_default_pixels.max_x -
                    homepage_default_pixels.min_x > 250 &&
                    homepage_default_pixels.max_y -
                    homepage_default_pixels.min_y > 90 &&
                    header_default_pixels.nonwhite > 300 &&
                    header_default_pixels.green > 20 &&
                    header_default_pixels.blue > 20 &&
                    header_default_pixels.max_x - header_default_pixels.min_x >
                    90 && header_default_pixels.max_y -
                    header_default_pixels.min_y > 25;
        }
        if (!ok) {
            _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                    "default info/draw home rc=%d %dx%d header rc=%d %dx%d",
                    homepage_default_rc, homepage_default_width,
                    homepage_default_height, header_default_rc,
                    header_default_width, header_default_height);
            g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
        }
    }
    if (ok) {
        ok = test1315_draw_metrics(homepage, 450, 175,
                &homepage_pixels) && test1315_draw_metrics(homepage, 450,
                175, &homepage_repeat) && test1315_pixels_same(
                &homepage_pixels, &homepage_repeat) &&
                homepage_pixels.nonwhite > 1000 &&
                homepage_pixels.green > 50 && homepage_pixels.blue > 50 &&
                homepage_pixels.max_x - homepage_pixels.min_x > 250 &&
                homepage_pixels.max_y - homepage_pixels.min_y > 90;
        if (!ok) {
            _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                    "home pixels=%lu g=%lu b=%lu box=%d,%d-%d,%d",
                    homepage_pixels.nonwhite, homepage_pixels.green,
                    homepage_pixels.blue, homepage_pixels.min_x,
                    homepage_pixels.min_y, homepage_pixels.max_x,
                    homepage_pixels.max_y);
            g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
        }
    }
    if (homepage != NULL) {
        PImage_FreeSvg(homepage);
        homepage = NULL;
    }
    if (header != NULL && ok) {
        ok = test1315_draw_metrics(header, 256, 100, &header_pixels) &&
                test1315_draw_metrics(header, 256, 100, &header_repeat) &&
                test1315_pixels_same(&header_pixels, &header_repeat) &&
                header_pixels.nonwhite > 400 && header_pixels.green > 20 &&
                header_pixels.blue > 20 &&
                header_pixels.max_x - header_pixels.min_x > 180 &&
                header_pixels.max_y - header_pixels.min_y > 55;
        if (!ok) {
            _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                    "header pixels=%lu g=%lu b=%lu box=%d,%d-%d,%d",
                    header_pixels.nonwhite, header_pixels.green,
                    header_pixels.blue, header_pixels.min_x,
                    header_pixels.min_y, header_pixels.max_x,
                    header_pixels.max_y);
            g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
        }
    }
    if (header != NULL) {
        PImage_FreeSvg(header);
        header = NULL;
    }
    if (ok) {
        homepage_again_rc = PImage_CreateSvgFromMemory(homepage_data,
                homepage_len, 450, 175, &homepage_again);
        header_again_rc = PImage_CreateSvgFromMemory(header_data, header_len,
                256, 100, &header_again);
        ok = homepage_again_rc == PIMAGE_OK && homepage_again != NULL &&
                header_again_rc == PIMAGE_OK && header_again != NULL &&
                test1315_draw_metrics(homepage_again, 450, 175,
                &homepage_reparse) && test1315_draw_metrics(header_again,
                256, 100, &header_reparse) && test1315_pixels_same(
                &homepage_pixels, &homepage_reparse) &&
                test1315_pixels_same(&header_pixels, &header_reparse);
        if (!ok) {
            _snprintf(g_test1315_failure, sizeof(g_test1315_failure) - 1,
                    "reparse rc=%d/%d handles=%d/%d", homepage_again_rc,
                    header_again_rc, homepage_again != NULL,
                    header_again != NULL);
            g_test1315_failure[sizeof(g_test1315_failure) - 1] = '\0';
        }
    }
    if (homepage_again != NULL) {
        PImage_FreeSvg(homepage_again);
    }
    if (header_again != NULL) {
        PImage_FreeSvg(header_again);
    }
    if (homepage_default != NULL) {
        PImage_FreeSvg(homepage_default);
    }
    if (header_default != NULL) {
        PImage_FreeSvg(header_default);
    }
    free(homepage_data);
    free(header_data);
    if (!ok) {
        return FALSE;
    }
    strcpy(g_test1315_failure, "ok");
    return TRUE;
}

typedef struct test1316_background_fixture {
    int calls;
    int frees;
} test1316_background_fixture;

static const char g_test1316_background_svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" "
        "height=\"80\" viewBox=\"0 0 160 80\">"
        "<rect width=\"160\" height=\"80\" fill=\"#ffffff\"/>"
        "<rect y=\"60\" width=\"160\" height=\"20\" "
        "fill=\"#00ff00\"/></svg>";

static char g_test1316_failure[320];

const char *test1316_core_svg_background_fit_last_error(void)
{
    return g_test1316_failure;
}

static int test1316_background_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    test1316_background_fixture *fixture;
    char *copy;
    int length;

    fixture = (test1316_background_fixture *) pw;
    if (fixture == NULL || url == NULL || out_data == NULL ||
            out_len == NULL || strcmp(url, "/img/fit.svg") != 0) {
        return 1;
    }
    fixture->calls++;
    *out_data = NULL;
    *out_len = 0;
    length = (int) sizeof(g_test1316_background_svg) - 1;
    copy = (char *) malloc((size_t) length);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, g_test1316_background_svg, (size_t) length);
    *out_data = copy;
    *out_len = length;
    return 0;
}

static void test1316_background_free(void *pw, char *data)
{
    test1316_background_fixture *fixture;

    fixture = (test1316_background_fixture *) pw;
    if (fixture != NULL) {
        fixture->frees++;
    }
    free(data);
}

/* TEST 1316 - a non-repeating SVG background must fit a responsive box.
 * The green path is intentionally only in the lower quarter of an intrinsic
 * 160x80 image.  A 160x40 CSS background area clips that path when the image
 * is drawn intrinsically; the bounded Core fit makes it visible at y=30..40
 * without changing the public Core or Image ABI. */
BOOL test1316_core_svg_background_fit(void)
{
    static const char HTML[] =
            "<!doctype html><html><body><div id='fit'></div>"
            "</body></html>";
    static const char CSS[] =
            "html,body{margin:0;padding:0;}"
            "#fit{display:block;width:160px;height:40px;"
            "background:#ffffff;background-image:url('/img/fit.svg');"
            "background-repeat:no-repeat;background-position:0 0;}";
    HANDLE document;
    HANDLE sheet;
    test1316_background_fixture fixture;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF upper_pixel;
    COLORREF lower_pixel;
    int found;
    int fetched;
    int x;
    int y;
    int width;
    int height;
    BOOL ok;

    strcpy(g_test1316_failure, "not run");
    document = NULL;
    sheet = NULL;
    memset(&fixture, 0, sizeof(fixture));
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    found = 0;
    fetched = 0;
    x = 0;
    y = 0;
    width = 0;
    height = 0;
    upper_pixel = RGB(0, 0, 0);
    lower_pixel = RGB(0, 0, 0);
    ok = FALSE;

    document = PCore_ParseHTML(HTML, (int) sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, (int) sizeof(CSS) - 1,
            "https://positron.local/background-fit.css");
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_FetchImageResources(document, test1316_background_fetch,
            test1316_background_free, &fixture, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || fixture.calls != 1 ||
            fixture.frees != 1) {
        _snprintf(g_test1316_failure, sizeof(g_test1316_failure) - 1,
                "resource/style found=%d fetched=%d calls=%d frees=%d",
                found, fetched, fixture.calls, fixture.frees);
        g_test1316_failure[sizeof(g_test1316_failure) - 1] = '\0';
        goto cleanup;
    }
    PCore_SetViewport(200, 100, 96);
    if (PCore_LayoutDocument(document, 200, 100) != 0 ||
            PCore_NodeBox(document, "div", &x, &y, &width, &height) != 0 ||
            x != 0 || y != 0 || width != 160 || height != 40) {
        _snprintf(g_test1316_failure, sizeof(g_test1316_failure) - 1,
                "geometry=%d,%d %dx%d", x, y, width, height);
        g_test1316_failure[sizeof(g_test1316_failure) - 1] = '\0';
        goto cleanup;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 200, 100) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        strcpy(g_test1316_failure, "offscreen GDI allocation failed");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 200, 100);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(document, memory_dc, 0, 0);
    upper_pixel = GetPixel(memory_dc, 10, 10);
    lower_pixel = GetPixel(memory_dc, 10, 35);
    if (!test1314_color_close(upper_pixel, RGB(255, 255, 255)) ||
            !test1314_color_close(lower_pixel, RGB(0, 255, 0))) {
        _snprintf(g_test1316_failure, sizeof(g_test1316_failure) - 1,
                "pixels upper=%06lX lower=%06lX",
                (unsigned long) upper_pixel & 0xffffffUL,
                (unsigned long) lower_pixel & 0xffffffUL);
        g_test1316_failure[sizeof(g_test1316_failure) - 1] = '\0';
        goto cleanup;
    }
    ok = TRUE;
    strcpy(g_test1316_failure, "ok");

cleanup:
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    return ok;
}

typedef struct test1317_iana_background_fixture {
    char *homepage_data;
    int homepage_len;
    char *header_data;
    int header_len;
    int homepage_calls;
    int header_calls;
    int frees;
} test1317_iana_background_fixture;

static char g_test1317_failure[384];

const char *test1317_iana_core_background_last_error(void)
{
    return g_test1317_failure;
}

static int test1317_background_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    test1317_iana_background_fixture *fixture;
    const char *source;
    int source_len;
    char *copy;

    fixture = (test1317_iana_background_fixture *) pw;
    if (fixture == NULL || url == NULL || out_data == NULL ||
            out_len == NULL) {
        return 1;
    }
    *out_data = NULL;
    *out_len = 0;
    source = NULL;
    source_len = 0;
    if (strcmp(url, "/static/img/iana-logo-homepage.3914c6f1ab9d.svg") ==
            0) {
        fixture->homepage_calls++;
        source = fixture->homepage_data;
        source_len = fixture->homepage_len;
    } else if (strcmp(url,
            "/static/img/iana-logo-header-notext.9385bc9b108d.svg") == 0) {
        fixture->header_calls++;
        source = fixture->header_data;
        source_len = fixture->header_len;
    } else {
        return 1;
    }
    if (source == NULL || source_len <= 0) {
        return 1;
    }
    copy = (char *) malloc((size_t) source_len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, source, (size_t) source_len);
    *out_data = copy;
    *out_len = source_len;
    return 0;
}

static void test1317_background_free(void *pw, char *data)
{
    test1317_iana_background_fixture *fixture;

    fixture = (test1317_iana_background_fixture *) pw;
    if (fixture != NULL) {
        fixture->frees++;
    }
    free(data);
}

/* TEST 1317 - the real IANA homepage CSS background path through Core.
 * TEST1315 proves that the two SVG files can draw directly, while TEST1316
 * proves a simplified SVG can fit a block background.  This regression keeps
 * the live page's responsive h1 rules and complete tracked SVG fixtures: a
 * device-backed 320px/128-DPI viewport must select the 128x50 CSS-pixel
 * header asset, scale its one background tile to the device h1 box, and paint
 * both logo colours. */
BOOL test1317_iana_core_background(void)
{
    static const char HTML[] =
            "<!doctype html><html><body><div class='homepage'>"
            "<header><h1 id='logo'><span>Internet Assigned Numbers "
            "Authority</span></h1></header></div></body></html>";
    static const char CSS[] =
            "html,body{margin:0;padding:0;background:#ffffff;}"
            ".homepage header{background:#ffffff;margin:0;padding:20px;"
            "box-sizing:border-box;}"
            ".homepage header h1{display:block;text-align:center;"
            "margin:0 auto;background-image:url(\"/static/img/"
            "iana-logo-homepage.3914c6f1ab9d.svg\");height:175px;"
            "width:450px;}"
            ".homepage header h1 span{display:none;}"
            "@media (width <= 1000px){.homepage header h1{margin:0;"
            "width:128px;height:50px;background-image:url(\"/static/img/"
            "iana-logo-header-notext.9385bc9b108d.svg\");}}";
    HANDLE document;
    HANDLE sheet;
    test1317_iana_background_fixture fixture;
    HDC screen_dc;
    HDC memory_dc;
    HDC expected_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    HBITMAP expected_bitmap;
    HBITMAP expected_old_bitmap;
    PIMAGE_SVG expected_svg;
    RECT rect;
    COLORREF color;
    int found;
    int fetched;
    int x;
    int y;
    int box_x;
    int box_y;
    int width;
    int height;
    int green;
    int blue;
    int nonwhite;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    int expected_width;
    int expected_height;
    int expected_device_width;
    int expected_device_height;
    int expected_create_rc;
    int expected_info_rc;
    int mismatch_count;
    int compare_x;
    int compare_y;
    BOOL ok;

    strcpy(g_test1317_failure, "not run");
    memset(&fixture, 0, sizeof(fixture));
    document = NULL;
    sheet = NULL;
    screen_dc = NULL;
    memory_dc = NULL;
    expected_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    expected_bitmap = NULL;
    expected_old_bitmap = NULL;
    expected_svg = NULL;
    found = 0;
    fetched = 0;
    x = 0;
    y = 0;
    box_x = 0;
    box_y = 0;
    width = 0;
    height = 0;
    green = 0;
    blue = 0;
    nonwhite = 0;
    min_x = 320;
    min_y = 320;
    max_x = -1;
    max_y = -1;
    expected_width = 0;
    expected_height = 0;
    expected_device_width = 0;
    expected_device_height = 0;
    expected_create_rc = PIMAGE_ERROR_ARGUMENT;
    expected_info_rc = PIMAGE_ERROR_ARGUMENT;
    mismatch_count = 0;
    compare_x = 0;
    compare_y = 0;
    ok = FALSE;

    if (!test1315_read_fixture(L"fixtures\\iana-logo-homepage.svg",
            &fixture.homepage_data, &fixture.homepage_len) ||
            !test1315_read_fixture(L"fixtures\\iana-logo-header-notext.svg",
            &fixture.header_data, &fixture.header_len)) {
        strcpy(g_test1317_failure, "exact IANA fixture read failed");
        goto cleanup;
    }
    document = PCore_ParseHTML(HTML, (int) sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, (int) sizeof(CSS) - 1,
            "https://www.iana.org/static/css/iana_website.css");
    /* This call must precede StyleDocument: media queries are selected while
     * computed styles are produced, not during the later layout pass. */
    PCore_SetDeviceViewport(320, 320, 128);
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_FetchImageResources(document, test1317_background_fetch,
            test1317_background_free, &fixture, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || fixture.header_calls != 1 ||
            fixture.homepage_calls != 0 || fixture.frees != 1) {
        _snprintf(g_test1317_failure, sizeof(g_test1317_failure) - 1,
                "style/fetch found=%d fetched=%d home/header=%d/%d frees=%d",
                found, fetched, fixture.homepage_calls,
                fixture.header_calls, fixture.frees);
        g_test1317_failure[sizeof(g_test1317_failure) - 1] = '\0';
        goto cleanup;
    }
    if (PCore_LayoutDocument(document, 320, 320) != 0 ||
            PCore_NodeBox(document, "h1", &x, &y, &width, &height) != 0 ||
            width <= 0 || width >= 300 || height <= 0 || height >= 100) {
        _snprintf(g_test1317_failure, sizeof(g_test1317_failure) - 1,
                "responsive box=%d,%d %dx%d", x, y, width, height);
        g_test1317_failure[sizeof(g_test1317_failure) - 1] = '\0';
        goto cleanup;
    }
    box_x = x;
    box_y = y;
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 320, 320) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        strcpy(g_test1317_failure, "offscreen GDI allocation failed");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 320, 320);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(document, memory_dc, 0, 0);
    for (y = 0; y < 320; y++) {
        for (x = 0; x < 320; x++) {
            color = GetPixel(memory_dc, x, y);
            if (color == CLR_INVALID) {
                continue;
            }
            if (GetRValue(color) < 248 || GetGValue(color) < 248 ||
                    GetBValue(color) < 248) {
                nonwhite++;
                if (x < min_x) { min_x = x; }
                if (x > max_x) { max_x = x; }
                if (y < min_y) { min_y = y; }
                if (y > max_y) { max_y = y; }
            }
            if (GetGValue(color) > GetRValue(color) + 25 &&
                    GetGValue(color) > GetBValue(color) + 15) {
                green++;
            }
            if (GetBValue(color) > GetRValue(color) + 25 &&
                    GetBValue(color) > GetGValue(color) + 20) {
                blue++;
            }
        }
    }
    if (nonwhite < 80 || green < 10 || blue < 10 || max_x - min_x < 90 ||
            max_y - min_y < 25) {
        _snprintf(g_test1317_failure, sizeof(g_test1317_failure) - 1,
                "paint pixels nonwhite/green/blue=%d/%d/%d box=%d,%d-%d,%d",
                nonwhite, green, blue, min_x, min_y, max_x, max_y);
        g_test1317_failure[sizeof(g_test1317_failure) - 1] = '\0';
        goto cleanup;
    }
    /* The live CSS leaves background-repeat at its default value.  With the
     * viewBox as the natural header size, one 128x50 tile must be identical
     * to drawing the retained SVG directly into the selected h1 box.  This
     * catches the old 300x117 intrinsic tile that produced the screenshot's
     * globe plus clipped wordmark fragments even though colour thresholds
     * still passed. */
    expected_create_rc = PImage_CreateSvgFromMemory(fixture.header_data,
            fixture.header_len, 0, 0, &expected_svg);
    if (expected_create_rc == PIMAGE_OK && expected_svg != NULL) {
        expected_info_rc = PImage_SvgGetInfo(expected_svg, &expected_width,
                &expected_height, NULL);
    }
    if (expected_info_rc == PIMAGE_OK) {
        expected_device_width = MulDiv(expected_width, 128, 96);
        expected_device_height = MulDiv(expected_height, 128, 96);
    }
    if (box_x < 0 || box_y < 0 || width != expected_device_width ||
            height != expected_device_height ||
            box_x + width > 320 || box_y + height > 320 ||
            expected_create_rc != PIMAGE_OK || expected_svg == NULL ||
            expected_info_rc != PIMAGE_OK || expected_width != 128 ||
            expected_height != 50) {
        _snprintf(g_test1317_failure, sizeof(g_test1317_failure) - 1,
                "header box=%d,%d %dx%d expected-device=%dx%d image rc=%d "
                "info=%d natural=%dx%d",
                box_x, box_y, width, height, expected_device_width,
                expected_device_height, expected_create_rc,
                expected_info_rc, expected_width, expected_height);
        g_test1317_failure[sizeof(g_test1317_failure) - 1] = '\0';
        goto cleanup;
    }
    expected_dc = CreateCompatibleDC(screen_dc);
    expected_bitmap = CreateCompatibleBitmap(screen_dc, 320, 320);
    if (expected_dc == NULL || expected_bitmap == NULL) {
        strcpy(g_test1317_failure, "expected SVG GDI allocation failed");
        goto cleanup;
    }
    expected_old_bitmap = (HBITMAP) SelectObject(expected_dc,
            expected_bitmap);
    FillRect(expected_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    if (PImage_DrawSvg(expected_svg, expected_dc, box_x, box_y, width,
            height) !=
            PIMAGE_OK) {
        strcpy(g_test1317_failure, "expected SVG draw failed");
        goto cleanup;
    }
    for (compare_y = box_y; compare_y < box_y + height; compare_y++) {
        for (compare_x = box_x; compare_x < box_x + width; compare_x++) {
            if (GetPixel(memory_dc, compare_x, compare_y) !=
                    GetPixel(expected_dc, compare_x, compare_y)) {
                mismatch_count++;
            }
        }
    }
    if (mismatch_count != 0) {
        _snprintf(g_test1317_failure, sizeof(g_test1317_failure) - 1,
                "Core/header direct pixel mismatches=%d", mismatch_count);
        g_test1317_failure[sizeof(g_test1317_failure) - 1] = '\0';
        goto cleanup;
    }
    ok = TRUE;
    strcpy(g_test1317_failure, "ok");

cleanup:
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (expected_old_bitmap != NULL && expected_dc != NULL) {
        SelectObject(expected_dc, expected_old_bitmap);
    }
    if (expected_bitmap != NULL) {
        DeleteObject(expected_bitmap);
    }
    if (expected_dc != NULL) {
        DeleteDC(expected_dc);
    }
    if (expected_svg != NULL) {
        PImage_FreeSvg(expected_svg);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    free(fixture.homepage_data);
    free(fixture.header_data);
    PCore_SetViewport(240, 320, 96);
    return ok;
}

typedef struct test1318_data_uri_fixture {
    int callback_calls;
} test1318_data_uri_fixture;

static char g_test1318_failure[384];

const char *test1318_core_css_data_uri_last_error(void)
{
    return g_test1318_failure;
}

static int test1318_data_uri_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    test1318_data_uri_fixture *fixture;

    fixture = (test1318_data_uri_fixture *) pw;
    if (fixture != NULL) {
        fixture->callback_calls++;
    }
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    (void) url;
    return 1;
}

static void test1318_data_uri_free(void *pw, char *data)
{
    (void) pw;
    free(data);
}

/* TEST 1318 - CSS data:image/svg+xml must travel through Core's computed
 * background-image discovery, bounded data-URI decoder, document image cache,
 * positron_image SVG decoder and the normal Core paint path.  The callback is
 * intentionally a hard failure: data URLs are local bytes and must never be
 * routed to the host's HTTP transport. */
BOOL test1318_core_css_data_uri(void)
{
    static const char HTML[] =
            "<!doctype html><html><body>"
            "<div id='percent'></div><div id='base64'></div>"
            "<div id='invalid'></div><div id='oversize'></div>"
            "</body></html>";
    static const char CSS[] =
            "html,body{margin:0;padding:0;background:#ffffff;}"
            "#percent,#base64,#invalid,#oversize{display:block;width:24px;"
            "height:24px;"
            "background-color:#ffffff;background-repeat:no-repeat;"
            "background-position:0 0;}"
            "#percent{background-image:url('data:image/svg+xml,%3Csvg%20"
            "xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20"
            "width%3D%2224%22%20height%3D%2224%22%20viewBox%3D%220%200%20"
            "24%2024%22%3E%3Cpath%20fill%3D%22%23000%22%20d%3D%22"
            "M3%205h18v3H3zM3%2010h18v3H3zM3%2015h18v3H3z%22%2F%3E"
            "%3C%2Fsvg%3E');}"
            "#base64{margin-top:4px;background-image:url('data:image/"
            "svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAw"
            "MC9zdmciIHdpZHRoPSIyNCIgaGVpZ2h0PSIyNCIgdmlld0JveD0iMCAwIDI0"
            "IDI0Ij48cGF0aCBmaWxsPSIjMDAwIiBkPSJNMyA1aDE4djNIM3pNMyAxMGgx"
            "OHYzSDN6TTMgMTVoMTh2M0gzeiIvPjwvc3ZnPg==');}"
            "#invalid{background-image:url('data:image/svg+xml,%ZZ');}";
    static const char OVERSIZE_PREFIX[] =
            "#oversize{background-image:url('data:image/svg+xml,<svg "
            "xmlns=\"http://www.w3.org/2000/svg\">";
    static const char OVERSIZE_PATH[] = "<path d=\"M0 0h1v1z\"/>";
    static const char OVERSIZE_SUFFIX[] = "</svg>');}";
    HANDLE document;
    HANDLE sheet;
    test1318_data_uri_fixture fixture;
    PCoreImageDecodeStats image_stats;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF percent_pixel;
    COLORREF base64_pixel;
    int found;
    int fetched;
    int css_len;
    int i;
    size_t prefix_len;
    size_t path_len;
    size_t suffix_len;
    size_t oversize_len;
    int path_count;
    char *css_data;
    BOOL ok;

    strcpy(g_test1318_failure, "not run");
    document = NULL;
    sheet = NULL;
    memset(&fixture, 0, sizeof(fixture));
    memset(&image_stats, 0, sizeof(image_stats));
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    found = 0;
    fetched = 0;
    css_len = 0;
    i = 0;
    prefix_len = strlen(OVERSIZE_PREFIX);
    path_len = strlen(OVERSIZE_PATH);
    suffix_len = strlen(OVERSIZE_SUFFIX);
    path_count = 65;
    oversize_len = prefix_len + path_len * (size_t) path_count + suffix_len;
    css_data = NULL;
    percent_pixel = RGB(255, 255, 255);
    base64_pixel = RGB(255, 255, 255);
    ok = FALSE;

    document = PCore_ParseHTML(HTML, (int) sizeof(HTML) - 1);
    css_len = (int) (sizeof(CSS) - 1 + oversize_len);
    css_data = (char *) malloc((size_t) css_len + 1);
    if (css_data == NULL) {
        strcpy(g_test1318_failure, "oversize fixture allocation failed");
        goto cleanup;
    }
    memcpy(css_data, CSS, sizeof(CSS) - 1);
    memcpy(css_data + sizeof(CSS) - 1, OVERSIZE_PREFIX, prefix_len);
    for (i = 0; i < path_count; i++) {
        memcpy(css_data + sizeof(CSS) - 1 + prefix_len +
                (size_t) i * path_len, OVERSIZE_PATH, path_len);
    }
    memcpy(css_data + sizeof(CSS) - 1 + oversize_len - suffix_len,
            OVERSIZE_SUFFIX, suffix_len);
    css_data[css_len] = '\0';
    sheet = PCore_ParseCSS(css_data, css_len,
            "https://positron.local/data-uri.css");
    PCore_SetViewport(80, 100, 96);
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_FetchImageResources(document, test1318_data_uri_fetch,
            test1318_data_uri_free, &fixture, &found, &fetched) != 0 ||
            found != 4 || fetched != 2 || fixture.callback_calls != 0) {
        _snprintf(g_test1318_failure, sizeof(g_test1318_failure) - 1,
                "data discovery found/fetched/callback=%d/%d/%d",
                found, fetched, fixture.callback_calls);
        g_test1318_failure[sizeof(g_test1318_failure) - 1] = '\0';
        goto cleanup;
    }
    if (PCore_LayoutDocument(document, 80, 100) != 0 ||
            PCore_GetImageDecodeStats(document, &image_stats) != 0 ||
            image_stats.svg_creates < 2) {
        _snprintf(g_test1318_failure, sizeof(g_test1318_failure) - 1,
                "layout/svg creates=%u", image_stats.svg_creates);
        g_test1318_failure[sizeof(g_test1318_failure) - 1] = '\0';
        goto cleanup;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 80, 100) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        strcpy(g_test1318_failure, "offscreen GDI allocation failed");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 80, 100);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(document, memory_dc, 0, 0);
    percent_pixel = GetPixel(memory_dc, 10, 6);
    base64_pixel = GetPixel(memory_dc, 10, 34);
    if (!test1314_color_close(percent_pixel, RGB(0, 0, 0)) ||
            !test1314_color_close(base64_pixel, RGB(0, 0, 0))) {
        _snprintf(g_test1318_failure, sizeof(g_test1318_failure) - 1,
                "paint percent/base64=%06lX/%06lX",
                (unsigned long) percent_pixel & 0xffffffUL,
                (unsigned long) base64_pixel & 0xffffffUL);
        g_test1318_failure[sizeof(g_test1318_failure) - 1] = '\0';
        goto cleanup;
    }
    ok = TRUE;
    strcpy(g_test1318_failure, "ok");

cleanup:
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    free(css_data);
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    PCore_SetViewport(240, 320, 96);
    return ok;
}

static char g_test1319_failure[384];

const char *test1319_image_rgba_round_stroke_last_error(void)
{
    return g_test1319_failure;
}

static int test1319_gray_alpha_pixel(COLORREF color)
{
    int red;
    int green;
    int blue;

    if (color == CLR_INVALID) {
        return 0;
    }
    red = (int) GetRValue(color);
    green = (int) GetGValue(color);
    blue = (int) GetBValue(color);
    return abs(red - green) <= 8 && abs(red - blue) <= 8 &&
            red >= 80 && red <= 200;
}

/* TEST 1319 - exact WinWorld navbar-toggler SVG paint regression.  This
 * fixture intentionally exercises the path that the CSS data URI reaches:
 * rgba() stroke alpha must survive the libsvgtiny-to-NanoSVG conversion and
 * round caps must extend each open line beyond its endpoint. */
BOOL test1319_image_rgba_round_stroke(void)
{
    static const char SVG[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"30\" "
            "height=\"30\" viewBox=\"0 0 30 30\">"
            "<path stroke=\"rgba(0, 0, 0, 0.5)\" "
            "stroke-linecap=\"round\" stroke-miterlimit=\"10\" "
            "stroke-width=\"2\" d=\"M4 7h22M4 15h22M4 23h22\"/>"
            "</svg>";
    PIMAGE_SVG svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF center_top;
    COLORREF center_middle;
    COLORREF center_bottom;
    COLORREF round_start;
    COLORREF round_end;
    COLORREF outside_start;
    int width;
    int height;
    unsigned int shapes;
    int create_rc;
    int draw_rc;
    BOOL ok;

    strcpy(g_test1319_failure, "not run");
    svg = NULL;
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    center_top = CLR_INVALID;
    center_middle = CLR_INVALID;
    center_bottom = CLR_INVALID;
    round_start = CLR_INVALID;
    round_end = CLR_INVALID;
    outside_start = CLR_INVALID;
    width = 0;
    height = 0;
    shapes = 0;
    create_rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            30, 30, &svg);
    ok = create_rc == PIMAGE_OK && svg != NULL;
    if (ok && (PImage_SvgGetInfo(svg, &width, &height, &shapes) !=
            PIMAGE_OK || width != 30 || height != 30 || shapes != 1)) {
        ok = FALSE;
        _snprintf(g_test1319_failure, sizeof(g_test1319_failure) - 1,
                "create/info rc=%d handle=%d size=%dx%d shapes=%u",
                create_rc, svg != NULL, width, height, shapes);
        g_test1319_failure[sizeof(g_test1319_failure) - 1] = '\0';
    }
    if (!ok && strcmp(g_test1319_failure, "not run") == 0) {
        _snprintf(g_test1319_failure, sizeof(g_test1319_failure) - 1,
                "create rc=%d handle=%d", create_rc, svg != NULL);
        g_test1319_failure[sizeof(g_test1319_failure) - 1] = '\0';
    }
    if (!ok) {
        goto cleanup;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 30, 30) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        strcpy(g_test1319_failure, "offscreen GDI allocation failed");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 30, 30);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    draw_rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 30, 30);
    if (draw_rc != PIMAGE_OK) {
        _snprintf(g_test1319_failure, sizeof(g_test1319_failure) - 1,
                "draw rc=%d", draw_rc);
        g_test1319_failure[sizeof(g_test1319_failure) - 1] = '\0';
        goto cleanup;
    }
    center_top = GetPixel(memory_dc, 15, 7);
    center_middle = GetPixel(memory_dc, 15, 15);
    center_bottom = GetPixel(memory_dc, 15, 23);
    round_start = GetPixel(memory_dc, 3, 7);
    round_end = GetPixel(memory_dc, 27, 7);
    outside_start = GetPixel(memory_dc, 2, 7);
    if (!test1319_gray_alpha_pixel(center_top) ||
            !test1319_gray_alpha_pixel(center_middle) ||
            !test1319_gray_alpha_pixel(center_bottom) ||
            !test1319_gray_alpha_pixel(round_start) ||
            !test1319_gray_alpha_pixel(round_end) ||
            GetRValue(outside_start) < 248 ||
            GetGValue(outside_start) < 248 ||
            GetBValue(outside_start) < 248) {
        _snprintf(g_test1319_failure, sizeof(g_test1319_failure) - 1,
                "pixels centers=%06lX/%06lX/%06lX caps=%06lX/%06lX "
                "outside=%06lX",
                (unsigned long) center_top & 0xffffffUL,
                (unsigned long) center_middle & 0xffffffUL,
                (unsigned long) center_bottom & 0xffffffUL,
                (unsigned long) round_start & 0xffffffUL,
                (unsigned long) round_end & 0xffffffUL,
                (unsigned long) outside_start & 0xffffffUL);
        g_test1319_failure[sizeof(g_test1319_failure) - 1] = '\0';
        goto cleanup;
    }
    ok = TRUE;
    strcpy(g_test1319_failure, "ok");

cleanup:
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (svg != NULL) {
        PImage_FreeSvg(svg);
    }
    return ok;
}

static char g_test1329_failure[384];

const char *test1329_core_bootstrap_hamburger_last_error(void)
{
    return g_test1329_failure;
}

static int test1329_gray_pixel(COLORREF color)
{
    int red;
    int green;
    int blue;

    if (color == CLR_INVALID) {
        return 0;
    }
    red = (int) GetRValue(color);
    green = (int) GetGValue(color);
    blue = (int) GetBValue(color);
    return abs(red - green) <= 10 && abs(red - blue) <= 10 &&
            red >= 60 && red <= 205;
}

/* TEST 1329 - the exact Bootstrap 4 navbar-toggler data URI through the
 * complete Core CSS -> data decoder -> Image SVG -> background paint path.
 * TEST 1319 proves the Image DLL in isolation; this regression deliberately
 * retains the descendant selector, button without an explicit type, encoded
 * rgba() stroke and round caps used by the consumer page. */
BOOL test1329_core_bootstrap_hamburger(void)
{
    static const char HTML[] =
            "<!doctype html><html><body>"
            "<nav class='navbar navbar-light'><button id='toggle' "
            "class='navbar-toggler'><span class='navbar-toggler-icon'>"
            "</span></button></nav></body></html>";
    static const char CSS[] =
            "html,body{margin:0;padding:0;background:#ffffff;}"
            ".navbar{display:block;width:72px;height:52px;padding:4px;"
            "background:#ffffff;}"
            ".navbar-toggler{display:inline-block;width:40px;height:40px;"
            "padding:4px;border:1px solid #dddddd;background:#ffffff;}"
            ".navbar-toggler-icon{display:inline-block;width:30px;"
            "height:30px;vertical-align:middle;content:\"\";"
            "background-position:50% 50%;background-size:100% 100%;"
            "background-repeat:no-repeat;}"
            ".navbar-light .navbar-toggler-icon{background-image:url("
            "\"data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/"
            "svg' width='30' height='30' viewBox='0 0 30 30'%3e%3cpath "
            "stroke='rgba%280, 0, 0, 0.5%29' stroke-linecap='round' "
            "stroke-miterlimit='10' stroke-width='2' d='M4 7h22M4 "
            "15h22M4 23h22'/%3e%3c/svg%3e\");}";
    HANDLE document;
    HANDLE sheet;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    test1318_data_uri_fixture fixture;
    PCoreImageDecodeStats image_stats;
    int found;
    int fetched;
    int button_x;
    int button_y;
    int button_w;
    int button_h;
    int button_kind;
    int button_selected;
    int button_disabled;
    int span_x;
    int span_y;
    int span_w;
    int span_h;
    int y;
    int x;
    int gray_count;
    int row_hit;
    int row_groups;
    int previous_hit;
    BOOL ok;

    strcpy(g_test1329_failure, "not run");
    document = NULL;
    sheet = NULL;
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    memset(&fixture, 0, sizeof(fixture));
    memset(&image_stats, 0, sizeof(image_stats));
    found = 0;
    fetched = 0;
    button_x = 0;
    button_y = 0;
    button_w = 0;
    button_h = 0;
    button_kind = 0;
    button_selected = 0;
    button_disabled = 0;
    span_x = 0;
    span_y = 0;
    span_w = 0;
    span_h = 0;
    y = 0;
    x = 0;
    gray_count = 0;
    row_hit = 0;
    row_groups = 0;
    previous_hit = 0;
    ok = FALSE;

    document = PCore_ParseHTML(HTML, (int) sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, (int) sizeof(CSS) - 1,
            "https://positron.local/bootstrap.css");
    PCore_SetViewport(100, 100, 96);
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_FetchImageResourcesEx(document, test1318_data_uri_fetch,
            test1318_data_uri_free, &fixture, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || fixture.callback_calls != 0) {
        _snprintf(g_test1329_failure, sizeof(g_test1329_failure) - 1,
                "data uri found/fetched/callback=%d/%d/%d", found, fetched,
                fixture.callback_calls);
        g_test1329_failure[sizeof(g_test1329_failure) - 1] = '\0';
        goto cleanup;
    }
    if (PCore_LayoutDocument(document, 100, 100) != 0 ||
            PCore_FormControlInfoById(document, "toggle", &button_x,
            &button_y, &button_w, &button_h, &button_kind, &button_selected,
            &button_disabled) != 0 || button_kind != 7 || button_w <= 0 ||
            button_h <= 0 || PCore_NodeBox(document, "span", &span_x,
            &span_y, &span_w, &span_h) != 0 || span_w < 20 || span_h < 20 ||
            PCore_GetImageDecodeStats(document, &image_stats) != 0 ||
            image_stats.svg_creates == 0) {
        _snprintf(g_test1329_failure, sizeof(g_test1329_failure) - 1,
                "layout button=%d,%d %dx%d kind=%d span=%d,%d %dx%d svg=%u",
                button_x, button_y, button_w, button_h, button_kind, span_x,
                span_y, span_w, span_h, image_stats.svg_creates);
        g_test1329_failure[sizeof(g_test1329_failure) - 1] = '\0';
        goto cleanup;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 100, 100) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        strcpy(g_test1329_failure, "offscreen GDI allocation failed");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 100, 100);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(document, memory_dc, 0, 0);
    for (y = span_y; y < span_y + span_h && y < 100; y++) {
        gray_count = 0;
        for (x = span_x; x < span_x + span_w && x < 100; x++) {
            if (test1329_gray_pixel(GetPixel(memory_dc, x, y))) {
                gray_count++;
            }
        }
        row_hit = gray_count >= 10;
        if (row_hit && !previous_hit) {
            row_groups++;
        }
        previous_hit = row_hit;
    }
    if (row_groups < 3) {
        _snprintf(g_test1329_failure, sizeof(g_test1329_failure) - 1,
                "hamburger rows=%d span=%d,%d %dx%d", row_groups, span_x,
                span_y, span_w, span_h);
        g_test1329_failure[sizeof(g_test1329_failure) - 1] = '\0';
        goto cleanup;
    }
    ok = TRUE;
    strcpy(g_test1329_failure, "ok");

cleanup:
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    PCore_SetViewport(240, 320, 96);
    return ok;
}
