/*
 * Conservative modern CSS value compatibility for libcss 3.11.
 *
 * This is deliberately not a general CSS Values parser. It converts numeric
 * oklch() colours and calc() expressions that can be reduced without layout
 * context. Unsupported syntax is preserved byte-for-byte for libcss to reject
 * normally.
 *
 * C89 only.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcore_css_values.h"

#define PCORE_VALUE_DEPTH 16

typedef struct pcore_value_buffer {
    char *data;
    unsigned int len;
    unsigned int cap;
    unsigned int limit;
} pcore_value_buffer;

typedef struct pcore_calc_value {
    double number;
    char unit[8];
    unsigned int unit_len;
} pcore_calc_value;

typedef struct pcore_calc_parser {
    const char *text;
    unsigned int pos;
    unsigned int end;
} pcore_calc_parser;

static int pcore_value_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static int pcore_value_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static char pcore_value_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char) (c + ('a' - 'A'));
    }
    return c;
}

static int pcore_value_append(pcore_value_buffer *out, const char *data,
        unsigned int len)
{
    unsigned int need;
    unsigned int cap;
    char *grown;

    if (out->len > out->limit || len > out->limit - out->len) {
        return 0;
    }
    need = out->len + len + 1U;
    if (need > out->cap) {
        cap = out->cap;
        while (cap < need) {
            unsigned int next = cap + cap / 2U + 64U;
            if (next <= cap || next > out->limit + 1U) {
                cap = out->limit + 1U;
                break;
            }
            cap = next;
        }
        if (cap < need) {
            return 0;
        }
        grown = (char *) realloc(out->data, cap);
        if (grown == NULL) {
            return 0;
        }
        out->data = grown;
        out->cap = cap;
    }
    if (len != 0) {
        memcpy(out->data + out->len, data, len);
        out->len += len;
    }
    out->data[out->len] = '\0';
    return 1;
}

static int pcore_value_function(const char *css, unsigned int len,
        unsigned int pos, const char *word, unsigned int word_len,
        unsigned int *open)
{
    unsigned int i;
    unsigned int p;

    if (pos + word_len > len ||
            (pos != 0 && pcore_value_name_char(css[pos - 1]))) {
        return 0;
    }
    for (i = 0; i < word_len; i++) {
        if (pcore_value_lower(css[pos + i]) != word[i]) {
            return 0;
        }
    }
    if (pos + word_len < len &&
            pcore_value_name_char(css[pos + word_len])) {
        return 0;
    }
    p = pos + word_len;
    while (p < len && pcore_value_space(css[p])) {
        p++;
    }
    if (p >= len || css[p] != '(') {
        return 0;
    }
    *open = p;
    return 1;
}

static int pcore_value_function_close(const char *css, unsigned int len,
        unsigned int open, unsigned int *close)
{
    unsigned int i = open + 1U;
    unsigned int depth = 1U;
    int quote = 0;
    int comment = 0;

    while (i < len) {
        if (comment) {
            if (css[i] == '*' && i + 1U < len && css[i + 1U] == '/') {
                i += 2U;
                comment = 0;
            } else {
                i++;
            }
            continue;
        }
        if (quote != 0) {
            if (css[i] == '\\' && i + 1U < len) {
                i += 2U;
            } else {
                if (css[i] == quote) {
                    quote = 0;
                }
                i++;
            }
            continue;
        }
        if (css[i] == '/' && i + 1U < len && css[i + 1U] == '*') {
            comment = 1;
            i += 2U;
        } else if (css[i] == '\'' || css[i] == '"') {
            quote = css[i++];
        } else if (css[i] == '(') {
            depth++;
            i++;
        } else if (css[i] == ')') {
            depth--;
            if (depth == 0) {
                *close = i;
                return 1;
            }
            i++;
        } else {
            i++;
        }
    }
    return 0;
}

static void pcore_value_skip(const char *text, unsigned int end,
        unsigned int *pos)
{
    while (*pos < end && pcore_value_space(text[*pos])) {
        (*pos)++;
    }
}

static int pcore_value_number(const char *text, unsigned int end,
        unsigned int *pos, double *out)
{
    unsigned int p = *pos;
    double value = 0.0;
    double scale = 0.1;
    int digits = 0;

    while (p < end && text[p] >= '0' && text[p] <= '9') {
        value = value * 10.0 + (double) (text[p] - '0');
        p++;
        digits = 1;
    }
    if (p < end && text[p] == '.') {
        p++;
        while (p < end && text[p] >= '0' && text[p] <= '9') {
            value += (double) (text[p] - '0') * scale;
            scale *= 0.1;
            p++;
            digits = 1;
        }
    }
    if (!digits) {
        return 0;
    }
    *pos = p;
    *out = value;
    return 1;
}

static int pcore_value_signed_number(const char *text, unsigned int end,
        unsigned int *pos, double *out)
{
    int negative = 0;

    if (*pos < end && (text[*pos] == '+' || text[*pos] == '-')) {
        negative = text[*pos] == '-';
        (*pos)++;
    }
    if (!pcore_value_number(text, end, pos, out)) {
        return 0;
    }
    if (negative) {
        *out = -*out;
    }
    return 1;
}

static double pcore_value_clamp(double value, double low, double high)
{
    if (value < low) { return low; }
    if (value > high) { return high; }
    return value;
}

static double pcore_value_srgb(double linear)
{
    linear = pcore_value_clamp(linear, 0.0, 1.0);
    if (linear <= 0.0031308) {
        return 12.92 * linear;
    }
    return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

/* Oklab matrices by Bjorn Ottosson, public domain / MIT:
 * https://bottosson.github.io/posts/oklab/ */
