/*
 * nsshim/netsurf/mouse.h - browser_mouse_state enum, pulled in by content.h /
 * html.h. Layout/redraw don't act on specific mouse values; provide the type
 * with the full set of bits so any reference compiles. Intercepted ahead of
 * the real netsurf/mouse.h.
 */
#ifndef PCORE_SHIM_NETSURF_MOUSE_H
#define PCORE_SHIM_NETSURF_MOUSE_H

typedef enum browser_mouse_state {
    BROWSER_MOUSE_HOVER         = 0,
    BROWSER_MOUSE_PRESS_1       = (1 << 0),
    BROWSER_MOUSE_PRESS_2       = (1 << 1),
    BROWSER_MOUSE_CLICK_1       = (1 << 2),
    BROWSER_MOUSE_CLICK_2       = (1 << 3),
    BROWSER_MOUSE_DOUBLE_CLICK  = (1 << 4),
    BROWSER_MOUSE_TRIPLE_CLICK  = (1 << 5),
    BROWSER_MOUSE_DRAG_1        = (1 << 6),
    BROWSER_MOUSE_DRAG_2        = (1 << 7),
    BROWSER_MOUSE_DRAG_ON       = (1 << 8),
    BROWSER_MOUSE_HOLDING_1     = (1 << 9),
    BROWSER_MOUSE_HOLDING_2     = (1 << 10),
    BROWSER_MOUSE_MOD_1         = (1 << 11),
    BROWSER_MOUSE_MOD_2         = (1 << 12),
    BROWSER_MOUSE_MOD_3         = (1 << 13)
} browser_mouse_state;

#endif
