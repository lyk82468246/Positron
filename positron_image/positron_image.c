/*
 * positron_image.c - public image DLL boundary.
 */

#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <svgtiny.h>

#ifndef POSITRON_IMAGE_EXPORTS
#define POSITRON_IMAGE_EXPORTS
#endif
#include "positron_image.h"
#include "pimage_raster.h"

typedef struct pimage_svg {
    struct svgtiny_diagram *diagram;
    void *raster_image;
    PIMAGE_SVG_CREATE_STATS create_stats;
} pimage_svg;

/* libsvgtiny consumes presentation attributes and an element's inline
 * style, but not CSS class rules from an SVG <style> element.  Keep the
 * compatibility bridge deliberately small: it expands simple class rules
 * into inline style attributes before parsing, without pretending to be a
 * general CSS engine. */
#define PIMAGE_SVG_MAX_STYLE_BLOCKS 8
#define PIMAGE_SVG_CLASS_NAME_MAX 64
#define PIMAGE_SVG_DECLARATION_MAX 2048
#define PIMAGE_SVG_NORMALIZED_MAX 262144

typedef struct pimage_svg_style_block {
    const char *data;
    size_t len;
} pimage_svg_style_block;

typedef struct pimage_svg_tag_info {
    const char *class_data;
    size_t class_len;
    const char *style_value_start;
    const char *style_value_end;
    size_t insert_offset;
    int has_class;
    int has_style;
    char style_quote;
} pimage_svg_tag_info;

static int pimage_svg_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static int pimage_svg_is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
}

static int pimage_svg_ascii_equal(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char) (a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char) (b + ('a' - 'A'));
    }
    return a == b;
}

static int pimage_svg_word_equal(const char *data, size_t len,
        const char *word)
{
    size_t i;
    size_t word_len;

    word_len = strlen(word);
    if (len != word_len) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (!pimage_svg_ascii_equal(data[i], word[i])) {
            return 0;
        }
    }
    return 1;
}

static size_t pimage_svg_tag_end(const char *data, size_t len,
        size_t start)
{
    size_t i;
    char quote;

    quote = '\0';
    for (i = start; i < len; i++) {
        if (quote != '\0') {
            if (data[i] == quote) {
                quote = '\0';
            }
        } else if (data[i] == '\'' || data[i] == '"') {
            quote = data[i];
        } else if (data[i] == '>') {
            return i + 1;
        }
    }
    return len;
}

static int pimage_svg_tag_name(const char *data, size_t start, size_t end,
        const char **out_name, size_t *out_len, int *out_closing)
{
    size_t i;
    size_t name_start;
    int closing;

    if (data == NULL || start >= end || data[start] != '<') {
        return 0;
    }
    i = start + 1;
    closing = 0;
    if (i < end && data[i] == '/') {
        closing = 1;
        i++;
    }
    if (i >= end || data[i] == '!' || data[i] == '?') {
        return 0;
    }
    name_start = i;
    while (i < end && pimage_svg_is_name_char(data[i])) {
        i++;
    }
    if (i == name_start) {
        return 0;
    }
    if (out_name != NULL) {
        *out_name = data + name_start;
    }
    if (out_len != NULL) {
        *out_len = i - name_start;
    }
    if (out_closing != NULL) {
        *out_closing = closing;
    }
    return 1;
}

static size_t pimage_svg_find_closing_style(const char *data, size_t len,
        size_t start)
{
    size_t i;
    size_t end;
    const char *name;
    size_t name_len;
    int closing;

    for (i = start; i < len; i++) {
        if (data[i] != '<') {
            continue;
        }
        end = pimage_svg_tag_end(data, len, i);
        if (end == len && (len == 0 || data[len - 1] != '>')) {
            return len;
        }
        if (pimage_svg_tag_name(data, i, end, &name, &name_len,
                &closing) && closing && pimage_svg_word_equal(name,
                name_len, "style")) {
            return i;
        }
        if (end > i) {
            i = end - 1;
        }
    }
    return len;
}

