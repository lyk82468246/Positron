/*
 * positron_http.h - HTTP/1.1 client for the Positron framework.
 * Modern HTTPS is built on positron_tls; plaintext HTTP uses WM WinInet.
 *
 * Current transport contract:
 *   - HTTPS uses positron_tls; plaintext HTTP uses WM6 WinInet
 *   - "Connection: close"; no keep-alive
 *   - Response body capped at 1 MB
 *   - Cert chain + hostname verified by default via the embedded
 *     CA bundle. Call PHttp_SetInsecure(TRUE) to bypass (intended
 *     for self-signed peers / diagnostics; not for production).
 */

#ifndef POSITRON_HTTP_H
#define POSITRON_HTTP_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_HTTP_EXPORTS
#  define PHTTP_API __declspec(dllexport)
#else
#  define PHTTP_API __declspec(dllimport)
#endif

/* Scheme values used by additive URL-aware helpers.  The legacy host/port
 * entry points remain ABI-compatible; use the URL entry points when a
 * non-default port must retain an explicit http/https scheme. */
#define PHTTP_SCHEME_HTTP  1
#define PHTTP_SCHEME_HTTPS 2

/* Response object returned by PHttp_Get / PHttp_Post.
 * Always free with PHttp_FreeResponse. */
typedef struct PHttpResponse {
    int    status_code;     /* e.g. 200, 404. 0 on transport failure. */
    char*  body;            /* heap-allocated, NUL-terminated.
                               Caller MUST NOT free directly; use
                               PHttp_FreeResponse on the parent. */
    int    body_len;        /* byte count of body excl. terminator */
    char   error_msg[256];  /* non-empty iff a transport-level error
                               occurred (resp may still be non-NULL) */
} PHttpResponse;

/* Resolve one bounded HTTP(S) reference against an existing request origin.
 *
 * base_host/base_port/base_path describe the current request.  base_port 0
 * means the HTTPS default port.  A NULL or
 * empty base host is accepted only for an absolute http(s) reference or a
 * network-path reference beginning with "//".  The resolver trims ASCII
 * whitespace, follows directory/query/dot-segment rules, strips fragments,
 * and returns caller-owned UTF-8 host/path buffers.  Userinfo, IPv6,
 * unsupported schemes, malformed ports, control characters and truncation
 * fail closed.  The path always begins with '/'.
 *
 * Returns 0 on success and non-zero on invalid input or insufficient output
 * capacity.  This helper does not perform network I/O and does not require
 * PHttp_Init. */
PHTTP_API int PHttp_ResolveReference(
    const char* base_host,
    int         base_port,
    const char* base_path,
    const char* reference,
    char*       out_host,
    int         out_host_capacity,
    char*       out_path,
    int         out_path_capacity,
    int*        out_port
);

/* Resolve a reference and retain the complete absolute URL.  base_url may be
 * NULL/empty only when reference is an absolute, network-path, or
 * scheme-less host reference.  A scheme-less reference is treated as HTTPS;
 * it is never silently downgraded to HTTP.  The returned URL is written to
 * caller-owned UTF-8 storage, has a '/' path, strips fragments, applies the
 * scheme's default port, and preserves explicit non-default ports.  This
 * helper performs no network I/O and does not require PHttp_Init. */
PHTTP_API int PHttp_ResolveReferenceUrl(
    const char* base_url,
    const char* reference,
    char*       out_url,
    int         out_url_capacity
);

/* Called synchronously on the thread running PHttp_GetEx/PHttp_PostEx.
 * received is the decoded response-body byte count accumulated so far.
 * total is Content-Length when known, or -1 for chunked/close-delimited
 * responses. Redirected responses may start a new sequence at zero.
 * Keep callbacks short and do not call PHttp_Cleanup from inside one. */
typedef void (*PHttpProgressCallback)(void* user_data,
                                      int received,
                                      int total);

/* Initialize HTTP module. Internally calls PTls_Init. */
PHTTP_API BOOL PHttp_Init(void);

