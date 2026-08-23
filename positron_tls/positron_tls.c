/*
 * positron_tls.c - Implementation of Positron TLS for Windows CE 5.2.
 *
 * Contains:
 *   - DllMain (process locks)
 *   - Custom mbedtls_hardware_poll
 *       primary: CryptGenRandom from CryptoAPI
 *       fallback: timer/ID jitter, accumulated via CTR_DRBG
 *   - Winsock2 BIO callbacks (no gethostbyname dependency in BIO)
 *   - PTls_* exported API
 *       PTls_Connect           - no cert verification (legacy / diag)
 *       PTls_ConnectVerified   - full chain + hostname check
 *       PTls_AddRootCA         - extend trust store at runtime
 *       PTls_Identity*         - persistent ECDSA peer identity
 *       PTls_ConnectPeer       - client certificate + DER SHA-256 pinning
 *       PTls_Server*           - bounded TLS listener / accept lifecycle
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
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/sha256.h"
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
static CRITICAL_SECTION  g_state_lock;
static BOOL              g_state_lock_inited = FALSE;
static CRITICAL_SECTION  g_identity_lock;
static BOOL              g_identity_lock_inited = FALSE;
static char              g_last_error[256];

/* CryptoAPI random provider, held for the lifetime of the process so we
 * don't pay acquire/release on every entropy poll. Zero if acquisition
 * failed at PTls_Init (we'll fall back to the jitter path). */
static HCRYPTPROV        g_crypt_prov       = 0;

/* Trust store. Parsed once from ca_bundle.h in PTls_Init; extra roots
 * may be appended via PTls_AddRootCA. */
static mbedtls_x509_crt  g_cacert;
static BOOL              g_cacert_inited    = FALSE;

static const char* PTLS_PERS = "positron_tls_v2";

/* ---------------------------------------------------------------------- */
/* Per-connection state                                                    */
/* ---------------------------------------------------------------------- */

typedef struct PTlsConn {
    SOCKET                       sock;
    mbedtls_ssl_context          ssl;
    mbedtls_ssl_config           conf;
    mbedtls_entropy_context      entropy;
    mbedtls_ctr_drbg_context     drbg;
    char                         bio_msg[128];
} PTlsConn;

typedef struct PTlsIdentity {
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
} PTlsIdentity;

typedef struct PTlsListener {
    SOCKET listen_sock;
    SOCKET pending_sock;
    PTlsIdentity* identity;
    unsigned int flags;
    int handshake_timeout_ms;
    CRITICAL_SECTION lock;
    HANDLE accept_idle;
    BOOL closing;
    BOOL accept_active;
} PTlsListener;

#define PTLS_IDENTITY_FILE_MAX 32768
#define PTLS_CERT_PEM_CAPACITY 8192
#define PTLS_KEY_PEM_CAPACITY 4096
#define PTLS_MIN_TIMEOUT_MS 1

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
        _snprintf(g_last_error, sizeof(g_last_error),
                  "%s: -0x%04X (%s)", fmt, (unsigned)-code, detail);
        g_last_error[sizeof(g_last_error) - 1] = '\0';
    } else {
        ptls_safe_copy(g_last_error, sizeof(g_last_error), fmt);
    }
    LeaveCriticalSection(&g_err_lock);
}

