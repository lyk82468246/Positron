/*
 * positron_tls.c - Implementation of Positron TLS for Windows CE 5.2.
 *
 * Contains:
 *   - DllMain (WSAStartup / WSACleanup)
 *   - Custom mbedtls_hardware_poll (entropy from WinCE timers/IDs)
 *   - Winsock2 BIO callbacks (no gethostbyname dependency in BIO)
 *   - PTls_* exported API
 *
 * C89 only: no slash-slash comments, no mid-block declarations.
 */

#include <windows.h>
#include <winsock2.h>
#include <stdio.h>      /* _snprintf */
#include <string.h>     /* memcpy, memset */

/* mbedTLS public headers */
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"

#include "positron_tls.h"

/* MBEDTLS_NET_C is disabled in our config; net error codes live behind
 * that guard, so re-declare the two we use locally. Values are pinned
 * to mbedTLS 2.28's net_sockets.h. */
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED   -0x004E
#endif
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED   -0x004C
#endif

/* ---------------------------------------------------------------------- */
/* Global state                                                           */
/* ---------------------------------------------------------------------- */

static BOOL              g_initialized      = FALSE;
static CRITICAL_SECTION  g_err_lock;
static BOOL              g_err_lock_inited  = FALSE;
static char              g_last_error[256];
static char              g_last_bio_msg[128];

static const char* PTLS_PERS = "positron_tls_client";

/* ---------------------------------------------------------------------- */
/* Per-connection state                                                    */
/* ---------------------------------------------------------------------- */

typedef struct PTlsConn {
    SOCKET                       sock;
    mbedtls_ssl_context          ssl;
    mbedtls_ssl_config           conf;
    mbedtls_entropy_context      entropy;
    mbedtls_ctr_drbg_context     drbg;
} PTlsConn;

/* ---------------------------------------------------------------------- */
/* Error helpers                                                           */
/*                                                                         */
/* WinCE coredll does not reliably export the ANSI variants (lstrcpynA,    */
/* wsprintfA), so we use the CRT _snprintf and a manual safe-copy.         */
/* _snprintf does NOT null-terminate on truncation, so we force it.        */
/* ---------------------------------------------------------------------- */

