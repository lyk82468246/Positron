/*
 * nsshim/desktop/gui_table.h - minimal NetSurf operation table.
 *
 * The real netsurf_table aggregates ~15 platform vtables (window, clipboard,
 * fetch, ...). The ported redraw.c only ever dereferences guit->layout (to
 * measure text the same way layout does). So we expose just that field; guit is
 * defined in pcore_nsshim.c pointing layout at our GDI font table.
 * Intercepted ahead of the real desktop/gui_table.h.
 */
#ifndef PCORE_SHIM_DESKTOP_GUI_TABLE_H
#define PCORE_SHIM_DESKTOP_GUI_TABLE_H

struct gui_layout_table;

struct netsurf_table {
    const struct gui_layout_table *layout;
};

#endif
