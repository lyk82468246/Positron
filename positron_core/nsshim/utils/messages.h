/*
 * nsshim/utils/messages.h - localised message lookup. redraw uses messages_get
 * / messages_get_errorcode only for error/broken-object text we never show
 * (object==NULL). Return empty strings. Intercepts the real utils/messages.h.
 */
#ifndef PCORE_SHIM_UTILS_MESSAGES_H
#define PCORE_SHIM_UTILS_MESSAGES_H

#include "utils/errors.h"

const char *messages_get(const char *key);
const char *messages_get_errorcode(nserror code);

#endif
