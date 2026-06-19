/*
 * pcore_plot_style.c - the global plot styles redraw.c references.
 *
 * NetSurf's desktop/plot_style.c is C99 designated-init; rather than c89ize all
 * 18 entries, this provides the 12 globals redraw.c actually uses (all
 * plot_style_t). Their exact values barely matter: they style form widgets
 * (checkbox/radio/scrollbar) we never draw (gadget==NULL) and the box-model
 * debug edges (html_redraw_debug is off). Positional initialisers (C89);
 * plot_style_t field order is stroke_type, stroke_width, stroke_colour,
 * fill_type, fill_colour.
 */

#include "netsurf/plot_style.h"

static plot_style_t s_fill_white = {
    0, 0, 0, PLOT_OP_TYPE_SOLID, 0xffffff
};
plot_style_t *plot_style_fill_white = &s_fill_white;

/* box-model debug edges (only drawn when html_redraw_debug is true) */
static const plot_style_t s_content_edge = {
    PLOT_OP_TYPE_SOLID, plot_style_int_to_fixed(1), 0x00ff0000, 0, 0
};
plot_style_t const * const plot_style_content_edge = &s_content_edge;

static const plot_style_t s_padding_edge = {
    PLOT_OP_TYPE_SOLID, plot_style_int_to_fixed(1), 0x000000ff, 0, 0
};
plot_style_t const * const plot_style_padding_edge = &s_padding_edge;

static const plot_style_t s_margin_edge = {
    PLOT_OP_TYPE_SOLID, plot_style_int_to_fixed(1), 0x0000ffff, 0, 0
};
plot_style_t const * const plot_style_margin_edge = &s_margin_edge;

static const plot_style_t s_broken_object = {
    PLOT_OP_TYPE_SOLID, plot_style_int_to_fixed(1), 0x000000ff,
    PLOT_OP_TYPE_SOLID, 0x008888ff
};
plot_style_t const * const plot_style_broken_object = &s_broken_object;

/* widget base/blob fills + strokes (widgets are never drawn here) */
static plot_style_t s_fill_wbasec = {
    0, 0, 0, PLOT_OP_TYPE_SOLID, WIDGET_BASEC
};
plot_style_t *plot_style_fill_wbasec = &s_fill_wbasec;

static plot_style_t s_fill_darkwbasec = {
    0, 0, 0, PLOT_OP_TYPE_SOLID, WIDGET_BASEC
};
plot_style_t *plot_style_fill_darkwbasec = &s_fill_darkwbasec;

static plot_style_t s_fill_lightwbasec = {
    0, 0, 0, PLOT_OP_TYPE_SOLID, WIDGET_BASEC
};
plot_style_t *plot_style_fill_lightwbasec = &s_fill_lightwbasec;

static plot_style_t s_fill_wblobc = {
    0, 0, 0, PLOT_OP_TYPE_SOLID, WIDGET_BLOBC
};
plot_style_t *plot_style_fill_wblobc = &s_fill_wblobc;

static plot_style_t s_stroke_wblobc = {
    PLOT_OP_TYPE_SOLID, plot_style_int_to_fixed(2), WIDGET_BLOBC, 0, 0
};
plot_style_t *plot_style_stroke_wblobc = &s_stroke_wblobc;

static plot_style_t s_stroke_darkwbasec = {
    PLOT_OP_TYPE_SOLID, 0, WIDGET_BASEC, 0, 0
};
plot_style_t *plot_style_stroke_darkwbasec = &s_stroke_darkwbasec;

static plot_style_t s_stroke_lightwbasec = {
    PLOT_OP_TYPE_SOLID, 0, WIDGET_BASEC, 0, 0
};
plot_style_t *plot_style_stroke_lightwbasec = &s_stroke_lightwbasec;

/* broken-object replacement font style (never drawn; object==NULL).
 * Partial positional init: families/family/size/weight/flags; the rest 0. */
static const plot_font_style_t s_fstyle_broken_object = {
    0, PLOT_FONT_FAMILY_SANS_SERIF, plot_style_int_to_fixed(16), 400, FONTF_NONE
};
plot_font_style_t const * const plot_fstyle_broken_object =
        &s_fstyle_broken_object;
