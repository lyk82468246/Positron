/*
 * nsshim/desktop/selection.h - text selection. Stubbed: redraw asks
 * selection_highlighted per text box; html_content.sel is NULL so this is never
 * truly hit, but provide the symbol. Intercepts the real desktop/selection.h.
 */
#ifndef PCORE_SHIM_DESKTOP_SELECTION_H
#define PCORE_SHIM_DESKTOP_SELECTION_H

#include <stdbool.h>

struct selection;

bool selection_highlighted(const struct selection *s,
        unsigned start, unsigned end,
        unsigned *start_idx, unsigned *end_idx);

#endif