static int pcore_value_oklch(const char *text, unsigned int start,
        unsigned int end, char *replacement, unsigned int replacement_size,
        unsigned int *replacement_len)
{
    unsigned int p = start;
    double lightness;
    double chroma;
    double hue;
    double alpha = 1.0;
    double radians;
    double lab_a;
    double lab_b;
    double l1;
    double m1;
    double s1;
    double l;
    double m;
    double s;
    double red;
    double green;
    double blue;
    int r;
    int g;
    int b;
    int a;
    int wrote;

    pcore_value_skip(text, end, &p);
    if (!pcore_value_signed_number(text, end, &p, &lightness)) {
        return 0;
    }
    if (p < end && text[p] == '%') {
        lightness /= 100.0;
        p++;
    }
    pcore_value_skip(text, end, &p);
    if (!pcore_value_signed_number(text, end, &p, &chroma)) {
        return 0;
    }
    if (p < end && text[p] == '%') {
        return 0;
    }
    pcore_value_skip(text, end, &p);
    if (!pcore_value_signed_number(text, end, &p, &hue)) {
        return 0;
    }
    if (p + 3U <= end && pcore_value_lower(text[p]) == 'd' &&
            pcore_value_lower(text[p + 1U]) == 'e' &&
            pcore_value_lower(text[p + 2U]) == 'g') {
        p += 3U;
    }
    pcore_value_skip(text, end, &p);
    if (p < end && text[p] == '/') {
        p++;
        pcore_value_skip(text, end, &p);
        if (!pcore_value_signed_number(text, end, &p, &alpha)) {
            return 0;
        }
        if (p < end && text[p] == '%') {
            alpha /= 100.0;
            p++;
        }
        pcore_value_skip(text, end, &p);
    }
    if (p != end || lightness < 0.0 || lightness > 1.0 ||
            chroma < 0.0 || chroma > 1.0 ||
            alpha < 0.0 || alpha > 1.0) {
        return 0;
    }

    radians = hue * 3.14159265358979323846 / 180.0;
    lab_a = chroma * cos(radians);
    lab_b = chroma * sin(radians);
    l1 = lightness + 0.3963377774 * lab_a + 0.2158037573 * lab_b;
    m1 = lightness - 0.1055613458 * lab_a - 0.0638541728 * lab_b;
    s1 = lightness - 0.0894841775 * lab_a - 1.2914855480 * lab_b;
    l = l1 * l1 * l1;
    m = m1 * m1 * m1;
    s = s1 * s1 * s1;
    red = pcore_value_srgb(4.0767416621 * l - 3.3077115913 * m +
            0.2309699292 * s);
    green = pcore_value_srgb(-1.2684380046 * l + 2.6097574011 * m -
            0.3413193965 * s);
    blue = pcore_value_srgb(-0.0041960863 * l - 0.7034186147 * m +
            1.7076147010 * s);
    r = (int) floor(red * 255.0 + 0.5);
    g = (int) floor(green * 255.0 + 0.5);
    b = (int) floor(blue * 255.0 + 0.5);
    a = (int) floor(alpha * 255.0 + 0.5);
    if (alpha >= 0.999999) {
        wrote = _snprintf(replacement, replacement_size - 1U,
                "#%02X%02X%02X", r, g, b);
    } else {
        /* libcss truncates 22:10 fixed alpha after multiplying by 255. */
        unsigned int fixed_alpha =
                ((unsigned int) a * 1024U + 254U) / 255U;
        wrote = _snprintf(replacement, replacement_size - 1U,
                "rgba(%d,%d,%d,%.6f)", r, g, b,
                (double) fixed_alpha / 1024.0);
    }
    replacement[replacement_size - 1U] = '\0';
    if (wrote <= 0 || (unsigned int) wrote >= replacement_size) {
        return 0;
    }
    *replacement_len = (unsigned int) wrote;
    return 1;
}

