/*
 * nsshim/desktop/frame_types.h - frameset dimension type used by html.h's
 * content_html_frames / content_html_iframe structs. Copied verbatim from the
 * real desktop/frame_types.h (which our slim pipeline can't reach by include
 * root the same way). Our pipeline ignores framesets; this just lets html.h
 * compile. Intercepted ahead of the real header.
 */
#ifndef PCORE_SHIM_DESKTOP_FRAME_TYPES_H
#define PCORE_SHIM_DESKTOP_FRAME_TYPES_H

struct frame_dimension {
    float value;
    enum {
        FRAME_DIMENSION_PIXELS,
        FRAME_DIMENSION_PERCENT,
        FRAME_DIMENSION_RELATIVE
    } unit;
};

struct content_html_iframe;
struct content_html_frames;

#endif