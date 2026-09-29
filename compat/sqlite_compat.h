/* VS2008's C89 runtime does not expose the C99 INFINITY macro. */
#include <math.h>
#ifndef INFINITY
#  define INFINITY HUGE_VAL
#endif
