/*
 * nsshim/netsurf/inttypes.h - NetSurf's print-format helpers. NSLOG is stubbed
 * empty so these are largely unused; provide stdint + the common ones.
 * Intercepted ahead of the real netsurf/inttypes.h.
 */
#ifndef PCORE_SHIM_NETSURF_INTTYPES_H
#define PCORE_SHIM_NETSURF_INTTYPES_H

#include <stdint.h>

#ifndef PRIsizet
#define PRIsizet "lu"
#endif
#ifndef SSIZET_FMT
#define SSIZET_FMT "ld"
#endif

#endif
