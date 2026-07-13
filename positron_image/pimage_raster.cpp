/*
 * pimage_raster.cpp - adapt libsvgtiny output to NanoSVG's anti-aliased
 * software rasterizer, then composite the premultiplied pixels with WM GDI.
 */

#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <svgtiny.h>

/* The Windows Mobile VS2008 CRT exposes the double-precision forms only. */
static float pimage_cosf(float value) { return (float) cos((double) value); }
static float pimage_sinf(float value) { return (float) sin((double) value); }
static float pimage_atan2f(float y, float x)
{
    return (float) atan2((double) y, (double) x);
}
static float pimage_acosf(float value) { return (float) acos((double) value); }

#define cosf pimage_cosf
#define sinf pimage_sinf
#define atan2f pimage_atan2f
#define acosf pimage_acosf
#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvg/nanosvgrast.h"
#undef cosf
#undef sinf
#undef atan2f
#undef acosf

#include "pimage_raster.h"

#define PIMAGE_RASTER_MAX_PIXELS 1048576

#define PIMAGE_ITEM_PATHS 1
#define PIMAGE_ITEM_TEXT 2

typedef struct pimage_raster_item {
    int type;
    NSVGshape *shapes;
    const struct svgtiny_shape *text_shape;
    struct pimage_raster_item *next;
} pimage_raster_item;

typedef struct pimage_raster_image {
    float width;
    float height;
    NSVGrasterizer *rasterizer;
    pimage_raster_item *items;
} pimage_raster_image;

typedef struct pimage_path_builder {
    NSVGpath *path;
    int capacity;
    float current_x;
    float current_y;
} pimage_path_builder;

static unsigned int pimage_nsvg_color(svgtiny_colour color)
{
    return ((unsigned int) svgtiny_RED(color)) |
            ((unsigned int) svgtiny_GREEN(color) << 8) |
            ((unsigned int) svgtiny_BLUE(color) << 16) |
            0xff000000UL;
}

static int pimage_path_reserve(pimage_path_builder *builder, int needed)
{
    float *points;
    int capacity;

    if (needed <= builder->capacity) {
        return 1;
    }
    capacity = (builder->capacity == 0) ? 16 : builder->capacity;
    while (capacity < needed) {
        if (capacity > 262144) {
            return 0;
        }
        capacity *= 2;
    }
    points = (float *) realloc(builder->path->pts,
            (size_t) capacity * 2 * sizeof(float));
    if (points == NULL) {
        return 0;
    }
    builder->path->pts = points;
    builder->capacity = capacity;
    return 1;
}

static int pimage_path_add_point(pimage_path_builder *builder,
        float x, float y)
{
    int index;

    if (!pimage_path_reserve(builder, builder->path->npts + 1)) {
        return 0;
    }
    index = builder->path->npts * 2;
    builder->path->pts[index] = x;
    builder->path->pts[index + 1] = y;
    builder->path->npts++;
    builder->current_x = x;
    builder->current_y = y;
    return 1;
}

static int pimage_path_add_cubic(pimage_path_builder *builder,
        float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    return pimage_path_add_point(builder, c1x, c1y) &&
            pimage_path_add_point(builder, c2x, c2y) &&
            pimage_path_add_point(builder, x, y);
}

static int pimage_path_add_line(pimage_path_builder *builder,
        float x, float y)
{
    float dx;
    float dy;
    float c1x;
    float c1y;
    float c2x;
    float c2y;

    dx = x - builder->current_x;
    dy = y - builder->current_y;
    c1x = builder->current_x + dx / 3.0f;
    c1y = builder->current_y + dy / 3.0f;
    c2x = builder->current_x + dx * 2.0f / 3.0f;
    c2y = builder->current_y + dy * 2.0f / 3.0f;
    return pimage_path_add_cubic(builder, c1x, c1y, c2x, c2y, x, y);
}

static void pimage_free_paths(NSVGpath *path)
{
    NSVGpath *next;

    while (path != NULL) {
        next = path->next;
        free(path->pts);
        free(path);
        path = next;
    }
}

static void pimage_free_shapes(NSVGshape *shape)
{
    NSVGshape *next;

    while (shape != NULL) {
        next = shape->next;
        pimage_free_paths(shape->paths);
        if (shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT ||
                shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) {
            free(shape->fill.gradient);
        }
        if (shape->stroke.type == NSVG_PAINT_LINEAR_GRADIENT ||
                shape->stroke.type == NSVG_PAINT_RADIAL_GRADIENT) {
            free(shape->stroke.gradient);
        }
        free(shape);
        shape = next;
    }
}

