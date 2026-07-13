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
 * scrollbar handling kept aligned with the corresponding NetSurf routines.
 *
 * C89.
 */

#include <assert.h>
#include <stdlib.h>

#include <dom/dom.h>
#include <libcss/libcss.h>

#include "utils/errors.h"
#include "desktop/scrollbar.h"
#include "content/handlers/html/box.h"

#define pcore_box_is_float(box) ((box)->type == BOX_FLOAT_LEFT || \
        (box)->type == BOX_FLOAT_RIGHT)

struct pcore_scrollbar_data {
    struct box *box;
    bool dragging;
};

static void pcore_scrollbar_callback(void *client_data,
        struct scrollbar_msg_data *msg)
{
    struct pcore_scrollbar_data *data;

    data = (struct pcore_scrollbar_data *) client_data;
    if (data == NULL || msg == NULL) {
        return;
    }
    if (msg->msg == SCROLLBAR_MSG_SCROLL_START) {
        data->dragging = true;
    } else if (msg->msg == SCROLLBAR_MSG_SCROLL_FINISHED) {
        data->dragging = false;
    }
}

/* Absolute document coordinates, including ancestor overflow offsets. */
void box_coords(struct box *box, int *x, int *y)
{
    *x = box->x;
    *y = box->y;
    while (box->parent != NULL) {
        if (pcore_box_is_float(box)) {
            assert(box->float_container != NULL);
            box = box->float_container;
        } else {
            box = box->parent;
        }
        *x += box->x - scrollbar_get_offset(box->scroll_x);
        *y += box->y - scrollbar_get_offset(box->scroll_y);
    }
}

bool box_vscrollbar_present(const struct box *box)
{
    return box->padding[TOP] + box->height + box->padding[BOTTOM] +
            box->border[BOTTOM].width < box->descendant_y1;
}

bool box_hscrollbar_present(const struct box *box)
{
    return box->padding[LEFT] + box->width + box->padding[RIGHT] +
            box->border[RIGHT].width < box->descendant_x1;
}

static void pcore_scrollbar_dispose(struct scrollbar **scrollbar)
{
    void *data;

    if (scrollbar == NULL || *scrollbar == NULL) {
        return;
    }
    data = scrollbar_get_data(*scrollbar);
    scrollbar_destroy(*scrollbar);
    free(data);
    *scrollbar = NULL;
}

bool pcore_scrollbar_is_dragging(struct scrollbar *scrollbar)
{
    struct pcore_scrollbar_data *data;

    if (scrollbar == NULL) {
        return false;
    }
    data = (struct pcore_scrollbar_data *) scrollbar_get_data(scrollbar);
    return data != NULL && data->dragging;
}

void pcore_box_scrollbars_destroy(struct box *box)
{
    struct box *child;

    if (box == NULL) {
        return;
    }
    for (child = box->children; child != NULL; child = child->next) {
        pcore_box_scrollbars_destroy(child);
    }
    pcore_scrollbar_dispose(&box->scroll_x);
    pcore_scrollbar_dispose(&box->scroll_y);
}

nserror box_handle_scrollbars(struct content *c, struct box *box,
        bool bottom, bool right)
{
    struct pcore_scrollbar_data *data;
    int visible_width;
    int visible_height;
    int full_width;
    int full_height;
    nserror res;

    (void) c;
    if (!bottom) {
        pcore_scrollbar_dispose(&box->scroll_x);
    }
    if (!right) {
        pcore_scrollbar_dispose(&box->scroll_y);
    }
    if (!bottom && !right) {
        return NSERROR_OK;
    }

    visible_width = box->width + box->padding[LEFT] + box->padding[RIGHT];
    visible_height = box->height + box->padding[TOP] + box->padding[BOTTOM];
    full_width = (box->descendant_x1 - box->border[RIGHT].width >
            visible_width) ? box->descendant_x1 + box->padding[RIGHT] :
            visible_width;
    full_height = (box->descendant_y1 - box->border[BOTTOM].width >
            visible_height) ? box->descendant_y1 + box->padding[BOTTOM] :
            visible_height;

    if (right) {
        if (box->scroll_y == NULL) {
            data = (struct pcore_scrollbar_data *) malloc(sizeof(*data));
            if (data == NULL) {
                return NSERROR_NOMEM;
            }
            data->box = box;
            data->dragging = false;
            res = scrollbar_create(false, visible_height, full_height,
                    visible_height, data, pcore_scrollbar_callback,
                    &box->scroll_y);
            if (res != NSERROR_OK) {
                free(data);
                return res;
            }
        } else {
            scrollbar_set_extents(box->scroll_y, visible_height,
                    visible_height, full_height);
        }
    }
    if (bottom) {
        if (box->scroll_x == NULL) {
            data = (struct pcore_scrollbar_data *) malloc(sizeof(*data));
            if (data == NULL) {
                return NSERROR_NOMEM;
            }
            data->box = box;
            data->dragging = false;
            res = scrollbar_create(true,
                    visible_width - (right ? SCROLLBAR_WIDTH : 0),
                    full_width, visible_width, data,
                    pcore_scrollbar_callback, &box->scroll_x);
            if (res != NSERROR_OK) {
                free(data);
                return res;
            }
        } else {
            scrollbar_set_extents(box->scroll_x,
                    visible_width - (right ? SCROLLBAR_WIDTH : 0),
                    visible_width, full_width);
        }
    }
    if (right && bottom) {
        scrollbar_make_pair(box->scroll_x, box->scroll_y);
    }
    return NSERROR_OK;
}
