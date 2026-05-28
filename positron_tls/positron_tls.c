/*
 * positron_tls.c - Implementation of Positron TLS for Windows CE 5.2.
 *
 * Contains:
 *   - DllMain (WSAStartup / WSACleanup)
 *   - Custom mbedtls_hardware_poll
 *       primary: CryptGenRandom from CryptoAPI
 *       fallback: timer/ID jitter, accumulated via CTR_DRBG
 *   - Winsock2 BIO callbacks (no gethostbyname dependency in BIO)
 *   - PTls_* exported API
 *       PTls_Connect           - no cert verification (legacy / diag)
 *       PTls_ConnectVerified   - full chain + hostname check
 *       PTls_AddRootCA         - extend trust store at runtime
 *
 * C89 only: no slash-slash comments, no mid-block declarations.
 */

#include <windows.h>
#include <winsock2.h>
#include <wincrypt.h>
#include <stdio.h>      /* _snprintf */
#include <string.h>     /* memcpy, memset */
#include <time.h>       /* struct tm; mbedtls_time_t typedefs through here */

/* mbedTLS public headers */
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"

#include "positron_tls.h"
#include "ca_bundle.h"

/* MBEDTLS_NET_C is disabled in our config; net error codes live behind
 * that guard, so re-declare the two we use locally. Values are pinned
 * to mbedTLS 2.x's net_sockets.h. */
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

/* CryptoAPI random provider, held for the lifetime of the process so we
 * don't pay acquire/release on every entropy poll. Zero if acquisition
 * failed at PTls_Init (we'll fall back to the jitter path). */
static HCRYPTPROV        g_crypt_prov       = 0;

/* Trust store. Parsed once from ca_bundle.h in PTls_Init; extra roots
 * may be appended via PTls_AddRootCA. */
static mbedtls_x509_crt  g_cacert;
static BOOL              g_cacert_inited    = FALSE;

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
/* Time shims. WinCE 5 coredll exposes neither time() nor gmtime_s, so   */
/* we route mbedTLS's clock through GetSystemTimeAsFileTime.              */
/*                                                                         */
/*   positron_time           -> registered via mbedtls_platform_set_time  */
/*                              (MBEDTLS_PLATFORM_TIME_ALT path).         */
/*   mbedtls_platform_gmtime_r -> compile-time symbol override            */
/*                              (MBEDTLS_PLATFORM_GMTIME_R_ALT path).     */
/*                                                                         */
/* FILETIME counts 100-ns ticks since 1601-01-01 UTC.                     */
/* Unix epoch = 1970-01-01 UTC. Delta = 116444736000000000 (100-ns).     */
/* ---------------------------------------------------------------------- */

#define POSITRON_FILETIME_EPOCH_DELTA  116444736000000000

/* External linkage: declared in mbedtls_config.h so platform.c's static
 * initializer for the mbedtls_time function pointer can capture it. */
time_t positron_time(time_t* t)
{
    SYSTEMTIME     st;
    FILETIME       ft;
    ULARGE_INTEGER ui;
    time_t         result;

    /* WinCE 5 lacks GetSystemTimeAsFileTime, so do it in two steps. */
    GetSystemTime(&st);
    if (!SystemTimeToFileTime(&st, &ft)) {
        result = 0;
        if (t != NULL) {
            *t = result;
        }
        return result;
    }

    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    result = (time_t)(
        (ui.QuadPart - POSITRON_FILETIME_EPOCH_DELTA) / 10000000);

    if (t != NULL) {
        *t = result;
    }
    return result;
}

struct tm* mbedtls_platform_gmtime_r(const mbedtls_time_t* tt,
                                     struct tm* tm_buf)
{
    FILETIME       ft;
    SYSTEMTIME     st;
    ULARGE_INTEGER ui;

    if (tt == NULL || tm_buf == NULL) {
        return NULL;
    }