static void ptls_set_error_bio(const char* fmt, int code, const char* bio)
{
    char detail[160];

    if (!g_err_lock_inited) {
        return;
    }
    EnterCriticalSection(&g_err_lock);
    mbedtls_strerror(code, detail, sizeof(detail));
    if (bio != NULL && bio[0] != '\0') {
        _snprintf(g_last_error, sizeof(g_last_error),
                  "%s: -0x%04X (%s) [BIO: %s]",
                  fmt, (unsigned)-code, detail, bio);
    } else {
        _snprintf(g_last_error, sizeof(g_last_error),
                  "%s: -0x%04X (%s)", fmt, (unsigned)-code, detail);
    }
    g_last_error[sizeof(g_last_error) - 1] = '\0';
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

PTLS_API int PTls_CopyLastError(char* out_utf8, int out_capacity)
{
    int len;

    if (out_utf8 == NULL || out_capacity <= 0 || !g_err_lock_inited) {
        return -1;
    }
    EnterCriticalSection(&g_err_lock);
    ptls_safe_copy(out_utf8, (size_t)out_capacity, g_last_error);
    len = (int)strlen(out_utf8);
    LeaveCriticalSection(&g_err_lock);
    return len;
}

PTLS_API int PTls_GetAbiVersion(void)
{
    return PTLS_ABI_VERSION;
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
    PTlsConn* c;
    SOCKET s;
    int    n;

    c = (PTlsConn*)ctx;
    s = c->sock;
    n = send(s, (const char*)buf, (int)len, 0);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        _snprintf(c->bio_msg, sizeof(c->bio_msg),
                  "send WSA=%d", err);
        c->bio_msg[sizeof(c->bio_msg) - 1] = '\0';
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int ptls_bio_recv(void* ctx, unsigned char* buf, size_t len)
{
    PTlsConn* c;
    SOCKET s;
    int    n;

    c = (PTlsConn*)ctx;
    s = c->sock;
    n = recv(s, (char*)buf, (int)len, 0);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        _snprintf(c->bio_msg, sizeof(c->bio_msg),
                  "recv WSA=%d", err);
        c->bio_msg[sizeof(c->bio_msg) - 1] = '\0';
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (n == 0) {
        ptls_safe_copy(c->bio_msg, sizeof(c->bio_msg),
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

static int ptls_remaining_ms(DWORD started, int timeout_ms)
{
    DWORD elapsed;

    elapsed = GetTickCount() - started;
    if (elapsed >= (DWORD)timeout_ms) {
        return 0;
    }
    return timeout_ms - (int)elapsed;
}

static int ptls_set_socket_blocking(SOCKET sock, int blocking)
{
    u_long nonblocking;

    nonblocking = blocking ? 0 : 1;
    if (ioctlsocket(sock, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        ptls_set_error_wsa("ioctlsocket(FIONBIO) failed");
        return 0;
    }
    return 1;
}

static SOCKET ptls_tcp_connect_timed(const char* host, int port,
                                     DWORD started, int timeout_ms)
{
    struct hostent* he;
    struct sockaddr_in sa;
    SOCKET s;
    int rc;
    int remaining;
    fd_set write_set;
    fd_set error_set;
    struct timeval tv;
    int socket_error;
    int socket_error_len;

    he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL) {
        ptls_set_error_wsa("gethostbyname failed");
        return INVALID_SOCKET;
    }
    remaining = ptls_remaining_ms(started, timeout_ms);
    if (remaining <= 0) {
        ptls_set_error("TCP connect timeout", 0);
        return INVALID_SOCKET;
    }

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        ptls_set_error_wsa("socket() failed");
        return INVALID_SOCKET;
    }
    if (!ptls_set_socket_blocking(s, 0)) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    rc = connect(s, (struct sockaddr*)&sa, sizeof(sa));
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK &&
        WSAGetLastError() != WSAEINPROGRESS) {
        ptls_set_error_wsa("connect() failed");
        closesocket(s);
        return INVALID_SOCKET;
    }
    if (rc == SOCKET_ERROR) {
        remaining = ptls_remaining_ms(started, timeout_ms);
        if (remaining <= 0) {
            ptls_set_error("TCP connect timeout", 0);
            closesocket(s);
            return INVALID_SOCKET;
        }
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        FD_SET(s, &write_set);
        FD_SET(s, &error_set);
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;
        rc = select(0, NULL, &write_set, &error_set, &tv);
        if (rc == 0) {
            ptls_set_error("TCP connect timeout", 0);
            closesocket(s);
            return INVALID_SOCKET;
        }
        if (rc == SOCKET_ERROR || FD_ISSET(s, &error_set)) {
            socket_error = 0;
            socket_error_len = sizeof(socket_error);
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&socket_error,
                       &socket_error_len);
            WSASetLastError(socket_error != 0 ? socket_error : WSAECONNREFUSED);
            ptls_set_error_wsa("connect() failed");
            closesocket(s);
            return INVALID_SOCKET;
        }
    }

    remaining = ptls_remaining_ms(started, timeout_ms);
    if (remaining <= 0) {
        ptls_set_error("TCP connect timeout", 0);
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

    if (!g_state_lock_inited) {
        return FALSE;
    }
    EnterCriticalSection(&g_state_lock);
    if (g_initialized) {
        LeaveCriticalSection(&g_state_lock);
        return TRUE;
    }
    rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        ptls_set_error("WSAStartup failed", 0);
        LeaveCriticalSection(&g_state_lock);
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

    /* Parse the embedded CA bundle (one PEM string per root) into the global
     * trust store. mbedtls_x509_crt_parse appends to the chain, so loop. */
    mbedtls_x509_crt_init(&g_cacert);
    g_cacert_inited = TRUE;
    {
        int i;
        int loaded = 0;
        for (i = 0; i < g_ca_cert_count; i++) {
            const char *pem = g_ca_certs[i];
            if (mbedtls_x509_crt_parse(&g_cacert,
                                       (const unsigned char*)pem,
                                       strlen(pem) + 1) == 0) {
                loaded++;
            }
        }
        if (loaded == 0) {
            ptls_set_error("ca_bundle parse", -1);
            mbedtls_x509_crt_free(&g_cacert);
            g_cacert_inited = FALSE;
            if (g_crypt_prov != 0) {
                CryptReleaseContext(g_crypt_prov, 0);
                g_crypt_prov = 0;
            }
            WSACleanup();
            LeaveCriticalSection(&g_state_lock);
            return FALSE;
        }
    }
    /* Individual certs that fail to parse are skipped; we go with whatever
     * roots loaded successfully.                                          */

    g_initialized = TRUE;
    LeaveCriticalSection(&g_state_lock);
    return TRUE;
}

PTLS_API void PTls_Cleanup(void)
{
    if (!g_state_lock_inited) {
        return;
    }
    EnterCriticalSection(&g_state_lock);
    if (!g_initialized) {
        LeaveCriticalSection(&g_state_lock);
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
    LeaveCriticalSection(&g_state_lock);
}

/* ---------------------------------------------------------------------- */
/* PTls_AddRootCA                                                          */
/* ---------------------------------------------------------------------- */

PTLS_API BOOL PTls_AddRootCA(const char* pem)
{
    int rc;
    size_t len;

    if (!g_state_lock_inited) {
        return FALSE;
    }
    EnterCriticalSection(&g_state_lock);
    if (!g_initialized || !g_cacert_inited) {
        ptls_set_error("PTls_Init not called", 0);
        LeaveCriticalSection(&g_state_lock);
        return FALSE;
    }
    if (pem == NULL || *pem == '\0') {
        ptls_set_error("AddRootCA: empty PEM", 0);
        LeaveCriticalSection(&g_state_lock);
        return FALSE;
    }
    len = strlen(pem) + 1;   /* mbedTLS PEM parser wants NUL counted. */
    rc = mbedtls_x509_crt_parse(&g_cacert,
                                (const unsigned char*)pem,
                                len);
    if (rc < 0) {
        ptls_set_error("AddRootCA parse", rc);
        LeaveCriticalSection(&g_state_lock);
        return FALSE;
    }
    LeaveCriticalSection(&g_state_lock);
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Persistent peer identity                                               */
/* ---------------------------------------------------------------------- */

static int ptls_utf8_path(const char* path_utf8, WCHAR* path_wide,
                          int wide_capacity)
{
    int needed;

    if (path_utf8 == NULL || path_utf8[0] == '\0' ||
        path_wide == NULL || wide_capacity <= 0) {
        ptls_set_error("identity path is null/empty", 0);
        return 0;
    }
    needed = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, NULL, 0);
    if (needed <= 0 || needed > wide_capacity ||
        MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1,
                            path_wide, wide_capacity) <= 0) {
        ptls_set_error("identity path is invalid UTF-8 or too long", 0);
        return 0;
    }
    return 1;
}

static int ptls_file_exists(const WCHAR* path, const char* label)
{
    DWORD attributes;
    DWORD error;
    char message[96];

    attributes = GetFileAttributesW(path);
    if (attributes != 0xFFFFFFFF) {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            _snprintf(message, sizeof(message), "%s path is a directory", label);
            message[sizeof(message) - 1] = '\0';
            ptls_set_error(message, 0);
            return -1;
        }
        return 1;
    }
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return 0;
    }
    _snprintf(message, sizeof(message), "%s status failed (Win32=%lu)",
              label, (unsigned long)error);
    message[sizeof(message) - 1] = '\0';
    ptls_set_error(message, 0);
    return -1;
}

static unsigned char* ptls_read_file(const WCHAR* path, const char* label,
                                     DWORD* out_size)
{
    HANDLE file;
    DWORD size;
    DWORD read_count;
    unsigned char* data;
    char message[96];

    *out_size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        _snprintf(message, sizeof(message), "%s open failed (Win32=%lu)",
                  label, (unsigned long)GetLastError());
        message[sizeof(message) - 1] = '\0';
        ptls_set_error(message, 0);
        return NULL;
    }
    size = GetFileSize(file, NULL);
    if (size == 0xFFFFFFFF || size == 0 || size > PTLS_IDENTITY_FILE_MAX) {
        CloseHandle(file);
        _snprintf(message, sizeof(message), "%s size is invalid", label);
        message[sizeof(message) - 1] = '\0';
        ptls_set_error(message, 0);
        return NULL;
    }
    data = (unsigned char*)LocalAlloc(LPTR, size + 1);
    if (data == NULL) {
        CloseHandle(file);
        ptls_set_error("identity file allocation failed", 0);
        return NULL;
    }
    read_count = 0;
    if (!ReadFile(file, data, size, &read_count, NULL) || read_count != size) {
        _snprintf(message, sizeof(message), "%s read failed (Win32=%lu)",
                  label, (unsigned long)GetLastError());
        message[sizeof(message) - 1] = '\0';
        ptls_set_error(message, 0);
        CloseHandle(file);
        LocalFree(data);
        return NULL;
    }
    CloseHandle(file);
    data[size] = '\0';
    *out_size = size;
    return data;
}

