/*
 * nsshim/netsurf/bitmap.h - the thin bitmap carrier used by Positron's image
 * bridge. NetSurf's HTML layout/redraw only needs an opaque bitmap pointer;
 * the platform plotter consumes these original encoded bytes through WM
 * Imaging. This deliberately does not replace NetSurf's layout/redraw code.
 */
#ifndef PCORE_SHIM_NETSURF_BITMAP_H
#define PCORE_SHIM_NETSURF_BITMAP_H

struct bitmap {
    const char *data;  /* borrowed document-cache bytes */
    int len;
    int width;         /* intrinsic pixels from WM Imaging */
    int height;
};

#endif
