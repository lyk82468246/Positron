/* Private WM6 pointer classification. No DOM, scroll tree or event semantics. */
#ifndef POSITRON_APP_INPUT_H
#define POSITRON_APP_INPUT_H

#include <windows.h>

typedef struct AppInputPointer {
    int active;
    int dragging;
    int threshold;
    POINT down;
    POINT last;
} AppInputPointer;

void AppInput_PointerCancel(AppInputPointer *pointer);
void AppInput_PointerBegin(AppInputPointer *pointer, int dpi, int x, int y);
/* Returns one after crossing the threshold, with physical viewport deltas.
 * A drag remains a drag even when returning to its starting point. */
int AppInput_PointerMove(AppInputPointer *pointer, int x, int y,
        int *out_dx, int *out_dy);

#endif
