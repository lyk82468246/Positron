/*
 * positron_image.c - public image DLL boundary.
 */

#include <windows.h>
#include <stddef.h>
#include <stdlib.h>

#include <svgtiny.h>

#ifndef POSITRON_IMAGE_EXPORTS
#define POSITRON_IMAGE_EXPORTS
#endif
#include "positron_image.h"
#include "pimage_raster.h"

typedef struct pimage_svg {
    struct svgtiny_diagram *diagram;
    void *raster_image;
} pimage_svg;

BOOL WINAPI DllMain(HANDLE instance, DWORD reason, LPVOID reserved)
{
    (void) instance;
    (void) reason;
    (void) reserved;
    return TRUE;
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
    svg->raster_image = pimage_raster_create(svg->diagram);
    if (svg->raster_image == NULL) {
        svgtiny_free(svg->diagram);
        free(svg);
        return PIMAGE_ERROR_MEMORY;
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

    if (svg == NULL || svg->diagram == NULL ||
            svg->raster_image == NULL || hdc == NULL ||
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
    return pimage_raster_draw(svg->raster_image, hdc,
            x, y, width, height) ? PIMAGE_OK : PIMAGE_ERROR_DRAW;
}

PIMAGE_API void PImage_FreeSvg(PIMAGE_SVG handle)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (svg != NULL) {
        pimage_raster_free(svg->raster_image);
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