static void pcore_calc_skip(pcore_calc_parser *parser)
{
    pcore_value_skip(parser->text, parser->end, &parser->pos);
}

static int pcore_calc_same_unit(const pcore_calc_value *left,
        const pcore_calc_value *right)
{
    return left->unit_len == right->unit_len &&
            memcmp(left->unit, right->unit, left->unit_len) == 0;
}

static int pcore_calc_add(pcore_calc_value *left,
        const pcore_calc_value *right, int subtract)
{
    if (!pcore_calc_same_unit(left, right)) {
        if (left->unit_len == 0 && fabs(left->number) < 0.0000001) {
            memcpy(left->unit, right->unit, right->unit_len);
            left->unit_len = right->unit_len;
        } else if (right->unit_len != 0 || fabs(right->number) >= 0.0000001) {
            return 0;
        }
    }
    left->number += subtract ? -right->number : right->number;
    return 1;
}

static int pcore_calc_multiply(pcore_calc_value *left,
        const pcore_calc_value *right, int divide)
{
    if (divide) {
        if (right->unit_len != 0 || fabs(right->number) < 0.0000001) {
            return 0;
        }
        left->number /= right->number;
        return 1;
    }
    if (left->unit_len != 0 && right->unit_len != 0) {
        return 0;
    }
    if (left->unit_len == 0 && right->unit_len != 0) {
        memcpy(left->unit, right->unit, right->unit_len);
        left->unit_len = right->unit_len;
    }
    left->number *= right->number;
    return 1;
}

static int pcore_calc_expression(pcore_calc_parser *parser,
        pcore_calc_value *out, unsigned int depth);

