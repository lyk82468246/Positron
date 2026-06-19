/*
 * nsshim/content/content.h - the internal content API is not needed by the
 * ported layout/redraw (they use the content_get_* accessors from
 * netsurf/content.h, and cast struct content* to html_content*). Empty stub to
 * intercept the heavy real header. If layout/redraw turn out to call something
 * from here, add just that declaration.
 */
#ifndef PCORE_SHIM_CONTENT_CONTENT_H
#define PCORE_SHIM_CONTENT_CONTENT_H

#endif
