#ifndef PIMAGE_JPEG_H
#define PIMAGE_JPEG_H

#include <windows.h>
#include <imaging.h>

HRESULT pimage_jpeg_encode_444(IImagingFactory *factory, IImage *image,
        int width, int height, int quality, unsigned char **out_data,
        int *out_len);

#endif
