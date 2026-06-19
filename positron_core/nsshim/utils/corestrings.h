/*
 * nsshim/utils/corestrings.h - stub for the handful of interned dom_strings the
 * ported layout.c references.
 *
 * NetSurf's corestrings is a huge generated table; layout.c only touches a few
 * (the <ol start/reversed/value> attributes and the box/canvas node-data
 * user-data keys). pcore_nsshim.c interns just these (pcore_nsshim_init). A
 * NULL/absent attribute simply falls back to defaults in layout.
 * Intercepted ahead of the real utils/corestrings.h.
 */
#ifndef PCORE_SHIM_UTILS_CORESTRINGS_H
#define PCORE_SHIM_UTILS_CORESTRINGS_H

#include <dom/dom.h>

extern dom_string *corestring_dom_start;
extern dom_string *corestring_dom_reversed;
extern dom_string *corestring_dom_value;
extern dom_string *corestring_dom___ns_key_box_node_data;
extern dom_string *corestring_dom___ns_key_canvas_node_data;

#endif