static void ptls_safe_copy(char* dst, size_t cap, const char* src)
{
    size_t i;
    if (cap == 0) {
        return;
    }
    for (i = 0; i + 1 < cap && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void ptls_set_error(const char* fmt, int code)
{
    char detail[160];

    if (!g_err_lock_inited) {
        return;
    }
    EnterCriticalSection(&g_err_lock);
    if (code != 0) {
        mbedtls_strerror(code, detail, sizeof(detail));
        if (g_last_bio_msg[0] != '\0') {
            _snprintf(g_last_error, sizeof(g_last_error),
                      "%s: -0x%04X (%s) [BIO: %s]",
                      fmt, (unsigned)-code, detail, g_last_bio_msg);
        } else {
            _snprintf(g_last_error, sizeof(g_last_error),
                      "%s: -0x%04X (%s)", fmt, (unsigned)-code, detail);
        }
        g_last_error[sizeof(g_last_error) - 1] = '\0';
    } else {
        ptls_safe_copy(g_last_error, sizeof(g_last_error), fmt);
    }
    g_last_bio_msg[0] = '\0';
    LeaveCriticalSection(&g_err_lock);
}

static void ptls_set_error_wsa(const char* fmt)
{
    char buf[200];
    int wsaerr;

    wsaerr = WSAGetLastError();
    _snprintf(buf, sizeof(buf), "%s (WSA=%d)", fmt, wsaerr);
    buf[sizeof(buf) - 1] = '\0';
    ptls_set_error(buf, 0);
}

PTLS_API const char* PTls_LastError(void)
{
    return g_last_error;
}

/* ---------------------------------------------------------------------- */
/* Custom hardware entropy for WinCE.                                      */
/* Called by mbedTLS when MBEDTLS_ENTROPY_HARDWARE_ALT is set.            */
/* Quality is low; sufficient for Phase 1. Phase 2 should add CryptoAPI. */
/* ---------------------------------------------------------------------- */

int mbedtls_hardware_poll(void* data, unsigned char* output,
                          size_t len, size_t* olen)
{
    LARGE_INTEGER qpc;
    DWORD         tick;
    DWORD         tid;
    DWORD         pid;
    SYSTEMTIME    st;
    size_t        i;
    BYTE          mix[32];
    size_t        copy;

    (void)data;

    QueryPerformanceCounter(&qpc);
    tick = GetTickCount();
    tid  = GetCurrentThreadId();
    pid  = GetCurrentProcessId();
    GetSystemTime(&st);

    /* Build a 32-byte block by hashing-style mixing.                     */
    /* This is not cryptographic mixing on its own - CTR_DRBG over it is.*/
    memcpy(&mix[0],  &qpc.LowPart,  4);
    memcpy(&mix[4],  &qpc.HighPart, 4);
    memcpy(&mix[8],  &tick,         4);
    memcpy(&mix[12], &tid,          4);
    memcpy(&mix[16], &pid,          4);
    memcpy(&mix[20], &st.wMilliseconds, 2);
    memcpy(&mix[22], &st.wSecond,       2);
    memcpy(&mix[24], &st.wMinute,       2);
    memcpy(&mix[26], &st.wHour,         2);
    memcpy(&mix[28], &st.wDay,          2);
    memcpy(&mix[30], &st.wMonth,        2);

    /* Fold each call into the buffer; caller (entropy module) will         */
    /* call us many times so timing jitter accumulates.                    */
    copy = len;
    if (copy > sizeof(mix)) {
        copy = sizeof(mix);
    }
    for (i = 0; i < copy; i++) {
        output[i] = mix[i] ^ (BYTE)(tick >> ((i & 3) * 8));
    }
    *olen = copy;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* BIO callbacks: route mbedTLS reads/writes to a raw SOCKET.             */
/* ---------------------------------------------------------------------- */

static int ptls_bio_send(void* ctx, const unsigned char* buf, size_t len)
{
    SOCKET s;
    int    n;

    s = *(SOCKET*)ctx;
    n = send(s, (const char*)buf, (int)len, 0);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        _snprintf(g_last_bio_msg, sizeof(g_last_bio_msg),
                  "send WSA=%d", err);
        g_last_bio_msg[sizeof(g_last_bio_msg) - 1] = '\0';
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int ptls_bio_recv(void* ctx, unsigned char* buf, size_t len)
{
    SOCKET s;
    int    n;

    s = *(SOCKET*)ctx;
    n = recv(s, (char*)buf, (int)len, 0);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        _snprintf(g_last_bio_msg, sizeof(g_last_bio_msg),
                  "recv WSA=%d", err);
        g_last_bio_msg[sizeof(g_last_bio_msg) - 1] = '\0';
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (n == 0) {
        ptls_safe_copy(g_last_bio_msg, sizeof(g_last_bio_msg),
                       "recv n=0 (peer closed mid-handshake)");
        return MBEDTLS_ERR_SSL_CONN_EOF;
    }
    return n;
}

/* ---------------------------------------------------------------------- */
/* DNS + TCP connect (WinCE has no getaddrinfo on 5.2).                   */
/* ---------------------------------------------------------------------- */

static SOCKET ptls_tcp_connect(const char* host, int port)
{
    struct hostent*    he;
    struct sockaddr_in sa;
    SOCKET             s;

    he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL) {
        ptls_set_error_wsa("gethostbyname failed");
        return INVALID_SOCKET;
    }

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        ptls_set_error_wsa("socket() failed");
        return INVALID_SOCKET;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((u_short)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], 4);

    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        ptls_set_error_wsa("connect() failed");
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* ---------------------------------------------------------------------- */
/* PTls_Init / PTls_Cleanup                                                */
/* ---------------------------------------------------------------------- */

PTLS_API BOOL PTls_Init(void)
{
    WSADATA wsa;
    int     rc;

    if (g_initialized) {
        return TRUE;
    }
    rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        ptls_set_error("WSAStartup failed", 0);
        return FALSE;
    }
    g_initialized = TRUE;
    return TRUE;
}

PTLS_API void PTls_Cleanup(void)
{
    if (g_initialized) {
        WSACleanup();
        g_initialized = FALSE;
    }
}

/* ---------------------------------------------------------------------- */
/* PTls_Connect                                                            */
/* ---------------------------------------------------------------------- */

PTLS_API HANDLE PTls_Connect(const char* host, int port)
{
    PTlsConn* c;
    int       rc;

    g_last_bio_msg[0] = '\0';

    if (!g_initialized) {
        ptls_set_error("PTls_Init not called", 0);
        return NULL;
    }
    if (host == NULL || *host == '\0') {
        ptls_set_error("host is null/empty", 0);
        return NULL;
    }

    c = (PTlsConn*)LocalAlloc(LPTR, sizeof(PTlsConn));
    if (c == NULL) {
        ptls_set_error("out of memory", 0);
        return NULL;
    }
    c->sock = INVALID_SOCKET;
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->drbg);

    rc = mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy,
                               (const unsigned char*)PTLS_PERS,
                               strlen(PTLS_PERS));
    if (rc != 0) {
        ptls_set_error("ctr_drbg_seed", rc);
        goto fail;
    }

    rc = mbedtls_ssl_config_defaults(&c->conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        ptls_set_error("ssl_config_defaults", rc);
        goto fail;
    }

    /* Phase 1: skip cert verification. Phase 2 will load a CA bundle.    */
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);

    rc = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (rc != 0) {
        ptls_set_error("ssl_setup", rc);
        goto fail;
    }

    rc = mbedtls_ssl_set_hostname(&c->ssl, host);
    if (rc != 0) {
        ptls_set_error("ssl_set_hostname", rc);
        goto fail;
    }

    c->sock = ptls_tcp_connect(host, port);
    if (c->sock == INVALID_SOCKET) {
        goto fail;
    }

    mbedtls_ssl_set_bio(&c->ssl, &c->sock, ptls_bio_send, ptls_bio_recv, NULL);

    /* Drive the handshake to completion (blocking socket). */
    while ((rc = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ &&
            rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ptls_set_error("ssl_handshake", rc);
            goto fail;
        }
    }

    return (HANDLE)c;

