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

struct textsearch_context;

struct content {
    /* redraw.c reads c->textsearch.context (NULL in our pipeline => no search
     * highlight). No other base field is read by layout/redraw. */
    struct {
        struct textsearch_context *context;
    } textsearch;
    int _pcore_placeholder;
};

#endif
