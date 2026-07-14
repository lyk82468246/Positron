/*
 * positron_core.c - implementation of the Positron rendering core DLL.
 *
 * Wraps the ported NetSurf engine behind opaque HANDLEs:
 *   PCore_ParseHTML -> dom_hubbub binding (hubbub -> libdom DOM tree)
 *   PCore_ParseCSS  -> libcss stylesheet parser
 *
 * The four NetSurf static libs are linked into this DLL by the project
 * (positron_hubbub / positron_netsurf / positron_libcss / positron_libdom);
 * the CRT shims the engine needs (bsearch/abort/strdup/strncasecmp/snprintf/
 * time) come from positron_netsurf.lib. Only the PCore_* functions below are
 * exported - every NetSurf symbol stays internal to the DLL.
 *
 * The parse logic here is the same end-to-end sequence proven on real ARM
 * hardware by test_host's TEST 7 / 7b, minus the MessageBox reporting.
 *
 * C89 only.
 */

#include <windows.h>
#include <limits.h>   /* ULONG_MAX */
#include <stdlib.h>   /* malloc / free */
#include <string.h>    /* strlen */

#include <libcss/libcss.h>
#include <libwapcaplet/libwapcaplet.h>

/* dom_hubbub binding (HTML -> libdom DOM). Included by relative path: the
 * binding header has no clean public name, and its sibling "errors.h"
 * resolves via the header's own directory. Pulls <dom/dom.h> too. */
#include "../netsurf-all-3.11/libdom/bindings/hubbub/parser.h"

#include "positron_core.h"
#include "pcore_internal.h"
#include "pcore_css_values.h"

/* ------------------------------------------------------------------ */
/* DllMain                                                            */
/* ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
    (void) hModule;
    (void) lpReserved;
    (void) reason;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

PCORE_API int PCore_Init(void)
{
    /* libcss, libdom and libwapcaplet all initialise lazily, so there is
     * nothing to do yet. Kept as the lifecycle hook a future layout/render
     * stage (font setup, default stylesheet, etc.) will hang off. */
    return 0;
}

PCORE_API void PCore_Shutdown(void)
{
    /* No global state to release yet. */
}

/* ------------------------------------------------------------------ */
/* HTML -> DOM                                                         */
/* ------------------------------------------------------------------ */

/* Swallow libdom/hubbub diagnostics (no stdout on WinCE anyway). */
static void pcore_dom_msg(uint32_t severity, void *ctx, const char *msg, ...)
{
    (void) severity;
    (void) ctx;
    (void) msg;
}

PCORE_API HANDLE PCore_ParseHTML(const char *html, unsigned int len)
{
    dom_hubbub_parser_params params;
    dom_hubbub_parser       *parser = NULL;
    dom_document            *doc = NULL;
    dom_hubbub_error         derr;

    if (html == NULL) {
        return NULL;
    }
    if (len == 0) {
        len = (unsigned int) strlen(html);
    }

    memset(&params, 0, sizeof(params));
    params.enc           = "UTF-8";
    params.fix_enc       = true;
    params.enable_script = false;
    params.script        = NULL;
    params.msg           = pcore_dom_msg;
    params.ctx           = NULL;
    params.daf           = NULL;

    derr = dom_hubbub_parser_create(&params, &parser, &doc);
    if (derr != DOM_HUBBUB_OK || parser == NULL || doc == NULL) {
        if (parser != NULL) {
            dom_hubbub_parser_destroy(parser);
        }
        return NULL;
    }

    derr = dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) html, len);
    if (derr != DOM_HUBBUB_OK) {
        dom_hubbub_parser_destroy(parser);
        dom_node_unref((dom_node *) doc);
        return NULL;
    }

    derr = dom_hubbub_parser_completed(parser);
    if (derr != DOM_HUBBUB_OK) {
        dom_hubbub_parser_destroy(parser);
        dom_node_unref((dom_node *) doc);
        return NULL;
    }

    /* Parser finished; the document is now owned by the caller. */
    dom_hubbub_parser_destroy(parser);
    return (HANDLE) doc;
}

