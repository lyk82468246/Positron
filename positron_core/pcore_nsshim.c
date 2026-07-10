/*
 * pcore_nsshim.c - implementations behind the nsshim/ stub headers.
 *
 * Backs the NetSurf utility surface the ported layout.c / redraw.c need but
 * which we deliberately do not bring in wholesale: interned corestrings, the
 * nsurl accessors, the `guit` operation table (its ->layout points at our GDI
 * font measurement table so redraw measures text exactly as layout did), and
 * the content_get_* object accessors. The image subset represents cached WM
 * Imaging bytes as a small bitmap carrier; form/iframe/scrollbar/gadget and
 * selection paths remain constant defaults.
 *
 * C89.
 */

#include <stdbool.h>
#include <string.h>

#include <dom/dom.h>

#include "utils/errors.h"      /* nserror (used by layout.h) */
#include "netsurf/layout.h"    /* struct gui_layout_table */
#include "netsurf/content.h"   /* content_get_* decls, content_type, redraw_data */
#include "netsurf/bitmap.h"    /* Positron's cached-image carrier */
#include "netsurf/plotters.h"  /* redraw_context / BITMAPF_* */

#include "utils/corestrings.h"
#include "utils/nsurl.h"
#include "desktop/gui_table.h"
#include "desktop/scrollbar.h"

#include "pcore_internal.h"

/* The GDI font measurement table, defined in pcore_plot_gdi.c (M2). */
extern const struct gui_layout_table pcore_gdi_layout;

/* ---- guit: minimal NetSurf operation table ----------------------- */

static struct netsurf_table pcore_nstable; /* ->layout set in init */
struct netsurf_table *guit = &pcore_nstable;

/* ---- corestrings (interned on first PCore use) ------------------- */

dom_string *corestring_dom_start = NULL;
dom_string *corestring_dom_reversed = NULL;
dom_string *corestring_dom_value = NULL;
dom_string *corestring_dom___ns_key_box_node_data = NULL;
dom_string *corestring_dom___ns_key_canvas_node_data = NULL;

static void pcore_intern1(const char *s, dom_string **out)
{
    if (*out == NULL) {
        dom_string_create((const uint8_t *) s, strlen(s), out);
    }
}

void pcore_nsshim_init(void)
{
    pcore_nstable.layout = &pcore_gdi_layout;

    pcore_intern1("start", &corestring_dom_start);
    pcore_intern1("reversed", &corestring_dom_reversed);
    pcore_intern1("value", &corestring_dom_value);
    pcore_intern1("__ns_key_box_node_data",
            &corestring_dom___ns_key_box_node_data);
    pcore_intern1("__ns_key_canvas_node_data",
            &corestring_dom___ns_key_canvas_node_data);
}

/* ---- nsurl: opaque, never constructed in our pipeline ------------ */

const char *nsurl_access(const struct nsurl *url)
{
    (void) url;
    return "";
}

struct nsurl *nsurl_ref(struct nsurl *url)
{
    return url;
}

void nsurl_unref(struct nsurl *url)
{
    (void) url;
}

/* ---- content_get_*: cached image objects plus safe defaults -------- */

static struct bitmap *pcore_image_bitmap(struct hlcache_handle *h)
{
    return (struct bitmap *) h;
}

content_type content_get_type(struct hlcache_handle *h)
{
    return (h != NULL) ? CONTENT_IMAGE : CONTENT_NONE;
}

int content_get_width(struct hlcache_handle *h)
{
    struct bitmap *bitmap = pcore_image_bitmap(h);
    return (bitmap != NULL) ? bitmap->width : 0;
}

int content_get_height(struct hlcache_handle *h)
{
    struct bitmap *bitmap = pcore_image_bitmap(h);
    return (bitmap != NULL) ? bitmap->height : 0;
}

int content_get_available_width(struct hlcache_handle *h)
{
    (void) h;
    return 0;
}

bool content_get_opaque(struct hlcache_handle *h)
{
    (void) h;
    return false;
}

struct nsurl *content_get_url(struct hlcache_handle *h)
{
    (void) h;
    return NULL;
}

bool content_redraw(struct hlcache_handle *h, struct content_redraw_data *data,
        const struct rect *clip, const struct redraw_context *ctx)
{
    (void) clip;
    if (h == NULL || data == NULL || ctx == NULL || ctx->plot == NULL ||
            ctx->plot->bitmap == NULL) {
        return false;
    }
    return ctx->plot->bitmap(ctx, pcore_image_bitmap(h), data->x, data->y,
            data->width, data->height, data->background_colour,
            (data->repeat_x ? BITMAPF_REPEAT_X : BITMAPF_NONE) |
            (data->repeat_y ? BITMAPF_REPEAT_Y : BITMAPF_NONE)) == NSERROR_OK;
}

void content_reformat(struct hlcache_handle *h, bool background,
        int width, int height)
{
    (void) h;
    (void) background;
    (void) width;
    (void) height;
}

bool content_can_reformat(struct hlcache_handle *h)
{
    (void) h;
    return false;
}

/* ---- scrollbar: never attached in the slim build ----------------- */

int scrollbar_get_offset(struct scrollbar *s)
{
    (void) s;
    return 0;
}
