/* Public Core consumer: geometry, native projection and paint assertions. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "positron_core.h"

static char g_input_height_error[256];
static const char g_input_height_html[] =
    "<html><body><p>Startup page / Home</p>"
    "<p><input id='empty' type='text' size='18' maxlength='1023' disabled></p>"
    "<p id='after'>Enter an HTTP(S) URL.</p>"
    "<p><input id='filled' value='https://www.iana.org/'></p>"
    "<p><input id='secret' type='password' value='gyp'></p>"
    "<p><input id='large' style='font-size:22px;line-height:1.6' value='gyp'></p>"
    "<p><input id='fixed' style='height:8px' value='gyp'></p>"
    "<p><input id='minimum' style='min-height:40px'></p>"
    "<p><input id='maximum' style='max-height:12px'></p>"
    "<p><input id='content' style='box-sizing:content-box;height:9px;"
    "padding:2px;border:1px solid black'></p>"
    "<p><input id='block' style='display:block' value='gyp'></p>"
    "<p><input id='border' style='box-sizing:border-box;height:30px;"
    "padding:2px;border:1px solid black'></p>"
    "<p><input id='zero' style='height:0' value='gyp'></p>"
    "<p id='tail'>Following paragraph: gyp \xe4\xb8\xad\xe6\x96\x87.</p>"
    "</body></html>";
static const char g_input_height_css[] =
    "body{font:14px sans-serif;line-height:1.35;margin:8px}"
    "input{font-size:inherit;background:#ddd}";

static int input_height_layout(HANDLE doc, HANDLE sheet, int dpi, int pass)
{
    int width;
    int height;
    width = MulDiv(pass == 1 ? 320 : 240, dpi, 96);
    height = MulDiv(pass == 1 ? 240 : 320, dpi, 96);
    PCore_SetDeviceViewport(width, height, dpi);
    return PCore_StyleDocument(doc, sheet) == 0 &&
            PCore_LayoutDocument(doc, width, height) == 0;
}

static int input_height_native(const PCoreTextInputInfo *info, int dpi)
{
    HWND parent;
    HWND edit;
    HDC dc;
    HFONT old;
    HFONT font;
    LOGFONTW logfont;
    TEXTMETRICW tm;
    RECT client;
    int ok;
    parent = CreateWindowW(L"STATIC", L"height fixture", WS_POPUP,
            0, 0, 320, 200, NULL, NULL, GetModuleHandle(NULL), NULL);
    edit = parent != NULL ? CreateWindowW(L"EDIT", L"gyp", WS_CHILD | WS_BORDER,
            0, 0, info->width, info->height, parent, NULL,
            GetModuleHandle(NULL), NULL) : NULL;
    ok = 0;
    font = NULL;
    if (edit != NULL) {
        dc = GetDC(edit);
        if (dc != NULL) {
            /* SYSTEM_FONT is meaningful only at this device's real DPI.
             * Synthetic layout DPIs instead use the fixture's CSS font size. */
            if (dpi != GetDeviceCaps(dc, LOGPIXELSY)) {
                memset(&logfont, 0, sizeof(logfont));
                logfont.lfHeight = -MulDiv(14, dpi, 96);
                logfont.lfWeight = FW_NORMAL;
                logfont.lfCharSet = DEFAULT_CHARSET;
                lstrcpyW(logfont.lfFaceName, L"Tahoma");
                font = CreateFontIndirectW(&logfont);
            }
            SendMessage(edit, WM_SETFONT, (WPARAM)(font != NULL ? font :
                    (HFONT)GetStockObject(SYSTEM_FONT)), FALSE);
            old = (HFONT)SelectObject(dc, font != NULL ? font :
                    (HFONT)GetStockObject(SYSTEM_FONT));
            memset(&tm, 0, sizeof(tm));
            if (GetTextMetricsW(dc, &tm) && GetClientRect(edit, &client)) {
                ok = tm.tmHeight > 0 && client.bottom - client.top >= tm.tmHeight;
            }
            SelectObject(dc, old);
            ReleaseDC(edit, dc);
        }
        DestroyWindow(edit);
    }
    if (font != NULL) DeleteObject(font);
    if (parent != NULL) DestroyWindow(parent);
    return ok;
}