PCORE_API void PCore_FreeDocument(HANDLE hDoc)
{
    if (hDoc == NULL) {
        return;
    }
    dom_node_unref((dom_node *) hDoc);
}

/* ------------------------------------------------------------------ */
/* CSS                                                                 */
/* ------------------------------------------------------------------ */

typedef struct pcore_css_resolve_ctx {
    PCoreResolveUrlFn resolve;
    void *pw;
} pcore_css_resolve_ctx;

static css_error pcore_css_resolve(void *pw, const char *base,
                                   lwc_string *rel, lwc_string **abs)
{
    pcore_css_resolve_ctx *ctx;
    size_t rel_len;
    char reference[2048];
    char resolved[2048];

    ctx = (pcore_css_resolve_ctx *) pw;
    if (ctx != NULL && ctx->resolve != NULL && base != NULL) {
        rel_len = lwc_string_length(rel);
        if (rel_len < sizeof(reference)) {
            memcpy(reference, lwc_string_data(rel), rel_len);
            reference[rel_len] = '\0';
            resolved[0] = '\0';
            if (ctx->resolve(ctx->pw, base, reference, resolved,
                    (int) sizeof(resolved)) == 0 && resolved[0] != '\0' &&
                    lwc_intern_string(resolved, strlen(resolved), abs) ==
                            lwc_error_ok) {
                return CSS_OK;
            }
        }
    }
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static int pcore_css_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static int pcore_css_width_word(const char *css, unsigned int len,
                                unsigned int pos)
{
    static const char word[] = "width";
    unsigned int i;

    if (pos + 5 > len) {
        return 0;
    }
    for (i = 0; i < 5; i++) {
        char c = css[pos + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char) (c + ('a' - 'A'));
        }
        if (c != word[i]) {
            return 0;
        }
    }
    return 1;
}

static unsigned int pcore_css_ulong_decimal(char *out, unsigned long value)
{
    char reverse[32];
    unsigned int count = 0;
    unsigned int i;

    do {
        reverse[count++] = (char) ('0' + value % 10UL);
        value /= 10UL;
    } while (value != 0 && count < sizeof(reverse));
    for (i = 0; i < count; i++) {
        out[i] = reverse[count - i - 1];
    }
    return count;
}

/* libcss 3.11 predates Media Queries level 4 range syntax. Convert only the
 * integer-pixel forms used by the current IANA stylesheet. Other ranges stay
 * untouched so an uncertain compatibility guess cannot change semantics. */
