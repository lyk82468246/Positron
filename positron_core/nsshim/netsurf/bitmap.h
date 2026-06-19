/*
 * nsshim/netsurf/bitmap.h - images. Our pipeline draws no objects/images
 * (box->object==NULL, redraw_context.background_images=false), so bitmap is an
 * opaque forward declaration only. Intercepts the real netsurf/bitmap.h.
 */
#ifndef PCORE_SHIM_NETSURF_BITMAP_H
#define PCORE_SHIM_NETSURF_BITMAP_H

struct bitmap;

#endif
