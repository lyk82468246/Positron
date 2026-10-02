#include <string.h>
#include "app_input.h"

void AppInput_PointerCancel(AppInputPointer *pointer)
{
    if (pointer != NULL) memset(pointer, 0, sizeof(*pointer));
}

void AppInput_PointerBegin(AppInputPointer *pointer, int dpi, int x, int y)
{
    if (pointer == NULL) return;
    AppInput_PointerCancel(pointer);
    pointer->active = 1;
    /* Four logical pixels of slop, converted once to physical input units. */
    pointer->threshold = MulDiv(4, dpi > 0 ? dpi : 96, 96);
    if (pointer->threshold < 1) pointer->threshold = 1;
    pointer->down.x = x;
    pointer->down.y = y;
    pointer->last = pointer->down;
}

int AppInput_PointerMove(AppInputPointer *pointer, int x, int y,
        int *out_dx, int *out_dy)
{
    int dx;
    int dy;

    if (out_dx == NULL || out_dy == NULL) return 0;
    *out_dx = 0;
    *out_dy = 0;
    if (pointer == NULL || !pointer->active) return 0;
    dx = x - pointer->down.x;
    dy = y - pointer->down.y;
    if (!pointer->dragging && dx <= pointer->threshold &&
            dx >= -pointer->threshold && dy <= pointer->threshold &&
            dy >= -pointer->threshold) return 0;
    pointer->dragging = 1;
    *out_dx = pointer->last.x - x;
    *out_dy = pointer->last.y - y;
    pointer->last.x = x;
    pointer->last.y = y;
    return 1;
}
