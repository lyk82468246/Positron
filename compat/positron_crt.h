/*
 * compat/positron_crt.h - prototypes for C runtime functions that the WinCE
 * coredll is missing, supplied by Positron in compat/positron_crt.c.
 *
 * This header is force-included (VCCLCompilerTool/ForcedIncludeFiles) into the
 * positron_netsurf project so every translation unit sees a *plain* prototype
 * for these functions.
 *
 * Why plain (not <search.h>): WinCE's <search.h> declares bsearch as
 *   _CRTIMP void * __cdecl bsearch(...)
 * i.e. __declspec(dllimport). But coredll.lib does NOT actually export bsearch
 * (only qsort survived). A dllimport declaration would make the compiler emit a
 * reference to __imp_bsearch, which nothing provides -> LNK2019. Our own impl
 * has ordinary linkage, so callers must see an ordinary prototype to match it.
 * Hence we declare it here ourselves and deliberately avoid <search.h>.
 */

#ifndef POSITRON_COMPAT_CRT_H
#define POSITRON_COMPAT_CRT_H

#include <stddef.h>   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Standard C bsearch. WinCE coredll omits it; provided in positron_crt.c. */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (__cdecl *compar)(const void *, const void *));

/* Standard C abort(). WinCE has no abort() at all (not declared, not exported
 * by coredll); NetSurf calls it on "unreachable" guards. Provided in
 * positron_crt.c. Declared plain (not noreturn) so callers that place code
 * after abort() - e.g. libhubbub detect.c's `abort(); return false;` - do not
 * trip an unreachable-code warning. */
void abort(void);

/* POSIX strdup / strncasecmp. WinCE coredll exports neither under these names;
 * provided in positron_crt.c. strncasecmp forwards to coredll's _strnicmp.
 * libcss needs both (css_stylesheet_create -> strdup; mq_parse_op ->
 * strncasecmp). */
char *strdup(const char *s);
int strncasecmp(const char *s1, const char *s2, size_t n);

/* C99 snprintf. WinCE coredll has only _vsnprintf; provided in positron_crt.c
 * (libdom needs the C99 name). time() is also provided there but declared via
 * <time.h>, not here, to avoid clashing with the platform prototype. */
int snprintf(char *buf, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_COMPAT_CRT_H */
