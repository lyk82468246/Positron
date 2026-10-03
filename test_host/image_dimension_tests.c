/* Public Core consumer fixtures: HTML img dimensions, cascade and paint. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "positron_core.h"

static char dimension_error[256];

static int dimension_fetch(void *pw, const char *url, char **data, int *length)
{
    char svg[256];
    const char *body;
    int width;
    int height;

    (void) pw;
    *data = NULL;
    *length = 0;
    if (strcmp(url, "/author.css") == 0) {
        body = "#external{width:60px;height:auto;}";
    } else {
        width = 390; height = 408;
        if (strcmp(url, "/windows.svg") == 0) { width = 150; height = 122; }
        else if (strcmp(url, "/apple.svg") == 0) { width = 150; height = 150; }
        else if (strcmp(url, "/os2.svg") == 0) { width = 355; height = 355; }
        else if (strcmp(url, "/dos.svg") != 0) { return 1; }
        _snprintf(svg, sizeof(svg) - 1,
                "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d'>"
                "<rect width='%d' height='%d' fill='#ff0000'/></svg>",
                width, height, width, height);
        svg[sizeof(svg) - 1] = '\0';
        body = svg;
    }
    *length = (int) strlen(body);
    *data = (char *) malloc((size_t) *length);
    if (*data == NULL) { return 1; }
    memcpy(*data, body, (size_t) *length);
    return 0;
}

static void dimension_free(void *pw, char *data)
{
    (void) pw;
    free(data);
}

static int dimension_expect(HANDLE document, const char *id,
        int expected_width, int expected_height, int dpi)
{
    int width;
    int height;
    int x;
    int y;
    int status;

    width = height = -999;
    status = PCore_NodeRelationById(document, id,
            PCORE_NODE_RELATION_LAYOUT_RECT_WIDTH, 0, NULL, 0, NULL, &width);
    /* A zero-area image has no usable geometry; the relation API clears its
     * output before failure. Ignoring width=0 would expose natural size. */
    if (expected_width == 0) {
        if (status == 2 && width == 0) { return 1; }
        strcpy(dimension_error, "zero image must not expose natural-size geometry");
        return 0;
    }
    if (status != 0 || PCore_NodeRelationById(document, id,
            PCORE_NODE_RELATION_LAYOUT_RECT_HEIGHT, 0, NULL, 0, NULL, &height) != 0 ||
            width != expected_width || height != expected_height) {
        _snprintf(dimension_error, sizeof(dimension_error) - 1,
                "dpi=%d id=%s CSS rect=%d,%d expected=%d,%d rc=%d", dpi, id,
                width, height, expected_width, expected_height, status);
        return 0;
    }
    if (expected_width > 0 && expected_height > 0 &&
            (PCore_FragmentInfoById(document, id, &x, &y, &width, &height) != 0 ||
            width != expected_width || height != expected_height)) {
        strcpy(dimension_error, "fragment/image rect mismatch");
        return 0;
    }
    return 1;
}