static size_t ptls_parse_length(const unsigned char* data, DWORD size)
{
    if (size >= 10 && memcmp(data, "-----BEGIN", 10) == 0) {
        return (size_t)size + 1;
    }
    return (size_t)size;
}

static PTlsIdentity* ptls_identity_load(const WCHAR* cert_path,
                                        const WCHAR* key_path)
{
    PTlsIdentity* identity;
    unsigned char* cert_data;
    unsigned char* key_data;
    DWORD cert_size;
    DWORD key_size;
    int rc;

    identity = NULL;
    cert_data = ptls_read_file(cert_path, "identity certificate", &cert_size);
    if (cert_data == NULL) {
        return NULL;
    }
    key_data = ptls_read_file(key_path, "identity private key", &key_size);
    if (key_data == NULL) {
        LocalFree(cert_data);
        return NULL;
    }
    identity = (PTlsIdentity*)LocalAlloc(LPTR, sizeof(PTlsIdentity));
    if (identity == NULL) {
        ptls_set_error("identity allocation failed", 0);
        goto fail;
    }
    mbedtls_x509_crt_init(&identity->cert);
    mbedtls_pk_init(&identity->key);
    rc = mbedtls_x509_crt_parse(&identity->cert, cert_data,
                                ptls_parse_length(cert_data, cert_size));
    if (rc != 0) {
        ptls_set_error("certificate parse failed", rc);
        goto fail;
    }
    rc = mbedtls_pk_parse_key(&identity->key, key_data,
                              ptls_parse_length(key_data, key_size),
                              NULL, 0);
    if (rc != 0) {
        ptls_set_error("private key parse failed", rc);
        goto fail;
    }
    rc = mbedtls_pk_check_pair(&identity->cert.pk, &identity->key);
    if (rc != 0) {
        ptls_set_error("certificate/private key mismatch", rc);
        goto fail;
    }
    LocalFree(key_data);
    LocalFree(cert_data);
    return identity;

fail:
    if (identity != NULL) {
        mbedtls_pk_free(&identity->key);
        mbedtls_x509_crt_free(&identity->cert);
        LocalFree(identity);
    }
    LocalFree(key_data);
    LocalFree(cert_data);
    return NULL;
}

static int ptls_format_validity(char not_before[15], char not_after[15])
{
    SYSTEMTIME now;
    SYSTEMTIME before;
    SYSTEMTIME after;
    SYSTEMTIME cap_time;
    FILETIME now_file;
    FILETIME before_file;
    FILETIME after_file;
    FILETIME cap_file;
    ULARGE_INTEGER value;
    ULARGE_INTEGER cap;

    GetSystemTime(&now);
    if (now.wYear < 2020 || now.wYear > 2037 ||
        !SystemTimeToFileTime(&now, &now_file)) {
        ptls_set_error("device UTC clock is outside 2020-2037", 0);
        return 0;
    }
    value.LowPart = now_file.dwLowDateTime;
    value.HighPart = now_file.dwHighDateTime;
    value.QuadPart -= 864000000000ULL;
    before_file.dwLowDateTime = value.LowPart;
    before_file.dwHighDateTime = value.HighPart;
    if (!FileTimeToSystemTime(&before_file, &before)) {
        ptls_set_error("identity notBefore calculation failed", 0);
        return 0;
    }

    value.LowPart = now_file.dwLowDateTime;
    value.HighPart = now_file.dwHighDateTime;
    value.QuadPart += 3153600000000000ULL;
    memset(&cap_time, 0, sizeof(cap_time));
    cap_time.wYear = 2037;
    cap_time.wMonth = 12;
    cap_time.wDay = 31;
    cap_time.wHour = 23;
    cap_time.wMinute = 59;
    cap_time.wSecond = 59;
    if (!SystemTimeToFileTime(&cap_time, &cap_file)) {
        ptls_set_error("identity validity cap calculation failed", 0);
        return 0;
    }
    cap.LowPart = cap_file.dwLowDateTime;
    cap.HighPart = cap_file.dwHighDateTime;
    if (value.QuadPart > cap.QuadPart) {
        value.QuadPart = cap.QuadPart;
    }
    after_file.dwLowDateTime = value.LowPart;
    after_file.dwHighDateTime = value.HighPart;
    if (!FileTimeToSystemTime(&after_file, &after)) {
        ptls_set_error("identity notAfter calculation failed", 0);
        return 0;
    }
    _snprintf(not_before, 15, "%04u%02u%02u%02u%02u%02u",
              (unsigned)before.wYear, (unsigned)before.wMonth,
              (unsigned)before.wDay, (unsigned)before.wHour,
              (unsigned)before.wMinute, (unsigned)before.wSecond);
    _snprintf(not_after, 15, "%04u%02u%02u%02u%02u%02u",
              (unsigned)after.wYear, (unsigned)after.wMonth,
              (unsigned)after.wDay, (unsigned)after.wHour,
              (unsigned)after.wMinute, (unsigned)after.wSecond);
    not_before[14] = '\0';
    not_after[14] = '\0';
    return 1;
}

