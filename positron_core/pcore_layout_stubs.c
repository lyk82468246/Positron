/*
 * pcore_layout_stubs.c - no-op link stubs for functions the ported layout.c
 * references but which live in NetSurf subsystems we deliberately don't bring
 * in yet (tables = M7, flex = M7, iframes/objects, the textarea widget, and the
 * box_dump debug dumper).
 *
 * Every one of these is only ever *called* on a path our slim box builder never
 * produces: box->object / box->gadget / html_content.bw are always NULL, and
 * construct maps display:table/flex to BOX_BLOCK so layout_table / layout_flex
 * are never entered. So these stubs exist purely to satisfy the linker; if one
 * is ever actually invoked it is a sign the staged scope changed.
 *
 * Signatures match the real declarations so the (unused) calls stay ABI-correct.
 * C89.
 */

#include <stdio.h>
#include <stdbool.h>

#include <libcss/libcss.h>

#include "netsurf/plot_style.h"
#include "content/handlers/html/box.h"

struct textarea;
struct browser_window;
struct hlcache_handle;
struct html_content;

/* --- tables (M7) -------------------------------------------------- */

bool table_calculate_column_types(const css_unit_ctx *unit_len_ctx,
        struct box *table)
{
    (void) unit_len_ctx;
    (void) table;
    return false;   /* "could not establish column types" -> minmax bails */
}

void table_used_border_for_cell(const css_unit_ctx *unit_len_ctx,
        struct box *cell)
{
    (void) unit_len_ctx;
    (void) cell;
}

/* --- flex (M7) ---------------------------------------------------- */

bool layout_flex(struct box *flex, int available_width,
        struct html_content *content)
{
    (void) flex;
    (void) available_width;
    (void) content;
    return false;
}

/* --- debug dump --------------------------------------------------- */

void box_dump(FILE *stream, struct box *box, unsigned int depth, bool style)
{
    (void) stream;
    (void) box;
    (void) depth;
    (void) style;
}

/* --- objects / iframes (NULL in our pipeline) --------------------- */

struct box *html_get_box_tree(struct hlcache_handle *h)
{
    (void) h;
    return NULL;
}

void browser_window_reformat(struct browser_window *bw, bool background,
        int width, int height)
{
    (void) bw;
    (void) background;
    (void) width;
    (void) height;
}

void browser_window_set_dimensions(struct browser_window *bw,
        int width, int height)
{
    (void) bw;
    (void) width;
    (void) height;
}

void browser_window_set_position(struct browser_window *bw, int x, int y)
{
    (void) bw;
    (void) x;
    (void) y;
}

/* --- textarea widget ---------------------------------------------- */

void textarea_set_layout(struct textarea *ta, const plot_font_style_t *fstyle,
        int width, int height, int top, int right, int bottom, int left)
{
    (void) ta;
    (void) fstyle;
    (void) width;
    (void) height;
    (void) top;
    (void) right;
    (void) bottom;
    (void) left;
}
