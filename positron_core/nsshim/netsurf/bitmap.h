/*
 * nsshim/netsurf/bitmap.h - the thin bitmap carrier used by Positron's image
 * bridge. NetSurf's HTML layout/redraw only needs an opaque bitmap pointer;
 * the platform plotter dispatches the retained object to WM Imaging or the
 * public Positron SVG service. This does not replace NetSurf layout/redraw.
 */
#ifndef PCORE_SHIM_NETSURF_BITMAP_H
#define PCORE_SHIM_NETSURF_BITMAP_H

#define PCORE_BITMAP_WM_IMAGE 1
#define PCORE_BITMAP_SVG      2

struct bitmap {
    int kind;
    const char *data;  /* borrowed document-cache bytes */
    int len;
    int width;         /* intrinsic pixels from the selected decoder */
    int height;
    void *svg;         /* PIMAGE_SVG, owned and released by this carrier */
};

#endif
