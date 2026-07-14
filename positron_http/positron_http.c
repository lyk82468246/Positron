/*
 * positron_http.c - HTTP/1.1 client over positron_tls.
 *
 * Lives on top of positron_tls (linked directly via .lib import).
 * Plain HTTP uses WM WinInet; modern HTTPS uses positron_tls.
 * No keep-alive in this phase (Connection: close).
 *
 * C89 only: no slash-slash comments, no mid-block declarations.
 */

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wininet.h>   /* WM6 built-in HTTP: used for plaintext http:// */

#include "positron_tls.h"
#include "positron_http.h"

#define MAX_RESP_BODY    (1 * 1024 * 1024)   /* 1 MB cap */
#define INITIAL_BUFCAP   8192
#define MAX_REDIRECTS    5                    /* 3xx Location follow limit */
#define WININET_CONNECT_TIMEOUT_MS 15000
#define WININET_IO_TIMEOUT_MS      15000

static BOOL g_initialized = FALSE;
static BOOL g_insecure    = FALSE;   /* default: verify chain + hostname */

static void report_progress(PHttpProgressCallback progress, void* user_data,
                            int received, int total)
{
    if (progress != NULL) {
        progress(user_data, received, total);
    }
}

/* ------------------------------------------------------------------- */
/* DllMain                                                              */
/* ------------------------------------------------------------------- */

BOOL WINAPI DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)hModule;
    (void)lpReserved;
    (void)reason;
    return TRUE;
}

/* ------------------------------------------------------------------- */
/* Init / Cleanup                                                       */
/* ------------------------------------------------------------------- */

PHTTP_API BOOL PHttp_Init(void)
{
    if (g_initialized) {
        return TRUE;
    }
    if (!PTls_Init()) {
        return FALSE;
    }
    g_initialized = TRUE;
    return TRUE;
}

PHTTP_API void PHttp_Cleanup(void)
{
    if (g_initialized) {
        PTls_Cleanup();
        g_initialized = FALSE;
    }
}

PHTTP_API BOOL PHttp_SetInsecure(BOOL insecure)
{
    BOOL prev = g_insecure;
    g_insecure = insecure ? TRUE : FALSE;
    return prev;
}

/* ------------------------------------------------------------------- */
/* Helpers                                                              */
/* ------------------------------------------------------------------- */

static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    }
    return c;
}

/* Case-insensitive byte-compare, length-limited.
 * Returns 0 iff first n bytes match (case-insensitive). */
static int ci_memcmp(const char* a, const char* b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = ascii_tolower((unsigned char)a[i]);
        int cb = ascii_tolower((unsigned char)b[i]);
        if (ca != cb) {
            return ca - cb;
        }
    }
    return 0;
}

/* ---- bytebuf: growable heap byte buffer --------------------------- */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} bytebuf;

static int bb_init(bytebuf* b)
{
    b->data = (char*)HeapAlloc(GetProcessHeap(), 0, INITIAL_BUFCAP);
    if (b->data == NULL) {
        b->len = 0;
        b->cap = 0;
        return -1;
    }
    b->data[0] = '\0';
    b->len = 0;
    b->cap = INITIAL_BUFCAP;
    return 0;
}

