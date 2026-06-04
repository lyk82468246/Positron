/*
 * compat/strings.h - POSIX <strings.h> shim for VS2008 + WinCE.
 *
 * WinCE / WM6 has no <strings.h>, and its <string.h> does not even declare
 * the MSVC case-insensitive comparators _stricmp/_strnicmp - although
 * coredll.lib DOES export them. NetSurf's libhubbub (charset/detect.c,
 * treebuilder.c) includes <strings.h> for strcasecmp/strncasecmp, so we
 * declare the underlying CRT functions and alias the POSIX names to them.
 */

#ifndef POSITRON_COMPAT_STRINGS_H
#define POSITRON_COMPAT_STRINGS_H

#include <stddef.h>   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Exported by coredll but missing from WM6 <string.h>; declare them here. */
int __cdecl _stricmp(const char *s1, const char *s2);
int __cdecl _strnicmp(const char *s1, const char *s2, size_t n);

#ifdef __cplusplus
}
#endif

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#endif /* POSITRON_COMPAT_STRINGS_H */
