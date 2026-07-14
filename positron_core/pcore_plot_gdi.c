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
#include <stdlib.h>              /* malloc / free for measurement buffers */

#include "utils/errors.h"        /* nserror - plotters.h uses it un-included */
#include "netsurf/types.h"       /* colour (XBGR), struct rect */
#include "netsurf/plot_style.h"  /* plot_style_t, plot_font_style_t */
#include "netsurf/plotters.h"    /* struct plotter_table, redraw_context */
#include "netsurf/bitmap.h"      /* cached WM Imaging image carrier */
#include "netsurf/layout.h"      /* struct gui_layout_table (font measure) */

#include "positron_core.h"
#include "positron_image.h"

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

/* Draw a line honouring dotted/dashed styles by hand. WinCE/WM GDI pens are
 * solid-only (no PS_DOT/PS_DASH), so rather than drop the style - which would
 * lose the page's real border appearance - we step along the line emitting
 * dash segments ourselves. Works for any direction; exact for the axis-aligned
 * border edges this is used for. */
static void stroke_line(HDC hdc, int x0, int y0, int x1, int y1,
        int w, COLORREF col, plot_operation_type_t st)
{
    HPEN pen = CreatePen(PS_SOLID, (w < 1) ? 1 : w, col);
    HPEN old;

    if (pen == NULL) {
        return;
    }
    old = (HPEN) SelectObject(hdc, pen);

    if (st == PLOT_OP_TYPE_DOT || st == PLOT_OP_TYPE_DASH) {
        int dx = x1 - x0;
        int dy = y1 - y0;
        int adx = (dx < 0) ? -dx : dx;
        int ady = (dy < 0) ? -dy : dy;
        int len = (adx > ady) ? adx : ady;
        int unit = (w < 1) ? 1 : w;
        int on = (st == PLOT_OP_TYPE_DOT) ? unit : (unit * 3);
        int t = 0;

        if (len <= 0) {
            MoveToEx(hdc, x0, y0, NULL);
            LineTo(hdc, x1, y1);
        } else {
            while (t < len) {
                int e = t + on;
                int sx;
                int sy;
                int ex;
                int ey;
                if (e > len) {
                    e = len;
                }
                sx = x0 + (dx * t) / len;
                sy = y0 + (dy * t) / len;
                ex = x0 + (dx * e) / len;
                ey = y0 + (dy * e) / len;
                MoveToEx(hdc, sx, sy, NULL);
                LineTo(hdc, ex, ey);
                t = e + on;   /* gap length == dash length */
            }
        }
    } else {
        MoveToEx(hdc, x0, y0, NULL);
        LineTo(hdc, x1, y1);
    }

    SelectObject(hdc, old);
    DeleteObject(pen);
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
        COLORREF col = ns_to_colorref(style->stroke_colour);
        if (w < 1) {
            w = 1;
        }
        if (style->stroke_type == PLOT_OP_TYPE_SOLID) {
            HPEN pen = CreatePen(PS_SOLID, w, col);
            if (pen != NULL) {
                HPEN oldpen = (HPEN) SelectObject(p->hdc, pen);
                HBRUSH oldbr = (HBRUSH) SelectObject(p->hdc,
                        (HBRUSH) GetStockObject(HOLLOW_BRUSH));
                Rectangle(p->hdc, r->x0, r->y0, r->x1, r->y1);
                SelectObject(p->hdc, oldbr);
                SelectObject(p->hdc, oldpen);
                DeleteObject(pen);
            }
        } else {
            /* Dotted/dashed: stroke the four edges by hand. */
            stroke_line(p->hdc, r->x0, r->y0, r->x1, r->y0, w, col,
                    style->stroke_type);
            stroke_line(p->hdc, r->x1, r->y0, r->x1, r->y1, w, col,
                    style->stroke_type);
            stroke_line(p->hdc, r->x1, r->y1, r->x0, r->y1, w, col,
                    style->stroke_type);
            stroke_line(p->hdc, r->x0, r->y1, r->x0, r->y0, w, col,
                    style->stroke_type);
        }
    }
    return NSERROR_OK;
}