static int input_height_paint(HANDLE doc, const PCoreTextInputInfo *info)
{
    HDC screen;
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old;
    RECT rect;
    int x;
    int y;
    int ink;
    screen = GetDC(NULL);
    dc = screen != NULL ? CreateCompatibleDC(screen) : NULL;
    bitmap = screen != NULL ? CreateCompatibleBitmap(screen, 640, 400) : NULL;
    ink = 0;
    if (dc != NULL && bitmap != NULL) {
        old = (HBITMAP)SelectObject(dc, bitmap);
        SetRect(&rect, 0, 0, 640, 400);
        FillRect(dc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
        PCore_PaintDocument(doc, dc, 0, 0);
        for (y = info->y; y < info->y + info->height && y < 400; y++) {
            for (x = info->x; x < info->x + info->width && x < 640; x++) {
                COLORREF pixel;
                pixel = GetPixel(dc, x, y);
                if (pixel != CLR_INVALID && pixel != RGB(255,255,255)) ink++;
            }
        }
        SelectObject(dc, old);
    }
    if (bitmap != NULL) DeleteObject(bitmap);
    if (dc != NULL) DeleteDC(dc);
    if (screen != NULL) ReleaseDC(NULL, screen);
    return ink > 4;
}

BOOL test1346_core_text_input_height(void)
{
    static const int dpis[] = {96, 128, 192};
    static const int author_heights[] = {8, 40, 12, 15};
    HANDLE doc;
    HANDLE sheet;
    PCoreTextInputInfo info[11];
    int baseline[11];
    char value[128];
    char focused[32];
    int dpi_index;
    int dpi;
    int pass;
    int index;
    int stage;
    int after_y;
    int tail_y;
    int tail_h;
    int bytes;
    int ok;
    doc = sheet = NULL;
    ok = 0;
    stage = index = pass = dpi = 0;
    memset(info, 0, sizeof(info));
    for (dpi_index = 0; dpi_index < 3; dpi_index++) {
        dpi = dpis[dpi_index];
        doc = PCore_ParseHTML(g_input_height_html, sizeof(g_input_height_html)-1);
        sheet = PCore_ParseCSS(g_input_height_css, sizeof(g_input_height_css)-1,
                "https://positron.local/input-height.css");
        stage = 1;
        if (doc == NULL || sheet == NULL) goto done;
        for (pass = 0; pass < 3; pass++) {
            stage = 2;
            if (!input_height_layout(doc, sheet, dpi, pass)) goto done;
            for (index = 0; index < 11; index++) {
                stage = 3;
                if (PCore_TextInputInfo(doc, index, &info[index], value,
                        sizeof(value)) != 0) goto done;
                if (index < 4 || index == 8) {
                    if (info[index].height < MulDiv(19, dpi, 96)) goto done;
                }
                if (pass == 0) baseline[index] = info[index].height;
                else if (baseline[index] != info[index].height) goto done;
                if (index > 0 && info[index].y <
                        info[index-1].y + info[index-1].height) goto done;
                if (index >= 4 && index <= 7 &&
                        abs(info[index].height - MulDiv(author_heights[index-4],
                        dpi, 96)) > 1) goto done;
                if (index == 9 && abs(info[index].height -
                        MulDiv(30, dpi, 96)) > 1) goto done;
                if (index == 10 && info[index].height != 0) goto done;
            }
            stage = 4;
            if (info[0].height != info[1].height ||
                    info[2].password != 1 ||
                    info[0].disabled != (pass == 0) ||
                    PCore_TextInputInfo(doc, 0, NULL, value,
                    sizeof(value)) != 0 || strcmp(value, pass == 1 ?
                    "gyp \xe4\xb8\xad\xe6\x96\x87" : "") ||
                    info[1].height != info[2].height ||
                    info[3].height <= info[0].height ||
                    PCore_FragmentInfoById(doc, "after", NULL, &after_y,
                    NULL, NULL) != 0 ||
                    MulDiv(after_y, dpi, 96) < info[0].y + info[0].height ||
                    PCore_FragmentInfoById(doc, "tail", NULL, &tail_y,
                    NULL, &tail_h) != 0 ||
                    PCore_DocumentHeight(doc) < MulDiv(tail_y + tail_h, dpi, 96)) {
                goto done;
            }
            stage = 5;
            if (!input_height_paint(doc, &info[0]) ||
                    !input_height_native(&info[0], dpi)) goto done;
            stage = 6;
            if (PCore_InteractionSetAt(doc, info[1].x + info[1].width / 2,
                    info[1].y + info[1].height / 2, PCORE_INTERACTION_FOCUS) < 0 ||
                    PCore_InteractionFocusElementId(doc, focused,
                    sizeof(focused), &bytes) != 0 || strcmp(focused, "filled")) {
                goto done;
            }
            PCore_InteractionClear(doc, PCORE_INTERACTION_FOCUS);
            stage = 7;
            if (pass == 0 &&
                    (PCore_TextInputSetValue(doc, 0, "gyp") != 2 ||
                    PCore_NodeRemoveAttributeById(doc, "empty", "disabled") != 0 ||
                    PCore_TextInputSetValue(doc, 0,
                    "gyp \xe4\xb8\xad\xe6\x96\x87") != 0)) goto done;
            if (pass == 1 && PCore_TextInputSetValue(doc, 0, "") != 0) goto done;
        }
        PCore_FreeDocument(doc); doc = NULL;
        PCore_FreeStylesheet(sheet); sheet = NULL;
    }
    ok = 1;
done:
    _snprintf(g_input_height_error, sizeof(g_input_height_error)-1,
            "stage=%d dpi=%d pass=%d index=%d rect=%dx%d at=%d,%d",
            stage, dpi, pass, index, info[index < 11 ? index : 0].width,
            info[index < 11 ? index : 0].height, info[0].x, info[0].y);
    g_input_height_error[sizeof(g_input_height_error)-1] = 0;
    if (doc != NULL) PCore_FreeDocument(doc);
    if (sheet != NULL) PCore_FreeStylesheet(sheet);
    PCore_SetViewport(320, 240, 96);
    return ok;
}

const char *test1346_core_text_input_height_error(void)
{
    return g_input_height_error;
}
