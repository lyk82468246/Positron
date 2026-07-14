/*
 * positron_http.h - HTTP/1.1 client for the Positron framework.
 * Modern HTTPS is built on positron_tls; plaintext HTTP uses WM WinInet.
 *
 * Phase 3 status:
 *   - HTTPS only (port is for clarity / future plain HTTP)
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
 * Perform HTTPS GET.
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

/*
 * Perform HTTPS POST. Same conventions as PHttp_Get.
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

/* Free a response returned by PHttp_Get / PHttp_Post. NULL-safe. */
PHTTP_API void PHttp_FreeResponse(PHttpResponse* resp);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_HTTP_H */
