/*
 * compat/sys/types.h - minimal POSIX sys/types.h shim for VS2008 + WinCE.
 *
 * WM6 SDK has no <sys/types.h>. NetSurf and many other portable C codebases
 * include it defensively for ssize_t / off_t / etc. We pull in the C standard
 * types and define ssize_t (the most commonly needed POSIX-only type).
 */

#ifndef POSITRON_COMPAT_SYS_TYPES_H
#define POSITRON_COMPAT_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
typedef int ssize_t;
#define _SSIZE_T_DEFINED
#endif

#ifndef _OFF_T_DEFINED
typedef long off_t;
#define _OFF_T_DEFINED
#endif

#endif /* POSITRON_COMPAT_SYS_TYPES_H */
