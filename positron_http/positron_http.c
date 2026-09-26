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

#define PHTTP_BODY_OK          0
#define PHTTP_BODY_READ_ERROR -1
#define PHTTP_BODY_TOO_LARGE  -2
#define PHTTP_BODY_NO_MEMORY  -3
#define PHTTP_CHUNK_BUFFER_MAX (MAX_RESP_BODY + 4096)

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

/* Keep the response body limit separate from the generic request/header
 * buffer so all response paths fail closed at the same boundary. */
static int phttp_body_append(bytebuf* b, const char* src, size_t n)
{
    if (n > (size_t)MAX_RESP_BODY ||
            b->len > (size_t)MAX_RESP_BODY - n) {
        return PHTTP_BODY_TOO_LARGE;
    }
    if (bb_append(b, src, n) != 0) {
        return PHTTP_BODY_NO_MEMORY;
    }
    return PHTTP_BODY_OK;
}

/* Keep chunk framing bounded independently from the decoded body.  The
 * consumed prefix is compacted before each append, so many small chunks do
 * not accumulate their framing overhead for the lifetime of a response. */
static int phttp_chunk_append(bytebuf* raw, size_t* consumed,
                              const char* src, size_t n)
{
    size_t remaining;

    if (raw == NULL || consumed == NULL || src == NULL) {
        return PHTTP_BODY_READ_ERROR;
    }
    if (*consumed > raw->len) {
        return PHTTP_BODY_READ_ERROR;
    }
    if (*consumed > 0) {
        remaining = raw->len - *consumed;
        if (remaining > 0) {
            memmove(raw->data, raw->data + *consumed, remaining);
        }
        raw->len = remaining;
        if (raw->data != NULL) {
            raw->data[raw->len] = '\0';
        }
        *consumed = 0;
    }
    if (n > (size_t)PHTTP_CHUNK_BUFFER_MAX ||
            raw->len > (size_t)PHTTP_CHUNK_BUFFER_MAX - n) {
        return PHTTP_BODY_TOO_LARGE;
    }
    if (bb_append(raw, src, n) != 0) {
        return PHTTP_BODY_NO_MEMORY;
    }
    return PHTTP_BODY_OK;
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

/* The public response is the first member.  Private metadata therefore does
 * not change the published PHttpResponse layout or its ABI. */
typedef struct {
    PHttpResponse public_response;
    char          final_url[PHTTP_URL_MAX];
} phttp_response_private;

static PHttpResponse* resp_new(void)
{
    phttp_response_private* r;
    r = (phttp_response_private*)HeapAlloc(GetProcessHeap(),
                                  HEAP_ZERO_MEMORY,
                                  sizeof(phttp_response_private));
    return r == NULL ? NULL : &r->public_response;
}

static phttp_response_private* resp_private(PHttpResponse* response)
{
    return (phttp_response_private*)response;
}

static void resp_set_final_url(PHttpResponse* response, const char* url)
{
    phttp_response_private* private_response;
    size_t length;

    if (response == NULL) {
        return;
    }
    private_response = resp_private(response);
    private_response->final_url[0] = '\0';
    if (url == NULL) {
        return;
    }
    length = strlen(url);
    if (length >= sizeof(private_response->final_url)) {
        return;
    }
    memcpy(private_response->final_url, url, length + 1);
}

static void resp_set_error(PHttpResponse* r, const char* msg)
{
    if (r == NULL || msg == NULL) {
        return;
    }
    _snprintf(r->error_msg, sizeof(r->error_msg) - 1, "%s", msg);
    r->error_msg[sizeof(r->error_msg) - 1] = '\0';
}

static void resp_set_body_result(PHttpResponse* response, int result)
{
    if (result == PHTTP_BODY_TOO_LARGE) {
        resp_set_error(response, "response body too large");
    } else if (result == PHTTP_BODY_NO_MEMORY) {
        resp_set_error(response, "response body allocation failed");
    } else {
        resp_set_error(response, "response body read failed");
    }
    if (response != NULL) {
        response->status_code = 0;
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
    int     append_result;
    size_t  chunk_size;
    const char* nl;
    size_t  i;
    char    sizebuf[24];

    if (bb_init(&raw) != 0) {
        return PHTTP_BODY_NO_MEMORY;
    }
    pos = 0;
    if (prefix_len > 0) {
        append_result = phttp_chunk_append(&raw, &pos, prefix,
                (size_t)prefix_len);
        if (append_result != PHTTP_BODY_OK) {
            bb_free(&raw);
            return append_result;
        }
    }

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
            if (n < 0) {
                bb_free(&raw);
                return PHTTP_BODY_READ_ERROR;
            }
            if (n == 0) {
                bb_free(&raw);
                return PHTTP_BODY_READ_ERROR;
            }
            append_result = phttp_chunk_append(&raw, &pos, tmp,
                    (size_t)n);
            if (append_result != PHTTP_BODY_OK) {
                bb_free(&raw);
                return append_result;
            }
            continue;
        }

        /* extract size (hex, possibly with ;ext) */
        {
            size_t size_str_len = (size_t)(nl - (raw.data + pos));
            char* size_end;
            unsigned long parsed_size;
            if (size_str_len >= sizeof(sizebuf)) {
                bb_free(&raw);
                return PHTTP_BODY_READ_ERROR;
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
            parsed_size = strtoul(sizebuf, &size_end, 16);
            if (size_end == sizebuf || *size_end != '\0') {
                bb_free(&raw);
                return PHTTP_BODY_READ_ERROR;
            }
            if (parsed_size > (unsigned long)MAX_RESP_BODY) {
                bb_free(&raw);
                return PHTTP_BODY_TOO_LARGE;
            }
            chunk_size = (size_t)parsed_size;
        }
        pos = (size_t)(nl - raw.data) + 2;

        if (chunk_size == 0) {
            /* terminating chunk; we are done */
            bb_free(&raw);
            return 0;
        }

        if (out_body->len + chunk_size > MAX_RESP_BODY) {
            bb_free(&raw);
            return PHTTP_BODY_TOO_LARGE;
        }

        /* ensure we have chunk_size + 2 (trailing \r\n) bytes past pos */
        while (raw.len - pos < chunk_size + 2) {
            n = PTls_Read(conn, tmp, (int)sizeof(tmp));
            if (n < 0) {
                bb_free(&raw);
                return PHTTP_BODY_READ_ERROR;
            }
            if (n == 0) {
                bb_free(&raw);
                return PHTTP_BODY_READ_ERROR;
            }
            append_result = phttp_chunk_append(&raw, &pos, tmp,
                    (size_t)n);
            if (append_result != PHTTP_BODY_OK) {
                bb_free(&raw);
                return append_result;
            }
        }

        if (raw.data[pos + chunk_size] != '\r' ||
                raw.data[pos + chunk_size + 1] != '\n') {
            bb_free(&raw);
            return PHTTP_BODY_READ_ERROR;
        }
        n = phttp_body_append(out_body, raw.data + pos, chunk_size);
        if (n != PHTTP_BODY_OK) {
            bb_free(&raw);
            return n;
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

static int phttp_is_ascii_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}

static int phttp_is_control(int c)
{
    return c < 0x20 || c == 0x7f;
}

static int phttp_starts_with_ci(const char* value, const char* prefix)
{
    size_t i;
    size_t n;

    if (value == NULL || prefix == NULL) {
        return 0;
    }
    n = strlen(prefix);
    for (i = 0; i < n; i++) {
        if (value[i] == '\0' || ascii_tolower((unsigned char)value[i]) !=
                ascii_tolower((unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static int phttp_trim_reference(const char* source, char* destination,
                                int capacity)
{
    const char* start;
    const char* end;
    size_t length;
    const char* scan;

    if (source == NULL || destination == NULL || capacity <= 1) {
        return 1;
    }
    for (scan = source; *scan != '\0'; scan++) {
        if (phttp_is_control((unsigned char)*scan)) {
            destination[0] = '\0';
            return 1;
        }
    }
    start = source;
    while (*start == ' ') {
        start++;
    }
    end = start + strlen(start);
    while (end > start && end[-1] == ' ') {
        end--;
    }
    length = (size_t)(end - start);
    if (length >= (size_t)capacity) {
        destination[0] = '\0';
        return 1;
    }
    memcpy(destination, start, length);
    destination[length] = '\0';
    return 0;
}

static const char* phttp_scheme_name(int scheme)
{
    return scheme == PHTTP_SCHEME_HTTP ? "http" : "https";
}

static int phttp_default_port(int scheme)
{
    return scheme == PHTTP_SCHEME_HTTP ? 80 : 443;
}

static int phttp_document_url(int scheme, const char* host, int port,
                              const char* path, char* url, int capacity)
{
    int default_port;
    int n;

    if ((scheme != PHTTP_SCHEME_HTTP && scheme != PHTTP_SCHEME_HTTPS) ||
            host == NULL || host[0] == '\0' || path == NULL ||
            path[0] != '/' || url == NULL || capacity <= 1 || port <= 0 ||
            port > 65535) {
        return 1;
    }
    default_port = port == phttp_default_port(scheme);
    if (default_port) {
        n = _snprintf(url, capacity - 1, "%s://%s%s",
                phttp_scheme_name(scheme), host, path);
    } else {
        n = _snprintf(url, capacity - 1, "%s://%s:%d%s",
                phttp_scheme_name(scheme), host, port, path);
    }
    url[capacity - 1] = '\0';
    return n < 0 || n >= capacity - 1 ? 1 : 0;
}

static int phttp_normalize_path(const char* source, char* path, int capacity)
{
    int segment_start[128];
    int segment_count;
    int segment_length;
    int n;
    int ended_with_slash;
    const char* p;
    const char* segment;

    if (source == NULL || path == NULL || capacity <= 1 || source[0] != '/') {
        return 1;
    }
    path[0] = '\0';
    n = 1;
    path[0] = '/';
    segment_count = 0;
    ended_with_slash = 1;
    p = source + 1;
    while (*p != '\0' && *p != '?' && *p != '#') {
        segment = p;
        segment_length = 0;
        while (*p != '\0' && *p != '/' && *p != '?' && *p != '#') {
            if (phttp_is_control((unsigned char)*p)) {
                path[0] = '\0';
                return 1;
            }
            p++;
            segment_length++;
        }
        if (segment_length == 0) {
            ended_with_slash = 1;
        } else if (segment_length == 1 && segment[0] == '.') {
            ended_with_slash = 0;
        } else if (segment_length == 2 && segment[0] == '.' &&
                segment[1] == '.') {
            if (segment_count > 0) {
                n = segment_start[segment_count - 1];
                segment_count--;
            }
            ended_with_slash = 0;
        } else {
            if (segment_count >=
                    (int)(sizeof(segment_start) / sizeof(segment_start[0]))) {
                path[0] = '\0';
                return 1;
            }
            if (n > 1 && path[n - 1] != '/') {
                if (n >= capacity - 1) {
                    path[0] = '\0';
                    return 1;
                }
                path[n++] = '/';
            }
            segment_start[segment_count++] = n;
            if (n + segment_length >= capacity) {
                path[0] = '\0';
                return 1;
            }
            memcpy(path + n, segment, (size_t)segment_length);
            n += segment_length;
            ended_with_slash = 0;
        }
        if (*p == '/') {
            p++;
            ended_with_slash = 1;
        }
    }
    if (ended_with_slash && n > 1) {
        if (n >= capacity - 1) {
            path[0] = '\0';
            return 1;
        }
        path[n++] = '/';
    }
    if (*p == '?') {
        while (*p != '\0' && *p != '#') {
            if (phttp_is_control((unsigned char)*p) || n >= capacity - 1) {
                path[0] = '\0';
                return 1;
            }
            path[n++] = *p++;
        }
    }
    path[n] = '\0';
    return 0;
}

static int phttp_merge_reference_path(const char* base_path,
                                      const char* reference, char* output,
                                      int capacity)
{
    const char* query;
    const char* slash;
    int base_length;
    int prefix_length;
    int reference_length;
    int n;

    if (base_path == NULL || base_path[0] != '/' || reference == NULL ||
            output == NULL || capacity <= 1) {
        return 1;
    }
    query = strchr(base_path, '?');
    base_length = query == NULL ? (int)strlen(base_path) :
            (int)(query - base_path);
    if (reference[0] == '\0' || reference[0] == '#') {
        if (base_length + (query == NULL ? 0 : (int)strlen(query)) >=
                capacity) {
            return 1;
        }
        cstrcpy(output, capacity, base_path);
        return 0;
    }
    if (reference[0] == '?') {
        reference_length = (int)strlen(reference);
        if (base_length + reference_length >= capacity) {
            return 1;
        }
        memcpy(output, base_path, (size_t)base_length);
        memcpy(output + base_length, reference, (size_t)reference_length);
        output[base_length + reference_length] = '\0';
        return 0;
    }
    if (reference[0] == '/') {
        if ((int)strlen(reference) >= capacity) {
            return 1;
        }
        cstrcpy(output, capacity, reference);
        return 0;
    }
    slash = base_path + base_length;
    while (slash > base_path && slash[-1] != '/') {
        slash--;
    }
    prefix_length = (int)(slash - base_path);
    reference_length = (int)strlen(reference);
    n = prefix_length + reference_length;
    if (n >= capacity) {
        return 1;
    }
    memcpy(output, base_path, (size_t)prefix_length);
    memcpy(output + prefix_length, reference, (size_t)reference_length);
    output[n] = '\0';
    return 0;
}

static int phttp_parse_resolved_url(const char* url, char* host, int hostcap,
                                    char* path, int pathcap, int* out_port,
                                    int* out_scheme)
{
    const char* p;
    const char* authority_end;
    const char* host_end;
    const char* colon;
    const char* q;
    int scheme;
    int port;
    int digits;
    int n;
    size_t suffix_length;
    char path_source[PHTTP_URL_MAX];

    if (url == NULL || host == NULL || hostcap <= 1 || path == NULL ||
            pathcap <= 1 || out_port == NULL || out_scheme == NULL) {
        return 1;
    }
    host[0] = '\0';
    path[0] = '\0';
    *out_port = 0;
    *out_scheme = 0;
    if (phttp_starts_with_ci(url, "http://")) {
        p = url + 7;
        scheme = PHTTP_SCHEME_HTTP;
    } else if (phttp_starts_with_ci(url, "https://")) {
        p = url + 8;
        scheme = PHTTP_SCHEME_HTTPS;
    } else {
        return 1;
    }
    authority_end = p;
    while (*authority_end != '\0' && *authority_end != '/' &&
            *authority_end != '?' && *authority_end != '#') {
        authority_end++;
    }
    if (authority_end == p) {
        return 1;
    }
    host_end = authority_end;
    colon = NULL;
    for (q = p; q < authority_end; q++) {
        if (*q == '@' || *q == '[' || *q == ']' ||
                phttp_is_ascii_space((unsigned char)*q) ||
                phttp_is_control((unsigned char)*q)) {
            return 1;
        }
        if (*q == ':') {
            if (colon != NULL) {
                return 1;
            }
            colon = q;
        }
    }
    if (colon != NULL) {
        host_end = colon;
    }
    n = (int)(host_end - p);
    if (n <= 0 || n >= hostcap) {
        return 1;
    }
    memcpy(host, p, (size_t)n);
    host[n] = '\0';
    port = phttp_default_port(scheme);
    if (colon != NULL) {
        port = 0;
        digits = 0;
        for (q = colon + 1; q < authority_end; q++) {
            if (*q < '0' || *q > '9' || port > 6553 ||
                    (port == 6553 && *q > '5')) {
                host[0] = '\0';
                path[0] = '\0';
                *out_port = 0;
                *out_scheme = 0;
                return 1;
            }
            port = port * 10 + (*q - '0');
            digits++;
        }
        if (digits == 0 || port <= 0 || port > 65535) {
            host[0] = '\0';
            path[0] = '\0';
            *out_port = 0;
            *out_scheme = 0;
            return 1;
        }
    }
    /* A URL authority may be followed directly by a query or fragment.  The
     * path normalizer intentionally requires a leading slash, so provide the
     * implicit root path before handing those forms to it. */
    if (*authority_end == '\0' || *authority_end == '#') {
        if (phttp_normalize_path("/", path, pathcap) != 0) {
            host[0] = '\0';
            path[0] = '\0';
            *out_port = 0;
            *out_scheme = 0;
            return 1;
        }
    } else if (*authority_end == '?') {
        suffix_length = strlen(authority_end);
        if (suffix_length >= sizeof(path_source) - 1) {
            host[0] = '\0';
            path[0] = '\0';
            *out_port = 0;
            *out_scheme = 0;
            return 1;
        }
        path_source[0] = '/';
        memcpy(path_source + 1, authority_end, suffix_length + 1);
        if (phttp_normalize_path(path_source, path, pathcap) != 0) {
            host[0] = '\0';
            path[0] = '\0';
            *out_port = 0;
            *out_scheme = 0;
            return 1;
        }
    } else if (phttp_normalize_path(authority_end, path, pathcap) != 0) {
        host[0] = '\0';
        path[0] = '\0';
        *out_port = 0;
        *out_scheme = 0;
        return 1;
    }
    *out_port = port;
    *out_scheme = scheme;
    return 0;
}

/* Turn a URL-like request into an absolute URL.  An omitted scheme is a
 * deliberate HTTPS default; callers that need plaintext must say http://. */
static int phttp_prepare_absolute_url(const char* source, char* output,
                                      int capacity)
{
    char trimmed[2048];
    int n;

    if (source == NULL || output == NULL || capacity <= 1 ||
            phttp_trim_reference(source, trimmed, sizeof(trimmed)) != 0 ||
            trimmed[0] == '\0') {
        return 1;
    }
    if (phttp_starts_with_ci(trimmed, "http://") ||
            phttp_starts_with_ci(trimmed, "https://")) {
        n = _snprintf(output, capacity - 1, "%s", trimmed);
    } else if (trimmed[0] == '/' && trimmed[1] == '/') {
        n = _snprintf(output, capacity - 1, "https:%s", trimmed);
    } else {
        n = _snprintf(output, capacity - 1, "https://%s", trimmed);
    }
    output[capacity - 1] = '\0';
    return n < 0 || n >= capacity - 1 ? 1 : 0;
}

static int phttp_reference_has_scheme(const char* reference)
{
    const char* p;

    if (reference == NULL ||
            !((reference[0] >= 'A' && reference[0] <= 'Z') ||
            (reference[0] >= 'a' && reference[0] <= 'z'))) {
        return 0;
    }
    p = reference + 1;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' ||
            *p == '.') {
        p++;
    }
    return *p == ':';
}

/* Resolve a URL reference and return a canonical absolute URL. */
static int phttp_resolve_url_text(const char* base_url,
                                  const char* reference, char* out_url,
                                  int out_url_capacity)
{
    char ref[2048];
    char base[2048];
    char combined[2048];
    char base_host[256];
    char base_path[1024];
    char merged_path[2048];
    char normalized_path[1024];
    char host[256];
    char path[1024];
    int base_port;
    int base_scheme;
    int port;
    int scheme;
    int n;

    if (reference == NULL || out_url == NULL || out_url_capacity <= 1 ||
            phttp_trim_reference(reference, ref, sizeof(ref)) != 0) {
        return 1;
    }
    out_url[0] = '\0';
    if (phttp_reference_has_scheme(ref) &&
            !phttp_starts_with_ci(ref, "http://") &&
            !phttp_starts_with_ci(ref, "https://")) {
        return 1;
    }
    if (base_url != NULL && base_url[0] != '\0') {
        if (phttp_prepare_absolute_url(base_url, base, sizeof(base)) != 0) {
            return 1;
        }
        if (phttp_parse_resolved_url(base, base_host, sizeof(base_host),
                base_path, sizeof(base_path), &base_port, &base_scheme) != 0) {
            return 1;
        }
        if (phttp_starts_with_ci(ref, "http://") ||
                phttp_starts_with_ci(ref, "https://")) {
            cstrcpy(combined, sizeof(combined), ref);
            if (phttp_parse_resolved_url(combined, host, sizeof(host), path,
                    sizeof(path), &port, &scheme) != 0) {
                return 1;
            }
            return phttp_document_url(scheme, host, port, path, out_url,
                    out_url_capacity);
        }
        if (ref[0] == '/' && ref[1] == '/') {
            n = _snprintf(combined, sizeof(combined) - 1, "%s:%s",
                    phttp_scheme_name(base_scheme), ref);
            combined[sizeof(combined) - 1] = '\0';
            if (n < 0 || n >= (int)sizeof(combined) - 1 ||
                    phttp_parse_resolved_url(combined, host, sizeof(host),
                    path, sizeof(path), &port, &scheme) != 0) {
                return 1;
            }
            return phttp_document_url(scheme, host, port, path, out_url,
                    out_url_capacity);
        }
        if (phttp_merge_reference_path(base_path, ref, merged_path,
                sizeof(merged_path)) != 0 ||
                phttp_normalize_path(merged_path, normalized_path,
                sizeof(normalized_path)) != 0) {
            return 1;
        }
        return phttp_document_url(base_scheme, base_host, base_port,
                normalized_path, out_url, out_url_capacity);
    } else {
        if (phttp_prepare_absolute_url(ref, combined, sizeof(combined)) != 0) {
            return 1;
        }
        if (phttp_parse_resolved_url(combined, host, sizeof(host), path,
                sizeof(path), &port, &scheme) != 0) {
            return 1;
        }
        return phttp_document_url(scheme, host, port, path, out_url,
                out_url_capacity);
    }
}

PHTTP_API int PHttp_ResolveReference(const char* base_host, int base_port,
                                     const char* base_path,
                                     const char* reference, char* out_host,
                                     int out_host_capacity, char* out_path,
                                     int out_path_capacity, int* out_port)
{
    char ref[2048];
    char base_url[2048];
    char resolved[2048];
    int base_scheme;
    int effective_base_port;
    int scheme;

    if (out_host == NULL || out_host_capacity <= 1 || out_path == NULL ||
            out_path_capacity <= 1 || out_port == NULL || reference == NULL) {
        return 1;
    }
    out_host[0] = '\0';
    out_path[0] = '\0';
    *out_port = 0;
    if (phttp_trim_reference(reference, ref, sizeof(ref)) != 0) {
        return 1;
    }
    if (base_host != NULL && base_host[0] != '\0' && base_path != NULL) {
        effective_base_port = base_port == 0 ? 443 : base_port;
        base_scheme = effective_base_port == 80 ? PHTTP_SCHEME_HTTP :
                PHTTP_SCHEME_HTTPS;
        if (phttp_document_url(base_scheme, base_host, effective_base_port,
                base_path, base_url, sizeof(base_url)) != 0) {
            return 1;
        }
        if (phttp_resolve_url_text(base_url, ref, resolved,
                sizeof(resolved)) != 0) {
            return 1;
        }
    } else {
        if (!(phttp_starts_with_ci(ref, "http://") ||
                phttp_starts_with_ci(ref, "https://") ||
                (ref[0] == '/' && ref[1] == '/'))) {
            return 1;
        }
        if (phttp_resolve_url_text(NULL, ref, resolved,
                sizeof(resolved)) != 0) {
            return 1;
        }
    }
    return phttp_parse_resolved_url(resolved, out_host, out_host_capacity,
            out_path, out_path_capacity, out_port, &scheme);
}

PHTTP_API int PHttp_ResolveReferenceUrl(const char* base_url,
                                         const char* reference,
                                         char* out_url,
                                         int out_url_capacity)
{
    if (out_url == NULL || out_url_capacity <= 1 || reference == NULL) {
        return 1;
    }
    out_url[0] = '\0';
    return phttp_resolve_url_text(base_url, reference, out_url,
            out_url_capacity);
}

/* Resolve a counted Location header against the current absolute URL. */
static int resolve_redirect_url(const char* loc, size_t loclen,
                               const char* cur_url, char* out_url,
                               int out_capacity)
{
    char location[2048];

    if (loc == NULL || cur_url == NULL || out_url == NULL ||
            loclen >= sizeof(location)) {
        return 0;
    }
    memcpy(location, loc, loclen);
    location[loclen] = '\0';
    return PHttp_ResolveReferenceUrl(cur_url, location, out_url,
            out_capacity) == 0;
}

/* Do not let a secure URL silently cross onto plaintext through a redirect.
 * The policy is enforced after resolution, so relative and network-path
 * Locations are covered as well as absolute URLs. */
static int phttp_redirect_allowed(int current_scheme, const char* next_url)
{
    char host[256];
    char path[1024];
    int port;
    int next_scheme;

    if (current_scheme != PHTTP_SCHEME_HTTPS) {
        return 1;
    }
    if (phttp_parse_resolved_url(next_url, host, sizeof(host), path,
            sizeof(path), &port, &next_scheme) != 0) {
        return 0;
    }
    return next_scheme != PHTTP_SCHEME_HTTP;
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
    if (!HttpQueryInfoW(hReq,
            HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &code, &sz, NULL)) {
        resp_set_error(resp, "HttpQueryInfo status failed");
        goto wdone;
    }
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
    if (total > MAX_RESP_BODY) {
        *out_status = 0;
        resp_set_error(resp, "response body too large");
        goto wdone;
    }
    report_progress(progress, user_data, 0, total);

    {
        char  tmp[2048];
        DWORD got;
        for (;;) {
            if (!InternetReadFile(hReq, tmp, sizeof(tmp), &got)) {
                *out_status = 0;
                resp_set_error(resp, "response body read failed");
                goto wdone;
            }
            if (got == 0) {
                break;
            }
            {
                int append_result = phttp_body_append(outbody, tmp,
                        (size_t)got);
                if (append_result != PHTTP_BODY_OK) {
                    *out_status = 0;
                    resp_set_error(resp, append_result ==
                            PHTTP_BODY_TOO_LARGE ?
                            "response body too large" :
                            "response body allocation failed");
                    goto wdone;
                }
            }
            report_progress(progress, user_data, (int)outbody->len, total);
        }
    }

    if (total >= 0 && outbody->len != (size_t)total) {
        *out_status = 0;
        resp_set_error(resp, "response body truncated");
        goto wdone;
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

static PHttpResponse* http_request_url(const char* method, const char* url,
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
    char           cur_url[2048];
    int            body_start;
    int            req_len;
    int            wrote;
    int            cl;
    int            redirects;
    int            follow;

    resp = resp_new();
    if (resp == NULL) {
        return NULL;
    }
    conn = NULL;
    request = NULL;
    recvbuf.data = NULL;
    bodybuf.data = NULL;
    redirects = 0;
    follow = (method != NULL && strcmp(method, "GET") == 0) ? 1 : 0;

    if (!g_initialized) {
        resp_set_error(resp, "PHttp_Init not called");
        return resp;
    }
    if (method == NULL || url == NULL) {
        resp_set_error(resp, "invalid arguments");
        return resp;
    }
    if (body != NULL && body_len < 0) {
        body_len = (int)strlen(body);
    }
    if (phttp_resolve_url_text(NULL, url, cur_url, sizeof(cur_url)) != 0) {
        resp_set_error(resp, "invalid HTTP(S) URL");
        return resp;
    }
    resp_set_final_url(resp, cur_url);
    if (bb_init(&bodybuf) != 0) {
        resp_set_error(resp, "OOM body buffer");
        return resp;
    }

    for (;;) {
        char        cur_host[256];
        char        cur_path[1024];
        char        location[1024];
        char        next_url[2048];
        const char* loc;
        size_t      loclen;
        int         cur_port;
        int         cur_scheme;
        int         status;

        cur_host[0] = '\0';
        cur_path[0] = '\0';
        location[0] = '\0';
        next_url[0] = '\0';
        loc = NULL;
        loclen = 0;
        if (phttp_parse_resolved_url(cur_url, cur_host, sizeof(cur_host),
                cur_path, sizeof(cur_path), &cur_port, &cur_scheme) != 0) {
            resp_set_error(resp, "invalid resolved URL");
            goto done;
        }
        resp_set_final_url(resp, cur_url);

        if (cur_scheme == PHTTP_SCHEME_HTTP) {
            if (wininet_fetch(method, cur_host, cur_port, cur_path,
                    headers, body, body_len, &status, location,
                    sizeof(location), &bodybuf, resp, progress,
                    user_data) != 0) {
                goto done;
            }
            resp->status_code = status;
            if (follow && is_redirect_code(status) &&
                    redirects < MAX_REDIRECTS && location[0] != '\0' &&
                    resolve_redirect_url(location, strlen(location), cur_url,
                    next_url, sizeof(next_url))) {
                if (!phttp_redirect_allowed(cur_scheme, next_url)) {
                    resp_set_error(resp, "HTTPS redirect to HTTP rejected");
                    break;
                }
                redirects++;
                cstrcpy(cur_url, sizeof(cur_url), next_url);
                bb_free(&bodybuf);
                if (bb_init(&bodybuf) != 0) {
                    resp_set_error(resp, "OOM body buffer");
                    goto done;
                }
                continue;
            }
            break;   /* final response; WinInet already read the body */
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
            loc = find_header(recvbuf.data, (size_t)body_start,
                              "Location", &loclen);
            if (loc != NULL && loclen > 0 &&
                    resolve_redirect_url(loc, loclen, cur_url, next_url,
                    sizeof(next_url))) {
                if (!phttp_redirect_allowed(cur_scheme, next_url)) {
                    resp_set_error(resp, "HTTPS redirect to HTTP rejected");
                    break;
                }
                redirects++;
                cstrcpy(cur_url, sizeof(cur_url), next_url);
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
            const char* prefix;
            int prefix_len;
            int chunk_result;

            prefix = recvbuf.data + body_start;
            prefix_len = (int)recvbuf.len - body_start;
            report_progress(progress, user_data, 0, -1);
            chunk_result = decode_chunked(conn, prefix, prefix_len,
                    &bodybuf, progress, user_data);
            if (chunk_result != PHTTP_BODY_OK) {
                resp_set_body_result(resp, chunk_result);
                goto done;
            }
        } else {
            size_t prefix_len;
            int append_result;

            cl = parse_content_length(recvbuf.data, (size_t)body_start);
            if (cl > MAX_RESP_BODY) {
                resp_set_body_result(resp, PHTTP_BODY_TOO_LARGE);
                goto done;
            }
            report_progress(progress, user_data, 0, cl);
            prefix_len = 0;
            if (recvbuf.len > (size_t)body_start) {
                prefix_len = recvbuf.len - (size_t)body_start;
                if (cl >= 0 && prefix_len > (size_t)cl) {
                    prefix_len = (size_t)cl;
                }
                append_result = phttp_body_append(&bodybuf,
                        recvbuf.data + body_start, prefix_len);
                if (append_result != PHTTP_BODY_OK) {
                    resp_set_body_result(resp, append_result);
                    goto done;
                }
                report_progress(progress, user_data,
                        (int)bodybuf.len, cl);
            }
            if (cl >= 0) {
                char tmp[2048];
                int  remaining;
                int  got;
                int  want;

                remaining = cl - (int)bodybuf.len;
                while (remaining > 0) {
                    want = remaining < (int)sizeof(tmp)
                           ? remaining : (int)sizeof(tmp);
                    got = PTls_Read(conn, tmp, want);
                    if (got < 0) {
                        resp_set_body_result(resp, PHTTP_BODY_READ_ERROR);
                        goto done;
                    }
                    if (got == 0) {
                        resp_set_error(resp, "response body truncated");
                        resp->status_code = 0;
                        goto done;
                    }
                    append_result = phttp_body_append(&bodybuf, tmp,
                            (size_t)got);
                    if (append_result != PHTTP_BODY_OK) {
                        resp_set_body_result(resp, append_result);
                        goto done;
                    }
                    remaining -= got;
                    report_progress(progress, user_data,
                            (int)bodybuf.len, cl);
                }
            } else {
                char tmp[2048];
                int  got;

                while (1) {
                    got = PTls_Read(conn, tmp, (int)sizeof(tmp));
                    if (got < 0) {
                        resp_set_body_result(resp, PHTTP_BODY_READ_ERROR);
                        goto done;
                    }
                    if (got == 0) {
                        break;
                    }
                    append_result = phttp_body_append(&bodybuf, tmp,
                            (size_t)got);
                    if (append_result != PHTTP_BODY_OK) {
                        resp_set_body_result(resp, append_result);
                        goto done;
                    }
                    report_progress(progress, user_data,
                            (int)bodybuf.len, -1);
                }
            }
        }
        break;   /* final TLS response complete */
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

/* Legacy host/port entry point.  It retains the historical port mapping;
 * callers that need an explicit non-default plaintext port should use the
 * URL-aware entry point so the scheme cannot be lost. */
static PHttpResponse* http_request(const char* method, const char* host,
                                   int port, const char* path,
                                   const char** headers,
                                   const char* body, int body_len,
                                   PHttpProgressCallback progress,
                                   void* user_data)
{
    char url[2048];
    int scheme;
    PHttpResponse* resp;

    if (port == 0) {
        port = 443;
    }
    scheme = port == 80 ? PHTTP_SCHEME_HTTP : PHTTP_SCHEME_HTTPS;
    if (phttp_document_url(scheme, host, port, path, url,
            sizeof(url)) != 0) {
        resp = resp_new();
        if (resp != NULL) {
            resp_set_error(resp, "invalid host/port/path");
        }
        return resp;
    }
    return http_request_url(method, url, headers, body, body_len,
            progress, user_data);
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

PHTTP_API PHttpResponse* PHttp_GetUrl(const char* url,
                                      const char** headers)
{
    return PHttp_GetUrlEx(url, headers, NULL, NULL);
}

PHTTP_API PHttpResponse* PHttp_GetUrlEx(const char* url,
                                        const char** headers,
                                        PHttpProgressCallback progress,
                                        void* user_data)
{
    return http_request_url("GET", url, headers, NULL, 0,
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

PHTTP_API PHttpResponse* PHttp_PostUrl(const char* url,
                                       const char** headers,
                                       const char* body, int body_len)
{
    return PHttp_PostUrlEx(url, headers, body, body_len, NULL, NULL);
}

PHTTP_API PHttpResponse* PHttp_PostUrlEx(const char* url,
                                         const char** headers,
                                         const char* body, int body_len,
                                         PHttpProgressCallback progress,
                                         void* user_data)
{
    return http_request_url("POST", url, headers, body, body_len,
            progress, user_data);
}

PHTTP_API int PHttp_ResponseGetFinalUrl(const PHttpResponse* response,
                                        char* out_url,
                                        int out_url_capacity)
{
    const phttp_response_private* private_response;
    size_t length;

    if (out_url == NULL || out_url_capacity <= 1) {
        return 1;
    }
    out_url[0] = '\0';
    if (response == NULL) {
        return 1;
    }
    private_response = (const phttp_response_private*)response;
    if (private_response->final_url[0] == '\0') {
        return 1;
    }
    length = strlen(private_response->final_url);
    if (length >= (size_t)out_url_capacity) {
        return 1;
    }
    memcpy(out_url, private_response->final_url, length + 1);
    return 0;
}

PHTTP_API void PHttp_FreeResponse(PHttpResponse* resp)
{
    if (resp == NULL) {
        return;
    }
    if (resp->body != NULL) {
        HeapFree(GetProcessHeap(), 0, resp->body);
    }
    HeapFree(GetProcessHeap(), 0, resp_private(resp));
}
