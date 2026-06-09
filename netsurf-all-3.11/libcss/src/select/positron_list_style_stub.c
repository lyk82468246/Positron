/*
 * positron_list_style_stub.c - Positron WinCE bring-up stub for libcss list
 * marker formatting.
 *
 * The upstream select/format_list_style.c implements CSS counter styles
 * (decimal, lower-roman, cjk-*, armenian, georgian, ...) with ~255 C99
 * designated / nested-struct initializers, which the VS2008 C89 compiler
 * cannot parse. For Phase 4 bring-up we exclude that file from the build and
 * provide this decimal-only implementation of the one exported entry point.
 * Full counter-style support is deferred; the original is preserved in git
 * (commit 9d55f7a) and can be ported later (e.g. via CeGCC).
 */

#include <stdio.h>
#include <string.h>

#include "select/propget.h"
#include "utils/utils.h"

/* Exported interface defined in libcss/select.h. Decimal-only for now:
 * formats the integer value followed by a '.' suffix, matching the common
 * default list marker. Writes up to buffer_length bytes (not necessarily
 * NUL-terminated) and reports the full required length in *format_length. */
css_error css_computed_format_list_style(
		const css_computed_style *style,
		int value,
		char *buffer,
		size_t buffer_length,
		size_t *format_length)
{
	char tmp[32];
	int n;
	size_t len;

	UNUSED(style);

	n = _snprintf(tmp, sizeof(tmp), "%d.", value);
	if (n < 0 || n > (int) sizeof(tmp)) {
		n = 0;
	}
	len = (size_t) n;

	if (format_length != NULL) {
		*format_length = len;
	}
	if (buffer != NULL && buffer_length > 0) {
		size_t copy = (len < buffer_length) ? len : buffer_length;
		memcpy(buffer, tmp, copy);
	}

	return CSS_OK;
}