static int pimage_svg_collect_style_blocks(const char *data, size_t len,
        pimage_svg_style_block *blocks, int *out_count)
{
    size_t i;
    size_t end;
    size_t close_start;
    size_t close_end;
    const char *name;
    size_t name_len;
    int closing;
    int count;

    if (out_count == NULL) {
        return -1;
    }
    *out_count = 0;
    if (data == NULL || blocks == NULL) {
        return -1;
    }
    count = 0;
    i = 0;
    while (i < len) {
        if (data[i] != '<') {
            i++;
            continue;
        }
        end = pimage_svg_tag_end(data, len, i);
        if (end == len && (len == 0 || data[len - 1] != '>')) {
            return -1;
        }
        if (pimage_svg_tag_name(data, i, end, &name, &name_len,
                &closing) && !closing && pimage_svg_word_equal(name,
                name_len, "style")) {
            close_start = pimage_svg_find_closing_style(data, len, end);
            if (close_start >= len) {
                return -1;
            }
            close_end = pimage_svg_tag_end(data, len, close_start);
            if (close_end == len && (len == 0 || data[len - 1] != '>')) {
                return -1;
            }
            if (count >= PIMAGE_SVG_MAX_STYLE_BLOCKS) {
                return -1;
            }
            blocks[count].data = data + end;
            blocks[count].len = close_start - end;
            count++;
            i = close_end;
            continue;
        }
        if (end > i) {
            i = end;
        } else {
            i++;
        }
    }
    *out_count = count;
    return 0;
}

static int pimage_svg_selector_has_class(const char *data, size_t len,
        const char *class_data, size_t class_len)
{
    size_t i;
    size_t j;
    char before;
    char after;

    if (class_len == 0 || class_len >= PIMAGE_SVG_CLASS_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (data[i] != '.') {
            continue;
        }
        before = (i == 0) ? ' ' : data[i - 1];
        if (!(pimage_svg_is_space(before) || before == ',' ||
                before == '>' || before == '+' || before == '~')) {
            continue;
        }
        j = i + 1;
        while (j < len && pimage_svg_is_name_char(data[j])) {
            j++;
        }
        if (j - i - 1 != class_len ||
                memcmp(data + i + 1, class_data, class_len) != 0) {
            continue;
        }
        after = (j < len) ? data[j] : ' ';
        if (pimage_svg_is_space(after) || after == ',' || after == '{') {
            return 1;
        }
    }
    return 0;
}

static int pimage_svg_append(char *out, size_t capacity, size_t *used,
        const char *data, size_t len)
{
    if (out == NULL || used == NULL || data == NULL ||
            *used > capacity || len > capacity - *used) {
        return 0;
    }
    memcpy(out + *used, data, len);
    *used += len;
    return 1;
}

static int pimage_svg_declaration_has_display_none(const char *data,
        size_t len)
{
    size_t i;
    size_t name_start;
    size_t name_len;
    size_t value_start;
    size_t value_end;

    if (data == NULL) {
        return 0;
    }
    i = 0;
    while (i < len) {
        while (i < len && (pimage_svg_is_space(data[i]) ||
                data[i] == ';')) {
            i++;
        }
        if (i >= len) {
            break;
        }
        name_start = i;
        while (i < len && pimage_svg_is_name_char(data[i]) &&
                data[i] != ':') {
            i++;
        }
        name_len = i - name_start;
        while (i < len && pimage_svg_is_space(data[i])) {
            i++;
        }
        if (i >= len || data[i] != ':') {
            while (i < len && data[i] != ';') {
                i++;
            }
            continue;
        }
        i++;
        while (i < len && pimage_svg_is_space(data[i])) {
            i++;
        }
        value_start = i;
        while (i < len && !pimage_svg_is_space(data[i]) &&
                data[i] != ';') {
            i++;
        }
        value_end = i;
        if (pimage_svg_word_equal(data + name_start, name_len, "display") &&
                pimage_svg_word_equal(data + value_start,
                value_end - value_start, "none")) {
            return 1;
        }
        while (i < len && data[i] != ';') {
            i++;
        }
    }
    return 0;
}

