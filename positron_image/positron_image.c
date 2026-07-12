/*
 * positron_image.c - public image DLL boundary.
 */

#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <svgtiny.h>

#ifndef POSITRON_IMAGE_EXPORTS
#define POSITRON_IMAGE_EXPORTS
#endif
#include "positron_image.h"

#define PIMAGE_CURVE_POINTS 30
#define PIMAGE_MAX_POINTS 65535
#define PIMAGE_MAX_SUBPATHS 4096

typedef struct pimage_svg {
    struct svgtiny_diagram *diagram;
} pimage_svg;

typedef struct pimage_path_points {
    POINT *points;
    int point_count;
    int point_capacity;
    int *counts;
    int subpath_count;
    int subpath_capacity;
    int subpath_start;
} pimage_path_points;

BOOL WINAPI DllMain(HANDLE instance, DWORD reason, LPVOID reserved)
{
    (void) instance;
    (void) reason;
    (void) reserved;
    return TRUE;
}

static int pimage_round(float value)
{
    return (int) ((value >= 0.0f) ? value + 0.5f : value - 0.5f);
}

static COLORREF pimage_color(svgtiny_colour color)
{
    return RGB(svgtiny_RED(color), svgtiny_GREEN(color),
            svgtiny_BLUE(color));
}

static int pimage_reserve_points(pimage_path_points *path, int needed)
{
    POINT *points;
    int capacity;

    if (needed > PIMAGE_MAX_POINTS) {
        return 0;
    }
    if (needed <= path->point_capacity) {
        return 1;
    }
    capacity = (path->point_capacity == 0) ? 128 : path->point_capacity;
    while (capacity < needed && capacity < PIMAGE_MAX_POINTS) {
        capacity *= 2;
        if (capacity > PIMAGE_MAX_POINTS) {
            capacity = PIMAGE_MAX_POINTS;
        }
    }
    points = (POINT *) realloc(path->points, capacity * sizeof(POINT));
    if (points == NULL) {
        return 0;
    }
    path->points = points;
    path->point_capacity = capacity;
    return 1;
}

static int pimage_add_point(pimage_path_points *path, float px, float py,
        float scale_x, float scale_y, int offset_x, int offset_y)
{
    POINT point;

    point.x = offset_x + pimage_round(px * scale_x);
    point.y = offset_y + pimage_round(py * scale_y);
    if (path->point_count > path->subpath_start &&
            path->points[path->point_count - 1].x == point.x &&
            path->points[path->point_count - 1].y == point.y) {
        return 1;
    }
    if (!pimage_reserve_points(path, path->point_count + 1)) {
        return 0;
    }
    path->points[path->point_count++] = point;
    return 1;
}

static int pimage_finish_subpath(pimage_path_points *path)
{
    int *counts;
    int count;
    int capacity;

    count = path->point_count - path->subpath_start;
    if (count <= 0) {
        return 1;
    }
    if (path->subpath_count >= PIMAGE_MAX_SUBPATHS) {
        return 0;
    }
    if (path->subpath_count == path->subpath_capacity) {
        capacity = (path->subpath_capacity == 0) ? 8 :
                path->subpath_capacity * 2;
        if (capacity > PIMAGE_MAX_SUBPATHS) {
            capacity = PIMAGE_MAX_SUBPATHS;
        }
        counts = (int *) realloc(path->counts, capacity * sizeof(int));
        if (counts == NULL) {
            return 0;
        }
        path->counts = counts;
        path->subpath_capacity = capacity;
    }
    path->counts[path->subpath_count++] = count;
    path->subpath_start = path->point_count;
    return 1;
}

/* Curve subdivision follows libnsfb src/plot/generic.c cubic_points(). */
static int pimage_add_cubic(pimage_path_points *path,
        float x0, float y0, const float *curve,
        float scale_x, float scale_y, int offset_x, int offset_y)
{
    int segment;
    double t;
    double mt;
    double a;
    double b;
    double c;
    double d;
    float px;
    float py;

    for (segment = 1; segment < PIMAGE_CURVE_POINTS; segment++) {
        t = (double) segment / (double) (PIMAGE_CURVE_POINTS - 1);
        mt = 1.0 - t;
        a = mt * mt * mt;
        b = 3.0 * t * mt * mt;
        c = 3.0 * t * t * mt;
        d = t * t * t;
        px = (float) (a * x0 + b * curve[0] + c * curve[2] +
                d * curve[4]);
        py = (float) (a * y0 + b * curve[1] + c * curve[3] +
                d * curve[5]);
        if (!pimage_add_point(path, px, py, scale_x, scale_y,
                offset_x, offset_y)) {
            return 0;
        }
    }
    return 1;
}

