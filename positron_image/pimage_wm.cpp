/*
 * pimage_wm.cpp - retained Windows Mobile Imaging API implementation.
 *
 * imaging.h exposes C++ COM interfaces. Keep them behind positron_image's
 * stable C ABI so C89 applications never depend on IImage directly.
 */

#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <imaging.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "positron_image.h"

#define PIMAGE_ENCODER_VALUE_LONG 4

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

static const GUID PIMAGE_FORMAT_PNG =
    { 0xb96b3caf, 0x0728, 0x11d3,
      { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static const GUID PIMAGE_FORMAT_JPEG =
    { 0xb96b3cae, 0x0728, 0x11d3,
      { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static const GUID PIMAGE_ENCODER_QUALITY =
    { 0x1d5be4b5, 0xfa4a, 0x452d,
      { 0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb } };

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

static const GUID *pimage_encode_format_guid(int format)
{
    if (format == PIMAGE_ENCODE_PNG) {
        return &PIMAGE_FORMAT_PNG;
    }
    if (format == PIMAGE_ENCODE_JPEG) {
        return &PIMAGE_FORMAT_JPEG;
    }
    return NULL;
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

extern "C" PIMAGE_API int PImage_EncodeBitmapEx(PIMAGE_BITMAP handle,
        int format, int quality, unsigned char **out_data, int *out_len)
{
    pimage_bitmap *bitmap = (pimage_bitmap *) handle;
    const GUID *format_guid;
    IImagingFactory *factory;
    ImageCodecInfo *encoders;
    IStream *stream;
    IImageEncoder *encoder;
    IImageSink *sink;
    HGLOBAL global;
    void *source;
    unsigned char *copy;
    UINT encoder_count;
    UINT i;
    LARGE_INTEGER move;
    ULARGE_INTEGER end;
    HRESULT hr;
    int result;

    if (out_data == NULL || out_len == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }
    *out_data = NULL;
    *out_len = 0;
    result = pimage_bitmap_check_thread(bitmap);
    if (result != PIMAGE_OK) {
        return result;
    }
    format_guid = pimage_encode_format_guid(format);
    if (format_guid == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (quality < -1 || quality > 100 ||
            (format != PIMAGE_ENCODE_JPEG && quality != -1)) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ARGUMENT, E_INVALIDARG);
        return PIMAGE_ERROR_ARGUMENT;
    }

    factory = NULL;
    encoders = NULL;
    stream = NULL;
    encoder = NULL;
    sink = NULL;
    source = NULL;
    copy = NULL;
    global = NULL;
    encoder_count = 0;
    hr = CoCreateInstance(PIMAGE_CLSID_ImagingFactory, NULL,
            CLSCTX_INPROC_SERVER, PIMAGE_IID_IImagingFactory,
            (void **) &factory);
    if (FAILED(hr) || factory == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_FACTORY, hr);
        return PIMAGE_ERROR_PLATFORM;
    }
    hr = factory->GetInstalledEncoders(&encoder_count, &encoders);
    if (FAILED(hr) || encoders == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ENCODER_LIST, hr);
        factory->Release();
        return PIMAGE_ERROR_PLATFORM;
    }
    for (i = 0; i < encoder_count; i++) {
        if (IsEqualGUID(encoders[i].FormatID, *format_guid)) {
            break;
        }
    }
    if (i == encoder_count) {
        CoTaskMemFree(encoders);
        factory->Release();
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ENCODER_LIST,
                IMGERR_CODECNOTFOUND);
        return PIMAGE_ERROR_UNSUPPORTED;
    }

    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (FAILED(hr) || stream == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_STREAM, hr);
    } else {
        hr = factory->CreateImageEncoderToStream(&encoders[i].Clsid,
                stream, &encoder);
        if (FAILED(hr) || encoder == NULL) {
            pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ENCODER, hr);
        }
    }
    CoTaskMemFree(encoders);
    factory->Release();
    if (FAILED(hr) || stream == NULL || encoder == NULL) {
        if (stream != NULL) {
            stream->Release();
        }
        return PIMAGE_ERROR_PLATFORM;
    }

    if (format == PIMAGE_ENCODE_JPEG && quality >= 0) {
        EncoderParameters parameters;
        ULONG quality_value;

        quality_value = (ULONG) quality;
        memset(&parameters, 0, sizeof(parameters));
        parameters.Count = 1;
        parameters.Parameter[0].Guid = PIMAGE_ENCODER_QUALITY;
        parameters.Parameter[0].NumberOfValues = 1;
        parameters.Parameter[0].Type = PIMAGE_ENCODER_VALUE_LONG;
        parameters.Parameter[0].Value = &quality_value;
        hr = encoder->SetEncoderParameters(&parameters);
        if (FAILED(hr)) {
            pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ENCODER, hr);
            encoder->Release();
            stream->Release();
            return PIMAGE_ERROR_UNSUPPORTED;
        }
    }

    hr = encoder->GetEncodeSink(&sink);
    if (FAILED(hr) || sink == NULL) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_SINK, hr);
    } else {
        hr = bitmap->image->PushIntoSink(sink);
        sink->Release();
        sink = NULL;
        if (FAILED(hr)) {
            pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_SINK, hr);
        }
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->TerminateEncoder();
        if (FAILED(hr)) {
            pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_ENCODER, hr);
        }
    }
    encoder->Release();
    encoder = NULL;
    if (FAILED(hr)) {
        stream->Release();
        return PIMAGE_ERROR_PLATFORM;
    }

    move.QuadPart = 0;
    end.QuadPart = 0;
    hr = stream->Seek(move, STREAM_SEEK_END, &end);
    if (FAILED(hr) || end.QuadPart == 0 || end.QuadPart > INT_MAX) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_OUTPUT,
                FAILED(hr) ? hr : E_FAIL);
        stream->Release();
        return PIMAGE_ERROR_PLATFORM;
    }
    hr = GetHGlobalFromStream(stream, &global);
    if (SUCCEEDED(hr)) {
        source = GlobalLock(global);
        if (source == NULL) {
            hr = E_OUTOFMEMORY;
        }
    }
    if (FAILED(hr)) {
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_OUTPUT, hr);
        stream->Release();
        return PIMAGE_ERROR_PLATFORM;
    }
    copy = (unsigned char *) malloc((size_t) end.QuadPart);
    if (copy == NULL) {
        GlobalUnlock(global);
        stream->Release();
        pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_MEMORY, E_OUTOFMEMORY);
        return PIMAGE_ERROR_MEMORY;
    }
    memcpy(copy, source, (size_t) end.QuadPart);
    GlobalUnlock(global);
    stream->Release();
    *out_data = copy;
    *out_len = (int) end.QuadPart;
    pimage_bitmap_set_error(PIMAGE_BITMAP_STAGE_NONE, S_OK);
    return PIMAGE_OK;
}

extern "C" PIMAGE_API int PImage_EncodeBitmap(PIMAGE_BITMAP handle,
        int format, unsigned char **out_data, int *out_len)
{
    return PImage_EncodeBitmapEx(handle, format, -1, out_data, out_len);
}

extern "C" PIMAGE_API void PImage_FreeBuffer(void *buffer)
{
    free(buffer);
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
