/*
 * nsshim/utils/nsurl.h - stub for NetSurf's URL object.
 *
 * The ported layout.c reaches nsurl_access once (NULL hrefs -> ""); box code
 * paths we don't exercise reference nsurl_ref/unref. nsurl stays an opaque
 * forward-declared type - we never construct or inspect one. Implemented in
 * pcore_nsshim.c. Intercepted ahead of the real utils/nsurl.h.
 */
#ifndef PCORE_SHIM_UTILS_NSURL_H
#define PCORE_SHIM_UTILS_NSURL_H

struct nsurl;
typedef struct nsurl nsurl;

const char *nsurl_access(const struct nsurl *url);
struct nsurl *nsurl_ref(struct nsurl *url);
void nsurl_unref(struct nsurl *url);

#endif