static int pimage_flatten_path(const struct svgtiny_shape *shape,
        float scale_x, float scale_y, int offset_x, int offset_y,
        pimage_path_points *out)
{
    unsigned int i;
    int command;
    float current_x;
    float current_y;
    float subpath_x;
    float subpath_y;
    POINT first;

    memset(out, 0, sizeof(*out));
    current_x = 0.0f;
    current_y = 0.0f;
    subpath_x = 0.0f;
    subpath_y = 0.0f;
    for (i = 0; i < shape->path_length; ) {
        command = (int) shape->path[i];
        if (command == svgtiny_PATH_MOVE) {
            if (i + 2 >= shape->path_length ||
                    !pimage_finish_subpath(out)) {
                return 0;
            }
            current_x = shape->path[i + 1];
            current_y = shape->path[i + 2];
            subpath_x = current_x;
            subpath_y = current_y;
            if (!pimage_add_point(out, current_x, current_y,
                    scale_x, scale_y, offset_x, offset_y)) {
                return 0;
            }
            i += 3;
        } else if (command == svgtiny_PATH_LINE) {
            if (i + 2 >= shape->path_length) {
                return 0;
            }
            current_x = shape->path[i + 1];
            current_y = shape->path[i + 2];
            if (!pimage_add_point(out, current_x, current_y,
                    scale_x, scale_y, offset_x, offset_y)) {
                return 0;
            }
            i += 3;
        } else if (command == svgtiny_PATH_BEZIER) {
            if (i + 6 >= shape->path_length ||
                    !pimage_add_cubic(out, current_x, current_y,
                    &shape->path[i + 1], scale_x, scale_y,
                    offset_x, offset_y)) {
                return 0;
            }
            current_x = shape->path[i + 5];
            current_y = shape->path[i + 6];
            i += 7;
        } else if (command == svgtiny_PATH_CLOSE) {
            if (out->point_count > out->subpath_start) {
                first = out->points[out->subpath_start];
                if (!pimage_reserve_points(out, out->point_count + 1)) {
                    return 0;
                }
                if (out->points[out->point_count - 1].x != first.x ||
                        out->points[out->point_count - 1].y != first.y) {
                    out->points[out->point_count++] = first;
                }
            }
            if (!pimage_finish_subpath(out)) {
                return 0;
            }
            current_x = subpath_x;
            current_y = subpath_y;
            i += 1;
        } else {
            return 0;
        }
    }
    return pimage_finish_subpath(out);
}

static int pimage_draw_shape(const struct svgtiny_shape *shape, HDC hdc,
        float scale_x, float scale_y, int x, int y)
{
    pimage_path_points path;
    HBRUSH brush;
    HBRUSH old_brush;
    HPEN pen;
    HPEN old_pen;
    int stroke_width;
    int i;
    int offset;
    int ok;

    if (!pimage_flatten_path(shape, scale_x, scale_y, x, y, &path)) {
        free(path.points);
        free(path.counts);
        return 0;
    }
    ok = 1;
    if (shape->fill != svgtiny_TRANSPARENT && path.subpath_count > 0) {
        brush = CreateSolidBrush(pimage_color(shape->fill));
        if (brush == NULL) {
            ok = 0;
        } else {
            old_brush = (HBRUSH) SelectObject(hdc, brush);
            old_pen = (HPEN) SelectObject(hdc, GetStockObject(NULL_PEN));
            offset = 0;
            for (i = 0; i < path.subpath_count; i++) {
                if (path.counts[i] >= 3 &&
                        !Polygon(hdc, path.points + offset,
                        path.counts[i])) {
                    ok = 0;
                }
                offset += path.counts[i];
            }
            SelectObject(hdc, old_pen);
            SelectObject(hdc, old_brush);
            DeleteObject(brush);
        }
    }
    if (shape->stroke != svgtiny_TRANSPARENT && path.subpath_count > 0) {
        stroke_width = pimage_round((float) shape->stroke_width *
                ((scale_x < scale_y) ? scale_x : scale_y));
        if (stroke_width < 1) {
            stroke_width = 1;
        }
        pen = CreatePen(PS_SOLID, stroke_width, pimage_color(shape->stroke));
        if (pen == NULL) {
            ok = 0;
        } else {
            old_pen = (HPEN) SelectObject(hdc, pen);
            offset = 0;
            for (i = 0; i < path.subpath_count; i++) {
                if (path.counts[i] >= 2 &&
                        !Polyline(hdc, path.points + offset,
                        path.counts[i])) {
                    ok = 0;
                }
                offset += path.counts[i];
            }
            SelectObject(hdc, old_pen);
            DeleteObject(pen);
        }
    }
    free(path.points);
    free(path.counts);
    return ok;
}

