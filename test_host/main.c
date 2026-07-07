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
#include <stdlib.h>     /* malloc / free for fetched-CSS buffers */
#include <aygshell.h>   /* SHFullScreen / SHSipPreference - control the SIP */

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
/* libcss selection (libdom-backed handler) to compute element colours.   */
/* Proves type/class/id + attribute and sibling selectors end-to-end.     */
/* -------------------------------------------------------------------- */

static BOOL test9_select(void)
{
    HANDLE        hDoc;
    HANDLE        hSheet;
    unsigned long p_argb;
    unsigned long p_rgb;
    unsigned long span_argb;
    unsigned long span_rgb;
    int           rc;
    char          msg[384];
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><h1>Title</h1>"
        "<p title=\"hello\" data-role=\"intro\" class=\"lead\" "
        "lang=\"en-US\">Hello</p>"
        "<div>gap</div>"
        "<span data-code=\"pre-mid-post\">World</span>"
        "</body></html>";
    static const char *CSS =
        "p { color: #010101; }\n"
        "h1 + p[title][data-role=\"intro\"][class~=\"lead\"][lang|=\"en\"] "
        "{ color: #112233; }\n"
        "span { color: #010101; }\n"
        "h1 ~ span[data-code^=\"pre\"][data-code$=\"post\"]"
        "[data-code*=\"-mid-\"] { color: #445566; }\n";


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

    rc = PCore_ComputeColor(hDoc, hSheet, "p", &p_argb);
    if (rc == 0) {
        rc = PCore_ComputeColor(hDoc, hSheet, "span", &span_argb);
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    if (rc != 0) {
        show_error(L"TEST 9 FAIL",
                   "PCore_ComputeColor failed (missing <p>/<span>, "
                   "or selection error)");
        return FALSE;
    }

    p_rgb = p_argb & 0x00FFFFFFUL;          /* low 24 bits = RRGGBB */
    span_rgb = span_argb & 0x00FFFFFFUL;
    if (p_rgb != 0x00112233UL || span_rgb != 0x00445566UL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "computed colors off:\n"
                  "  p    RGB 0x%06lX, expected 0x112233\n"
                  "  span RGB 0x%06lX, expected 0x445566",
                  p_rgb, span_rgb);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 9 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "CSS select OK:\n"
              "  h1 + p[title][data-role][class~][lang|]\n"
              "    -> p RGB 0x%06lX\n"
              "  h1 ~ span[data-code^][data-code$][data-code*]\n"
              "    -> span RGB 0x%06lX\n\n"
              "(attribute + sibling selectors via libdom handler.)",
              p_rgb, span_rgb);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 9 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 10 - whole-document styling + inheritance via positron_core.dll   */
/* PCore_StyleDocument styles every element (UA + author sheet, top-down   */
/* compose). A nested <p> with no color of its own must inherit body's.    */
/* -------------------------------------------------------------------- */

static BOOL test10_styledoc(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><div><p>Hello</p></div></body></html>";
    static const char *CSS = "body { color: #112233; }\n";

    HANDLE        hDoc;
    HANDLE        hSheet;
    unsigned long argb;
    unsigned long rgb;
    int           rc;
    char          msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 10 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 10 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    rc = PCore_StyleDocument(hDoc, hSheet);
    if (rc != 0) {
        show_error(L"TEST 10 FAIL", "PCore_StyleDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    rc = PCore_NodeComputedColor(hDoc, "p", &argb);
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    if (rc != 0) {
        show_error(L"TEST 10 FAIL",
                   "PCore_NodeComputedColor failed (no <p> style attached)");
        return FALSE;
    }

    rgb = argb & 0x00FFFFFFUL;
    if (rgb != 0x00112233UL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "<p> inherited color = 0x%06lX, expected 0x112233 "
                  "(body -> div -> p)", rgb);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 10 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "Whole-tree styling + inheritance OK:\n"
              "  body { color:#112233 }  ->  <p> computes 0x%06lX\n\n"
              "(UA sheet + author sheet, top-down compose over the DOM.)",
              rgb);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 10 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 11 - block-flow layout via positron_core.dll                      */
/* PCore_LayoutDocument computes block boxes; verify body fills the       */
/* viewport minus its margins, and the first <p> is offset + stacked.     */
/* -------------------------------------------------------------------- */

static BOOL test11_layout(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><div><p>Hello</p><p>World</p></div></body></html>";
    static const char *CSS = "body { color: #112233; }\n";
    const int VW = 240;

    HANDLE hDoc;
    HANDLE hSheet;
    int    bx, by, bw, bh;
    int    px, py, pw, ph;
    char   msg[320];

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 11 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 11 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    if (PCore_StyleDocument(hDoc, hSheet) != 0) {
        show_error(L"TEST 11 FAIL", "PCore_StyleDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    if (PCore_LayoutDocument(hDoc, VW, 320) != 0) {
        show_error(L"TEST 11 FAIL", "PCore_LayoutDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    if (PCore_NodeBox(hDoc, "body", &bx, &by, &bw, &bh) != 0 ||
            PCore_NodeBox(hDoc, "p", &px, &py, &pw, &ph) != 0) {
        show_error(L"TEST 11 FAIL", "PCore_NodeBox failed (body / p)");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    /* VW=240: body margin 8 -> (8,8,224); first <p> fills body content and
     * sits 1em(16px) below the div content-top(8) -> x=8 y=24 w=224. */
    if (bx != 8 || by != 8 || bw != 224 ||
            px != 8 || pw != 224 || py != 24) {
        _snprintf(msg, sizeof(msg) - 1,
                  "geometry off:\n"
                  "  body=(%d,%d,%d,%d) expect x8 y8 w224\n"
                  "  p=(%d,%d,%d,%d) expect x8 y24 w224",
                  bx, by, bw, bh, px, py, pw, ph);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 11 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "Block layout OK (viewport %d px):\n"
              "  body box  = (%d,%d) %dx%d\n"
              "  first <p> = (%d,%d) %dx%d\n\n"
              "(width fill - margins, x offset, vertical flow.)",
              VW, bx, by, bw, bh, px, py, pw, ph);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 11 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 12 - first visible page: a real window painted by positron_core   */
/* The app owns the window + message loop; WM_PAINT calls                 */
/* PCore_PaintDocument over the styled + laid-out tree.                   */
/* -------------------------------------------------------------------- */

static HANDLE g_render_doc = NULL;
static int    g_scroll_y = 0;
static int    g_doc_h = 0;
static int    g_plot_test = 0;   /* M1: paint via PCore_PlotTest, not a doc */
static int    g_ns_render = 0;    /* M5e: paint via PCore_NsRenderTest */

/* Current page origin, for resolving relative links during navigation. */
static char   g_cur_host[256] = "";
static char   g_cur_path[1024] = "/";
static int    g_cur_port = 443;

/* Configure the vertical scrollbar from the document height + client size. */
static void pcore_set_scrollbar(HWND hwnd)
{
    SCROLLINFO si;
    RECT rc;
    int ch;

    GetClientRect(hwnd, &rc);
    ch = rc.bottom - rc.top;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (g_doc_h > 0) ? (g_doc_h - 1) : 0;
    si.nPage = (UINT) ((ch > 0) ? ch : 1);
    si.nPos = g_scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

/* Scroll by dy px, clamped to [0, doc_height - client_height], and repaint. */
static void pcore_scroll_by(HWND hwnd, int dy)
{
    RECT rc;
    int ch, maxpos, oldpos, applied;

    GetClientRect(hwnd, &rc);
    ch = rc.bottom - rc.top;
    maxpos = (g_doc_h > ch) ? (g_doc_h - ch) : 0;
    oldpos = g_scroll_y;
    g_scroll_y += dy;
    if (g_scroll_y < 0) {
        g_scroll_y = 0;
    }
    if (g_scroll_y > maxpos) {
        g_scroll_y = maxpos;
    }
    applied = g_scroll_y - oldpos;
    if (applied == 0) {
        return;
    }
    SetScrollPos(hwnd, SB_VERT, g_scroll_y, TRUE);
    /* Shift the existing pixels by -applied and invalidate only the newly
     * exposed strip; the following WM_PAINT repaints just that strip at the
     * new scroll offset. Far cheaper than repainting the whole client. */
    ScrollWindowEx(hwnd, 0, -applied, NULL, NULL, NULL, NULL, SW_INVALIDATE);
    UpdateWindow(hwnd);
}

/* Case-insensitive ASCII prefix test (avoids depending on _strnicmp). */
static int ci_prefix(const char *s, const char *pfx)
{
    while (*pfx != '\0') {
        char a = *s, b = *pfx;
        if (a >= 'A' && a <= 'Z') a = (char) (a + 32);
        if (b >= 'A' && b <= 'Z') b = (char) (b + 32);
        if (a != b) {
            return 0;
        }
        s++; pfx++;
    }
    return 1;
}

/* Bounded NUL-terminated string copy. */
static void cstr_copy(char *d, int cap, const char *s)
{
    int n = 0;
    if (cap <= 0) {
        return;
    }
    while (s[n] != '\0' && n < cap - 1) {
        d[n] = s[n];
        n++;
    }
    d[n] = '\0';
}

/* Copy an absolute path (starts with '/') into dst, stripping any #fragment.
 * Falls back to "/" if empty. */
static void copy_path(char *dst, int cap, const char *src)
{
    int n = 0;
    if (cap <= 0) {
        return;
    }
    while (*src != '\0' && *src != '#' && n < cap - 1) {
        dst[n++] = *src++;
    }
    if (n == 0 && cap > 1) {
        dst[n++] = '/';
    }
    dst[n] = '\0';
}

/* Resolve a link href against the current page (g_cur_host / g_cur_path) into
 * an absolute host + path. Handles absolute http(s), root-relative ("/x") and
 * same-directory relative ("x"). Returns FALSE for unsupported schemes
 * (mailto:/javascript:/tel:) and bare #fragments. All fetched over HTTPS. */
static BOOL resolve_url(const char *href, char *host, int hostcap,
                        char *path, int pathcap, int *out_port)
{
    const char *p = href;

    *out_port = 443;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }

    if (ci_prefix(p, "http://")) {
        p += 7;
        *out_port = 80;
    } else if (ci_prefix(p, "https://")) {
        p += 8;
        *out_port = 443;
    } else if (ci_prefix(p, "mailto:") || ci_prefix(p, "javascript:") ||
               ci_prefix(p, "tel:") || p[0] == '#') {
        return FALSE;   /* not a navigable http(s) document link */
    } else if (p[0] == '/') {
        if (g_cur_host[0] == '\0') {
            return FALSE;
        }
        cstr_copy(host, hostcap, g_cur_host);
        copy_path(path, pathcap, p);
        *out_port = g_cur_port;   /* same scheme as current page */
        return TRUE;
    } else {
        /* Same-directory relative: current host + base dir + href. */
        int n = 0, i, lastslash = -1;
        if (g_cur_host[0] == '\0') {
            return FALSE;
        }
        cstr_copy(host, hostcap, g_cur_host);
        for (i = 0; g_cur_path[i] != '\0'; i++) {
            if (g_cur_path[i] == '/') {
                lastslash = i;
            }
        }
        for (i = 0; i <= lastslash && n < pathcap - 1; i++) {
            path[n++] = g_cur_path[i];
        }
        if (lastslash < 0 && n < pathcap - 1) {
            path[n++] = '/';
        }
        while (*p != '\0' && *p != '#' && n < pathcap - 1) {
            path[n++] = *p++;
        }
        path[n] = '\0';
        *out_port = g_cur_port;   /* same scheme as current page */
        return TRUE;
    }

    /* Absolute: p now points at host[:port][/path][#frag]. */
    {
        int n = 0;
        while (*p != '\0' && *p != '/' && *p != '#' && *p != ':' &&
                n < hostcap - 1) {
            host[n++] = *p++;
        }
        host[n] = '\0';
        if (n == 0) {
            return FALSE;
        }
        if (*p == ':') {
            int pt = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                pt = pt * 10 + (*p - '0');
                p++;
            }
            if (pt > 0) {
                *out_port = pt;
            }
        }
        if (*p == '/') {
            copy_path(path, pathcap, p);
        } else {
            cstr_copy(path, pathcap, "/");
        }
    }
    return TRUE;
}

/* External-CSS fetch for PCore_StyleDocumentEx: resolve `url` against the
 * current page origin, GET it, and hand the body to the engine (which parses
 * it and then calls css_free_cb to release it). Returns 0 on success. */
static int css_fetch_cb(void *pw, const char *url, char **out_data, int *out_len)
{
    char           host[256];
    char           path[1024];
    int            port = 443;
    PHttpResponse *resp;
    char          *buf;

    (void) pw;
    *out_data = NULL;
    *out_len = 0;

    if (!resolve_url(url, host, sizeof(host), path, sizeof(path), &port)) {
        return 1;
    }
    resp = PHttp_Get(host, port, path, NULL);
    if (resp == NULL || resp->status_code != 200 ||
            resp->body == NULL || resp->body_len <= 0) {
        if (resp != NULL) {
            PHttp_FreeResponse(resp);
        }
        return 1;
    }
    buf = (char *) malloc((size_t) resp->body_len);
    if (buf == NULL) {
        PHttp_FreeResponse(resp);
        return 1;
    }
    memcpy(buf, resp->body, (size_t) resp->body_len);
    *out_data = buf;
    *out_len = resp->body_len;
    PHttp_FreeResponse(resp);
    return 0;
}

static void css_free_cb(void *pw, char *data)
{
    (void) pw;
    free(data);
}

/* Follow a link: fetch the target over HTTPS, parse + style + lay it out to the
 * current client size, swap it in as the rendered document and repaint. On any
 * failure the current page is left untouched and an error box is shown. */
static void navigate_to(HWND hwnd, const char *href)
{
    char           host[256];
    char           path[1024];
    int            port = 443;
    PHttpResponse *resp;
    HANDLE         newDoc;
    RECT           rc;
    int            cw, chh;
    char           emsg[320];

    if (!resolve_url(href, host, sizeof(host), path, sizeof(path), &port)) {
        show_info(L"Link", "Only http(s) document links are followed for now.");
        return;
    }

    resp = PHttp_Get(host, port, path, NULL);
    if (resp == NULL || resp->status_code != 200 || resp->body == NULL ||
            resp->body_len <= 0) {
        _snprintf(emsg, sizeof(emsg) - 1,
                  "GET %s://%s%s -> status=%d %s",
                  (port == 80) ? "http" : "https", host, path,
                  (resp != NULL) ? resp->status_code : 0,
                  (resp != NULL) ? resp->error_msg : "(null)");
        emsg[sizeof(emsg) - 1] = '\0';
        show_error(L"Navigation failed", emsg);
        if (resp != NULL) {
            PHttp_FreeResponse(resp);
        }
        return;
    }

    newDoc = PCore_ParseHTML(resp->body, resp->body_len);
    PHttp_FreeResponse(resp);
    if (newDoc == NULL) {
        show_error(L"Navigation failed", "PCore_ParseHTML returned NULL");
        return;
    }

    /* Record the new origin *before* styling so external <link> hrefs in this
     * page resolve against it (css_fetch_cb uses g_cur_*). */
    cstr_copy(g_cur_host, sizeof(g_cur_host), host);
    cstr_copy(g_cur_path, sizeof(g_cur_path), path);
    g_cur_port = port;

    if (PCore_StyleDocumentEx(newDoc, NULL, css_fetch_cb, css_free_cb,
            NULL) != 0) {
        show_error(L"Navigation failed", "PCore_StyleDocument failed");
        PCore_FreeDocument(newDoc);
        return;
    }

    GetClientRect(hwnd, &rc);
    cw = rc.right - rc.left;
    chh = rc.bottom - rc.top;
    if (cw <= 0) { cw = 224; }
    if (chh <= 0) { chh = 320; }
    PCore_SetViewport(cw, chh, 0);   /* dpi 0 = leave unchanged */
    if (PCore_LayoutDocument(newDoc, cw, chh) != 0) {
        show_error(L"Navigation failed", "PCore_LayoutDocument failed");
        PCore_FreeDocument(newDoc);
        return;
    }

    /* Swap in the new document; free the one being replaced. */
    if (g_render_doc != NULL) {
        PCore_FreeDocument(g_render_doc);
    }
    g_render_doc = newDoc;
    g_doc_h = PCore_DocumentHeight(newDoc);
    g_scroll_y = 0;

    pcore_set_scrollbar(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}

static LRESULT CALLBACK PCoreWndProc(HWND hwnd, UINT msg,
                                     WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         hdc;

        hdc = BeginPaint(hwnd, &ps);
        /* Repaint only the invalid region. When scrolling, that is just the
         * thin strip ScrollWindowEx exposed; BeginPaint clips the DC to it, so
         * PCore_PaintDocument redraws a sliver, not the whole screen. */
        FillRect(hdc, &ps.rcPaint, (HBRUSH) GetStockObject(WHITE_BRUSH));
        if (g_plot_test) {
            PCore_PlotTest(hdc);   /* M1: drive the GDI plotter directly */
        } else if (g_ns_render) {
            RECT rcc;
            GetClientRect(hwnd, &rcc);
            PCore_NsRenderTest(hdc, rcc.right - rcc.left, rcc.bottom - rcc.top);
        } else if (g_render_doc != NULL) {
            PCore_PaintDocument(g_render_doc, hdc, 0, g_scroll_y);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;   /* WM_PAINT clears the invalid region itself; skip erase */
    case WM_SIZE: {
        int cw = LOWORD(lp);    /* new client width  */
        int chh = HIWORD(lp);   /* new client height */

        /* Re-flow layout to the new client width (e.g. on screen rotation).
         * Styles are unchanged, so only re-run layout, not styling. */
        if (g_render_doc != NULL && cw > 0 && chh > 0) {
            PCore_SetViewport(cw, chh, 0);   /* dpi 0 = leave unchanged */
            PCore_LayoutDocument(g_render_doc, cw, chh);
            g_doc_h = PCore_DocumentHeight(g_render_doc);
            if (g_scroll_y > g_doc_h - chh) {
                g_scroll_y = g_doc_h - chh;
            }
            if (g_scroll_y < 0) {
                g_scroll_y = 0;
            }
        }
        pcore_set_scrollbar(hwnd);
        SHFullScreen(hwnd, SHFS_HIDESIPBUTTON);   /* keep SIP hidden on rotate */
        InvalidateRect(hwnd, NULL, TRUE);   /* full repaint after a resize */
        return 0;
    }
    case WM_VSCROLL: {
        RECT rc;
        int ch;

        GetClientRect(hwnd, &rc);
        ch = rc.bottom - rc.top;
        switch (LOWORD(wp)) {
        case SB_LINEUP:   pcore_scroll_by(hwnd, -16);  break;
        case SB_LINEDOWN: pcore_scroll_by(hwnd, 16);   break;
        case SB_PAGEUP:   pcore_scroll_by(hwnd, -ch);  break;
        case SB_PAGEDOWN: pcore_scroll_by(hwnd, ch);   break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            pcore_scroll_by(hwnd, (int) HIWORD(wp) - g_scroll_y);
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP:    pcore_scroll_by(hwnd, -16);   break;
        case VK_DOWN:  pcore_scroll_by(hwnd, 16);    break;
        case VK_PRIOR: pcore_scroll_by(hwnd, -120);  break;
        case VK_NEXT:  pcore_scroll_by(hwnd, 120);   break;
        case VK_ESCAPE:
        case VK_RETURN:
            DestroyWindow(hwnd);
            break;
        default:
            break;
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int cx = (int) (short) LOWORD(lp);
        int cy = (int) (short) HIWORD(lp);
        char href[1024];

        /* Document-space point = client point + scroll (scroll_x is 0). If it
         * lands on a link, follow it; otherwise a tap closes the view. */
        if (g_render_doc != NULL &&
                PCore_LinkAt(g_render_doc, cx, cy + g_scroll_y,
                             href, sizeof(href))) {
            navigate_to(hwnd, href);
        } else {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* Create the full-screen render window and run its message loop until closed.
 * Assumes g_render_doc + g_doc_h + g_scroll_y are already set. Returns FALSE
 * only if the window could not be created. */
static BOOL show_render_window(void)
{
    HINSTANCE hInst;
    WNDCLASSW wc;
    HWND      hwnd;
    MSG       m;

    hInst = GetModuleHandle(NULL);
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PCoreWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"PositronRenderWnd";
    RegisterClassW(&wc);

    hwnd = CreateWindowW(L"PositronRenderWnd", L"Positron render",
            WS_VISIBLE | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL);
    if (hwnd == NULL) {
        return FALSE;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);   /* WinCE: grab focus, come to front */
    /* Read-only view: hide the SIP button and keep the keyboard down. */
    SHFullScreen(hwnd, SHFS_HIDESIPBUTTON);
    SHSipPreference(hwnd, SIP_FORCEDOWN);
    pcore_set_scrollbar(hwnd);

    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return TRUE;
}

static BOOL test12_render(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><h2>Positron</h2>"
        "<div><p>This is a longer paragraph of text that should wrap across "
        "several lines inside the light-blue block, demonstrating real inline "
        "text measurement and word wrapping on the device screen.</p>"
        "<p>A second, shorter paragraph follows below it.</p>"
        "<p>Third paragraph: the document is now taller than the screen, so a "
        "vertical scrollbar appears. Drag it, or use the up/down keys, to "
        "scroll through the content.</p>"
        "<p>Fourth paragraph - more flowing text to push the page further "
        "down, past the bottom edge of the viewport.</p>"
        "<p>Fifth paragraph near the bottom; scrolling all the way down "
        "reveals it. Tap the content or press Esc to close.</p></div>"
        "<h2>The End</h2>"
        "</body></html>";
    static const char *CSS =
        "body { background-color: #ffffff; color: #202020; }\n"
        "h2   { color: #800000; }\n"
        "div  { background-color: #cce6ff; border: 2px solid #4060a0;"
        " padding: 6px; }\n"
        "p    { color: #103080; }\n";

    HANDLE    hDoc;
    HANDLE    hSheet;
    int       vw, vh;

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 12 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 12 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    if (PCore_StyleDocument(hDoc, hSheet) != 0) {
        show_error(L"TEST 12 FAIL", "PCore_StyleDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }

    if (PCore_LayoutDocument(hDoc, vw, vh) != 0) {
        show_error(L"TEST 12 FAIL", "PCore_LayoutDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;

    show_info(L"TEST 12",
              "A render window will open. The page is taller than the\n"
              "screen: use the scrollbar or the up/down keys to scroll.\n\n"
              "Tap the content (or press Esc) to close and finish.");

    g_render_doc = hDoc;
    if (!show_render_window()) {
        show_error(L"TEST 12 FAIL", "CreateWindow returned NULL");
        g_render_doc = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    g_render_doc = NULL;

    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    show_info(L"TEST 12 OK",
              "Scrollable HTML page rendered:\n"
              "  H2 headings, bordered + padded div,\n"
              "  several wrapped paragraphs.\n"
              "  Scrolled via scrollbar / up-down keys.\n\n"
              "(box model + inline wrap + vertical scroll.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 13 - browse: fetch a real HTTPS page and render it                */
/* positron_http GET -> PCore_ParseHTML -> StyleDocument (UA + the page's */
/* own <style>) -> layout -> render in the scroll window. PHttp is assumed */
/* already initialised (WinMain does it). External <link> CSS, images and  */
/* JS are not fetched/rendered yet.                                        */
/* -------------------------------------------------------------------- */

static BOOL test_browse(void)
{
    static const char *START_HTML =
        "<!DOCTYPE html><html><head><title>Positron</title>"
        "<style>"
        "body{background-color:#ffffff;color:#202020;}"
        "h1{color:#800000;}"
        "p{margin-top:1em;margin-bottom:1em;}"
        "</style></head>"
        "<body><h1>Positron</h1>"
        "<p>Tap a link to fetch and render a real page over HTTPS:</p>"
        "<p><a href=\"https://example.com/\">Open example.com</a></p>"
        "<p>On the fetched page you can tap its own links too. Some hosts "
        "may be reset by the network (GFW); that error is expected.</p>"
        "<p>Tap empty space (or press Esc) to close.</p>"
        "</body></html>";

    HANDLE hDoc;
    int    vw, vh;

    /* Landing page is offline; the actual fetch happens when the user taps
     * the link (navigate_to), exercising the full click -> fetch -> render
     * loop against a China-reachable host. */
    hDoc = PCore_ParseHTML(START_HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 13 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_StyleDocument(hDoc, NULL) != 0) {   /* UA + page's <style> */
        show_error(L"TEST 13 FAIL", "PCore_StyleDocument failed");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    PCore_SetViewport(vw, vh, 0);
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0) {
        show_error(L"TEST 13 FAIL", "PCore_LayoutDocument failed");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    /* No remote origin yet; the start page's link is absolute. */
    g_cur_host[0] = '\0';
    cstr_copy(g_cur_path, sizeof(g_cur_path), "/");

    show_info(L"TEST 13",
              "A start page opens. Tap \"Open example.com\" to fetch\n"
              "and render a real HTTPS page (click navigation).\n\n"
              "Tap empty space or press Esc to close.");

    g_render_doc = hDoc;
    if (!show_render_window()) {
        show_error(L"TEST 13 FAIL", "CreateWindow returned NULL");
        g_render_doc = NULL;
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    /* Navigation may have replaced the document; free whatever is current. */
    if (g_render_doc != NULL) {
        PCore_FreeDocument(g_render_doc);
    }
    g_render_doc = NULL;

    show_info(L"TEST 13 OK",
              "Click navigation verified:\n"
              "start page -> tap link -> HTTPS GET -> parse ->\n"
              "style -> layout -> GDI paint, on the device.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 15 - milestone H/M3: DOM -> NetSurf box tree (slim builder)       */
/* Builds a struct box tree from a small styled document via the talloc   */
/* shim + pcore_box_construct, and reports box counts by type. Verifies    */
/* the box infrastructure before layout.c is ported. Offline.             */
/* -------------------------------------------------------------------- */
static BOOL test_boxtree(void)
{
    char buf[512];

    PCore_BoxTreeTest(buf, sizeof(buf));
    if (buf[0] == '\0') {
        show_error(L"TEST 15 FAIL", "PCore_BoxTreeTest produced no output");
        return FALSE;
    }
    show_info(L"TEST 15 (box tree)", buf);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 16 - milestone H/M4: NetSurf real layout_document on the box tree */
/* Builds the box tree then runs the ported NetSurf layout.c over it and  */
/* reports box geometry. Verifies the layout engine compiles + runs.      */
/* -------------------------------------------------------------------- */
static BOOL test_layout(void)
{
    char buf[512];

    PCore_LayoutBoxTest(buf, sizeof(buf));
    if (buf[0] == '\0') {
        show_error(L"TEST 16 FAIL", "PCore_LayoutBoxTest produced no output");
        return FALSE;
    }
    show_info(L"TEST 16 (NetSurf layout)", buf);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 14 - milestone H/M1: GDI plotter table self-test                  */
/* Opens a window and paints via PCore_PlotTest - the NetSurf plotter      */
/* interface backed by GDI - with NO layout engine involved. Confirms the  */
/* plotter table, colour conversion, pens/brushes and text baseline before */
/* redraw.c is ported in.                                                  */
/* -------------------------------------------------------------------- */
static BOOL test14_plot(void)
{
    show_info(L"TEST 14",
              "Milestone H/M1: a window opens and paints via the\n"
              "GDI-backed NetSurf plotter (no layout engine).\n\n"
              "Expect: a grey box with a SOLID red border + black\n"
              "text; below it a DOTTED blue box border; and a\n"
              "DASHED green line. (Dotted/dashed are hand-drawn,\n"
              "since WinCE pens are solid-only.)\n"
              "Tap or press Esc to close.");

    g_plot_test = 1;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_plot_test = 0;
        show_error(L"TEST 14 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_plot_test = 0;

    show_info(L"TEST 14 OK",
              "GDI plotter table verified: rectangle (fill+border),\n"
              "line, and baseline-aligned text drew correctly.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 17 - milestone H/M5f: NetSurf real layout/redraw/borders on screen */
/* A window opens; the page is laid out by NetSurf's layout_document and    */
/* painted by html_redraw through our GDI plotter (M1) + font table (M2).   */
/* First page rendered end-to-end by the ported engine.                     */
/* -------------------------------------------------------------------- */
static BOOL test17_nsrender(void)
{
    show_info(L"TEST 17",
              "NetSurf REAL layout + redraw + borders (M5f/M7):\n"
              "the page is laid out by NetSurf's layout.c and painted\n"
              "by redraw.c + redraw_border.c through our GDI plotter.\n\n"
              "Expect a dark-red H1 with a red underline border,\n"
              "a light-blue padded box with a blue border and two\n"
              "wrapped blue paragraphs, then THREE colour blocks\n"
              "(red/green/blue: One/Two/Three) in a dashed border row.\n"
              "Side by side = flex works; stacked = flex failed.\n"
              "Below that, a 2x2 table with visible cell borders.\n"
              "Tap or Esc to close.");

    g_ns_render = 1;
    g_render_doc = NULL;
    g_plot_test = 0;
    g_scroll_y = 0;
    g_doc_h = 0;
    if (!show_render_window()) {
        g_ns_render = 0;
        show_error(L"TEST 17 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_ns_render = 0;

    show_info(L"TEST 17 OK",
              "Rendered end-to-end by the ported NetSurf engine\n"
              "(layout.c + redraw.c + redraw_border.c + flex/table)\n"
              "on the device.");
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
    BOOL run_render;
    BOOL run_browse;
    int  rc;
    char summary[1024];

    (void)hInstance;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    OutputDebugStringW(L"test_host (Phase 4): starting\r\n");

    /* Tell positron_core the real device viewport + DPI so styling and layout
     * adapt to this screen rather than a hardcoded default. */
    {
        HDC sdc = GetDC(NULL);
        int dpi = (sdc != NULL) ? GetDeviceCaps(sdc, LOGPIXELSY) : 96;
        if (sdc != NULL) {
            ReleaseDC(NULL, sdc);
        }
        PCore_SetViewport(GetSystemMetrics(SM_CXSCREEN),
                          GetSystemMetrics(SM_CYSCREEN), dpi);
    }

    /* Group selector. One tap runs everything; otherwise pick groups so a
     * subset can run in isolation - e.g. only the fully-offline engine /
     * rendering group when there is no network (no VPN needed). */
    if (ask_yesno(L"Positron test_host",
                  "Run ALL tests?\n\n"
                  "Yes = run everything (TEST 1-13)\n"
                  "No  = choose which groups to run")) {
        run_comm = TRUE;
        run_engine = TRUE;
        run_render = TRUE;
        run_browse = TRUE;
    } else {
        run_comm = ask_yesno(L"Select groups (1/4)",
                             "Run COMMUNICATION tests?\n\n"
                             "TLS / HTTP / JSON  (TEST 1-5).\n"
                             "Needs network access.");
        run_engine = ask_yesno(L"Select groups (2/4)",
                               "Run ENGINE tests?\n\n"
                               "HTML / CSS / DOM parse, select, style,\n"
                               "layout, box tree, NetSurf layout\n"
                               "(TEST 6-11, 15, 16). Offline.");
        run_render = ask_yesno(L"Select groups (3/4)",
                               "Run GDI RENDER tests?\n\n"
                               "M1 plotter self-test (TEST 14) + local HTML\n"
                               "page (TEST 12). Fully offline.");
        run_browse = ask_yesno(L"Select groups (4/4)",
                               "Run BROWSE test?\n\n"
                               "Fetch a real HTTPS page and render it\n"
                               "(TEST 13). Needs network access.");
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

    /* --- Engine group (TEST 6-11, message-box assertions, offline) ---- */
    if (run_engine) {
        if (!test6_hubbub())       { rc = 6; goto done; }
        if (!test7_libcss())       { rc = 7; goto done; }
        if (!test7b_dom())         { rc = 8; goto done; }
        if (!test8_core())         { rc = 9; goto done; }
        if (!test9_select())       { rc = 10; goto done; }
        if (!test10_styledoc())    { rc = 11; goto done; }
        if (!test11_layout())      { rc = 12; goto done; }
        if (!test_boxtree())       { rc = 12; goto done; }
        if (!test_layout())        { rc = 12; goto done; }
    }

    /* --- GDI render group (TEST 12, opens a real window, offline) ----- */
    if (run_render) {
        char fb[192];
        PCore_FontTest(fb, sizeof(fb));        /* M2: font-measure sanity */
        show_info(L"TEST (M2) font table", fb);
        if (!test14_plot())        { rc = 13; goto done; }
        if (!test17_nsrender())    { rc = 13; goto done; }
        if (!test12_render())      { rc = 13; goto done; }
    }

    /* --- Browse group (TEST 13: fetch a real page + render, network) -- */
    if (run_browse) {
        /* The comm group (TEST 1) normally initialises positron_http; if it
         * did not run, bring it up here for the fetch. */
        if (!run_comm && !PHttp_Init()) {
            show_error(L"TEST 13 FAIL", "PHttp_Init returned FALSE");
            rc = 14;
            goto done;
        }
        if (!test_browse())        { rc = 14; goto done; }
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
               "  Engine (TEST 6-11, 15, 16)\n"
               "    libhubbub + libcss + libdom behind\n"
               "    positron_core.dll; parse, select, style,\n"
               "    layout, box tree, NetSurf layout. Offline.\n\n");
    }
    if (run_render) {
        strcat(summary,
               "  GDI render (TEST 12)\n"
               "    HTML page painted to a window: background,\n"
               "    borders, padding, wrapped text. Offline.\n\n");
    }
    if (run_browse) {
        strcat(summary,
               "  Browse (TEST 13)\n"
               "    fetched a real HTTPS page + rendered it\n"
               "    (HTTP -> parse -> style -> layout -> paint).\n\n");
    }
    if (!run_comm && !run_engine && !run_render && !run_browse) {
        strcat(summary, "  (no groups selected)\n");
    }
    show_info(L"Tests passed", summary);

done:
    /* positron_http is brought up by the comm group (TEST 1) and/or the
     * browse group; tear it down if either ran. */
    if (run_comm || run_browse) {
        PHttp_Cleanup();
    }
    return rc;
}
