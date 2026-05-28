/*
 * positron_http.c - HTTP/1.1 client over positron_tls.
 *
 * Lives on top of positron_tls (linked directly via .lib import).
 * No WinInet. No keep-alive in this phase (Connection: close).
 *
 * C89 only: no slash-slash comments, no mid-block declarations.
 */

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "positron_tls.h"

#define POSITRON_HTTP_EXPORTS
#include "positron_http.h"

#define MAX_RESP_BODY    (1 * 1024 * 1024)   /* 1 MB cap */
#define INITIAL_BUFCAP   8192

static BOOL g_initialized = FALSE;

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
                          bytebuf* out_body)
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
        pos += chunk_size + 2;   /* skip data + trailing \r\n */
    }
}

/* ---- worker ------------------------------------------------------ */

static PHttpResponse* http_request(const char* method, const char* host,
                                   int port, const char* path,
                                   const char** headers,
                                   const char* body, int body_len)
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

    request = build_request(method, host, path, headers, body, body_len);
    if (request == NULL) {
        resp_set_error(resp, "OOM building request");
        goto done;
    }

    conn = PTls_Connect(host, port);
    if (conn == NULL) {
        resp_set_error(resp, PTls_LastError());
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

    resp->status_code = parse_status(recvbuf.data, (size_t)body_start);

    if (bb_init(&bodybuf) != 0) {
        resp_set_error(resp, "OOM body buffer");
        goto done;
    }

    if (is_chunked(recvbuf.data, (size_t)body_start)) {
        const char* prefix = recvbuf.data + body_start;
        int prefix_len = (int)recvbuf.len - body_start;
        if (decode_chunked(conn, prefix, prefix_len, &bodybuf) != 0) {
            resp_set_error(resp, "chunked decode failed");
            /* still keep partial body */
        }
    } else {
        cl = parse_content_length(recvbuf.data, (size_t)body_start);
        if (recvbuf.len > (size_t)body_start) {
            bb_append(&bodybuf,
                      recvbuf.data + body_start,
                      recvbuf.len - (size_t)body_start);
        }
        if (cl > 0) {
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
                if (bodybuf.len >= MAX_RESP_BODY) {
                    break;
                }
            }
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
    return http_request("GET", host, port, path, headers, NULL, 0);
}

PHTTP_API PHttpResponse* PHttp_Post(const char* host, int port,
                                    const char* path,
                                    const char** headers,
                                    const char* body, int body_len)
{
    return http_request("POST", host, port, path, headers, body, body_len);
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
