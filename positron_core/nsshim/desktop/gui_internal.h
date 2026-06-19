/*
 * nsshim/desktop/gui_internal.h - exposes the global `guit` operation table.
 * Intercepted ahead of the real desktop/gui_internal.h.
 */
#ifndef PCORE_SHIM_DESKTOP_GUI_INTERNAL_H
#define PCORE_SHIM_DESKTOP_GUI_INTERNAL_H

#include "desktop/gui_table.h"

extern struct netsurf_table *guit;

#endif