fail:
    if (c->sock != INVALID_SOCKET) {
        closesocket(c->sock);
    }
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
    LocalFree(c);
    return NULL;
}

/* ---------------------------------------------------------------------- */
/* PTls_Write / PTls_Read                                                  */
/* ---------------------------------------------------------------------- */

PTLS_API int PTls_Write(HANDLE hConn, const char* buf, int len)
{
    PTlsConn* c;
    int       rc;
    int       total;

    if (hConn == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    c = (PTlsConn*)hConn;
    total = 0;

    while (total < len) {
        rc = mbedtls_ssl_write(&c->ssl,
                               (const unsigned char*)buf + total,
                               (size_t)(len - total));
        if (rc == MBEDTLS_ERR_SSL_WANT_READ ||
            rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (rc < 0) {
            ptls_set_error("ssl_write", rc);
            return rc;
        }
        total += rc;
    }
    return total;
}

PTLS_API int PTls_Read(HANDLE hConn, char* buf, int len)
{
    PTlsConn* c;
    int       rc;

    if (hConn == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    c = (PTlsConn*)hConn;

    for (;;) {
        rc = mbedtls_ssl_read(&c->ssl, (unsigned char*)buf, (size_t)len);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ ||
            rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return 0;
        }
        if (rc < 0) {
            ptls_set_error("ssl_read", rc);
            return rc;
        }
        return rc;
    }
}

/* ---------------------------------------------------------------------- */
/* PTls_Close                                                              */
/* ---------------------------------------------------------------------- */

PTLS_API void PTls_Close(HANDLE hConn)
{
    PTlsConn* c;
    int       rc;

    if (hConn == NULL) {
        return;
    }
    c = (PTlsConn*)hConn;

    /* Best-effort close_notify; ignore errors. */
    do {
        rc = mbedtls_ssl_close_notify(&c->ssl);
    } while (rc == MBEDTLS_ERR_SSL_WANT_READ ||
             rc == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (c->sock != INVALID_SOCKET) {
        closesocket(c->sock);
        c->sock = INVALID_SOCKET;
    }
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
    LocalFree(c);
}

/* ---------------------------------------------------------------------- */
/* DllMain                                                                 */
/* ---------------------------------------------------------------------- */

BOOL WINAPI DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)hModule;
    (void)lpReserved;

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            InitializeCriticalSection(&g_err_lock);
            g_err_lock_inited = TRUE;
            g_last_error[0]   = '\0';
            break;
        case DLL_PROCESS_DETACH:
            if (g_initialized) {
                WSACleanup();
                g_initialized = FALSE;
            }
            if (g_err_lock_inited) {
                DeleteCriticalSection(&g_err_lock);
                g_err_lock_inited = FALSE;
            }
            break;
        default:
            break;
    }
    return TRUE;
}
