/* Positron-facing copy of NetSurf 3.11's scrollbar interface. */
#ifndef PCORE_SHIM_DESKTOP_SCROLLBAR_H
#define PCORE_SHIM_DESKTOP_SCROLLBAR_H

#include <stdbool.h>
#include <limits.h>

#include "utils/errors.h"
#include "netsurf/mouse.h"

struct scrollbar;
struct rect;
struct redraw_context;

#define SCROLLBAR_WIDTH 16
#define SCROLL_TOP       INT_MIN
#define SCROLL_PAGE_UP   (INT_MIN + 1)
#define SCROLL_PAGE_DOWN (INT_MAX - 1)
#define SCROLL_BOTTOM    INT_MAX

typedef enum {
    SCROLLBAR_MSG_MOVED,
    SCROLLBAR_MSG_SCROLL_START,
    SCROLLBAR_MSG_SCROLL_FINISHED
} scrollbar_msg;

struct scrollbar_msg_data {
    struct scrollbar *scrollbar;
    scrollbar_msg msg;
    int scroll_offset;
    int x0, y0, x1, y1;
};

typedef enum {
    SCROLLBAR_MOUSE_NONE = 0,
    SCROLLBAR_MOUSE_USED = (1 << 0),
    SCROLLBAR_MOUSE_BOTH = (1 << 1),
    SCROLLBAR_MOUSE_UP = (1 << 2),
    SCROLLBAR_MOUSE_PUP = (1 << 3),
    SCROLLBAR_MOUSE_VRT = (1 << 4),
    SCROLLBAR_MOUSE_PDWN = (1 << 5),
    SCROLLBAR_MOUSE_DWN = (1 << 6),
    SCROLLBAR_MOUSE_LFT = (1 << 7),
    SCROLLBAR_MOUSE_PLFT = (1 << 8),
    SCROLLBAR_MOUSE_HRZ = (1 << 9),
    SCROLLBAR_MOUSE_PRGT = (1 << 10),
    SCROLLBAR_MOUSE_RGT = (1 << 11)
} scrollbar_mouse_status;

typedef void (*scrollbar_client_callback)(void *client_data,
        struct scrollbar_msg_data *scrollbar_data);

nserror scrollbar_create(bool horizontal, int length, int full_size,
        int visible_size, void *client_data,
        scrollbar_client_callback client_callback, struct scrollbar **s);
void scrollbar_destroy(struct scrollbar *s);
int scrollbar_get_offset(struct scrollbar *s);
nserror scrollbar_redraw(struct scrollbar *s, int x, int y,
        const struct rect *clip, float scale,
        const struct redraw_context *ctx);
void scrollbar_set(struct scrollbar *s, int value, bool bar_pos);
bool scrollbar_scroll(struct scrollbar *s, int change);
void scrollbar_set_extents(struct scrollbar *s, int length,
        int visible_size, int full_size);
bool scrollbar_is_horizontal(struct scrollbar *s);
scrollbar_mouse_status scrollbar_mouse_action(struct scrollbar *s,
        browser_mouse_state mouse, int x, int y);
const char *scrollbar_mouse_status_to_message(scrollbar_mouse_status status);
void scrollbar_mouse_drag_end(struct scrollbar *s,
        browser_mouse_state mouse, int x, int y);
void scrollbar_start_content_drag(struct scrollbar *s, int x, int y);
void scrollbar_make_pair(struct scrollbar *horizontal,
        struct scrollbar *vertical);
void *scrollbar_get_data(struct scrollbar *s);

#endif
