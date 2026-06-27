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

#include "utils/errors.h"
#include "netsurf/plot_style.h"
#include "content/handlers/html/box.h"

struct textarea;
struct browser_window;
struct hlcache_handle;
struct html_content;
struct selection;
struct textsearch_context;
struct scrollbar;
struct redraw_context;
struct form_control;

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

/* --- redraw: selection / search / scrollbar / messages / borders --- */

bool selection_highlighted(const struct selection *s,
        unsigned start, unsigned end,
        unsigned *start_idx, unsigned *end_idx)
{
    (void) s;
    (void) start;
    (void) end;
    (void) start_idx;
    (void) end_idx;
    return false;
}

bool content_textsearch_ishighlighted(struct textsearch_context *textsearch,
        unsigned start_offset, unsigned end_offset,
        unsigned *start_idx, unsigned *end_idx)
{
    (void) textsearch;
    (void) start_offset;
    (void) end_offset;
    (void) start_idx;
    (void) end_idx;
    return false;
}

nserror scrollbar_redraw(struct scrollbar *s, int x, int y,
        const struct rect *clip, float scale,
        const struct redraw_context *ctx)
{
    (void) s;
    (void) x;
    (void) y;
    (void) clip;
    (void) scale;
    (void) ctx;
    return NSERROR_OK;
}

const char *messages_get(const char *key)
{
    (void) key;
    return "";
}

const char *messages_get_errorcode(nserror code)
{
    (void) code;
    return "";
}

/* Border drawing lives in redraw_border.c, not ported yet (M5 follow-up).
 * Stubbed to "drew nothing, ok" so the page body/text render first; real
 * borders come next. */
bool html_redraw_borders(struct box *box, int x_parent, int y_parent,
        int p_width, int p_height, const struct rect *clip, float scale,
        const struct redraw_context *ctx)
{
    (void) box;
    (void) x_parent;
    (void) y_parent;
    (void) p_width;
    (void) p_height;
    (void) clip;
    (void) scale;
    (void) ctx;
    return true;
}

bool html_redraw_inline_borders(struct box *box, struct rect b,
        const struct rect *clip, float scale, bool first, bool last,
        const struct redraw_context *ctx)
{
    (void) box;
    (void) b;
    (void) clip;
    (void) scale;
    (void) first;
    (void) last;
    (void) ctx;
    return true;
}

/* --- redraw: textarea / iframe / form-select widgets (never drawn) --- */

void textarea_redraw(struct textarea *ta, int x, int y, colour bg, float scale,
        const struct rect *clip, const struct redraw_context *ctx)
{
    (void) ta;
    (void) x;
    (void) y;
    (void) bg;
    (void) scale;
    (void) clip;
    (void) ctx;
}

bool browser_window_redraw(struct browser_window *bw, int x, int y,
        const struct rect *clip, const struct redraw_context *ctx)
{
    (void) bw;
    (void) x;
    (void) y;
    (void) clip;
    (void) ctx;
    return true;
}

bool form_redraw_select_menu(struct form_control *control, int x, int y,
        float scale, const struct rect *clip,
        const struct redraw_context *ctx)
{
    (void) control;
    (void) x;
    (void) y;
    (void) scale;
    (void) clip;
    (void) ctx;
    return true;
}

bool form_clip_inside_select_menu(struct form_control *control, float scale,
        const struct rect *clip)
{
    (void) control;
    (void) scale;
    (void) clip;
    return false;
}