static int dimension_case(int dpi)
{
    static const char HTML[] =
        "<!doctype html><html><head><link rel='stylesheet' href='/author.css'>"
        "<style>#block{width:40px;height:auto;}</style></head><body>"
        "<a href='#destination' style='display:block;width:80px'>"
        "<img id='dos' src='/dos.svg' width='80px'></a>"
        "<img id='windows' src='/windows.svg' width='80'>"
        "<img id='apple' src='/apple.svg' width='80'>"
        "<img id='os2' src='/os2.svg' width='80'>"
        "<img id='height' src='/apple.svg' height='40px'>"
        "<img id='both' src='/dos.svg' width='80' height='30'>"
        "<img id='block' src='/apple.svg' width='80'>"
        "<img id='external' src='/apple.svg' width='80'>"
        "<img id='inline' src='/apple.svg' width='80' style='width:50px'>"
        "<img id='auto' src='/apple.svg' width='80' style='width:auto'>"
        "<img id='missing' src='/apple.svg'>"
        "<img id='empty' src='/apple.svg' width=''>"
        "<img id='invalid' src='/apple.svg' width='bad'>"
        "<img id='negative' src='/apple.svg' width='-80'>"
        "<img id='sign' src='/apple.svg' width='+80'>"
        "<img id='overflow' src='/apple.svg' width='999999999999999999999'>"
        "<img id='zero' src='/apple.svg' width='0'>"
        "<img id='space' src='/apple.svg' width=' &#9;080px'>"
        "<img id='fraction' src='/apple.svg' width='80.5px'>"
        "<img id='percent' src='/apple.svg' width='25%'>"
        "<img id='units' src='/apple.svg' width='80em'>"
        "<img id='max' src='/apple.svg' width='80' style='max-width:40px'>"
        "<img id='min' src='/apple.svg' width='30' style='min-width:60px'>"
        "<div class='media'><img id='flex' src='/dos.svg' width='80px'>"
        "<img id='flex-css' src='/apple.svg' width='80' style='width:40px'>"
        "<img id='flex-min' src='/apple.svg' width='30' style='min-width:60px'>"
        "<div style='flex:1'>Text</div></div>"
        "<img id='budget' src='/apple.svg'>"
        "<img id='mutation' src='/apple.svg' width='80'>"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;}body{width:240px;}"
        "img{display:block;margin:0;padding:0;border:0;}"
        ".media{display:flex;align-items:flex-start;width:240px;}";
    HANDLE document;
    HANDLE sheet;
    int found;
    int fetched;
    int ok;
    int width;
    int height;
    int x;
    int y;
    HDC screen;
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    COLORREF inside;
    COLORREF outside;
    char link[64];
    char oversized[130];
    PCoreImageDecodeStats first_decode;
    PCoreImageDecodeStats last_decode;

    document = sheet = NULL;
    screen = dc = NULL;
    bitmap = old_bitmap = NULL;
    width = MulDiv(240, dpi, 96);
    height = MulDiv(200, dpi, 96);
    ok = 0;
    PCore_SetDeviceViewport(width, height, dpi);
    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1, "https://positron.local/base.css");
    memset(oversized, '0', sizeof(oversized));
    oversized[128] = '1';
    oversized[129] = '\0';
    if (document == NULL || sheet == NULL ||
            PCore_NodeSetAttributeById(document, "budget", "width", oversized) != 0 ||
            PCore_StyleDocumentEx(document,
            sheet, dimension_fetch, dimension_free, NULL) != 0 ||
            PCore_FetchImageResources(document, dimension_fetch, dimension_free,
            NULL, &found, &fetched) != 0 || found != 28 || fetched != 28 ||
            PCore_LayoutDocument(document, width, height) != 0) {
        strcpy(dimension_error, "image dimensions parse/style/fetch/layout failed");
        goto cleanup;
    }
    if (!dimension_expect(document, "dos", 80, 83, dpi) ||
            !dimension_expect(document, "windows", 80, 65, dpi) ||
            !dimension_expect(document, "apple", 80, 80, dpi) ||
            !dimension_expect(document, "os2", 80, 80, dpi) ||
            !dimension_expect(document, "height", 40, 40, dpi) ||
            !dimension_expect(document, "both", 80, 30, dpi) ||
            !dimension_expect(document, "block", 40, 40, dpi) ||
            !dimension_expect(document, "external", 60, 60, dpi) ||
            !dimension_expect(document, "inline", 50, 50, dpi) ||
            !dimension_expect(document, "auto", 150, 150, dpi) ||
            !dimension_expect(document, "missing", 150, 150, dpi) ||
            !dimension_expect(document, "empty", 150, 150, dpi) ||
            !dimension_expect(document, "invalid", 150, 150, dpi) ||
            !dimension_expect(document, "negative", 150, 150, dpi) ||
            !dimension_expect(document, "sign", 150, 150, dpi) ||
            !dimension_expect(document, "overflow", 150, 150, dpi) ||
            !dimension_expect(document, "zero", 0, 0, dpi) ||
            !dimension_expect(document, "space", 80, 80, dpi) ||
            !dimension_expect(document, "fraction", dpi == 96 ? 81 : 80,
                    dpi == 96 ? 81 : 80, dpi) ||
            !dimension_expect(document, "percent", 60, 60, dpi) ||
            !dimension_expect(document, "units", 80, 80, dpi) ||
            !dimension_expect(document, "max", 40, 40, dpi) ||
            !dimension_expect(document, "min", 60, 60, dpi) ||
            !dimension_expect(document, "flex", 80, 83, dpi) ||
            !dimension_expect(document, "flex-css", 40, 40, dpi) ||
            !dimension_expect(document, "flex-min", 60, 60, dpi) ||
            !dimension_expect(document, "budget", 150, 150, dpi) ||
            PCore_DocumentWidth(document) > width) {
        goto cleanup;
    }
    screen = GetDC(NULL);
    dc = screen != NULL ? CreateCompatibleDC(screen) : NULL;
    bitmap = screen != NULL ? CreateCompatibleBitmap(screen, width, height) : NULL;
    if (dc == NULL || bitmap == NULL) { goto cleanup; }
    old_bitmap = (HBITMAP) SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, width, height, WHITENESS);
    PCore_PaintDocument(document, dc, 0, 0);
    inside = GetPixel(dc, MulDiv(40, dpi, 96), MulDiv(40, dpi, 96));
    outside = GetPixel(dc, MulDiv(90, dpi, 96), MulDiv(40, dpi, 96));
    x = y = -999;
    if (PCore_NodeBox(document, "img", &x, &y, &width, &height) != 0) {
        strcpy(dimension_error, "physical image box unavailable");
        goto cleanup;
    }
    if (inside != RGB(255, 0, 0) || outside != RGB(255, 255, 255) ||
            width != MulDiv(80, dpi, 96) || height != width * 408 / 390) {
        _snprintf(dimension_error, sizeof(dimension_error) - 1,
                "dpi=%d paint inside=%08lx outside=%08lx physical=%d,%d at=%d,%d",
                dpi, (unsigned long) inside, (unsigned long) outside,
                width, height, x, y);
        goto cleanup;
    }
    if (PCore_LinkAt(document, MulDiv(40, dpi, 96), MulDiv(40, dpi, 96),
            link, sizeof(link)) != 1 || strcmp(link, "#destination") != 0 ||
            PCore_LinkAt(document, MulDiv(90, dpi, 96), MulDiv(40, dpi, 96),
            link, sizeof(link)) != 0) {
        strcpy(dimension_error, "physical image link hit bounds mismatch");
        goto cleanup;
    }
    memset(&first_decode, 0, sizeof(first_decode));
    if (PCore_GetImageDecodeStats(document, &first_decode) != 0 ||
            first_decode.svg_creates != 4) {
        strcpy(dimension_error, "duplicate image resources did not share decoding");
        goto cleanup;
    }
    if (PCore_NodeSetAttributeById(document, "mutation", "width", "40") != 0 ||
            PCore_FragmentInfoById(document, "mutation", NULL, NULL, NULL, NULL) == 0) {
        strcpy(dimension_error, "attribute mutation did not invalidate layout");
        goto cleanup;
    }
    PCore_SetDeviceViewport(MulDiv(240, dpi, 96), MulDiv(200, dpi, 96), dpi);
    if (PCore_StyleDocumentEx(document, sheet, dimension_fetch,
            dimension_free, NULL) != 0 || PCore_LayoutDocument(document,
            MulDiv(240, dpi, 96), MulDiv(200, dpi, 96)) != 0 ||
            !dimension_expect(document, "mutation", 40, 40, dpi) ||
            PCore_NodeRemoveAttributeById(document, "mutation", "width") != 0) {
        goto cleanup;
    }
    PCore_SetDeviceViewport(MulDiv(240, dpi, 96), MulDiv(200, dpi, 96), dpi);
    if (PCore_StyleDocumentEx(document, sheet, dimension_fetch,
            dimension_free, NULL) != 0 || PCore_LayoutDocument(document,
            MulDiv(240, dpi, 96), MulDiv(200, dpi, 96)) != 0 ||
            !dimension_expect(document, "mutation", 150, 150, dpi)) { goto cleanup; }
    if (PCore_NodeSetAttributeById(document, "mutation", "height", "30") != 0) {
        goto cleanup;
    }
    PCore_SetDeviceViewport(MulDiv(240, dpi, 96), MulDiv(200, dpi, 96), dpi);
    if (PCore_StyleDocumentEx(document, sheet, dimension_fetch,
            dimension_free, NULL) != 0 || PCore_LayoutDocument(document,
            MulDiv(240, dpi, 96), MulDiv(200, dpi, 96)) != 0 ||
            !dimension_expect(document, "mutation", 30, 30, dpi)) { goto cleanup; }
    memset(&last_decode, 0, sizeof(last_decode));
    if (PCore_GetImageDecodeStats(document, &last_decode) != 0 ||
            last_decode.svg_creates != first_decode.svg_creates) {
        strcpy(dimension_error, "dimension mutation reparsed cached images");
        goto cleanup;
    }
    ok = 1;
cleanup:
    if (old_bitmap != NULL) { SelectObject(dc, old_bitmap); }
    if (bitmap != NULL) { DeleteObject(bitmap); }
    if (dc != NULL) { DeleteDC(dc); }
    if (screen != NULL) { ReleaseDC(NULL, screen); }
    if (document != NULL) { PCore_FreeDocument(document); }
    if (sheet != NULL) { PCore_FreeStylesheet(sheet); }
    PCore_SetViewport(240, 320, 96);
    dimension_error[sizeof(dimension_error) - 1] = '\0';
    return ok;
}

BOOL test1340_core_image_dimension_contract(void)
{
    dimension_error[0] = '\0';
    return dimension_case(96) && dimension_case(192);
}

const char *test1340_core_image_dimension_last_error(void)
{
    return dimension_error;
}
