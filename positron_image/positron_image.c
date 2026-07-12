/*
 * positron_image.c - public image DLL boundary.
 */

#include <windows.h>
#include <stddef.h>

#include <svgtiny.h>

#define POSITRON_IMAGE_EXPORTS
#include "positron_image.h"

BOOL WINAPI DllMain(HANDLE instance, DWORD reason, LPVOID reserved)
{
    (void) instance;
    (void) reason;
    (void) reserved;
    return TRUE;
}

PIMAGE_API int PImage_SvgInfoFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, int *out_w, int *out_h,
        unsigned int *out_shape_count)
{
    struct svgtiny_diagram *diagram;
    svgtiny_code code;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = 0;
    }
    if (data == NULL || len <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (viewport_w <= 0) {
        viewport_w = 300;
    }
    if (viewport_h <= 0) {
        viewport_h = 150;
    }

    diagram = svgtiny_create();
    if (diagram == NULL) {
        return PIMAGE_ERROR_MEMORY;
    }
    code = svgtiny_parse(diagram, data, (size_t) len,
            "positron:memory.svg", viewport_w, viewport_h);
    if (code != svgtiny_OK) {
        svgtiny_free(diagram);
        return PIMAGE_ERROR_SVG_BASE + (int) code;
    }

    if (out_w != NULL) {
        *out_w = diagram->width;
    }
    if (out_h != NULL) {
        *out_h = diagram->height;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = diagram->shape_count;
    }
    svgtiny_free(diagram);
    return PIMAGE_OK;
}
