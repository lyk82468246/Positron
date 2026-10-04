/*
 * positron_http.h - HTTP/1.1 client for the Positron framework.
 * Modern HTTPS is built on positron_tls; plaintext HTTP uses WM WinInet.
 *
 * Legacy complete-body transport contract (streaming GET is additive):
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

/* Maximum UTF-8 URL length accepted by URL-aware entry points and returned
 * by PHttp_ResponseGetFinalUrl. */
#define PHTTP_URL_MAX 2048

/* One-shot streaming GET. Legacy response/body ABI and 1 MiB ceiling remain
 * unchanged. New path is direct IPv4 TCP/TLS, not WinInet/proxy/cache aware. */
#define PHTTP_STREAM_VERSION 1
#define PHTTP_STREAM_OK 0
#define PHTTP_STREAM_ARGUMENT -1
#define PHTTP_STREAM_STATE -2
#define PHTTP_STREAM_CANCELLED -3
#define PHTTP_STREAM_TIMEOUT -4
#define PHTTP_STREAM_TRANSPORT -5
#define PHTTP_STREAM_PROTOCOL -6
#define PHTTP_STREAM_LIMIT -7
#define PHTTP_STREAM_SINK -8
#define PHTTP_STREAM_UNSUPPORTED -9
#define PHTTP_STREAM_MEMORY -10
#define PHTTP_STREAM_HEADERS_MAX_BYTES 16384
#define PHTTP_STREAM_MAX_HEADER_FIELDS 64
#define PHTTP_STREAM_BLOCK_BYTES 4096
#define PHTTP_STREAM_BODY_MAX_BYTES 1073741824UL

typedef struct PHttpStreamResult {
    unsigned long size;
    unsigned long version;
    int result;          /* STREAM_* terminal; not an HTTP status. */
    int transport_error; /* PTLS_TRANSPORT_* detail, 0 otherwise. */
    int status_code;     /* final response status, 0 before final headers. */
    int redirect_hop;    /* <=5; intermediate bodies are never delivered. */
    __int64 received;    /* transfer-decoded bytes accepted by sink. */
    __int64 total;       /* final Content-Length, or -1. */
    char final_url[PHTTP_URL_MAX];
    char content_type[256];
    char content_disposition[512]; /* metadata only, not a safe file name. */
    char content_encoding[64]; /* identity/empty only supported initially. */
} PHttpStreamResult;
typedef int (*PHttpStreamHeadersFn)(void* user_data,
        const PHttpStreamResult* metadata, const char* headers,
        unsigned long header_bytes);
typedef int (*PHttpStreamSinkFn)(void* user_data, const unsigned char* block,
        unsigned long bytes, __int64 received, __int64 total);
typedef struct PHttpStreamOptions {
    unsigned long size;
    unsigned long version;
    unsigned long timeout_ms; /* total request incl. redirects, 1..120000. */
    unsigned long body_limit; /* 1..BODY_MAX, explicitly selected by host. */
    PHttpStreamHeadersFn headers; /* optional, final response once only. */
    PHttpStreamSinkFn sink;       /* required, 0 accept, nonzero stop. */
    void* user_data;
} PHttpStreamOptions;

/* Create copies canonical URL, options and <=32 request headers/8192 bytes.
 * Reserved framing/proxy headers and CR/LF are rejected. Authorization/Cookie
 * are allowed, but carrying them through a cross-origin redirect is rejected
 * as UNSUPPORTED before the next connection, never leaked to the new origin.
 * Outputs unchanged on error.
 * Requires Init, but no network or callbacks. Get executes once on one worker;
 * callbacks synchronous there, borrow arguments until return, no reentry except
 * Cancel. Header callback can reject before any body bytes. Non-2xx is a normal
 * final response; OK means complete transfer, not 2xx or saved-file success.
 * Cancel is the sole cross-thread request API, idempotent before terminal;
 * cancel/completion linearize under a request lock, late cancel returns STATE
 * without changing terminal result. Cancellation is cooperative: no callbacks
 * after Get returns, no worker termination or second socket closer. Deadlines
 * bound I/O waits, not host callbacks/TLS crypto. DNS jobs are bounded/isolated
 * by TLS. Join Get AND all concurrent Cancel callers before Close/Cleanup.
 * Result/Header queries and Close refuse running requests; Close returns STATE
 * without freeing during callbacks. Freed handles cannot be queried/cancelled.
 * A failed transfer may have accepted partial sink bytes: host must discard or
 * explicitly retain them, never publish them as a completed download.
 * Header text is ASCII/valid UTF-8; other obs-text encodings are UNSUPPORTED.
 * Headers total <=16KiB/64 fields, each line <=1023 bytes. Duplicate framing or
 * singleton download fields, conflicting TE/CL, compression and malformed
 * chunks fail closed. Other duplicate fields are retained in arrival order.
 * HTTPS always verifies chain + hostname, rejects downgrade redirects; no
 * insecure streaming option or implicit HTTP retry. HTTP->HTTPS allowed.
 * Chunked strips chunk framing/extensions/trailers, total=-1; counts are 64-bit,
 * chunk/trailer budgets fixed, no response-body accumulation or file I/O. */
PHTTP_API int PHttp_StreamCreateGet(const char* url, const char** headers,
        const PHttpStreamOptions* options, HANDLE* out_request);
PHTTP_API int PHttp_StreamGet(HANDLE request);
PHTTP_API int PHttp_StreamCancel(HANDLE request);
PHTTP_API int PHttp_StreamGetResult(HANDLE request, PHttpStreamResult* result);
PHTTP_API int PHttp_StreamGetHeader(HANDLE request, const char* name,
        unsigned long occurrence, char* output, unsigned long capacity);