    ui.QuadPart = ((ULONGLONG)(*tt)) * 10000000ULL
                  + POSITRON_FILETIME_EPOCH_DELTA;
    ft.dwLowDateTime  = ui.LowPart;
    ft.dwHighDateTime = ui.HighPart;

    if (!FileTimeToSystemTime(&ft, &st)) {
        return NULL;
    }

    tm_buf->tm_sec   = (int)st.wSecond;
    tm_buf->tm_min   = (int)st.wMinute;
    tm_buf->tm_hour  = (int)st.wHour;
    tm_buf->tm_mday  = (int)st.wDay;
    tm_buf->tm_mon   = (int)st.wMonth - 1;
    tm_buf->tm_year  = (int)st.wYear  - 1900;
    tm_buf->tm_wday  = (int)st.wDayOfWeek;
    tm_buf->tm_yday  = 0;
    tm_buf->tm_isdst = 0;
    return tm_buf;
}

/* ---------------------------------------------------------------------- */
/* Entropy. Primary path is CryptGenRandom (CryptoAPI). Fallback is a    */
/* timer/ID jitter mix; mbedTLS CTR_DRBG accumulates it across many calls*/
/* so the result is still usable, just lower quality.                     */
/* ---------------------------------------------------------------------- */

static int hwpoll_jitter(unsigned char* output, size_t len, size_t* olen)
{
    LARGE_INTEGER qpc;
    DWORD         tick;
    DWORD         tid;
    DWORD         pid;
    SYSTEMTIME    st;
    size_t        i;
    BYTE          mix[32];
    size_t        copy;

    QueryPerformanceCounter(&qpc);
    tick = GetTickCount();
    tid  = GetCurrentThreadId();
    pid  = GetCurrentProcessId();
    GetSystemTime(&st);

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

int mbedtls_hardware_poll(void* data, unsigned char* output,
                          size_t len, size_t* olen)
{
    (void)data;

    if (g_crypt_prov != 0) {
        if (CryptGenRandom(g_crypt_prov, (DWORD)len, (BYTE*)output)) {
            *olen = len;
            return 0;
        }
        /* CryptGenRandom failed unexpectedly; fall through to jitter. */
    }
    return hwpoll_jitter(output, len, olen);
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

    /* Route mbedTLS clock through positron_time (FILETIME-based). */
    /* gmtime_r is symbol-replaced at link time, no registration needed. */
    mbedtls_platform_set_time(positron_time);

    /* Best-effort: hold one CryptoAPI provider for entire process. */
    /* VERIFYCONTEXT: no key container needed, just random / hashes. */
    /* SILENT:        never prompt for UI on WM6.                    */
    if (!CryptAcquireContextW(&g_crypt_prov, NULL, NULL,
                              PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        g_crypt_prov = 0;
        /* Not fatal - hardware_poll falls back to jitter. */
    }

    /* Parse embedded CA bundle into the global trust store. */
    mbedtls_x509_crt_init(&g_cacert);
    g_cacert_inited = TRUE;
    rc = mbedtls_x509_crt_parse(&g_cacert,
                                (const unsigned char*)g_ca_bundle_pem,
                                g_ca_bundle_pem_len);
    if (rc < 0) {
        ptls_set_error("ca_bundle parse", rc);
        mbedtls_x509_crt_free(&g_cacert);
        g_cacert_inited = FALSE;
        if (g_crypt_prov != 0) {
            CryptReleaseContext(g_crypt_prov, 0);
            g_crypt_prov = 0;
        }
        WSACleanup();
        return FALSE;
    }
    /* rc > 0 means: this many certs failed to parse but others succeeded.
     * That's acceptable - we go with whatever roots loaded.            */

    g_initialized = TRUE;
    return TRUE;
}

PTLS_API void PTls_Cleanup(void)
{
    if (!g_initialized) {
        return;
    }
    if (g_cacert_inited) {
        mbedtls_x509_crt_free(&g_cacert);
        g_cacert_inited = FALSE;
    }
    if (g_crypt_prov != 0) {
        CryptReleaseContext(g_crypt_prov, 0);
        g_crypt_prov = 0;
    }
    WSACleanup();
    g_initialized = FALSE;
}

/* ---------------------------------------------------------------------- */
/* PTls_AddRootCA                                                          */
/* ---------------------------------------------------------------------- */

PTLS_API BOOL PTls_AddRootCA(const char* pem)
{
    int rc;
    size_t len;

    if (!g_initialized || !g_cacert_inited) {
        ptls_set_error("PTls_Init not called", 0);
        return FALSE;
    }
    if (pem == NULL || *pem == '\0') {
        ptls_set_error("AddRootCA: empty PEM", 0);
        return FALSE;
    }
    len = strlen(pem) + 1;   /* mbedTLS PEM parser wants NUL counted. */
    rc = mbedtls_x509_crt_parse(&g_cacert,
                                (const unsigned char*)pem,
                                len);
    if (rc < 0) {
        ptls_set_error("AddRootCA parse", rc);
        return FALSE;
    }
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Internal connect helper. Verify-mode controls whether we require a    */
/* trusted chain + hostname match. On success returns the live conn.     */
/* On failure returns NULL after writing to g_last_error.                 */
/* ---------------------------------------------------------------------- */

static PTlsConn* tls_connect_internal(const char* host, int port,
                                      int verify_mode)
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

    mbedtls_ssl_conf_authmode(&c->conf, verify_mode);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);

    if (verify_mode == MBEDTLS_SSL_VERIFY_REQUIRED) {
        mbedtls_ssl_conf_ca_chain(&c->conf, &g_cacert, NULL);
    }

    rc = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (rc != 0) {
        ptls_set_error("ssl_setup", rc);
        goto fail;
    }

    /* SNI + hostname-check input. Always set; mbedTLS only uses it for
     * hostname check when verify_mode != VERIFY_NONE.                    */
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
            /* If the failure was specifically certificate verification,
             * render the verify flags into a human-readable message so
             * callers learn whether it was expiry / hostname / chain. */
            if (verify_mode == MBEDTLS_SSL_VERIFY_REQUIRED) {
                uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);
                if (flags != 0 && flags != (uint32_t)-1) {
                    char info[400];
                    int  n;
                    info[0] = '\0';
                    n = mbedtls_x509_crt_verify_info(info, sizeof(info),
                                                    "", flags);
                    if (n > 0) {
                        /* Strip trailing newline mbedTLS adds. */
                        if (info[n - 1] == '\n') {
                            info[n - 1] = '\0';
                        }
                    }
                    EnterCriticalSection(&g_err_lock);
                    _snprintf(g_last_error, sizeof(g_last_error),
                              "verify failed (flags=0x%08X): %s",
                              (unsigned)flags, info);
                    g_last_error[sizeof(g_last_error) - 1] = '\0';
                    LeaveCriticalSection(&g_err_lock);
                    goto fail;
                }
            }
            ptls_set_error("ssl_handshake", rc);
            goto fail;
        }
    }

    return c;

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
/* PTls_Connect / PTls_ConnectVerified                                     */
/* ---------------------------------------------------------------------- */

PTLS_API HANDLE PTls_Connect(const char* host, int port)
{
    return (HANDLE)tls_connect_internal(host, port,
                                        MBEDTLS_SSL_VERIFY_NONE);
}

PTLS_API HANDLE PTls_ConnectVerified(const char* host, int port)
{
    return (HANDLE)tls_connect_internal(host, port,
                                        MBEDTLS_SSL_VERIFY_REQUIRED);
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
                if (g_cacert_inited) {
                    mbedtls_x509_crt_free(&g_cacert);
                    g_cacert_inited = FALSE;
                }
                if (g_crypt_prov != 0) {
                    CryptReleaseContext(g_crypt_prov, 0);
                    g_crypt_prov = 0;
                }
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