static int bb_append(bytebuf* b, const char* src, size_t n)
{
    size_t newcap;
    char*  tmp;
    if (b->data == NULL) {
        return -1;
    }
    if (b->len + n + 1 > b->cap) {
        newcap = b->cap;
        while (newcap < b->len + n + 1) {
            newcap *= 2;
            if (newcap > MAX_RESP_BODY + 65536) {
                return -1;
            }
        }
        tmp = (char*)HeapReAlloc(GetProcessHeap(), 0, b->data, newcap);
        if (tmp == NULL) {
            return -1;
        }
        b->data = tmp;
        b->cap = newcap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static void bb_free(bytebuf* b)
{
    if (b->data != NULL) {
        HeapFree(GetProcessHeap(), 0, b->data);
        b->data = NULL;
    }
    b->len = 0;
    b->cap = 0;
}

/* ---- response object --------------------------------------------- */

static PHttpResponse* resp_new(void)
{
    PHttpResponse* r;
    r = (PHttpResponse*)HeapAlloc(GetProcessHeap(),
                                  HEAP_ZERO_MEMORY,
                                  sizeof(PHttpResponse));
    return r;
}

static void resp_set_error(PHttpResponse* r, const char* msg)
{
    if (r == NULL || msg == NULL) {
        return;
    }
    _snprintf(r->error_msg, sizeof(r->error_msg) - 1, "%s", msg);
    r->error_msg[sizeof(r->error_msg) - 1] = '\0';
}

static void resp_set_effective_url(PHttpResponse* r, const char* host,
                                   int port, const char* path)
{
    const char* scheme;
    int n;

    if (r == NULL || host == NULL || path == NULL) {
        return;
    }
    scheme = (port == 80) ? "http" : "https";
    if (port == 80 || port == 443) {
        n = _snprintf(r->effective_url, sizeof(r->effective_url) - 1,
                      "%s://%s%s", scheme, host, path);
    } else {
        n = _snprintf(r->effective_url, sizeof(r->effective_url) - 1,
                      "%s://%s:%d%s", scheme, host, port, path);
    }
    if (n < 0 || n >= (int)sizeof(r->effective_url) - 1) {
        r->effective_url[0] = '\0';
    } else {
        r->effective_url[sizeof(r->effective_url) - 1] = '\0';
    }
}

/* ---- request building -------------------------------------------- */

static char* build_request(const char* method, const char* host,
                           const char* path, const char** headers,
                           const char* body, int body_len)
{
    bytebuf b;
    char    cl_line[48];
    int     i;
    int     cl_len;

    if (bb_init(&b) != 0) {
        return NULL;
    }

    /* request line */
    if (bb_append(&b, method, strlen(method)) != 0) goto fail;
    if (bb_append(&b, " ", 1) != 0) goto fail;
    if (bb_append(&b, path, strlen(path)) != 0) goto fail;
    if (bb_append(&b, " HTTP/1.1\r\n", 11) != 0) goto fail;

    /* Host */
    if (bb_append(&b, "Host: ", 6) != 0) goto fail;
    if (bb_append(&b, host, strlen(host)) != 0) goto fail;
    if (bb_append(&b, "\r\n", 2) != 0) goto fail;

    /* fixed headers */
    if (bb_append(&b, "Connection: close\r\n", 19) != 0) goto fail;
    if (bb_append(&b, "User-Agent: Positron/0.2 (WinCE)\r\n", 34) != 0) goto fail;
    if (bb_append(&b, "Accept-Encoding: identity\r\n", 27) != 0) goto fail;

    /* user-supplied headers */
    if (headers != NULL) {
        for (i = 0; headers[i] != NULL; i++) {
            if (bb_append(&b, headers[i], strlen(headers[i])) != 0) goto fail;
            if (bb_append(&b, "\r\n", 2) != 0) goto fail;
        }
    }

    /* Content-Length (POST only; body_len == 0 also legal) */
    if (body != NULL) {
        cl_len = _snprintf(cl_line, sizeof(cl_line),
                           "Content-Length: %d\r\n", body_len);
        if (cl_len < 0) goto fail;
        if (bb_append(&b, cl_line, (size_t)cl_len) != 0) goto fail;
    }

    /* header/body separator */
    if (bb_append(&b, "\r\n", 2) != 0) goto fail;

    /* body */
    if (body != NULL && body_len > 0) {
        if (bb_append(&b, body, (size_t)body_len) != 0) goto fail;
    }

    return b.data;   /* transfer ownership to caller */

fail:
    bb_free(&b);
    return NULL;
}

/* ---- header parsing ---------------------------------------------- */

/* Parse status code from "HTTP/1.1 NNN reason..." */
static int parse_status(const char* line, size_t hlen)
{
    size_t i;
    int    code;
    code = 0;
    for (i = 0; i < hlen; i++) {
        if (line[i] == ' ') {
            i++;
            while (i < hlen && line[i] >= '0' && line[i] <= '9') {
                code = code * 10 + (line[i] - '0');
                i++;
            }
            return code;
        }
    }
    return 0;
}

/* Find header value (case-insensitive). Returns pointer into headers
 * or NULL. Length of value is returned in *out_len. */
static const char* find_header(const char* headers, size_t hlen,
                               const char* name, size_t* out_len)
{
    size_t name_len;
    size_t i;
    size_t line_start;
    size_t value_start;

    name_len = strlen(name);
    *out_len = 0;

    /* skip status line */
    for (i = 0; i + 1 < hlen; i++) {
        if (headers[i] == '\r' && headers[i + 1] == '\n') {
            i += 2;
            break;
        }
    }

    while (i < hlen) {
        line_start = i;
        if (i + name_len + 1 > hlen) {
            break;
        }
        if (ci_memcmp(headers + i, name, name_len) == 0
            && headers[i + name_len] == ':') {
            value_start = i + name_len + 1;
            while (value_start < hlen
                   && (headers[value_start] == ' '
                       || headers[value_start] == '\t')) {
                value_start++;
            }
            i = value_start;
            while (i + 1 < hlen
                   && !(headers[i] == '\r' && headers[i + 1] == '\n')) {
                i++;
            }
            *out_len = i - value_start;
            return headers + value_start;
        }
        /* move to next line */
        while (i + 1 < hlen
               && !(headers[i] == '\r' && headers[i + 1] == '\n')) {
            i++;
        }
        if (i + 1 >= hlen) {
            break;
        }
        i += 2;
        if (i == line_start) {
            break;
        }
    }
    return NULL;
}

static int parse_content_length(const char* headers, size_t hlen)
{
    const char* v;
    size_t      vlen;
    char        tmp[32];

    v = find_header(headers, hlen, "Content-Length", &vlen);
    if (v == NULL || vlen == 0 || vlen >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, v, vlen);
    tmp[vlen] = '\0';
    return (int)strtol(tmp, NULL, 10);
}

static BOOL is_chunked(const char* headers, size_t hlen)
{
    const char* v;
    size_t      vlen;

    v = find_header(headers, hlen, "Transfer-Encoding", &vlen);
    if (v == NULL) {
        return FALSE;
    }
    /* value contains "chunked" (possibly with other codings) */
    if (vlen >= 7 && ci_memcmp(v, "chunked", 7) == 0) {
        return TRUE;
    }
    {
        size_t i;
        for (i = 0; i + 7 <= vlen; i++) {
            if (ci_memcmp(v + i, "chunked", 7) == 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* ---- read helpers ------------------------------------------------ */

/* Read until "\r\n\r\n" sequence found in buf. Returns index of byte
 * AFTER the terminator (i.e. start of body), or -1 on error. */
static int read_until_headers(HANDLE conn, bytebuf* buf)
{
    char tmp[1024];
    int  n;
    size_t i;

    while (1) {
        if (buf->len >= 4) {
            for (i = 0; i + 3 < buf->len; i++) {
                if (buf->data[i] == '\r' && buf->data[i + 1] == '\n'
                    && buf->data[i + 2] == '\r' && buf->data[i + 3] == '\n') {
                    return (int)(i + 4);
                }
            }
        }
        if (buf->len > 32768) {
            return -1;   /* header block unreasonably large */
        }
        n = PTls_Read(conn, tmp, (int)sizeof(tmp));
        if (n <= 0) {
            return -1;
        }
        if (bb_append(buf, tmp, (size_t)n) != 0) {
            return -1;
        }
    }
}

/* Decode chunked-encoded body. `prefix`/`prefix_len` is whatever
 * chunked bytes were already present in the receive buffer past the
 * header terminator. Reads further from conn as needed. Appends
 * decoded bytes to out_body. */
static int decode_chunked(HANDLE conn,
                          const char* prefix, int prefix_len,
                          bytebuf* out_body,
                          PHttpProgressCallback progress,
                          void* user_data)
{
    bytebuf raw;
    size_t  pos;
    char    tmp[2048];
    int     n;
    size_t  chunk_size;
    const char* nl;
    size_t  i;
    char    sizebuf[24];

    if (bb_init(&raw) != 0) {
        return -1;
    }
    if (prefix_len > 0) {
        if (bb_append(&raw, prefix, (size_t)prefix_len) != 0) {
            bb_free(&raw);
            return -1;
        }
    }
    pos = 0;

    while (1) {
        /* find \r\n marking end of chunk-size line */
        nl = NULL;
        for (i = pos; i + 1 < raw.len; i++) {
            if (raw.data[i] == '\r' && raw.data[i + 1] == '\n') {
                nl = raw.data + i;
                break;
            }
        }
        if (nl == NULL) {
            n = PTls_Read(conn, tmp, (int)sizeof(tmp));
            if (n <= 0) {
                bb_free(&raw);
                return -1;
            }
            if (bb_append(&raw, tmp, (size_t)n) != 0) {
                bb_free(&raw);
                return -1;
            }
            continue;
        }

        /* extract size (hex, possibly with ;ext) */
        {
            size_t size_str_len = (size_t)(nl - (raw.data + pos));
            if (size_str_len >= sizeof(sizebuf)) {
                bb_free(&raw);
                return -1;
            }
            memcpy(sizebuf, raw.data + pos, size_str_len);
            sizebuf[size_str_len] = '\0';
            /* truncate at ';' if chunk-ext present */
            {
                char* semi = strchr(sizebuf, ';');
                if (semi != NULL) {
                    *semi = '\0';
                }
            }
            chunk_size = (size_t)strtoul(sizebuf, NULL, 16);
        }
        pos = (size_t)(nl - raw.data) + 2;

        if (chunk_size == 0) {
            /* terminating chunk; we are done */
            bb_free(&raw);
            return 0;
        }

        if (out_body->len + chunk_size > MAX_RESP_BODY) {
            bb_free(&raw);
            return -1;
        }

        /* ensure we have chunk_size + 2 (trailing \r\n) bytes past pos */
        while (raw.len - pos < chunk_size + 2) {
            n = PTls_Read(conn, tmp, (int)sizeof(tmp));
            if (n <= 0) {
                bb_free(&raw);
                return -1;
            }
            if (bb_append(&raw, tmp, (size_t)n) != 0) {
                bb_free(&raw);
                return -1;
            }
        }

        if (bb_append(out_body, raw.data + pos, chunk_size) != 0) {
            bb_free(&raw);
            return -1;
        }
        report_progress(progress, user_data, (int)out_body->len, -1);
        pos += chunk_size + 2;   /* skip data + trailing \r\n */
    }
}

/* ---- redirect handling ------------------------------------------- */

static int is_redirect_code(int c)
{
    return (c == 301 || c == 302 || c == 303 || c == 307 || c == 308);
}

/* Bounded NUL-terminated copy. */
static void cstrcpy(char* d, int cap, const char* s)
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

/* Copy a request path, stopping at any '#' fragment; fall back to "/". */
static void copy_path_h(char* d, int cap, const char* s)
{
    int n = 0;
    if (cap <= 0) {
        return;
    }
    while (s[n] != '\0' && s[n] != '#' && n < cap - 1) {
        d[n] = s[n];
        n++;
    }
    if (n == 0 && cap > 1) {
        d[n++] = '/';
    }
    d[n] = '\0';
}

/* Case-insensitive ASCII prefix test over a counted string. */
static int ci_prefix(const char* s, size_t slen, const char* pfx)
{
    size_t n = strlen(pfx);
    size_t i;
    if (slen < n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char a = s[i], b = pfx[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

/* Resolve a Location header value (loc/loclen, not NUL-terminated) against the
 * current host/path/port into out_host/out_path/out_port. Handles absolute
 * http(s) (with optional :port), root-relative ("/x") and same-directory
 * relative ("x"). Returns 1 on success, 0 if it cannot be parsed. */
static int resolve_redirect(const char* loc, size_t loclen,
                            const char* cur_host, const char* cur_path,
                            int cur_port,
                            char* out_host, int hostcap,
                            char* out_path, int pathcap, int* out_port)
{
    char        buf[1200];
    const char* p;
    int         scheme_port = cur_port;

    while (loclen > 0 && (loc[loclen - 1] == '\r' || loc[loclen - 1] == '\n' ||
                          loc[loclen - 1] == ' '  || loc[loclen - 1] == '\t')) {
        loclen--;
    }
    if (loclen == 0 || loclen >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, loc, loclen);
    buf[loclen] = '\0';
    p = buf;

    if (ci_prefix(p, strlen(p), "https://")) {
        p += 8;
        scheme_port = 443;
    } else if (ci_prefix(p, strlen(p), "http://")) {
        p += 7;
        scheme_port = 80;
    } else if (p[0] == '/') {
        cstrcpy(out_host, hostcap, cur_host);
        copy_path_h(out_path, pathcap, p);
        *out_port = cur_port;
        return 1;
    } else {
        /* same-directory relative: current host + base dir + target */
        int i, lastslash = -1, k = 0;
        cstrcpy(out_host, hostcap, cur_host);
        for (i = 0; cur_path[i] != '\0'; i++) {
            if (cur_path[i] == '/') {
                lastslash = i;
            }
        }
        for (i = 0; i <= lastslash && k < pathcap - 1; i++) {
            out_path[k++] = cur_path[i];
        }
        if (lastslash < 0 && k < pathcap - 1) {
            out_path[k++] = '/';
        }
        for (i = 0; p[i] != '\0' && p[i] != '#' && k < pathcap - 1; i++) {
            out_path[k++] = p[i];
        }
        out_path[k] = '\0';
        *out_port = cur_port;
        return 1;
    }

    /* Absolute: p now points at host[:port][/path]. */
    {
        int    k = 0;
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/' && p[n] != ':' && k < hostcap - 1) {
            out_host[k++] = p[n++];
        }
        out_host[k] = '\0';
        if (k == 0) {
            return 0;
        }
        *out_port = scheme_port;
        if (p[n] == ':') {
            int port = 0;
            n++;
            while (p[n] >= '0' && p[n] <= '9') {
                port = port * 10 + (p[n] - '0');
                n++;
            }
            if (port > 0) {
                *out_port = port;
            }
        }
        if (p[n] == '/') {
            copy_path_h(out_path, pathcap, p + n);
        } else {
            cstrcpy(out_path, pathcap, "/");
        }
    }
    return 1;
}

/* ---- plaintext HTTP via WinInet (WM6 built-in) ------------------- */

/* Fetch a plain http:// resource using WM6's own WinInet stack rather than a
 * hand-rolled socket/HTTP path (Positron patches WM6, it does not reinvent its
 * networking). HTTPS stays on mbedTLS: WinInet's SChannel is stuck on
 * SSL3/TLS1.0 + old ciphers and cannot reach modern sites, which is the gap
 * positron_tls exists to fill. Auto-redirect is disabled so the caller's loop
 * keeps control of cross-scheme redirects (an http->https hop must switch back
 * to mbedTLS). Writes *out_status, the Location header (UTF-8) into out_loc,
 * and appends the body to *outbody. Returns 0 on success, non-zero on a
 * transport error (with resp's error_msg set). */
static int wininet_fetch(const char* method, const char* host, int port,
                         const char* path, const char** headers,
                         const char* body, int body_len,
                         int* out_status, char* out_loc, int loc_cap,
                         bytebuf* outbody, PHttpResponse* resp,
                         PHttpProgressCallback progress, void* user_data)
{
    HINTERNET hInet = NULL;
    HINTERNET hConn = NULL;
    HINTERNET hReq  = NULL;
    WCHAR     whost[256];
    WCHAR     wpath[1024];
    WCHAR     wmethod[8];
    DWORD     flags;
    DWORD     code;
    DWORD     sz;
    DWORD     content_length;
    int       total;
    int       rc = 1;

    out_loc[0] = '\0';
    *out_status = 0;

    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 8);

    hInet = InternetOpenW(L"Positron", INTERNET_OPEN_TYPE_PRECONFIG,
                          NULL, NULL, 0);
    if (hInet == NULL) {
        resp_set_error(resp, "InternetOpen failed");
        goto wdone;
    }
    {
        DWORD connect_timeout;
        DWORD io_timeout;
        DWORD retries;

        connect_timeout = WININET_CONNECT_TIMEOUT_MS;
        io_timeout = WININET_IO_TIMEOUT_MS;
        retries = 1;
        InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT,
                &connect_timeout, sizeof(connect_timeout));
        InternetSetOption(hInet, INTERNET_OPTION_CONNECT_RETRIES,
                &retries, sizeof(retries));
        InternetSetOption(hInet, INTERNET_OPTION_CONTROL_RECEIVE_TIMEOUT,
                &io_timeout, sizeof(io_timeout));
        InternetSetOption(hInet, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT,
                &io_timeout, sizeof(io_timeout));
        InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT,
                &io_timeout, sizeof(io_timeout));
    }

    hConn = InternetConnectW(hInet, whost, (INTERNET_PORT)port, NULL, NULL,
                             INTERNET_SERVICE_HTTP, 0, 0);
    if (hConn == NULL) {
        char eb[160];
        _snprintf(eb, sizeof(eb) - 1, "%s:%d (http): InternetConnect err=%lu",
                  host, port, (unsigned long)GetLastError());
        eb[sizeof(eb) - 1] = '\0';
        resp_set_error(resp, eb);
        goto wdone;
    }

    flags = INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_RELOAD;
    hReq = HttpOpenRequestW(hConn, wmethod, wpath, NULL, NULL, NULL, flags, 0);
    if (hReq == NULL) {
        resp_set_error(resp, "HttpOpenRequest failed");
        goto wdone;
    }

    if (headers != NULL) {
        int i;
        for (i = 0; headers[i] != NULL; i++) {
            WCHAR wh[512];
            MultiByteToWideChar(CP_UTF8, 0, headers[i], -1, wh, 512);
            HttpAddRequestHeadersW(hReq, wh, (DWORD)-1,
                    HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
        }
    }

    if (!HttpSendRequestW(hReq, NULL, 0, (LPVOID)body,
                          (DWORD)(body != NULL ? body_len : 0))) {
        char eb[160];
        _snprintf(eb, sizeof(eb) - 1, "%s:%d (http): HttpSendRequest err=%lu",
                  host, port, (unsigned long)GetLastError());
        eb[sizeof(eb) - 1] = '\0';
        resp_set_error(resp, eb);
        goto wdone;
    }

    code = 0;
    sz = sizeof(code);
    HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &code, &sz, NULL);
    *out_status = (int)code;

    {
        WCHAR wloc[1024];
        DWORD lsz = sizeof(wloc);
        if (HttpQueryInfoW(hReq, HTTP_QUERY_LOCATION, wloc, &lsz, NULL)) {
            WideCharToMultiByte(CP_UTF8, 0, wloc, -1, out_loc, loc_cap,
                                NULL, NULL);
            out_loc[loc_cap - 1] = '\0';
        }
    }

    content_length = 0;
    sz = sizeof(content_length);
    total = -1;
    if (HttpQueryInfoW(hReq,
            HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
            &content_length, &sz, NULL)) {
        total = (content_length <= 0x7fffffffUL) ?
                (int)content_length : -1;
    }
    report_progress(progress, user_data, 0, total);

    {
        char  tmp[2048];
        DWORD got;
        for (;;) {
            if (!InternetReadFile(hReq, tmp, sizeof(tmp), &got)) {
                break;
            }
            if (got == 0) {
                break;
            }
            if (bb_append(outbody, tmp, (size_t)got) != 0) {
                break;
            }
            report_progress(progress, user_data, (int)outbody->len, total);
            if (outbody->len >= MAX_RESP_BODY) {
                break;
            }
        }
    }

    rc = 0;

wdone:
    if (hReq != NULL) {
        InternetCloseHandle(hReq);
    }
    if (hConn != NULL) {
        InternetCloseHandle(hConn);
    }
    if (hInet != NULL) {
        InternetCloseHandle(hInet);
    }
    return rc;
}

/* ---- worker ------------------------------------------------------ */

static PHttpResponse* http_request(const char* method, const char* host,
                                   int port, const char* path,
                                   const char** headers,
                                   const char* body, int body_len,
                                   PHttpProgressCallback progress,
                                   void* user_data)
{
    PHttpResponse* resp;
    HANDLE         conn;
    char*          request;
    bytebuf        recvbuf;
    bytebuf        bodybuf;
    int            body_start;
    int            req_len;
    int            wrote;
    int            cl;

    resp = resp_new();
    if (resp == NULL) {
        return NULL;
    }
    conn = NULL;
    request = NULL;
    recvbuf.data = NULL;
    bodybuf.data = NULL;

    if (!g_initialized) {
        resp_set_error(resp, "PHttp_Init not called");
        return resp;
    }
    if (host == NULL || path == NULL || method == NULL) {
        resp_set_error(resp, "invalid arguments");
        return resp;
    }
    if (body != NULL && body_len < 0) {
        body_len = (int)strlen(body);
    }

    {
        char cur_host[256];
        char cur_path[1024];
        int  cur_port = port;
        int  redirects = 0;
        int  follow = (strcmp(method, "GET") == 0) ? 1 : 0;

        cstrcpy(cur_host, sizeof(cur_host), host);
        cstrcpy(cur_path, sizeof(cur_path), path);

        if (bb_init(&bodybuf) != 0) {
            resp_set_error(resp, "OOM body buffer");
            goto done;
        }

        for (;;) {
            char        location[1024];
            const char* loc = NULL;
            size_t      loclen = 0;
            int         status;

            location[0] = '\0';
            resp_set_effective_url(resp, cur_host, cur_port, cur_path);

            /* Transport by scheme/port: port 80 = plaintext http via WinInet
             * (WM6 built-in); anything else = TLS via mbedTLS. */
            if (cur_port == 80) {
                if (wininet_fetch(method, cur_host, cur_port, cur_path,
                        headers, body, body_len, &status,
                        location, sizeof(location), &bodybuf, resp,
                        progress, user_data) != 0) {
                    goto done;   /* error already set */
                }
                resp->status_code = status;

                if (follow && is_redirect_code(status) &&
                        redirects < MAX_REDIRECTS && location[0] != '\0') {
                    char nhost[256];
                    char npath[1024];
                    int  nport;
                    if (resolve_redirect(location, strlen(location),
                            cur_host, cur_path, cur_port,
                            nhost, sizeof(nhost), npath, sizeof(npath),
                            &nport)) {
                        redirects++;
                        cstrcpy(cur_host, sizeof(cur_host), nhost);
                        cstrcpy(cur_path, sizeof(cur_path), npath);
                        cur_port = nport;
                        bb_free(&bodybuf);   /* discard the 3xx body */
                        if (bb_init(&bodybuf) != 0) {
                            resp_set_error(resp, "OOM body buffer");
                            goto done;
                        }
                        continue;
                    }
                }
                break;   /* final response; body already in bodybuf */
            }

            /* ---- TLS https:// via mbedTLS ---- */
            request = build_request(method, cur_host, cur_path, headers,
                                    body, body_len);
            if (request == NULL) {
                resp_set_error(resp, "OOM building request");
                goto done;
            }

            conn = g_insecure ? PTls_Connect(cur_host, cur_port)
                              : PTls_ConnectVerified(cur_host, cur_port);
            if (conn == NULL) {
                char eb[320];
                _snprintf(eb, sizeof(eb) - 1, "%s:%d (hop %d): %s",
                          cur_host, cur_port, redirects, PTls_LastError());
                eb[sizeof(eb) - 1] = '\0';
                resp_set_error(resp, eb);
                goto done;
            }

            req_len = (int)strlen(request);
            wrote = PTls_Write(conn, request, req_len);
            if (wrote != req_len) {
                resp_set_error(resp, "PTls_Write incomplete");
                goto done;
            }

            if (bb_init(&recvbuf) != 0) {
                resp_set_error(resp, "OOM recv buffer");
                goto done;
            }

            body_start = read_until_headers(conn, &recvbuf);
            if (body_start <= 0) {
                resp_set_error(resp, "header block read failed");
                goto done;
            }

            status = parse_status(recvbuf.data, (size_t)body_start);
            resp->status_code = status;

            /* Follow a 3xx Location (GET only) before reading the body. */
            if (follow && is_redirect_code(status) &&
                    redirects < MAX_REDIRECTS) {
                char nhost[256];
                char npath[1024];
                int  nport;

                loc = find_header(recvbuf.data, (size_t)body_start,
                                  "Location", &loclen);
                if (loc != NULL && loclen > 0 &&
                        resolve_redirect(loc, loclen, cur_host, cur_path,
                                cur_port, nhost, sizeof(nhost),
                                npath, sizeof(npath), &nport)) {
                    redirects++;
                    cstrcpy(cur_host, sizeof(cur_host), nhost);
                    cstrcpy(cur_path, sizeof(cur_path), npath);
                    cur_port = nport;
                    HeapFree(GetProcessHeap(), 0, request);
                    request = NULL;
                    bb_free(&recvbuf);
                    recvbuf.data = NULL;
                    PTls_Close(conn);
                    conn = NULL;
                    continue;
                }
                /* No usable Location: return the 3xx response as-is. */
            }

            /* Final TLS response: read the body into bodybuf. */
            if (is_chunked(recvbuf.data, (size_t)body_start)) {
                const char* prefix = recvbuf.data + body_start;
                int prefix_len = (int)recvbuf.len - body_start;
                report_progress(progress, user_data, 0, -1);
                if (decode_chunked(conn, prefix, prefix_len, &bodybuf,
                        progress, user_data) != 0) {
                    resp_set_error(resp, "chunked decode failed");
                    /* still keep partial body */
                }
            } else {
                cl = parse_content_length(recvbuf.data, (size_t)body_start);
                report_progress(progress, user_data, 0, cl);
                if (recvbuf.len > (size_t)body_start) {
                    size_t prefix_len;

                    prefix_len = recvbuf.len - (size_t)body_start;
                    if (cl >= 0 && prefix_len > (size_t)cl) {
                        prefix_len = (size_t)cl;
                    }
                    if (bb_append(&bodybuf, recvbuf.data + body_start,
                            prefix_len) != 0) {
                        resp_set_error(resp, "response body too large");
                        goto done;
                    }
                    report_progress(progress, user_data,
                            (int)bodybuf.len, cl);
                }
                if (cl >= 0) {
                    char tmp[2048];
                    int  remaining = cl - (int)bodybuf.len;
                    int  got;
                    int  want;
                    while (remaining > 0) {
                        want = remaining < (int)sizeof(tmp)
                               ? remaining : (int)sizeof(tmp);
                        got = PTls_Read(conn, tmp, want);
                        if (got <= 0) {
                            break;
                        }
                        if (bb_append(&bodybuf, tmp, (size_t)got) != 0) {
                            break;
                        }
                        remaining -= got;
                        report_progress(progress, user_data,
                                (int)bodybuf.len, cl);
                    }
                } else {
                    /* No CL, not chunked: read until close. */
                    char tmp[2048];
                    int  got;
                    while (1) {
                        got = PTls_Read(conn, tmp, (int)sizeof(tmp));
                        if (got <= 0) {
                            break;
                        }
                        if (bb_append(&bodybuf, tmp, (size_t)got) != 0) {
                            break;
                        }
                        report_progress(progress, user_data,
                                (int)bodybuf.len, -1);
                        if (bodybuf.len >= MAX_RESP_BODY) {
                            break;
                        }
                    }
                }
            }
            break;   /* final TLS response complete */
        }
    }

    /* transfer body ownership into response */
    resp->body = bodybuf.data;
    resp->body_len = (int)bodybuf.len;
    bodybuf.data = NULL;

done:
    if (request != NULL) {
        HeapFree(GetProcessHeap(), 0, request);
    }
    if (recvbuf.data != NULL) {
        bb_free(&recvbuf);
    }
    if (bodybuf.data != NULL) {
        bb_free(&bodybuf);
    }
    if (conn != NULL) {
        PTls_Close(conn);
    }
    return resp;
}

/* ------------------------------------------------------------------- */
/* Public API                                                           */
/* ------------------------------------------------------------------- */

PHTTP_API PHttpResponse* PHttp_Get(const char* host, int port,
                                   const char* path,
                                   const char** headers)
{
    return PHttp_GetEx(host, port, path, headers, NULL, NULL);
}

PHTTP_API PHttpResponse* PHttp_GetEx(const char* host, int port,
                                     const char* path,
                                     const char** headers,
                                     PHttpProgressCallback progress,
                                     void* user_data)
{
    return http_request("GET", host, port, path, headers, NULL, 0,
                        progress, user_data);
}

PHTTP_API PHttpResponse* PHttp_Post(const char* host, int port,
                                    const char* path,
                                    const char** headers,
                                    const char* body, int body_len)
{
    return PHttp_PostEx(host, port, path, headers, body, body_len,
                        NULL, NULL);
}

PHTTP_API PHttpResponse* PHttp_PostEx(const char* host, int port,
                                      const char* path,
                                      const char** headers,
                                      const char* body, int body_len,
                                      PHttpProgressCallback progress,
                                      void* user_data)
{
    return http_request("POST", host, port, path, headers, body, body_len,
                        progress, user_data);
}

PHTTP_API void PHttp_FreeResponse(PHttpResponse* resp)
{
    if (resp == NULL) {
        return;
    }
    if (resp->body != NULL) {
        HeapFree(GetProcessHeap(), 0, resp->body);
    }
    HeapFree(GetProcessHeap(), 0, resp);
}