PHTTP_API int PHttp_StreamClose(HANDLE request);

/* Request-scoped network phase observation.  This is additive to the legacy
 * progress callback and does not change PHttpResponse.  All text fields are
 * UTF-8, NUL-terminated, fixed-capacity snapshots; no request headers or
 * body bytes are exposed. */
#define PHTTP_OBSERVER_VERSION 1
#define PHTTP_OBSERVER_SCHEME_CAPACITY 8
#define PHTTP_OBSERVER_HOST_CAPACITY 256
#define PHTTP_OBSERVER_ERROR_CODE_CAPACITY 32
#define PHTTP_OBSERVER_ERROR_MESSAGE_CAPACITY 256

#define PHTTP_OBSERVER_FLAG_PHASE_MERGED 0x00000001u

typedef enum PHttpObserverPhase {
    PHTTP_PHASE_NONE = 0,
    PHTTP_PHASE_RESOLVING_NAME = 1,
    PHTTP_PHASE_CONNECTING = 2,
    PHTTP_PHASE_TLS_HANDSHAKE = 3,
    PHTTP_PHASE_SENDING_REQUEST = 4,
    PHTTP_PHASE_WAITING_RESPONSE = 5,
    PHTTP_PHASE_RECEIVING_HEADERS = 6,
    PHTTP_PHASE_RECEIVING_BODY = 7,
    PHTTP_PHASE_REDIRECTING = 8,
    PHTTP_PHASE_COMPLETE = 9,
    PHTTP_PHASE_FAILED = 10
} PHttpObserverPhase;

typedef struct PHttpObserverEvent {
    unsigned int size;
    unsigned int version;
    int          phase;         /* PHttpObserverPhase. */
    int          failure_phase; /* terminal FAILED phase, else NONE. */
    int          redirect_hop;  /* initial request is hop 0. */
    int          status_code;   /* known HTTP status, else -1. */
    int          received;     /* decoded body bytes, else -1. */
    int          total;         /* decoded total, or -1 if unknown. */
    int          port;          /* current target port, else -1. */
    unsigned int flags;
    char         scheme[PHTTP_OBSERVER_SCHEME_CAPACITY];
    char         host[PHTTP_OBSERVER_HOST_CAPACITY];
    char         error_code[PHTTP_OBSERVER_ERROR_CODE_CAPACITY];
    char         error_message[PHTTP_OBSERVER_ERROR_MESSAGE_CAPACITY];
} PHttpObserverEvent;

typedef void (*PHttpObserverCallback)(const PHttpObserverEvent* event,
                                      void* user_data);

typedef struct PHttpObserver {
    unsigned int         size;
    unsigned int         version;
    PHttpObserverCallback callback;
    void*                user_data;
} PHttpObserver;

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
 * network-path reference beginning with "//".  The resolver trims
 * surrounding ASCII spaces, follows directory/query/dot-segment rules, strips fragments,
 * and returns caller-owned UTF-8 host/path buffers.  Userinfo, IPv6,
 * unsupported schemes, malformed ports, C0/DEL control characters and truncation
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

/* Additive observer variants.  The old progress callback remains unchanged.
 * Pass NULL to keep the request unobserved.  A non-NULL observer must have
 * size >= sizeof(PHttpObserver), the current version, and a callback.
 *
 * Events are synchronous on the request thread.  The event and its fixed
 * strings are borrowed until the callback returns; user_data is the exact
 * pointer supplied in the observer.  Callbacks must not re-enter HTTP/TLS,
 * call PHttp_Cleanup, free the event, or treat observation as cancellation.
 * A valid request produces exactly one COMPLETE or FAILED event before the
 * function returns.  COMPLETE means transport/body processing finished; it
 * does not imply a 2xx status.  FAILED reports the last actual phase and a
 * bounded error snapshot.  RECEIVING_BODY may repeat and retains the legacy
 * decoded received/total meaning.  Redirects emit REDIRECTING and restart
 * the target/phase sequence at the next hop. */
PHTTP_API PHttpResponse* PHttp_GetEx2(
    const char*           host,
    int                   port,
    const char*           path,
    const char**          headers,
    PHttpProgressCallback progress,
    void*                 user_data,
    const PHttpObserver*  observer
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

PHTTP_API PHttpResponse* PHttp_GetUrlEx2(
    const char*           url,
    const char**          headers,
    PHttpProgressCallback progress,
    void*                 user_data,
    const PHttpObserver*  observer
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

PHTTP_API PHttpResponse* PHttp_PostEx2(
    const char*           host,
    int                   port,
    const char*           path,
    const char**          headers,
    const char*           body,
    int                   body_len,
    PHttpProgressCallback progress,
    void*                 user_data,
    const PHttpObserver*  observer
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

PHTTP_API PHttpResponse* PHttp_PostUrlEx2(
    const char*           url,
    const char**          headers,
    const char*           body,
    int                   body_len,
    PHttpProgressCallback progress,
    void*                 user_data,
    const PHttpObserver*  observer
);

/* Copy the final absolute URL reached by a response, including the last
 * successful redirect target.  Fragments are excluded because they are
 * browser history state and are never sent on the wire.  The response
 * layout remains ABI-compatible; this additive query is the supported way
 * to retrieve the final URL.  Returns 0 on success.  Invalid input, an empty
 * final URL, or insufficient capacity returns non-zero and writes an empty
 * string without partial output. */
PHTTP_API int PHttp_ResponseGetFinalUrl(
    const PHttpResponse* response,
    char*                out_url,
    int                  out_url_capacity
);

/* Free a response returned by PHttp_Get / PHttp_Post. NULL-safe. */
PHTTP_API void PHttp_FreeResponse(PHttpResponse* resp);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_HTTP_H */