static void pimage_free_items(pimage_raster_item *item)
{
    pimage_raster_item *next;

    while (item != NULL) {
        next = item->next;
        pimage_free_shapes(item->shapes);
        free(item);
        item = next;
    }
}

static int pimage_finish_path(pimage_path_builder *builder,
        NSVGpath **first, NSVGpath **last)
{
    NSVGpath *path;

    path = builder->path;
    if (path == NULL) {
        return 1;
    }
    builder->path = NULL;
    builder->capacity = 0;
    if (path->npts < 4 || ((path->npts - 1) % 3) != 0) {
        free(path->pts);
        free(path);
        return 1;
    }
    if (*last == NULL) {
        *first = path;
    } else {
        (*last)->next = path;
    }
    *last = path;
    return 1;
}

static int pimage_begin_path(pimage_path_builder *builder,
        NSVGpath **first, NSVGpath **last, float x, float y)
{
    if (!pimage_finish_path(builder, first, last)) {
        return 0;
    }
    builder->path = (NSVGpath *) calloc(1, sizeof(NSVGpath));
    if (builder->path == NULL) {
        return 0;
    }
    return pimage_path_add_point(builder, x, y);
}

static NSVGpath *pimage_convert_paths(const struct svgtiny_shape *source)
{
    pimage_path_builder builder;
    NSVGpath *first;
    NSVGpath *last;
    unsigned int i;
    int command;

    memset(&builder, 0, sizeof(builder));
    first = NULL;
    last = NULL;
    for (i = 0; i < source->path_length; ) {
        command = (int) source->path[i];
        if (command == svgtiny_PATH_MOVE) {
            if (i + 2 >= source->path_length ||
                    !pimage_begin_path(&builder, &first, &last,
                    source->path[i + 1], source->path[i + 2])) {
                goto error;
            }
            i += 3;
        } else if (command == svgtiny_PATH_LINE) {
            if (builder.path == NULL || i + 2 >= source->path_length ||
                    !pimage_path_add_line(&builder,
                    source->path[i + 1], source->path[i + 2])) {
                goto error;
            }
            i += 3;
        } else if (command == svgtiny_PATH_BEZIER) {
            if (builder.path == NULL || i + 6 >= source->path_length ||
                    !pimage_path_add_cubic(&builder,
                    source->path[i + 1], source->path[i + 2],
                    source->path[i + 3], source->path[i + 4],
                    source->path[i + 5], source->path[i + 6])) {
                goto error;
            }
            i += 7;
        } else if (command == svgtiny_PATH_CLOSE) {
            if (builder.path == NULL) {
                goto error;
            }
            builder.path->closed = 1;
            if (!pimage_finish_path(&builder, &first, &last)) {
                goto error;
            }
            i += 1;
        } else {
            goto error;
        }
    }
    if (!pimage_finish_path(&builder, &first, &last)) {
        goto error;
    }
    return first;

error:
    if (builder.path != NULL) {
        free(builder.path->pts);
        free(builder.path);
    }
    pimage_free_paths(first);
    return NULL;
}

