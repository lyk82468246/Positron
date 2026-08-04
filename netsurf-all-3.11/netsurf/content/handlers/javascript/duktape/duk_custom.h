/*
 * Copyright 2015 Daniel Silverstone <dsilvers@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Custom configuration for duktape
 */

#include "utils/config.h"

#if defined(_WIN32_WCE)
/* Windows Mobile exposes the FILETIME conversion APIs used by Duktape but
 * not SystemTimeToTzSpecificLocalTime. Use the existing no-DST provider;
 * timezone-aware formatting is not part of this first standalone ABI. */
#undef DUK_USE_DATE_TZO_WINDOWS
#define DUK_USE_DATE_TZO_WINDOWS_NO_DST
#endif

#ifndef HAVE_STRPTIME
#undef DUK_USE_DATE_PRS_STRPTIME
#undef DUK_USE_DATE_PRS_GETDATE
#undef DUK_USE_DATE_PARSE_STRING
#endif

#define DUK_USE_FASTINT
#define DUK_USE_REGEXP_CANON_WORKAROUND

/* Required for execution timeout checking */
#define DUK_USE_INTERRUPT_COUNTER

extern duk_bool_t dukky_check_timeout(void *udata);
#define DUK_USE_EXEC_TIMEOUT_CHECK dukky_check_timeout

#include "netsurf/inttypes.h"
