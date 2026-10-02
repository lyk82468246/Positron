/* Offline consumer regression for Core fragment CSS/device coordinates.
 * Layout, lookup and paint remain in positron_core.dll. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "positron_core.h"

static char g_fragment_error[256];

static const char g_fragment_html[] =
    "<!doctype html><html><body>"
    "<h2 id='version'>Version</h2><h3 id='system'>System</h3>"
    "<a name='legacy'>Legacy</a><div id='shared'>ID wins</div>"
    "<a name='shared'>Wrong named target</a>"
    "<section id='negative'>Negative</section><div id='hidden'>Hidden</div>"
    "<a name='hidden'>Must not replace hidden ID</a>"
    "<input id='field' type='text' value='native'>"
    "<a id='link' href='#system'>System link</a>"
    "<div id='tail'>Tail</div></body></html>";

static const char g_fragment_css[] =
    "html,body{margin:0;padding:0;background:#ffffff}"
    "h2,h3,a,div,section,input{margin:0;padding:0;border:0;box-sizing:content-box}"
    "h2,h3,a,div,section{position:absolute;display:block;left:18px;"
    "width:90px;height:24px;font:12px sans-serif;line-height:24px}"
    /* Primary colours are exact on WM6 RGB565 and RGB888 memory DCs. */
    "#version{top:270px;background:#0000ff}"
    "#system{top:480px;background:#00ff00}"
    "a[name=legacy]{top:360px}#shared{top:400px}"
    "a[name=shared]{top:420px}"
    "#negative{left:-14px;top:150px}#hidden{display:none}"
    "a[name=hidden]{top:440px}"
    "#field{position:absolute;left:120px;top:40px;width:80px;height:24px}"
    "#link{left:18px;top:80px}#tail{top:900px}";

static int fragment_expect(HANDLE document, const char *token, int by_id,
        int expected_x, int expected_y, int dpi)
{
    int x;
    int y;
    int width;
    int height;
    int status;
    int relation_y;

    x = y = width = height = -999;
    status = by_id ? PCore_FragmentInfoById(document, token,
            &x, &y, &width, &height) :
            PCore_FragmentInfoByToken(document, token,
            &x, &y, &width, &height);
    if (status != 0 || x != expected_x || y != expected_y ||
            width != 90 || height != 24) {
        _snprintf(g_fragment_error, sizeof(g_fragment_error) - 1,
                "dpi=%d %s id=%d rc=%d got=%d,%d,%d,%d expected=%d,%d,90,24",
                dpi, token, by_id, status, x, y, width, height,
                expected_x, expected_y);
        return 0;
    }
    y = -999;
    status = by_id ? PCore_FragmentInfoById(document, token,
            NULL, &y, NULL, NULL) :
            PCore_FragmentInfoByToken(document, token,
            NULL, &y, NULL, NULL);
    if (status != 0 || y != expected_y || (by_id ?
            PCore_FragmentInfoById(document, token, NULL, NULL, NULL, NULL) :
            PCore_FragmentInfoByToken(document, token, NULL, NULL,
            NULL, NULL)) != 0) {
        strcpy(g_fragment_error, "optional fragment outputs failed");
        return 0;
    }
    if (by_id && (PCore_NodeRelationById(document, token,
            PCORE_NODE_RELATION_LAYOUT_RECT_Y, 0, NULL, 0,
            NULL, &relation_y) != 0 || relation_y != expected_y)) {
        strcpy(g_fragment_error, "fragment disagrees with CSS layout rect");
        return 0;
    }
    return 1;
}

static int fragment_reject(HANDLE document, const char *token, int by_id)
{
    int x;
    int y;
    int width;
    int height;
    int status;

    x = 11; y = 22; width = 33; height = 44;
    status = by_id ? PCore_FragmentInfoById(document, token,
            &x, &y, &width, &height) :
            PCore_FragmentInfoByToken(document, token,
            &x, &y, &width, &height);
    if (status == 0 || x != 11 || y != 22 || width != 33 || height != 44) {
        strcpy(g_fragment_error, "failed fragment lookup changed outputs");
        return 0;
    }
    return 1;
}

/* A fixed-height, coloured heading is far enough from the document bottom
 * that the host's legal physical scroll offset is not bottom-clamped.
 * This checks pixels at the actual viewport top, not just non-zero scroll. */