static int ptls_temp_path(const WCHAR* target, WCHAR* temp, int capacity)
{
    static const WCHAR suffix[] = L".ptls.tmp";
    int target_len;
    int suffix_len;

    target_len = lstrlenW(target);
    suffix_len = lstrlenW(suffix);
    if (target_len + suffix_len + 1 > capacity) {
        ptls_set_error("identity temporary path is too long", 0);
        return 0;
    }
    lstrcpyW(temp, target);
    lstrcatW(temp, suffix);
    return 1;
}

static int ptls_write_new_file(const WCHAR* path,
                               const unsigned char* data, DWORD size,
                               const char* label)
{
    HANDLE file;
    DWORD written;
    char message[96];

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        _snprintf(message, sizeof(message), "%s create failed (Win32=%lu)",
                  label, (unsigned long)GetLastError());
        message[sizeof(message) - 1] = '\0';
        ptls_set_error(message, 0);
        return 0;
    }
    written = 0;
    if (!WriteFile(file, data, size, &written, NULL) || written != size ||
        !FlushFileBuffers(file)) {
        _snprintf(message, sizeof(message), "%s write failed (Win32=%lu)",
                  label, (unsigned long)GetLastError());
        message[sizeof(message) - 1] = '\0';
        ptls_set_error(message, 0);
        CloseHandle(file);
        DeleteFileW(path);
        return 0;
    }
    CloseHandle(file);
    return 1;
}

static int ptls_persist_identity(const WCHAR* cert_path,
                                 const WCHAR* key_path,
                                 const unsigned char* cert_pem,
                                 const unsigned char* key_pem)
{
    WCHAR cert_temp[MAX_PATH];
    WCHAR key_temp[MAX_PATH];
    int committed_cert;

    if (!ptls_temp_path(cert_path, cert_temp, MAX_PATH) ||
        !ptls_temp_path(key_path, key_temp, MAX_PATH)) {
        return 0;
    }
    DeleteFileW(cert_temp);
    DeleteFileW(key_temp);
    if (!ptls_write_new_file(cert_temp, cert_pem,
                             (DWORD)strlen((const char*)cert_pem),
                             "identity certificate temporary file")) {
        return 0;
    }
    if (!ptls_write_new_file(key_temp, key_pem,
                             (DWORD)strlen((const char*)key_pem),
                             "identity private-key temporary file")) {
        DeleteFileW(cert_temp);
        return 0;
    }

    committed_cert = 0;
    if (!MoveFileW(cert_temp, cert_path)) {
        ptls_set_error("identity certificate commit failed", 0);
        goto fail;
    }
    committed_cert = 1;
    if (!MoveFileW(key_temp, key_path)) {
        ptls_set_error("identity private-key commit failed; rolled back", 0);
        goto fail;
    }
    return 1;

fail:
    if (committed_cert) {
        DeleteFileW(cert_path);
    }
    DeleteFileW(cert_temp);
    DeleteFileW(key_temp);
    return 0;
}

static PTlsIdentity* ptls_identity_create(const WCHAR* cert_path,
                                          const WCHAR* key_path)
{
    PTlsIdentity* identity;
    mbedtls_x509write_cert writer;
    mbedtls_mpi serial;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    unsigned char serial_bytes[16];
    unsigned char* cert_pem;
    unsigned char* key_pem;
    char not_before[15];
    char not_after[15];
    int rc;

    identity = NULL;
    cert_pem = NULL;
    key_pem = NULL;
    mbedtls_x509write_crt_init(&writer);
    mbedtls_mpi_init(&serial);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                               (const unsigned char*)PTLS_PERS,
                               strlen(PTLS_PERS));
    if (rc != 0) {
        ptls_set_error("identity ctr_drbg_seed failed", rc);
        goto fail;
    }
    if (!ptls_format_validity(not_before, not_after)) {
        goto fail;
    }
    identity = (PTlsIdentity*)LocalAlloc(LPTR, sizeof(PTlsIdentity));
    if (identity == NULL) {
        ptls_set_error("identity allocation failed", 0);
        goto fail;
    }
    mbedtls_x509_crt_init(&identity->cert);
    mbedtls_pk_init(&identity->key);
    rc = mbedtls_pk_setup(&identity->key,
                          mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (rc != 0) {
        ptls_set_error("identity EC key setup failed", rc);
        goto fail;
    }
    rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                             mbedtls_pk_ec(identity->key),
                             mbedtls_ctr_drbg_random, &drbg);
    if (rc != 0) {
        ptls_set_error("identity ECDSA P-256 generation failed", rc);
        goto fail;
    }
    rc = mbedtls_ctr_drbg_random(&drbg, serial_bytes,
                                 sizeof(serial_bytes));
    if (rc != 0) {
        ptls_set_error("identity serial generation failed", rc);
        goto fail;
    }
    serial_bytes[0] &= 0x7F;
    serial_bytes[0] |= 0x01;
    rc = mbedtls_mpi_read_binary(&serial, serial_bytes,
                                 sizeof(serial_bytes));
    if (rc != 0) {
        ptls_set_error("identity serial import failed", rc);
        goto fail;
    }

    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &identity->key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &identity->key);
    rc = mbedtls_x509write_crt_set_subject_name(&writer,
                                                "CN=Positron Peer");
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_issuer_name(&writer,
                                                   "CN=Positron Peer");
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_serial(&writer, &serial);
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_validity(&writer,
                                                not_before, not_after);
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1);
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_key_usage(
                &writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_ns_cert_type(
                &writer, MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT |
                         MBEDTLS_X509_NS_CERT_TYPE_SSL_SERVER);
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_subject_key_identifier(&writer);
    }
    if (rc == 0) {
        rc = mbedtls_x509write_crt_set_authority_key_identifier(&writer);
    }
    if (rc != 0) {
        ptls_set_error("identity certificate setup failed", rc);
        goto fail;
    }

    cert_pem = (unsigned char*)LocalAlloc(LPTR, PTLS_CERT_PEM_CAPACITY);
    key_pem = (unsigned char*)LocalAlloc(LPTR, PTLS_KEY_PEM_CAPACITY);
    if (cert_pem == NULL || key_pem == NULL) {
        ptls_set_error("identity PEM allocation failed", 0);
        goto fail;
    }
    rc = mbedtls_x509write_crt_pem(&writer, cert_pem,
                                   PTLS_CERT_PEM_CAPACITY,
                                   mbedtls_ctr_drbg_random, &drbg);
    if (rc != 0) {
        ptls_set_error("identity certificate PEM write failed", rc);
        goto fail;
    }
    rc = mbedtls_pk_write_key_pem(&identity->key, key_pem,
                                  PTLS_KEY_PEM_CAPACITY);
    if (rc != 0) {
        ptls_set_error("identity private-key PEM write failed", rc);
        goto fail;
    }
    rc = mbedtls_x509_crt_parse(&identity->cert, cert_pem,
                                strlen((const char*)cert_pem) + 1);
    if (rc != 0 || mbedtls_pk_check_pair(&identity->cert.pk,
                                         &identity->key) != 0) {
        ptls_set_error("generated identity validation failed",
                       rc != 0 ? rc : -1);
        goto fail;
    }
    if (!ptls_persist_identity(cert_path, key_path, cert_pem, key_pem)) {
        goto fail;
    }

    LocalFree(key_pem);
    LocalFree(cert_pem);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&writer);
    return identity;

