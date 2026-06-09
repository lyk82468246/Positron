/*
 * compat/positron_crt.c - implementations of C runtime functions that the
 * WinCE / WM6 coredll is missing.
 *
 * Currently: bsearch. <search.h> declares it (as dllimport) but coredll.lib
 * does not export it, so any real call would fail to link. NetSurf's
 * libparserutils (charset/aliases.c) needs it, as will libcss/libdom later.
 *
 * Ordinary linkage so it matches the plain prototype in positron_crt.h.
 */

#include <stddef.h>
#include <stdlib.h>    /* malloc (for strdup) */
#include <string.h>    /* strlen, memcpy (for strdup) */
#include <stdarg.h>    /* va_list (for snprintf) */
#include <stdio.h>     /* _vsnprintf (for snprintf) */
#include <time.h>      /* time_t (for time) */
#include <windows.h>   /* TerminateProcess, GetCurrentProcess, GetSystemTime */

/* coredll exports _strnicmp (case-insensitive compare) but the WM6 <string.h>
 * doesn't declare it; declare it plainly for our strncasecmp forwarder. */
int __cdecl _strnicmp(const char *, const char *, size_t);

/*
 * Standard binary search over a sorted array.
 *
 * key    - element to search for
 * base   - start of the sorted array
 * nmemb  - number of elements
 * size   - size of each element in bytes
 * compar - comparison function: <0 if key<elem, 0 if equal, >0 if key>elem
 *
 * Returns a pointer to a matching element, or NULL if none.
 */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (__cdecl *compar)(const void *, const void *))
{
	const char *b = (const char *) base;
	size_t lo = 0;
	size_t hi = nmemb;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const void *p = b + mid * size;
		int r = compar(key, p);

		if (r < 0) {
			hi = mid;
		} else if (r > 0) {
			lo = mid + 1;
		} else {
			return (void *) p;
		}
	}

	return NULL;
}

/*
 * Standard C abort(). WinCE has no abort() in coredll at all, but NetSurf
 * (e.g. libhubbub charset/detect.c) calls it on "should never be reached"
 * defensive guards. If one ever fires we want immediate, abnormal
 * termination - no atexit handlers, no buffer flushing - which is exactly
 * what abort() promises. TerminateProcess on our own process is the closest
 * WinCE primitive; exit code 3 mirrors the MSVC CRT's abort() convention.
 */
void abort(void)
{
	TerminateProcess(GetCurrentProcess(), 3);
}

/*
 * POSIX strdup. WinCE coredll does not export it (libcss css_stylesheet_create
 * needs it). Allocate strlen+1 and copy, standard contract: caller free()s.
 */
char *strdup(const char *s)
{
	size_t n;
	char *p;

	if (s == NULL) {
		return NULL;
	}
	n = strlen(s) + 1;
	p = (char *) malloc(n);
	if (p != NULL) {
		memcpy(p, s, n);
	}
	return p;
}

/*
 * POSIX strncasecmp. WinCE coredll has the MSVC-named _strnicmp but not the
 * POSIX name (libcss mq_parse_op needs it). Thin forwarder.
 */
int strncasecmp(const char *s1, const char *s2, size_t n)
{
	return _strnicmp(s1, s2, n);
}

/*
 * C99 snprintf. WinCE coredll has _vsnprintf but not the C99 name (libdom
 * html_element.c needs it). Thin varargs forwarder. Note: like _vsnprintf,
 * this does not guarantee NUL-termination on truncation - libdom's callers
 * size their buffers accordingly.
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = _vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

/*
 * Standard C time(). WinCE 5 coredll has no time() at all (NetSurf libdom
 * stamps DOM events with it). Derive Unix seconds from the system clock via
 * FILETIME (100ns ticks since 1601-01-01; 11644473600 s to the Unix epoch).
 * Matches positron_tls's positron_time logic.
 */
time_t time(time_t *timer)
{
	SYSTEMTIME st;
	FILETIME ft;
	ULARGE_INTEGER uli;
	time_t secs;

	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	secs = (time_t) (uli.QuadPart / 10000000ULL - 11644473600ULL);

	if (timer != NULL) {
		*timer = secs;
	}
	return secs;
}
