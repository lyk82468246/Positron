/*
 * nsshim/desktop/scrollbar.h - box overflow scrollbars. Our slim box builder
 * never attaches scrollbars (box->scroll_x/y are NULL), so scrollbar_get_offset
 * always returns 0; redraw's scrollbar_redraw (added later) is a no-op.
 * SCROLLBAR_WIDTH is used in a few width calcs. Intercepted ahead of the real
 * desktop/scrollbar.h.
 */
#ifndef PCORE_SHIM_DESKTOP_SCROLLBAR_H
#define PCORE_SHIM_DESKTOP_SCROLLBAR_H

#include "utils/errors.h"

struct scrollbar;
struct rect;
struct redraw_context;

#define SCROLLBAR_WIDTH 16

int scrollbar_get_offset(struct scrollbar *s);
nserror scrollbar_redraw(struct scrollbar *s, int x, int y,
        const struct rect *clip, float scale,
        const struct redraw_context *ctx);

#endif
