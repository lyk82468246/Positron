/*
 * positron_tls.h - Public API for Positron TLS module.
 * Target: Windows Mobile 6 Professional (WinCE 5.2, ARMV4I).
 *
 * All strings on the wire (host, request, response) are 8-bit char.
 * Wide-char is only used by callers for UI display.
 */

#ifndef POSITRON_TLS_H
#define POSITRON_TLS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_TLS_EXPORTS
#  define PTLS_API __declspec(dllexport)
#else
#  define PTLS_API __declspec(dllimport)
#endif

/* Initialize Winsock, global TLS state, the embedded CA bundle, and
 * (best-effort) a CryptoAPI random-number provider. Idempotent. */
PTLS_API BOOL  PTls_Init(void);

/* Tear down everything PTls_Init created. */
PTLS_API void  PTls_Cleanup(void);

/* Append a PEM-encoded root certificate to the trust chain used by
 * PTls_ConnectVerified. Multiple roots can be added; for an enterprise
 * CA bundle, concatenate them in the PEM string. Returns FALSE on
 * parse failure (PTls_LastError describes it). NOT thread-safe; call
 * during process startup before issuing any PTls_ConnectVerified. */
PTLS_API BOOL  PTls_AddRootCA(const char* pem);

/* Resolve host, open TCP socket, perform TLS 1.2 handshake WITHOUT
 * certificate verification. Returned handle is fully usable. Reserved
 * for diagnostic / self-signed scenarios; new code SHOULD prefer
 * PTls_ConnectVerified. */
PTLS_API HANDLE PTls_Connect(const char* host, int port);

/* Timeout-aware variant. timeout_ms covers TCP connect, TLS handshake and
 * each subsequent PTls_Read/PTls_Write wait. A zero value selects the
 * library default. Existing PTls_Connect uses that default. */
PTLS_API HANDLE PTls_ConnectWithTimeout(const char* host, int port,
                                        DWORD timeout_ms);

/* Same as PTls_Connect but verifies the server certificate chain
 * against the embedded CA bundle (plus any roots added via
 * PTls_AddRootCA) AND verifies that the server's certificate matches
 * `host`. On failure returns NULL; PTls_LastError contains a verify
 * info string. */
PTLS_API HANDLE PTls_ConnectVerified(const char* host, int port);

/* Verified timeout-aware variant; ownership matches PTls_ConnectVerified. */
PTLS_API HANDLE PTls_ConnectVerifiedWithTimeout(const char* host, int port,
                                                DWORD timeout_ms);

/* Returns bytes written, or negative on error. */
PTLS_API int   PTls_Write(HANDLE hConn, const char* buf, int len);

/* Returns bytes read, 0 on clean shutdown, negative on error. */
PTLS_API int   PTls_Read(HANDLE hConn, char* buf, int len);

/* Closes TLS, closes socket, frees handle. NULL-safe. */
PTLS_API void  PTls_Close(HANDLE hConn);

/* Static buffer; valid until next API call on this thread. */
PTLS_API const char* PTls_LastError(void);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_TLS_H */