/* Return the end of the XML element beginning at start. This is deliberately
 * only a bounded structural scan used to remove a simple display:none
 * subtree; libdom remains the authoritative XML parser. */
static size_t pimage_svg_element_end(const char *data, size_t len,
        size_t start, size_t open_end)
{
    size_t cursor;
    size_t end;
    size_t insert;
    const char *name;
    size_t name_len;
    int closing;
    int depth;
    int self_closing;

    (void) name;
    (void) name_len;
    if (data == NULL || start >= len || open_end <= start ||
            open_end > len) {
        return len;
    }
    insert = open_end - 1;
    while (insert > start && pimage_svg_is_space(data[insert - 1])) {
        insert--;
    }
    if (insert > start && data[insert - 1] == '/') {
        return open_end;
    }
    depth = 1;
    cursor = open_end;
    while (cursor < len) {
        if (data[cursor] != '<') {
            cursor++;
            continue;
        }
        end = pimage_svg_tag_end(data, len, cursor);
        if (end == len && data[len - 1] != '>') {
            return len;
        }
        if (pimage_svg_tag_name(data, cursor, end, &name, &name_len,
                &closing)) {
            self_closing = 0;
            insert = end - 1;
            while (insert > cursor && pimage_svg_is_space(data[insert - 1])) {
                insert--;
            }
            if (insert > cursor && data[insert - 1] == '/') {
                self_closing = 1;
            }
            if (closing) {
                depth--;
            } else if (!self_closing) {
                depth++;
            }
            if (depth == 0) {
                return end;
            }
        }
        cursor = end;
    }
    return len;
}

static int pimage_svg_collect_class_declaration(
        const pimage_svg_style_block *blocks, int block_count,
        const char *class_data, size_t class_len, char *out,
        size_t capacity, size_t *out_len)
{
    size_t block_index;
    size_t i;
    size_t brace;
    size_t close;
    size_t selector_start;
    size_t selector_len;
    size_t declaration_start;
    size_t declaration_end;
    size_t trimmed_end;
    int found;

    if (out == NULL || out_len == NULL || blocks == NULL ||
            class_data == NULL) {
        return -1;
    }
    found = 0;
    for (block_index = 0; block_index < (size_t) block_count;
            block_index++) {
        i = 0;
        while (i < blocks[block_index].len) {
            while (i < blocks[block_index].len &&
                    pimage_svg_is_space(blocks[block_index].data[i])) {
                i++;
            }
            if (i >= blocks[block_index].len) {
                break;
            }
            brace = i;
            while (brace < blocks[block_index].len &&
                    blocks[block_index].data[brace] != '{') {
                if (blocks[block_index].data[brace] == '/' &&
                        brace + 1 < blocks[block_index].len &&
                        blocks[block_index].data[brace + 1] == '*') {
                    close = brace + 2;
                    while (close + 1 < blocks[block_index].len &&
                            !(blocks[block_index].data[close] == '*' &&
                            blocks[block_index].data[close + 1] == '/')) {
                        close++;
                    }
                    if (close + 1 >= blocks[block_index].len) {
                        return -1;
                    }
                    brace = close + 2;
                    continue;
                }
                brace++;
            }
            if (brace >= blocks[block_index].len) {
                break;
            }
            selector_start = i;
            selector_len = brace - selector_start;
            close = brace + 1;
            while (close < blocks[block_index].len &&
                    blocks[block_index].data[close] != '}') {
                close++;
            }
            if (close >= blocks[block_index].len) {
                return -1;
            }
            if (pimage_svg_selector_has_class(
                    blocks[block_index].data + selector_start,
                    selector_len, class_data, class_len)) {
                declaration_start = brace + 1;
                declaration_end = close;
                while (declaration_start < declaration_end &&
                        pimage_svg_is_space(blocks[block_index].data[
                        declaration_start])) {
                    declaration_start++;
                }
                trimmed_end = declaration_end;
                while (trimmed_end > declaration_start &&
                        pimage_svg_is_space(blocks[block_index].data[
                        trimmed_end - 1])) {
                    trimmed_end--;
                }
                if (trimmed_end > declaration_start &&
                        *out_len > 0 && !pimage_svg_append(out, capacity,
                        out_len, ";", 1)) {
                    return -1;
                }
                if (trimmed_end > declaration_start &&
                        !pimage_svg_append(out, capacity, out_len,
                        blocks[block_index].data + declaration_start,
                        trimmed_end - declaration_start)) {
                    return -1;
                }
                found = 1;
            }
            i = close + 1;
        }
    }
    if (*out_len >= capacity) {
        return -1;
    }
    out[*out_len] = '\0';
    return found;
}

