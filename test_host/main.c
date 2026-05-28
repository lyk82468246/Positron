/*
 * test_host/main.c - Phase 1 verification harness.
 *
 * Builds as a WinCE GUI executable (subsystem windowsce,5.02) targeting
 * Windows Mobile 6 Professional. There is no stdout console, so all
 * output goes through MessageBoxW + OutputDebugStringA.
 *
 * Flow:
 *   1. PTls_Init()
 *   2. PTls_Connect("api.anthropic.com", 443)   <-- TLS 1.2 handshake
 *   3. PTls_Write() a minimal HTTP/1.1 GET
 *   4. PTls_Read() up to 4 KB of response
 *   5. Show first ~256 bytes in a MessageBox
 *   6. PTls_Close() + PTls_Cleanup()
 */

#include <windows.h>
#include <string.h>
#include "../positron_tls/positron_tls.h"

static const char* GET_REQUEST =
    "GET / HTTP/1.1\r\n"
    "Host: api.anthropic.com\r\n"
    "User-Agent: Positron/0.1 (WinCE)\r\n"
    "Connection: close\r\n"
    "\r\n";

/* Convert UTF-8/ASCII to wide for MessageBoxW. Returns wide chars written
 * (excluding terminator). Truncates safely if dst is too small.            */
static int ascii_to_wide(const char* src, int src_len, WCHAR* dst, int dst_cap)
{
    int n;

    n = MultiByteToWideChar(CP_UTF8, 0, src, src_len, dst, dst_cap - 1);
    if (n <= 0) {
        /* Fallback: try ANSI                                              */
        n = MultiByteToWideChar(CP_ACP, 0, src, src_len, dst, dst_cap - 1);
        if (n <= 0) {
            dst[0] = L'\0';
            return 0;
        }
    }
    dst[n] = L'\0';
    return n;
}

static void show_msg(const WCHAR* title, const char* body)
{
    WCHAR wbuf[1024];
    int   body_len;

    body_len = (int)strlen(body);
    if (body_len > 512) {
        body_len = 512;
    }
    ascii_to_wide(body, body_len, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    MessageBoxW(NULL, wbuf, title, MB_OK | MB_ICONINFORMATION);
}

static void show_error(const WCHAR* stage)
{
    WCHAR  msg[512];
    WCHAR  wbody[400];
    const char* err;

    err = PTls_LastError();
    ascii_to_wide(err, (int)strlen(err), wbody,
                  sizeof(wbody) / sizeof(wbody[0]));
    wsprintfW(msg, L"%s\n\n%s", stage, wbody);
    OutputDebugStringW(msg);
    MessageBoxW(NULL, msg, L"Positron TLS - error", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                   LPWSTR lpCmdLine, int nCmdShow)
{
    HANDLE h;
    char   recv_buf[4096];
    int    total;
    int    n;
    int    wrote;
    int    req_len;

    (void)hInstance;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    OutputDebugStringW(L"test_host: starting\r\n");

    if (!PTls_Init()) {
        show_error(L"PTls_Init failed");
        return 1;
    }

    h = PTls_Connect("api.anthropic.com", 443);
    if (h == NULL) {
        show_error(L"PTls_Connect failed");
        PTls_Cleanup();
        return 2;
    }
    OutputDebugStringW(L"test_host: TLS handshake OK\r\n");

    req_len = (int)strlen(GET_REQUEST);
    wrote   = PTls_Write(h, GET_REQUEST, req_len);
    if (wrote != req_len) {
        show_error(L"PTls_Write failed");
        PTls_Close(h);
        PTls_Cleanup();
        return 3;
    }

    /* Read until buffer fills, peer closes, or error.                     */
    total = 0;
    while (total < (int)sizeof(recv_buf) - 1) {
        n = PTls_Read(h, recv_buf + total,
                      (int)sizeof(recv_buf) - 1 - total);
        if (n <= 0) {
            break;
        }
        total += n;
    }
    recv_buf[total] = '\0';

    if (total <= 0) {
        show_error(L"PTls_Read returned no data");
    } else {
        /* Only display the first ~256 bytes per spec.                     */
        if (total > 256) {
            recv_buf[256] = '\0';
        }
        show_msg(L"Positron TLS - response", recv_buf);
    }

    PTls_Close(h);
    PTls_Cleanup();
    OutputDebugStringW(L"test_host: done\r\n");
    return 0;
}
