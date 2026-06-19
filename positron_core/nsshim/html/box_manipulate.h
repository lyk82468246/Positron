/*
 * nsshim/html/box_manipulate.h - redraw.c includes this for box_handle_scrollbars
 * (implemented as a no-op in pcore_box_inspect.c, since we attach no scrollbars).
 * The real header also declares box_create/box_free/etc., which redraw doesn't
 * call. Intercepts the real html/box_manipulate.h.
 */
#ifndef PCORE_SHIM_HTML_BOX_MANIPULATE_H
#define PCORE_SHIM_HTML_BOX_MANIPULATE_H

#include <stdbool.h>

#include "utils/errors.h"

struct box;
struct content;

nserror box_handle_scrollbars(struct content *c, struct box *box,
        bool bottom, bool right);

#endif