static int pimage_svg_scan_tag(const char *data, size_t start, size_t end,
        pimage_svg_tag_info *info)
{
    size_t i;
    size_t name_start;
    size_t name_len;
    size_t value_start;
    size_t value_end;
    size_t insert;
    char quote;

    if (data == NULL || info == NULL || start >= end || data[start] != '<') {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    insert = end - 1;
    while (insert > start && pimage_svg_is_space(data[insert - 1])) {
        insert--;
    }
    if (insert > start && data[insert - 1] == '/') {
        insert--;
    }
    info->insert_offset = insert;
    i = start + 1;
    if (i < end && data[i] == '/') {
        return 1;
    }
    while (i < end && !pimage_svg_is_space(data[i]) &&
            data[i] != '>' && data[i] != '/') {
        i++;
    }
    while (i < end) {
        while (i < end && (pimage_svg_is_space(data[i]) ||
                data[i] == '/')) {
            i++;
        }
        if (i >= end || data[i] == '>') {
            break;
        }
        name_start = i;
        while (i < end && pimage_svg_is_name_char(data[i])) {
            i++;
        }
        name_len = i - name_start;
        while (i < end && pimage_svg_is_space(data[i])) {
            i++;
        }
        if (i >= end || data[i] != '=') {
            while (i < end && !pimage_svg_is_space(data[i]) &&
                    data[i] != '>') {
                i++;
            }
            continue;
        }
        i++;
        while (i < end && pimage_svg_is_space(data[i])) {
            i++;
        }
        value_start = i;
        quote = '\0';
        if (i < end && (data[i] == '\'' || data[i] == '"')) {
            quote = data[i];
            value_start = ++i;
            while (i < end && data[i] != quote) {
                i++;
            }
            value_end = i;
            if (i < end) {
                i++;
            }
        } else {
            while (i < end && !pimage_svg_is_space(data[i]) &&
                    data[i] != '>') {
                i++;
            }
            value_end = i;
        }
        if (pimage_svg_word_equal(data + name_start, name_len, "class")) {
            info->class_data = data + value_start;
            info->class_len = value_end - value_start;
            info->has_class = 1;
        } else if (pimage_svg_word_equal(data + name_start, name_len,
                "style")) {
            info->style_value_start = data + value_start;
            info->style_value_end = data + value_end;
            info->style_quote = quote;
            info->has_style = 1;
        }
    }
    return 1;
}

static int pimage_svg_normalize_class_styles(const char *data, int len,
        char **out_data)
{
    pimage_svg_style_block blocks[PIMAGE_SVG_MAX_STYLE_BLOCKS];
    pimage_svg_tag_info tag;
    char declaration[PIMAGE_SVG_DECLARATION_MAX];
    char class_name[PIMAGE_SVG_CLASS_NAME_MAX];
    char *out;
    size_t capacity;
    size_t i;
    size_t end;
    size_t close_start;
    size_t close_end;
    size_t out_len;
    size_t class_i;
    size_t class_start;
    size_t class_len;
    size_t declaration_len;
    const char *name;
    size_t name_len;
    int closing;
    int block_count;
    int class_found;
    int result;
    int changed;

    if (out_data == NULL) {
        return -1;
    }
    *out_data = NULL;
    if (data == NULL || len <= 0) {
        return 0;
    }
    if (pimage_svg_collect_style_blocks(data, (size_t) len, blocks,
            &block_count) != 0) {
        return -1;
    }
    if (block_count == 0) {
        return 0;
    }
    if ((size_t) len >= PIMAGE_SVG_NORMALIZED_MAX - 1) {
        return -1;
    }
    capacity = (size_t) len + ((size_t) len / 2) + 256;
    if (capacity > PIMAGE_SVG_NORMALIZED_MAX) {
        capacity = PIMAGE_SVG_NORMALIZED_MAX;
    }
    out = (char *) malloc(capacity);
    if (out == NULL) {
        return -1;
    }
    out_len = 0;
    i = 0;
    changed = 0;
    while (i < (size_t) len) {
        if (data[i] != '<') {
            if (!pimage_svg_append(out, capacity, &out_len, data + i, 1)) {
                free(out);
                return -1;
            }
            i++;
            continue;
        }
        end = pimage_svg_tag_end(data, (size_t) len, i);
        if (end == (size_t) len && data[len - 1] != '>') {
            free(out);
            return -1;
        }
        if (pimage_svg_tag_name(data, i, end, &name, &name_len,
                &closing) && !closing && pimage_svg_word_equal(name,
                name_len, "style")) {
            close_start = pimage_svg_find_closing_style(data, (size_t) len,
                    end);
            if (close_start >= (size_t) len) {
                free(out);
                return -1;
            }
            close_end = pimage_svg_tag_end(data, (size_t) len, close_start);
            if (close_end == (size_t) len && data[len - 1] != '>') {
                free(out);
                return -1;
            }
            if (!pimage_svg_append(out, capacity, &out_len, data + i,
                    close_end - i)) {
                free(out);
                return -1;
            }
            i = close_end;
            continue;
        }
        if (!pimage_svg_scan_tag(data, i, end, &tag) || !tag.has_class) {
            if (!pimage_svg_append(out, capacity, &out_len, data + i,
                    end - i)) {
                free(out);
                return -1;
            }
            i = end;
            continue;
        }
        declaration[0] = '\0';
        declaration_len = 0;
        class_found = 0;
        class_i = 0;
        while (class_i < tag.class_len) {
            while (class_i < tag.class_len &&
                    pimage_svg_is_space(tag.class_data[class_i])) {
                class_i++;
            }
            class_start = class_i;
            while (class_i < tag.class_len &&
                    !pimage_svg_is_space(tag.class_data[class_i])) {
                class_i++;
            }
            class_len = class_i - class_start;
            if (class_len == 0) {
                continue;
            }
            if (class_len >= sizeof(class_name)) {
                free(out);
                return -1;
            }
            memcpy(class_name, tag.class_data + class_start, class_len);
            class_name[class_len] = '\0';
            result = pimage_svg_collect_class_declaration(blocks,
                    block_count, class_name, class_len, declaration,
                    sizeof(declaration), &declaration_len);
            if (result < 0) {
                free(out);
                return -1;
            }
            if (result > 0) {
                class_found = 1;
            }
        }
        if (!class_found) {
            if (!pimage_svg_append(out, capacity, &out_len, data + i,
                    end - i)) {
                free(out);
                return -1;
            }
            i = end;
            continue;
        }
        if (!tag.has_style && pimage_svg_declaration_has_display_none(
                declaration, declaration_len)) {
            end = pimage_svg_element_end(data, (size_t) len, i, end);
            if (end <= i || end > (size_t) len) {
                free(out);
                return -1;
            }
            changed = 1;
            i = end;
            continue;
        }
        if (tag.has_style) {
            if (tag.style_quote == '\0') {
                free(out);
                return -1;
            }
            if (!pimage_svg_append(out, capacity, &out_len, data + i,
                    (size_t) (tag.style_value_end - data - i)) ||
                    !pimage_svg_append(out, capacity, &out_len, ";",
                    1) || !pimage_svg_append(out, capacity, &out_len,
                    declaration, declaration_len) ||
                    !pimage_svg_append(out, capacity, &out_len,
                    tag.style_value_end, end -
                    (size_t) (tag.style_value_end - data))) {
                free(out);
                return -1;
            }
        } else {
            if (!pimage_svg_append(out, capacity, &out_len, data + i,
                    tag.insert_offset - i) ||
                    !pimage_svg_append(out, capacity, &out_len,
                    " style=\"", 8) ||
                    !pimage_svg_append(out, capacity, &out_len,
                    declaration, declaration_len) ||
                    !pimage_svg_append(out, capacity, &out_len, "\"", 1) ||
                    !pimage_svg_append(out, capacity, &out_len,
                    data + tag.insert_offset, end - tag.insert_offset)) {
                free(out);
                return -1;
            }
        }
        changed = 1;
        i = end;
    }
    if (!changed) {
        free(out);
        return 0;
    }
    if (out_len >= capacity) {
        free(out);
        return -1;
    }
    out[out_len] = '\0';
    *out_data = out;
    return 1;
}

/* libsvgtiny uses the caller's viewport height when an SVG omits an explicit
 * height.  That changes the intrinsic aspect ratio of the common viewBox-only
 * form: the old 300x150 default turns IANA's 450x175 and negative-viewBox
 * assets into a 2:1 canvas before Core can size a CSS background.  Read only
 * the bounded root viewBox attribute and derive the fallback height from its
 * ratio.  This is not a second XML parser: malformed, unquoted, over-budget
 * or non-positive values simply retain the historical fallback and libsvgtiny
 * remains authoritative for the actual SVG parse. */
#define PIMAGE_SVG_VIEWBOX_SCAN_MAX 8192
#define PIMAGE_SVG_VIEWBOX_VALUE_MAX 1000000.0

static int pimage_svg_parse_viewbox_number(const char *data, size_t len,
        size_t *cursor, float *out_value)
{
    char token[64];
    size_t start;
    size_t count;
    char *end;
    double value;

    if (data == NULL || cursor == NULL || out_value == NULL) {
        return 0;
    }
    while (*cursor < len && (pimage_svg_is_space(data[*cursor]) ||
            data[*cursor] == ',')) {
        (*cursor)++;
    }
    start = *cursor;
    while (*cursor < len && (data[*cursor] == '+' ||
            data[*cursor] == '-' || data[*cursor] == '.' ||
            (data[*cursor] >= '0' && data[*cursor] <= '9') ||
            data[*cursor] == 'e' || data[*cursor] == 'E')) {
        (*cursor)++;
    }
    count = *cursor - start;
    if (count == 0 || count >= sizeof(token)) {
        return 0;
    }
    memcpy(token, data + start, count);
    token[count] = '\0';
    value = strtod(token, &end);
    if (end != token + count || value != value ||
            value < -PIMAGE_SVG_VIEWBOX_VALUE_MAX ||
            value > PIMAGE_SVG_VIEWBOX_VALUE_MAX) {
        return 0;
    }
    *out_value = (float) value;
    return 1;
}

static int pimage_svg_viewbox_ratio(const char *data, size_t len,
        float *out_width, float *out_height)
{
    size_t scan_len;
    size_t i;
    size_t end;
    size_t cursor;
    size_t name_start;
    size_t name_len;
    size_t value_start;
    size_t value_end;
    char quote;
    const char *name;
    int closing;
    float min_x;
    float min_y;
    float view_width;
    float view_height;

    if (out_width != NULL) {
        *out_width = 0.0f;
    }
    if (out_height != NULL) {
        *out_height = 0.0f;
    }
    if (data == NULL || len == 0 || out_width == NULL ||
            out_height == NULL) {
        return 0;
    }
    scan_len = len;
    if (scan_len > PIMAGE_SVG_VIEWBOX_SCAN_MAX) {
        scan_len = PIMAGE_SVG_VIEWBOX_SCAN_MAX;
    }
    i = 0;
    while (i < scan_len) {
        if (data[i] != '<') {
            i++;
            continue;
        }
        end = pimage_svg_tag_end(data, scan_len, i);
        if (end <= i || end > scan_len) {
            return 0;
        }
        if (!pimage_svg_tag_name(data, i, end, &name, &name_len,
                &closing) || closing || !pimage_svg_word_equal(name,
                name_len, "svg")) {
            i = end;
            continue;
        }
        cursor = (size_t) (name - data) + name_len;
        while (cursor < end - 1) {
            while (cursor < end - 1 && (pimage_svg_is_space(data[cursor]) ||
                    data[cursor] == '/')) {
                cursor++;
            }
            if (cursor >= end - 1) {
                break;
            }
            name_start = cursor;
            while (cursor < end - 1 && pimage_svg_is_name_char(
                    data[cursor])) {
                cursor++;
            }
            name_len = cursor - name_start;
            while (cursor < end - 1 && pimage_svg_is_space(data[cursor])) {
                cursor++;
            }
            if (name_len == 0 || cursor >= end - 1 || data[cursor] != '=') {
                while (cursor < end - 1 && !pimage_svg_is_space(data[cursor])) {
                    cursor++;
                }
                continue;
            }
            cursor++;
            while (cursor < end - 1 && pimage_svg_is_space(data[cursor])) {
                cursor++;
            }
            if (cursor >= end - 1 || (data[cursor] != '\'' &&
                    data[cursor] != '"')) {
                return 0;
            }
            quote = data[cursor++];
            value_start = cursor;
            while (cursor < end - 1 && data[cursor] != quote) {
                cursor++;
            }
            if (cursor >= end - 1) {
                return 0;
            }
            value_end = cursor;
            cursor++;
            if (name_len != 7 || memcmp(data + name_start, "viewBox", 7) !=
                    0) {
                continue;
            }
            cursor = value_start;
            if (!pimage_svg_parse_viewbox_number(data, value_end, &cursor,
                    &min_x) || !pimage_svg_parse_viewbox_number(data,
                    value_end, &cursor, &min_y) ||
                    !pimage_svg_parse_viewbox_number(data, value_end,
                    &cursor, &view_width) ||
                    !pimage_svg_parse_viewbox_number(data, value_end,
                    &cursor, &view_height) || view_width <= 0.0f ||
                    view_height <= 0.0f) {
                return 0;
            }
            (void) min_x;
            (void) min_y;
            *out_width = view_width;
            *out_height = view_height;
            return 1;
        }
        return 0;
    }
    return 0;
}

BOOL WINAPI DllMain(HANDLE instance, DWORD reason, LPVOID reserved)
{
    (void) instance;
    (void) reason;
    (void) reserved;
    return TRUE;
}

PIMAGE_API unsigned long PImage_GetAbiVersion(void)
{
    return PIMAGE_ABI_VERSION;
}

PIMAGE_API int PImage_CreateSvgFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, PIMAGE_SVG *out_svg)
{
    pimage_svg *svg;
    svgtiny_code code;
    char *normalized_data;
    const char *parse_data;
    size_t parse_len;
    DWORD started;
    DWORD phase_started;
    int normalize_result;
    float viewbox_width;
    float viewbox_height;

    if (out_svg == NULL) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    *out_svg = NULL;
    if (data == NULL || len <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (viewport_w <= 0) {
        viewport_w = 300;
    }
    if (viewport_h <= 0) {
        viewport_h = 150;
        if (pimage_svg_viewbox_ratio(data, (size_t) len, &viewbox_width,
                &viewbox_height)) {
            viewbox_height = (float) viewport_w * viewbox_height /
                    viewbox_width;
            if (viewbox_height >= 1.0f && viewbox_height <= 1000000.0f) {
                viewport_h = (int) (viewbox_height + 0.5f);
            }
        }
    }
    normalized_data = NULL;
    normalize_result = pimage_svg_normalize_class_styles(data, (size_t) len,
            &normalized_data);
    if (normalize_result < 0) {
        return PIMAGE_ERROR_MEMORY;
    }
    parse_data = (normalized_data != NULL) ? normalized_data : data;
    parse_len = (normalized_data != NULL) ? strlen(normalized_data) :
            (size_t) len;
    started = GetTickCount();
    phase_started = started;
    svg = (pimage_svg *) calloc(1, sizeof(*svg));
    if (svg == NULL) {
        free(normalized_data);
        return PIMAGE_ERROR_MEMORY;
    }
    svg->diagram = svgtiny_create();
    if (svg->diagram == NULL) {
        free(normalized_data);
        free(svg);
        return PIMAGE_ERROR_MEMORY;
    }
    svg->create_stats.setup_ms = GetTickCount() - phase_started;
    phase_started = GetTickCount();
    code = svgtiny_parse(svg->diagram, parse_data, parse_len,
            "positron:memory.svg", viewport_w, viewport_h);
    free(normalized_data);
    svg->create_stats.parse_ms = GetTickCount() - phase_started;
    if (code != svgtiny_OK) {
        svgtiny_free(svg->diagram);
        free(svg);
        return PIMAGE_ERROR_SVG_BASE + (int) code;
    }
    phase_started = GetTickCount();
    svg->raster_image = pimage_raster_create(svg->diagram);
    svg->create_stats.raster_ms = GetTickCount() - phase_started;
    if (svg->raster_image == NULL) {
        svgtiny_free(svg->diagram);
        free(svg);
        return PIMAGE_ERROR_MEMORY;
    }
    svg->create_stats.total_ms = GetTickCount() - started;
    *out_svg = (PIMAGE_SVG) svg;
    return PIMAGE_OK;
}

PIMAGE_API int PImage_SvgGetInfo(PIMAGE_SVG handle, int *out_w, int *out_h,
        unsigned int *out_shape_count)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = 0;
    }
    if (svg == NULL || svg->diagram == NULL) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (out_w != NULL) {
        *out_w = svg->diagram->width;
    }
    if (out_h != NULL) {
        *out_h = svg->diagram->height;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = svg->diagram->shape_count;
    }
    return PIMAGE_OK;
}

