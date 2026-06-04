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
#include <windows.h>   /* TerminateProcess, GetCurrentProcess (for abort) */

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