static char *pcore_css_compat_ranges(const char *css, unsigned int len,
                                     unsigned int *out_len)
{
    char *out;
    unsigned int i = 0;
    unsigned int o = 0;
    int quote = 0;
    int comment = 0;

    if (len > 0x7fffffffU / 2U) {
        return NULL;
    }
    out = (char *) malloc((size_t) len * 2U + 1U);
    if (out == NULL) {
        return NULL;
    }

    while (i < len) {
        if (comment) {
            out[o++] = css[i];
            if (css[i] == '*' && i + 1 < len && css[i + 1] == '/') {
                out[o++] = css[++i];
                comment = 0;
            }
            i++;
            continue;
        }
        if (quote != 0) {
            out[o++] = css[i];
            if (css[i] == '\\' && i + 1 < len) {
                out[o++] = css[++i];
            } else if (css[i] == quote) {
                quote = 0;
            }
            i++;
            continue;
        }
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            out[o++] = css[i++];
            out[o++] = css[i++];
            comment = 1;
            continue;
        }
        if (css[i] == '\'' || css[i] == '"') {
            quote = css[i];
            out[o++] = css[i++];
            continue;
        }
        if (css[i] == '(') {
            unsigned int p = i + 1;
            unsigned int number_start;
            unsigned int number_end;
            unsigned long value = 0;
            int inclusive = 0;
            int overflow = 0;

            while (p < len && pcore_css_space(css[p])) { p++; }
            if (pcore_css_width_word(css, len, p)) {
                p += 5;
                while (p < len && pcore_css_space(css[p])) { p++; }
                if (p < len && css[p] == '<') {
                    p++;
                    if (p < len && css[p] == '=') {
                        inclusive = 1;
                        p++;
                    }
                    while (p < len && pcore_css_space(css[p])) { p++; }
                    number_start = p;
                    while (p < len && css[p] >= '0' && css[p] <= '9') {
                        unsigned long digit =
                                (unsigned long) (css[p] - '0');
                        if (value > (ULONG_MAX - digit) / 10UL) {
                            overflow = 1;
                        } else if (!overflow) {
                            value = value * 10UL + digit;
                        }
                        p++;
                    }
                    number_end = p;
                    while (p < len && pcore_css_space(css[p])) { p++; }
                    if (number_end > number_start && p + 2 <= len &&
                            (css[p] == 'p' || css[p] == 'P') &&
                            (css[p + 1] == 'x' || css[p + 1] == 'X')) {
                        p += 2;
                        while (p < len && pcore_css_space(css[p])) { p++; }
                        if (p < len && css[p] == ')' &&
                                (inclusive || (!overflow && value > 0))) {
                            char number[32];
                            unsigned int number_len;
                            static const char prefix[] = "(max-width:";

                            memcpy(out + o, prefix, sizeof(prefix) - 1);
                            o += (unsigned int) (sizeof(prefix) - 1);
                            if (inclusive) {
                                memcpy(out + o, css + number_start,
                                        number_end - number_start);
                                o += number_end - number_start;
                            } else {
                                number_len = pcore_css_ulong_decimal(number,
                                        value - 1UL);
                                memcpy(out + o, number, number_len);
                                o += number_len;
                            }
                            memcpy(out + o, "px)", 3);
                            o += 3;
                            i = p + 1;
                            continue;
                        }
                    }
                }
            }
        }
        out[o++] = css[i++];
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

/* libcss still has no CSS custom-properties implementation. Keep this
 * compatibility layer deliberately narrower than the specification: collect
 * declarations from top-level, exact :root rules in one stylesheet, then
 * substitute var() uses in that same stylesheet. This covers design-token
 * sheets such as IANA's without pretending to implement element scope or the
 * cross-stylesheet cascade. */
#define PCORE_CSS_VAR_MAX 128
#define PCORE_CSS_VAR_DEPTH 16

typedef struct pcore_css_var {
    const char *name;
    unsigned int name_len;
    const char *value;
    unsigned int value_len;
} pcore_css_var;

typedef struct pcore_css_var_ctx {
    pcore_css_var vars[PCORE_CSS_VAR_MAX];
    unsigned char active[PCORE_CSS_VAR_MAX];
    unsigned int count;
} pcore_css_var_ctx;

typedef struct pcore_css_buffer {
    char *data;
    unsigned int len;
    unsigned int cap;
    unsigned int limit;
} pcore_css_buffer;

static int pcore_css_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static unsigned int pcore_css_skip_trivia(const char *css, unsigned int len,
        unsigned int pos)
{
    for (;;) {
        while (pos < len && pcore_css_space(css[pos])) {
            pos++;
        }
        if (pos + 1 < len && css[pos] == '/' && css[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len &&
                    !(css[pos] == '*' && css[pos + 1] == '/')) {
                pos++;
            }
            if (pos + 1 >= len) {
                return len;
            }
            pos += 2;
            continue;
        }
        return pos;
    }
}

static int pcore_css_root_prelude(const char *css, unsigned int start,
        unsigned int end)
{
    static const char root[] = ":root";
    unsigned int p;
    unsigned int i;

    p = pcore_css_skip_trivia(css, end, start);
    if (p + 5 > end) {
        return 0;
    }
    for (i = 0; i < 5; i++) {
        char c = css[p + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char) (c + ('a' - 'A'));
        }
        if (c != root[i]) {
            return 0;
        }
    }
    p = pcore_css_skip_trivia(css, end, p + 5);
    return p == end;
}

