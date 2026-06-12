/*
 * pcore_plot_gdi.c - GDI backing for NetSurf's plotter interface.
 *
 * Milestone H, M1. NetSurf's redraw.c emits all drawing through a
 * `struct plotter_table` of function pointers (target-independent plotting).
 * To paint the laid-out page with NetSurf's own redraw engine we back that
 * table with WinCE GDI here - this is the "deal with GDI" half of porting the
 * real layout/redraw engine; the engine itself (layout.c / redraw.c) is ported
 * in later milestones and will call into this table.
 *
 * M1 stands alone: PCore_PlotTest() builds a redraw_context around a given DC
 * and drives the plotter directly (no layout yet), so the table, colour
 * conversion, pen/brush handling and text baseline can be device-verified
 * before redraw.c is wired in.
 *
 * Coordinates handed to the plotter are final device pixels: NetSurf's redraw
 * applies the scroll/origin itself (via content_redraw_data), so priv carries
 * only the target DC.
 *
 * C89 only.
 */

#include <windows.h>
#include <string.h>

#include "utils/errors.h"        /* nserror - plotters.h uses it un-included */
#include "netsurf/types.h"       /* colour (XBGR), struct rect */
#include "netsurf/plot_style.h"  /* plot_style_t, plot_font_style_t */
#include "netsurf/plotters.h"    /* struct plotter_table, redraw_context */

#include "positron_core.h"

/* Private context behind redraw_context.priv. */
typedef struct pcore_plot_ctx {
    HDC hdc;
} pcore_plot_ctx;

/* NetSurf `colour` is XBGR (red in the low byte) - identical byte order to a
 * Win32 COLORREF (0x00BBGGRR), so the conversion is just masking the alpha. */
static COLORREF ns_to_colorref(colour c)
{
    return (COLORREF) (c & 0x00FFFFFF);
}

/* WinCE/WM GDI only renders solid cosmetic pens - PS_DOT / PS_DASH are
 * neither defined in the WM SDK nor drawable here - so dotted/dashed CSS
 * strokes fall back to solid. */
static int ns_pen_style(plot_operation_type_t t)
{
    (void) t;
    return PS_SOLID;
}

/* Build an HFONT from a NetSurf plot_font_style_t, scaled to the DC's DPI.
 * (M2 will grow this into the shared gui_layout_table font path.) */
static HFONT pcore_plot_font(HDC hdc, const plot_font_style_t *fstyle)
{
    LOGFONTW lf;
    int dpi;
    int pt;
    int px;
    const WCHAR *face;

    memset(&lf, 0, sizeof(lf));

    dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpi <= 0) {
        dpi = 96;
    }
    pt = plot_style_fixed_to_int(fstyle->size);   /* size is in pt (fixed) */
    if (pt < 1) {
        pt = 1;
    }
    px = MulDiv(pt, dpi, 72);
    if (px < 1) {
        px = 1;
    }

    lf.lfHeight = -px;
    lf.lfWeight = fstyle->weight;   /* CSS 100..900 maps directly to LOGFONT */
    if (fstyle->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) {
        lf.lfItalic = (BYTE) 1;
    }
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;

    switch (fstyle->family) {
    case PLOT_FONT_FAMILY_SERIF:
        face = L"Times New Roman";
        lf.lfPitchAndFamily = (BYTE) (DEFAULT_PITCH | FF_ROMAN);
        break;
    case PLOT_FONT_FAMILY_MONOSPACE:
        face = L"Courier New";
        lf.lfPitchAndFamily = (BYTE) (FIXED_PITCH | FF_MODERN);
        break;
    default:
        face = L"Tahoma";
        lf.lfPitchAndFamily = (BYTE) (DEFAULT_PITCH | FF_DONTCARE);
        break;
    }
    lstrcpyW(lf.lfFaceName, face);
    return CreateFontIndirectW(&lf);
}

/* ------------------------------------------------------------------ */
/* plotter callbacks                                                   */
/* ------------------------------------------------------------------ */

static nserror plot_clip(const struct redraw_context *ctx,
        const struct rect *clip)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    HRGN rgn = CreateRectRgn(clip->x0, clip->y0, clip->x1, clip->y1);
    if (rgn != NULL) {
        SelectClipRgn(p->hdc, rgn);
        DeleteObject(rgn);
    }
    return NSERROR_OK;
}