fail:
    if (identity != NULL) {
        mbedtls_pk_free(&identity->key);
        mbedtls_x509_crt_free(&identity->cert);
        LocalFree(identity);
    }
    if (key_pem != NULL) {
        LocalFree(key_pem);
    }
    if (cert_pem != NULL) {
        LocalFree(cert_pem);
    }
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&writer);
    return NULL;
}

PTLS_API HANDLE PTls_IdentityLoadOrCreate(const char* cert_path_utf8,
                                          const char* key_path_utf8)
{
    WCHAR cert_path[MAX_PATH];
    WCHAR key_path[MAX_PATH];
    int cert_exists;
    int key_exists;
    PTlsIdentity* identity;

    if (!g_initialized) {
        ptls_set_error("PTls_Init not called", 0);
        return NULL;
    }
    if (!ptls_utf8_path(cert_path_utf8, cert_path, MAX_PATH) ||
        !ptls_utf8_path(key_path_utf8, key_path, MAX_PATH)) {
        return NULL;
    }
    EnterCriticalSection(&g_identity_lock);
    cert_exists = ptls_file_exists(cert_path, "identity certificate");
    key_exists = ptls_file_exists(key_path, "identity private key");
    if (cert_exists < 0 || key_exists < 0) {
        identity = NULL;
    } else if (cert_exists != key_exists) {
        ptls_set_error(cert_exists ? "identity key missing" :
                                     "identity cert missing", 0);
        identity = NULL;
    } else if (cert_exists) {
        identity = ptls_identity_load(cert_path, key_path);
    } else {
        identity = ptls_identity_create(cert_path, key_path);
    }
    LeaveCriticalSection(&g_identity_lock);
    return (HANDLE)identity;
}

PTLS_API void PTls_IdentityClose(HANDLE hIdentity)
{
    PTlsIdentity* identity;

    if (hIdentity == NULL) {
        return;
    }
    identity = (PTlsIdentity*)hIdentity;
    mbedtls_pk_free(&identity->key);
    mbedtls_x509_crt_free(&identity->cert);
    LocalFree(identity);
}

