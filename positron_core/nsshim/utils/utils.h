/*
 * nsshim/utils/utils.h - the few generic helper macros the ported layout.c /
 * redraw.c use. The real utils/utils.h also declares string/path helpers we
 * don't need; add any that surface at link time. Intercepted ahead of it.
 */
#ifndef PCORE_SHIM_UTILS_UTILS_H
#define PCORE_SHIM_UTILS_UTILS_H

#include <stddef.h>

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define SLEN(s)           (sizeof((s)) - 1)
#define NOF_ELEMENTS(a)   (sizeof(a) / sizeof(*(a)))
#define ARRAY_SIZE(a)     (sizeof(a) / sizeof((a)[0]))

#ifndef UNUSED
#define UNUSED(x)         ((x) = (x))
#endif

#define fallthrough

#endif
