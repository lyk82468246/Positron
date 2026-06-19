/*
 * nsshim/content/content_protected.h - placeholder for NetSurf's struct content.
 *
 * The real one is the content base class (url, mime, status machine, handler
 * vtable, cache state, ...). html_content embeds it as `base`, but the ported
 * layout.c / redraw.c never read any base field (they cast struct content* back
 * to html_content*). So a placeholder of any size satisfies the embed.
 * Intercepted ahead of the real content/content_protected.h.
 */
#ifndef PCORE_SHIM_CONTENT_PROTECTED_H
#define PCORE_SHIM_CONTENT_PROTECTED_H

struct content {
    int _pcore_placeholder;
};

#endif