static unsigned int pcore_css_block_end(const char *css, unsigned int len,
        unsigned int open)
{
    unsigned int i = open + 1;
    unsigned int depth = 1;
    int quote = 0;
    int comment = 0;

    while (i < len) {
        if (comment) {
            if (css[i] == '*' && i + 1 < len && css[i + 1] == '/') {
                i += 2;
                comment = 0;
            } else {
                i++;
            }
            continue;
        }
        if (quote != 0) {
            if (css[i] == '\\' && i + 1 < len) {
                i += 2;
            } else {
                if (css[i] == quote) {
                    quote = 0;
                }
                i++;
            }
            continue;
        }
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            comment = 1;
            i += 2;
        } else if (css[i] == '\'' || css[i] == '"') {
            quote = css[i++];
        } else if (css[i] == '{') {
            depth++;
            i++;
        } else if (css[i] == '}') {
            depth--;
            if (depth == 0) {
                return i;
            }
            i++;
        } else {
            i++;
        }
    }
    return len;
}

static void pcore_css_add_root_decl(pcore_css_var_ctx *ctx,
        const char *css, unsigned int start, unsigned int end)
{
    unsigned int p;
    unsigned int colon;
    unsigned int name_end;
    unsigned int value_start;
    unsigned int value_end;
    unsigned int i;

    p = pcore_css_skip_trivia(css, end, start);
    if (p + 3 > end || css[p] != '-' || css[p + 1] != '-') {
        return;
    }
    colon = p + 2;
    while (colon < end && pcore_css_name_char(css[colon])) {
        colon++;
    }
    name_end = colon;
    while (colon < end && pcore_css_space(css[colon])) {
        colon++;
    }
    if (name_end == p + 2 || colon >= end || css[colon] != ':') {
        return;
    }
    value_start = colon + 1;
    while (value_start < end && pcore_css_space(css[value_start])) {
        value_start++;
    }
    value_end = end;
    while (value_end > value_start && pcore_css_space(css[value_end - 1])) {
        value_end--;
    }
    if (value_start == value_end) {
        return;
    }

    for (i = 0; i < ctx->count; i++) {
        if (ctx->vars[i].name_len == name_end - p &&
                memcmp(ctx->vars[i].name, css + p, name_end - p) == 0) {
            ctx->vars[i].value = css + value_start;
            ctx->vars[i].value_len = value_end - value_start;
            return;
        }
    }
    if (ctx->count < PCORE_CSS_VAR_MAX) {
        pcore_css_var *var = &ctx->vars[ctx->count++];
        var->name = css + p;
        var->name_len = name_end - p;
        var->value = css + value_start;
        var->value_len = value_end - value_start;
    }
}

static void pcore_css_collect_root_block(pcore_css_var_ctx *ctx,
        const char *css, unsigned int start, unsigned int end)
{
    unsigned int decl = start;
    unsigned int i = start;
    unsigned int paren = 0;
    int quote = 0;
    int comment = 0;

    while (i < end) {
        if (comment) {
            if (css[i] == '*' && i + 1 < end && css[i + 1] == '/') {
                i += 2;
                comment = 0;
            } else {
                i++;
            }
            continue;
        }
        if (quote != 0) {
            if (css[i] == '\\' && i + 1 < end) {
                i += 2;
            } else {
                if (css[i] == quote) {
                    quote = 0;
                }
                i++;
            }
            continue;
        }
        if (css[i] == '/' && i + 1 < end && css[i + 1] == '*') {
            comment = 1;
            i += 2;
        } else if (css[i] == '\'' || css[i] == '"') {
            quote = css[i++];
        } else if (css[i] == '(') {
            paren++;
            i++;
        } else if (css[i] == ')' && paren != 0) {
            paren--;
            i++;
        } else if (css[i] == ';' && paren == 0) {
            pcore_css_add_root_decl(ctx, css, decl, i);
            decl = ++i;
        } else {
            i++;
        }
    }
    pcore_css_add_root_decl(ctx, css, decl, end);
}