static BOOL ptls_cert_fingerprint(const mbedtls_x509_crt* cert,
                                  char* out_hex, int out_capacity)
{
    static const char HEX[] = "0123456789ABCDEF";
    unsigned char digest[32];
    int i;
    int rc;

    if (cert == NULL || cert->raw.p == NULL || cert->raw.len == 0) {
        ptls_set_error("peer certificate unavailable", 0);
        return FALSE;
    }
    if (out_hex == NULL || out_capacity < PTLS_FINGERPRINT_HEX_CAPACITY) {
        ptls_set_error("fingerprint output capacity must be at least 65", 0);
        return FALSE;
    }
    rc = mbedtls_sha256_ret(cert->raw.p, cert->raw.len, digest, 0);
    if (rc != 0) {
        ptls_set_error("certificate SHA-256 failed", rc);
        return FALSE;
    }
    for (i = 0; i < 32; i++) {
        out_hex[i * 2] = HEX[(digest[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = HEX[digest[i] & 0x0F];
    }
    out_hex[64] = '\0';
    return TRUE;
}

PTLS_API BOOL PTls_IdentityFingerprint(HANDLE hIdentity,
                                       char* out_hex, int out_capacity)
{
    PTlsIdentity* identity;

    if (hIdentity == NULL) {
        ptls_set_error("identity handle is null", 0);
        return FALSE;
    }
    identity = (PTlsIdentity*)hIdentity;
    return ptls_cert_fingerprint(&identity->cert, out_hex, out_capacity);
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
    c->bio_msg[0] = '\0';
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

    mbedtls_ssl_set_bio(&c->ssl, c, ptls_bio_send, ptls_bio_recv, NULL);

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
            ptls_set_error_bio("ssl_handshake", rc, c->bio_msg);
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

static PTlsConn* ptls_conn_create(void)
{
    PTlsConn* conn;

    conn = (PTlsConn*)LocalAlloc(LPTR, sizeof(PTlsConn));
    if (conn == NULL) {
        ptls_set_error("connection allocation failed", 0);
        return NULL;
    }
    conn->sock = INVALID_SOCKET;
    conn->bio_msg[0] = '\0';
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->conf);
    mbedtls_entropy_init(&conn->entropy);
    mbedtls_ctr_drbg_init(&conn->drbg);
    return conn;
}

static void ptls_conn_destroy(PTlsConn* conn, int close_notify)
{
    int rc;

    if (conn == NULL) {
        return;
    }
    if (close_notify) {
        do {
            rc = mbedtls_ssl_close_notify(&conn->ssl);
        } while (rc == MBEDTLS_ERR_SSL_WANT_READ ||
                 rc == MBEDTLS_ERR_SSL_WANT_WRITE);
    }
    if (conn->sock != INVALID_SOCKET) {
        closesocket(conn->sock);
        conn->sock = INVALID_SOCKET;
    }
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_ctr_drbg_free(&conn->drbg);
    mbedtls_entropy_free(&conn->entropy);
    LocalFree(conn);
}

static int ptls_allow_untrusted_self_signed(void* context,
                                            mbedtls_x509_crt* cert,
                                            int depth,
                                            uint32_t* flags)
{
    (void)context;
    (void)cert;
    (void)depth;
    *flags &= ~((uint32_t)MBEDTLS_X509_BADCERT_NOT_TRUSTED);
    return 0;
}

static int ptls_handshake_timed(PTlsConn* conn, DWORD started,
                                int timeout_ms, const char* label)
{
    int remaining;
    int rc;
    int selected;
    fd_set read_set;
    fd_set write_set;
    struct timeval timeout;
    uint32_t flags;
    char info[400];
    int n;

    for (;;) {
        remaining = ptls_remaining_ms(started, timeout_ms);
        if (remaining <= 0) {
            ptls_set_error(label, 0);
            return 0;
        }
        rc = mbedtls_ssl_handshake(&conn->ssl);
        if (rc == 0) {
            return ptls_set_socket_blocking(conn->sock, 1);
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ ||
            rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            remaining = ptls_remaining_ms(started, timeout_ms);
            if (remaining <= 0) {
                ptls_set_error(label, 0);
                return 0;
            }
            FD_ZERO(&read_set);
            FD_ZERO(&write_set);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ) {
                FD_SET(conn->sock, &read_set);
            } else {
                FD_SET(conn->sock, &write_set);
            }
            timeout.tv_sec = remaining / 1000;
            timeout.tv_usec = (remaining % 1000) * 1000;
            selected = select(0, &read_set, &write_set, NULL, &timeout);
            if (selected == 0) {
                ptls_set_error(label, 0);
                return 0;
            }
            if (selected == SOCKET_ERROR) {
                ptls_set_error_wsa("TLS handshake select failed");
                return 0;
            }
            continue;
        }
        if (ptls_remaining_ms(started, timeout_ms) <= 0 ||
            strstr(conn->bio_msg, "WSA=10060") != NULL) {
            ptls_set_error(label, 0);
            return 0;
        }
        flags = mbedtls_ssl_get_verify_result(&conn->ssl);
        if (flags != 0 && flags != (uint32_t)-1) {
            info[0] = '\0';
            n = mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
            if (n > 0 && info[n - 1] == '\n') {
                info[n - 1] = '\0';
            }
            EnterCriticalSection(&g_err_lock);
            _snprintf(g_last_error, sizeof(g_last_error),
                      "%s certificate verify failed (flags=0x%08X): %s",
                      label, (unsigned)flags, info);
            g_last_error[sizeof(g_last_error) - 1] = '\0';
            LeaveCriticalSection(&g_err_lock);
        } else {
            ptls_set_error_bio("TLS handshake failed", rc, conn->bio_msg);
        }
        return 0;
    }
}

static int ptls_normalize_fingerprint(const char* input,
                                      char normalized[65], int* has_pin)
{
    int i;
    char value;

    *has_pin = 0;
    normalized[0] = '\0';
    if (input == NULL || input[0] == '\0') {
        return 1;
    }
    if (strlen(input) != 64) {
        ptls_set_error("fingerprint malformed: expected 64 hex digits", 0);
        return 0;
    }
    for (i = 0; i < 64; i++) {
        value = input[i];
        if (value >= 'a' && value <= 'f') {
            value = (char)(value - 'a' + 'A');
        }
        if (!((value >= '0' && value <= '9') ||
              (value >= 'A' && value <= 'F'))) {
            ptls_set_error("fingerprint malformed: expected 64 hex digits", 0);
            return 0;
        }
        normalized[i] = value;
    }
    normalized[64] = '\0';
    *has_pin = 1;
    return 1;
}

PTLS_API HANDLE PTls_ConnectPeer(const char* host_utf8,
                                 int port,
                                 HANDLE hIdentity,
                                 const char* expected_fingerprint,
                                 int timeout_ms)
{
    PTlsIdentity* identity;
    PTlsConn* conn;
    DWORD started;
    int rc;
    char normalized[65];
    char actual[65];
    int has_pin;

    if (!g_initialized) {
        ptls_set_error("PTls_Init not called", 0);
        return NULL;
    }
    if (host_utf8 == NULL || host_utf8[0] == '\0') {
        ptls_set_error("peer host is null/empty", 0);
        return NULL;
    }
    if (port <= 0 || port > 65535) {
        ptls_set_error("peer port is outside 1..65535", 0);
        return NULL;
    }
    if (hIdentity == NULL) {
        ptls_set_error("peer identity handle is null", 0);
        return NULL;
    }
    if (timeout_ms < PTLS_MIN_TIMEOUT_MS) {
        ptls_set_error("peer timeout must be positive", 0);
        return NULL;
    }
    if (!ptls_normalize_fingerprint(expected_fingerprint,
                                    normalized, &has_pin)) {
        return NULL;
    }
    identity = (PTlsIdentity*)hIdentity;
    conn = ptls_conn_create();
    if (conn == NULL) {
        return NULL;
    }
    started = GetTickCount();
    rc = mbedtls_ctr_drbg_seed(&conn->drbg, mbedtls_entropy_func,
                               &conn->entropy,
                               (const unsigned char*)PTLS_PERS,
                               strlen(PTLS_PERS));
    if (rc != 0) {
        ptls_set_error("peer ctr_drbg_seed failed", rc);
        goto fail;
    }
    rc = mbedtls_ssl_config_defaults(&conn->conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        ptls_set_error("peer ssl_config_defaults failed", rc);
        goto fail;
    }
    mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &conn->drbg);
    mbedtls_ssl_conf_ca_chain(&conn->conf, &identity->cert, NULL);
    mbedtls_ssl_conf_verify(&conn->conf,
                            ptls_allow_untrusted_self_signed, NULL);
    rc = mbedtls_ssl_conf_own_cert(&conn->conf, &identity->cert,
                                   &identity->key);
    if (rc != 0) {
        ptls_set_error("peer client identity setup failed", rc);
        goto fail;
    }
    rc = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
    if (rc != 0) {
        ptls_set_error("peer ssl_setup failed", rc);
        goto fail;
    }
    conn->sock = ptls_tcp_connect_timed(host_utf8, port,
                                        started, timeout_ms);
    if (conn->sock == INVALID_SOCKET) {
        goto fail;
    }
    mbedtls_ssl_set_bio(&conn->ssl, conn,
                        ptls_bio_send, ptls_bio_recv, NULL);
    if (!ptls_handshake_timed(conn, started, timeout_ms,
                              "TLS handshake timeout")) {
        goto fail;
    }
    if (!ptls_cert_fingerprint(mbedtls_ssl_get_peer_cert(&conn->ssl),
                               actual, sizeof(actual))) {
        goto fail;
    }
    if (has_pin && strcmp(actual, normalized) != 0) {
        EnterCriticalSection(&g_err_lock);
        _snprintf(g_last_error, sizeof(g_last_error),
                  "fingerprint mismatch: expected %s, got %s",
                  normalized, actual);
        g_last_error[sizeof(g_last_error) - 1] = '\0';
        LeaveCriticalSection(&g_err_lock);
        goto fail;
    }
    return (HANDLE)conn;

fail:
    ptls_conn_destroy(conn, 0);
    return NULL;
}

PTLS_API BOOL PTls_PeerFingerprint(HANDLE hConn,
                                   char* out_hex, int out_capacity)
{
    PTlsConn* conn;

    if (hConn == NULL) {
        ptls_set_error("connection handle is null", 0);
        return FALSE;
    }
    conn = (PTlsConn*)hConn;
    return ptls_cert_fingerprint(mbedtls_ssl_get_peer_cert(&conn->ssl),
                                 out_hex, out_capacity);
}

/* ---------------------------------------------------------------------- */
/* TLS peer server                                                        */
/* ---------------------------------------------------------------------- */

PTLS_API HANDLE PTls_ServerListen(HANDLE hIdentity,
                                  int port,
                                  unsigned int flags,
                                  int handshake_timeout_ms)
{
    PTlsListener* listener;
    SOCKET sock;
    struct sockaddr_in address;
    int reuse;

    if (!g_initialized) {
        ptls_set_error("PTls_Init not called", 0);
        return NULL;
    }
    if (hIdentity == NULL) {
        ptls_set_error("listener identity handle is null", 0);
        return NULL;
    }
    if (port <= 0 || port > 65535) {
        ptls_set_error("listener port is outside 1..65535", 0);
        return NULL;
    }
    if ((flags & ~PTLS_SERVER_REQUIRE_CLIENT_CERT) != 0) {
        ptls_set_error("listener flags contain unsupported bits", 0);
        return NULL;
    }
    if (handshake_timeout_ms < PTLS_MIN_TIMEOUT_MS) {
        ptls_set_error("listener handshake timeout must be positive", 0);
        return NULL;
    }
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        ptls_set_error_wsa("listener socket() failed");
        return NULL;
    }
    reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((u_short)port);
    if (bind(sock, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        ptls_set_error_wsa("listener bind failed");
        closesocket(sock);
        return NULL;
    }
    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        ptls_set_error_wsa("listener listen failed");
        closesocket(sock);
        return NULL;
    }
    listener = (PTlsListener*)LocalAlloc(LPTR, sizeof(PTlsListener));
    if (listener == NULL) {
        ptls_set_error("listener allocation failed", 0);
        closesocket(sock);
        return NULL;
    }
    InitializeCriticalSection(&listener->lock);
    listener->accept_idle = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (listener->accept_idle == NULL) {
        ptls_set_error("listener event creation failed", 0);
        DeleteCriticalSection(&listener->lock);
        closesocket(sock);
        LocalFree(listener);
        return NULL;
    }
    listener->listen_sock = sock;
    listener->pending_sock = INVALID_SOCKET;
    listener->identity = (PTlsIdentity*)hIdentity;
    listener->flags = flags;
    listener->handshake_timeout_ms = handshake_timeout_ms;
    listener->closing = FALSE;
    listener->accept_active = FALSE;
    return (HANDLE)listener;
}

