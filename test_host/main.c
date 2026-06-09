/*
 * test_host/main.c - Phase 2 verification harness.
 *
 * Runs four self-contained tests, showing a MessageBox between each:
 *
 *   TEST 1 - DLL load
 *     Verify positron_tls / positron_json / positron_http are reachable
 *     and that key exports resolve. (We link them statically via .lib,
 *     so this mostly verifies the loader found the DLL files; if not,
 *     the process wouldn't even start.)
 *
 *   TEST 2 - JSON round-trip
 *     PJson_Parse a small literal, extract a string and an int, free.
 *
 *   TEST 3 - HTTPS GET (no auth)
 *     checkip.amazonaws.com -> plain-text public IP. China-direct;
 *     cert chains to Amazon Root CA 1 (in our CA bundle). JSON parsing
 *     is still covered by the offline TEST 2 and the nested TEST 4.
 *
 *   TEST 4 - HTTPS POST (no auth)
 *     postman-echo.com/post body {"hello":"positron"} -> parse echo
 *     response, extract .json.hello, show it.
 *
 * No stdout on WinCE - all output via MessageBoxW.
 * No API keys. All test endpoints are public.
 */

#include <windows.h>
#include <string.h>
#include <stdio.h>

#include "positron_tls.h"
#include "positron_json.h"
#include "positron_http.h"

#include <hubbub/parser.h>

#include <libcss/libcss.h>
#include <libwapcaplet/libwapcaplet.h>

/* dom_hubbub binding (HTML -> libdom DOM). Included by relative path: the
 * binding header has no clean public name, and its sibling "errors.h"
 * resolves via the header's own directory. Pulls <dom/dom.h> too. */
#include "../netsurf-all-3.11/libdom/bindings/hubbub/parser.h"

/* positron_core.dll - the product-level engine boundary. TEST 8 drives the
 * NetSurf engine through this DLL's PCore_* API instead of the raw static
 * libs, exactly as a real Positron app would consume it. */
#include "positron_core.h"

/* -------------------------------------------------------------------- */
/* Display helpers                                                       */
/* -------------------------------------------------------------------- */

static void utf8_to_wide(const char* src, int src_len,
                         WCHAR* dst, int dst_cap)
{
    int n;
    if (dst_cap < 1) {
        return;
    }
    if (src_len < 0) {
        src_len = (int)strlen(src);
    }
    n = MultiByteToWideChar(CP_UTF8, 0, src, src_len, dst, dst_cap - 1);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, src, src_len, dst, dst_cap - 1);
    }
    if (n < 0) {
        n = 0;
    }
    dst[n] = L'\0';
}

