/*
 * positron_image.h - reusable image services for Windows Mobile programs.
 *
 * The public API is a C ABI and does not expose WM Imaging, libdom or
 * libsvgtiny objects. All input buffers remain owned by the caller.
 * SVG objects must be released with PImage_FreeSvg.
 */

#ifndef POSITRON_IMAGE_H
#define POSITRON_IMAGE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_IMAGE_EXPORTS
#  define PIMAGE_API __declspec(dllexport)
#else
#  define PIMAGE_API __declspec(dllimport)
#endif

enum {
    PIMAGE_OK = 0,
    PIMAGE_ERROR_ARGUMENT = 1,
    PIMAGE_ERROR_MEMORY = 2,
    PIMAGE_ERROR_DRAW = 3,
    PIMAGE_ERROR_SVG_BASE = 100
};

typedef HANDLE PIMAGE_SVG;

/* Parse and retain an in-memory UTF-8 SVG. The caller keeps ownership of the
 * input bytes; the returned opaque object owns all parsed data. */
PIMAGE_API int PImage_CreateSvgFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, PIMAGE_SVG *out_svg);

PIMAGE_API int PImage_SvgGetInfo(PIMAGE_SVG svg, int *out_w, int *out_h,
        unsigned int *out_shape_count);

/* Draw a parsed SVG into an HDC. Non-positive width/height use the intrinsic
 * dimensions. Paths use anti-aliased solid fills and scaled solid strokes.
 * Basic SVG text uses the native WM GDI font backend. */
PIMAGE_API int PImage_DrawSvg(PIMAGE_SVG svg, HDC hdc,
        int x, int y, int width, int height);

PIMAGE_API void PImage_FreeSvg(PIMAGE_SVG svg);

/* Parse an in-memory UTF-8 SVG through libsvgtiny and return its intrinsic
 * dimensions and shape count. viewport_w/viewport_h provide fallback sizing
 * for percentage or omitted dimensions; non-positive values use 300x150.
 * Returns PIMAGE_OK or PIMAGE_ERROR_SVG_BASE + svgtiny_code. */
PIMAGE_API int PImage_SvgInfoFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, int *out_w, int *out_h,
        unsigned int *out_shape_count);

#ifdef __cplusplus
}
#endif

#endif
