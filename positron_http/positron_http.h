/*
 * positron_http.h - HTTP/1.1 client for the Positron framework.
 * Built on positron_tls; no WinInet, no SChannel.
 *
 * Phase 2 limitations:
 *   - Always HTTPS (port argument exists for clarity / future plain
 *     HTTP, but only :443 is verified).
 *   - "Connection: close" only; no keep-alive in this phase.
 *   - Response body capped at 1 MB to keep WM6 RAM usage bounded.
 *   - Cert validation inherits positron_tls Phase 1 setting
 *     (VERIFY_NONE; CA bundle is Phase 3+).
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

/* Initialize HTTP module. Internally calls PTls_Init.
 * Returns TRUE on success. Safe to call multiple times. */
PHTTP_API BOOL PHttp_Init(void);

/* Cleanup. Internally calls PTls_Cleanup. */
PHTTP_API void PHttp_Cleanup(void);

/*
 * Perform HTTPS GET.
 *
 * host    : "api.example.com"
 * port    : 443 (or 80 in future plain-HTTP scenarios)
 * path    : "/v1/foo?bar=baz" (must start with '/')
 * headers : NULL or a NULL-terminated array of "Key: Value" strings;
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

/* Free a response returned by PHttp_Get / PHttp_Post. NULL-safe. */
PHTTP_API void PHttp_FreeResponse(PHttpResponse* resp);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_HTTP_H */