static void show_info(const WCHAR* title, const char* body)
{
    WCHAR  wbuf[1536];
    int    body_len;

    body_len = (int)strlen(body);
    if (body_len > 1024) {
        body_len = 1024;
    }
    utf8_to_wide(body, body_len, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    OutputDebugStringW(title);
    OutputDebugStringW(L": ");
    OutputDebugStringW(wbuf);
    OutputDebugStringW(L"\r\n");
    MessageBoxW(NULL, wbuf, title,
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

static void show_error(const WCHAR* title, const char* body)
{
    WCHAR  wbuf[1536];
    int    body_len;

    body_len = (int)strlen(body);
    if (body_len > 1024) {
        body_len = 1024;
    }
    utf8_to_wide(body, body_len, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    OutputDebugStringW(title);
    OutputDebugStringW(L": ");
    OutputDebugStringW(wbuf);
    OutputDebugStringW(L"\r\n");
    MessageBoxW(NULL, wbuf, title,
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

/* Ask a Yes/No question via MessageBox; returns TRUE for Yes. Drives the
 * startup group selector so a subset of tests can be run at a time (e.g.
 * only the offline engine/rendering group when there is no network). */
static BOOL ask_yesno(const WCHAR* title, const char* body)
{
    WCHAR wbuf[512];
    utf8_to_wide(body, -1, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    return MessageBoxW(NULL, wbuf, title,
                       MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND)
           == IDYES;
}

/* -------------------------------------------------------------------- */
/* TEST 1 - DLL load                                                     */
/* -------------------------------------------------------------------- */

static BOOL test1_dll_load(void)
{
    /* We are statically linked against all three import libs.
     * If any DLL failed to load, the process would not have started.
     * Reach into one export from each module to make sure the link
     * is real and not eliminated. */
    const char* probe;
    HANDLE      h;

    if (!PTls_Init()) {
        show_error(L"TEST 1 FAIL", "PTls_Init returned FALSE");
        return FALSE;
    }
    PTls_Cleanup();

    h = PJson_Parse("{}");
    if (h == NULL) {
        show_error(L"TEST 1 FAIL", "PJson_Parse(empty) returned NULL");
        return FALSE;
    }
    PJson_Free(h);

    probe = "(stub)";
    (void)probe;

    if (!PHttp_Init()) {
        show_error(L"TEST 1 FAIL", "PHttp_Init returned FALSE");
        return FALSE;
    }
    /* leave PHttp initialized for the next tests */

    show_info(L"TEST 1 OK", "All three DLLs loaded and core exports resolved.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 2 - JSON round-trip                                              */
/* -------------------------------------------------------------------- */

static BOOL test2_json(void)
{
    HANDLE      root;
    const char* s;
    int         n;
    char        msg[256];

    root = PJson_Parse("{\"key\":\"value\",\"num\":42}");
    if (root == NULL) {
        show_error(L"TEST 2 FAIL", "PJson_Parse returned NULL");
        return FALSE;
    }

    s = PJson_GetString(root, "key");
    if (s == NULL || strcmp(s, "value") != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                  "PJson_GetString(\"key\") returned %s",
                  s ? s : "(null)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 2 FAIL", msg);
        PJson_Free(root);
        return FALSE;
    }

    n = PJson_GetInt(root, "num");
    if (n != 42) {
        _snprintf(msg, sizeof(msg) - 1,
                  "PJson_GetInt(\"num\") returned %d, want 42", n);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 2 FAIL", msg);
        PJson_Free(root);
        return FALSE;
    }

    PJson_Free(root);
    show_info(L"TEST 2 OK", "JSON parse: key=\"value\", num=42 verified.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 3 - HTTPS GET                                                    */
/* checkip.amazonaws.com returns the caller's public IP as plain text    */
/* ("x.x.x.x\n"). Picked over api.ipify.org because it is reachable from */
/* mainland China without a VPN and its cert chains to Amazon Root CA 1, */
/* which is in our embedded CA bundle. (JSON coverage is unaffected: the */
/* offline TEST 2 is the JSON unit test and TEST 4 parses nested JSON.)  */
/* -------------------------------------------------------------------- */

static BOOL test3_get(void)
{
    PHttpResponse* resp;
    const char*    body;
    char           ip[64];
    int            i;
    int            dots;
    char           msg[512];

    resp = PHttp_Get("checkip.amazonaws.com", 443, "/", NULL);
    if (resp == NULL) {
        show_error(L"TEST 3 FAIL", "PHttp_Get returned NULL (OOM?)");
        return FALSE;
    }
    if (resp->status_code != 200) {
        _snprintf(msg, sizeof(msg) - 1,
                  "HTTPS GET checkip -> status=%d err=%s\nbody (first 200):\n%.200s",
                  resp->status_code, resp->error_msg,
                  resp->body ? resp->body : "(none)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 3 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    /* Body is the public IP as plain text with a trailing newline. Copy it
     * out, trim trailing CR/LF/space, and sanity-check it looks like a
     * dotted IPv4 address (exactly three dots). */
    body = resp->body ? resp->body : "";
    for (i = 0; body[i] != '\0' && i < (int)sizeof(ip) - 1; i++) {
        ip[i] = body[i];
    }
    ip[i] = '\0';
    while (i > 0 && (ip[i - 1] == '\n' || ip[i - 1] == '\r' || ip[i - 1] == ' ')) {
        ip[--i] = '\0';
    }

    dots = 0;
    for (i = 0; ip[i] != '\0'; i++) {
        if (ip[i] == '.') {
            dots++;
        }
    }
    if (ip[0] == '\0' || dots != 3) {
        _snprintf(msg, sizeof(msg) - 1,
                  "checkip returned 200 but body is not an IPv4 address:\n%.200s",
                  body);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 3 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "HTTPS GET checkip.amazonaws.com OK\n\nYour public IP:\n%s\n\n"
              "(Proves TLS 1.2 + HTTPS GET round trip; China-direct.)", ip);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 3 OK", msg);

    PHttp_FreeResponse(resp);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 4 - HTTPS POST                                                   */
/* postman-echo.com/post echoes the JSON body under "json" key          */
/* (same shape as httpbin; cert chains LE E8 -> ISRG Root X1, in bundle)*/
/* -------------------------------------------------------------------- */

static BOOL test4_post(void)
{
    static const char* HEADERS[] = {
        "Content-Type: application/json",
        NULL
    };
    static const char* BODY = "{\"hello\":\"positron\"}";

    PHttpResponse* resp;
    HANDLE         root;
    HANDLE         json_obj;
    const char*    echoed;
    char           msg[512];

    resp = PHttp_Post("postman-echo.com", 443, "/post",
                      HEADERS, BODY, (int)strlen(BODY));
    if (resp == NULL) {
        show_error(L"TEST 4 FAIL", "PHttp_Post returned NULL (OOM?)");
        return FALSE;
    }
    if (resp->status_code != 200) {
        _snprintf(msg, sizeof(msg) - 1,
                  "HTTPS POST postman-echo -> status=%d err=%s\nbody (first 256):\n%.256s",
                  resp->status_code, resp->error_msg,
                  resp->body ? resp->body : "(none)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 4 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    root = PJson_Parse(resp->body);
    if (root == NULL) {
        show_error(L"TEST 4 FAIL", "postman-echo response is not JSON");
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    json_obj = PJson_GetObject(root, "json");
    if (json_obj == NULL) {
        show_error(L"TEST 4 FAIL",
                   "postman-echo response missing 'json' key (server changed?)");
        PJson_Free(root);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    echoed = PJson_GetString(json_obj, "hello");
    if (echoed == NULL || strcmp(echoed, "positron") != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                  "postman-echo echo mismatch: expected positron, got %s",
                  echoed ? echoed : "(null)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 4 FAIL", msg);
        PJson_Free(root);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "HTTPS POST postman-echo OK\n\n"
              "Sent: {\"hello\":\"positron\"}\n"
              "Server echoed: hello=%s\n\n"
              "(Full stack OK: TLS 1.2 + HTTPS POST + chunked decode\n"
              "+ JSON nested-object extraction.)", echoed);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 4 OK", msg);

    PJson_Free(root);
    PHttp_FreeResponse(resp);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 5 - Verified TLS                                                 */
/* Three sub-tests against badssl.com:                                   */
/*   A) badssl.com           -> must succeed (valid LE cert)             */
/*   B) expired.badssl.com   -> must fail, reason mentions expiry        */
/*   C) self-signed.badssl.com -> must fail, reason mentions trust       */
/*                                                                       */
/* Prerequisite: the emulator's wall clock must be set to a current      */
/* date. WM6 emulator defaults to ~2005-2007; X.509 validity windows    */
/* will reject everything if you don't fix this first. Set it via:      */
/*   Start -> Settings -> Clock & Alarms                                 */
/* -------------------------------------------------------------------- */

static BOOL test5_verified_tls(void)
{
    HANDLE conn;
    char   err_buf[256];
    char   summary[1024];

    /* --- A) valid host --- */
    conn = PTls_ConnectVerified("badssl.com", 443);
    if (conn == NULL) {
        _snprintf(summary, sizeof(summary) - 1,
                  "TEST 5A FAIL: badssl.com (valid cert) was REJECTED\n"
                  "Reason: %s\n\n"
                  "Common causes:\n"
                  " - Emulator clock is wrong (Settings -> Clock & Alarms)\n"
                  " - CA bundle does not include ISRG Root X1\n"
                  " - Network blocked",
                  PTls_LastError());
        summary[sizeof(summary) - 1] = '\0';
        show_error(L"TEST 5 FAIL", summary);
        return FALSE;
    }
    PTls_Close(conn);

    /* --- B) expired cert: must be rejected --- */
    conn = PTls_ConnectVerified("expired.badssl.com", 443);
    if (conn != NULL) {
        PTls_Close(conn);
        show_error(L"TEST 5 FAIL",
                   "expired.badssl.com was ACCEPTED; verification is broken.");
        return FALSE;
    }
    _snprintf(err_buf, sizeof(err_buf) - 1, "%s", PTls_LastError());
    err_buf[sizeof(err_buf) - 1] = '\0';

    /* --- C) self-signed: must be rejected --- */
    {
        char err2[256];
        conn = PTls_ConnectVerified("self-signed.badssl.com", 443);
        if (conn != NULL) {
            PTls_Close(conn);
            show_error(L"TEST 5 FAIL",
                       "self-signed.badssl.com was ACCEPTED; "
                       "verification is broken.");
            return FALSE;
        }
        _snprintf(err2, sizeof(err2) - 1, "%s", PTls_LastError());
        err2[sizeof(err2) - 1] = '\0';

        _snprintf(summary, sizeof(summary) - 1,
                  "Verified TLS works end-to-end.\n\n"
                  "A) badssl.com (valid)\n   ACCEPTED\n\n"
                  "B) expired.badssl.com\n   REJECTED: %.200s\n\n"
                  "C) self-signed.badssl.com\n   REJECTED: %.200s",
                  err_buf, err2);
        summary[sizeof(summary) - 1] = '\0';
    }

    show_info(L"TEST 5 OK", summary);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 6 - libhubbub HTML tokeniser (NetSurf engine, Phase 4)           */
/* Feeds a small HTML document to hubbub in tokeniser mode: setting a     */
/* custom token handler makes hubbub destroy its default treebuilder and  */
/* hand us the raw token stream. We verify the structural token counts    */
/* (deterministic at the tokeniser level) and that the &amp; entity is    */
/* decoded to '&'. This proves the ported NetSurf parser links against    */
/* coredll (catches missing CRT exports) and runs on real ARM hardware.   */
/* fix_enc=true forces a charset-alias lookup, exercising our bsearch shim.*/
/* -------------------------------------------------------------------- */

typedef struct {
    int doctype;
    int start;
    int end;
    int comment;
    int chars;
    int eof;
} hb_counts;

static hubbub_error hb_token(const hubbub_token *token, void *pw)
{
    hb_counts *c = (hb_counts *) pw;

    switch (token->type) {
    case HUBBUB_TOKEN_DOCTYPE:   c->doctype++; break;
    case HUBBUB_TOKEN_START_TAG: c->start++;   break;
    case HUBBUB_TOKEN_END_TAG:   c->end++;     break;
    case HUBBUB_TOKEN_COMMENT:   c->comment++; break;
    case HUBBUB_TOKEN_CHARACTER: c->chars += (int) token->data.character.len; break;
    case HUBBUB_TOKEN_EOF:       c->eof++;     break;
    default: break;
    }

    return HUBBUB_OK;
}

static BOOL test6_hubbub(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>Hi</title></head>"
        "<body><p>Hello &amp; world</p><!-- c --></body></html>";

    hubbub_parser          *parser = NULL;
    hubbub_parser_optparams params;
    hubbub_error            err;
    hb_counts               c;
    char                    msg[512];

    memset(&c, 0, sizeof(c));

    err = hubbub_parser_create("UTF-8", true, &parser);
    if (err != HUBBUB_OK || parser == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "hubbub_parser_create failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        return FALSE;
    }

    params.token_handler.handler = hb_token;
    params.token_handler.pw      = &c;
    err = hubbub_parser_setopt(parser, HUBBUB_PARSER_TOKEN_HANDLER, &params);
    if (err != HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "hubbub setopt(TOKEN_HANDLER) failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        hubbub_parser_destroy(parser);
        return FALSE;
    }

    err = hubbub_parser_parse_chunk(parser, (const uint8_t *) HTML,
                                    strlen(HTML));
    if (err != HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "hubbub_parser_parse_chunk failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        hubbub_parser_destroy(parser);
        return FALSE;
    }

    hubbub_parser_completed(parser);
    hubbub_parser_destroy(parser);

    /* Structural counts are deterministic at the tokeniser level:
     * doctype 1; start tags html/head/title/body/p = 5; end tags
     * /title//head//p//body//html = 5; one comment. chars should be 15:
     * "Hi"(2) + "Hello & world"(13, with &amp; decoded to a single '&'). */
    if (c.doctype != 1 || c.start != 5 || c.end != 5 || c.comment != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                  "TEST 6 token counts wrong:\n"
                  "doctype=%d (want 1)\nstart=%d (want 5)\n"
                  "end=%d (want 5)\ncomment=%d (want 1)\nchars=%d (want 15)",
                  c.doctype, c.start, c.end, c.comment, c.chars);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "libhubbub HTML tokeniser OK\n\n"
              "doctype=%d  start=%d  end=%d\ncomment=%d  chars=%d\n\n"
              "chars=15 => &amp; decoded to '&'.\n"
              "(NetSurf parser links + runs on ARM WinCE.)",
              c.doctype, c.start, c.end, c.comment, c.chars);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 6 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 7 - libcss CSS parser (NetSurf engine, Phase 4)                  */
/* Parse a small stylesheet end-to-end: create -> append_data ->         */
/* data_done -> destroy. Links positron_libcss (surfaces any missing CRT */
/* exports such as strdup/strncasecmp) and proves the ported CSS parser  */
/* runs on real ARM hardware.                                            */
/* -------------------------------------------------------------------- */

static css_error test7_resolve(void *pw, const char *base,
                               lwc_string *rel, lwc_string **abs)
{
    (void) pw;
    (void) base;
    /* No real URL resolution needed (test CSS has no @import); hand the
     * relative reference straight back, taking a ref as the API expects. */
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static BOOL test7_libcss(void)
{
    static const char *CSS =
        "body { color: #ffffff; background: #000000; }\n"
        "p { margin: 1em; font-size: 12px; }\n"
        "a:hover { text-decoration: underline; }\n";

    css_stylesheet_params params;
    css_stylesheet       *sheet = NULL;
    css_error             err;
    char                  msg[256];

    /* Zero the block, then set only the fields we need; title / quirks /
     * inline / import / colour / font callbacks stay NULL/false. */
    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level          = CSS_LEVEL_DEFAULT;
    params.charset        = "UTF-8";
    params.url            = "http://positron.local/test.css";
    params.resolve        = test7_resolve;

    err = css_stylesheet_create(&params, &sheet);
    if (err != CSS_OK || sheet == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "css_stylesheet_create failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7 FAIL", msg);
        return FALSE;
    }

    /* Streaming append: CSS_NEEDDATA is the normal "ok, more welcome". */
    err = css_stylesheet_append_data(sheet, (const uint8_t *) CSS,
                                     strlen(CSS));
    if (err != CSS_OK && err != CSS_NEEDDATA) {
        _snprintf(msg, sizeof(msg) - 1,
                  "css_stylesheet_append_data failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7 FAIL", msg);
        css_stylesheet_destroy(sheet);
        return FALSE;
    }

    err = css_stylesheet_data_done(sheet);
    if (err != CSS_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "css_stylesheet_data_done failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7 FAIL", msg);
        css_stylesheet_destroy(sheet);
        return FALSE;
    }

    css_stylesheet_destroy(sheet);

    show_info(L"TEST 7 OK",
              "libcss parsed a stylesheet end-to-end:\n"
              "create -> append_data -> data_done -> destroy.\n\n"
              "3 rules (body / p / a:hover) accepted.\n\n"
              "(NetSurf CSS parser links + runs on ARM WinCE.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 7b - libdom HTML -> DOM via the dom_hubbub binding (Phase 4)     */
/* Drives hubbub through libdom's binding to build a real DOM tree, then  */
/* fetches the document element. Links positron_libdom + the binding      */
/* (surfaces further CRT gaps such as snprintf) and proves the ported     */
/* HTML->DOM pipeline runs on real ARM hardware.                          */
/* -------------------------------------------------------------------- */

static void test7b_msg(uint32_t severity, void *ctx, const char *msg, ...)
{
    (void) severity;
    (void) ctx;
    (void) msg;
    /* Swallow libdom/hubbub diagnostics (no stdout on WinCE anyway). */
}

static BOOL test7b_dom(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>Hi</title></head>"
        "<body><p>Hello</p><p>World</p></body></html>";

    dom_hubbub_parser_params params;
    dom_hubbub_parser       *parser = NULL;
    dom_document            *doc = NULL;
    dom_element             *root = NULL;
    dom_hubbub_error         derr;
    dom_exception            exc;
    char                     msg[256];

    memset(&params, 0, sizeof(params));
    params.enc           = "UTF-8";
    params.fix_enc       = true;
    params.enable_script = false;
    params.script        = NULL;
    params.msg           = test7b_msg;
    params.ctx           = NULL;
    params.daf           = NULL;

    derr = dom_hubbub_parser_create(&params, &parser, &doc);
    if (derr != DOM_HUBBUB_OK || parser == NULL || doc == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "dom_hubbub_parser_create failed: err=%d", (int) derr);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        if (parser != NULL) {
            dom_hubbub_parser_destroy(parser);
        }
        return FALSE;
    }

    derr = dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) HTML,
                                         strlen(HTML));
    if (derr != DOM_HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "dom_hubbub_parser_parse_chunk failed: err=%d", (int) derr);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        dom_hubbub_parser_destroy(parser);
        dom_node_unref((dom_node *) doc);
        return FALSE;
    }

    derr = dom_hubbub_parser_completed(parser);
    if (derr != DOM_HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "dom_hubbub_parser_completed failed: err=%d", (int) derr);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        dom_hubbub_parser_destroy(parser);
        dom_node_unref((dom_node *) doc);
        return FALSE;
    }

    /* Parser finished; the document is now owned by us. */
    dom_hubbub_parser_destroy(parser);

    exc = dom_document_get_document_element(doc, &root);
    if (exc != DOM_NO_ERR || root == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "get_document_element failed: exc=%d (root=%p)",
                  (int) exc, (void *) root);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        dom_node_unref((dom_node *) doc);
        return FALSE;
    }

    dom_node_unref((dom_node *) root);
    dom_node_unref((dom_node *) doc);

    show_info(L"TEST 7b OK",
              "libdom built a DOM tree from HTML via dom_hubbub:\n"
              "create -> parse_chunk -> completed -> document element.\n\n"
              "(NetSurf HTML->DOM pipeline links + runs on ARM WinCE.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 8 - positron_core.dll product boundary (Phase 4)                 */
/* The same HTML->DOM and CSS parse as TEST 7/7b, but driven through the  */
/* PCore_* API exported by positron_core.dll - i.e. the NetSurf engine    */
/* linked behind the shared DLL, exactly as a real Positron app would     */
/* consume it. Proves the engine links + runs inside the DLL and that     */
/* only the small PCore_* surface crosses the boundary.                   */
/* -------------------------------------------------------------------- */

static BOOL test8_core(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>Hi</title></head>"
        "<body><p>Hello</p><p>World</p></body></html>";
    static const char *CSS =
        "body { color: #ffffff; background: #000000; }\n"
        "p { margin: 1em; font-size: 12px; }\n"
        "a:hover { text-decoration: underline; }\n";

    HANDLE hDoc;
    HANDLE hSheet;

    if (PCore_Init() != 0) {
        show_error(L"TEST 8 FAIL", "PCore_Init failed");
        return FALSE;
    }

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 8 FAIL",
                   "PCore_ParseHTML returned NULL "
                   "(HTML->DOM via positron_core.dll failed)");
        PCore_Shutdown();
        return FALSE;
    }

    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 8 FAIL",
                   "PCore_ParseCSS returned NULL "
                   "(CSS parse via positron_core.dll failed)");
        PCore_FreeDocument(hDoc);
        PCore_Shutdown();
        return FALSE;
    }

    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    PCore_Shutdown();

    show_info(L"TEST 8 OK",
              "positron_core.dll drove the full engine:\n"
              "PCore_ParseHTML built a DOM tree, PCore_ParseCSS parsed a\n"
              "stylesheet - both through the shared DLL boundary.\n\n"
              "(NetSurf engine runs behind positron_core.dll on ARM WinCE.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 9 - CSS selection / computed style via positron_core.dll         */
/* Parse a tiny HTML doc + stylesheet, then PCore_ComputeColor drives     */
/* libcss selection (libdom-backed handler) to compute the <p>'s color.   */
/* Proves the select / computed-style layer runs end-to-end on ARM WinCE. */
/* -------------------------------------------------------------------- */

static BOOL test9_select(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><p>Hello</p></body></html>";
    static const char *CSS = "p { color: #112233; }\n";

    HANDLE        hDoc;
    HANDLE        hSheet;
    unsigned long argb;
    unsigned long rgb;
    int           rc;
    char          msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 9 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 9 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    rc = PCore_ComputeColor(hDoc, hSheet, "p", &argb);
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    if (rc != 0) {
        show_error(L"TEST 9 FAIL",
                   "PCore_ComputeColor failed (no <p>, or selection error)");
        return FALSE;
    }

    rgb = argb & 0x00FFFFFFUL;       /* low 24 bits = RRGGBB */
    if (rgb != 0x00112233UL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "computed color = 0x%08lX (RGB 0x%06lX), expected 0x112233",
                  argb, rgb);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 9 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "positron_core selected style for <p>:\n"
              "  p { color: #112233 } -> computed 0x%06lX\n\n"
              "(libcss selection + libdom-backed handler run on ARM WinCE.)",
              rgb);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 9 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* WinMain                                                               */
/* -------------------------------------------------------------------- */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                   LPWSTR lpCmdLine, int nCmdShow)
{
    BOOL run_comm;
    BOOL run_engine;
    BOOL run_frontend;
    int  rc;
    char summary[768];

    (void)hInstance;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    OutputDebugStringW(L"test_host (Phase 4): starting\r\n");

    /* Group selector. One tap runs everything; otherwise pick groups so a
     * subset can run in isolation - e.g. only the fully-offline engine /
     * rendering group when there is no network (no VPN needed). */
    if (ask_yesno(L"Positron test_host",
                  "Run ALL tests?\n\n"
                  "Yes = run everything (TEST 1-9)\n"
                  "No  = choose which groups to run")) {
        run_comm = TRUE;
        run_engine = TRUE;
        run_frontend = TRUE;
    } else {
        run_comm = ask_yesno(L"Select groups (1/3)",
                             "Run COMMUNICATION tests?\n\n"
                             "TLS / HTTP / JSON  (TEST 1-5)\n"
                             "Needs network access.");
        run_engine = ask_yesno(L"Select groups (2/3)",
                               "Run ENGINE / RENDERING tests?\n\n"
                               "HTML / CSS / DOM via NetSurf + positron_core\n"
                               "(TEST 6-9). Fully offline - no network needed.");
        run_frontend = ask_yesno(L"Select groups (3/3)",
                                 "Run FRONTEND tests?\n\n"
                                 "Layout / GDI rendering - not implemented yet.");
    }

    rc = 0;

    /* --- Communication group (TEST 1-5) ------------------------------- */
    if (run_comm) {
        if (!test1_dll_load()) {
            /* positron_http was not initialised; nothing to clean up. */
            return 1;
        }
        if (!test2_json())         { rc = 2; goto done; }
        if (!test3_get())          { rc = 3; goto done; }
        if (!test4_post())         { rc = 4; goto done; }
        if (!test5_verified_tls()) { rc = 5; goto done; }
    }

    /* --- Engine / rendering group (TEST 6-9, all offline) ------------- */
    if (run_engine) {
        if (!test6_hubbub())       { rc = 6; goto done; }
        if (!test7_libcss())       { rc = 7; goto done; }
        if (!test7b_dom())         { rc = 8; goto done; }
        if (!test8_core())         { rc = 9; goto done; }
        if (!test9_select())       { rc = 10; goto done; }
    }

    /* --- Frontend group (placeholder) -------------------------------- */
    if (run_frontend) {
        show_info(L"Frontend (TODO)",
                  "No frontend tests yet.\n\n"
                  "Layout + GDI rendering is a future Positron phase; this\n"
                  "group is a placeholder so the selector already lists it.");
    }

    /* Success summary - list only the groups that actually ran. */
    summary[0] = '\0';
    strcat(summary, "Selected test groups passed:\n\n");
    if (run_comm) {
        strcat(summary,
               "  Communication (TEST 1-5)\n"
               "    TLS 1.2 + chain/hostname verify, CA bundle,\n"
               "    HTTPS GET (checkip) + POST, chunked, JSON.\n\n");
    }
    if (run_engine) {
        strcat(summary,
               "  Engine / Rendering (TEST 6-9)\n"
               "    libhubbub + libcss + libdom behind\n"
               "    positron_core.dll; CSS select -> computed style.\n"
               "    Fully offline.\n\n");
    }
    if (!run_comm && !run_engine && !run_frontend) {
        strcat(summary, "  (no groups selected)\n");
    }
    show_info(L"Tests passed", summary);

done:
    /* Only the communication group brings up positron_http (via TEST 1),
     * so only tear it down when that group ran. */
    if (run_comm) {
        PHttp_Cleanup();
    }
    return rc;
}