static nserror plot_rectangle(const struct redraw_context *ctx,
        const plot_style_t *style, const struct rect *r)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    RECT rc;

    if (style->fill_type != PLOT_OP_TYPE_NONE) {
        HBRUSH br = CreateSolidBrush(ns_to_colorref(style->fill_colour));
        if (br != NULL) {
            rc.left = r->x0;
            rc.top = r->y0;
            rc.right = r->x1;
            rc.bottom = r->y1;
            FillRect(p->hdc, &rc, br);   /* excludes right/bottom: NS grid */
            DeleteObject(br);
        }
    }
    if (style->stroke_type != PLOT_OP_TYPE_NONE) {
        int w = plot_style_fixed_to_int(style->stroke_width);
        HPEN pen;
        if (w < 1) {
            w = 1;
        }
        pen = CreatePen(ns_pen_style(style->stroke_type), w,
                ns_to_colorref(style->stroke_colour));
        if (pen != NULL) {
            HPEN oldpen = (HPEN) SelectObject(p->hdc, pen);
            HBRUSH oldbr = (HBRUSH) SelectObject(p->hdc,
                    (HBRUSH) GetStockObject(HOLLOW_BRUSH));
            Rectangle(p->hdc, r->x0, r->y0, r->x1, r->y1);
            SelectObject(p->hdc, oldbr);
            SelectObject(p->hdc, oldpen);
            DeleteObject(pen);
        }
    }
    return NSERROR_OK;
}

static nserror plot_line(const struct redraw_context *ctx,
        const plot_style_t *style, const struct rect *l)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    int w = plot_style_fixed_to_int(style->stroke_width);
    HPEN pen;

    if (w < 1) {
        w = 1;
    }
    pen = CreatePen(ns_pen_style(style->stroke_type), w,
            ns_to_colorref(style->stroke_colour));
    if (pen != NULL) {
        HPEN old = (HPEN) SelectObject(p->hdc, pen);
        MoveToEx(p->hdc, l->x0, l->y0, NULL);
        LineTo(p->hdc, l->x1, l->y1);
        SelectObject(p->hdc, old);
        DeleteObject(pen);
    }
    return NSERROR_OK;
}

static nserror plot_text(const struct redraw_context *ctx,
        const plot_font_style_t *fstyle, int x, int y,
        const char *text, size_t length)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    WCHAR wbuf[1024];
    int wl;
    HFONT f;
    HFONT old;
    TEXTMETRICW tm;
    int top;

    wl = MultiByteToWideChar(CP_UTF8, 0, text, (int) length, wbuf, 1024);
    if (wl <= 0) {
        return NSERROR_OK;
    }
    f = pcore_plot_font(p->hdc, fstyle);
    if (f == NULL) {
        return NSERROR_OK;
    }
    old = (HFONT) SelectObject(p->hdc, f);
    SetTextColor(p->hdc, ns_to_colorref(fstyle->foreground));
    SetBkMode(p->hdc, TRANSPARENT);
    /* NetSurf passes the text BASELINE as y; GDI draws from the cell top. */
    top = y;
    if (GetTextMetricsW(p->hdc, &tm)) {
        top = y - tm.tmAscent;
    }
    ExtTextOutW(p->hdc, x, top, 0, NULL, wbuf, wl, NULL);
    SelectObject(p->hdc, old);
    DeleteObject(f);
    return NSERROR_OK;
}

static nserror plot_disc(const struct redraw_context *ctx,
        const plot_style_t *style, int x, int y, int radius)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    HBRUSH br;
    HBRUSH oldbr;
    HPEN pen;
    HPEN oldpen;
    int filled = (style->fill_type != PLOT_OP_TYPE_NONE);

    br = filled ? CreateSolidBrush(ns_to_colorref(style->fill_colour))
                : (HBRUSH) GetStockObject(HOLLOW_BRUSH);
    pen = CreatePen(PS_SOLID, 1, ns_to_colorref(
            (style->stroke_type != PLOT_OP_TYPE_NONE)
                    ? style->stroke_colour : style->fill_colour));
    oldbr = (HBRUSH) SelectObject(p->hdc, br);
    oldpen = (HPEN) SelectObject(p->hdc, pen);
    Ellipse(p->hdc, x - radius, y - radius, x + radius, y + radius);
    SelectObject(p->hdc, oldpen);
    SelectObject(p->hdc, oldbr);
    if (pen != NULL) {
        DeleteObject(pen);
    }
    if (filled && br != NULL) {
        DeleteObject(br);
    }
    return NSERROR_OK;
}

static nserror plot_arc(const struct redraw_context *ctx,
        const plot_style_t *style, int x, int y,
        int radius, int angle1, int angle2)
{
    /* Rare (a few CSS shapes). Stub for now; redraw works without it. */
    (void) ctx; (void) style; (void) x; (void) y;
    (void) radius; (void) angle1; (void) angle2;
    return NSERROR_OK;
}

