#ifndef PIMAGE_RASTER_H
#define PIMAGE_RASTER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

struct svgtiny_diagram;

void *pimage_raster_create(const struct svgtiny_diagram *diagram);
void pimage_raster_free(void *raster_image);
int pimage_raster_draw(void *raster_image, HDC hdc,
        int x, int y, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
