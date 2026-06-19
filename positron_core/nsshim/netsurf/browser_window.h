/*
 * nsshim/netsurf/browser_window.h - browser_window is opaque to layout/redraw
 * (html_content.bw is NULL in our pipeline). Forward declaration only; add
 * declarations if any browser_window_* call surfaces at link time.
 * Intercepted ahead of the real netsurf/browser_window.h.
 */
#ifndef PCORE_SHIM_NETSURF_BROWSER_WINDOW_H
#define PCORE_SHIM_NETSURF_BROWSER_WINDOW_H

struct browser_window;

/* Used by html.h's frame/iframe structs (frameset scrolling). Values copied
 * from the real netsurf/browser_window.h; layout/redraw never act on them. */
typedef enum {
    BW_SCROLLING_AUTO,
    BW_SCROLLING_YES,
    BW_SCROLLING_NO
} browser_scrolling;

#endif
