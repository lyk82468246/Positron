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
