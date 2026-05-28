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

/* Initialize Winsock and global TLS state. Idempotent. */
PTLS_API BOOL  PTls_Init(void);

/* Tear down Winsock and global TLS state. */
PTLS_API void  PTls_Cleanup(void);

/* Resolve host, open TCP socket, perform TLS 1.2 handshake.
 * Returns opaque handle, or NULL on failure (see PTls_LastError).
 * NOTE: Phase 1 uses MBEDTLS_SSL_VERIFY_NONE - no cert chain check. */
PTLS_API HANDLE PTls_Connect(const char* host, int port);

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
