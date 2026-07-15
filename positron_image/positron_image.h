/*
 * positron_image.h - reusable image services for Windows Mobile programs.
 *
 * The public API is a C ABI and does not expose WM Imaging, libdom or
 * libsvgtiny objects. All input buffers remain owned by the caller.
 * Bitmap and SVG objects must be released by the matching PImage_Free*
 * function. Retained objects are used on the thread that created them.
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

/* ABI version uses major in the high 16 bits and minor in the low 16 bits.
 * A major change may break callers; minor versions only add compatible API. */
#define PIMAGE_ABI_VERSION_ENCODE(major, minor) \
    ((((unsigned long) (major)) << 16) | ((unsigned long) (minor) & 0xffffUL))
#define PIMAGE_ABI_VERSION_GET_MAJOR(version) \
    ((unsigned int) (((unsigned long) (version) >> 16) & 0xffffUL))
#define PIMAGE_ABI_VERSION_GET_MINOR(version) \
    ((unsigned int) ((unsigned long) (version) & 0xffffUL))
#define PIMAGE_ABI_VERSION_MAJOR 1
#define PIMAGE_ABI_VERSION_MINOR 2
#define PIMAGE_ABI_VERSION \
    PIMAGE_ABI_VERSION_ENCODE(PIMAGE_ABI_VERSION_MAJOR, \
            PIMAGE_ABI_VERSION_MINOR)

PIMAGE_API unsigned long PImage_GetAbiVersion(void);

enum {
    PIMAGE_OK = 0,
    PIMAGE_ERROR_ARGUMENT = 1,
    PIMAGE_ERROR_MEMORY = 2,
    PIMAGE_ERROR_DRAW = 3,
    PIMAGE_ERROR_PLATFORM = 4,
    PIMAGE_ERROR_THREAD = 5,
    PIMAGE_ERROR_UNSUPPORTED = 6,
    PIMAGE_ERROR_SVG_BASE = 100
};

enum {
    PIMAGE_BITMAP_STAGE_NONE = 0,
    PIMAGE_BITMAP_STAGE_ARGUMENT = 1,
    PIMAGE_BITMAP_STAGE_COM_INIT = 2,
    PIMAGE_BITMAP_STAGE_FACTORY = 3,
    PIMAGE_BITMAP_STAGE_CREATE = 4,
    PIMAGE_BITMAP_STAGE_INFO = 5,
    PIMAGE_BITMAP_STAGE_DRAW = 6,
    PIMAGE_BITMAP_STAGE_MEMORY = 7,
    PIMAGE_BITMAP_STAGE_THREAD = 8,
    PIMAGE_BITMAP_STAGE_ENCODER_LIST = 9,
    PIMAGE_BITMAP_STAGE_STREAM = 10,
    PIMAGE_BITMAP_STAGE_ENCODER = 11,
    PIMAGE_BITMAP_STAGE_SINK = 12,
    PIMAGE_BITMAP_STAGE_OUTPUT = 13
};

enum {
    PIMAGE_ENCODE_PNG = 1,
    PIMAGE_ENCODE_JPEG = 2
};

typedef HANDLE PIMAGE_BITMAP;
typedef HANDLE PIMAGE_SVG;

/* Create a retained Windows Mobile Imaging object from encoded BMP, PNG,
 * JPEG, GIF or another codec installed on the device. The DLL copies the
 * input bytes, so the caller may release its buffer after this call. The
 * returned handle must be queried, drawn and freed on the creating thread. */
PIMAGE_API int PImage_CreateBitmapFromMemory(const char *data, int len,
        PIMAGE_BITMAP *out_bitmap);

PIMAGE_API int PImage_BitmapGetInfo(PIMAGE_BITMAP bitmap,
        int *out_w, int *out_h);

/* Non-positive width/height use the intrinsic dimensions. Repeated draws use
 * the retained image object and do not recreate the decoder. */
PIMAGE_API int PImage_DrawBitmap(PIMAGE_BITMAP bitmap, HDC hdc,
        int x, int y, int width, int height);

PIMAGE_API void PImage_FreeBitmap(PIMAGE_BITMAP bitmap);

/* Encode a retained bitmap. The DLL allocates the result; release it with
 * PImage_FreeBuffer. PNG uses the installed WM Imaging codec. The base JPEG
 * call preserves the device codec's default behavior. */
PIMAGE_API int PImage_EncodeBitmap(PIMAGE_BITMAP bitmap, int format,
        unsigned char **out_data, int *out_len);

/* JPEG quality is 0..100, or -1 for the device default. Explicit JPEG
 * quality uses the bundled libjpeg-turbo encoder with 4:4:4 sampling.
 * PNG accepts -1. */
PIMAGE_API int PImage_EncodeBitmapEx(PIMAGE_BITMAP bitmap, int format,
        int quality, unsigned char **out_data, int *out_len);

PIMAGE_API void PImage_FreeBuffer(void *buffer);

/* Process-global diagnostic for the immediately preceding bitmap call.
 * stage is one of PIMAGE_BITMAP_STAGE_*; hr is the native HRESULT. */
PIMAGE_API void PImage_BitmapLastError(int *out_stage,
        unsigned long *out_hr);

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
