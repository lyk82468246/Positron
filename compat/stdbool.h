/*
 * compat/stdbool.h - C99 stdbool shim for VS2008 (which lacks one).
 *
 * Some NetSurf headers include <stdbool.h>. VS2008's C compiler does
 * not ship one. We just provide the minimum the C99 spec requires.
 *
 * Under C++ this file is a no-op - bool/true/false are built-ins.
 */

#ifndef POSITRON_COMPAT_STDBOOL_H
#define POSITRON_COMPAT_STDBOOL_H

#ifndef __cplusplus
#  ifndef bool
typedef int bool;
#  endif
#  ifndef true
#    define true  1
#  endif
#  ifndef false
#    define false 0
#  endif
#endif

#define __bool_true_false_are_defined 1

#endif /* POSITRON_COMPAT_STDBOOL_H */