static NSVGshape *pimage_convert_shape(const struct svgtiny_shape *source)
{
    NSVGshape *shape;
    NSVGgradient *gradient;
    size_t gradient_size;
    unsigned int i;

    if (source->path == NULL) {
        return NULL;
    }
    shape = (NSVGshape *) calloc(1, sizeof(NSVGshape));
    if (shape == NULL) {
        return NULL;
    }
    shape->paths = pimage_convert_paths(source);
    if (shape->paths == NULL) {
        free(shape);
        return NULL;
    }
    if (source->fill_gradient_type != svgtiny_GRADIENT_NONE &&
            source->fill_gradient_stop_count != 0) {
        gradient_size = sizeof(NSVGgradient) +
                (source->fill_gradient_stop_count - 1) *
                sizeof(NSVGgradientStop);
        gradient = (NSVGgradient *) calloc(1, gradient_size);
        if (gradient == NULL) {
            pimage_free_paths(shape->paths);
            free(shape);
            return NULL;
        }
        memcpy(gradient->xform, source->fill_gradient_xform,
                sizeof(gradient->xform));
        gradient->spread = NSVG_SPREAD_PAD;
        gradient->nstops = (int) source->fill_gradient_stop_count;
        for (i = 0; i < source->fill_gradient_stop_count; i++) {
            gradient->stops[i].offset =
                    source->fill_gradient_stop[i].offset;
            gradient->stops[i].color = pimage_nsvg_color(
                    source->fill_gradient_stop[i].color);
        }
        shape->fill.type =
                (source->fill_gradient_type == svgtiny_GRADIENT_RADIAL) ?
                NSVG_PAINT_RADIAL_GRADIENT : NSVG_PAINT_LINEAR_GRADIENT;
        shape->fill.gradient = gradient;
    } else {
        shape->fill.type = (source->fill == svgtiny_TRANSPARENT) ?
                NSVG_PAINT_NONE : NSVG_PAINT_COLOR;
        shape->fill.color = pimage_nsvg_color(source->fill);
    }
    shape->stroke.type = (source->stroke == svgtiny_TRANSPARENT) ?
            NSVG_PAINT_NONE : NSVG_PAINT_COLOR;
    shape->stroke.color = pimage_nsvg_color(source->stroke);
    shape->opacity = 1.0f;
    shape->strokeWidth = (float) source->stroke_width;
    shape->strokeLineJoin = NSVG_JOIN_MITER;
    shape->strokeLineCap = NSVG_CAP_BUTT;
    shape->miterLimit = 4.0f;
    shape->fillRule = (source->fill_rule == svgtiny_FILL_EVENODD) ?
            NSVG_FILLRULE_EVENODD : NSVG_FILLRULE_NONZERO;
    shape->flags = NSVG_FLAGS_VISIBLE;
    return shape;
}

