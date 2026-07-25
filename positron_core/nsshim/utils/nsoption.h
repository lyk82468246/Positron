/*
 * nsshim/utils/nsoption.h - fixed NetSurf options for positron_core.
 *
 * Positron does not expose NetSurf's mutable frontend option table yet.
 * Keep the options read by the port explicit instead of silently returning
 * zero for every name. Token pasting deliberately makes an unlisted option a
 * compile error when a newly imported upstream source starts reading it.
 */
#ifndef PCORE_SHIM_UTILS_NSOPTION_H
#define PCORE_SHIM_UTILS_NSOPTION_H

#include <stdbool.h>

/* NetSurf 3.11 desktop defaults retained by the current WM engine. */
#define PCORE_NSOPTION_BOOL_core_select_menu       false
#define PCORE_NSOPTION_BOOL_remove_backgrounds     false
#define PCORE_NSOPTION_INT_font_min_size            85

/* Product policy for paths that will be imported later. Keeping these named
 * here prevents a future source addition from accidentally changing policy. */
#define PCORE_NSOPTION_BOOL_author_level_css        true
#define PCORE_NSOPTION_BOOL_foreground_images       true
#define PCORE_NSOPTION_BOOL_background_images       true
#define PCORE_NSOPTION_BOOL_enable_javascript       false

#define PCORE_NSOPTION_BOOL_VALUE(name) \
    PCORE_NSOPTION_BOOL_VALUE_INNER(name)
#define PCORE_NSOPTION_BOOL_VALUE_INNER(name) \
    PCORE_NSOPTION_BOOL_##name
#define PCORE_NSOPTION_INT_VALUE(name) \
    PCORE_NSOPTION_INT_VALUE_INNER(name)
#define PCORE_NSOPTION_INT_VALUE_INNER(name) \
    PCORE_NSOPTION_INT_##name
#define PCORE_NSOPTION_UINT_VALUE(name) \
    PCORE_NSOPTION_UINT_VALUE_INNER(name)
#define PCORE_NSOPTION_UINT_VALUE_INNER(name) \
    PCORE_NSOPTION_UINT_##name
#define PCORE_NSOPTION_STRING_VALUE(name) \
    PCORE_NSOPTION_STRING_VALUE_INNER(name)
#define PCORE_NSOPTION_STRING_VALUE_INNER(name) \
    PCORE_NSOPTION_STRING_##name

#define nsoption_bool(name)   (PCORE_NSOPTION_BOOL_VALUE(name))
#define nsoption_int(name)    (PCORE_NSOPTION_INT_VALUE(name))
#define nsoption_uint(name)   (PCORE_NSOPTION_UINT_VALUE(name))
#define nsoption_charp(name)  ((char *) PCORE_NSOPTION_STRING_VALUE(name))

#endif