static int pcore_calc_factor(pcore_calc_parser *parser,
        pcore_calc_value *out, unsigned int depth)
{
    int negative = 0;
    unsigned int unit_start;

    if (depth >= PCORE_VALUE_DEPTH) {
        return 0;
    }
    pcore_calc_skip(parser);
    if (parser->pos < parser->end &&
            (parser->text[parser->pos] == '+' ||
             parser->text[parser->pos] == '-')) {
        negative = parser->text[parser->pos] == '-';
        parser->pos++;
        if (!pcore_calc_factor(parser, out, depth + 1U)) {
            return 0;
        }
        if (negative) {
            out->number = -out->number;
        }
        return 1;
    }
    if (parser->pos < parser->end && parser->text[parser->pos] == '(') {
        parser->pos++;
        if (!pcore_calc_expression(parser, out, depth + 1U)) {
            return 0;
        }
        pcore_calc_skip(parser);
        if (parser->pos >= parser->end || parser->text[parser->pos] != ')') {
            return 0;
        }
        parser->pos++;
        return 1;
    }
    if (!pcore_value_number(parser->text, parser->end, &parser->pos,
            &out->number)) {
        return 0;
    }
    out->unit_len = 0;
    unit_start = parser->pos;
    if (parser->pos < parser->end && parser->text[parser->pos] == '%') {
        out->unit[out->unit_len++] = parser->text[parser->pos++];
    } else {
        while (parser->pos < parser->end &&
                ((parser->text[parser->pos] >= 'a' &&
                  parser->text[parser->pos] <= 'z') ||
                 (parser->text[parser->pos] >= 'A' &&
                  parser->text[parser->pos] <= 'Z'))) {
            if (out->unit_len + 1U >= sizeof(out->unit)) {
                return 0;
            }
            out->unit[out->unit_len++] =
                    pcore_value_lower(parser->text[parser->pos++]);
        }
    }
    if (parser->pos == unit_start) {
        out->unit_len = 0;
    }
    out->unit[out->unit_len] = '\0';
    return 1;
}

static int pcore_calc_term(pcore_calc_parser *parser,
        pcore_calc_value *out, unsigned int depth)
{
    pcore_calc_value right;

    if (!pcore_calc_factor(parser, out, depth)) {
        return 0;
    }
    for (;;) {
        char op;
        pcore_calc_skip(parser);
        if (parser->pos >= parser->end ||
                (parser->text[parser->pos] != '*' &&
                 parser->text[parser->pos] != '/')) {
            return 1;
        }
        op = parser->text[parser->pos++];
        if (!pcore_calc_factor(parser, &right, depth)) {
            return 0;
        }
        if (!pcore_calc_multiply(out, &right, op == '/')) {
            return 0;
        }
    }
}

static int pcore_calc_expression(pcore_calc_parser *parser,
        pcore_calc_value *out, unsigned int depth)
{
    pcore_calc_value right;

    if (!pcore_calc_term(parser, out, depth)) {
        return 0;
    }
    for (;;) {
        char op;
        pcore_calc_skip(parser);
        if (parser->pos >= parser->end ||
                (parser->text[parser->pos] != '+' &&
                 parser->text[parser->pos] != '-')) {
            return 1;
        }
        op = parser->text[parser->pos++];
        if (!pcore_calc_term(parser, &right, depth)) {
            return 0;
        }
        if (!pcore_calc_add(out, &right, op == '-')) {
            return 0;
        }
    }
}

static int pcore_value_calc(const char *text, unsigned int start,
        unsigned int end, char *replacement, unsigned int replacement_size,
        unsigned int *replacement_len)
{
    pcore_calc_parser parser;
    pcore_calc_value value;
    int wrote;
    unsigned int len;

    parser.text = text;
    parser.pos = start;
    parser.end = end;
    if (!pcore_calc_expression(&parser, &value, 0)) {
        return 0;
    }
    pcore_calc_skip(&parser);
    if (parser.pos != end || value.number != value.number ||
            value.number < -1000000000.0 || value.number > 1000000000.0) {
        return 0;
    }
    if (fabs(value.number) < 0.0000005) {
        value.number = 0.0;
    }
    wrote = _snprintf(replacement, replacement_size - 1U, "%.6f",
            value.number);
    replacement[replacement_size - 1U] = '\0';
    if (wrote <= 0 || (unsigned int) wrote >= replacement_size) {
        return 0;
    }
    len = (unsigned int) wrote;
    while (len > 0 && replacement[len - 1U] == '0') {
        len--;
    }
    if (len > 0 && replacement[len - 1U] == '.') {
        len--;
    }
    if (len == 0 || (len == 1 && replacement[0] == '-')) {
        replacement[0] = '0';
        len = 1;
    }
    if (len + value.unit_len + 1U > replacement_size) {
        return 0;
    }
    memcpy(replacement + len, value.unit, value.unit_len);
    len += value.unit_len;
    replacement[len] = '\0';
    *replacement_len = len;
    return 1;
}

