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

PCORE_API HANDLE PCore_ParseCSS(const char *css, unsigned int len,
                                const char *url)
{
    css_stylesheet_params params;
    css_stylesheet       *sheet = NULL;
    css_error             err;

    if (css == NULL) {
        return NULL;
    }
    if (len == 0) {
        len = (unsigned int) strlen(css);
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
        return NULL;
    }

    /* Streaming append: CSS_NEEDDATA is the normal "ok, more welcome". */
    err = css_stylesheet_append_data(sheet, (const uint8_t *) css, len);
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
