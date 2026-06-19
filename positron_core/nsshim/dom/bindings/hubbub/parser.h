/*
 * nsshim/dom/bindings/hubbub/parser.h - stub for the dom-hubbub binding header.
 *
 * private.h embeds dom_hubbub_parser* (pointer) and dom_hubbub_encoding_source
 * (value); html.h / script decls use dom_hubbub_error. The real header lives at
 * libdom/bindings/hubbub/parser.h and pulls hubbub + dom error headers via
 * include roots we don't expose here. positron_core.c includes the REAL header
 * by relative path (so dom_hubbub_parser_create etc. resolve there) in a
 * different TU; this stub only serves the layout/redraw path, which never calls
 * those functions. Intercepts <dom/bindings/hubbub/parser.h>.
 */
#ifndef PCORE_SHIM_DOM_HUBBUB_PARSER_H
#define PCORE_SHIM_DOM_HUBBUB_PARSER_H

struct dom_node;

typedef enum { DOM_HUBBUB_OK = 0 } dom_hubbub_error;

typedef dom_hubbub_error (*dom_script)(void *ctx, struct dom_node *node);

typedef struct dom_hubbub_parser dom_hubbub_parser;

typedef enum dom_hubbub_encoding_source {
    DOM_HUBBUB_ENCODING_SOURCE_HEADER,
    DOM_HUBBUB_ENCODING_SOURCE_DETECTED,
    DOM_HUBBUB_ENCODING_SOURCE_META
} dom_hubbub_encoding_source;

#endif