/* Public Core consumer fixtures: no private layout/font implementation. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "positron_core.h"

static char g_wrap_error[256];
static const char g_wrap_url[] =
        "https://www.iana.org/help/example-domains";
static const char g_wrap_query[] =
        "https://www.iana.org/help/example-domains?query="
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char g_wrap_title[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
static const char g_wrap_unicode[] =
        "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80"
        "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80"
        "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80"
        "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80"
        "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80"
        "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80";
static const char g_wrap_english[] =
        "Small words fit on ordinary lines without being cut in half.";

static int wrap_relation(HANDLE doc, unsigned int relation,
        unsigned int index, int *number)
{
    return PCore_NodeRelationById(doc, "text", relation, index,
            NULL, 0, NULL, number) == 0;
}

static int wrap_case(const char *text, const char *rule,
        int expected_wrap, int shrink_to_fit, int dpi,
        int *height_out, int *width_out)
{
    HANDLE doc;
    HANDLE sheet;
    HDC screen;
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old;
    char html[2048];
    char css[1024];
    char actual[512];
    char href[128];
    int bytes;
    int pass;
    int count;
    int i;
    int width;
    int height;
    int container_height;
    int container_width;
    int ok;
    static const int widths[] = { 128, 224, 128 };

    doc = sheet = NULL;
    screen = dc = NULL;
    bitmap = old = NULL;
    ok = 0;
    pass = -1;
    count = container_height = container_width = -1;
    width = 0;
    _snprintf(html, sizeof(html) - 1,
            "<!doctype html><html><body><div id='container'>"
            "<a id='text' href='%s'>%s</a></div></body></html>",
            g_wrap_url, text);
    _snprintf(css, sizeof(css) - 1,
            "html,body{margin:0;padding:0;background:white}"
            "body{font:16px sans-serif;line-height:24px}"
            "#container{margin:0;padding:0;%s%s}"
            "a{color:black;text-decoration:none}",
            shrink_to_fit ? "display:inline-block;" : "", rule);
    doc = PCore_ParseHTML(html, (int) strlen(html));
    sheet = PCore_ParseCSS(css, (int) strlen(css),
            "https://positron.local/text-wrap.css");
    if (doc == NULL || sheet == NULL) { goto cleanup; }
    screen = GetDC(NULL);
    dc = screen != NULL ? CreateCompatibleDC(screen) : NULL;
    bitmap = screen != NULL ? CreateCompatibleBitmap(screen,
            MulDiv(256, dpi, 96), MulDiv(256, dpi, 96)) : NULL;
    if (dc == NULL || bitmap == NULL) { goto cleanup; }
    old = (HBITMAP) SelectObject(dc, bitmap);
    for (pass = 0; pass < 3; pass++) {
        RECT rect;
        width = MulDiv(widths[pass], dpi, 96);
        height = MulDiv(256, dpi, 96);
        PCore_SetDeviceViewport(width, height, dpi);
        if (PCore_StyleDocument(doc, sheet) != 0 ||
                PCore_LayoutDocument(doc, width, height) != 0 ||
                PCore_NodeTextContentById(doc, "text", actual,
                sizeof(actual), &bytes) != 0 || strcmp(actual, text) != 0 ||
                !wrap_relation(doc, PCORE_NODE_RELATION_LAYOUT_FRAGMENT_COUNT,
                0, &count) || count <= 0 ||
                PCore_FragmentInfoById(doc, "container", NULL, NULL,
                &container_width, &container_height) != 0) {
            goto cleanup;
        }
        if (expected_wrap == 1 && (PCore_DocumentWidth(doc) > width ||
                container_height <= 24 || count < 2)) { goto cleanup; }
        /* NetSurf can place empty inline decoration lines around an
         * overflowing anchor. Assert one text fragment and overflow, not
         * a fabricated 24px total height for that legacy path. */
        if (expected_wrap == 0 && (count != 1 ||
                PCore_DocumentWidth(doc) <= width)) { goto cleanup; }
        SetRect(&rect, 0, 0, width, height);
        FillRect(dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
        PCore_PaintDocument(doc, dc, 0, 0);
        for (i = 0; i < count; i++) {
            int x;
            int y;
            int w;
            int h;
            int px;
            int py;
            int hit;
            int ink;
            if (!wrap_relation(doc, PCORE_NODE_RELATION_LAYOUT_FRAGMENT_X_AT,
                    i, &x) || !wrap_relation(doc,
                    PCORE_NODE_RELATION_LAYOUT_FRAGMENT_Y_AT, i, &y) ||
                    !wrap_relation(doc,
                    PCORE_NODE_RELATION_LAYOUT_FRAGMENT_WIDTH_AT, i, &w) ||
                    !wrap_relation(doc,
                    PCORE_NODE_RELATION_LAYOUT_FRAGMENT_HEIGHT_AT, i, &h)) {
                goto cleanup;
            }
            if (w == 0 || h == 0) { continue; }
            if (expected_wrap == 1 &&
                    (x < 0 || x + w > widths[pass] || w <= 0)) {
                goto cleanup;
            }
            px = MulDiv(x + w / 2, dpi, 96);
            py = MulDiv(y + h / 2, dpi, 96);
            hit = PCore_LinkAt(doc, px, py, href, sizeof(href));
            if (hit != 1 || strcmp(href, g_wrap_url) != 0) { goto cleanup; }
            /* Every visible text fragment must actually paint ink inside its
             * projected rectangle. No golden font-specific glyph widths. */
            ink = 0;
            if (y + h < 256 && x + w <= widths[pass]) {
                for (py = MulDiv(y, dpi, 96);
                        py < MulDiv(y + h, dpi, 96) && !ink; py++) {
                    for (px = MulDiv(x, dpi, 96);
                            px < MulDiv(x + w, dpi, 96); px++) {
                        COLORREF pixel;
                        pixel = GetPixel(dc, px, py);
                        if (pixel != CLR_INVALID && pixel != RGB(255,255,255)) {
                            ink = 1;
                            break;
                        }
                    }
                }
                if (!ink) { goto cleanup; }
            }
        }
        if (pass == 0) {
            *height_out = container_height;
            *width_out = container_width;
        } else if (pass == 2 && (*height_out != container_height ||
                *width_out != container_width)) {
            goto cleanup; /* narrow -> wide -> narrow must be reproducible */
        }
    }
    ok = 1;