static nserror plot_polygon(const struct redraw_context *ctx,
        const plot_style_t *style, const int *poly, unsigned int n)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    POINT pts[64];
    unsigned int i;
    HBRUSH br;
    HBRUSH oldbr;
    HPEN oldpen;

    if (n < 2 || n > 64) {
        return NSERROR_OK;
    }
    for (i = 0; i < n; i++) {
        pts[i].x = poly[i * 2];
        pts[i].y = poly[i * 2 + 1];
    }
    br = CreateSolidBrush(ns_to_colorref(style->fill_colour));
    oldbr = (HBRUSH) SelectObject(p->hdc,
            (br != NULL) ? br : (HBRUSH) GetStockObject(HOLLOW_BRUSH));
    oldpen = (HPEN) SelectObject(p->hdc, (HPEN) GetStockObject(NULL_PEN));
    Polygon(p->hdc, pts, (int) n);
    SelectObject(p->hdc, oldpen);
    SelectObject(p->hdc, oldbr);
    if (br != NULL) {
        DeleteObject(br);
    }
    return NSERROR_OK;
}

static nserror plot_path(const struct redraw_context *ctx,
        const plot_style_t *style, const float *p,
        unsigned int n, const float transform[6])
{
    /* Bezier paths (SVG / <canvas>) - not needed for HTML box redraw. */
    (void) ctx; (void) style; (void) p; (void) n; (void) transform;
    return NSERROR_OK;
}

static nserror plot_bitmap(const struct redraw_context *ctx,
        struct bitmap *bitmap, int x, int y, int width, int height,
        colour bg, bitmap_flags_t flags)
{
    /* Images land in a later milestone (WM Imaging API); stubbed so the box
     * redraw path - which only calls this when box->object != NULL - links. */
    (void) ctx; (void) bitmap; (void) x; (void) y;
    (void) width; (void) height; (void) bg; (void) flags;
    return NSERROR_OK;
}

/* The GDI plotter table. Field order MUST match struct plotter_table:
 * clip, arc, disc, line, rectangle, polygon, path, bitmap, text,
 * group_start, group_end, flush, option_knockout. Non-static: redraw.c
 * (milestone M5) will reference it. */
const struct plotter_table pcore_gdi_plotters = {
    plot_clip,
    plot_arc,
    plot_disc,
    plot_line,
    plot_rectangle,
    plot_polygon,
    plot_path,
    plot_bitmap,
    plot_text,
    NULL,   /* group_start */
    NULL,   /* group_end   */
    NULL,   /* flush       */
    false   /* option_knockout */
};

/* ------------------------------------------------------------------ */
/* M1 self-test: drive the plotter directly (no layout engine yet)     */
/* ------------------------------------------------------------------ */

PCORE_API void PCore_PlotTest(HDC hdc)
{
    pcore_plot_ctx        pctx;
    struct redraw_context rc;
    plot_style_t          box;
    plot_style_t          ln;
    plot_font_style_t     fs;
    struct rect           r;

    if (hdc == NULL) {
        return;
    }

    pctx.hdc = hdc;
    memset(&rc, 0, sizeof(rc));
    rc.interactive = false;
    rc.background_images = false;
    rc.plot = &pcore_gdi_plotters;
    rc.priv = &pctx;

    /* A grey box with a 2px red border. */
    memset(&box, 0, sizeof(box));
    box.fill_type = PLOT_OP_TYPE_SOLID;
    box.fill_colour = 0x00d9d9d9;                  /* grey (channel-neutral) */
    box.stroke_type = PLOT_OP_TYPE_SOLID;
    box.stroke_width = plot_style_int_to_fixed(2);
    box.stroke_colour = 0x000000ff;                /* XBGR: red */
    r.x0 = 10; r.y0 = 10; r.x1 = 220; r.y1 = 90;
    rc.plot->rectangle(&rc, &box, &r);

    /* A blue horizontal line below it. */
    memset(&ln, 0, sizeof(ln));
    ln.stroke_type = PLOT_OP_TYPE_SOLID;
    ln.stroke_width = plot_style_int_to_fixed(1);
    ln.stroke_colour = 0x00ff0000;                 /* XBGR: blue */
    r.x0 = 10; r.y0 = 104; r.x1 = 220; r.y1 = 104;
    rc.plot->line(&rc, &ln, &r);

    /* Black text inside the box (y is the BASELINE). */
    memset(&fs, 0, sizeof(fs));
    fs.family = PLOT_FONT_FAMILY_SANS_SERIF;
    fs.size = plot_style_int_to_fixed(12);         /* 12pt */
    fs.weight = 400;
    fs.flags = FONTF_NONE;
    fs.foreground = 0x00000000;                    /* black */
    fs.background = 0x00ffffff;
    rc.plot->text(&rc, &fs, 20, 52,
            "Positron GDI plotter OK", 23);
}
