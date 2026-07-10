/*
 * pcore_wmimage.cpp - Windows Mobile Imaging API bridge.
 *
 * The engine proper stays C89, but the WM Imaging SDK header exposes C++ COM
 * interfaces (IImagingFactory/IImage). Keep that surface isolated here and
 * export plain C PCore_* functions for the rest of Positron.
 */

#define _WIN32_DCOM

#include <windows.h>
#include <objbase.h>
#include <imaging.h>
#include <string.h>

#include "positron_core.h"

static const CLSID PCORE_CLSID_ImagingFactory =
    { 0x327abda8, 0x072b, 0x11d3,
      { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static const IID PCORE_IID_IImagingFactory =
    { 0x327abda7, 0x072b, 0x11d3,
      { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static HRESULT pcore_wmimage_com_init(BOOL *did_init)
{
    HRESULT hr;

    *did_init = FALSE;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == S_OK || hr == S_FALSE) {
        *did_init = TRUE;
        return S_OK;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        return S_OK;
    }
    return hr;
}

static void pcore_wmimage_com_done(BOOL did_init)
{
    if (did_init) {
        CoUninitialize();
    }
}

static HRESULT pcore_wmimage_create(const char *data, int len, IImage **image)
{
    HRESULT hr;
    IImagingFactory *factory;

    *image = NULL;
    if (data == NULL || len <= 0) {
        return E_INVALIDARG;
    }

    factory = NULL;
    hr = CoCreateInstance(PCORE_CLSID_ImagingFactory, NULL,
            CLSCTX_INPROC_SERVER, PCORE_IID_IImagingFactory,
            (void **) &factory);
    if (SUCCEEDED(hr) && factory != NULL) {
        hr = factory->CreateImageFromBuffer(data, (UINT) len,
                BufferDisposalFlagNone, image);
        factory->Release();
    }
    return hr;
}

extern "C" PCORE_API int PCore_ImageInfoFromMemory(const char *data, int len,
        int *out_w, int *out_h)
{
    HRESULT hr;
    BOOL did_init;
    IImage *image;
    ImageInfo info;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }

    image = NULL;
    hr = pcore_wmimage_com_init(&did_init);
    if (FAILED(hr)) {
        return 1;
    }
    hr = pcore_wmimage_create(data, len, &image);
    if (FAILED(hr) || image == NULL) {
        pcore_wmimage_com_done(did_init);
        return 1;
    }

    memset(&info, 0, sizeof(info));
    hr = image->GetImageInfo(&info);
    image->Release();
    if (FAILED(hr)) {
        pcore_wmimage_com_done(did_init);
        return 1;
    }

    if (out_w != NULL) {
        *out_w = (int) info.Width;
    }
    if (out_h != NULL) {
        *out_h = (int) info.Height;
    }
    pcore_wmimage_com_done(did_init);
    return 0;
}

extern "C" PCORE_API int PCore_DrawImageFromMemory(const char *data, int len,
        HDC hdc, int x, int y, int w, int h)
{
    HRESULT hr;
    BOOL did_init;
    IImage *image;
    ImageInfo info;
    RECT dst;

    if (hdc == NULL) {
        return 1;
    }

    image = NULL;
    hr = pcore_wmimage_com_init(&did_init);
    if (FAILED(hr)) {
        return 1;
    }
    hr = pcore_wmimage_create(data, len, &image);
    if (FAILED(hr) || image == NULL) {
        pcore_wmimage_com_done(did_init);
        return 1;
    }

    if (w <= 0 || h <= 0) {
        memset(&info, 0, sizeof(info));
        hr = image->GetImageInfo(&info);
        if (FAILED(hr)) {
            image->Release();
            pcore_wmimage_com_done(did_init);
            return 1;
        }
        if (w <= 0) {
            w = (int) info.Width;
        }
        if (h <= 0) {
            h = (int) info.Height;
        }
    }

    dst.left = x;
    dst.top = y;
    dst.right = x + w;
    dst.bottom = y + h;
    hr = image->Draw(hdc, &dst, NULL);
    image->Release();
    pcore_wmimage_com_done(did_init);
    return SUCCEEDED(hr) ? 0 : 1;
}