static nserror plot_line(const struct redraw_context *ctx,
        const plot_style_t *style, const struct rect *l)
{
    pcore_plot_ctx *p = (pcore_plot_ctx *) ctx->priv;
    int w = plot_style_fixed_to_int(style->stroke_width);

    if (w < 1) {
        w = 1;
    }
    stroke_line(p->hdc, l->x0, l->y0, l->x1, l->y1, w,
            ns_to_colorref(style->stroke_colour), style->stroke_type);
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

static nserror pcore_plot_bitmap_once(pcore_plot_ctx *p,
        struct bitmap *bitmap, int x, int y, int width, int height)
{
    if (bitmap->kind == PCORE_BITMAP_SVG && bitmap->svg != NULL) {
        return PImage_DrawSvg((PIMAGE_SVG) bitmap->svg, p->hdc,
                x, y, width, height) == PIMAGE_OK ?
                NSERROR_OK : NSERROR_INVALID;
    }
    if (bitmap->kind == PCORE_BITMAP_WM_IMAGE &&
            bitmap->native_image != NULL) {
        return PImage_DrawBitmap((PIMAGE_BITMAP) bitmap->native_image,
                p->hdc, x, y, width, height) == PIMAGE_OK ?
                NSERROR_OK : NSERROR_INVALID;
    }
    return NSERROR_INVALID;
}

static nserror plot_bitmap(const struct redraw_context *ctx,
        struct bitmap *bitmap, int x, int y, int width, int height,
        colour bg, bitmap_flags_t flags)
{
    pcore_plot_ctx *p;
    RECT clip;
    int repeat_x;
    int repeat_y;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    int px;
    int py;
    nserror err;

    (void) bg;
    if (ctx == NULL || ctx->priv == NULL || bitmap == NULL) {
        return NSERROR_INVALID;
    }
    p = (pcore_plot_ctx *) ctx->priv;
    if (p->hdc == NULL || width <= 0 || height <= 0) {
        return NSERROR_OK;
    }
    repeat_x = (flags & BITMAPF_REPEAT_X) != 0;
    repeat_y = (flags & BITMAPF_REPEAT_Y) != 0;
    if (!repeat_x && !repeat_y) {
        return pcore_plot_bitmap_once(p, bitmap, x, y, width, height);
    }
    if (GetClipBox(p->hdc, &clip) == ERROR) {
        clip.left = x;
        clip.top = y;
        clip.right = x + width;
        clip.bottom = y + height;
    }

    start_x = x;
    start_y = y;
    if (repeat_x) {
        while (start_x > clip.left) {
            start_x -= width;
        }
        if (start_x + width <= clip.left) {
            start_x += ((clip.left - start_x) / width) * width;
            while (start_x + width <= clip.left) {
                start_x += width;
            }
        }
    }
    if (repeat_y) {
        while (start_y > clip.top) {
            start_y -= height;
        }
        if (start_y + height <= clip.top) {
            start_y += ((clip.top - start_y) / height) * height;
            while (start_y + height <= clip.top) {
                start_y += height;
            }
        }
    }
    end_x = repeat_x ? clip.right : start_x + 1;
    end_y = repeat_y ? clip.bottom : start_y + 1;
    for (px = start_x; px < end_x; px += width) {
        for (py = start_y; py < end_y; py += height) {
            err = pcore_plot_bitmap_once(p, bitmap, px, py, width, height);
            if (err != NSERROR_OK) {
                return err;
            }
        }
    }
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

/* ================================================================== */
/* M2: GDI font-measurement table (struct gui_layout_table)            */
/* layout.c (font_func) and redraw.c (guit->layout) both measure text   */
/* through this. It works in UTF-8 byte offsets while GDI measures       */
/* UTF-16, so we decode UTF-8 -> UTF-16 keeping each unit's byte offset. */
/* ================================================================== */

/* Lazily-created screen-compatible DC for measurement (process lifetime). */
static HDC g_measure_dc = NULL;

static HDC pcore_measure_dc(void)
{
    if (g_measure_dc == NULL) {
        g_measure_dc = CreateCompatibleDC(NULL);
    }
    return g_measure_dc;
}

/* Small font cache shared by the measurement table (the hot path during
 * layout). Keyed on the resolved px size + weight + italic + family. */
typedef struct pcore_fc2 {
    int   px;
    int   weight;
    int   italic;
    int   family;
    HFONT font;
} pcore_fc2;

static pcore_fc2 g_fc[24];
static int       g_fc_n = 0;

static int pcore_font_px(HDC dc, const plot_font_style_t *fstyle)
{
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    int pt;
    int px;

    if (dpi <= 0) {
        dpi = 96;
    }
    pt = plot_style_fixed_to_int(fstyle->size);
    if (pt < 1) {
        pt = 1;
    }
    px = MulDiv(pt, dpi, 72);
    return (px < 1) ? 1 : px;
}

static HFONT pcore_font_for(HDC dc, const plot_font_style_t *fstyle)
{
    int px = pcore_font_px(dc, fstyle);
    int italic = (fstyle->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) ? 1 : 0;
    int weight = fstyle->weight;
    int family = (int) fstyle->family;
    int i;
    HFONT f;

    for (i = 0; i < g_fc_n; i++) {
        if (g_fc[i].px == px && g_fc[i].weight == weight &&
                g_fc[i].italic == italic && g_fc[i].family == family) {
            return g_fc[i].font;
        }
    }
    f = pcore_plot_font(dc, fstyle);   /* reuse the M1 LOGFONT builder */
    if (f != NULL && g_fc_n < 24) {
        g_fc[g_fc_n].px = px;
        g_fc[g_fc_n].weight = weight;
        g_fc[g_fc_n].italic = italic;
        g_fc[g_fc_n].family = family;
        g_fc[g_fc_n].font = f;
        g_fc_n++;
    }
    return f;
}

/* Decode UTF-8 [0,len) into wbuf (UTF-16) and byteofs[] (the UTF-8 byte offset
 * at which each UTF-16 unit's code point begins; byteofs[nunits] == len).
 * wbuf and byteofs must hold at least `cap` and `cap+1` entries. Returns the
 * number of UTF-16 units, or -1 on overflow. */
static int pcore_utf8_map(const char *s, int len, WCHAR *wbuf,
        int *byteofs, int cap)
{
    int i = 0;
    int n = 0;

    while (i < len) {
        unsigned char c = (unsigned char) s[i];
        unsigned int cp;
        int nb;
        int k;

        if (c < 0x80) {
            cp = c; nb = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F; nb = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F; nb = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07; nb = 4;
        } else {
            cp = c; nb = 1;   /* invalid lead byte: pass through as one unit */
        }
        for (k = 1; k < nb; k++) {
            unsigned char cc;
            if (i + k >= len) {
                nb = k;       /* truncated sequence */
                break;
            }
            cc = (unsigned char) s[i + k];
            if ((cc & 0xC0) != 0x80) {
                nb = k;       /* not a continuation byte */
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }

        if (cp <= 0xFFFF) {
            if (n >= cap) {
                return -1;
            }
            byteofs[n] = i;
            wbuf[n++] = (WCHAR) cp;
        } else {
            if (n + 1 >= cap) {
                return -1;
            }
            cp -= 0x10000;
            byteofs[n] = i;
            wbuf[n++] = (WCHAR) (0xD800 + (cp >> 10));
            byteofs[n] = i;
            wbuf[n++] = (WCHAR) (0xDC00 + (cp & 0x3FF));
        }
        i += nb;
    }
    byteofs[n] = len;
    return n;
}

static nserror gdi_font_width(const struct plot_font_style *fstyle,
        const char *string, size_t length, int *width)
{
    HDC dc = pcore_measure_dc();
    WCHAR *wbuf;
    int wl;
    SIZE sz;

    *width = 0;
    if (length == 0 || dc == NULL) {
        return NSERROR_OK;
    }
    wbuf = (WCHAR *) malloc(sizeof(WCHAR) * length);
    if (wbuf == NULL) {
        return NSERROR_NOMEM;
    }
    wl = MultiByteToWideChar(CP_UTF8, 0, string, (int) length,
            wbuf, (int) length);
    if (wl > 0) {
        HFONT f = pcore_font_for(dc, fstyle);
        HFONT old = (HFONT) SelectObject(dc, f);
        if (GetTextExtentPoint32W(dc, wbuf, wl, &sz)) {
            *width = sz.cx;
        }
        SelectObject(dc, old);
    }
    free(wbuf);
    return NSERROR_OK;
}

/* Shared helper: measure cumulative per-unit widths and the byte map. Returns
 * unit count (>=0), filling *dx_out / *byteofs_out (caller frees) or -1. */
static int pcore_measure(const struct plot_font_style *fstyle,
        const char *string, int length, WCHAR **wbuf_out,
        int **dx_out, int **byteofs_out)
{
    HDC dc = pcore_measure_dc();
    WCHAR *wbuf;
    int *dx;
    int *byteofs;
    int wl;
    int dummy = 0;
    SIZE sz;
    HFONT f;
    HFONT old;

    *wbuf_out = NULL; *dx_out = NULL; *byteofs_out = NULL;
    if (dc == NULL) {
        return -1;
    }
    wbuf = (WCHAR *) malloc(sizeof(WCHAR) * (length > 0 ? length : 1));
    dx = (int *) malloc(sizeof(int) * (length > 0 ? length : 1));
    byteofs = (int *) malloc(sizeof(int) * (length + 1));
    if (wbuf == NULL || dx == NULL || byteofs == NULL) {
        free(wbuf); free(dx); free(byteofs);
        return -1;
    }
    wl = pcore_utf8_map(string, length, wbuf, byteofs, length);
    if (wl <= 0) {
        free(wbuf); free(dx); free(byteofs);
        return -1;
    }
    f = pcore_font_for(dc, fstyle);
    old = (HFONT) SelectObject(dc, f);
    /* Big nMaxExtent so alpDx is filled for ALL units; we derive the fit
     * count ourselves from the cumulative widths. */
    GetTextExtentExPointW(dc, wbuf, wl, 0x7FFFFFFF, &dummy, dx, &sz);
    SelectObject(dc, old);

    *wbuf_out = wbuf;
    *dx_out = dx;
    *byteofs_out = byteofs;
    return wl;
}

static nserror gdi_font_position(const struct plot_font_style *fstyle,
        const char *string, size_t length, int x,
        size_t *char_offset, int *actual_x)
{
    WCHAR *wbuf;
    int *dx;
    int *byteofs;
    int wl;
    int fit;

    *char_offset = 0;
    *actual_x = 0;
    if (length == 0) {
        return NSERROR_OK;
    }
    wl = pcore_measure(fstyle, string, (int) length, &wbuf, &dx, &byteofs);
    if (wl < 0) {
        return NSERROR_OK;
    }
    fit = 0;
    while (fit < wl && dx[fit] <= x) {
        fit++;
    }
    if (fit >= wl) {
        *char_offset = length;
        *actual_x = dx[wl - 1];
    } else {
        *char_offset = (size_t) byteofs[fit];
        *actual_x = (fit > 0) ? dx[fit - 1] : 0;
    }
    free(wbuf); free(dx); free(byteofs);
    return NSERROR_OK;
}

static nserror gdi_font_split(const struct plot_font_style *fstyle,
        const char *string, size_t length, int x,
        size_t *char_offset, int *actual_x)
{
    WCHAR *wbuf;
    int *dx;
    int *byteofs;
    int wl;
    int fit;
    int i;
    int sp;

    *char_offset = length;
    *actual_x = 0;
    if (length == 0) {
        return NSERROR_OK;
    }
    wl = pcore_measure(fstyle, string, (int) length, &wbuf, &dx, &byteofs);
    if (wl < 0) {
        return NSERROR_OK;
    }

    fit = 0;
    while (fit < wl && dx[fit] <= x) {
        fit++;
    }
    if (fit >= wl) {
        /* Whole string fits: no split. */
        *char_offset = length;
        *actual_x = dx[wl - 1];
    } else {
        /* Prefer the last space at/before the fit boundary (actual_x <= x). */
        sp = -1;
        for (i = fit; i >= 0; i--) {
            if (i < wl && wbuf[i] == L' ') {
                sp = i;
                break;
            }
        }
        if (sp < 0) {
            /* No break fits: first space after x (actual_x > x). */
            for (i = fit; i < wl; i++) {
                if (wbuf[i] == L' ') {
                    sp = i;
                    break;
                }
            }
        }
        if (sp >= 0) {
            *char_offset = (size_t) byteofs[sp + 1];  /* first char after sp */
            *actual_x = dx[sp];
        } else {
            *char_offset = length;                    /* unsplittable */
            *actual_x = dx[wl - 1];
        }
    }
    if (*char_offset == 0) {
        *char_offset = length;   /* contract: never return 0 */
    }
    free(wbuf); free(dx); free(byteofs);
    return NSERROR_OK;
}

/* The GDI font-measurement table. Non-static: layout.c / redraw.c (M4/M5)
 * install it as content->font_func and guit->layout respectively. */
const struct gui_layout_table pcore_gdi_layout = {
    gdi_font_width,
    gdi_font_position,
    gdi_font_split
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

    /* A grey box with a 2px SOLID red border. */
    memset(&box, 0, sizeof(box));
    box.fill_type = PLOT_OP_TYPE_SOLID;
    box.fill_colour = 0x00d9d9d9;                  /* grey (channel-neutral) */
    box.stroke_type = PLOT_OP_TYPE_SOLID;
    box.stroke_width = plot_style_int_to_fixed(2);
    box.stroke_colour = 0x000000ff;                /* XBGR: red */
    r.x0 = 10; r.y0 = 10; r.x1 = 220; r.y1 = 70;
    rc.plot->rectangle(&rc, &box, &r);

    /* Black text inside the box (y is the BASELINE). */
    memset(&fs, 0, sizeof(fs));
    fs.family = PLOT_FONT_FAMILY_SANS_SERIF;
    fs.size = plot_style_int_to_fixed(12);         /* 12pt */
    fs.weight = 400;
    fs.flags = FONTF_NONE;
    fs.foreground = 0x00000000;                    /* black */
    fs.background = 0x00ffffff;
    rc.plot->text(&rc, &fs, 20, 46,
            "Positron GDI plotter OK", 23);

    /* A DOTTED blue box border (no fill) - drawn by hand, since WinCE pens
     * are solid-only. */
    memset(&box, 0, sizeof(box));
    box.fill_type = PLOT_OP_TYPE_NONE;
    box.stroke_type = PLOT_OP_TYPE_DOT;
    box.stroke_width = plot_style_int_to_fixed(2);
    box.stroke_colour = 0x00ff0000;                /* XBGR: blue */
    r.x0 = 10; r.y0 = 82; r.x1 = 220; r.y1 = 120;
    rc.plot->rectangle(&rc, &box, &r);

    /* A DASHED green line below. */
    memset(&ln, 0, sizeof(ln));
    ln.stroke_type = PLOT_OP_TYPE_DASH;
    ln.stroke_width = plot_style_int_to_fixed(2);
    ln.stroke_colour = 0x0000ff00;                 /* XBGR: green */
    r.x0 = 10; r.y0 = 134; r.x1 = 220; r.y1 = 134;
    rc.plot->line(&rc, &ln, &r);
}

/* M2 self-test: measure a known string and find a split point, formatting a
 * numeric summary so the font table can be sanity-checked without a window. */
PCORE_API void PCore_FontTest(char *out, int cap)
{
    static const char *S = "Hello world example";  /* 19 bytes */
    plot_font_style_t fs;
    int w = 0;
    size_t off = 0;
    int ax = 0;
    WCHAR wtmp[256];

    if (out == NULL || cap <= 0) {
        return;
    }
    memset(&fs, 0, sizeof(fs));
    fs.family = PLOT_FONT_FAMILY_SANS_SERIF;
    fs.size = plot_style_int_to_fixed(12);
    fs.weight = 400;
    fs.flags = FONTF_NONE;
    fs.foreground = 0x00000000;
    fs.background = 0x00ffffff;

    pcore_gdi_layout.width(&fs, S, 19, &w);
    pcore_gdi_layout.split(&fs, S, 19, w / 2, &off, &ax);

    /* WinCE coredll exports only wsprintfW (Unicode); format wide then narrow. */
    wsprintfW(wtmp,
            L"width('Hello world example')=%d px\r\n"
            L"split at half-width(%d): byte offset=%d, x=%d\r\n"
            L"(offset should land on a space boundary)",
            w, w / 2, (int) off, ax);
    WideCharToMultiByte(CP_ACP, 0, wtmp, -1, out, cap, NULL, NULL);
    out[cap - 1] = '\0';
}