cleanup:
    if (!ok) {
        _snprintf(g_wrap_error, sizeof(g_wrap_error) - 1,
                "dpi=%d pass=%d exp=%d shrink=%d page=%d view=%d "
                "box=%dx%d count=%d text=%u rule=%s", dpi, pass,
                expected_wrap, shrink_to_fit,
                doc != NULL ? PCore_DocumentWidth(doc) : -1, width,
                container_width, container_height, count,
                (unsigned int) strlen(text), rule);
    }
    if (old != NULL) { SelectObject(dc, old); }
    if (bitmap != NULL) { DeleteObject(bitmap); }
    if (dc != NULL) { DeleteDC(dc); }
    if (screen != NULL) { ReleaseDC(NULL, screen); }
    if (doc != NULL) { PCore_FreeDocument(doc); }
    if (sheet != NULL) { PCore_FreeStylesheet(sheet); }
    PCore_SetViewport(320, 240, 96);
    return ok;
}

static int wrap_cluster_case(const char *text, int dpi)
{
    HANDLE doc;
    HANDLE sheet;
    char html[512];
    char actual[256];
    int bytes;
    int count;
    int ok;
    static const char css[] =
            "html,body{margin:0;padding:0}"
            "body{font:16px sans-serif;line-height:24px;overflow-wrap:break-word}";
    _snprintf(html, sizeof(html) - 1,
            "<html><body><a id='text' href='#x'>%s</a></body></html>", text);
    PCore_SetDeviceViewport(MulDiv(8, dpi, 96), MulDiv(256, dpi, 96), dpi);
    doc = PCore_ParseHTML(html, (int) strlen(html));
    sheet = PCore_ParseCSS(css, sizeof(css) - 1,
            "https://positron.local/clusters.css");
    ok = doc != NULL && sheet != NULL &&
            PCore_StyleDocument(doc, sheet) == 0 &&
            PCore_LayoutDocument(doc, MulDiv(8, dpi, 96),
            MulDiv(256, dpi, 96)) == 0 &&
            wrap_relation(doc, PCORE_NODE_RELATION_LAYOUT_FRAGMENT_COUNT,
            0, &count) && count == 3 &&
            PCore_NodeTextContentById(doc, "text", actual, sizeof(actual),
            &bytes) == 0 && strcmp(actual, text) == 0;
    if (doc != NULL) { PCore_FreeDocument(doc); }
    if (sheet != NULL) { PCore_FreeStylesheet(sheet); }
    PCore_SetViewport(320, 240, 96);
    if (!ok) {
        _snprintf(g_wrap_error, sizeof(g_wrap_error) - 1,
                "tiny viewport scalar/cluster progress failed dpi=%d", dpi);
    }
    return ok;
}

