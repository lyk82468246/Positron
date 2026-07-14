/*
 * pimage_wm.cpp - retained Windows Mobile Imaging API implementation.
 *
 * imaging.h exposes C++ COM interfaces. Keep them behind positron_image's
 * stable C ABI so C89 applications never depend on IImage directly.
 */

#include <windows.h>
#include <objbase.h>
#include <imaging.h>
#include <stdlib.h>
#include <string.h>

#include "positron_image.h"

typedef struct pimage_bitmap {
    IImage *image;
    void *encoded;
    int width;
    int height;
    DWORD owner_thread;
    BOOL did_com_init;
} pimage_bitmap;

static const CLSID PIMAGE_CLSID_ImagingFactory =
    { 0x327abda8, 0x072b, 0x11d3,
      { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static const IID PIMAGE_IID_IImagingFactory =
    { 0x327abda7, 0x072b, 0x11d3,
      { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static HRESULT g_bitmap_last_hr = S_OK;
static int g_bitmap_last_stage = PIMAGE_BITMAP_STAGE_NONE;

static void pimage_bitmap_set_error(int stage, HRESULT hr)
{
    g_bitmap_last_stage = stage;
    g_bitmap_last_hr = hr;
}

static HRESULT pimage_bitmap_com_init(BOOL *did_init)
{
    HRESULT hr;

    *did_init = FALSE;
    /* WM6 maps its normal COM initialization to the multithreaded model. */
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) {
        *did_init = TRUE;
        return S_OK;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        return S_OK;
    }
    return hr;
}

static int pimage_bitmap_check_thread(const pimage_bitmap *bitmap)
{
    if (bitmap == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (bitmap->owner_thread != GetCurrentThreadId()) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_THREAD,
                RPC_E_WRONG_THREAD);
        return PIMAGE_ERROR_THREAD;
    }
    return PIMAGE_OK;
}

extern "C" PIMAGE_API int PImage_CreateBitmapFromMemory(const char *data,
        int len, PIMAGE_BITMAP *out_bitmap)
{
    pimage_bitmap *bitmap;
    IImagingFactory *factory;
    ImageInfo info;
    HRESULT hr;

    pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_NONE, S_OK);
    if (out_bitmap == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }
    *out_bitmap = NULL;
    if (data == NULL || len <= 0) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }

    bitmap = (pimage_bitmap *) calloc(1, sizeof(*bitmap));
    if (bitmap == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_MEMORY, E_OUTOFMEMORY);
        return PIMAGE_ERROR_MEMORY;
    }
    bitmap->encoded = malloc((size_t) len);
    if (bitmap->encoded == NULL) {
        free(bitmap);
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_MEMORY, E_OUTOFMEMORY);
        return PIMAGE_ERROR_MEMORY;
    }
    memcpy(bitmap->encoded, data, (size_t) len);
    bitmap->owner_thread = GetCurrentThreadId();

    hr = pimage_bitmap_com_init(&bitmap->did_com_init);
    if (FAILED(hr)) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_COM_INIT, hr);
        free(bitmap->encoded);
        free(bitmap);
        return PIMAGE_ERROR_PLATFORM;
    }

    factory = NULL;
    hr = CoCreateInstance(PIMAGE_CLSID_ImagingFactory, NULL,
            CLSCTX_INPROC_SERVER, PIMAGE_IID_IImagingFactory,
            (void **) &factory);
    if (FAILED(hr) || factory == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_FACTORY, hr);
    } else {
        hr = factory->CreateImageFromBuffer(bitmap->encoded, (UINT) len,
                BufferDisposalFlagNone, &bitmap->image);
        factory->Release();
        if (FAILED(hr) || bitmap->image == NULL) {
            pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_CREATE, hr);
        }
    }
    if (FAILED(hr) || bitmap->image == NULL) {
        if (bitmap->did_com_init) {
            CoUninitialize();
        }
        free(bitmap->encoded);
        free(bitmap);
        return PIMAGE_ERROR_PLATFORM;
    }

    memset(&info, 0, sizeof(info));
    hr = bitmap->image->GetImageInfo(&info);
    if (FAILED(hr) || info.Width == 0 || info.Height == 0) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_INFO,
                FAILED(hr) ? hr : E_FAIL);
        bitmap->image->Release();
        if (bitmap->did_com_init) {
            CoUninitialize();
        }
        free(bitmap->encoded);
        free(bitmap);
        return PIMAGE_ERROR_PLATFORM;
    }
    bitmap->width = (int) info.Width;
    bitmap->height = (int) info.Height;
    *out_bitmap = (PIMAGE_BITMAP) bitmap;
    pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_NONE, S_OK);
    return PIMAGE_OK;
}

extern "C" PIMAGE_API int PImage_BitmapGetInfo(PIMAGE_BITMAP handle,
        int *out_w, int *out_h)
{
    pimage_bitmap *bitmap = (pimage_bitmap *) handle;
    int result;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    result = pimage_bitmap_check_thread(bitmap);
    if (result != PIMAGE_OK) {
        return result;
    }
    if (out_w != NULL) {
        *out_w = bitmap->width;
    }
    if (out_h != NULL) {
        *out_h = bitmap->height;
    }
    pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_NONE, S_OK);
    return PIMAGE_OK;
}

extern "C" PIMAGE_API int PImage_DrawBitmap(PIMAGE_BITMAP handle, HDC hdc,
        int x, int y, int width, int height)
{
    pimage_bitmap *bitmap = (pimage_bitmap *) handle;
    RECT dst;
    HRESULT hr;
    int result;

    result = pimage_bitmap_check_thread(bitmap);
    if (result != PIMAGE_OK) {
        return result;
    }
    if (hdc == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (width <= 0) {
        width = bitmap->width;
    }
    if (height <= 0) {
        height = bitmap->height;
    }
    if (width <= 0 || height <= 0) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }

    dst.left = x;
    dst.top = y;
    dst.right = x + width;
    dst.bottom = y + height;
    hr = bitmap->image->Draw(hdc, &dst, NULL);
    if (FAILED(hr)) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_DRAW, hr);
        return PIMAGE_ERROR_DRAW;
    }
    pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_NONE, S_OK);
    return PIMAGE_OK;
}

extern "C" PIMAGE_API void PImage_FreeBitmap(PIMAGE_BITMAP handle)
{
    pimage_bitmap *bitmap = (pimage_bitmap *) handle;

    if (bitmap == NULL) {
        return;
    }
    if (bitmap->owner_thread != GetCurrentThreadId()) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_THREAD,
                RPC_E_WRONG_THREAD);
        return;
    }
    if (bitmap->image != NULL) {
        bitmap->image->Release();
    }
    free(bitmap->encoded);
    if (bitmap->did_com_init) {
        CoUninitialize();
    }
    free(bitmap);
}

extern "C" PIMAGE_API void PImage_BitmapLastError(int *out_stage,
        unsigned long *out_hr)
{
    if (out_stage != NULL) {
        *out_stage = g_bitmap_last_stage;
    }
    if (out_hr != NULL) {
        *out_hr = (unsigned long) g_bitmap_last_hr;
    }
}