static int fragment_paint_section(HANDLE document, HDC dc,
        const char *id, int expected_y, int dpi, COLORREF expected_colour)
{
    RECT viewport;
    int x;
    int y;
    int width;
    int height;
    int physical_x;
    int physical_y;
    int physical_width;
    int physical_height;
    COLORREF pixel;
    const char *tag;

    tag = strcmp(id, "version") == 0 ? "h2" : "h3";
    if (PCore_FragmentInfoById(document, id, &x, &y, &width, &height) != 0 ||
            PCore_NodeBox(document, tag, &physical_x, &physical_y,
            &physical_width, &physical_height) != 0 ||
            MulDiv(x, dpi, 96) != physical_x ||
            MulDiv(y, dpi, 96) != physical_y ||
            MulDiv(width, dpi, 96) != physical_width ||
            MulDiv(height, dpi, 96) != physical_height ||
            y != expected_y || physical_y >
            PCore_DocumentHeight(document) - MulDiv(240, dpi, 96)) {
        strcpy(g_fragment_error, "CSS-to-device section alignment failed");
        return 0;
    }
    SetRect(&viewport, 0, 0, MulDiv(320, dpi, 96), MulDiv(240, dpi, 96));
    FillRect(dc, &viewport, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(document, dc, 0, MulDiv(y, dpi, 96));
    pixel = GetPixel(dc, MulDiv(100, dpi, 96), MulDiv(1, dpi, 96));
    if (pixel != expected_colour) {
        _snprintf(g_fragment_error, sizeof(g_fragment_error) - 1,
                "dpi=%d %s top pixel=%06lX expected=%06lX scroll=%d",
                dpi, id, (unsigned long) pixel,
                (unsigned long) expected_colour, MulDiv(y, dpi, 96));
        return 0;
    }
    return 1;
}

static int fragment_case(int dpi, int device_backed)
{
    HANDLE document;
    HANDLE sheet;
    HANDLE other;
    HDC screen_dc;
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    int ok;
    int width;
    int height;
    int field_x;
    int field_y;
    int field_w;
    int field_h;
    int kind;
    int disabled;
    int focus_bytes;
    char href[64];

    document = sheet = other = NULL;
    screen_dc = dc = NULL;
    bitmap = old_bitmap = NULL;
    ok = 0;
    width = MulDiv(320, dpi, 96);
    height = MulDiv(240, dpi, 96);
    if (device_backed) {
        PCore_SetDeviceViewport(width, height, dpi);
    } else {
        PCore_SetViewport(320, 240, 96);
    }
    document = PCore_ParseHTML(g_fragment_html, sizeof(g_fragment_html) - 1);
    sheet = PCore_ParseCSS(g_fragment_css, sizeof(g_fragment_css) - 1,
            "https://positron.local/fragment-dpi.css");
    if (document == NULL || sheet == NULL ||
            !fragment_reject(document, "version", 1) ||
            !fragment_reject(document, "version", 0) ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, width, height) != 0) {
        if (g_fragment_error[0] == '\0') {
            strcpy(g_fragment_error, "fragment fixture parse/style/layout failed");
        }
        goto cleanup;
    }
    if (!fragment_expect(document, "version", 1, 18, 270, dpi) ||
            !fragment_expect(document, "version", 0, 18, 270, dpi) ||
            !fragment_expect(document, "system", 1, 18, 480, dpi) ||
            !fragment_expect(document, "system", 0, 18, 480, dpi) ||
            !fragment_expect(document, "legacy", 0, 18, 360, dpi) ||
            !fragment_expect(document, "shared", 0, 18, 400, dpi) ||
            !fragment_expect(document, "negative", 1, -15, 150, dpi) ||
            !fragment_expect(document, "negative", 0, -15, 150, dpi) ||
            !fragment_reject(document, "missing", 1) ||
            !fragment_reject(document, "missing", 0) ||
            !fragment_reject(document, "hidden", 1) ||
            !fragment_reject(document, "hidden", 0) ||
            !fragment_reject(document, "", 1) ||
            !fragment_reject(document, "", 0) ||
            !fragment_reject(document, NULL, 1) ||
            !fragment_reject(document, NULL, 0) ||
            !fragment_reject(NULL, "version", 1) ||
            !fragment_reject(NULL, "version", 0)) {
        goto cleanup;
    }
    /* Existing libcss negative-length rounding subtracts half a physical
     * pixel before FIXTOINT (floor), so integral left:-14px lays out at
     * -15/-22/-29 device px. Fragment describes that actual box, not the
     * authored length. Check the independent physical API without tolerance. */
    if (PCore_NodeBox(document, "section", &field_x, &field_y,
            &field_w, &field_h) != 0 ||
            field_x != -MulDiv(14, dpi, 96) - 1 ||
            field_y != MulDiv(150, dpi, 96) ||
            field_w != MulDiv(90, dpi, 96) ||
            field_h != MulDiv(24, dpi, 96)) {
        strcpy(g_fragment_error, "negative physical layout/projection mismatch");
        goto cleanup;
    }
    /* Changing global viewport and laying out another document must not
     * change the old document's geometry conversion or paint snapshot. */
    PCore_SetDeviceViewport(320, 240, 96);
    other = PCore_ParseHTML("<html><body>other</body></html>", 31);
    if (other == NULL || PCore_StyleDocument(other, NULL) != 0 ||
            PCore_LayoutDocument(other, 320, 240) != 0 ||
            !fragment_expect(document, "version", 1, 18, 270, dpi) ||
            !fragment_expect(document, "legacy", 0, 18, 360, dpi)) {
        if (g_fragment_error[0] == '\0') {
            strcpy(g_fragment_error, "layout DPI snapshot failed");
        }
        goto cleanup;
    }
    /* Existing physical APIs must keep their units for hit tests/native
     * control projection; they are not part of the Fragment correction. */
    if (PCore_FormControlInfoById(document, "field", &field_x, &field_y,
            &field_w, &field_h, &kind, NULL, &disabled) != 0 ||
            field_x != MulDiv(120, dpi, 96) ||
            field_y != MulDiv(40, dpi, 96) || field_w <= 0 || field_h <= 0 ||
            kind != 3 || disabled || PCore_InteractionSetAt(document,
            field_x + field_w / 2, field_y + field_h / 2,
            PCORE_INTERACTION_FOCUS) < 0 ||
            PCore_InteractionFocusElementId(document, href,
            sizeof(href), &focus_bytes) != 0 || focus_bytes != 5 ||
            strcmp(href, "field") != 0 ||
            PCore_LinkInfoById(document, "link", &field_x, &field_y,
            &field_w, &field_h, href, sizeof(href)) != 0 ||
            PCore_LinkAt(document, field_x + field_w / 2,
            field_y + field_h / 2, href, sizeof(href)) != 1 ||
            strcmp(href, "#system") != 0) {
        strcpy(g_fragment_error, "physical hit/native geometry regressed");
        goto cleanup;
    }
    screen_dc = GetDC(NULL);
    dc = screen_dc != NULL ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = screen_dc != NULL ? CreateCompatibleBitmap(screen_dc,
            width, height) : NULL;
    if (dc == NULL || bitmap == NULL) {
        strcpy(g_fragment_error, "fragment fixture GDI allocation failed");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(dc, bitmap);
    if (!fragment_paint_section(document, dc, "version", 270,
            dpi, RGB(0, 0, 255)) ||
            !fragment_paint_section(document, dc, "system", 480,
            dpi, RGB(0, 255, 0))) {
        goto cleanup;
    }
    ok = 1;
cleanup:
    if (old_bitmap != NULL) { SelectObject(dc, old_bitmap); }
    if (bitmap != NULL) { DeleteObject(bitmap); }
    if (dc != NULL) { DeleteDC(dc); }
    if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
    if (other != NULL) { PCore_FreeDocument(other); }
    if (document != NULL) { PCore_FreeDocument(document); }
    if (sheet != NULL) { PCore_FreeStylesheet(sheet); }
    PCore_SetViewport(320, 240, 96);
    g_fragment_error[sizeof(g_fragment_error) - 1] = '\0';
    return ok;
}

BOOL test1330_core_fragment_dpi_contract(void)
{
    static const int DPI[] = { 96, 144, 192 };
    int index;
    int repeat;

    g_fragment_error[0] = '\0';
    for (repeat = 0; repeat < 2; repeat++) {
        for (index = 0; index < 3; index++) {
            if (!fragment_case(DPI[index], 1)) { return FALSE; }
        }
        if (!fragment_case(96, 0)) { return FALSE; }
    }
    return TRUE;
}

const char *test1330_core_fragment_dpi_last_error(void)
{
    return g_fragment_error;
}