static void pcore_css_collect_root_vars(pcore_css_var_ctx *ctx,
        const char *css, unsigned int len)
{
    unsigned int segment = 0;
    unsigned int i = 0;
    int quote = 0;
    int comment = 0;

    while (i < len) {
        if (comment) {
            if (css[i] == '*' && i + 1 < len && css[i + 1] == '/') {
                i += 2;
                comment = 0;
            } else {
                i++;
            }
            continue;
        }
        if (quote != 0) {
            if (css[i] == '\\' && i + 1 < len) {
                i += 2;
            } else {
                if (css[i] == quote) {
                    quote = 0;
                }
                i++;
            }
            continue;
        }
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            comment = 1;
            i += 2;
        } else if (css[i] == '\'' || css[i] == '"') {
            quote = css[i++];
        } else if (css[i] == ';') {
            segment = ++i;
        } else if (css[i] == '{') {
            unsigned int close = pcore_css_block_end(css, len, i);
            if (close >= len) {
                return;
            }
            if (pcore_css_root_prelude(css, segment, i)) {
                pcore_css_collect_root_block(ctx, css, i + 1, close);
            }
            i = close + 1;
            segment = i;
        } else {
            i++;
        }
    }
}

static int pcore_css_buffer_append(pcore_css_buffer *out, const char *data,
        unsigned int len)
{
    unsigned int need;
    unsigned int cap;
    char *grown;

    if (len > out->limit - out->len) {
        return 0;
    }
    need = out->len + len + 1;
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

static int pcore_css_var_index(const pcore_css_var_ctx *ctx,
        const char *name, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < ctx->count; i++) {
        if (ctx->vars[i].name_len == len &&
                memcmp(ctx->vars[i].name, name, len) == 0) {
            return (int) i;
        }
    }
    return -1;
}

static int pcore_css_expand_segment(pcore_css_var_ctx *ctx,
        const char *css, unsigned int len, pcore_css_buffer *out,
        unsigned int depth, int strict);

static int pcore_css_expand_variable(pcore_css_var_ctx *ctx, int index,
        pcore_css_buffer *out, unsigned int depth)
{
    int ok;

    if (index < 0 || (unsigned int) index >= ctx->count ||
            depth >= PCORE_CSS_VAR_DEPTH || ctx->active[index]) {
        return 0;
    }
    ctx->active[index] = 1;
    ok = pcore_css_expand_segment(ctx, ctx->vars[index].value,
            ctx->vars[index].value_len, out, depth + 1, 1);
    ctx->active[index] = 0;
    return ok;
}

static int pcore_css_var_word(const char *css, unsigned int len,
        unsigned int pos, unsigned int *open)
{
    unsigned int p;
    char c0;
    char c1;
    char c2;

    if (pos + 3 > len ||
            (pos != 0 && pcore_css_name_char(css[pos - 1]))) {
        return 0;
    }
    c0 = css[pos];
    c1 = css[pos + 1];
    c2 = css[pos + 2];
    if (c0 >= 'A' && c0 <= 'Z') { c0 = (char) (c0 + 32); }
    if (c1 >= 'A' && c1 <= 'Z') { c1 = (char) (c1 + 32); }
    if (c2 >= 'A' && c2 <= 'Z') { c2 = (char) (c2 + 32); }
    if (c0 != 'v' || c1 != 'a' || c2 != 'r') {
        return 0;
    }
    p = pos + 3;
    while (p < len && pcore_css_space(css[p])) {
        p++;
    }
    if (p >= len || css[p] != '(') {
        return 0;
    }
    *open = p;
    return 1;
}