PIMAGE_API int PImage_CreateSvgFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, PIMAGE_SVG *out_svg)
{
    pimage_svg *svg;
    svgtiny_code code;

    if (out_svg == NULL) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    *out_svg = NULL;
    if (data == NULL || len <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (viewport_w <= 0) {
        viewport_w = 300;
    }
    if (viewport_h <= 0) {
        viewport_h = 150;
    }
    svg = (pimage_svg *) calloc(1, sizeof(*svg));
    if (svg == NULL) {
        return PIMAGE_ERROR_MEMORY;
    }
    svg->diagram = svgtiny_create();
    if (svg->diagram == NULL) {
        free(svg);
        return PIMAGE_ERROR_MEMORY;
    }
    code = svgtiny_parse(svg->diagram, data, (size_t) len,
            "positron:memory.svg", viewport_w, viewport_h);
    if (code != svgtiny_OK) {
        svgtiny_free(svg->diagram);
        free(svg);
        return PIMAGE_ERROR_SVG_BASE + (int) code;
    }
    *out_svg = (PIMAGE_SVG) svg;
    return PIMAGE_OK;
}

PIMAGE_API int PImage_SvgGetInfo(PIMAGE_SVG handle, int *out_w, int *out_h,
        unsigned int *out_shape_count)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = 0;
    }
    if (svg == NULL || svg->diagram == NULL) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (out_w != NULL) {
        *out_w = svg->diagram->width;
    }
    if (out_h != NULL) {
        *out_h = svg->diagram->height;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = svg->diagram->shape_count;
    }
    return PIMAGE_OK;
}

PIMAGE_API int PImage_DrawSvg(PIMAGE_SVG handle, HDC hdc,
        int x, int y, int width, int height)
{
    pimage_svg *svg = (pimage_svg *) handle;
    float scale_x;
    float scale_y;
    unsigned int i;

    if (svg == NULL || svg->diagram == NULL || hdc == NULL ||
            svg->diagram->width <= 0 || svg->diagram->height <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (width <= 0) {
        width = svg->diagram->width;
    }
    if (height <= 0) {
        height = svg->diagram->height;
    }
    if (width <= 0 || height <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    scale_x = (float) width / (float) svg->diagram->width;
    scale_y = (float) height / (float) svg->diagram->height;
    for (i = 0; i < svg->diagram->shape_count; i++) {
        if (svg->diagram->shape[i].path != NULL &&
                !pimage_draw_shape(&svg->diagram->shape[i], hdc,
                scale_x, scale_y, x, y)) {
            return PIMAGE_ERROR_DRAW;
        }
    }
    return PIMAGE_OK;
}

PIMAGE_API void PImage_FreeSvg(PIMAGE_SVG handle)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (svg != NULL) {
        svgtiny_free(svg->diagram);
        free(svg);
    }
}

PIMAGE_API int PImage_SvgInfoFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, int *out_w, int *out_h,
        unsigned int *out_shape_count)
{
    PIMAGE_SVG svg;
    int result;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = 0;
    }
    result = PImage_CreateSvgFromMemory(data, len, viewport_w, viewport_h,
            &svg);
    if (result != PIMAGE_OK) {
        return result;
    }
    result = PImage_SvgGetInfo(svg, out_w, out_h, out_shape_count);
    PImage_FreeSvg(svg);
    return result;
}
