/*
 * positron_tls.h - Public API for Positron TLS module.
 * Target: Windows Mobile 6 Professional (WinCE 5.2, ARMV4I).
 *
 * Public strings and file paths are UTF-8. Opaque HANDLE values must only be
 * released by their matching PTls_*Close function.
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

#define PTLS_ABI_VERSION 2
#define PTLS_FINGERPRINT_HEX_CAPACITY 65

#define PTLS_SERVER_REQUIRE_CLIENT_CERT 0x0001u

/* Available before PTls_Init. Older DLLs do not export this symbol. */
PTLS_API int PTls_GetAbiVersion(void);

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

/* Same as PTls_Connect but verifies the server certificate chain
 * against the embedded CA bundle (plus any roots added via
 * PTls_AddRootCA) AND verifies that the server's certificate matches
 * `host`. On failure returns NULL; PTls_LastError contains a verify
 * info string. */
PTLS_API HANDLE PTls_ConnectVerified(const char* host, int port);

/* Load a PEM certificate/private-key pair, or create an ECDSA P-256
 * self-signed identity when neither file exists. A one-file, malformed, or
 * mismatched pair fails without replacing either target. PTls_Init must have
 * succeeded. The identity must outlive every listener/connection using it. */
PTLS_API HANDLE PTls_IdentityLoadOrCreate(const char* cert_path_utf8,
                                          const char* key_path_utf8);

/* Release an unused identity. NULL-safe. */
PTLS_API void PTls_IdentityClose(HANDLE hIdentity);

/* Copy the identity certificate DER SHA-256 as 64 uppercase hex characters
 * plus NUL. out_capacity must be at least
 * PTLS_FINGERPRINT_HEX_CAPACITY. */
PTLS_API BOOL PTls_IdentityFingerprint(HANDLE hIdentity,
                                       char* out_hex,
                                       int out_capacity);

/* Bind an IPv4 TLS listener. The current ABI accepts ports 1..65535.
 * handshake_timeout_ms applies to each accepted TLS handshake. */
PTLS_API HANDLE PTls_ServerListen(HANDLE hIdentity,
                                  int port,
                                  unsigned int flags,
                                  int handshake_timeout_ms);

/* Accept one TCP peer and finish its TLS handshake. Only one Accept may be
 * active per listener. remote_ip_utf8 is optional; when supplied its capacity
 * must be at least 16. remote_port is optional. Before processing application
 * data, an authenticated server should compare PTls_PeerFingerprint with its
 * own pairing database. A concurrent ServerClose interrupts the blocking
 * accept/handshake. */
PTLS_API HANDLE PTls_ServerAccept(HANDLE hListener,
                                  char* remote_ip_utf8,
                                  int remote_ip_capacity,
                                  int* remote_port);

/* Stop listening, interrupt an active ServerAccept, wait for it to leave the
 * listener, then release the listener. Existing accepted connections remain
 * valid. NULL-safe. */
PTLS_API void PTls_ServerClose(HANDLE hListener);

/* Connect using a peer identity rather than the Internet CA model. The local
 * identity certificate is sent to the server. A non-empty fingerprint must be
 * exactly 64 hexadecimal digits and is checked against the peer certificate
 * before this function returns. NULL/empty enables discovery-only TOFU and
 * does not authenticate which device answered. timeout_ms covers TCP connect
 * and TLS handshake after name resolution. */
PTLS_API HANDLE PTls_ConnectPeer(const char* host_utf8,
                                 int port,
                                 HANDLE hIdentity,
                                 const char* expected_fingerprint,
                                 int timeout_ms);

/* Copy the connected peer leaf-certificate DER SHA-256 using the same output
 * format as PTls_IdentityFingerprint. */
PTLS_API BOOL PTls_PeerFingerprint(HANDLE hConn,
                                   char* out_hex,
                                   int out_capacity);

/* Returns bytes written, or negative on error. */
PTLS_API int   PTls_Write(HANDLE hConn, const char* buf, int len);

/* Returns bytes read, 0 on clean shutdown, negative on error. */
PTLS_API int   PTls_Read(HANDLE hConn, char* buf, int len);

/* Closes TLS, closes socket, frees handle. NULL-safe. */
PTLS_API void  PTls_Close(HANDLE hConn);

/* Legacy borrowed process-global buffer. Concurrent callers should use
 * PTls_CopyLastError instead. */
PTLS_API const char* PTls_LastError(void);

/* Copy an atomic snapshot of the latest process-wide error. Returns bytes
 * copied excluding NUL, or a negative value for invalid arguments. */
PTLS_API int PTls_CopyLastError(char* out_utf8, int out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_TLS_H */
