/* libjpeg-turbo encoder adapter for retained Windows Mobile images. */

#include <windows.h>
#include <imaging.h>
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"
#include "jerror.h"

#include "pimage_jpeg.h"

typedef struct pimage_jpeg_error {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} pimage_jpeg_error;

typedef struct pimage_jpeg_destination {
    struct jpeg_destination_mgr base;
    unsigned char *data;
    size_t capacity;
    size_t length;
} pimage_jpeg_destination;

#define PIMAGE_JPEG_INITIAL_CAPACITY 4096

static void pimage_jpeg_error_exit(j_common_ptr common)
{
    pimage_jpeg_error *error;

    error = (pimage_jpeg_error *) common->err;
    longjmp(error->jump, 1);
}

static void pimage_jpeg_destination_init(j_compress_ptr compressor)
{
    pimage_jpeg_destination *destination;

    destination = (pimage_jpeg_destination *) compressor->dest;
    if (destination->data == NULL) {
        destination->data = (unsigned char *) malloc(
                PIMAGE_JPEG_INITIAL_CAPACITY);
        if (destination->data == NULL) {
            ERREXIT1(compressor, JERR_OUT_OF_MEMORY, 10);
        }
        destination->capacity = PIMAGE_JPEG_INITIAL_CAPACITY;
    }
    destination->length = 0;
    destination->base.next_output_byte = destination->data;
    destination->base.free_in_buffer = destination->capacity;
}

static boolean pimage_jpeg_destination_grow(j_compress_ptr compressor)
{
    pimage_jpeg_destination *destination;
    unsigned char *grown;
    size_t old_capacity;
    size_t new_capacity;

    destination = (pimage_jpeg_destination *) compressor->dest;
    old_capacity = destination->capacity;
    if (old_capacity > ((size_t) -1) / 2) {
        ERREXIT1(compressor, JERR_OUT_OF_MEMORY, 10);
    }
    new_capacity = old_capacity * 2;
    grown = (unsigned char *) realloc(destination->data, new_capacity);
    if (grown == NULL) {
        ERREXIT1(compressor, JERR_OUT_OF_MEMORY, 10);
    }
    destination->data = grown;
    destination->capacity = new_capacity;
    destination->base.next_output_byte = grown + old_capacity;
    destination->base.free_in_buffer = new_capacity - old_capacity;
    return TRUE;
}

static void pimage_jpeg_destination_finish(j_compress_ptr compressor)
{
    pimage_jpeg_destination *destination;

    destination = (pimage_jpeg_destination *) compressor->dest;
    destination->length = destination->capacity -
            destination->base.free_in_buffer;
}

HRESULT pimage_jpeg_encode_444(IImagingFactory *factory, IImage *image,
        int width, int height, int quality, unsigned char **out_data,
        int *out_len)
{
    struct jpeg_compress_struct compressor;
    pimage_jpeg_error error;
    pimage_jpeg_destination destination;
    IBitmapImage *bitmap;
    BitmapData bits;
    RECT rect;
    JSAMPROW row[1];
    unsigned char *encoded;
    HRESULT hr;
    int compressor_created;
    int bits_locked;
    int component;

    if (factory == NULL || image == NULL || width <= 0 || height <= 0 ||
            quality < 0 || quality > 100 || out_data == NULL ||
            out_len == NULL) {
        return E_INVALIDARG;
    }
    *out_data = NULL;
    *out_len = 0;
    bitmap = NULL;
    encoded = NULL;
    compressor_created = 0;
    bits_locked = 0;
    memset(&compressor, 0, sizeof(compressor));
    memset(&error, 0, sizeof(error));
    memset(&destination, 0, sizeof(destination));
    memset(&bits, 0, sizeof(bits));

    hr = factory->CreateBitmapFromImage(image, (UINT) width, (UINT) height,
            PIXFMT_24BPP_RGB, InterpolationHintDefault, &bitmap);
    if (FAILED(hr) || bitmap == NULL) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    hr = bitmap->LockBits(&rect, IMGLOCK_READ, PIXFMT_24BPP_RGB, &bits);
    if (FAILED(hr) || bits.Scan0 == NULL || bits.Stride == 0) {
        bitmap->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }
    bits_locked = 1;

    compressor.err = jpeg_std_error(&error.base);
    error.base.error_exit = pimage_jpeg_error_exit;
    if (setjmp(error.jump) != 0) {
        if (compressor_created) {
            jpeg_destroy_compress(&compressor);
        }
        free(destination.data);
        if (bits_locked) {
            bitmap->UnlockBits(&bits);
        }
        bitmap->Release();
        return E_FAIL;
    }

    jpeg_create_compress(&compressor);
    compressor_created = 1;
    destination.base.init_destination = pimage_jpeg_destination_init;
    destination.base.empty_output_buffer = pimage_jpeg_destination_grow;
    destination.base.term_destination = pimage_jpeg_destination_finish;
    compressor.dest = &destination.base;
    compressor.image_width = (JDIMENSION) width;
    compressor.image_height = (JDIMENSION) height;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_EXT_BGR;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, quality, TRUE);
    compressor.dct_method = JDCT_ISLOW;
    for (component = 0; component < compressor.num_components; component++) {
        compressor.comp_info[component].h_samp_factor = 1;
        compressor.comp_info[component].v_samp_factor = 1;
    }
    jpeg_start_compress(&compressor, TRUE);
    while (compressor.next_scanline < compressor.image_height) {
        row[0] = (JSAMPROW) ((unsigned char *) bits.Scan0 +
                (INT) compressor.next_scanline * bits.Stride);
        if (jpeg_write_scanlines(&compressor, row, 1) != 1) {
            pimage_jpeg_error_exit((j_common_ptr) &compressor);
        }
    }
    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    compressor_created = 0;
    bitmap->UnlockBits(&bits);
    bits_locked = 0;
    bitmap->Release();

    encoded = destination.data;
    if (encoded == NULL || destination.length == 0 ||
            destination.length > INT_MAX) {
        free(destination.data);
        return E_FAIL;
    }
    *out_data = encoded;
    *out_len = (int) destination.length;
    return S_OK;
}