PIMAGE_API int PImage_SvgGetCreateStats(PIMAGE_SVG handle,
        PIMAGE_SVG_CREATE_STATS *out_stats)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (out_stats == NULL) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    memset(out_stats, 0, sizeof(*out_stats));
    if (svg == NULL || svg->diagram == NULL) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    *out_stats = svg->create_stats;
    return PIMAGE_OK;
}

PIMAGE_API int PImage_DrawSvg(PIMAGE_SVG handle, HDC hdc,
        int x, int y, int width, int height)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (svg == NULL || svg->diagram == NULL ||
            svg->raster_image == NULL || hdc == NULL ||
            svg->diagram->width <= 0 || svg->diagram->height <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    if (width <= 0) {
        width = svg->diagram->width;
    }
    if (height <= 0) {
        height = svg->diagram->height;
    }
    if (width <= 0 || height <= 0) {
        return PIMAGE_ERROR_ARGUMENT;
    }
    return pimage_raster_draw(svg->raster_image, hdc,
            x, y, width, height) ? PIMAGE_OK : PIMAGE_ERROR_DRAW;
}

PIMAGE_API void PImage_FreeSvg(PIMAGE_SVG handle)
{
    pimage_svg *svg = (pimage_svg *) handle;

    if (svg != NULL) {
        pimage_raster_free(svg->raster_image);
        svgtiny_free(svg->diagram);
        free(svg);
    }
}

PIMAGE_API int PImage_SvgInfoFromMemory(const char *data, int len,
        int viewport_w, int viewport_h, int *out_w, int *out_h,
        unsigned int *out_shape_count)
{
    PIMAGE_SVG svg;
    int result;

    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (out_shape_count != NULL) {
        *out_shape_count = 0;
    }
    result = PImage_CreateSvgFromMemory(data, len, viewport_w, viewport_h,
            &svg);
    if (result != PIMAGE_OK) {
        return result;
    }
    result = PImage_SvgGetInfo(svg, out_w, out_h, out_shape_count);
    PImage_FreeSvg(svg);
    return result;
}