static int pcore_css_var_bounds(const char *css, unsigned int len,
        unsigned int open, unsigned int *comma, unsigned int *close)
{
    unsigned int i = open + 1;
    unsigned int depth = 1;
    int quote = 0;
    int comment = 0;

    *comma = len;
    while (i < len) {
        if (comment) {
            if (css[i] == '*' && i + 1 < len && css[i + 1] == '/') {
                i += 2;
                comment = 0;
            } else {
                i++;
            }
            continue;
        }
        if (quote != 0) {
            if (css[i] == '\\' && i + 1 < len) {
                i += 2;
            } else {
                if (css[i] == quote) {
                    quote = 0;
                }
                i++;
            }
            continue;
        }
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            comment = 1;
            i += 2;
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
            if (css[i] == ',' && depth == 1 && *comma == len) {
                *comma = i;
            }
            i++;
        }
    }
    return 0;
}

static int pcore_css_expand_segment(pcore_css_var_ctx *ctx,
        const char *css, unsigned int len, pcore_css_buffer *out,
        unsigned int depth, int strict)
{
    unsigned int i = 0;
    int quote = 0;
    int comment = 0;

    if (depth >= PCORE_CSS_VAR_DEPTH) {
        return 0;
    }
    while (i < len) {
        if (comment) {
            unsigned int start = i;
            while (i < len) {
                if (css[i] == '*' && i + 1 < len && css[i + 1] == '/') {
                    i += 2;
                    comment = 0;
                    break;
                }
                i++;
            }
            if (!pcore_css_buffer_append(out, css + start, i - start)) {
                return 0;
            }
            continue;
        }
        if (quote != 0) {
            unsigned int start = i;
            while (i < len) {
                if (css[i] == '\\' && i + 1 < len) {
                    i += 2;
                } else {
                    if (css[i] == quote) {
                        quote = 0;
                        i++;
                        break;
                    }
                    i++;
                }
            }
            if (!pcore_css_buffer_append(out, css + start, i - start)) {
                return 0;
            }
            continue;
        }
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            comment = 1;
            continue;
        }
        if (css[i] == '\'' || css[i] == '"') {
            quote = css[i];
            if (!pcore_css_buffer_append(out, css + i, 1)) {
                return 0;
            }
            i++;
            continue;
        }
        {
            unsigned int open;
            if (pcore_css_var_word(css, len, i, &open)) {
                unsigned int comma;
                unsigned int close;
                unsigned int name_start;
                unsigned int name_end;
                unsigned int save;
                int index;
                int ok = 0;

                if (!pcore_css_var_bounds(css, len, open, &comma, &close)) {
                    if (strict) {
                        return 0;
                    }
                } else {
                    name_start = open + 1;
                    name_end = (comma < len) ? comma : close;
                    while (name_start < name_end &&
                            pcore_css_space(css[name_start])) {
                        name_start++;
                    }
                    while (name_end > name_start &&
                            pcore_css_space(css[name_end - 1])) {
                        name_end--;
                    }
                    index = pcore_css_var_index(ctx, css + name_start,
                            name_end - name_start);
                    save = out->len;
                    if (index >= 0) {
                        ok = pcore_css_expand_variable(ctx, index, out,
                                depth);
                    }
                    if (!ok && comma < len) {
                        unsigned int fallback_start = comma + 1;
                        unsigned int fallback_end = close;
                        out->len = save;
                        out->data[out->len] = '\0';
                        while (fallback_start < fallback_end &&
                                pcore_css_space(css[fallback_start])) {
                            fallback_start++;
                        }
                        while (fallback_end > fallback_start &&
                                pcore_css_space(css[fallback_end - 1])) {
                            fallback_end--;
                        }
                        ok = pcore_css_expand_segment(ctx,
                                css + fallback_start,
                                fallback_end - fallback_start, out,
                                depth + 1, 1);
                    }
                    if (ok) {
                        i = close + 1;
                        continue;
                    }
                    out->len = save;
                    out->data[out->len] = '\0';
                    if (strict) {
                        return 0;
                    }
                    if (!pcore_css_buffer_append(out, css + i,
                            close - i + 1)) {
                        return 0;
                    }
                    i = close + 1;
                    continue;
                }
            }
        }
        if (!pcore_css_buffer_append(out, css + i, 1)) {
            return 0;
        }
        i++;
    }
    return 1;
}