/* Cleanup. Internally calls PTls_Cleanup. */
PHTTP_API void PHttp_Cleanup(void);

/* Toggle certificate verification for all subsequent PHttp_Get /
 * PHttp_Post calls.
 *   FALSE (default): use PTls_ConnectVerified - chain + hostname check.
 *   TRUE           : use PTls_Connect       - no cert checks.
 * Returns the previous setting. */
PHTTP_API BOOL PHttp_SetInsecure(BOOL insecure);

/*
 * Perform a GET using the legacy host/port ABI.  Port 80 selects plaintext
 * HTTP; port 443 and other positive ports select HTTPS.  Port 0 means HTTPS
 * on the default port.  For an explicit plaintext non-default port use
 * PHttp_GetUrl/PHttp_GetUrlEx with an `http://` URL so the scheme is not
 * ambiguous.
 *
 * host    : "api.example.com"
 * port    : 443
 * path    : "/v1/foo?bar=baz" (must start with '/')
 * headers : NULL or NULL-terminated array of "Key: Value" strings;
 *           must NOT include Host, Content-Length, Connection
 *           (those are added automatically).
 *
 * Returns heap-allocated PHttpResponse, NEVER NULL. On transport
 * failure, returned->status_code is 0 and returned->error_msg is set.
 * Caller MUST free with PHttp_FreeResponse.
 */
PHTTP_API PHttpResponse* PHttp_Get(
    const char*  host,
    int          port,
    const char*  path,
    const char** headers
);

/* Progress-reporting GET. The legacy PHttp_Get ABI is unchanged and is
 * equivalent to calling this function with progress == NULL. */
PHTTP_API PHttpResponse* PHttp_GetEx(
    const char*           host,
    int                   port,
    const char*           path,
    const char**          headers,
    PHttpProgressCallback progress,
    void*                 user_data
);

/* URL-aware GET.  The URL may be absolute http(s), or a scheme-less host
 * reference which defaults to HTTPS.  Omitted ports use 80 for http and 443
 * for https; explicit ports are preserved.  HTTPS failures are not retried
 * over HTTP. */
PHTTP_API PHttpResponse* PHttp_GetUrl(
    const char*           url,
    const char**          headers
);

PHTTP_API PHttpResponse* PHttp_GetUrlEx(
    const char*           url,
    const char**          headers,
    PHttpProgressCallback progress,
    void*                 user_data
);

/*
 * Perform a POST using the legacy host/port ABI.  Same conventions as
 * PHttp_Get.  Use PHttp_PostUrl/PHttp_PostUrlEx for an explicit scheme and a
 * non-default port.
 *
 * body     : raw request body bytes (typically JSON)
 * body_len : byte length of body. Pass -1 to use strlen(body).
 *
 * Caller is responsible for setting Content-Type header.
 */
PHTTP_API PHttpResponse* PHttp_Post(
    const char*  host,
    int          port,
    const char*  path,
    const char** headers,
    const char*  body,
    int          body_len
);

/* Progress-reporting POST; response progress has the same semantics as
 * PHttp_GetEx. Upload progress is not reported. */
PHTTP_API PHttpResponse* PHttp_PostEx(
    const char*           host,
    int                   port,
    const char*           path,
    const char**          headers,
    const char*           body,
    int                   body_len,
    PHttpProgressCallback progress,
    void*                 user_data
);

/* URL-aware POST.  Scheme, default port, explicit port and redirect policy
 * follow the URL-aware GET contract. */
PHTTP_API PHttpResponse* PHttp_PostUrl(
    const char*           url,
    const char**          headers,
    const char*           body,
    int                   body_len
);

PHTTP_API PHttpResponse* PHttp_PostUrlEx(
    const char*           url,
    const char**          headers,
    const char*           body,
    int                   body_len,
    PHttpProgressCallback progress,
    void*                 user_data
);

/* Free a response returned by PHttp_Get / PHttp_Post. NULL-safe. */
PHTTP_API void PHttp_FreeResponse(PHttpResponse* resp);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_HTTP_H */
