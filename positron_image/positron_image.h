/*
 * positron_image.h - reusable image services for Windows Mobile programs.
 *
 * The public API is a C ABI and does not expose WM Imaging, libdom or
 * libsvgtiny objects. All input buffers remain owned by the caller.
 */

#ifndef POSITRON_IMAGE_H
#define POSITRON_IMAGE_H

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
    PIMAGE_ERROR_SVG_BASE = 100
};

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