static char *pcore_css_compat_root_vars(const char *css, unsigned int len,
        unsigned int *out_len)
{
    pcore_css_var_ctx ctx;
    pcore_css_buffer out;
    unsigned long limit;

    memset(&ctx, 0, sizeof(ctx));
    pcore_css_collect_root_vars(&ctx, css, len);
    if (ctx.count == 0) {
        return NULL;
    }
    limit = (unsigned long) len * 8UL + 4096UL;
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
    if (!pcore_css_expand_segment(&ctx, css, len, &out, 0, 0)) {
        free(out.data);
        return NULL;
    }
    *out_len = out.len;
    return out.data;
}

css_stylesheet *pcore_parse_css_internal(const char *css, unsigned int len,
        const char *url, PCoreResolveUrlFn resolve, void *resolve_pw,
        css_error *out_done)
{
    css_stylesheet_params params;
    css_stylesheet       *sheet = NULL;
    css_error             err;
    pcore_css_resolve_ctx resolve_ctx;
    char                 *compat_css;
    char                 *vars_css;
    char                 *values_css;
    const char           *parse_css;
    unsigned int          parse_len;

    if (css == NULL) {
        return NULL;
    }
    if (out_done != NULL) {
        *out_done = CSS_INVALID;
    }
    if (len == 0) {
        len = (unsigned int) strlen(css);
    }
    parse_css = css;
    parse_len = len;
    compat_css = pcore_css_compat_ranges(css, len, &parse_len);
    if (compat_css != NULL) {
        parse_css = compat_css;
    }
    vars_css = pcore_css_compat_root_vars(parse_css, parse_len, &parse_len);
    if (vars_css != NULL) {
        parse_css = vars_css;
    }
    values_css = pcore_css_compat_values(parse_css, parse_len, &parse_len);
    if (values_css != NULL) {
        parse_css = values_css;
    }

    resolve_ctx.resolve = resolve;
    resolve_ctx.pw = resolve_pw;
    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level          = CSS_LEVEL_DEFAULT;
    params.charset        = "UTF-8";
    params.url            = (url != NULL) ? url : "http://positron.local/";
    params.resolve        = pcore_css_resolve;
    params.resolve_pw     = &resolve_ctx;

    err = css_stylesheet_create(&params, &sheet);
    if (err != CSS_OK || sheet == NULL) {
        free(values_css);
        free(vars_css);
        free(compat_css);
        return NULL;
    }

    /* Streaming append: CSS_NEEDDATA is the normal "ok, more welcome". */
    err = css_stylesheet_append_data(sheet, (const uint8_t *) parse_css,
            parse_len);
    free(values_css);
    free(vars_css);
    free(compat_css);
    if (err != CSS_OK && err != CSS_NEEDDATA) {
        css_stylesheet_destroy(sheet);
        return NULL;
    }

    err = css_stylesheet_data_done(sheet);
    if (err != CSS_OK && err != CSS_IMPORTS_PENDING) {
        css_stylesheet_destroy(sheet);
        return NULL;
    }
    if (out_done != NULL) {
        *out_done = err;
    }

    return sheet;
}

PCORE_API HANDLE PCore_ParseCSS(const char *css, unsigned int len,
                                const char *url)
{
    css_stylesheet *sheet;
    css_error done;

    sheet = pcore_parse_css_internal(css, len, url, NULL, NULL, &done);
    if (sheet == NULL) {
        return NULL;
    }
    if (done != CSS_OK) {
        css_stylesheet_destroy(sheet);
        return NULL;
    }

    return (HANDLE) sheet;
}

PCORE_API void PCore_FreeStylesheet(HANDLE hSheet)
{
    if (hSheet == NULL) {
        return;
    }
    css_stylesheet_destroy((css_stylesheet *) hSheet);
}