char *pcore_css_compat_values(const char *css, unsigned int len,
        unsigned int *out_len)
{
    pcore_value_buffer out;
    unsigned long limit;
    unsigned int i = 0;
    unsigned int conversions = 0;
    int quote = 0;
    int comment = 0;

    if (css == NULL || out_len == NULL) {
        return NULL;
    }
    limit = (unsigned long) len * 2UL + 4096UL;
    if (limit > 0x7ffffffeUL) {
        return NULL;
    }
    out.cap = len + 64U;
    if (out.cap < len || out.cap > (unsigned int) limit + 1U) {
        out.cap = (unsigned int) limit + 1U;
    }
    out.data = (char *) malloc(out.cap);
    if (out.data == NULL) {
        return NULL;
    }
    out.len = 0;
    out.limit = (unsigned int) limit;
    out.data[0] = '\0';

    while (i < len) {
        if (comment) {
            unsigned int start = i;
            while (i < len) {
                if (css[i] == '*' && i + 1U < len && css[i + 1U] == '/') {
                    i += 2U;
                    comment = 0;
                    break;
                }
                i++;
            }
            if (!pcore_value_append(&out, css + start, i - start)) {
                free(out.data);
                return NULL;
            }
            continue;
        }
        if (quote != 0) {
            unsigned int start = i;
            while (i < len) {
                if (css[i] == '\\' && i + 1U < len) {
                    i += 2U;
                } else {
                    if (css[i] == quote) {
                        quote = 0;
                        i++;
                        break;
                    }
                    i++;
                }
            }
            if (!pcore_value_append(&out, css + start, i - start)) {
                free(out.data);
                return NULL;
            }
            continue;
        }
        if (css[i] == '/' && i + 1U < len && css[i + 1U] == '*') {
            comment = 1;
            continue;
        }
        if (css[i] == '\'' || css[i] == '"') {
            quote = css[i];
            if (!pcore_value_append(&out, css + i, 1)) {
                free(out.data);
                return NULL;
            }
            i++;
            continue;
        }
        {
            unsigned int open;
            unsigned int close;
            char replacement[96];
            unsigned int replacement_len;
            int converted = 0;

            if (pcore_value_function(css, len, i, "oklch", 5U, &open) &&
                    pcore_value_function_close(css, len, open, &close)) {
                converted = pcore_value_oklch(css, open + 1U, close,
                        replacement, sizeof(replacement), &replacement_len);
            } else if (pcore_value_function(css, len, i, "calc", 4U,
                    &open) &&
                    pcore_value_function_close(css, len, open, &close)) {
                converted = pcore_value_calc(css, open + 1U, close,
                        replacement, sizeof(replacement), &replacement_len);
            }
            if (converted) {
                if (!pcore_value_append(&out, replacement, replacement_len)) {
                    free(out.data);
                    return NULL;
                }
                i = close + 1U;
                conversions++;
                continue;
            }
        }
        if (!pcore_value_append(&out, css + i, 1)) {
            free(out.data);
            return NULL;
        }
        i++;
    }

    if (conversions == 0) {
        free(out.data);
        return NULL;
    }
    *out_len = out.len;
    return out.data;
}
