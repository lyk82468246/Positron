/*
 * nsshim/desktop/print.h - printing. We never print; opaque forward
 * declaration of print_settings is enough for redraw.c to compile.
 * Intercepts the real desktop/print.h.
 */
#ifndef PCORE_SHIM_DESKTOP_PRINT_H
#define PCORE_SHIM_DESKTOP_PRINT_H

struct print_settings;

#endif
