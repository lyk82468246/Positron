/*
 * HTML pattern constraint adapter.
 *
 * The browser core does not execute JavaScript, so it uses a small, static
 * C89 regex engine for the common ASCII subset that can be represented safely
 * on WM6. Unsupported syntax is ignored by form validation instead of being
 * guessed. The matcher is intentionally single-call-at-a-time, matching the
 * UI-thread form validation path.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "../third_party/tiny-regex-c/re.h"
#include "pcore_pattern.h"

#define PCORE_PATTERN_MAX_BYTES 4096
#define PCORE_PATTERN_MAX_VALUE_BYTES 65535
#define PCORE_PATTERN_MAX_OBJECTS 27
#define PCORE_PATTERN_MAX_CLASS_BYTES 38

static int pcore_pattern_is_escape_char(unsigned char c)
{
    if (c == 'd' || c == 'D' || c == 's' || c == 'S' ||
            c == 'w' || c == 'W') {
        return 1;
    }
    return c == '.' || c == '^' || c == '$' || c == '*' ||
            c == '+' || c == '?' || c == '[' || c == ']' ||
            c == '\\' || c == '-' || c == '(' || c == ')' ||
            c == '|' || c == '{' || c == '}';
}

/*
 * tiny-regex-c intentionally supports a compact syntax. This preflight is
 * important: its compiler has fixed storage and treats some unsupported
 * operators as ordinary characters. Rejecting those cases prevents a page
 * from receiving a false validity result.
 */
static int pcore_pattern_supported(const char *pattern, size_t length)
{
    size_t index;
    size_t class_bytes;
    unsigned int objects;
    unsigned char c;
    unsigned char escaped;
    int closed;

    if (pattern == NULL || length == 0 || length > PCORE_PATTERN_MAX_BYTES) {
        return (length == 0) ? 1 : 0;
    }
    index = 0;
    class_bytes = 0;
    objects = 0;
    while (index < length) {
        c = (unsigned char) pattern[index];
        if (c >= 0x80) {
            return 0;
        }
        if (c == '\\') {
            if (index + 1 >= length) {
                return 0;
            }
            escaped = (unsigned char) pattern[index + 1];
            if (escaped >= 0x80 || !pcore_pattern_is_escape_char(escaped)) {
                return 0;
            }
            objects++;
            index += 2;
        } else if (c == '[') {
            objects++;
            index++;
            if (index < length && pattern[index] == '^') {
                /* The vendored engine documents inverted classes as broken. */
                return 0;
            }
            closed = 0;
            while (index < length) {
                c = (unsigned char) pattern[index];
                if (c == ']') {
                    closed = 1;
                    index++;
                    break;
                }
                if (c == '\\' || c >= 0x80) {
                    /* Keep character classes to literal/range ASCII. */
                    return 0;
                }
                class_bytes++;
                if (class_bytes > PCORE_PATTERN_MAX_CLASS_BYTES) {
                    return 0;
                }
                index++;
            }
            if (!closed) {
                return 0;
            }
        } else if (c == '(' || c == ')' || c == '|' ||
                c == '{' || c == '}') {
            return 0;
        } else if (c == '*' || c == '+' || c == '?') {
            if (objects == 0) {
                return 0;
            }
            objects++;
            index++;
        } else if (c == '^') {
            if (index != 0) {
                return 0;
            }
            objects++;
            index++;
        } else if (c == '$') {
            if (index + 1 != length) {
                return 0;
            }
            objects++;
            index++;
        } else if (c == ']') {
            return 0;
        } else {
            objects++;
            index++;
        }
        if (objects > PCORE_PATTERN_MAX_OBJECTS) {
            return 0;
        }
    }
    return 1;
}

int pcore_pattern_match_full(const char *pattern, size_t pattern_len,
        const char *value, size_t value_len)
{
    char *anchored;
    char *text;
    size_t anchored_len;
    re_t compiled;
    int match_length;
    int match_index;
    int result;

    if (pattern == NULL || value == NULL || value_len == 0 ||
            value_len > PCORE_PATTERN_MAX_VALUE_BYTES ||
            pattern_len > PCORE_PATTERN_MAX_BYTES ||
            pattern_len > (size_t) INT_MAX - 3 ||
            value_len > (size_t) INT_MAX) {
        return -1;
    }
    if (!pcore_pattern_supported(pattern, pattern_len)) {
        return -1;
    }
    anchored_len = pattern_len + 2;
    anchored = (char *) malloc(anchored_len + 1);
    text = (char *) malloc(value_len + 1);
    if (anchored == NULL || text == NULL) {
        free(anchored);
        free(text);
        return -1;
    }
    anchored[0] = '^';
    if (pattern_len > 0) {
        memcpy(anchored + 1, pattern, pattern_len);
    }
    anchored[anchored_len - 1] = '$';
    anchored[anchored_len] = '\0';
    memcpy(text, value, value_len);
    text[value_len] = '\0';

    compiled = re_compile(anchored);
    if (compiled == NULL) {
        free(anchored);
        free(text);
        return -1;
    }
    match_length = 0;
    match_index = re_matchp(compiled, text, &match_length);
    result = (match_index == 0 && match_length == (int) value_len) ? 1 : 0;
    free(anchored);
    free(text);
    return result;
}
