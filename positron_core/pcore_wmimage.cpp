/*
 * pcore_wmimage.cpp - compatibility forwarding for the legacy core ABI.
 *
 * Native image ownership now belongs to positron_image.dll. Keep these
 * PCore_* exports so existing consumers continue to load unchanged.
 */

#include <windows.h>

#include "positron_core.h"
#include "positron_image.h"

static int pcore_image_result(void)
{
    int stage = PIMAGE_BITMAP_STAGE_NONE;

    PImage_BitmapLastError(&stage, NULL);
    return stage != PIMAGE_BITMAP_STAGE_NONE ? stage : 4;
}

extern "C" PCORE_API int PCore_ImageInfoFromMemory(const char *data, int len,
        int *out_w, int *out_h)
{
    PIMAGE_BITMAP bitmap = NULL;
    int result;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    result = PImage_CreateBitmapFromMemory(data, len, &bitmap);
    if (result != PIMAGE_OK) {
        return pcore_image_result();
    }
    result = PImage_BitmapGetInfo(bitmap, out_w, out_h);
    PImage_FreeBitmap(bitmap);
    return result == PIMAGE_OK ? 0 : pcore_image_result();
}

extern "C" PCORE_API int PCore_DrawImageFromMemory(const char *data, int len,
        HDC hdc, int x, int y, int w, int h)
{
    PIMAGE_BITMAP bitmap = NULL;
    int result;

    result = PImage_CreateBitmapFromMemory(data, len, &bitmap);
    if (result != PIMAGE_OK) {
        return pcore_image_result();
    }
    result = PImage_DrawBitmap(bitmap, hdc, x, y, w, h);
    PImage_FreeBitmap(bitmap);
    return result == PIMAGE_OK ? 0 : pcore_image_result();
}

extern "C" PCORE_API void PCore_ImageLastError(int *out_stage,
        unsigned long *out_hr)
{
    PImage_BitmapLastError(out_stage, out_hr);
}
