/*
 * pcore_layout_stubs.c - no-op link stubs for functions the ported layout.c
 * references but which live in NetSurf subsystems we deliberately don't bring
 * in yet (iframes/objects, the textarea widget, selection/search,
 * and the box_dump debug dumper).
 *
 * The builder creates checkbox/radio gadgets whose layout and redraw are
 * self-contained in the ported NetSurf sources. Text/password/textarea boxes
 * use NetSurf geometry but are covered by platform-native EDIT children, so
 * their internal textarea widget pointer deliberately remains NULL and these
 * layout/redraw calls remain no-ops. Select widgets, iframes and browser-window
 * objects are not produced. Table, flex, scrollbar and border redraw now come
 * from real NetSurf sources.
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

/* tables: the real table_calculate_column_types / table_used_border_for_cell
 * now come from the ported table.c (M7-table). */

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

/* --- redraw: selection / search / scrollbar / messages ------------- */

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
