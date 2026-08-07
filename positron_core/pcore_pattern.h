#ifndef POSITRON_PCORE_PATTERN_H
#define POSITRON_PCORE_PATTERN_H

#include <stddef.h>

/*
 * Match one HTML input value against a bounded pattern subset. The return
 * value is 1 for a full match, 0 for a mismatch, and -1 when the pattern is
 * invalid or outside the deliberately small WM6 engine subset.
 */
int pcore_pattern_match_full(const char *pattern, size_t pattern_len,
        const char *value, size_t value_len);

#endif