static void ptls_listener_finish_accept(PTlsListener* listener,
                                        PTlsConn* conn)
{
    EnterCriticalSection(&listener->lock);
    if (conn != NULL && listener->pending_sock == conn->sock) {
        listener->pending_sock = INVALID_SOCKET;
    }
    listener->accept_active = FALSE;
    SetEvent(listener->accept_idle);
    LeaveCriticalSection(&listener->lock);
}

PTLS_API HANDLE PTls_ServerAccept(HANDLE hListener,
                                  char* remote_ip_utf8,
                                  int remote_ip_capacity,
                                  int* remote_port)
{
    PTlsListener* listener;
    SOCKET listen_sock;
    SOCKET accepted;
    struct sockaddr_in remote;
    int remote_size;
    PTlsConn* conn;
    DWORD started;
    int rc;
    unsigned char* address_bytes;

    if (hListener == NULL) {
        ptls_set_error("listener handle is null", 0);
        return NULL;
    }
    if (remote_ip_utf8 != NULL && remote_ip_capacity < 16) {
        ptls_set_error("remote IP output capacity must be at least 16", 0);
        return NULL;
    }
    listener = (PTlsListener*)hListener;
    EnterCriticalSection(&listener->lock);
    if (listener->closing || listener->listen_sock == INVALID_SOCKET) {
        LeaveCriticalSection(&listener->lock);
        ptls_set_error("listener closed", 0);
        return NULL;
    }
    if (listener->accept_active) {
        LeaveCriticalSection(&listener->lock);
        ptls_set_error("listener already has an active accept", 0);
        return NULL;
    }
    listener->accept_active = TRUE;
    ResetEvent(listener->accept_idle);
    listen_sock = listener->listen_sock;
    LeaveCriticalSection(&listener->lock);

    memset(&remote, 0, sizeof(remote));
    remote_size = sizeof(remote);
    accepted = accept(listen_sock, (struct sockaddr*)&remote, &remote_size);
    if (accepted == INVALID_SOCKET) {
        EnterCriticalSection(&listener->lock);
        listener->accept_active = FALSE;
        SetEvent(listener->accept_idle);
        if (listener->closing) {
            LeaveCriticalSection(&listener->lock);
            ptls_set_error("listener closed", 0);
        } else {
            LeaveCriticalSection(&listener->lock);
            ptls_set_error_wsa("listener accept failed");
        }
        return NULL;
    }
    EnterCriticalSection(&listener->lock);
    if (listener->closing) {
        listener->accept_active = FALSE;
        SetEvent(listener->accept_idle);
        LeaveCriticalSection(&listener->lock);
        closesocket(accepted);
        ptls_set_error("listener closed", 0);
        return NULL;
    }
    listener->pending_sock = accepted;
    LeaveCriticalSection(&listener->lock);

    conn = ptls_conn_create();
    if (conn == NULL) {
        closesocket(accepted);
        EnterCriticalSection(&listener->lock);
        listener->pending_sock = INVALID_SOCKET;
        LeaveCriticalSection(&listener->lock);
        ptls_listener_finish_accept(listener, NULL);
        return NULL;
    }
    conn->sock = accepted;
    started = GetTickCount();
    if (!ptls_set_socket_blocking(conn->sock, 0)) {
        goto fail;
    }
    rc = mbedtls_ctr_drbg_seed(&conn->drbg, mbedtls_entropy_func,
                               &conn->entropy,
                               (const unsigned char*)PTLS_PERS,
                               strlen(PTLS_PERS));
    if (rc != 0) {
        ptls_set_error("server ctr_drbg_seed failed", rc);
        goto fail;
    }
    rc = mbedtls_ssl_config_defaults(&conn->conf,
                                     MBEDTLS_SSL_IS_SERVER,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        ptls_set_error("server ssl_config_defaults failed", rc);
        goto fail;
    }
    mbedtls_ssl_conf_authmode(
            &conn->conf,
            (listener->flags & PTLS_SERVER_REQUIRE_CLIENT_CERT) != 0 ?
            MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_cert_req_ca_list(
            &conn->conf, MBEDTLS_SSL_CERT_REQ_CA_LIST_DISABLED);
    mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &conn->drbg);
    mbedtls_ssl_conf_ca_chain(&conn->conf, &listener->identity->cert, NULL);
    mbedtls_ssl_conf_verify(&conn->conf,
                            ptls_allow_untrusted_self_signed, NULL);
    rc = mbedtls_ssl_conf_own_cert(&conn->conf,
                                   &listener->identity->cert,
                                   &listener->identity->key);
    if (rc != 0) {
        ptls_set_error("server identity setup failed", rc);
        goto fail;
    }
    rc = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
    if (rc != 0) {
        ptls_set_error("server ssl_setup failed", rc);
        goto fail;
    }
    mbedtls_ssl_set_bio(&conn->ssl, conn,
                        ptls_bio_send, ptls_bio_recv, NULL);
    if (!ptls_handshake_timed(conn, started,
                              listener->handshake_timeout_ms,
                              "TLS handshake timeout")) {
        goto fail;
    }
    EnterCriticalSection(&listener->lock);
    if (listener->closing) {
        LeaveCriticalSection(&listener->lock);
        ptls_set_error("listener closed", 0);
        goto fail;
    }
    LeaveCriticalSection(&listener->lock);
    if ((listener->flags & PTLS_SERVER_REQUIRE_CLIENT_CERT) != 0 &&
        mbedtls_ssl_get_peer_cert(&conn->ssl) == NULL) {
        ptls_set_error("client certificate required", 0);
        goto fail;
    }
    if (remote_ip_utf8 != NULL) {
        address_bytes = (unsigned char*)&remote.sin_addr;
        _snprintf(remote_ip_utf8, (size_t)remote_ip_capacity,
                  "%u.%u.%u.%u",
                  (unsigned)address_bytes[0], (unsigned)address_bytes[1],
                  (unsigned)address_bytes[2], (unsigned)address_bytes[3]);
        remote_ip_utf8[remote_ip_capacity - 1] = '\0';
    }
    if (remote_port != NULL) {
        *remote_port = (int)ntohs(remote.sin_port);
    }
    ptls_listener_finish_accept(listener, conn);
    return (HANDLE)conn;

fail:
    ptls_listener_finish_accept(listener, conn);
    ptls_conn_destroy(conn, 0);
    return NULL;
}