BOOL test1345_core_text_wrap_contract(void)
{
    static const int dpis[] = { 96, 128, 192 };
    static const char *rules[] = {
        "overflow-wrap:break-word;", "word-wrap:break-word;",
        "overflow-wrap:normal;word-wrap:break-word;",
        "word-wrap:normal;overflow-wrap:BREAK-WORD;",
        "overflow-wrap:break-word!important;word-wrap:normal;",
        "overflow-wrap:break-word;white-space:pre-wrap;",
        "overflow-wrap:break-word;white-space:pre-line;"
    };
    int dpi;
    int index;
    int h;
    int w;
    int h_other;
    int w_other;
    g_wrap_error[0] = '\0';
    for (dpi = 0; dpi < 3; dpi++) {
        if (!wrap_case(g_wrap_url, "", 0, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_url, "overflow-wrap:anywhere;", 0, 0,
                dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_url, "overflow-wrap:break-word;word-wrap:normal;",
                0, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_url, "overflow-wrap:break-word;white-space:nowrap;",
                0, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_url, "overflow-wrap:break-word;white-space:pre;",
                0, 0, dpis[dpi], &h, &w)) { return FALSE; }
        for (index = 0; index < sizeof(rules) / sizeof(rules[0]); index++) {
            if (!wrap_case(g_wrap_url, rules[index], 1, 0,
                    dpis[dpi], &h, &w)) { return FALSE; }
        }
        if (!wrap_case(g_wrap_query, rules[0], 1, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_title, rules[0], 1, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_unicode, rules[0], 1, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_english, "", -1, 0, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_english, rules[0], -1, 0, dpis[dpi],
                &h_other, &w_other)) { return FALSE; }
        if (h != h_other || w != w_other) {
            strcpy(g_wrap_error, "ordinary space wrapping changed");
            return FALSE;
        }
        /* break-word must not reduce min-content/shrink-to-fit width. */
        if (!wrap_case(g_wrap_url, "", 0, 1, dpis[dpi], &h, &w) ||
                !wrap_case(g_wrap_url, rules[0], 0, 1, dpis[dpi],
                &h_other, &w_other)) { return FALSE; }
        if (w != w_other || h != h_other) {
            strcpy(g_wrap_error, "break-word changed intrinsic width");
            return FALSE;
        }
        if (!wrap_cluster_case("A\xcc\x81" "A\xcc\x81" "A\xcc\x81",
                dpis[dpi]) ||
                !wrap_cluster_case("\xf0\xa0\x80\x80\xf0\xa0\x80\x80"
                "\xf0\xa0\x80\x80", dpis[dpi]) ||
                !wrap_cluster_case("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9"
                "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9"
                "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9", dpis[dpi]) ||
                !wrap_cluster_case("\xf0\x9f\x87\xba\xf0\x9f\x87\xb8"
                "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8"
                "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8", dpis[dpi])) {
            return FALSE;
        }
    }
    return TRUE;
}

const char *test1345_core_text_wrap_last_error(void)
{
    g_wrap_error[sizeof(g_wrap_error) - 1] = '\0';
    return g_wrap_error;
}
