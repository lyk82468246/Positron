/*
 * nsshim/content/textsearch.h - find-in-page highlighting. Stubbed: redraw asks
 * content_textsearch_ishighlighted per text box; we always say "not
 * highlighted". Intercepts the real content/textsearch.h.
 */
#ifndef PCORE_SHIM_CONTENT_TEXTSEARCH_H
#define PCORE_SHIM_CONTENT_TEXTSEARCH_H

#include <stdbool.h>

struct textsearch_context;

bool content_textsearch_ishighlighted(struct textsearch_context *textsearch,
        unsigned start_offset, unsigned end_offset,
        unsigned *start_idx, unsigned *end_idx);

#endif
