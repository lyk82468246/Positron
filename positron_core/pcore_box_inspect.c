/*
 * pcore_box_inspect.c - the handful of box-tree query functions that the
 * ported layout.c / redraw.c call, slimmed from NetSurf's box_inspect.c +
 * box_manipulate.c.
 *
 * The real box_inspect.c pulls nsurl / content / mouse / scrollbar / html
 * private state for box_at_point / box_dump / box_pick_text_box etc. that we
 * don't need. layout.c and redraw.c only call box_coords, the scrollbar
 * predicates, and box_handle_scrollbars - and the many box_is_* / box_children
 * / box_has_background / box_sizing predicates come for free as static inlines
 * in layout_internal.h. So this provides just those few, with the float /
 * scrollbar handling reduced to what our slim box builder currently produces
 * (no floats, no scrollbars yet - revisited when those land).
 *
 * C89.
 */

#include <dom/dom.h>
#include <libcss/libcss.h>

#include "utils/errors.h"
#include "content/handlers/html/box.h"

/* Absolute document coordinates of a box (NetSurf box_coords). We do not yet
 * generate float boxes or scrollable boxes, so the float_container / scrollbar
 * offset handling of the original collapses to a plain parent-chain sum. */
void box_coords(struct box *box, int *x, int *y)
{
    *x = box->x;
    *y = box->y;
    while (box->parent != NULL) {
        box = box->parent;
        *x += box->x;
        *y += box->y;
    }
}

/* We never attach scrollbars in the slim build. */
int box_vscrollbar_present(const struct box *box)
{
    return (box->scroll_y != NULL) ? 1 : 0;
}

int box_hscrollbar_present(const struct box *box)
{
    return (box->scroll_x != NULL) ? 1 : 0;
}

/* redraw.c calls this to (re)create overflow scrollbars; we have none, so it is
 * a no-op success. Signature matches box_manipulate.h. */
nserror box_handle_scrollbars(struct content *c, struct box *box,
        bool bottom, bool right)
{
    (void) c;
    (void) box;
    (void) bottom;
    (void) right;
    return NSERROR_OK;
}
