/*
 * nsshim/utils/nsoption.h - stub for NetSurf's configuration options.
 *
 * layout.c reads a couple of boolean options (nsoption_bool). We have no
 * options system; default everything to off / zero. Intercepted ahead of the
 * real utils/nsoption.h.
 */
#ifndef PCORE_SHIM_UTILS_NSOPTION_H
#define PCORE_SHIM_UTILS_NSOPTION_H

#include <stdbool.h>

#define nsoption_bool(name)   (false)
#define nsoption_int(name)    (0)
#define nsoption_charp(name)  ((char *) 0)

#endif