PTLS_API void PTls_ServerClose(HANDLE hListener)
{
    PTlsListener* listener;
    SOCKET listen_sock;
    SOCKET pending_sock;

    if (hListener == NULL) {
        return;
    }
    listener = (PTlsListener*)hListener;
    EnterCriticalSection(&listener->lock);
    listener->closing = TRUE;
    listen_sock = listener->listen_sock;
    pending_sock = listener->pending_sock;
    listener->listen_sock = INVALID_SOCKET;
    listener->pending_sock = INVALID_SOCKET;
    LeaveCriticalSection(&listener->lock);

    if (listen_sock != INVALID_SOCKET) {
        shutdown(listen_sock, SD_BOTH);
        closesocket(listen_sock);
    }
    if (pending_sock != INVALID_SOCKET) {
        shutdown(pending_sock, SD_BOTH);
    }
    WaitForSingleObject(listener->accept_idle, INFINITE);
    CloseHandle(listener->accept_idle);
    DeleteCriticalSection(&listener->lock);
    LocalFree(listener);
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

    if (hConn == NULL) {
        return;
    }
    c = (PTlsConn*)hConn;
    ptls_conn_destroy(c, 1);
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
            InitializeCriticalSection(&g_state_lock);
            g_state_lock_inited = TRUE;
            InitializeCriticalSection(&g_identity_lock);
            g_identity_lock_inited = TRUE;
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
            if (g_identity_lock_inited) {
                DeleteCriticalSection(&g_identity_lock);
                g_identity_lock_inited = FALSE;
            }
            if (g_state_lock_inited) {
                DeleteCriticalSection(&g_state_lock);
                g_state_lock_inited = FALSE;
            }
            break;
        default:
            break;
    }
    return TRUE;
}
