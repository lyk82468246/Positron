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

/* No real URL resolution needed yet (no @import in the sheets we handle);
 * hand the relative reference straight back, taking a ref as the API expects. */
static css_error pcore_css_resolve(void *pw, const char *base,
                                   lwc_string *rel, lwc_string **abs)
{
    (void) pw;
    (void) base;
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

PCORE_API HANDLE PCore_ParseCSS(const char *css, unsigned int len,
                                const char *url)
{
    css_stylesheet_params params;
    css_stylesheet       *sheet = NULL;
    css_error             err;
    char                 *compat_css;
    const char           *parse_css;
    unsigned int          parse_len;

    if (css == NULL) {
        return NULL;
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

    /* Zero the block, then set only the fields we need; title / quirks /
     * inline / import / colour / font callbacks stay NULL/false. */
    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level          = CSS_LEVEL_DEFAULT;
    params.charset        = "UTF-8";
    params.url            = (url != NULL) ? url : "http://positron.local/";
    params.resolve        = pcore_css_resolve;

    err = css_stylesheet_create(&params, &sheet);
    if (err != CSS_OK || sheet == NULL) {
        free(compat_css);
        return NULL;
    }

    /* Streaming append: CSS_NEEDDATA is the normal "ok, more welcome". */
    err = css_stylesheet_append_data(sheet, (const uint8_t *) parse_css,
            parse_len);
    free(compat_css);
    if (err != CSS_OK && err != CSS_NEEDDATA) {
        css_stylesheet_destroy(sheet);
        return NULL;
    }

    err = css_stylesheet_data_done(sheet);
    if (err != CSS_OK) {
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