extern "C" void *pimage_raster_create(const struct svgtiny_diagram *diagram)
{
    pimage_raster_image *result;
    pimage_raster_item *item;
    pimage_raster_item *last_item;
    NSVGshape *shape;
    NSVGshape *last_shape;
    unsigned int i;

    if (diagram == NULL) {
        return NULL;
    }
    result = (pimage_raster_image *) calloc(1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    result->width = (float) diagram->width;
    result->height = (float) diagram->height;
    last_item = NULL;
    last_shape = NULL;
    for (i = 0; i < diagram->shape_count; i++) {
        if (diagram->shape[i].path == NULL && diagram->shape[i].text == NULL) {
            continue;
        }
        if (diagram->shape[i].text != NULL) {
            item = (pimage_raster_item *) calloc(1, sizeof(*item));
            if (item == NULL) {
                pimage_free_items(result->items);
                free(result);
                return NULL;
            }
            item->type = PIMAGE_ITEM_TEXT;
            item->text_shape = &diagram->shape[i];
            if (last_item == NULL) {
                result->items = item;
            } else {
                last_item->next = item;
            }
            last_item = item;
            last_shape = NULL;
            continue;
        }
        if (last_item == NULL || last_item->type != PIMAGE_ITEM_PATHS) {
            item = (pimage_raster_item *) calloc(1, sizeof(*item));
            if (item == NULL) {
                pimage_free_items(result->items);
                free(result);
                return NULL;
            }
            item->type = PIMAGE_ITEM_PATHS;
            if (last_item == NULL) {
                result->items = item;
            } else {
                last_item->next = item;
            }
            last_item = item;
            last_shape = NULL;
        }
        shape = pimage_convert_shape(&diagram->shape[i]);
        if (shape == NULL) {
            pimage_free_items(result->items);
            free(result);
            return NULL;
        }
        if (last_shape == NULL) {
            last_item->shapes = shape;
        } else {
            last_shape->next = shape;
        }
        last_shape = shape;
    }
    result->rasterizer = nsvgCreateRasterizer();
    if (result->rasterizer == NULL) {
        pimage_free_items(result->items);
        free(result);
        return NULL;
    }
    return result;
}

extern "C" void pimage_raster_free(void *raster_image)
{
    pimage_raster_image *image = (pimage_raster_image *) raster_image;

    if (image != NULL) {
        nsvgDeleteRasterizer(image->rasterizer);
        pimage_free_items(image->items);
        free(image);
    }
}

static int pimage_make_raster_size(const pimage_raster_image *image,
        int width, int height, int *out_width, int *out_height,
        float *out_scale)
{
    double scale_x;
    double scale_y;
    double scale;
    double pixels;
    int raster_width;
    int raster_height;

    if (image->width <= 0.0f || image->height <= 0.0f ||
            width <= 0 || height <= 0) {
        return 0;
    }
    scale_x = (double) width / (double) image->width;
    scale_y = (double) height / (double) image->height;
    scale = (scale_x > scale_y) ? scale_x : scale_y;
    pixels = (double) image->width * (double) image->height *
            scale * scale;
    if (pixels > PIMAGE_RASTER_MAX_PIXELS) {
        scale = sqrt((double) PIMAGE_RASTER_MAX_PIXELS /
                ((double) image->width * (double) image->height));
    }
    raster_width = (int) ceil((double) image->width * scale);
    raster_height = (int) ceil((double) image->height * scale);
    if (raster_width < 1) { raster_width = 1; }
    if (raster_height < 1) { raster_height = 1; }
    if ((double) raster_width * (double) raster_height >
            PIMAGE_RASTER_MAX_PIXELS) {
        return 0;
    }
    *out_width = raster_width;
    *out_height = raster_height;
    *out_scale = (float) scale;
    return 1;
}

static int pimage_draw_path_batch(pimage_raster_image *image,
        const pimage_raster_item *item, HDC hdc, int x, int y,
        int width, int height, int raster_width, int raster_height,
        float scale)
{
    NSVGimage nsvg_image;
    unsigned char *rgba;
    unsigned char *dib_bits;
    unsigned char *source;
    unsigned char *target;
    BITMAPINFO bitmap_info;
    BLENDFUNCTION blend;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    HDC memory_dc;
    int stride;
    int row;
    int column;
    int alpha;
    int ok;

    memset(&nsvg_image, 0, sizeof(nsvg_image));
    nsvg_image.width = image->width;
    nsvg_image.height = image->height;
    nsvg_image.shapes = item->shapes;
    stride = raster_width * 4;
    rgba = (unsigned char *) malloc((size_t) stride * raster_height);
    if (rgba == NULL) {
        return 0;
    }
    nsvgRasterize(image->rasterizer, &nsvg_image, 0.0f, 0.0f, scale,
            rgba, raster_width, raster_height, stride);

    memset(&bitmap_info, 0, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = raster_width;
    bitmap_info.bmiHeader.biHeight = raster_height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    dib_bits = NULL;
    bitmap = CreateDIBSection(hdc, &bitmap_info, DIB_RGB_COLORS,
            (void **) &dib_bits, NULL, 0);
    if (bitmap == NULL || dib_bits == NULL) {
        free(rgba);
        if (bitmap != NULL) { DeleteObject(bitmap); }
        return 0;
    }
    for (row = 0; row < raster_height; row++) {
        source = rgba + row * stride;
        target = dib_bits + (raster_height - 1 - row) * stride;
        for (column = 0; column < raster_width; column++) {
            alpha = source[3];
            target[0] = (unsigned char) ((source[2] * alpha + 127) / 255);
            target[1] = (unsigned char) ((source[1] * alpha + 127) / 255);
            target[2] = (unsigned char) ((source[0] * alpha + 127) / 255);
            target[3] = (unsigned char) alpha;
            source += 4;
            target += 4;
        }
    }
    free(rgba);

    memory_dc = CreateCompatibleDC(hdc);
    if (memory_dc == NULL) {
        DeleteObject(bitmap);
        return 0;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    ok = AlphaBlend(hdc, x, y, width, height, memory_dc,
            0, 0, raster_width, raster_height, blend) ? 1 : 0;
    SelectObject(memory_dc, old_bitmap);
    DeleteDC(memory_dc);
    DeleteObject(bitmap);
    return ok;
}

static const WCHAR *pimage_font_face(svgtiny_font_family family)
{
    if (family == svgtiny_FONT_MONOSPACE) {
        return L"Courier New";
    }
    if (family == svgtiny_FONT_SERIF) {
        return L"Times New Roman";
    }
    return L"Tahoma";
}

static int pimage_draw_text(const pimage_raster_image *image,
        const struct svgtiny_shape *shape, HDC hdc,
        int x, int y, int width, int height)
{
    WCHAR *wide;
    LOGFONTW font_desc;
    HFONT font;
    HFONT old_font;
    SIZE extent;
    COLORREF old_color;
    int old_mode;
    UINT old_align;
    double scale_x;
    double scale_y;
    double radians;
    double anchor;
    int wide_len;
    int font_height;
    int escapement;
    int draw_x;
    int draw_y;
    int ok;

    if (shape == NULL || shape->text == NULL ||
            shape->fill == svgtiny_TRANSPARENT) {
        return 1;
    }
    wide_len = MultiByteToWideChar(CP_UTF8, 0, shape->text, -1,
            NULL, 0);
    if (wide_len <= 1) {
        return wide_len == 1;
    }
    wide = (WCHAR *) malloc((size_t) wide_len * sizeof(WCHAR));
    if (wide == NULL) {
        return 0;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, shape->text, -1,
            wide, wide_len) == 0) {
        free(wide);
        return 0;
    }
    scale_x = (double) width / (double) image->width;
    scale_y = (double) height / (double) image->height;
    font_height = (int) floor((double) shape->text_size * scale_y + 0.5);
    if (font_height < 1) {
        font_height = 1;
    }
    escapement = (int) floor((double) shape->text_rotation * 10.0 + 0.5);
    memset(&font_desc, 0, sizeof(font_desc));
    font_desc.lfHeight = -font_height;
    font_desc.lfEscapement = escapement;
    font_desc.lfOrientation = escapement;
    font_desc.lfWeight = shape->text_weight;
    font_desc.lfItalic = shape->text_italic ? (BYTE) 1 : (BYTE) 0;
    font_desc.lfCharSet = DEFAULT_CHARSET;
    font_desc.lfOutPrecision = OUT_DEFAULT_PRECIS;
    font_desc.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    font_desc.lfQuality = DEFAULT_QUALITY;
    font_desc.lfPitchAndFamily = (shape->text_family ==
            svgtiny_FONT_MONOSPACE) ? (BYTE) (FIXED_PITCH | FF_MODERN) :
            (BYTE) (DEFAULT_PITCH | FF_DONTCARE);
    lstrcpyW(font_desc.lfFaceName, pimage_font_face(shape->text_family));
    font = CreateFontIndirectW(&font_desc);
    if (font == NULL) {
        free(wide);
        return 0;
    }
    old_font = (HFONT) SelectObject(hdc, font);
    old_mode = SetBkMode(hdc, TRANSPARENT);
    old_color = SetTextColor(hdc, RGB(svgtiny_RED(shape->fill),
            svgtiny_GREEN(shape->fill), svgtiny_BLUE(shape->fill)));
    old_align = SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
    if (!GetTextExtentPoint32W(hdc, wide, wide_len - 1, &extent)) {
        extent.cx = 0;
        extent.cy = 0;
    }
    anchor = 0.0;
    if (shape->text_anchor == svgtiny_TEXT_ANCHOR_MIDDLE) {
        anchor = (double) extent.cx / 2.0;
    } else if (shape->text_anchor == svgtiny_TEXT_ANCHOR_END) {
        anchor = (double) extent.cx;
    }
    radians = (double) shape->text_rotation * 3.14159265358979323846 / 180.0;
    draw_x = x + (int) floor((double) shape->text_x * scale_x -
            anchor * cos(radians) + 0.5);
    draw_y = y + (int) floor((double) shape->text_y * scale_y -
            anchor * sin(radians) + 0.5);
    ok = ExtTextOutW(hdc, draw_x, draw_y, 0, NULL,
            wide, (UINT) (wide_len - 1), NULL) ? 1 : 0;
    SetTextAlign(hdc, old_align);
    SetTextColor(hdc, old_color);
    SetBkMode(hdc, old_mode);
    SelectObject(hdc, old_font);
    DeleteObject(font);
    free(wide);
    return ok;
}

extern "C" int pimage_raster_draw(void *raster_image, HDC hdc,
        int x, int y, int width, int height)
{
    pimage_raster_image *image = (pimage_raster_image *) raster_image;
    pimage_raster_item *item;
    float scale;
    int raster_width;
    int raster_height;

    if (image == NULL || hdc == NULL ||
            !pimage_make_raster_size(image, width, height,
            &raster_width, &raster_height, &scale)) {
        return 0;
    }
    for (item = image->items; item != NULL; item = item->next) {
        if (item->type == PIMAGE_ITEM_PATHS) {
            if (!pimage_draw_path_batch(image, item, hdc, x, y,
                    width, height, raster_width, raster_height, scale)) {
                return 0;
            }
        } else if (item->type == PIMAGE_ITEM_TEXT) {
            if (!pimage_draw_text(image, item->text_shape, hdc,
                    x, y, width, height)) {
                return 0;
            }
        }
    }
    return 1;
}
