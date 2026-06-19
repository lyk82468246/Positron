/*
 * nsshim/utils/log.h - stub for NetSurf's logging, intercepted ahead of the
 * real utils/log.h on the positron_core include path.
 *
 * WinCE has no console; the ported layout.c / redraw.c call NSLOG ~37 times for
 * diagnostics we don't want. Compile them all to nothing.
 */
#ifndef PCORE_SHIM_UTILS_LOG_H
#define PCORE_SHIM_UTILS_LOG_H

/* The real log.h chain is what normally drags in nserror for the NetSurf .c
 * files; since we stub log.h (included very early by layout.c/redraw.c/etc.),
 * pull the real errors.h here so nserror is defined before layout.h uses it. */
#include "utils/errors.h"

#define NSLOG(catname, level, ...)   ((void) 0)

#endif
