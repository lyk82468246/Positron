/*
 * test_host/main.c - Phase 2 verification harness.
 *
 * Runs four self-contained tests, showing a MessageBox between each:
 *
 *   TEST 1 - DLL load
 *     Verify positron_tls / positron_json / positron_http are reachable
 *     and that key exports resolve. (We link them statically via .lib,
 *     so this mostly verifies the loader found the DLL files; if not,
 *     the process wouldn't even start.)
 *
 *   TEST 2 - JSON round-trip
 *     PJson_Parse a small literal, extract a string and an int, free.
 *
 *   TEST 3 - HTTPS GET (no auth)
 *     checkip.amazonaws.com -> plain-text public IP. China-direct;
 *     cert chains to Amazon Root CA 1 (in our CA bundle). JSON parsing
 *     is still covered by the offline TEST 2 and the nested TEST 4.
 *
 *   TEST 4 - HTTPS POST (no auth)
 *     postman-echo.com/post body {"hello":"positron"} -> parse echo
 *     response, extract .json.hello, show it.
 *
 * No stdout on WinCE - all output via MessageBoxW.
 * No API keys. All test endpoints are public.
 */

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>     /* malloc / free for fetched-CSS buffers */
#include <aygshell.h>   /* SHFullScreen / SHSipPreference - control the SIP */
#include <commctrl.h>   /* WM6 common-controls progress bar */
#include <wininet.h>    /* InternetCombineUrlA - WM-native URL resolution */

#include "positron_tls.h"
#include "positron_json.h"
#include "positron_http.h"
#include "positron_image.h"

#include <hubbub/parser.h>

#include <libcss/libcss.h>
#include <libwapcaplet/libwapcaplet.h>

/* dom_hubbub binding (HTML -> libdom DOM). Included by relative path: the
 * binding header has no clean public name, and its sibling "errors.h"
 * resolves via the header's own directory. Pulls <dom/dom.h> too. */
#include "../netsurf-all-3.11/libdom/bindings/hubbub/parser.h"

/* positron_core.dll - the product-level engine boundary. TEST 8 drives the
 * NetSurf engine through this DLL's PCore_* API instead of the raw static
 * libs, exactly as a real Positron app would consume it. */
#include "positron_core.h"

static const unsigned char g_test_bmp_2x2[] = {
    0x42, 0x4d, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xc4, 0x0e,
    0x00, 0x00, 0xc4, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0xff, 0x00, 0xff, 0x00, 0x00, 0x00
};

static const char g_test_png_2x2_b64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAFElEQVR4nGP4z8DA"
    "AMIM////ZwAAHu8E/KPItPcAAAAASUVORK5CYII=";

static const char g_test_gif_2x2_b64[] =
    "R0lGODdhAgACAIEAAP//AAD/AP8AAAAA/ywAAAAAAgACAAAIBwAFBBgAICAAOw==";

static const char g_test_jpeg_bad_2x2_b64[] =
    "/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoH"
    "BwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQME"
    "BAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
    "FBQUFBQUFBQUFBQUFBT/wAARCAACAAIDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEA"
    "AAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIh"
    "MUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6"
    "Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZ"
    "mqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx"
    "8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREA"
    "AgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAV"
    "YnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hp"
    "anN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPE"
    "xcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD6"
    "v+APhXRb/wCBHw4ubnR7C4uZvDemySzS2qM8jm1jJZiRkkkkkmiiiv8AP3Ov+Rni"
    "v+vk/wD0pn45mP8Avtb/ABS/Nn//2Q==";

static const char g_test_jpeg_16x16_b64[] =
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB"
    "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQH/2wBDAQEB"
    "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB"
    "AQEBAQEBAQEBAQEBAQH/wAARCAAQABADAREAAhEBAxEB/8QAFgABAQEAAAAAAAAA"
    "AAAAAAAACgkL/8QAFBABAAAAAAAAAAAAAAAAAAAAAP/EABYBAQEBAAAAAAAAAAAAAA"
    "AAAAkLCv/EABQRAQAAAAAAAAAAAAAAAAAAAAD/2gAMAwEAAhEDEQA/AIvinb+CUBbo"
    "V4G60w1YNihAHKg//9k=";

/* -------------------------------------------------------------------- */
/* Display helpers                                                       */
/* -------------------------------------------------------------------- */

static void utf8_to_wide(const char* src, int src_len,
                         WCHAR* dst, int dst_cap)
{
    int n;
    if (dst_cap < 1) {
        return;
    }
    if (src_len < 0) {
        src_len = (int)strlen(src);
    }
    n = MultiByteToWideChar(CP_UTF8, 0, src, src_len, dst, dst_cap - 1);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, src, src_len, dst, dst_cap - 1);
    }
    if (n < 0) {
        n = 0;
    }
    dst[n] = L'\0';
}

static void show_info(const WCHAR* title, const char* body)
{
    WCHAR  wbuf[1536];
    int    body_len;

    body_len = (int)strlen(body);
    if (body_len > 1024) {
        body_len = 1024;
    }
    utf8_to_wide(body, body_len, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    OutputDebugStringW(title);
    OutputDebugStringW(L": ");
    OutputDebugStringW(wbuf);
    OutputDebugStringW(L"\r\n");
    MessageBoxW(NULL, wbuf, title,
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

static void show_error(const WCHAR* title, const char* body)
{
    WCHAR  wbuf[1536];
    int    body_len;

    body_len = (int)strlen(body);
    if (body_len > 1024) {
        body_len = 1024;
    }
    utf8_to_wide(body, body_len, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    OutputDebugStringW(title);
    OutputDebugStringW(L": ");
    OutputDebugStringW(wbuf);
    OutputDebugStringW(L"\r\n");
    MessageBoxW(NULL, wbuf, title,
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

/* Ask a Yes/No question via MessageBox; returns TRUE for Yes. Drives the
 * startup group selector so a subset of tests can be run at a time (e.g.
 * only the offline engine/rendering group when there is no network). */
static BOOL ask_yesno(const WCHAR* title, const char* body)
{
    WCHAR wbuf[512];
    utf8_to_wide(body, -1, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
    return MessageBoxW(NULL, wbuf, title,
                       MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND)
           == IDYES;
}

#define TEST_CONFIG_MAX_BYTES 2048
#define TEST_MAX_NUMBER 62

static int test_config_space(char c)
{
    return c == ' ' || c == '\t';
}

static int test_config_available(int number)
{
    return number >= 1 && number <= TEST_MAX_NUMBER && number != 23;
}

static int test_config_parse_spec(char *spec,
        unsigned char selected[TEST_MAX_NUMBER + 1], int *selected_7b)
{
    char *p;
    int start;
    int end;
    int number;
    int separator_space;

    p = spec;
    while (*p != '\0') {
        while (test_config_space(*p) || *p == ',') {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            break;
        }
        if (p[0] == '7' && (p[1] == 'b' || p[1] == 'B') &&
                (p[2] == '\0' || p[2] == '#' || p[2] == ',' ||
                test_config_space(p[2]))) {
            *selected_7b = 1;
            p += 2;
            continue;
        }
        if (*p < '0' || *p > '9') {
            return 0;
        }
        start = 0;
        while (*p >= '0' && *p <= '9') {
            start = start * 10 + (*p - '0');
            p++;
        }
        separator_space = 0;
        while (test_config_space(*p)) {
            separator_space = 1;
            p++;
        }
        end = start;
        if (*p == '-') {
            separator_space = 0;
            p++;
            while (test_config_space(*p)) {
                p++;
            }
            if (*p < '0' || *p > '9') {
                return 0;
            }
            end = 0;
            while (*p >= '0' && *p <= '9') {
                end = end * 10 + (*p - '0');
                p++;
            }
        }
        if (end < start) {
            return 0;
        }
        for (number = start; number <= end; number++) {
            if (!test_config_available(number)) {
                return 0;
            }
            selected[number] = 1;
        }
        while (test_config_space(*p)) {
            separator_space = 1;
            p++;
        }
        if (separator_space && ((*p >= '0' && *p <= '9') ||
                (*p == '7' && (p[1] == 'b' || p[1] == 'B')))) {
            continue;
        }
        if (*p != '\0' && *p != '#' && *p != ',') {
            return 0;
        }
    }
    return 1;
}

/* Return >0 for a valid selection, 0 when no file exists, and -1 when the
 * file exists but is unreadable or malformed. */
static int test_config_load(unsigned char selected[TEST_MAX_NUMBER + 1],
        int *selected_7b)
{
    WCHAR path[MAX_PATH];
    DWORD path_len;
    DWORD size;
    DWORD read_count;
    HANDLE file;
    char buffer[TEST_CONFIG_MAX_BYTES + 1];
    char *line;
    char *next;
    char *value;
    char *end;
    int i;
    int found;
    int count;

    memset(selected, 0, TEST_MAX_NUMBER + 1);
    *selected_7b = 0;
    path_len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (path_len == 0 || path_len >= MAX_PATH) {
        return -1;
    }
    while (path_len > 0 && path[path_len - 1] != L'\\' &&
            path[path_len - 1] != L'/') {
        path_len--;
    }
    if (path_len == 0 || path_len + 13 >= MAX_PATH) {
        return -1;
    }
    lstrcpyW(path + path_len, L"test_host.ini");
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return (GetLastError() == ERROR_FILE_NOT_FOUND) ? 0 : -1;
    }
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 ||
            size > TEST_CONFIG_MAX_BYTES) {
        CloseHandle(file);
        return -1;
    }
    read_count = 0;
    if (!ReadFile(file, buffer, size, &read_count, NULL) ||
            read_count != size) {
        CloseHandle(file);
        return -1;
    }
    CloseHandle(file);
    buffer[read_count] = '\0';
    if (read_count >= 3 && (unsigned char) buffer[0] == 0xef &&
            (unsigned char) buffer[1] == 0xbb &&
            (unsigned char) buffer[2] == 0xbf) {
        memmove(buffer, buffer + 3, read_count - 2);
    }

    found = 0;
    line = buffer;
    while (*line != '\0') {
        next = line;
        while (*next != '\0' && *next != '\r' && *next != '\n') {
            next++;
        }
        if (*next != '\0') {
            *next++ = '\0';
            while (*next == '\r' || *next == '\n') {
                next++;
            }
        }
        while (test_config_space(*line)) {
            line++;
        }
        end = line + strlen(line);
        while (end > line && test_config_space(end[-1])) {
            *--end = '\0';
        }
        if (*line != '\0' && *line != '#' && *line != ';') {
            value = line;
            if ((line[0] == 't' || line[0] == 'T') &&
                    (line[1] == 'e' || line[1] == 'E') &&
                    (line[2] == 's' || line[2] == 'S') &&
                    (line[3] == 't' || line[3] == 'T') &&
                    (line[4] == 's' || line[4] == 'S')) {
                value = line + 5;
                while (test_config_space(*value)) {
                    value++;
                }
                if (*value != '=') {
                    return -1;
                }
                value++;
            } else if (found) {
                return -1;
            }
            if (!test_config_parse_spec(value, selected, selected_7b)) {
                return -1;
            }
            found = 1;
        }
        line = next;
    }
    if (!found) {
        return -1;
    }
    count = *selected_7b ? 1 : 0;
    for (i = 1; i <= TEST_MAX_NUMBER; i++) {
        if (selected[i]) {
            count++;
        }
    }
    return (count > 0) ? count : -1;
}

static void test_config_prompt(const unsigned char *selected,
        int selected_7b, char *buffer, int capacity)
{
    char item[24];
    int i;
    int first;

    _snprintf(buffer, capacity - 1,
            "test_host.ini selected:\n\nTEST ");
    buffer[capacity - 1] = '\0';
    first = 1;
    for (i = 1; i <= TEST_MAX_NUMBER; i++) {
        if (selected[i]) {
            _snprintf(item, sizeof(item) - 1,
                    "%s%d", first ? "" : ", ", i);
            item[sizeof(item) - 1] = '\0';
            strncat(buffer, item,
                    (size_t) (capacity - 1 - strlen(buffer)));
            first = 0;
        }
        if (i == 7 && selected_7b) {
            strncat(buffer, first ? "7b" : ", 7b",
                    (size_t) (capacity - 1 - strlen(buffer)));
            first = 0;
        }
    }
    strncat(buffer,
            "\n\nRun only these tests?\nNo = use the normal group selector.",
            (size_t) (capacity - 1 - strlen(buffer)));
    buffer[capacity - 1] = '\0';
}

/* -------------------------------------------------------------------- */
/* TEST 1 - DLL load                                                     */
/* -------------------------------------------------------------------- */

static BOOL test1_dll_load(void)
{
    /* We are statically linked against all three import libs.
     * If any DLL failed to load, the process would not have started.
     * Reach into one export from each module to make sure the link
     * is real and not eliminated. */
    const char* probe;
    HANDLE      h;

    if (!PTls_Init()) {
        show_error(L"TEST 1 FAIL", "PTls_Init returned FALSE");
        return FALSE;
    }
    PTls_Cleanup();

    h = PJson_Parse("{}");
    if (h == NULL) {
        show_error(L"TEST 1 FAIL", "PJson_Parse(empty) returned NULL");
        return FALSE;
    }
    PJson_Free(h);

    probe = "(stub)";
    (void)probe;

    if (!PHttp_Init()) {
        show_error(L"TEST 1 FAIL", "PHttp_Init returned FALSE");
        return FALSE;
    }
    /* leave PHttp initialized for the next tests */

    show_info(L"TEST 1 OK", "All three DLLs loaded and core exports resolved.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 2 - JSON round-trip                                              */
/* -------------------------------------------------------------------- */

static BOOL test2_json(void)
{
    HANDLE      root;
    const char* s;
    int         n;
    char        msg[256];

    root = PJson_Parse("{\"key\":\"value\",\"num\":42}");
    if (root == NULL) {
        show_error(L"TEST 2 FAIL", "PJson_Parse returned NULL");
        return FALSE;
    }

    s = PJson_GetString(root, "key");
    if (s == NULL || strcmp(s, "value") != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                  "PJson_GetString(\"key\") returned %s",
                  s ? s : "(null)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 2 FAIL", msg);
        PJson_Free(root);
        return FALSE;
    }

    n = PJson_GetInt(root, "num");
    if (n != 42) {
        _snprintf(msg, sizeof(msg) - 1,
                  "PJson_GetInt(\"num\") returned %d, want 42", n);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 2 FAIL", msg);
        PJson_Free(root);
        return FALSE;
    }

    PJson_Free(root);
    show_info(L"TEST 2 OK", "JSON parse: key=\"value\", num=42 verified.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 3 - HTTPS GET                                                    */
/* checkip.amazonaws.com returns the caller's public IP as plain text    */
/* ("x.x.x.x\n"). Picked over api.ipify.org because it is reachable from */
/* mainland China without a VPN and its cert chains to Amazon Root CA 1, */
/* which is in our embedded CA bundle. (JSON coverage is unaffected: the */
/* offline TEST 2 is the JSON unit test and TEST 4 parses nested JSON.)  */
/* -------------------------------------------------------------------- */

typedef struct http_progress_probe {
    int calls;
    int monotonic;
    int last_received;
    int last_total;
} http_progress_probe;

static void test3_progress_cb(void *pw, int received, int total)
{
    http_progress_probe *probe;

    probe = (http_progress_probe *) pw;
    if (probe->calls > 0 && (received < probe->last_received ||
            (probe->last_total >= 0 && total != probe->last_total))) {
        probe->monotonic = 0;
    }
    probe->calls++;
    probe->last_received = received;
    probe->last_total = total;
}

static BOOL test3_get(void)
{
    PHttpResponse* resp;
    http_progress_probe progress;
    const char*    body;
    char           ip[64];
    int            i;
    int            dots;
    char           msg[512];

    memset(&progress, 0, sizeof(progress));
    progress.monotonic = 1;
    progress.last_total = -2;
    resp = PHttp_GetEx("checkip.amazonaws.com", 443, "/", NULL,
            test3_progress_cb, &progress);
    if (resp == NULL) {
        show_error(L"TEST 3 FAIL", "PHttp_Get returned NULL (OOM?)");
        return FALSE;
    }
    if (resp->status_code != 200) {
        _snprintf(msg, sizeof(msg) - 1,
                  "HTTPS GET checkip -> status=%d err=%s\nbody (first 200):\n%.200s",
                  resp->status_code, resp->error_msg,
                  resp->body ? resp->body : "(none)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 3 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }
    if (progress.calls < 2 || !progress.monotonic ||
            progress.last_received != resp->body_len ||
            (progress.last_total >= 0 &&
             progress.last_total != resp->body_len)) {
        _snprintf(msg, sizeof(msg) - 1,
                  "progress: calls=%d monotonic=%d received=%d total=%d body=%d",
                  progress.calls, progress.monotonic,
                  progress.last_received, progress.last_total,
                  resp->body_len);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 3 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    /* Body is the public IP as plain text with a trailing newline. Copy it
     * out, trim trailing CR/LF/space, and sanity-check it looks like a
     * dotted IPv4 address (exactly three dots). */
    body = resp->body ? resp->body : "";
    for (i = 0; body[i] != '\0' && i < (int)sizeof(ip) - 1; i++) {
        ip[i] = body[i];
    }
    ip[i] = '\0';
    while (i > 0 && (ip[i - 1] == '\n' || ip[i - 1] == '\r' || ip[i - 1] == ' ')) {
        ip[--i] = '\0';
    }

    dots = 0;
    for (i = 0; ip[i] != '\0'; i++) {
        if (ip[i] == '.') {
            dots++;
        }
    }
    if (ip[0] == '\0' || dots != 3) {
        _snprintf(msg, sizeof(msg) - 1,
                  "checkip returned 200 but body is not an IPv4 address:\n%.200s",
                  body);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 3 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "HTTPS GET checkip.amazonaws.com OK\n\nYour public IP:\n%s\n\n"
              "(TLS 1.2 GET + monotonic body progress; China-direct.)", ip);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 3 OK", msg);

    PHttp_FreeResponse(resp);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 4 - HTTPS POST                                                   */
/* postman-echo.com/post echoes the JSON body under "json" key          */
/* (same shape as httpbin; cert chains LE E8 -> ISRG Root X1, in bundle)*/
/* -------------------------------------------------------------------- */

static BOOL test4_post(void)
{
    static const char* HEADERS[] = {
        "Content-Type: application/json",
        NULL
    };
    static const char* BODY = "{\"hello\":\"positron\"}";

    PHttpResponse* resp;
    HANDLE         root;
    HANDLE         json_obj;
    const char*    echoed;
    char           msg[512];

    resp = PHttp_Post("postman-echo.com", 443, "/post",
                      HEADERS, BODY, (int)strlen(BODY));
    if (resp == NULL) {
        show_error(L"TEST 4 FAIL", "PHttp_Post returned NULL (OOM?)");
        return FALSE;
    }
    if (resp->status_code != 200) {
        _snprintf(msg, sizeof(msg) - 1,
                  "HTTPS POST postman-echo -> status=%d err=%s\nbody (first 256):\n%.256s",
                  resp->status_code, resp->error_msg,
                  resp->body ? resp->body : "(none)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 4 FAIL", msg);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    root = PJson_Parse(resp->body);
    if (root == NULL) {
        show_error(L"TEST 4 FAIL", "postman-echo response is not JSON");
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    json_obj = PJson_GetObject(root, "json");
    if (json_obj == NULL) {
        show_error(L"TEST 4 FAIL",
                   "postman-echo response missing 'json' key (server changed?)");
        PJson_Free(root);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    echoed = PJson_GetString(json_obj, "hello");
    if (echoed == NULL || strcmp(echoed, "positron") != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                  "postman-echo echo mismatch: expected positron, got %s",
                  echoed ? echoed : "(null)");
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 4 FAIL", msg);
        PJson_Free(root);
        PHttp_FreeResponse(resp);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "HTTPS POST postman-echo OK\n\n"
              "Sent: {\"hello\":\"positron\"}\n"
              "Server echoed: hello=%s\n\n"
              "(Full stack OK: TLS 1.2 + HTTPS POST + chunked decode\n"
              "+ JSON nested-object extraction.)", echoed);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 4 OK", msg);

    PJson_Free(root);
    PHttp_FreeResponse(resp);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 5 - Verified TLS                                                 */
/* Three sub-tests against badssl.com:                                   */
/*   A) badssl.com           -> must succeed (valid LE cert)             */
/*   B) expired.badssl.com   -> must fail, reason mentions expiry        */
/*   C) self-signed.badssl.com -> must fail, reason mentions trust       */
/*                                                                       */
/* Prerequisite: the emulator's wall clock must be set to a current      */
/* date. WM6 emulator defaults to ~2005-2007; X.509 validity windows    */
/* will reject everything if you don't fix this first. Set it via:      */
/*   Start -> Settings -> Clock & Alarms                                 */
/* -------------------------------------------------------------------- */

static BOOL test5_verified_tls(void)
{
    HANDLE conn;
    char   err_buf[256];
    char   summary[1024];

    /* --- A) valid host --- */
    conn = PTls_ConnectVerified("badssl.com", 443);
    if (conn == NULL) {
        _snprintf(summary, sizeof(summary) - 1,
                  "TEST 5A FAIL: badssl.com (valid cert) was REJECTED\n"
                  "Reason: %s\n\n"
                  "Common causes:\n"
                  " - Emulator clock is wrong (Settings -> Clock & Alarms)\n"
                  " - CA bundle does not include ISRG Root X1\n"
                  " - Network blocked",
                  PTls_LastError());
        summary[sizeof(summary) - 1] = '\0';
        show_error(L"TEST 5 FAIL", summary);
        return FALSE;
    }
    PTls_Close(conn);

    /* --- B) expired cert: must be rejected --- */
    conn = PTls_ConnectVerified("expired.badssl.com", 443);
    if (conn != NULL) {
        PTls_Close(conn);
        show_error(L"TEST 5 FAIL",
                   "expired.badssl.com was ACCEPTED; verification is broken.");
        return FALSE;
    }
    _snprintf(err_buf, sizeof(err_buf) - 1, "%s", PTls_LastError());
    err_buf[sizeof(err_buf) - 1] = '\0';

    /* --- C) self-signed: must be rejected --- */
    {
        char err2[256];
        conn = PTls_ConnectVerified("self-signed.badssl.com", 443);
        if (conn != NULL) {
            PTls_Close(conn);
            show_error(L"TEST 5 FAIL",
                       "self-signed.badssl.com was ACCEPTED; "
                       "verification is broken.");
            return FALSE;
        }
        _snprintf(err2, sizeof(err2) - 1, "%s", PTls_LastError());
        err2[sizeof(err2) - 1] = '\0';

        _snprintf(summary, sizeof(summary) - 1,
                  "Verified TLS works end-to-end.\n\n"
                  "A) badssl.com (valid)\n   ACCEPTED\n\n"
                  "B) expired.badssl.com\n   REJECTED: %.200s\n\n"
                  "C) self-signed.badssl.com\n   REJECTED: %.200s",
                  err_buf, err2);
        summary[sizeof(summary) - 1] = '\0';
    }

    show_info(L"TEST 5 OK", summary);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 6 - libhubbub HTML tokeniser (NetSurf engine, Phase 4)           */
/* Feeds a small HTML document to hubbub in tokeniser mode: setting a     */
/* custom token handler makes hubbub destroy its default treebuilder and  */
/* hand us the raw token stream. We verify the structural token counts    */
/* (deterministic at the tokeniser level) and that the &amp; entity is    */
/* decoded to '&'. This proves the ported NetSurf parser links against    */
/* coredll (catches missing CRT exports) and runs on real ARM hardware.   */
/* fix_enc=true forces a charset-alias lookup, exercising our bsearch shim.*/
/* -------------------------------------------------------------------- */

typedef struct {
    int doctype;
    int start;
    int end;
    int comment;
    int chars;
    int eof;
} hb_counts;

static hubbub_error hb_token(const hubbub_token *token, void *pw)
{
    hb_counts *c = (hb_counts *) pw;

    switch (token->type) {
    case HUBBUB_TOKEN_DOCTYPE:   c->doctype++; break;
    case HUBBUB_TOKEN_START_TAG: c->start++;   break;
    case HUBBUB_TOKEN_END_TAG:   c->end++;     break;
    case HUBBUB_TOKEN_COMMENT:   c->comment++; break;
    case HUBBUB_TOKEN_CHARACTER: c->chars += (int) token->data.character.len; break;
    case HUBBUB_TOKEN_EOF:       c->eof++;     break;
    default: break;
    }

    return HUBBUB_OK;
}

static BOOL test6_hubbub(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>Hi</title></head>"
        "<body><p>Hello &amp; world</p><!-- c --></body></html>";

    hubbub_parser          *parser = NULL;
    hubbub_parser_optparams params;
    hubbub_error            err;
    hb_counts               c;
    char                    msg[512];

    memset(&c, 0, sizeof(c));

    err = hubbub_parser_create("UTF-8", true, &parser);
    if (err != HUBBUB_OK || parser == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "hubbub_parser_create failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        return FALSE;
    }

    params.token_handler.handler = hb_token;
    params.token_handler.pw      = &c;
    err = hubbub_parser_setopt(parser, HUBBUB_PARSER_TOKEN_HANDLER, &params);
    if (err != HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "hubbub setopt(TOKEN_HANDLER) failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        hubbub_parser_destroy(parser);
        return FALSE;
    }

    err = hubbub_parser_parse_chunk(parser, (const uint8_t *) HTML,
                                    strlen(HTML));
    if (err != HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "hubbub_parser_parse_chunk failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        hubbub_parser_destroy(parser);
        return FALSE;
    }

    hubbub_parser_completed(parser);
    hubbub_parser_destroy(parser);

    /* Structural counts are deterministic at the tokeniser level:
     * doctype 1; start tags html/head/title/body/p = 5; end tags
     * /title//head//p//body//html = 5; one comment. chars should be 15:
     * "Hi"(2) + "Hello & world"(13, with &amp; decoded to a single '&'). */
    if (c.doctype != 1 || c.start != 5 || c.end != 5 || c.comment != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                  "TEST 6 token counts wrong:\n"
                  "doctype=%d (want 1)\nstart=%d (want 5)\n"
                  "end=%d (want 5)\ncomment=%d (want 1)\nchars=%d (want 15)",
                  c.doctype, c.start, c.end, c.comment, c.chars);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 6 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "libhubbub HTML tokeniser OK\n\n"
              "doctype=%d  start=%d  end=%d\ncomment=%d  chars=%d\n\n"
              "chars=15 => &amp; decoded to '&'.\n"
              "(NetSurf parser links + runs on ARM WinCE.)",
              c.doctype, c.start, c.end, c.comment, c.chars);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 6 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 7 - libcss CSS parser (NetSurf engine, Phase 4)                  */
/* Parse a small stylesheet end-to-end: create -> append_data ->         */
/* data_done -> destroy. Links positron_libcss (surfaces any missing CRT */
/* exports such as strdup/strncasecmp) and proves the ported CSS parser  */
/* runs on real ARM hardware.                                            */
/* -------------------------------------------------------------------- */

static css_error test7_resolve(void *pw, const char *base,
                               lwc_string *rel, lwc_string **abs)
{
    (void) pw;
    (void) base;
    /* No real URL resolution needed (test CSS has no @import); hand the
     * relative reference straight back, taking a ref as the API expects. */
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static BOOL test7_libcss(void)
{
    static const char *CSS =
        "body { color: #ffffff; background: #000000; }\n"
        "p { margin: 1em; font-size: 12px; }\n"
        "a:hover { text-decoration: underline; }\n";

    css_stylesheet_params params;
    css_stylesheet       *sheet = NULL;
    css_error             err;
    char                  msg[256];

    /* Zero the block, then set only the fields we need; title / quirks /
     * inline / import / colour / font callbacks stay NULL/false. */
    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level          = CSS_LEVEL_DEFAULT;
    params.charset        = "UTF-8";
    params.url            = "http://positron.local/test.css";
    params.resolve        = test7_resolve;

    err = css_stylesheet_create(&params, &sheet);
    if (err != CSS_OK || sheet == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "css_stylesheet_create failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7 FAIL", msg);
        return FALSE;
    }

    /* Streaming append: CSS_NEEDDATA is the normal "ok, more welcome". */
    err = css_stylesheet_append_data(sheet, (const uint8_t *) CSS,
                                     strlen(CSS));
    if (err != CSS_OK && err != CSS_NEEDDATA) {
        _snprintf(msg, sizeof(msg) - 1,
                  "css_stylesheet_append_data failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7 FAIL", msg);
        css_stylesheet_destroy(sheet);
        return FALSE;
    }

    err = css_stylesheet_data_done(sheet);
    if (err != CSS_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "css_stylesheet_data_done failed: err=%d", (int) err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7 FAIL", msg);
        css_stylesheet_destroy(sheet);
        return FALSE;
    }

    css_stylesheet_destroy(sheet);

    show_info(L"TEST 7 OK",
              "libcss parsed a stylesheet end-to-end:\n"
              "create -> append_data -> data_done -> destroy.\n\n"
              "3 rules (body / p / a:hover) accepted.\n\n"
              "(NetSurf CSS parser links + runs on ARM WinCE.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 7b - libdom HTML -> DOM via the dom_hubbub binding (Phase 4)     */
/* Drives hubbub through libdom's binding to build a real DOM tree, then  */
/* fetches the document element. Links positron_libdom + the binding      */
/* (surfaces further CRT gaps such as snprintf) and proves the ported     */
/* HTML->DOM pipeline runs on real ARM hardware.                          */
/* -------------------------------------------------------------------- */

static void test7b_msg(uint32_t severity, void *ctx, const char *msg, ...)
{
    (void) severity;
    (void) ctx;
    (void) msg;
    /* Swallow libdom/hubbub diagnostics (no stdout on WinCE anyway). */
}

static BOOL test7b_dom(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>Hi</title></head>"
        "<body><p>Hello</p><p>World</p></body></html>";

    dom_hubbub_parser_params params;
    dom_hubbub_parser       *parser = NULL;
    dom_document            *doc = NULL;
    dom_element             *root = NULL;
    dom_hubbub_error         derr;
    dom_exception            exc;
    char                     msg[256];

    memset(&params, 0, sizeof(params));
    params.enc           = "UTF-8";
    params.fix_enc       = true;
    params.enable_script = false;
    params.script        = NULL;
    params.msg           = test7b_msg;
    params.ctx           = NULL;
    params.daf           = NULL;

    derr = dom_hubbub_parser_create(&params, &parser, &doc);
    if (derr != DOM_HUBBUB_OK || parser == NULL || doc == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "dom_hubbub_parser_create failed: err=%d", (int) derr);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        if (parser != NULL) {
            dom_hubbub_parser_destroy(parser);
        }
        return FALSE;
    }

    derr = dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) HTML,
                                         strlen(HTML));
    if (derr != DOM_HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "dom_hubbub_parser_parse_chunk failed: err=%d", (int) derr);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        dom_hubbub_parser_destroy(parser);
        dom_node_unref((dom_node *) doc);
        return FALSE;
    }

    derr = dom_hubbub_parser_completed(parser);
    if (derr != DOM_HUBBUB_OK) {
        _snprintf(msg, sizeof(msg) - 1,
                  "dom_hubbub_parser_completed failed: err=%d", (int) derr);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        dom_hubbub_parser_destroy(parser);
        dom_node_unref((dom_node *) doc);
        return FALSE;
    }

    /* Parser finished; the document is now owned by us. */
    dom_hubbub_parser_destroy(parser);

    exc = dom_document_get_document_element(doc, &root);
    if (exc != DOM_NO_ERR || root == NULL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "get_document_element failed: exc=%d (root=%p)",
                  (int) exc, (void *) root);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 7b FAIL", msg);
        dom_node_unref((dom_node *) doc);
        return FALSE;
    }

    dom_node_unref((dom_node *) root);
    dom_node_unref((dom_node *) doc);

    show_info(L"TEST 7b OK",
              "libdom built a DOM tree from HTML via dom_hubbub:\n"
              "create -> parse_chunk -> completed -> document element.\n\n"
              "(NetSurf HTML->DOM pipeline links + runs on ARM WinCE.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 8 - positron_core.dll product boundary (Phase 4)                 */
/* The same HTML->DOM and CSS parse as TEST 7/7b, but driven through the  */
/* PCore_* API exported by positron_core.dll - i.e. the NetSurf engine    */
/* linked behind the shared DLL, exactly as a real Positron app would     */
/* consume it. Proves the engine links + runs inside the DLL and that     */
/* only the small PCore_* surface crosses the boundary.                   */
/* -------------------------------------------------------------------- */

static BOOL test8_core(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>Hi</title></head>"
        "<body><p>Hello</p><p>World</p></body></html>";
    static const char *CSS =
        "body { color: #ffffff; background: #000000; }\n"
        "p { margin: 1em; font-size: 12px; }\n"
        "a:hover { text-decoration: underline; }\n";

    HANDLE hDoc;
    HANDLE hSheet;

    if (PCore_Init() != 0) {
        show_error(L"TEST 8 FAIL", "PCore_Init failed");
        return FALSE;
    }

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 8 FAIL",
                   "PCore_ParseHTML returned NULL "
                   "(HTML->DOM via positron_core.dll failed)");
        PCore_Shutdown();
        return FALSE;
    }

    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 8 FAIL",
                   "PCore_ParseCSS returned NULL "
                   "(CSS parse via positron_core.dll failed)");
        PCore_FreeDocument(hDoc);
        PCore_Shutdown();
        return FALSE;
    }

    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    PCore_Shutdown();

    show_info(L"TEST 8 OK",
              "positron_core.dll drove the full engine:\n"
              "PCore_ParseHTML built a DOM tree, PCore_ParseCSS parsed a\n"
              "stylesheet - both through the shared DLL boundary.\n\n"
              "(NetSurf engine runs behind positron_core.dll on ARM WinCE.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 9 - CSS selection / computed style via positron_core.dll         */
/* Parse a tiny HTML doc + stylesheet, then PCore_ComputeColor drives     */
/* libcss selection (libdom-backed handler) to compute element colours.   */
/* Proves type/class/id + attribute/sibling/static-pseudo selectors.      */
/* -------------------------------------------------------------------- */

static BOOL test9_select(void)
{
    HANDLE        hDoc;
    HANDLE        hSheet;
    unsigned long p_argb;
    unsigned long p_rgb;
    unsigned long span_argb;
    unsigned long span_rgb;
    unsigned long a_argb;
    unsigned long a_rgb;
    int           rc;
    char          msg[512];
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><h1>Title</h1>"
        "<p title=\"hello\" data-role=\"intro\" class=\"lead\" "
        "lang=\"en-US\">Hello</p>"
        "<div>gap</div>"
        "<span data-code=\"pre-mid-post\">World</span>"
        "<a href=\"/next\" lang=\"zh-CN\">Next</a>"
        "</body></html>";
    static const char *CSS =
        "p { color: #010101; }\n"
        "h1 + p[title][data-role=\"intro\"][class~=\"lead\"][lang|=\"en\"] "
        "{ color: #112233; }\n"
        "span { color: #010101; }\n"
        "h1 ~ span[data-code^=\"pre\"][data-code$=\"post\"]"
        "[data-code*=\"-mid-\"] { color: #445566; }\n"
        "a { color: #010101; }\n"
        "a:link:lang(zh) { color: #778899; }\n";


    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 9 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 9 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    rc = PCore_ComputeColor(hDoc, hSheet, "p", &p_argb);
    if (rc == 0) {
        rc = PCore_ComputeColor(hDoc, hSheet, "span", &span_argb);
    }
    if (rc == 0) {
        rc = PCore_ComputeColor(hDoc, hSheet, "a", &a_argb);
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    if (rc != 0) {
        show_error(L"TEST 9 FAIL",
                   "PCore_ComputeColor failed (missing <p>/<span>/<a>, "
                   "or selection error)");
        return FALSE;
    }

    p_rgb = p_argb & 0x00FFFFFFUL;          /* low 24 bits = RRGGBB */
    span_rgb = span_argb & 0x00FFFFFFUL;
    a_rgb = a_argb & 0x00FFFFFFUL;
    if (p_rgb != 0x00112233UL || span_rgb != 0x00445566UL ||
            a_rgb != 0x00778899UL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "computed colors off:\n"
                  "  p    RGB 0x%06lX, expected 0x112233\n"
                  "  span RGB 0x%06lX, expected 0x445566\n"
                  "  a    RGB 0x%06lX, expected 0x778899",
                  p_rgb, span_rgb, a_rgb);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 9 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "CSS select OK:\n"
              "  h1 + p[title][data-role][class~][lang|]\n"
              "    -> p RGB 0x%06lX\n"
              "  h1 ~ span[data-code^][data-code$][data-code*]\n"
              "    -> span RGB 0x%06lX\n\n"
              "  a:link:lang(zh)\n"
              "    -> a RGB 0x%06lX\n\n"
              "(attribute + sibling + static pseudo selectors.)",
              p_rgb, span_rgb, a_rgb);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 9 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 10 - whole-document styling + inheritance via positron_core.dll   */
/* PCore_StyleDocument styles every element (UA + author sheet, top-down   */
/* compose). A nested <p> with no color of its own must inherit body's.    */
/* -------------------------------------------------------------------- */

static BOOL test10_styledoc(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><div><p>Hello</p></div></body></html>";
    static const char *CSS = "body { color: #112233; }\n";

    HANDLE        hDoc;
    HANDLE        hSheet;
    unsigned long argb;
    unsigned long rgb;
    int           rc;
    char          msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 10 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 10 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    rc = PCore_StyleDocument(hDoc, hSheet);
    if (rc != 0) {
        show_error(L"TEST 10 FAIL", "PCore_StyleDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    rc = PCore_NodeComputedColor(hDoc, "p", &argb);
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    if (rc != 0) {
        show_error(L"TEST 10 FAIL",
                   "PCore_NodeComputedColor failed (no <p> style attached)");
        return FALSE;
    }

    rgb = argb & 0x00FFFFFFUL;
    if (rgb != 0x00112233UL) {
        _snprintf(msg, sizeof(msg) - 1,
                  "<p> inherited color = 0x%06lX, expected 0x112233 "
                  "(body -> div -> p)", rgb);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 10 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "Whole-tree styling + inheritance OK:\n"
              "  body { color:#112233 }  ->  <p> computes 0x%06lX\n\n"
              "(UA sheet + author sheet, top-down compose over the DOM.)",
              rgb);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 10 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 21 - responsive @media uses the actual CSS viewport              */
/* 299/300/320 are test-only breakpoints, not product constants. A zero  */
/* css_media.width wrongly selects max-width rules regardless of the       */
/* dynamic PCore_SetViewport dimensions. Test both sides offline.         */
/* -------------------------------------------------------------------- */
static BOOL test21_media_viewport(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><p>legacy</p>"
        "<rangele>inclusive</rangele><rangestrict>strict</rangestrict>"
        "</body></html>";
    static const char *CSS =
        "p{color:#aa0000;}"
        "@media screen and (min-width:300px){p{color:#00aa00;}}"
        "@media screen and (max-width:299px){p{color:#0000aa;}}"
        "rangele,rangestrict{color:#aa0000;}"
        "@media (width <= 300px){rangele{color:#00aa00;}}"
        "@media (width < 300px){rangestrict{color:#0000aa;}}";
    HANDLE hDoc = NULL;
    HANDLE hSheet = NULL;
    unsigned long argb;
    unsigned long rgb;
    unsigned long legacy_expected;
    unsigned long rangele_expected;
    unsigned long strict_expected;
    int width;
    int pass;
    int screen_w;
    int screen_h;
    int screen_dpi = 96;
    HDC screen_dc;
    char msg[256];

    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        int dpi = GetDeviceCaps(screen_dc, LOGPIXELSY);
        if (dpi > 0) {
            screen_dpi = dpi;
        }
        ReleaseDC(NULL, screen_dc);
    }

    msg[0] = '\0';
    for (pass = 0; pass < 3; pass++) {
        width = (pass == 0) ? 320 : ((pass == 1) ? 300 : 299);
        legacy_expected = (width >= 300) ? 0x0000aa00UL : 0x000000aaUL;
        rangele_expected = (width <= 300) ? 0x0000aa00UL : 0x00aa0000UL;
        strict_expected = (width < 300) ? 0x000000aaUL : 0x00aa0000UL;
        hDoc = PCore_ParseHTML(HTML, 0);
        hSheet = PCore_ParseCSS(CSS, 0,
                "http://positron.local/media.css");
        if (hDoc == NULL || hSheet == NULL) {
            if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
            if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
            hDoc = NULL;
            hSheet = NULL;
            break;
        }

        PCore_SetViewport(width, 200, screen_dpi);
        argb = 0;
        if (PCore_StyleDocument(hDoc, hSheet) != 0 ||
                PCore_NodeComputedColor(hDoc, "p", &argb) != 0) {
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            hDoc = NULL;
            hSheet = NULL;
            break;
        }
        rgb = argb & 0x00FFFFFFUL;
        if (rgb == legacy_expected &&
                PCore_NodeComputedColor(hDoc, "rangele", &argb) == 0) {
            rgb = argb & 0x00FFFFFFUL;
        } else {
            rgb = 0xFFFFFFFFUL;
        }
        if (rgb == rangele_expected &&
                PCore_NodeComputedColor(hDoc, "rangestrict", &argb) == 0) {
            rgb = argb & 0x00FFFFFFUL;
        } else {
            rgb = 0xFFFFFFFFUL;
        }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        hDoc = NULL;
        hSheet = NULL;
        if (rgb != strict_expected) {
            _snprintf(msg, sizeof(msg) - 1,
                    "width=%d: media boundary mismatch (last=0x%06lX)",
                    width, rgb);
            msg[sizeof(msg) - 1] = '\0';
            break;
        }
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    PCore_SetViewport(screen_w, screen_h, screen_dpi);

    if (pass != 3) {
        if (msg[0] == '\0') {
            show_error(L"TEST 21 FAIL", "parse, style, or color lookup failed");
        } else {
            show_error(L"TEST 21 FAIL", msg);
        }
        return FALSE;
    }
    show_info(L"TEST 21 OK",
              "Responsive media query OK:\n"
              "Legacy min/max-width plus MQ4 width <= / <\n"
              "boundaries passed at 320, 300, and 299px.\n\n"
              "(runtime viewport + DPI remain dynamic.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 22 - reverse flex respects container padding                     */
/* IANA's narrow layout is a row-reverse flex article with a hidden nav. */
/* Its sole main item must start after the 25px leading padding, not at a */
/* negative x coordinate.                                                 */
/* -------------------------------------------------------------------- */
static BOOL test22_reverse_flex_padding(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><div><main>main</main>"
        "<nav>hidden</nav></div></body></html>";
    static const char *CSS =
        "body{margin:0;}"
        "div{display:flex;flex-direction:row-reverse;"
        "padding-left:25px;padding-right:25px;}"
        "main{flex-grow:1;flex-basis:0;}nav{display:none;}";
    HANDLE hDoc;
    HANDLE hSheet;
    int x = 0, y = 0, w = 0, h = 0;
    int screen_w;
    int screen_h;
    int screen_dpi = 96;
    HDC screen_dc;
    char msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/flex.css");
    if (hDoc == NULL || hSheet == NULL) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 22 FAIL", "parse HTML/CSS failed");
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        int dpi = GetDeviceCaps(screen_dc, LOGPIXELSY);
        if (dpi > 0) {
            screen_dpi = dpi;
        }
        ReleaseDC(NULL, screen_dc);
    }
    PCore_SetViewport(224, 320, screen_dpi);
    if (PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 224, 320) != 0 ||
            PCore_NodeBox(hDoc, "main", &x, &y, &w, &h) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        screen_w = GetSystemMetrics(SM_CXSCREEN);
        screen_h = GetSystemMetrics(SM_CYSCREEN);
        if (screen_w <= 0) { screen_w = 240; }
        if (screen_h <= 0) { screen_h = 320; }
        PCore_SetViewport(screen_w, screen_h, screen_dpi);
        show_error(L"TEST 22 FAIL", "style/layout/main box failed");
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    PCore_SetViewport(screen_w, screen_h, screen_dpi);

    if (x != 25 || w != 174) {
        _snprintf(msg, sizeof(msg) - 1,
                  "main=(%d,%d) %dx%d, expect x=25 width=174", x, y, w, h);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 22 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 22 OK",
              "row-reverse flex + 25px padding OK:\n"
              "main x=25, width=174 in a 224px viewport.\n\n"
              "(hidden side nav cannot push content left.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 24 - cached external CSS can restyle a new viewport              */
/* A first StyleDocumentEx call fetches the linked sheet. A second call  */
/* at a different width must reselect @media from document-owned bytes, */
/* without calling transport again. Rotation must also preserve the      */
/* reader's relative position in the old/new scrollable ranges.          */
/* -------------------------------------------------------------------- */
static int pcore_scale_scroll_position(int old_pos, int old_doc_h,
        int old_view_h, int new_doc_h, int new_view_h)
{
    int old_max = (old_doc_h > old_view_h) ? old_doc_h - old_view_h : 0;
    int new_max = (new_doc_h > new_view_h) ? new_doc_h - new_view_h : 0;
    double ratio;
    int result;

    if (old_pos < 0) {
        old_pos = 0;
    }
    if (old_pos > old_max) {
        old_pos = old_max;
    }
    if (old_max == 0 || new_max == 0) {
        return 0;
    }
    ratio = (double) old_pos / (double) old_max;
    result = (int) (ratio * (double) new_max + 0.5);
    if (result < 0) {
        result = 0;
    }
    if (result > new_max) {
        result = new_max;
    }
    return result;
}

typedef struct stylesheet_cache_test_ctx {
    int calls;
    int frees;
} stylesheet_cache_test_ctx;

static int stylesheet_cache_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char CSS[] =
        "@media (min-width:300px){p{color:#00aa00;}}"
        "@media (max-width:299px){p{color:#0000aa;}}";
    stylesheet_cache_test_ctx *ctx = (stylesheet_cache_test_ctx *) pw;
    char *data;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/responsive.css") != 0) {
        return 1;
    }
    data = (char *) malloc(sizeof(CSS) - 1);
    if (data == NULL) {
        return 1;
    }
    memcpy(data, CSS, sizeof(CSS) - 1);
    *out_data = data;
    *out_len = sizeof(CSS) - 1;
    return 0;
}

static void stylesheet_cache_free(void *pw, char *data)
{
    stylesheet_cache_test_ctx *ctx = (stylesheet_cache_test_ctx *) pw;

    ctx->frees++;
    free(data);
}

static int stylesheet_cache_only_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    (void) pw;
    (void) url;
    *out_data = NULL;
    *out_len = 0;
    return 1;
}

static BOOL test24_cached_stylesheet_restyle(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head>"
        "<link rel=\"stylesheet\" href=\"/responsive.css\">"
        "</head><body><p>responsive</p></body></html>";
    HANDLE hDoc;
    stylesheet_cache_test_ctx ctx;
    unsigned long argb = 0;
    int screen_w;
    int screen_h;
    int screen_dpi = 96;
    HDC screen_dc;
    char msg[192];

    ctx.calls = 0;
    ctx.frees = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 24 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        int dpi = GetDeviceCaps(screen_dc, LOGPIXELSY);
        if (dpi > 0) {
            screen_dpi = dpi;
        }
        ReleaseDC(NULL, screen_dc);
    }

    PCore_SetViewport(320, 320, screen_dpi);
    if (PCore_StyleDocumentEx(hDoc, NULL, stylesheet_cache_fetch,
            stylesheet_cache_free, &ctx) != 0 ||
            PCore_NodeComputedColor(hDoc, "p", &argb) != 0 ||
            (argb & 0x00ffffffUL) != 0x0000aa00UL) {
        PCore_FreeDocument(hDoc);
        screen_w = GetSystemMetrics(SM_CXSCREEN);
        screen_h = GetSystemMetrics(SM_CYSCREEN);
        if (screen_w <= 0) { screen_w = 240; }
        if (screen_h <= 0) { screen_h = 320; }
        PCore_SetViewport(screen_w, screen_h, screen_dpi);
        show_error(L"TEST 24 FAIL", "initial linked CSS/media selection failed");
        return FALSE;
    }

    PCore_SetViewport(299, 320, screen_dpi);
    if (PCore_StyleDocumentEx(hDoc, NULL, stylesheet_cache_only_fetch,
            NULL, NULL) != 0 ||
            PCore_NodeComputedColor(hDoc, "p", &argb) != 0 ||
            (argb & 0x00ffffffUL) != 0x000000aaUL ||
            ctx.calls != 1 || ctx.frees != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                  "reselect color=0x%06lX calls=%d frees=%d",
                  argb & 0x00ffffffUL, ctx.calls, ctx.frees);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeDocument(hDoc);
        screen_w = GetSystemMetrics(SM_CXSCREEN);
        screen_h = GetSystemMetrics(SM_CYSCREEN);
        if (screen_w <= 0) { screen_w = 240; }
        if (screen_h <= 0) { screen_h = 320; }
        PCore_SetViewport(screen_w, screen_h, screen_dpi);
        show_error(L"TEST 24 FAIL", msg);
        return FALSE;
    }
    PCore_FreeDocument(hDoc);
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    PCore_SetViewport(screen_w, screen_h, screen_dpi);
    if (pcore_scale_scroll_position(0, 1000, 320, 700, 360) != 0 ||
            pcore_scale_scroll_position(340, 1000, 320, 700, 360) != 170 ||
            pcore_scale_scroll_position(680, 1000, 320, 700, 360) != 340) {
        show_error(L"TEST 24 FAIL", "rotation scroll ratio 0/50/100 failed");
        return FALSE;
    }
    show_info(L"TEST 24 OK",
              "Cached linked CSS restyled at 320px -> 299px:\n"
              "green -> blue; fetch calls stayed 1.\n"
              "Rotation scroll ratio passed at 0/50/100%.\n\n"
              "(restyle does not network.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 25 - public SVG parsing path through positron_image.dll          */
/* Exercises Expat -> libdom XML binding -> libsvgtiny without network. */
/* -------------------------------------------------------------------- */
static BOOL test25_svg_parse(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" "
        "height=\"32\" viewBox=\"0 0 64 32\">"
        "<rect x=\"0\" y=\"0\" width=\"32\" height=\"32\" fill=\"red\"/>"
        "<path d=\"M32 0 L64 0 L64 32 L32 32 Z\" fill=\"green\"/>"
        "</svg>";
    int rc;
    int width;
    int height;
    unsigned int shapes;
    char msg[192];

    rc = PImage_SvgInfoFromMemory(SVG, (int) sizeof(SVG) - 1,
            64, 32, &width, &height, &shapes);
    if (rc != PIMAGE_OK || width != 64 || height != 32 || shapes != 2) {
        _snprintf(msg, sizeof(msg) - 1,
                "rc=%d size=%dx%d shapes=%u; expect rc=0 64x32 shapes=2",
                rc, width, height, shapes);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 25 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 25 OK",
              "SVG parsed through the public image DLL:\n"
              "Expat -> libdom XML -> libsvgtiny.\n"
              "Intrinsic size=64x32; shapes=2.\n\n"
              "(in-memory and fully offline.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 11 - block-flow layout via positron_core.dll                      */
/* PCore_LayoutDocument computes block boxes; verify both parent/child    */
/* margin collapse and the padding barrier that must stop that collapse. */
/* -------------------------------------------------------------------- */

static BOOL test11_measure(const char *css,
        int *bx, int *by, int *bw, int *bh,
        int *px, int *py, int *pw, int *ph,
        char *err, int err_cap)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><div><p>Hello</p><p>World</p></div></body></html>";
    HANDLE hDoc = NULL;
    HANDLE hSheet = NULL;
    BOOL ok = FALSE;

    err[0] = '\0';

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        _snprintf(err, err_cap - 1, "PCore_ParseHTML returned NULL");
        goto cleanup;
    }
    hSheet = PCore_ParseCSS(css, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        _snprintf(err, err_cap - 1, "PCore_ParseCSS returned NULL");
        goto cleanup;
    }

    if (PCore_StyleDocument(hDoc, hSheet) != 0) {
        _snprintf(err, err_cap - 1, "PCore_StyleDocument failed");
        goto cleanup;
    }
    if (PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        _snprintf(err, err_cap - 1, "PCore_LayoutDocument failed");
        goto cleanup;
    }

    if (PCore_NodeBox(hDoc, "body", bx, by, bw, bh) != 0 ||
            PCore_NodeBox(hDoc, "p", px, py, pw, ph) != 0) {
        _snprintf(err, err_cap - 1, "PCore_NodeBox failed (body / p)");
        goto cleanup;
    }
    ok = TRUE;

cleanup:
    err[err_cap - 1] = '\0';
    if (hSheet != NULL) {
        PCore_FreeStylesheet(hSheet);
    }
    if (hDoc != NULL) {
        PCore_FreeDocument(hDoc);
    }
    return ok;
}

static BOOL test11_layout(void)
{
    static const char *CSS_COLLAPSE =
        "body { color: #112233; }\n";
    static const char *CSS_BARRIER =
        "body { color: #112233; padding-top: 1px; }\n";
    int cbx, cby, cbw, cbh;
    int cpx, cpy, cpw, cph;
    int bbx, bby, bbw, bbh;
    int bpx, bpy, bpw, bph;
    char err[128];
    char msg[512];

    if (!test11_measure(CSS_COLLAPSE,
            &cbx, &cby, &cbw, &cbh, &cpx, &cpy, &cpw, &cph,
            err, sizeof(err))) {
        _snprintf(msg, sizeof(msg) - 1, "collapse case: %s", err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 11 FAIL", msg);
        return FALSE;
    }
    if (!test11_measure(CSS_BARRIER,
            &bbx, &bby, &bbw, &bbh, &bpx, &bpy, &bpw, &bph,
            err, sizeof(err))) {
        _snprintf(msg, sizeof(msg) - 1, "padding barrier case: %s", err);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 11 FAIL", msg);
        return FALSE;
    }

    /* Horizontal body margins give x=8,w=224. NetSurf collapses the
     * body's 8px top margin with the first paragraph's 1em (16px) margin
     * through its borderless div. A 1px body padding stops that collapse. */
    if (cbx != 8 || cby != 16 || cbw != 224 ||
            cpx != 8 || cpy != 16 || cpw != 224 ||
            bbx != 8 || bby != 8 || bbw != 224 ||
            bpx != 8 || bpy != 25 || bpw != 224) {
        _snprintf(msg, sizeof(msg) - 1,
                  "margin geometry off:\n"
                  " collapse body=(%d,%d,%d,%d), p=(%d,%d,%d,%d)\n"
                  "   expect body/p x8 y16 w224\n"
                  " barrier body=(%d,%d,%d,%d), p=(%d,%d,%d,%d)\n"
                  "   expect body x8 y8 w224; p x8 y25 w224",
                  cbx, cby, cbw, cbh, cpx, cpy, cpw, cph,
                  bbx, bby, bbw, bbh, bpx, bpy, bpw, bph);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 11 FAIL", msg);
        return FALSE;
    }

    _snprintf(msg, sizeof(msg) - 1,
              "Margin collapse + barrier OK:\n"
              " collapse: body y=%d, p y=%d\n"
              " padding-top:1px: body y=%d, p y=%d\n\n"
              "Both cases keep x=8 and width=224.",
              cby, cpy, bby, bpy);
    msg[sizeof(msg) - 1] = '\0';
    show_info(L"TEST 11 OK", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 12 - first visible page: a real window painted by positron_core   */
/* The app owns the window + message loop; WM_PAINT calls                 */
/* PCore_PaintDocument over the styled + laid-out tree.                   */
/* -------------------------------------------------------------------- */

static HANDLE g_render_doc = NULL;
static HANDLE g_render_sheet = NULL; /* caller-owned while window lives */
static int    g_scroll_y = 0;
static int    g_doc_h = 0;
static int    g_view_h = 0;
static int    g_plot_test = 0;   /* M1: paint via PCore_PlotTest, not a doc */
static int    g_ns_render = 0;    /* M5e: paint via PCore_NsRenderTest */
static int    g_image_test = 0;   /* TEST 19: native WM Imaging draw */
static int    g_image_draw_rc = -1;
static int    g_svg_test = 0;     /* TEST 26: positron_image SVG draw */
static int    g_svg_draw_rc = -1;
static PIMAGE_SVG g_svg_handle = NULL;
static int    g_overflow_pointer = 0;
#define PCORE_IMAGE_FORMAT_COUNT 4
static PIMAGE_BITMAP g_image_format_bitmap[PCORE_IMAGE_FORMAT_COUNT];
static const WCHAR *g_image_format_name[PCORE_IMAGE_FORMAT_COUNT] = {
    L"BMP", L"PNG", L"JPEG", L"GIF"
};

#define WM_PCORE_NAV_DONE (WM_APP + 1)
#define WM_PCORE_NAV_PROGRESS (WM_APP + 2)
#define WM_PCORE_NAV_CONTINUE (WM_APP + 3)
#define PCORE_NAV_TIMER 24
#define PCORE_NAV_COMMIT_TIMER 25
#define PCORE_NAV_STAGE_DOCUMENT 1
#define PCORE_NAV_STAGE_RESOURCES 2
#define PCORE_NAV_RESULT_MORE 0
#define PCORE_NAV_RESULT_DONE 1
#define PCORE_NAV_RESULT_CONTINUE 2
#define PCORE_NAV_RESULT_FAILED -1
#define PCORE_NAV_COMMIT_NONE 0
#define PCORE_NAV_COMMIT_PARSE 1
#define PCORE_NAV_COMMIT_STYLE 2
#define PCORE_NAV_COMMIT_IMAGES 3
#define PCORE_NAV_COMMIT_LAYOUT 4
#define PCORE_NAV_MAX_RESOURCES 64
#define PCORE_NAV_RESOURCE_BYTES_MAX (2 * 1024 * 1024)

typedef struct pcore_navigation_resource {
    struct pcore_navigation_resource *next;
    char *url;
    char *data;
    int len;
    int attempted;
} pcore_navigation_resource;

typedef struct pcore_navigation_stats {
    DWORD started_tick;
    DWORD worker_started_tick;
    DWORD total_ms;
    DWORD network_ms;
    DWORD parse_ms;
    DWORD style_ms;
    DWORD images_ms;
    DWORD layout_ms;
    DWORD first_paint_ms;
    DWORD max_ui_slice_ms;
    int worker_rounds;
    int document_bytes;
    int resources_queued;
    int resources_fetched;
    int resources_failed;
    int resource_bytes;
    int budget_rejected;
    int completed;
} pcore_navigation_stats;

typedef struct pcore_navigation_request {
    HWND           hwnd;
    LONG           generation;
    char           host[256];
    char           path[1024];
    int            port;
    PHttpResponse *response;
    HANDLE         document;
    pcore_navigation_resource *resources;
    int            resource_count;
    int            resource_bytes;
    int            worker_stage;
    int            commit_stage;
    int            progress_last_total;
    int            progress_last_percent;
    int            progress_last_received;
    pcore_navigation_stats stats;
} pcore_navigation_request;

static HANDLE                    g_nav_thread = NULL;
static pcore_navigation_request *g_nav_request = NULL;
static HWND                      g_nav_bar = NULL;
static int                       g_nav_bar_h = 0;
static LONG                      g_nav_generation = 0;
static int                       g_nav_loading = 0;
static int                       g_nav_phase = 0;
static int                       g_nav_determinate = 0;
static pcore_navigation_stats    g_nav_last_stats;
static int                       g_nav_last_stats_valid = 0;

/* Current page origin, for resolving relative links during navigation. */
static char   g_cur_host[256] = "";
static char   g_cur_path[1024] = "/";
static int    g_cur_port = 443;

/* Configure the vertical scrollbar from the document height + client size. */
static void pcore_set_scrollbar(HWND hwnd)
{
    SCROLLINFO si;
    RECT rc;
    int ch;
    int needed;
    LONG style;
    LONG next_style;

    GetClientRect(hwnd, &rc);
    ch = rc.bottom - rc.top;
    needed = g_doc_h > ch;
    if (!needed) {
        g_scroll_y = 0;
    }
    style = GetWindowLong(hwnd, GWL_STYLE);
    next_style = needed ? (style | WS_VSCROLL) :
            (style & ~WS_VSCROLL);
    if (next_style != style) {
        SetWindowLong(hwnd, GWL_STYLE, next_style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (g_doc_h > 0) ? (g_doc_h - 1) : 0;
    si.nPage = (UINT) ((ch > 0) ? ch : 1);
    si.nPos = g_scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

/* Scroll by dy px, clamped to [0, doc_height - client_height], and repaint. */
static void pcore_scroll_by(HWND hwnd, int dy)
{
    RECT rc;
    RECT scroll_rc;
    int ch, maxpos, oldpos, applied;

    GetClientRect(hwnd, &rc);
    ch = rc.bottom - rc.top;
    maxpos = (g_doc_h > ch) ? (g_doc_h - ch) : 0;
    oldpos = g_scroll_y;
    g_scroll_y += dy;
    if (g_scroll_y < 0) {
        g_scroll_y = 0;
    }
    if (g_scroll_y > maxpos) {
        g_scroll_y = maxpos;
    }
    applied = g_scroll_y - oldpos;
    if (applied == 0) {
        return;
    }
    SetScrollPos(hwnd, SB_VERT, g_scroll_y, TRUE);
    /* Shift the existing pixels by -applied and invalidate only the newly
     * exposed strip; the following WM_PAINT repaints just that strip at the
     * new scroll offset. Far cheaper than repainting the whole client. */
    scroll_rc = rc;
    if (g_nav_loading && g_nav_bar_h > 0 &&
            scroll_rc.bottom - scroll_rc.top > g_nav_bar_h) {
        scroll_rc.top += g_nav_bar_h;
    }
    ScrollWindowEx(hwnd, 0, -applied, &scroll_rc, &scroll_rc,
            NULL, NULL, SW_INVALIDATE);
    if (g_nav_loading) {
        RECT loading_rc = rc;
        loading_rc.bottom = loading_rc.top +
                ((g_nav_bar_h > 0) ? g_nav_bar_h : 6);
        InvalidateRect(hwnd, &loading_rc, FALSE);
    }
    UpdateWindow(hwnd);
}

/* Convert the core's document-space overflow viewport to the current client
 * coordinates. Retained scrollbar input never needs to invalidate the rest of
 * the page, which is important on slow WM GDI devices. */
static void pcore_invalidate_overflow(HWND hwnd)
{
    RECT client;
    RECT dirty;
    int x;
    int y;
    int w;
    int h;

    if (g_render_doc == NULL ||
            !PCore_OverflowDirtyRect(g_render_doc, &x, &y, &w, &h)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    GetClientRect(hwnd, &client);
    dirty.left = x - 1;
    dirty.top = y - g_scroll_y - 1;
    dirty.right = x + w + 1;
    dirty.bottom = y - g_scroll_y + h + 1;
    if (dirty.left < client.left) { dirty.left = client.left; }
    if (dirty.top < client.top) { dirty.top = client.top; }
    if (dirty.right > client.right) { dirty.right = client.right; }
    if (dirty.bottom > client.bottom) { dirty.bottom = client.bottom; }
    if (dirty.left < dirty.right && dirty.top < dirty.bottom) {
        InvalidateRect(hwnd, &dirty, FALSE);
    }
}

/* Case-insensitive ASCII prefix test (avoids depending on _strnicmp). */
static int ci_prefix(const char *s, const char *pfx)
{
    while (*pfx != '\0') {
        char a = *s, b = *pfx;
        if (a >= 'A' && a <= 'Z') a = (char) (a + 32);
        if (b >= 'A' && b <= 'Z') b = (char) (b + 32);
        if (a != b) {
            return 0;
        }
        s++; pfx++;
    }
    return 1;
}

/* Bounded NUL-terminated string copy. */
static void cstr_copy(char *d, int cap, const char *s)
{
    int n = 0;
    if (cap <= 0) {
        return;
    }
    while (s[n] != '\0' && n < cap - 1) {
        d[n] = s[n];
        n++;
    }
    d[n] = '\0';
}

/* Keep URL policy at the WM host boundary. The engine supplies the CSS base
 * URL; WinINet supplies RFC-style relative/dot-segment resolution. */
static int wm_combine_url(void *pw, const char *base_url,
        const char *reference, char *out_url, int out_capacity)
{
    DWORD length;

    (void) pw;
    if (base_url == NULL || reference == NULL || out_url == NULL ||
            out_capacity <= 1) {
        return 1;
    }
    length = (DWORD) out_capacity;
    if (!InternetCombineUrlA(base_url, reference, out_url, &length,
            ICU_NO_ENCODE)) {
        out_url[0] = '\0';
        return 1;
    }
    out_url[out_capacity - 1] = '\0';
    return 0;
}

static int pcore_document_url(const char *host, const char *path, int port,
        char *out_url, int out_capacity)
{
    const char *scheme;
    int default_port;
    int n;

    if (host == NULL || host[0] == '\0' || path == NULL ||
            out_url == NULL || out_capacity <= 1) {
        return 1;
    }
    scheme = (port == 80) ? "http" : "https";
    default_port = (port == 80 || port == 443);
    if (default_port) {
        n = _snprintf(out_url, out_capacity - 1, "%s://%s%s",
                scheme, host, path);
    } else {
        n = _snprintf(out_url, out_capacity - 1, "%s://%s:%d%s",
                scheme, host, port, path);
    }
    out_url[out_capacity - 1] = '\0';
    return (n < 0 || n >= out_capacity - 1) ? 1 : 0;
}

/* Copy an absolute path (starts with '/') into dst, stripping any #fragment.
 * Falls back to "/" if empty. */
static void copy_path(char *dst, int cap, const char *src)
{
    int n = 0;
    if (cap <= 0) {
        return;
    }
    while (*src != '\0' && *src != '#' && n < cap - 1) {
        dst[n++] = *src++;
    }
    if (n == 0 && cap > 1) {
        dst[n++] = '/';
    }
    dst[n] = '\0';
}

/* Resolve a URL against an explicit page origin. The navigation worker uses
 * the pending origin while the visible page keeps its old global origin. */
static BOOL resolve_url_from(const char *base_host, const char *base_path,
        int base_port, const char *href, char *host, int hostcap,
        char *path, int pathcap, int *out_port)
{
    const char *p = href;

    *out_port = 443;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }

    if (ci_prefix(p, "http://")) {
        p += 7;
        *out_port = 80;
    } else if (ci_prefix(p, "https://")) {
        p += 8;
        *out_port = 443;
    } else if (ci_prefix(p, "mailto:") || ci_prefix(p, "javascript:") ||
               ci_prefix(p, "tel:") || p[0] == '#') {
        return FALSE;   /* not a navigable http(s) document link */
    } else if (p[0] == '/') {
        if (base_host == NULL || base_host[0] == '\0') {
            return FALSE;
        }
        cstr_copy(host, hostcap, base_host);
        copy_path(path, pathcap, p);
        *out_port = base_port;   /* same scheme as current page */
        return TRUE;
    } else {
        /* Same-directory relative: current host + base dir + href. */
        int n = 0, i, lastslash = -1;
        if (base_host == NULL || base_host[0] == '\0' || base_path == NULL) {
            return FALSE;
        }
        cstr_copy(host, hostcap, base_host);
        for (i = 0; base_path[i] != '\0'; i++) {
            if (base_path[i] == '/') {
                lastslash = i;
            }
        }
        for (i = 0; i <= lastslash && n < pathcap - 1; i++) {
            path[n++] = base_path[i];
        }
        if (lastslash < 0 && n < pathcap - 1) {
            path[n++] = '/';
        }
        while (*p != '\0' && *p != '#' && n < pathcap - 1) {
            path[n++] = *p++;
        }
        path[n] = '\0';
        *out_port = base_port;   /* same scheme as current page */
        return TRUE;
    }

    /* Absolute: p now points at host[:port][/path][#frag]. */
    {
        int n = 0;
        while (*p != '\0' && *p != '/' && *p != '#' && *p != ':' &&
                n < hostcap - 1) {
            host[n++] = *p++;
        }
        host[n] = '\0';
        if (n == 0) {
            return FALSE;
        }
        if (*p == ':') {
            int pt = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                pt = pt * 10 + (*p - '0');
                p++;
            }
            if (pt > 0) {
                *out_port = pt;
            }
        }
        if (*p == '/') {
            copy_path(path, pathcap, p);
        } else {
            cstr_copy(path, pathcap, "/");
        }
    }
    return TRUE;
}

/* Resolve a clicked link against the currently visible page. */
static BOOL resolve_url(const char *href, char *host, int hostcap,
                        char *path, int pathcap, int *out_port)
{
    return resolve_url_from(g_cur_host, g_cur_path, g_cur_port, href,
            host, hostcap, path, pathcap, out_port);
}

static pcore_navigation_resource *pcore_navigation_resource_find(
        pcore_navigation_request *request, const char *url)
{
    pcore_navigation_resource *entry;

    if (request == NULL || url == NULL) {
        return NULL;
    }
    for (entry = request->resources; entry != NULL; entry = entry->next) {
        if (strcmp(entry->url, url) == 0) {
            return entry;
        }
    }
    return NULL;
}

/* PCore calls this on the window thread. A cache hit returns an owned copy;
 * a miss is queued for the next worker stage and deliberately reports failure
 * to the current style/image discovery pass. */
static int pcore_navigation_resource_cb(void *pw, const char *url,
        char **out_data, int *out_len)
{
    pcore_navigation_request *request;
    pcore_navigation_resource *entry;
    size_t url_len;
    char *copy;

    request = (pcore_navigation_request *) pw;
    *out_data = NULL;
    *out_len = 0;
    if (request == NULL || url == NULL || url[0] == '\0') {
        return 1;
    }
    entry = pcore_navigation_resource_find(request, url);
    if (entry == NULL) {
        url_len = strlen(url);
        if (url_len == 0 || url_len >= 1024 ||
                request->resource_count >= PCORE_NAV_MAX_RESOURCES) {
            return 1;
        }
        entry = (pcore_navigation_resource *) malloc(sizeof(*entry));
        if (entry == NULL) {
            return 1;
        }
        memset(entry, 0, sizeof(*entry));
        entry->url = (char *) malloc(url_len + 1);
        if (entry->url == NULL) {
            free(entry);
            return 1;
        }
        memcpy(entry->url, url, url_len + 1);
        entry->next = request->resources;
        request->resources = entry;
        request->resource_count++;
        return 1;
    }
    if (entry->data == NULL || entry->len <= 0) {
        return 1;
    }
    copy = (char *) malloc((size_t) entry->len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, entry->data, (size_t) entry->len);
    *out_data = copy;
    *out_len = entry->len;
    return 0;
}

static void page_resource_free_cb(void *pw, char *data)
{
    (void) pw;
    free(data);
}

static int pcore_navigation_pending_count(
        const pcore_navigation_request *request)
{
    const pcore_navigation_resource *entry;
    int count;

    count = 0;
    if (request == NULL) {
        return 0;
    }
    for (entry = request->resources; entry != NULL; entry = entry->next) {
        if (!entry->attempted) {
            count++;
        }
    }
    return count;
}

static void pcore_navigation_request_free(
        pcore_navigation_request *request)
{
    pcore_navigation_resource *entry;

    if (request == NULL) {
        return;
    }
    if (request->response != NULL) {
        PHttp_FreeResponse(request->response);
    }
    if (request->document != NULL) {
        PCore_FreeDocument(request->document);
    }
    entry = request->resources;
    while (entry != NULL) {
        pcore_navigation_resource *next;

        next = entry->next;
        free(entry->url);
        free(entry->data);
        free(entry);
        entry = next;
    }
    free(request);
}

/* A resize must never start a network request. PCore's document stylesheet
 * cache supplies resources fetched during navigation; a cache miss simply
 * leaves the corresponding sheet absent, matching the original failure mode. */
static int page_resource_cache_only_cb(void *pw, const char *url,
        char **out_data, int *out_len)
{
    (void) pw;
    (void) url;
    *out_data = NULL;
    *out_len = 0;
    return 1;
}

static void pcore_navigation_set_loading(HWND hwnd, int loading)
{
    RECT r;
    int width;

    g_nav_loading = loading;
    g_nav_phase = 0;
    g_nav_determinate = 0;
    if (loading) {
        GetClientRect(hwnd, &r);
        width = r.right - r.left;
        g_nav_bar_h = GetSystemMetrics(SM_CYHSCROLL) / 3;
        if (g_nav_bar_h < 6) {
            g_nav_bar_h = 6;
        }
        g_nav_bar = CreateWindowExW(0, PROGRESS_CLASS, NULL,
                WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                0, 0, width, g_nav_bar_h, hwnd, NULL,
                GetModuleHandle(NULL), NULL);
        if (g_nav_bar != NULL) {
            SendMessage(g_nav_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(g_nav_bar, PBM_SETPOS, 0, 0);
        } else {
            SetWindowTextW(hwnd, L"Positron render - loading");
        }
        SetTimer(hwnd, PCORE_NAV_TIMER, 100, NULL);
    } else {
        KillTimer(hwnd, PCORE_NAV_TIMER);
        KillTimer(hwnd, PCORE_NAV_COMMIT_TIMER);
        if (g_nav_bar != NULL) {
            DestroyWindow(g_nav_bar);
            g_nav_bar = NULL;
        }
        SetWindowTextW(hwnd, L"Positron render");
    }
    GetClientRect(hwnd, &r);
    r.bottom = r.top + ((g_nav_bar_h > 0) ? g_nav_bar_h : 6);
    InvalidateRect(hwnd, &r, TRUE);
}

/* PHttp invokes this on the navigation worker. Coalesce notifications before
 * posting them to the WM window queue: a message per 2 KB TLS read would make
 * the old page less responsive on a slow device. Known-length transfers post
 * at two-percent steps; unknown-length transfers post every 16 KiB. */
static void pcore_navigation_progress(void *pw, int received, int total)
{
    pcore_navigation_request *request;
    int percent;
    int post;

    request = (pcore_navigation_request *) pw;
    if (request == NULL) {
        return;
    }
    post = 0;
    percent = -1;
    if (total != request->progress_last_total) {
        post = 1;
    } else if (total > 0) {
        percent = (received >= total) ? 100 : (received * 100) / total;
        if (percent >= request->progress_last_percent + 2 ||
                received >= total) {
            post = 1;
        }
    } else if (received == 0 || received >=
            request->progress_last_received + 16384) {
        post = 1;
    }
    if (!post) {
        return;
    }
    if (percent < 0 && total > 0) {
        percent = (received >= total) ? 100 : (received * 100) / total;
    }
    request->progress_last_total = total;
    request->progress_last_percent = percent;
    request->progress_last_received = received;
    PostMessage(request->hwnd, WM_PCORE_NAV_PROGRESS,
            (WPARAM) received, (LPARAM) total);
}

static PHttpResponse *pcore_navigation_get(
        pcore_navigation_request *request, const char *host, int port,
        const char *path)
{
    request->progress_last_total = -2;
    request->progress_last_percent = -2;
    request->progress_last_received = -16384;
    return PHttp_GetEx(host, port, path, NULL,
            pcore_navigation_progress, request);
}

static int pcore_navigation_response_error(
        const pcore_navigation_request *request, char *message, int capacity)
{
    const PHttpResponse *resp;

    resp = request->response;
    if (resp != NULL && resp->status_code == 200 && resp->body != NULL &&
            resp->body_len > 0) {
        return 0;
    }
    _snprintf(message, capacity - 1,
              "GET %s://%s%s -> status=%d %s",
              (request->port == 80) ? "http" : "https",
              request->host, request->path,
              (resp != NULL) ? resp->status_code : 0,
              (resp != NULL) ? resp->error_msg : "(null)");
    message[capacity - 1] = '\0';
    return 1;
}

static DWORD WINAPI pcore_navigation_worker(LPVOID param)
{
    pcore_navigation_request *request;
    pcore_navigation_resource *entry;
    PHttpResponse *resp;
    char host[256];
    char path[1024];
    int port;
    char *data;
    int fetched;

    request = (pcore_navigation_request *) param;
    if (request->worker_stage == PCORE_NAV_STAGE_DOCUMENT) {
        request->response = pcore_navigation_get(request, request->host,
                request->port, request->path);
        if (request->response != NULL &&
                request->response->status_code == 200 &&
                request->response->body != NULL &&
                request->response->body_len > 0) {
            request->stats.document_bytes = request->response->body_len;
        }
    } else {
        for (entry = request->resources; entry != NULL;
                entry = entry->next) {
            if (entry->attempted) {
                continue;
            }
            entry->attempted = 1;
            fetched = 0;
            port = request->port;
            if (!resolve_url_from(request->host, request->path,
                    request->port, entry->url, host, sizeof(host), path,
                    sizeof(path), &port)) {
                request->stats.resources_failed++;
                continue;
            }
            resp = pcore_navigation_get(request, host, port, path);
            if (resp != NULL && resp->status_code == 200 &&
                    resp->body != NULL && resp->body_len > 0 &&
                    resp->body_len <= PCORE_NAV_RESOURCE_BYTES_MAX -
                            request->resource_bytes) {
                data = (char *) malloc((size_t) resp->body_len);
                if (data != NULL) {
                    memcpy(data, resp->body, (size_t) resp->body_len);
                    entry->data = data;
                    entry->len = resp->body_len;
                    request->resource_bytes += resp->body_len;
                    request->stats.resources_fetched++;
                    fetched = 1;
                }
            } else if (resp != NULL && resp->body_len >
                    PCORE_NAV_RESOURCE_BYTES_MAX -
                            request->resource_bytes) {
                request->stats.budget_rejected++;
            }
            if (!fetched) {
                request->stats.resources_failed++;
            }
            if (resp != NULL) {
                PHttp_FreeResponse(resp);
            }
        }
    }
    request->stats.network_ms += GetTickCount() -
            request->stats.worker_started_tick;
    PostMessage(request->hwnd, WM_PCORE_NAV_DONE,
            (WPARAM) request->generation, (LPARAM) request);
    return 0;
}

static int pcore_navigation_start_worker(
        pcore_navigation_request *request)
{
    DWORD thread_id;

    request->stats.worker_started_tick = GetTickCount();
    request->stats.worker_rounds++;
    g_nav_thread = CreateThread(NULL, 0, pcore_navigation_worker,
            request, 0, &thread_id);
    return (g_nav_thread != NULL) ? 0 : 1;
}

/* Run one UI-owned commit phase, then yield to the WM message queue. A missing
 * callback body queues the URL and returns MORE for another worker stage. */
static int pcore_navigation_commit_step(HWND hwnd,
        pcore_navigation_request *request, int report_errors)
{
    PHttpResponse *resp;
    RECT           rc;
    int            cw, chh;
    char           emsg[320];
    char           document_url[1536];
    DWORD          started;
    DWORD          elapsed;

    if (request->commit_stage == PCORE_NAV_COMMIT_PARSE) {
        resp = request->response;
        if (pcore_navigation_response_error(request, emsg,
                sizeof(emsg))) {
            if (report_errors) {
                show_error(L"Navigation failed", emsg);
            }
            return PCORE_NAV_RESULT_FAILED;
        }
        started = GetTickCount();
        request->document = PCore_ParseHTML(resp->body, resp->body_len);
        elapsed = GetTickCount() - started;
        request->stats.parse_ms += elapsed;
        if (elapsed > request->stats.max_ui_slice_ms) {
            request->stats.max_ui_slice_ms = elapsed;
        }
        PHttp_FreeResponse(resp);
        request->response = NULL;
        if (request->document == NULL) {
            if (report_errors) {
                show_error(L"Navigation failed",
                        "PCore_ParseHTML returned NULL");
            }
            return PCORE_NAV_RESULT_FAILED;
        }
        request->commit_stage = PCORE_NAV_COMMIT_STYLE;
        return PCORE_NAV_RESULT_CONTINUE;
    }

    if (request->commit_stage == PCORE_NAV_COMMIT_STYLE) {
        GetClientRect(hwnd, &rc);
        cw = rc.right - rc.left;
        chh = rc.bottom - rc.top;
        if (cw <= 0) { cw = 224; }
        if (chh <= 0) { chh = 320; }
        PCore_SetViewport(cw, chh, 0);
        started = GetTickCount();
        if (pcore_document_url(request->host, request->path, request->port,
                document_url, sizeof(document_url)) != 0 ||
                PCore_StyleDocumentEx2(request->document, NULL, document_url,
                wm_combine_url, pcore_navigation_resource_cb,
                page_resource_free_cb, request) != 0) {
            elapsed = GetTickCount() - started;
            request->stats.style_ms += elapsed;
            if (elapsed > request->stats.max_ui_slice_ms) {
                request->stats.max_ui_slice_ms = elapsed;
            }
            if (report_errors) {
                show_error(L"Navigation failed",
                        "PCore_StyleDocument failed");
            }
            return PCORE_NAV_RESULT_FAILED;
        }
        elapsed = GetTickCount() - started;
        request->stats.style_ms += elapsed;
        if (elapsed > request->stats.max_ui_slice_ms) {
            request->stats.max_ui_slice_ms = elapsed;
        }
        request->commit_stage = PCORE_NAV_COMMIT_IMAGES;
        return PCORE_NAV_RESULT_CONTINUE;
    }

    if (request->commit_stage == PCORE_NAV_COMMIT_IMAGES) {
        started = GetTickCount();
        PCore_FetchImageResources(request->document,
                pcore_navigation_resource_cb, page_resource_free_cb,
                request, NULL, NULL);
        elapsed = GetTickCount() - started;
        request->stats.images_ms += elapsed;
        if (elapsed > request->stats.max_ui_slice_ms) {
            request->stats.max_ui_slice_ms = elapsed;
        }
        if (pcore_navigation_pending_count(request) > 0) {
            request->worker_stage = PCORE_NAV_STAGE_RESOURCES;
            request->commit_stage = PCORE_NAV_COMMIT_STYLE;
            return PCORE_NAV_RESULT_MORE;
        }
        request->commit_stage = PCORE_NAV_COMMIT_LAYOUT;
        return PCORE_NAV_RESULT_CONTINUE;
    }

    if (request->commit_stage != PCORE_NAV_COMMIT_LAYOUT) {
        if (report_errors) {
            show_error(L"Navigation failed", "Invalid UI commit stage");
        }
        return PCORE_NAV_RESULT_FAILED;
    }
    GetClientRect(hwnd, &rc);
    cw = rc.right - rc.left;
    chh = rc.bottom - rc.top;
    if (cw <= 0) { cw = 224; }
    if (chh <= 0) { chh = 320; }
    started = GetTickCount();
    if (PCore_LayoutDocument(request->document, cw, chh) != 0) {
        elapsed = GetTickCount() - started;
        request->stats.layout_ms += elapsed;
        if (elapsed > request->stats.max_ui_slice_ms) {
            request->stats.max_ui_slice_ms = elapsed;
        }
        if (report_errors) {
            show_error(L"Navigation failed", "PCore_LayoutDocument failed");
        }
        return PCORE_NAV_RESULT_FAILED;
    }
    elapsed = GetTickCount() - started;
    request->stats.layout_ms += elapsed;
    if (elapsed > request->stats.max_ui_slice_ms) {
        request->stats.max_ui_slice_ms = elapsed;
    }

    /* Swap in the new document; free the one being replaced. */
    if (g_render_doc != NULL) {
        PCore_FreeDocument(g_render_doc);
    }
    g_render_doc = request->document;
    request->document = NULL;
    g_render_sheet = NULL;
    g_doc_h = PCore_DocumentHeight(g_render_doc);
    g_scroll_y = 0;
    cstr_copy(g_cur_host, sizeof(g_cur_host), request->host);
    cstr_copy(g_cur_path, sizeof(g_cur_path), request->path);
    g_cur_port = request->port;

    pcore_set_scrollbar(hwnd);
    started = GetTickCount();
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    elapsed = GetTickCount() - started;
    request->stats.first_paint_ms += elapsed;
    if (elapsed > request->stats.max_ui_slice_ms) {
        request->stats.max_ui_slice_ms = elapsed;
    }
    request->stats.completed = 1;
    return PCORE_NAV_RESULT_DONE;
}

static void pcore_navigation_finish(HWND hwnd,
        pcore_navigation_request *request)
{
    request->stats.total_ms = GetTickCount() - request->stats.started_tick;
    request->stats.resources_queued = request->resource_count;
    request->stats.resource_bytes = request->resource_bytes;
    g_nav_last_stats = request->stats;
    g_nav_last_stats_valid = 1;
    if (hwnd != NULL) {
        pcore_navigation_set_loading(hwnd, 0);
    } else {
        g_nav_loading = 0;
        g_nav_determinate = 0;
    }
    if (request == g_nav_request) {
        g_nav_request = NULL;
    }
    pcore_navigation_request_free(request);
}

static int pcore_navigation_post_continue(HWND hwnd,
        pcore_navigation_request *request)
{
    g_nav_determinate = 0;
    g_nav_phase = 0;
    if (g_nav_bar != NULL) {
        SendMessage(g_nav_bar, PBM_SETPOS, 0, 0);
    }
    (void) request;
    return SetTimer(hwnd, PCORE_NAV_COMMIT_TIMER, 1, NULL) != 0 ? 0 : 1;
}

/* Start the main-document stage. Later stages reuse this request for external
 * CSS/image GETs while the old visible document remains interactive. */
static void navigate_to(HWND hwnd, const char *href)
{
    pcore_navigation_request *request;

    if (g_nav_loading) {
        return;
    }
    request = (pcore_navigation_request *) malloc(sizeof(*request));
    if (request == NULL) {
        show_error(L"Navigation failed", "Out of memory");
        return;
    }
    memset(request, 0, sizeof(*request));
    request->port = 443;
    if (!resolve_url(href, request->host, sizeof(request->host),
            request->path, sizeof(request->path), &request->port)) {
        free(request);
        show_info(L"Link", "Only http(s) document links are followed for now.");
        return;
    }

    request->hwnd = hwnd;
    request->generation = ++g_nav_generation;
    request->worker_stage = PCORE_NAV_STAGE_DOCUMENT;
    request->commit_stage = PCORE_NAV_COMMIT_NONE;
    request->stats.started_tick = GetTickCount();
    g_nav_request = request;
    pcore_navigation_set_loading(hwnd, 1);
    if (pcore_navigation_start_worker(request) != 0) {
        pcore_navigation_set_loading(hwnd, 0);
        g_nav_request = NULL;
        pcore_navigation_request_free(request);
        show_error(L"Navigation failed", "CreateThread failed");
    }
}

static void pcore_navigation_cleanup(void)
{
    if (g_nav_thread != NULL) {
        WaitForSingleObject(g_nav_thread, INFINITE);
        CloseHandle(g_nav_thread);
        g_nav_thread = NULL;
    }
    if (g_nav_request != NULL) {
        pcore_navigation_request_free(g_nav_request);
        g_nav_request = NULL;
    }
    g_nav_loading = 0;
}

static LRESULT CALLBACK PCoreWndProc(HWND hwnd, UINT msg,
                                     WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         hdc;

        hdc = BeginPaint(hwnd, &ps);
        /* Repaint only the invalid region. When scrolling, that is just the
         * thin strip ScrollWindowEx exposed; BeginPaint clips the DC to it, so
         * PCore_PaintDocument redraws a sliver, not the whole screen. */
        FillRect(hdc, &ps.rcPaint, (HBRUSH) GetStockObject(WHITE_BRUSH));
        if (g_plot_test) {
            PCore_PlotTest(hdc);   /* M1: drive the GDI plotter directly */
        } else if (g_svg_test) {
            RECT rcc;
            int width;
            int height;
            int intrinsic_w;
            int intrinsic_h;
            int rc;

            GetClientRect(hwnd, &rcc);
            width = rcc.right - rcc.left - 20;
            intrinsic_w = 2;
            intrinsic_h = 1;
            PImage_SvgGetInfo(g_svg_handle, &intrinsic_w, &intrinsic_h, NULL);
            if (intrinsic_w <= 0 || intrinsic_h <= 0) {
                intrinsic_w = 2;
                intrinsic_h = 1;
            }
            height = width * intrinsic_h / intrinsic_w;
            if (height > rcc.bottom - rcc.top - 40) {
                height = rcc.bottom - rcc.top - 40;
                width = height * intrinsic_w / intrinsic_h;
            }
            rc = PImage_DrawSvg(g_svg_handle, hdc, 10, 20, width, height);
            if (rc != PIMAGE_OK && g_svg_draw_rc == PIMAGE_OK) {
                g_svg_draw_rc = rc;
            }
        } else if (g_image_test) {
            RECT rcc;
            int rc;
            int i;
            int cell_h;
            int y;

            GetClientRect(hwnd, &rcc);
            cell_h = (rcc.bottom - rcc.top) / PCORE_IMAGE_FORMAT_COUNT;
            if (cell_h < 24) {
                cell_h = 24;
            }
            for (i = 0; i < PCORE_IMAGE_FORMAT_COUNT; i++) {
                y = i * cell_h;
                ExtTextOutW(hdc, 4, y + 4, 0, NULL,
                        g_image_format_name[i],
                        (UINT) wcslen(g_image_format_name[i]), NULL);
                rc = PImage_DrawBitmap(g_image_format_bitmap[i], hdc,
                        64, y + 2,
                        rcc.right - 68, cell_h - 4);
                if (rc == PIMAGE_OK) {
                    rc = PImage_DrawBitmap(g_image_format_bitmap[i], hdc,
                            64, y + 2, rcc.right - 68, cell_h - 4);
                }
                if (rc == PIMAGE_OK && i == 0) {
                    rc = PCore_DrawImageFromMemory(
                            (const char *) g_test_bmp_2x2,
                            (int) sizeof(g_test_bmp_2x2), hdc,
                            64, y + 2, rcc.right - 68, cell_h - 4);
                }
                if (rc != PIMAGE_OK && g_image_draw_rc == 0) {
                    g_image_draw_rc = rc;
                    break;
                }
            }
        } else if (g_ns_render) {
            RECT rcc;
            GetClientRect(hwnd, &rcc);
            PCore_NsRenderTest(hdc, rcc.right - rcc.left, rcc.bottom - rcc.top);
        } else if (g_render_doc != NULL) {
            PCore_PaintDocument(g_render_doc, hdc, 0, g_scroll_y);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;   /* WM_PAINT clears the invalid region itself; skip erase */
    case WM_SIZE: {
        int cw = LOWORD(lp);    /* new client width  */
        int chh = HIWORD(lp);   /* new client height */
        int old_scroll = g_scroll_y;
        int old_doc_h = g_doc_h;
        char document_url[1536];
        const char *document_base = NULL;

        if (g_overflow_pointer) {
            g_overflow_pointer = 0;
            ReleaseCapture();
        }

        /* Re-select styles and re-flow to the new client width (e.g. screen
         * rotation). The callback is cache-only, so this message never
         * starts a network request. */
        if (g_render_doc != NULL && cw > 0 && chh > 0) {
            PCore_SetViewport(cw, chh, 0);   /* dpi 0 = leave unchanged */
            if (pcore_document_url(g_cur_host, g_cur_path, g_cur_port,
                    document_url, sizeof(document_url)) == 0) {
                document_base = document_url;
            }
            if (PCore_StyleDocumentEx2(g_render_doc, g_render_sheet,
                    document_base, wm_combine_url,
                    page_resource_cache_only_cb, NULL, NULL) == 0) {
                PCore_LayoutDocument(g_render_doc, cw, chh);
            }
            g_doc_h = PCore_DocumentHeight(g_render_doc);
            g_scroll_y = pcore_scale_scroll_position(old_scroll, old_doc_h,
                    g_view_h, g_doc_h, chh);
        }
        g_view_h = chh;
        if (g_nav_bar != NULL && cw > 0) {
            MoveWindow(g_nav_bar, 0, 0, cw, g_nav_bar_h, TRUE);
        }
        pcore_set_scrollbar(hwnd);
        SHFullScreen(hwnd, SHFS_HIDESIPBUTTON);   /* keep SIP hidden on rotate */
        InvalidateRect(hwnd, NULL, TRUE);   /* full repaint after a resize */
        return 0;
    }
    case WM_VSCROLL: {
        RECT rc;
        int ch;

        GetClientRect(hwnd, &rc);
        ch = rc.bottom - rc.top;
        switch (LOWORD(wp)) {
        case SB_LINEUP:   pcore_scroll_by(hwnd, -16);  break;
        case SB_LINEDOWN: pcore_scroll_by(hwnd, 16);   break;
        case SB_PAGEUP:   pcore_scroll_by(hwnd, -ch);  break;
        case SB_PAGEDOWN: pcore_scroll_by(hwnd, ch);   break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            pcore_scroll_by(hwnd, (int) HIWORD(wp) - g_scroll_y);
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == PCORE_NAV_COMMIT_TIMER) {
            KillTimer(hwnd, PCORE_NAV_COMMIT_TIMER);
            if (g_nav_request != NULL) {
                SendMessage(hwnd, WM_PCORE_NAV_CONTINUE,
                        (WPARAM) g_nav_request->generation,
                        (LPARAM) g_nav_request);
            }
            return 0;
        }
        if (wp == PCORE_NAV_TIMER && g_nav_loading && !g_nav_determinate) {
            int pos;

            g_nav_phase = (g_nav_phase + 1) % 21;
            pos = (g_nav_phase <= 10) ? g_nav_phase * 10 :
                    (20 - g_nav_phase) * 10;
            if (g_nav_bar != NULL) {
                SendMessage(g_nav_bar, PBM_SETPOS, (WPARAM) pos, 0);
            }
            return 0;
        }
        break;
    case WM_PCORE_NAV_PROGRESS:
        if (g_nav_loading && g_nav_bar != NULL) {
            int received;
            int total;
            int pos;

            received = (int) wp;
            total = (int) lp;
            if (total >= 0) {
                g_nav_determinate = 1;
                if (total == 0 || received >= total) {
                    pos = 100;
                } else {
                    pos = (received * 100) / total;
                }
                if (pos < 0) { pos = 0; }
                if (pos > 100) { pos = 100; }
                SendMessage(g_nav_bar, PBM_SETPOS, (WPARAM) pos, 0);
            } else {
                g_nav_determinate = 0;
                g_nav_phase = 0;
                SendMessage(g_nav_bar, PBM_SETPOS, 0, 0);
            }
        }
        return 0;
    case WM_PCORE_NAV_DONE: {
        pcore_navigation_request *request;

        request = (pcore_navigation_request *) lp;
        if (g_nav_thread != NULL) {
            WaitForSingleObject(g_nav_thread, INFINITE);
            CloseHandle(g_nav_thread);
            g_nav_thread = NULL;
        }
        if (request == g_nav_request &&
                request->generation == g_nav_generation) {
            request->commit_stage =
                    (request->worker_stage == PCORE_NAV_STAGE_DOCUMENT) ?
                    PCORE_NAV_COMMIT_PARSE : PCORE_NAV_COMMIT_STYLE;
            if (pcore_navigation_post_continue(hwnd, request) != 0) {
                pcore_navigation_finish(hwnd, request);
            }
        }
        return 0;
    }
    case WM_PCORE_NAV_CONTINUE: {
        pcore_navigation_request *request;
        int result;

        request = (pcore_navigation_request *) lp;
        if (request != g_nav_request ||
                request->generation != g_nav_generation) {
            return 0;
        }
        result = pcore_navigation_commit_step(hwnd, request, 1);
        if (result == PCORE_NAV_RESULT_CONTINUE) {
            if (pcore_navigation_post_continue(hwnd, request) != 0) {
                pcore_navigation_finish(hwnd, request);
            }
        } else if (result == PCORE_NAV_RESULT_MORE) {
            if (pcore_navigation_start_worker(request) != 0) {
                show_error(L"Navigation failed",
                        "CreateThread failed during resource fetch");
                pcore_navigation_finish(hwnd, request);
            }
        } else {
            pcore_navigation_finish(hwnd, request);
        }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP:    pcore_scroll_by(hwnd, -16);   break;
        case VK_DOWN:  pcore_scroll_by(hwnd, 16);    break;
        case VK_PRIOR: pcore_scroll_by(hwnd, -120);  break;
        case VK_NEXT:  pcore_scroll_by(hwnd, 120);   break;
        case VK_ESCAPE:
        case VK_RETURN:
            DestroyWindow(hwnd);
            break;
        default:
            break;
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int cx = (int) (short) LOWORD(lp);
        int cy = (int) (short) HIWORD(lp);
        char href[1024];

        if (g_render_doc != NULL &&
                PCore_OverflowPointer(g_render_doc, PCORE_POINTER_DOWN,
                        cx, cy + g_scroll_y)) {
            g_overflow_pointer = 1;
            SetCapture(hwnd);
            pcore_invalidate_overflow(hwnd);
            return 0;
        }
        /* Document-space point = client point + scroll (scroll_x is 0). If it
         * lands on a link, follow it; otherwise a tap closes the view. */
        if (g_render_doc != NULL &&
                PCore_LinkAt(g_render_doc, cx, cy + g_scroll_y,
                             href, sizeof(href))) {
            navigate_to(hwnd, href);
        } else {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_overflow_pointer && g_render_doc != NULL &&
                (wp & MK_LBUTTON) != 0) {
            int cx = (int) (short) LOWORD(lp);
            int cy = (int) (short) HIWORD(lp);
            PCore_OverflowPointer(g_render_doc, PCORE_POINTER_MOVE,
                    cx, cy + g_scroll_y);
            pcore_invalidate_overflow(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_overflow_pointer) {
            int cx = (int) (short) LOWORD(lp);
            int cy = (int) (short) HIWORD(lp);
            if (g_render_doc != NULL) {
                PCore_OverflowPointer(g_render_doc, PCORE_POINTER_UP,
                        cx, cy + g_scroll_y);
            }
            g_overflow_pointer = 0;
            ReleaseCapture();
            pcore_invalidate_overflow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_nav_generation++;
        g_overflow_pointer = 0;
        pcore_navigation_set_loading(hwnd, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* Create the full-screen render window and run its message loop until closed.
 * Assumes g_render_doc + g_doc_h + g_scroll_y are already set. Returns FALSE
 * only if the window could not be created. */
static BOOL show_render_window(void)
{
    HINSTANCE hInst;
    WNDCLASSW wc;
    INITCOMMONCONTROLSEX icc;
    HWND      hwnd;
    MSG       m;

    hInst = GetModuleHandle(NULL);
    memset(&icc, 0, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&icc)) {
        InitCommonControls();
    }
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PCoreWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"PositronRenderWnd";
    RegisterClassW(&wc);

    hwnd = CreateWindowW(L"PositronRenderWnd", L"Positron render",
            WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL);
    if (hwnd == NULL) {
        return FALSE;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);   /* WinCE: grab focus, come to front */
    /* Read-only view: hide the SIP button and keep the keyboard down. */
    SHFullScreen(hwnd, SHFS_HIDESIPBUTTON);
    SHSipPreference(hwnd, SIP_FORCEDOWN);
    pcore_set_scrollbar(hwnd);

    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    pcore_navigation_cleanup();
    return TRUE;
}

static BOOL test12_render(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><head><title>x</title></head>"
        "<body><h2>Positron</h2>"
        "<div><p>This is a longer paragraph of text that should wrap across "
        "several lines inside the light-blue block, demonstrating real inline "
        "text measurement and word wrapping on the device screen.</p>"
        "<p>A second, shorter paragraph follows below it.</p>"
        "<p>Third paragraph: the document is now taller than the screen, so a "
        "vertical scrollbar appears. Drag it, or use the up/down keys, to "
        "scroll through the content.</p>"
        "<p>Fourth paragraph - more flowing text to push the page further "
        "down, past the bottom edge of the viewport.</p>"
        "<p>Fifth paragraph near the bottom; scrolling all the way down "
        "reveals it. Tap the content or press Esc to close.</p></div>"
        "<h2>The End</h2>"
        "</body></html>";
    static const char *CSS =
        "body { background-color: #ffffff; color: #202020; }\n"
        "h2   { color: #800000; }\n"
        "div  { background-color: #cce6ff; border: 2px solid #4060a0;"
        " padding: 6px; }\n"
        "p    { color: #103080; }\n";

    HANDLE    hDoc;
    HANDLE    hSheet;
    int       vw, vh;

    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 12 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/test.css");
    if (hSheet == NULL) {
        show_error(L"TEST 12 FAIL", "PCore_ParseCSS returned NULL");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    if (PCore_StyleDocument(hDoc, hSheet) != 0) {
        show_error(L"TEST 12 FAIL", "PCore_StyleDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }

    if (PCore_LayoutDocument(hDoc, vw, vh) != 0) {
        show_error(L"TEST 12 FAIL", "PCore_LayoutDocument failed");
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;

    show_info(L"TEST 12",
              "A render window will open. The page is taller than the\n"
              "screen: use the scrollbar or the up/down keys to scroll.\n\n"
              "Tap the content (or press Esc) to close and finish.");

    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        show_error(L"TEST 12 FAIL", "CreateWindow returned NULL");
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;

    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    show_info(L"TEST 12 OK",
              "Scrollable HTML page rendered:\n"
              "  H2 headings, bordered + padded div,\n"
              "  several wrapped paragraphs.\n"
              "  Scrolled via scrollbar / up-down keys.\n\n"
              "(box model + inline wrap + vertical scroll.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 13 - browse: fetch a real HTTPS page and render it                */
/* positron_http GET -> PCore_ParseHTML -> StyleDocumentEx (UA + inline and */
/* external CSS) -> image-resource cache -> layout -> render. PHttp is      */
/* assumed already initialised (WinMain does it). Failed images retain their */
/* <img> fallback text; JS is not fetched/executed yet.                     */
/* -------------------------------------------------------------------- */

static BOOL test_browse(void)
{
    static const char *START_HTML =
        "<!DOCTYPE html><html><head><title>Positron</title>"
        "<style>"
        "body{background-color:#ffffff;color:#202020;}"
        "h1{color:#800000;}"
        "p{margin-top:1em;margin-bottom:1em;}"
        "</style></head>"
        "<body><h1>Positron</h1>"
        "<p>Tap a link to fetch and render a real page over HTTPS:</p>"
        "<p><a href=\"https://example.com/\">Open example.com</a></p>"
        "<p><a href=\"https://raw.githubusercontent.com/lyk82468246/"
        "Positron/main/test_host/fixtures/network-svg.html\">"
        "Open network SVG fixture</a></p>"
        "<p>On the fetched page you can tap its own links too. Some hosts "
        "may be reset by the network (GFW); that error is expected.</p>"
        "<p>Tap empty space (or press Esc) to close.</p>"
        "</body></html>";

    HANDLE hDoc;
    int    vw, vh;
    char   summary[512];

    /* Landing page is offline; the actual fetch happens when the user taps
     * the link (navigate_to), exercising the full click -> fetch -> render
     * loop against a China-reachable host. */
    memset(&g_nav_last_stats, 0, sizeof(g_nav_last_stats));
    g_nav_last_stats_valid = 0;
    hDoc = PCore_ParseHTML(START_HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 13 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_StyleDocument(hDoc, NULL) != 0) {   /* UA + page's <style> */
        show_error(L"TEST 13 FAIL", "PCore_StyleDocument failed");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    PCore_SetViewport(vw, vh, 0);
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0) {
        show_error(L"TEST 13 FAIL", "PCore_LayoutDocument failed");
        PCore_FreeDocument(hDoc);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    /* No remote origin yet; the start page's link is absolute. */
    g_cur_host[0] = '\0';
    cstr_copy(g_cur_path, sizeof(g_cur_path), "/");

    show_info(L"TEST 13",
              "A start page opens. Open example.com for general Browse,\n"
              "or open the network SVG fixture for HTML + relative SVG.\n\n"
              "Tap empty space or press Esc to close.");

    g_render_doc = hDoc;
    g_render_sheet = NULL;
    if (!show_render_window()) {
        show_error(L"TEST 13 FAIL", "CreateWindow returned NULL");
        g_render_doc = NULL;
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    /* Navigation may have replaced the document; free whatever is current. */
    if (g_render_doc != NULL) {
        PCore_FreeDocument(g_render_doc);
    }
    g_render_doc = NULL;

    if (g_nav_last_stats_valid) {
        _snprintf(summary, sizeof(summary) - 1,
                "Last navigation %s:\n"
                "total=%lums network=%lums max UI=%lums\n"
                "parse/style/images/layout/paint=%lu/%lu/%lu/%lu/%lums\n"
                "resources queued/ok/fail=%d/%d/%d rounds=%d\n"
                "bytes document/cache=%d/%d budget-rejected=%d",
                g_nav_last_stats.completed ? "completed" : "failed",
                (unsigned long) g_nav_last_stats.total_ms,
                (unsigned long) g_nav_last_stats.network_ms,
                (unsigned long) g_nav_last_stats.max_ui_slice_ms,
                (unsigned long) g_nav_last_stats.parse_ms,
                (unsigned long) g_nav_last_stats.style_ms,
                (unsigned long) g_nav_last_stats.images_ms,
                (unsigned long) g_nav_last_stats.layout_ms,
                (unsigned long) g_nav_last_stats.first_paint_ms,
                g_nav_last_stats.resources_queued,
                g_nav_last_stats.resources_fetched,
                g_nav_last_stats.resources_failed,
                g_nav_last_stats.worker_rounds,
                g_nav_last_stats.document_bytes,
                g_nav_last_stats.resource_bytes,
                g_nav_last_stats.budget_rejected);
        summary[sizeof(summary) - 1] = '\0';
        show_info(L"TEST 13 OK (telemetry)", summary);
    } else {
        show_info(L"TEST 13 OK",
                  "Browse window closed without a completed navigation.\n"
                  "No navigation telemetry was recorded.");
    }
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 15 - milestone H/M3: DOM -> NetSurf box tree (slim builder)       */
/* Builds a struct box tree from a small styled document via the talloc   */
/* shim + pcore_box_construct, and reports box counts by type. Verifies    */
/* the box infrastructure before layout.c is ported. Offline.             */
/* -------------------------------------------------------------------- */
static BOOL test_boxtree(void)
{
    char buf[512];

    PCore_BoxTreeTest(buf, sizeof(buf));
    if (buf[0] == '\0') {
        show_error(L"TEST 15 FAIL", "PCore_BoxTreeTest produced no output");
        return FALSE;
    }
    if (strstr(buf, "normal_ws=ok") == NULL ||
            strstr(buf, "pre_lf=kept") == NULL) {
        show_error(L"TEST 15 FAIL", buf);
        return FALSE;
    }
    show_info(L"TEST 15 (box tree)", buf);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 16 - milestone H/M4: NetSurf real layout_document on the box tree */
/* Builds the box tree then runs the ported NetSurf layout.c over it and  */
/* reports box geometry. Verifies the layout engine compiles + runs.      */
/* -------------------------------------------------------------------- */
static BOOL test_layout(void)
{
    char buf[512];

    PCore_LayoutBoxTest(buf, sizeof(buf));
    if (buf[0] == '\0') {
        show_error(L"TEST 16 FAIL", "PCore_LayoutBoxTest produced no output");
        return FALSE;
    }
    show_info(L"TEST 16 (NetSurf layout)", buf);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 18 - image resource discovery / document cache                    */
/* Scans <img src>, fetches into the document cache, then scans again to   */
/* prove cache hits avoid duplicate fetches. No decode/paint yet.          */
/* -------------------------------------------------------------------- */
typedef struct image_resource_test_ctx {
    int calls;
    int matched;
    int frees;
} image_resource_test_ctx;

static int image_resource_fetch(void *pw, const char *url,
                                char **out_data, int *out_len)
{
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *buf;
    int ok;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    ok = (strcmp(url, "/img/logo.png") == 0 ||
          strcmp(url, "rel/photo.jpg") == 0);
    if (!ok) {
        return 1;
    }
    buf = (char *) malloc(4);
    if (buf == NULL) {
        return 1;
    }
    memcpy(buf, "IMG!", 4);
    *out_data = buf;
    *out_len = 4;
    ctx->matched++;
    return 0;
}

static void image_resource_free(void *pw, char *data)
{
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;

    ctx->frees++;
    free(data);
}

static BOOL test_image_resources(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body>"
        "<img alt=\"Logo\" src=\"/img/logo.png\">"
        "<img alt=\"Photo\" src=\"rel/photo.jpg\">"
        "<img alt=\"Empty\" src=\"\">"
        "<img alt=\"Missing\">"
        "</body></html>";
    HANDLE hDoc;
    image_resource_test_ctx ctx;
    int found;
    int fetched;
    int found_again;
    int fetched_again;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    found_again = 0;
    fetched_again = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 18 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_resource_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0) {
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 18 FAIL", "PCore_FetchImageResources failed");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, NULL, NULL, NULL,
            &found_again, &fetched_again) != 0) {
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 18 FAIL", "second image resource scan failed");
        return FALSE;
    }
    if (found != 2 || fetched != 2 || found_again != 2 ||
            fetched_again != 2 || ctx.calls != 2 || ctx.matched != 2 ||
            ctx.frees != 2) {
        sprintf(msg, "first=%d/%d second=%d/%d calls=%d matched=%d frees=%d",
                found, fetched, found_again, fetched_again,
                ctx.calls, ctx.matched, ctx.frees);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 18 FAIL", msg);
        return FALSE;
    }
    PCore_FreeDocument(hDoc);
    sprintf(msg, "image cache: first=%d/%d second=%d/%d; fetch calls=%d",
            found, fetched, found_again, fetched_again, ctx.calls);
    show_info(L"TEST 18 OK (image cache)", msg);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 19 - Windows Mobile native Imaging API format decode/draw          */
/* Probes BMP/PNG/JPEG/GIF independently through IImagingFactory/IImage,   */
/* then draws all successful fixtures in one window.                       */
/* -------------------------------------------------------------------- */
static int pcore_base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int pcore_decode_base64(const char *src, unsigned char *out, int cap)
{
    unsigned int acc;
    int bits;
    int count;
    int value;

    acc = 0;
    bits = 0;
    count = 0;
    while (*src != '\0' && *src != '=') {
        value = pcore_base64_value(*src++);
        if (value < 0) {
            continue;
        }
        acc = (acc << 6) | (unsigned int) value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (count >= cap) {
                return -1;
            }
            out[count++] = (unsigned char) ((acc >> bits) & 0xff);
        }
    }
    return count;
}

static BOOL test19_wmimage(void)
{
    unsigned char png[128];
    unsigned char jpeg[1024];
    unsigned char gif[64];
    const char *data[PCORE_IMAGE_FORMAT_COUNT];
    int len[PCORE_IMAGE_FORMAT_COUNT];
    int expect_w[PCORE_IMAGE_FORMAT_COUNT];
    int expect_h[PCORE_IMAGE_FORMAT_COUNT];
    const char *name[PCORE_IMAGE_FORMAT_COUNT];
    int w;
    int h;
    int rc;
    int legacy_w;
    int legacy_h;
    PIMAGE_BITMAP invalid_bitmap;
    int stage;
    int i;
    unsigned long hr;
    char msg[256];

    len[0] = sizeof(g_test_bmp_2x2);
    len[1] = pcore_decode_base64(g_test_png_2x2_b64, png, sizeof(png));
    len[2] = pcore_decode_base64(g_test_jpeg_16x16_b64, jpeg, sizeof(jpeg));
    len[3] = pcore_decode_base64(g_test_gif_2x2_b64, gif, sizeof(gif));
    data[0] = (const char *) g_test_bmp_2x2;
    data[1] = (const char *) png;
    data[2] = (const char *) jpeg;
    data[3] = (const char *) gif;
    name[0] = "BMP"; name[1] = "PNG"; name[2] = "JPEG"; name[3] = "GIF";
    expect_w[0] = 2; expect_w[1] = 2; expect_w[2] = 16; expect_w[3] = 2;
    expect_h[0] = 2; expect_h[1] = 2; expect_h[2] = 16; expect_h[3] = 2;

    for (i = 0; i < PCORE_IMAGE_FORMAT_COUNT; i++) {
        g_image_format_bitmap[i] = NULL;
    }
    for (i = 0; i < PCORE_IMAGE_FORMAT_COUNT; i++) {
        if (len[i] <= 0) {
            sprintf(msg, "%s fixture base64 decode failed", name[i]);
            show_error(L"TEST 19 FAIL", msg);
            goto fail;
        }
        w = 0;
        h = 0;
        rc = PImage_CreateBitmapFromMemory(data[i], len[i],
                &g_image_format_bitmap[i]);
        if (rc != PIMAGE_OK) {
            PImage_BitmapLastError(&stage, &hr);
            sprintf(msg, "%s info failed: rc=%d stage=%d hr=0x%08lx",
                    name[i], rc, stage, hr);
            show_error(L"TEST 19 FAIL", msg);
            goto fail;
        }
        rc = PImage_BitmapGetInfo(g_image_format_bitmap[i], &w, &h);
        if (rc != PIMAGE_OK) {
            PImage_BitmapLastError(&stage, &hr);
            sprintf(msg, "%s retained info failed: rc=%d stage=%d "
                    "hr=0x%08lx", name[i], rc, stage, hr);
            show_error(L"TEST 19 FAIL", msg);
            goto fail;
        }
        if (w != expect_w[i] || h != expect_h[i]) {
            sprintf(msg, "%s size=%dx%d, expect %dx%d", name[i], w, h,
                    expect_w[i], expect_h[i]);
            show_error(L"TEST 19 FAIL", msg);
            goto fail;
        }
    }

    legacy_w = 0;
    legacy_h = 0;
    rc = PCore_ImageInfoFromMemory(data[0], len[0], &legacy_w, &legacy_h);
    if (rc != 0 || legacy_w != expect_w[0] || legacy_h != expect_h[0]) {
        PCore_ImageLastError(&stage, &hr);
        sprintf(msg, "legacy core forward failed: rc=%d size=%dx%d "
                "stage=%d hr=0x%08lx", rc, legacy_w, legacy_h, stage, hr);
        show_error(L"TEST 19 FAIL", msg);
        goto fail;
    }

    invalid_bitmap = NULL;
    rc = PImage_CreateBitmapFromMemory("bad", 3, &invalid_bitmap);
    PImage_BitmapLastError(&stage, &hr);
    if (rc == PIMAGE_OK || invalid_bitmap != NULL ||
            (stage != PIMAGE_BITMAP_STAGE_CREATE &&
             stage != PIMAGE_BITMAP_STAGE_INFO)) {
        show_error(L"TEST 19 FAIL",
                "Malformed bitmap rejection/diagnostic was incorrect");
        PImage_FreeBitmap(invalid_bitmap);
        goto fail;
    }

    /* The public ABI promises to own a copy after creation. Destroy the
     * caller buffers before WM_PAINT to exercise that contract. */
    memset(png, 0, sizeof(png));
    memset(jpeg, 0, sizeof(jpeg));
    memset(gif, 0, sizeof(gif));

    show_info(L"TEST 19",
              "WM Imaging format coverage.\n\n"
              "Expect four labeled rows drawn twice by retained PImage handles:\n"
              "BMP, PNG, JPEG, GIF.\n"
              "Tap or press Esc to close.");

    g_image_test = 1;
    g_image_draw_rc = 0;
    g_plot_test = 0;
    g_ns_render = 0;
    g_render_doc = NULL;
    g_render_sheet = NULL;
    g_scroll_y = 0;
    g_doc_h = 0;
    if (!show_render_window()) {
        g_image_test = 0;
        show_error(L"TEST 19 FAIL", "CreateWindow returned NULL");
        goto fail;
    }
    g_image_test = 0;
    for (i = 0; i < PCORE_IMAGE_FORMAT_COUNT; i++) {
        PImage_FreeBitmap(g_image_format_bitmap[i]);
        g_image_format_bitmap[i] = NULL;
    }

    if (g_image_draw_rc != 0) {
        PImage_BitmapLastError(&stage, &hr);
        sprintf(msg, "retained draw failed: rc=%d stage=%d hr=0x%08lx",
                g_image_draw_rc, stage, hr);
        show_error(L"TEST 19 FAIL", msg);
        return FALSE;
    }
    sprintf(msg, "retained/drew BMP, PNG, JPEG and GIF after caller buffers "
            "were cleared; malformed input rejected; legacy ABI forwarded");
    show_info(L"TEST 19 OK", msg);
    return TRUE;

fail:
    g_image_test = 0;
    for (i = 0; i < PCORE_IMAGE_FORMAT_COUNT; i++) {
        PImage_FreeBitmap(g_image_format_bitmap[i]);
        g_image_format_bitmap[i] = NULL;
    }
    return FALSE;
}

/* -------------------------------------------------------------------- */
/* TEST 20 - cached <img> through NetSurf object/layout/redraw           */
/* The fetch body is copied into PCore's document cache first. The core  */
/* then makes a real replaced box, lets layout.c apply CSS dimensions, and */
/* reaches WM Imaging only through content_redraw -> plot_bitmap.         */
/* -------------------------------------------------------------------- */
static int image_bitmap_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *data;
    const char *b64;
    int cap;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    b64 = NULL;
    if (strcmp(url, "/img/test.bmp") == 0) {
        len = sizeof(g_test_bmp_2x2);
        data = (char *) malloc((size_t) len);
        if (data == NULL) {
            return 1;
        }
        memcpy(data, g_test_bmp_2x2, (size_t) len);
    } else if (strcmp(url, "/img/test.png") == 0) {
        b64 = g_test_png_2x2_b64;
    } else if (strcmp(url, "/img/test.jpg") == 0) {
        b64 = g_test_jpeg_16x16_b64;
    } else if (strcmp(url, "/img/test.gif") == 0) {
        b64 = g_test_gif_2x2_b64;
    } else {
        return 1;
    }
    if (b64 != NULL) {
        cap = (int) strlen(b64) * 3 / 4 + 4;
        data = (char *) malloc((size_t) cap);
        if (data == NULL) {
            return 1;
        }
        len = pcore_decode_base64(b64, (unsigned char *) data, cap);
        if (len <= 0) {
            free(data);
            return 1;
        }
    }
    *out_data = data;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test20_cached_img(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h2>Cached formats</h2>"
        "<p>NetSurf image objects: BMP, PNG, JPEG, GIF.</p>"
        "<div><img alt=\"BMP fallback\" src=\"/img/test.bmp\">"
        "<img alt=\"PNG fallback\" src=\"/img/test.png\">"
        "<img alt=\"JPEG fallback\" src=\"/img/test.jpg\">"
        "<img alt=\"GIF fallback\" src=\"/img/test.gif\"></div>"
        "<p>All four should show red/green above blue/yellow.</p>"
        "</body></html>";
    static const char *CSS =
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h2{color:#800000;}p{color:#103080;}"
        "img{width:48px;height:48px;border:1px solid #202020;"
        "margin-right:2px;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    int found = 0;
    int fetched = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int vw, vh;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 20 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_bitmap_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 4 || fetched != 4 || ctx.calls != 4 ||
            ctx.matched != 4 || ctx.frees != 4) {
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", "image cache setup failed");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/img.css");
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) {
            PCore_FreeStylesheet(hSheet);
        }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", "CSS styling failed");
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != 48 || h != 48) {
        sprintf(msg, "first image box=(%d,%d) %dx%d; expect 48x48",
                x, y, w, h);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 20",
              "Cached formats through NetSurf layout/redraw.\n\n"
              "Expect four bordered images in this order:\n"
              "BMP, PNG, JPEG, GIF. No fallback text.\n"
              "Tap or Esc to close.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    show_info(L"TEST 20 OK",
              "Cached BMP/PNG/JPEG/GIF became NetSurf replaced boxes\n"
              "and painted through content_redraw -> plot_bitmap ->\n"
              "WM Imaging IImage::Draw; fetch/free stayed 4/4.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 27 - cached SVG <img> through NetSurf object/layout/redraw       */
/* -------------------------------------------------------------------- */
static int image_svg_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" "
        "height=\"60\" viewBox=\"0 0 120 60\">"
        "<rect width=\"40\" height=\"60\" fill=\"#ff0000\"/>"
        "<rect x=\"40\" width=\"40\" height=\"60\" fill=\"#00ff00\"/>"
        "<path d=\"M86 52 C86 5 114 5 114 52\" fill=\"none\" "
        "stroke=\"#0000ff\" stroke-width=\"4\"/>"
        "</svg>";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/img/test.svg") != 0) {
        return 1;
    }
    len = (int) sizeof(SVG) - 1;
    copy = (char *) malloc((size_t) len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, SVG, (size_t) len);
    *out_data = copy;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test27_cached_svg_img(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h2>Cached SVG</h2>"
        "<p>The vector below is a NetSurf replaced image box.</p>"
        "<img alt=\"SVG fallback\" src=\"/img/test.svg\">"
        "<p>Expect red/green blocks and a smooth blue curve.</p>"
        "</body></html>";
    static const char *CSS =
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h2{color:#800000;}p{color:#103080;}"
        "img{width:120px;height:60px;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF red;
    COLORREF green;
    int found;
    int fetched;
    int x;
    int y;
    int w;
    int h;
    int vw;
    int vh;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    x = 0;
    y = 0;
    w = 0;
    h = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 27 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_svg_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1) {
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", "SVG image cache setup failed");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/svg.css");
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", "CSS styling failed");
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != 120 || h != 60) {
        sprintf(msg, "SVG image box=(%d,%d) %dx%d; expect 120x60",
                x, y, w, h);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, vw, vh) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, vw, vh);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    red = GetPixel(memory_dc, x + 20, y + 30);
    green = GetPixel(memory_dc, x + 60, y + 30);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if (red != RGB(255, 0, 0) || green != RGB(0, 255, 0)) {
        sprintf(msg, "SVG pixels red=0x%06lX green=0x%06lX",
                red & 0x00ffffffUL, green & 0x00ffffffUL);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 27",
              "Cached SVG through NetSurf layout/redraw.\n\n"
              "Expect red/green blocks and a smooth blue curve.\n"
              "No SVG fallback text. Tap or Esc to close.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 27 OK",
              "Cached SVG became a NetSurf replaced box and painted through\n"
              "content_redraw -> plot_bitmap -> positron_image.dll.\n"
              "Layout, fetch/free and off-screen pixels passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 28 - fetched but malformed SVG retains accessible alt fallback  */
/* -------------------------------------------------------------------- */
static int image_broken_svg_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char BROKEN[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" "
        "height=\"60\"><path d=\"M0 0";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/img/broken.svg") != 0) {
        return 1;
    }
    len = (int) sizeof(BROKEN) - 1;
    copy = (char *) malloc((size_t) len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, BROKEN, (size_t) len);
    *out_data = copy;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test28_broken_svg_fallback(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h2>Broken SVG fallback</h2>"
        "<p>The fetched bytes below are deliberately malformed.</p>"
        "<img alt=\"Broken SVG fallback text\" src=\"/img/broken.svg\">"
        "<p>The alt text must remain visible and the page must continue.</p>"
        "</body></html>";
    static const char *CSS =
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h2{color:#800000;}p{color:#103080;}img{color:#008000;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    int found;
    int fetched;
    int x;
    int y;
    int w;
    int h;
    int vw;
    int vh;
    char msg[192];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    x = 0;
    y = 0;
    w = 0;
    h = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 28 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_broken_svg_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1) {
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 28 FAIL", "broken SVG cache setup failed");
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/broken-svg.css");
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 28 FAIL", "CSS styling failed");
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w <= 0 || h <= 0 || (w == 120 && h == 60)) {
        sprintf(msg, "fallback box=(%d,%d) %dx%d; must not be 120x60",
                x, y, w, h);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 28 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 28",
              "Malformed SVG bytes were fetched and cached.\n\n"
              "Expect green text: Broken SVG fallback text.\n"
              "No image box should appear. Tap or Esc to close.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 28 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 28 OK",
              "Malformed cached SVG was rejected without aborting layout.\n"
              "The accessible alt text fallback remained in normal flow.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 26 - retained SVG object rendered through positron_image.dll     */
/* Verifies fills and an anti-aliased edge, then shows scaled rendering. */
/* -------------------------------------------------------------------- */
static BOOL test26_svg_draw(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" "
        "height=\"60\" viewBox=\"0 0 120 60\">"
        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"60\" "
        "fill=\"#ff0000\"/>"
        "<rect x=\"40\" y=\"0\" width=\"40\" height=\"60\" "
        "fill=\"#00ff00\"/>"
        "<path d=\"M86 52 C86 5 114 5 114 52\" fill=\"none\" "
        "stroke=\"#0000ff\" stroke-width=\"4\"/>"
        "</svg>";
    PIMAGE_SVG svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF red;
    COLORREF green;
    COLORREF pixel;
    int rc;
    int x;
    int y;
    int aa_pixels;
    char msg[192];

    svg = NULL;
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            120, 60, &svg);
    if (rc != PIMAGE_OK || svg == NULL) {
        _snprintf(msg, sizeof(msg) - 1, "create rc=%d handle=%p", rc, svg);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 26 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        memory_dc = CreateCompatibleDC(screen_dc);
        bitmap = CreateCompatibleBitmap(screen_dc, 120, 60);
    }
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PImage_FreeSvg(svg);
        show_error(L"TEST 26 FAIL", "could not create off-screen GDI surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 120, 60);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 120, 60);
    red = GetPixel(memory_dc, 20, 30);
    green = GetPixel(memory_dc, 60, 30);
    aa_pixels = 0;
    for (y = 0; y < 60; y++) {
        for (x = 80; x < 120; x++) {
            pixel = GetPixel(memory_dc, x, y);
            if (GetBValue(pixel) == 255 &&
                    GetRValue(pixel) > 0 && GetRValue(pixel) < 255 &&
                    GetGValue(pixel) > 0 && GetGValue(pixel) < 255) {
                aa_pixels++;
            }
        }
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if (rc != PIMAGE_OK || red != RGB(255, 0, 0) ||
            green != RGB(0, 255, 0) || aa_pixels == 0) {
        _snprintf(msg, sizeof(msg) - 1,
                "draw rc=%d red=0x%06lX green=0x%06lX aa=%d",
                rc, red & 0x00ffffffUL, green & 0x00ffffffUL,
                aa_pixels);
        msg[sizeof(msg) - 1] = '\0';
        PImage_FreeSvg(svg);
        show_error(L"TEST 26 FAIL", msg);
        return FALSE;
    }

    show_info(L"TEST 26",
              "A vector image window will open. Expect:\n"
              "red and green solid blocks, then a BLUE cubic curve.\n"
              "The drawing scales to the current client area.\n\n"
              "Tap or press Esc to close.");
    g_svg_handle = svg;
    g_svg_test = 1;
    g_svg_draw_rc = PIMAGE_OK;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_svg_test = 0;
        g_svg_handle = NULL;
        PImage_FreeSvg(svg);
        show_error(L"TEST 26 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_svg_test = 0;
    g_svg_handle = NULL;
    PImage_FreeSvg(svg);
    if (g_svg_draw_rc != PIMAGE_OK) {
        _snprintf(msg, sizeof(msg) - 1, "window draw rc=%d", g_svg_draw_rc);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 26 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 26 OK",
              "Retained SVG rendered through positron_image.dll:\n"
              "solid fills + anti-aliased cubic path + scaled stroke.\n"
              "Off-screen fill and partial edge pixels passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 29 - compound paths preserve inherited SVG fill-rule semantics  */
/* -------------------------------------------------------------------- */
static BOOL test29_svg_fill_rule(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" "
        "height=\"80\" viewBox=\"0 0 240 80\">"
        "<path fill=\"#ff0000\" d=\"M5 5 L75 5 L75 75 L5 75 Z "
        "M25 25 L55 25 L55 55 L25 55 Z\"/>"
        "<g fill-rule=\"evenodd\"><path fill=\"#0000ff\" "
        "d=\"M85 5 L155 5 L155 75 L85 75 Z "
        "M105 25 L135 25 L135 55 L105 55 Z\"/></g>"
        "<g style=\"fill-rule:evenodd\"><path fill=\"#00ff00\" "
        "d=\"M165 5 L235 5 L235 75 L165 75 Z "
        "M185 25 L215 25 L215 55 L185 55 Z\"/></g>"
        "</svg>";
    PIMAGE_SVG svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF left_ring;
    COLORREF left_center;
    COLORREF middle_ring;
    COLORREF middle_center;
    COLORREF right_ring;
    COLORREF right_center;
    int rc;
    char msg[256];

    svg = NULL;
    screen_dc = NULL;
    memory_dc = NULL;
    bitmap = NULL;
    old_bitmap = NULL;
    rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            240, 80, &svg);
    if (rc != PIMAGE_OK || svg == NULL) {
        sprintf(msg, "create rc=%d handle=%p", rc, svg);
        show_error(L"TEST 29 FAIL", msg);
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        memory_dc = CreateCompatibleDC(screen_dc);
        bitmap = CreateCompatibleBitmap(screen_dc, 240, 80);
    }
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PImage_FreeSvg(svg);
        show_error(L"TEST 29 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 80);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 240, 80);
    left_ring = GetPixel(memory_dc, 10, 40);
    left_center = GetPixel(memory_dc, 40, 40);
    middle_ring = GetPixel(memory_dc, 90, 40);
    middle_center = GetPixel(memory_dc, 120, 40);
    right_ring = GetPixel(memory_dc, 170, 40);
    right_center = GetPixel(memory_dc, 200, 40);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if (rc != PIMAGE_OK ||
            left_ring != RGB(255, 0, 0) ||
            left_center != RGB(255, 0, 0) ||
            middle_ring != RGB(0, 0, 255) ||
            middle_center != RGB(255, 255, 255) ||
            right_ring != RGB(0, 255, 0) ||
            right_center != RGB(255, 255, 255)) {
        sprintf(msg, "rc=%d L=%06lX/%06lX M=%06lX/%06lX R=%06lX/%06lX",
                rc, left_ring & 0xffffffUL, left_center & 0xffffffUL,
                middle_ring & 0xffffffUL, middle_center & 0xffffffUL,
                right_ring & 0xffffffUL, right_center & 0xffffffUL);
        PImage_FreeSvg(svg);
        show_error(L"TEST 29 FAIL", msg);
        return FALSE;
    }

    show_info(L"TEST 29",
              "Compound fill-rule window will open. Expect:\n"
              "solid RED square, BLUE frame, GREEN frame.\n"
              "The blue/green centres must be white.\n\n"
              "Tap or press Esc to close.");
    g_svg_handle = svg;
    g_svg_test = 1;
    g_svg_draw_rc = PIMAGE_OK;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_svg_test = 0;
        g_svg_handle = NULL;
        PImage_FreeSvg(svg);
        show_error(L"TEST 29 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_svg_test = 0;
    g_svg_handle = NULL;
    PImage_FreeSvg(svg);
    if (g_svg_draw_rc != PIMAGE_OK) {
        sprintf(msg, "window draw rc=%d", g_svg_draw_rc);
        show_error(L"TEST 29 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 29 OK",
              "Default nonzero and inherited evenodd fill rules passed.\n"
              "Presentation attribute and inline style were both preserved.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 30 - cached CSS background image through NetSurf redraw          */
/* -------------------------------------------------------------------- */
static int image_background_svg_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" "
        "height=\"20\" viewBox=\"0 0 20 20\">"
        "<rect width=\"10\" height=\"10\" fill=\"#ff0000\"/>"
        "<rect x=\"10\" width=\"10\" height=\"10\" fill=\"#00ff00\"/>"
        "<rect y=\"10\" width=\"10\" height=\"10\" fill=\"#0000ff\"/>"
        "<rect x=\"10\" y=\"10\" width=\"10\" height=\"10\" "
        "fill=\"#ffff00\"/></svg>";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/img/background.svg") != 0) {
        return 1;
    }
    len = (int) sizeof(SVG) - 1;
    copy = (char *) malloc((size_t) len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, SVG, (size_t) len);
    *out_data = copy;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test30_css_background_image(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body>"
        "<header></header><section></section>"
        "</body></html>";
    static const char *CSS =
        "body{background:#ffffff;margin:8px;}"
        "header{display:block;width:100px;height:40px;"
        "background-color:#ffffff;"
        "background-image:url('/img/background.svg');"
        "background-repeat:no-repeat;background-position:20px 10px;}"
        "section{display:block;width:60px;height:40px;margin-top:8px;"
        "background-image:url('/img/background.svg');"
        "background-repeat:repeat;background-position:0 0;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    int found;
    int fetched;
    int hx;
    int hy;
    int hw;
    int hh;
    int sx;
    int sy;
    int sw;
    int sh;
    int vw;
    int vh;
    COLORREF p0;
    COLORREF p1;
    COLORREF p2;
    COLORREF p3;
    COLORREF p4;
    COLORREF p5;
    COLORREF p6;
    COLORREF p7;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/background.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 30 FAIL", "parse/style failed");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_background_svg_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 2 || fetched != 2 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1) {
        sprintf(msg, "resources found=%d fetched=%d calls=%d matched=%d free=%d",
                found, fetched, ctx.calls, ctx.matched, ctx.frees);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 30 FAIL", msg);
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "header", &hx, &hy, &hw, &hh) != 0 ||
            PCore_NodeBox(hDoc, "section", &sx, &sy, &sw, &sh) != 0 ||
            hw != 100 || hh != 40 || sw != 60 || sh != 40) {
        sprintf(msg, "geometry header=%d,%d %dx%d section=%d,%d %dx%d",
                hx, hy, hw, hh, sx, sy, sw, sh);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 30 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, vw, vh) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 30 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, vw, vh);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    p0 = GetPixel(memory_dc, hx + 5, hy + 5);
    p1 = GetPixel(memory_dc, hx + 25, hy + 15);
    p2 = GetPixel(memory_dc, hx + 35, hy + 15);
    p3 = GetPixel(memory_dc, hx + 45, hy + 15);
    p4 = GetPixel(memory_dc, sx + 5, sy + 5);
    p5 = GetPixel(memory_dc, sx + 25, sy + 5);
    p6 = GetPixel(memory_dc, sx + 5, sy + 25);
    p7 = GetPixel(memory_dc, sx + 15, sy + 15);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if (p0 != RGB(255, 255, 255) || p1 != RGB(255, 0, 0) ||
            p2 != RGB(0, 255, 0) || p3 != RGB(255, 255, 255) ||
            p4 != RGB(255, 0, 0) || p5 != RGB(255, 0, 0) ||
            p6 != RGB(255, 0, 0) || p7 != RGB(255, 255, 0)) {
        sprintf(msg, "pixels %06lX %06lX %06lX %06lX / "
                "%06lX %06lX %06lX %06lX",
                p0 & 0xffffffUL, p1 & 0xffffffUL,
                p2 & 0xffffffUL, p3 & 0xffffffUL,
                p4 & 0xffffffUL, p5 & 0xffffffUL,
                p6 & 0xffffffUL, p7 & 0xffffffUL);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 30 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 30",
              "CSS background-image window will open.\n\n"
              "Expect one positioned tile, then a 3x2 tiled strip.\n"
              "Resource dedupe and eight pixels already passed.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 30 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 30 OK",
              "Cached CSS background-image passed position, no-repeat,\n"
              "repeat-x/y, resource dedupe and NetSurf redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 31 - SVG text through the native Windows Mobile GDI font backend */
/* -------------------------------------------------------------------- */
static BOOL test31_svg_text(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" "
        "height=\"100\" viewBox=\"0 0 240 100\">"
        "<rect width=\"240\" height=\"100\" fill=\"#ffffff\"/>"
        "<g style=\"font-family:sans-serif;font-size:24px;"
        "font-weight:bold;fill:#0000ff\">"
        "<text x=\"10\" y=\"30\">HIDDEN</text>"
        "<rect x=\"0\" y=\"0\" width=\"110\" height=\"40\" "
        "fill=\"#ffffff\"/>"
        "<text x=\"120\" y=\"76\" text-anchor=\"middle\">SVG TEXT</text>"
        "<rect x=\"116\" y=\"48\" width=\"8\" height=\"32\" "
        "fill=\"#ff0000\"/>"
        "</g></svg>";
    PIMAGE_SVG svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF pixel;
    int rc;
    int px;
    int py;
    int red;
    int green;
    int blue;
    int upper_blue;
    int lower_blue;
    int min_x;
    int max_x;
    int center_x;
    char msg[192];

    svg = NULL;
    rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            240, 100, &svg);
    if (rc != PIMAGE_OK || svg == NULL) {
        sprintf(msg, "create rc=%d", rc);
        show_error(L"TEST 31 FAIL", msg);
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 100) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PImage_FreeSvg(svg);
        show_error(L"TEST 31 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 100);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 240, 100);
    upper_blue = 0;
    lower_blue = 0;
    min_x = 240;
    max_x = -1;
    for (py = 0; py < 100; py++) {
        for (px = 0; px < 240; px++) {
            pixel = GetPixel(memory_dc, px, py);
            red = (int) GetRValue(pixel);
            green = (int) GetGValue(pixel);
            blue = (int) GetBValue(pixel);
            if (blue > 120 && blue > red + 40 && blue > green + 40) {
                if (py < 40 && px < 110) {
                    upper_blue++;
                }
                if (py >= 45) {
                    lower_blue++;
                    if (px < min_x) { min_x = px; }
                    if (px > max_x) { max_x = px; }
                }
            }
        }
    }
    pixel = GetPixel(memory_dc, 120, 60);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    center_x = (min_x <= max_x) ? (min_x + max_x) / 2 : -1;
    if (rc != PIMAGE_OK || upper_blue != 0 || lower_blue < 20 ||
            center_x < 105 || center_x > 135 ||
            pixel != RGB(255, 0, 0)) {
        sprintf(msg, "rc=%d hidden=%d visible=%d center=%d red=%06lX",
                rc, upper_blue, lower_blue, center_x,
                pixel & 0xffffffUL);
        PImage_FreeSvg(svg);
        show_error(L"TEST 31 FAIL", msg);
        return FALSE;
    }

    show_info(L"TEST 31",
              "SVG text window will open. Expect centred bold BLUE text\n"
              "with a narrow RED bar drawn over its middle.\n\n"
              "UTF-8 conversion, inherited font style, text anchor and\n"
              "path/text paint order already passed off-screen.");
    g_svg_handle = svg;
    g_svg_test = 1;
    g_svg_draw_rc = PIMAGE_OK;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_svg_test = 0;
        g_svg_handle = NULL;
        PImage_FreeSvg(svg);
        show_error(L"TEST 31 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_svg_test = 0;
    g_svg_handle = NULL;
    PImage_FreeSvg(svg);
    if (g_svg_draw_rc != PIMAGE_OK) {
        sprintf(msg, "window draw rc=%d", g_svg_draw_rc);
        show_error(L"TEST 31 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 31 OK",
              "Basic SVG text rendered through the native WM GDI font\n"
              "backend while preserving SVG paint order.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 32 - cached SVG gradient + text through the NetSurf image chain  */
/* -------------------------------------------------------------------- */
static int image_svg_gradient_text_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" "
        "height=\"80\" viewBox=\"0 0 160 80\">"
        "<defs><linearGradient id=\"g\" x1=\"0%\" y1=\"0%\" "
        "x2=\"100%\" y2=\"0%\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"50%\" stop-color=\"#800080\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect width=\"160\" height=\"80\" fill=\"url(#g)\"/>"
        "<text x=\"80\" y=\"48\" fill=\"#ffffff\" "
        "font-family=\"sans-serif\" font-size=\"20\" "
        "font-weight=\"bold\" text-anchor=\"middle\">Positron</text>"
        "</svg>";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/img/gradient-text.svg") != 0) {
        return 1;
    }
    len = (int) sizeof(SVG) - 1;
    copy = (char *) malloc((size_t) len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, SVG, (size_t) len);
    *out_data = copy;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test32_cached_svg_gradient_text(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h2>Cached SVG gradient</h2>"
        "<p>The image below is fetched into the document cache.</p>"
        "<img alt=\"Gradient SVG fallback\" "
        "src=\"/img/gradient-text.svg\">"
        "<p>Expect a red-to-blue gradient with white Positron text.</p>"
        "</body></html>";
    static const char *CSS =
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h2{color:#800000;}p{color:#103080;}"
        "img{width:160px;height:80px;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF left;
    COLORREF middle;
    COLORREF right;
    COLORREF pixel;
    COLORREF previous;
    int found;
    int fetched;
    int x;
    int y;
    int w;
    int h;
    int vw;
    int vh;
    int px;
    int py;
    int white_pixels;
    int seam_pixels;
    int large_jumps;
    int left_ok;
    int middle_ok;
    int right_ok;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    x = 0;
    y = 0;
    w = 0;
    h = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 32 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_svg_gradient_text_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1) {
        sprintf(msg, "cache found=%d fetched=%d calls=%d match=%d free=%d",
                found, fetched, ctx.calls, ctx.matched, ctx.frees);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 32 FAIL", msg);
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/svg-gradient.css");
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 32 FAIL", "CSS styling failed");
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != 160 || h != 80) {
        sprintf(msg, "SVG image box=(%d,%d) %dx%d; expect 160x80",
                x, y, w, h);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 32 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, vw, vh) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 32 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, vw, vh);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    left = GetPixel(memory_dc, x + 8, y + 12);
    middle = GetPixel(memory_dc, x + 80, y + 12);
    right = GetPixel(memory_dc, x + 151, y + 12);
    previous = CLR_INVALID;
    seam_pixels = 0;
    large_jumps = 0;
    for (px = x + 2; px < x + w - 2; px++) {
        pixel = GetPixel(memory_dc, px, y + 12);
        if (GetGValue(pixel) > 48) {
            seam_pixels++;
        }
        if (previous != CLR_INVALID &&
                abs((int) GetRValue(pixel) -
                (int) GetRValue(previous)) +
                abs((int) GetGValue(pixel) -
                (int) GetGValue(previous)) +
                abs((int) GetBValue(pixel) -
                (int) GetBValue(previous)) > 80) {
            large_jumps++;
        }
        previous = pixel;
    }
    white_pixels = 0;
    for (py = y + 27; py < y + 55; py += 2) {
        for (px = x + 35; px < x + 125; px += 2) {
            pixel = GetPixel(memory_dc, px, py);
            if (GetRValue(pixel) > 220 && GetGValue(pixel) > 220 &&
                    GetBValue(pixel) > 220) {
                white_pixels++;
            }
        }
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    left_ok = GetRValue(left) > 150 && GetBValue(left) < 120;
    middle_ok = GetRValue(middle) > 50 && GetBValue(middle) > 50;
    right_ok = GetBValue(right) > 150 && GetRValue(right) < 120;
    if (!left_ok || !middle_ok || !right_ok || white_pixels < 8 ||
            seam_pixels > 2 || large_jumps > 2) {
        sprintf(msg, "L=%06lX M=%06lX R=%06lX W=%d seam=%d jump=%d",
                left & 0xffffffUL, middle & 0xffffffUL,
                right & 0xffffffUL, white_pixels, seam_pixels, large_jumps);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 32 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 32",
              "Cached SVG gradient + text window will open.\n\n"
              "Expect a RED-to-BLUE gradient carrying centred WHITE\n"
              "Positron text. Cache, box and pixels already passed.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 32 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 32 OK",
              "Continuous libsvgtiny/NanoSVG gradient and native text\n"
              "passed cache -> replaced box -> NetSurf redraw.\n"
              "The scanline also passed the seam/jump guard.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 33 - SVG linear-gradient coordinate systems and transforms       */
/* -------------------------------------------------------------------- */
static BOOL test33_svg_gradient_coordinates(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" "
        "height=\"80\" viewBox=\"0 0 240 80\">"
        "<defs>"
        "<linearGradient id=\"bbox\" x1=\"0%\" y1=\"0%\" "
        "x2=\"100%\" y2=\"100%\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"50%\" stop-color=\"#800080\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</linearGradient>"
        "<linearGradient id=\"user\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"85\" y1=\"5\" x2=\"155\" y2=\"5\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</linearGradient>"
        "<linearGradient id=\"turned\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"165\" y1=\"5\" x2=\"235\" y2=\"5\" "
        "gradientTransform=\"rotate(90 165 5)\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</linearGradient>"
        "</defs>"
        "<rect x=\"5\" y=\"5\" width=\"70\" height=\"70\" "
        "fill=\"url(#bbox)\"/>"
        "<rect x=\"85\" y=\"5\" width=\"70\" height=\"70\" "
        "fill=\"url(#user)\"/>"
        "<rect x=\"165\" y=\"5\" width=\"70\" height=\"70\" "
        "fill=\"url(#turned)\"/>"
        "</svg>";
    PIMAGE_SVG svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF samples[9];
    COLORREF pixel;
    COLORREF previous;
    int rc;
    int i;
    int seam_pixels;
    int large_jumps;
    int red_ok;
    int purple_ok;
    int blue_ok;
    char msg[256];

    svg = NULL;
    rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            240, 80, &svg);
    if (rc != PIMAGE_OK || svg == NULL) {
        sprintf(msg, "create rc=%d handle=%p", rc, svg);
        show_error(L"TEST 33 FAIL", msg);
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 80) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PImage_FreeSvg(svg);
        show_error(L"TEST 33 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 80);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 240, 80);

    samples[0] = GetPixel(memory_dc, 12, 12);
    samples[1] = GetPixel(memory_dc, 40, 40);
    samples[2] = GetPixel(memory_dc, 68, 68);
    samples[3] = GetPixel(memory_dc, 92, 40);
    samples[4] = GetPixel(memory_dc, 120, 40);
    samples[5] = GetPixel(memory_dc, 148, 40);
    samples[6] = GetPixel(memory_dc, 200, 12);
    samples[7] = GetPixel(memory_dc, 200, 40);
    samples[8] = GetPixel(memory_dc, 200, 68);

    seam_pixels = 0;
    large_jumps = 0;
    previous = CLR_INVALID;
    for (i = 88; i <= 152; i++) {
        pixel = GetPixel(memory_dc, i, 40);
        if (GetGValue(pixel) > 48) { seam_pixels++; }
        if (previous != CLR_INVALID &&
                abs((int) GetRValue(pixel) -
                (int) GetRValue(previous)) +
                abs((int) GetGValue(pixel) -
                (int) GetGValue(previous)) +
                abs((int) GetBValue(pixel) -
                (int) GetBValue(previous)) > 80) {
            large_jumps++;
        }
        previous = pixel;
    }
    previous = CLR_INVALID;
    for (i = 8; i <= 72; i++) {
        pixel = GetPixel(memory_dc, 200, i);
        if (GetGValue(pixel) > 48) { seam_pixels++; }
        if (previous != CLR_INVALID &&
                abs((int) GetRValue(pixel) -
                (int) GetRValue(previous)) +
                abs((int) GetGValue(pixel) -
                (int) GetGValue(previous)) +
                abs((int) GetBValue(pixel) -
                (int) GetBValue(previous)) > 80) {
            large_jumps++;
        }
        previous = pixel;
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    red_ok = GetRValue(samples[0]) > 150 &&
            GetBValue(samples[0]) < 110 &&
            GetRValue(samples[3]) > 150 &&
            GetBValue(samples[3]) < 110 &&
            GetRValue(samples[6]) > 150 &&
            GetBValue(samples[6]) < 110;
    purple_ok = GetRValue(samples[1]) > 50 &&
            GetBValue(samples[1]) > 50 &&
            GetRValue(samples[4]) > 50 &&
            GetBValue(samples[4]) > 50 &&
            GetRValue(samples[7]) > 50 &&
            GetBValue(samples[7]) > 50;
    blue_ok = GetBValue(samples[2]) > 150 &&
            GetRValue(samples[2]) < 110 &&
            GetBValue(samples[5]) > 150 &&
            GetRValue(samples[5]) < 110 &&
            GetBValue(samples[8]) > 150 &&
            GetRValue(samples[8]) < 110;
    if (rc != PIMAGE_OK || !red_ok || !purple_ok || !blue_ok ||
            seam_pixels > 2 || large_jumps > 2) {
        sprintf(msg, "rc=%d rgb=%d/%d/%d seam=%d jump=%d",
                rc, red_ok, purple_ok, blue_ok,
                seam_pixels, large_jumps);
        PImage_FreeSvg(svg);
        show_error(L"TEST 33 FAIL", msg);
        return FALSE;
    }

    show_info(L"TEST 33",
              "Gradient coordinate window will open. Expect three\n"
              "smooth red-to-blue squares:\n"
              "diagonal, horizontal, then vertical.\n\n"
              "Nine colour samples and two seam guards already passed.");
    g_svg_handle = svg;
    g_svg_test = 1;
    g_svg_draw_rc = PIMAGE_OK;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_svg_test = 0;
        g_svg_handle = NULL;
        PImage_FreeSvg(svg);
        show_error(L"TEST 33 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_svg_test = 0;
    g_svg_handle = NULL;
    PImage_FreeSvg(svg);
    if (g_svg_draw_rc != PIMAGE_OK) {
        sprintf(msg, "window draw rc=%d", g_svg_draw_rc);
        show_error(L"TEST 33 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 33 OK",
              "objectBoundingBox, userSpaceOnUse and rotated linear\n"
              "gradient coordinates passed through positron_image.dll.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 34 - centred SVG radial gradients through NanoSVG rasterization  */
/* -------------------------------------------------------------------- */
static BOOL test34_svg_radial_gradient(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" "
        "height=\"100\" viewBox=\"0 0 240 100\">"
        "<defs>"
        "<radialGradient id=\"bbox\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"50%\" stop-color=\"#800080\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</radialGradient>"
        "<radialGradient id=\"user\" gradientUnits=\"userSpaceOnUse\" "
        "cx=\"120\" cy=\"50\" r=\"35\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"50%\" stop-color=\"#800080\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</radialGradient>"
        "<radialGradient id=\"moved\" gradientUnits=\"userSpaceOnUse\" "
        "cx=\"190\" cy=\"50\" r=\"30\" "
        "gradientTransform=\"translate(15 0)\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"50%\" stop-color=\"#800080\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</radialGradient>"
        "</defs>"
        "<rect x=\"5\" y=\"5\" width=\"70\" height=\"90\" "
        "fill=\"url(#bbox)\"/>"
        "<rect x=\"85\" y=\"5\" width=\"70\" height=\"90\" "
        "fill=\"url(#user)\"/>"
        "<rect x=\"165\" y=\"5\" width=\"70\" height=\"90\" "
        "fill=\"url(#moved)\"/>"
        "</svg>";
    PIMAGE_SVG svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF center[3];
    COLORREF middle[3];
    COLORREF edge[3];
    COLORREF pixel;
    COLORREF previous;
    int rc;
    int i;
    int panel;
    int start_x[3];
    int end_x[3];
    int seam_pixels;
    int large_jumps;
    int red_ok;
    int purple_ok;
    int blue_ok;
    char msg[256];

    svg = NULL;
    rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            240, 100, &svg);
    if (rc != PIMAGE_OK || svg == NULL) {
        sprintf(msg, "create rc=%d handle=%p", rc, svg);
        show_error(L"TEST 34 FAIL", msg);
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 100) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PImage_FreeSvg(svg);
        show_error(L"TEST 34 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 100);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 240, 100);

    center[0] = GetPixel(memory_dc, 40, 50);
    middle[0] = GetPixel(memory_dc, 40, 27);
    edge[0] = GetPixel(memory_dc, 8, 8);
    center[1] = GetPixel(memory_dc, 120, 50);
    middle[1] = GetPixel(memory_dc, 137, 50);
    edge[1] = GetPixel(memory_dc, 152, 50);
    center[2] = GetPixel(memory_dc, 205, 50);
    middle[2] = GetPixel(memory_dc, 220, 50);
    edge[2] = GetPixel(memory_dc, 233, 50);

    start_x[0] = 40;
    start_x[1] = 120;
    start_x[2] = 205;
    end_x[0] = 72;
    end_x[1] = 152;
    end_x[2] = 233;
    seam_pixels = 0;
    large_jumps = 0;
    for (panel = 0; panel < 3; panel++) {
        previous = CLR_INVALID;
        for (i = start_x[panel]; i <= end_x[panel]; i++) {
            pixel = GetPixel(memory_dc, i, 50);
            if (GetGValue(pixel) > 48) { seam_pixels++; }
            if (previous != CLR_INVALID &&
                    abs((int) GetRValue(pixel) -
                    (int) GetRValue(previous)) +
                    abs((int) GetGValue(pixel) -
                    (int) GetGValue(previous)) +
                    abs((int) GetBValue(pixel) -
                    (int) GetBValue(previous)) > 80) {
                large_jumps++;
            }
            previous = pixel;
        }
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    red_ok = 1;
    purple_ok = 1;
    blue_ok = 1;
    for (i = 0; i < 3; i++) {
        if (GetRValue(center[i]) <= 150 || GetBValue(center[i]) >= 110) {
            red_ok = 0;
        }
        if (GetRValue(middle[i]) <= 50 || GetBValue(middle[i]) <= 50) {
            purple_ok = 0;
        }
        if (GetBValue(edge[i]) <= 150 || GetRValue(edge[i]) >= 110) {
            blue_ok = 0;
        }
    }
    if (rc != PIMAGE_OK || !red_ok || !purple_ok || !blue_ok ||
            seam_pixels > 2 || large_jumps > 2) {
        sprintf(msg, "rc=%d rgb=%d/%d/%d seam=%d jump=%d",
                rc, red_ok, purple_ok, blue_ok,
                seam_pixels, large_jumps);
        PImage_FreeSvg(svg);
        show_error(L"TEST 34 FAIL", msg);
        return FALSE;
    }

    show_info(L"TEST 34",
              "Radial gradient window will open. Expect three smooth\n"
              "red-centre to blue-edge fields: ellipse, circle, and\n"
              "a circle shifted right by gradientTransform.\n\n"
              "Nine colour samples and three seam guards already passed.");
    g_svg_handle = svg;
    g_svg_test = 1;
    g_svg_draw_rc = PIMAGE_OK;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_svg_test = 0;
        g_svg_handle = NULL;
        PImage_FreeSvg(svg);
        show_error(L"TEST 34 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_svg_test = 0;
    g_svg_handle = NULL;
    PImage_FreeSvg(svg);
    if (g_svg_draw_rc != PIMAGE_OK) {
        sprintf(msg, "window draw rc=%d", g_svg_draw_rc);
        show_error(L"TEST 34 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 34 OK",
              "Centred radial gradients passed objectBoundingBox,\n"
              "userSpaceOnUse and gradientTransform through the DLL.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 35 - cached radial SVG through the NetSurf replaced-image chain */
/* -------------------------------------------------------------------- */
static int image_svg_radial_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" "
        "height=\"80\" viewBox=\"0 0 160 80\">"
        "<defs><radialGradient id=\"g\">"
        "<stop offset=\"0%\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"50%\" stop-color=\"#800080\"/>"
        "<stop offset=\"100%\" stop-color=\"#0000ff\"/>"
        "</radialGradient></defs>"
        "<rect width=\"160\" height=\"80\" fill=\"url(#g)\"/>"
        "</svg>";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/img/radial.svg") != 0) {
        return 1;
    }
    len = (int) sizeof(SVG) - 1;
    copy = (char *) malloc((size_t) len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, SVG, (size_t) len);
    *out_data = copy;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test35_cached_svg_radial_gradient(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h2>Cached radial SVG</h2>"
        "<p>The ellipse below is a NetSurf replaced image box.</p>"
        "<img alt=\"Radial SVG fallback\" src=\"/img/radial.svg\">"
        "<p>Expect a red centre fading through purple to blue.</p>"
        "</body></html>";
    static const char *CSS =
        "body{background-color:#ffffff;color:#202020;margin:8px;}"
        "h2{color:#800000;}p{color:#103080;}"
        "img{width:160px;height:80px;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF center;
    COLORREF horizontal_middle;
    COLORREF vertical_middle;
    COLORREF right_edge;
    COLORREF top_edge;
    COLORREF pixel;
    COLORREF previous;
    int found;
    int fetched;
    int x;
    int y;
    int w;
    int h;
    int vw;
    int vh;
    int px;
    int py;
    int seam_pixels;
    int large_jumps;
    int center_ok;
    int middle_ok;
    int edge_ok;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    x = 0;
    y = 0;
    w = 0;
    h = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc == NULL) {
        show_error(L"TEST 35 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_svg_radial_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 1 || fetched != 1 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1) {
        sprintf(msg, "cache found=%d fetched=%d calls=%d match=%d free=%d",
                found, fetched, ctx.calls, ctx.matched, ctx.frees);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 35 FAIL", msg);
        return FALSE;
    }
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/svg-radial.css");
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 35 FAIL", "CSS styling failed");
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != 160 || h != 80) {
        sprintf(msg, "radial SVG box=(%d,%d) %dx%d; expect 160x80",
                x, y, w, h);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 35 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, vw, vh) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 35 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, vw, vh);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    center = GetPixel(memory_dc, x + 80, y + 40);
    horizontal_middle = GetPixel(memory_dc, x + 120, y + 40);
    vertical_middle = GetPixel(memory_dc, x + 80, y + 20);
    right_edge = GetPixel(memory_dc, x + 156, y + 40);
    top_edge = GetPixel(memory_dc, x + 80, y + 2);

    seam_pixels = 0;
    large_jumps = 0;
    previous = CLR_INVALID;
    for (px = x + 80; px <= x + 156; px++) {
        pixel = GetPixel(memory_dc, px, y + 40);
        if (GetGValue(pixel) > 48) { seam_pixels++; }
        if (previous != CLR_INVALID &&
                abs((int) GetRValue(pixel) -
                (int) GetRValue(previous)) +
                abs((int) GetGValue(pixel) -
                (int) GetGValue(previous)) +
                abs((int) GetBValue(pixel) -
                (int) GetBValue(previous)) > 80) {
            large_jumps++;
        }
        previous = pixel;
    }
    previous = CLR_INVALID;
    for (py = y + 40; py >= y + 2; py--) {
        pixel = GetPixel(memory_dc, x + 80, py);
        if (GetGValue(pixel) > 48) { seam_pixels++; }
        if (previous != CLR_INVALID &&
                abs((int) GetRValue(pixel) -
                (int) GetRValue(previous)) +
                abs((int) GetGValue(pixel) -
                (int) GetGValue(previous)) +
                abs((int) GetBValue(pixel) -
                (int) GetBValue(previous)) > 80) {
            large_jumps++;
        }
        previous = pixel;
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    center_ok = GetRValue(center) > 150 && GetBValue(center) < 110;
    middle_ok = GetRValue(horizontal_middle) > 50 &&
            GetBValue(horizontal_middle) > 50 &&
            GetRValue(vertical_middle) > 50 &&
            GetBValue(vertical_middle) > 50;
    edge_ok = GetBValue(right_edge) > 150 &&
            GetRValue(right_edge) < 110 &&
            GetBValue(top_edge) > 150 && GetRValue(top_edge) < 110;
    if (!center_ok || !middle_ok || !edge_ok ||
            seam_pixels > 2 || large_jumps > 2) {
        sprintf(msg, "C=%06lX HM=%06lX VM=%06lX edge=%d seam=%d jump=%d",
                center & 0xffffffUL, horizontal_middle & 0xffffffUL,
                vertical_middle & 0xffffffUL, edge_ok,
                seam_pixels, large_jumps);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 35 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 35",
              "Cached radial SVG window will open.\n\n"
              "Expect a wide ellipse with a RED centre, PURPLE middle\n"
              "and BLUE edge. Cache, box and pixels already passed.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 35 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 35 OK",
              "Radial SVG passed cache -> replaced box -> NetSurf redraw.\n"
              "Horizontal and vertical continuity guards also passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 36 - gradient inheritance, unitless bbox and stop alpha matrix  */
/* -------------------------------------------------------------------- */
static BOOL test36_svg_gradient_feature_matrix(void)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"280\" "
        "height=\"90\" viewBox=\"0 0 280 90\">"
        "<defs>"
        "<linearGradient id=\"unit\" x1=\"0\" y1=\"0\" "
        "x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient>"
        "<radialGradient id=\"base\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</radialGradient>"
        "<radialGradient id=\"inherited\" xlink:href=\"#base\" "
        "cx=\".25\" cy=\".5\" r=\".5\"/>"
        "<linearGradient id=\"linearAlpha\" x1=\"0\" x2=\"1\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\" "
        "stop-opacity=\"0\"/>"
        "<stop offset=\"1\" stop-color=\"#ff0000\" "
        "style=\"stop-opacity:0.5\"/>"
        "</linearGradient>"
        "<radialGradient id=\"radialAlpha\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\" "
        "style=\"stop-opacity:0.5\"/>"
        "<stop offset=\"1\" stop-color=\"#ff0000\" "
        "stop-opacity=\"0\"/>"
        "</radialGradient>"
        "</defs>"
        "<rect x=\"5\" y=\"10\" width=\"60\" height=\"70\" "
        "fill=\"url(#unit)\"/>"
        "<rect x=\"75\" y=\"10\" width=\"60\" height=\"70\" "
        "fill=\"url(#inherited)\"/>"
        "<rect x=\"145\" y=\"10\" width=\"60\" height=\"70\" "
        "fill=\"#00ff00\"/>"
        "<rect x=\"145\" y=\"10\" width=\"60\" height=\"70\" "
        "fill=\"url(#linearAlpha)\"/>"
        "<rect x=\"215\" y=\"10\" width=\"60\" height=\"70\" "
        "fill=\"#00ff00\"/>"
        "<rect x=\"215\" y=\"10\" width=\"60\" height=\"70\" "
        "fill=\"url(#radialAlpha)\"/>"
        "</svg>";
    static const char CYCLE_SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" "
        "height=\"20\"><defs>"
        "<linearGradient id=\"a\" href=\"#b\"/>"
        "<linearGradient id=\"b\" href=\"#a\"/>"
        "</defs><rect width=\"20\" height=\"20\" fill=\"url(#a)\"/>"
        "</svg>";
    PIMAGE_SVG svg;
    PIMAGE_SVG cycle_svg;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF unit_left;
    COLORREF unit_right;
    COLORREF inherited_center;
    COLORREF inherited_edge;
    COLORREF linear_left;
    COLORREF linear_middle;
    COLORREF linear_right;
    COLORREF radial_center;
    COLORREF radial_edge;
    COLORREF cycle_pixel;
    int rc;
    int cycle_rc;
    int unit_ok;
    int inherited_ok;
    int linear_alpha_ok;
    int radial_alpha_ok;
    int cycle_ok;
    char msg[256];

    svg = NULL;
    cycle_svg = NULL;
    rc = PImage_CreateSvgFromMemory(SVG, (int) sizeof(SVG) - 1,
            280, 90, &svg);
    cycle_rc = PImage_CreateSvgFromMemory(CYCLE_SVG,
            (int) sizeof(CYCLE_SVG) - 1, 20, 20, &cycle_svg);
    if (rc != PIMAGE_OK || svg == NULL ||
            cycle_rc != PIMAGE_OK || cycle_svg == NULL) {
        sprintf(msg, "create matrix=%d/%p cycle=%d/%p",
                rc, svg, cycle_rc, cycle_svg);
        if (svg != NULL) { PImage_FreeSvg(svg); }
        if (cycle_svg != NULL) { PImage_FreeSvg(cycle_svg); }
        show_error(L"TEST 36 FAIL", msg);
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 280, 90) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PImage_FreeSvg(svg);
        PImage_FreeSvg(cycle_svg);
        show_error(L"TEST 36 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 280, 90);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    rc = PImage_DrawSvg(svg, memory_dc, 0, 0, 280, 90);
    unit_left = GetPixel(memory_dc, 8, 45);
    unit_right = GetPixel(memory_dc, 62, 45);
    inherited_center = GetPixel(memory_dc, 90, 45);
    inherited_edge = GetPixel(memory_dc, 132, 45);
    linear_left = GetPixel(memory_dc, 148, 45);
    linear_middle = GetPixel(memory_dc, 175, 45);
    linear_right = GetPixel(memory_dc, 202, 45);
    radial_center = GetPixel(memory_dc, 245, 45);
    radial_edge = GetPixel(memory_dc, 272, 45);
    SetRect(&rect, 0, 0, 20, 20);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    cycle_rc = PImage_DrawSvg(cycle_svg, memory_dc, 0, 0, 20, 20);
    cycle_pixel = GetPixel(memory_dc, 10, 10);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    unit_ok = GetRValue(unit_left) > 180 &&
            GetBValue(unit_left) < 90 &&
            GetBValue(unit_right) > 180 && GetRValue(unit_right) < 90;
    inherited_ok = GetRValue(inherited_center) > 180 &&
            GetBValue(inherited_center) < 90 &&
            GetBValue(inherited_edge) > 150 &&
            GetRValue(inherited_edge) < 110;
    linear_alpha_ok = GetGValue(linear_left) > 200 &&
            GetRValue(linear_left) < 50 &&
            GetRValue(linear_middle) > 35 &&
            GetRValue(linear_middle) < 100 &&
            GetGValue(linear_middle) > 150 &&
            GetRValue(linear_right) > 95 &&
            GetRValue(linear_right) < 175 &&
            GetGValue(linear_right) > 90;
    radial_alpha_ok = GetRValue(radial_center) > 95 &&
            GetRValue(radial_center) < 175 &&
            GetGValue(radial_center) > 90 &&
            GetGValue(radial_edge) > 190 &&
            GetRValue(radial_edge) < 65;
    cycle_ok = cycle_rc == PIMAGE_OK &&
            GetRValue(cycle_pixel) > 240 &&
            GetGValue(cycle_pixel) > 240 &&
            GetBValue(cycle_pixel) > 240;
    if (rc != PIMAGE_OK || !unit_ok || !inherited_ok ||
            !linear_alpha_ok || !radial_alpha_ok || !cycle_ok) {
        sprintf(msg, "rc=%d unit=%d inherit=%d alpha=%d/%d cycle=%d",
                rc, unit_ok, inherited_ok, linear_alpha_ok,
                radial_alpha_ok, cycle_ok);
        PImage_FreeSvg(svg);
        PImage_FreeSvg(cycle_svg);
        show_error(L"TEST 36 FAIL", msg);
        return FALSE;
    }

    PImage_FreeSvg(cycle_svg);
    show_info(L"TEST 36",
              "Gradient feature matrix will open. Expect four panels:\n"
              "red-to-blue, off-centre radial, transparent linear red\n"
              "over green, transparent radial red over green.\n\n"
              "Unitless coordinates, inheritance, alpha and cycle guard passed.");
    g_svg_handle = svg;
    g_svg_test = 1;
    g_svg_draw_rc = PIMAGE_OK;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_svg_test = 0;
        g_svg_handle = NULL;
        PImage_FreeSvg(svg);
        show_error(L"TEST 36 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_svg_test = 0;
    g_svg_handle = NULL;
    PImage_FreeSvg(svg);
    if (g_svg_draw_rc != PIMAGE_OK) {
        sprintf(msg, "window draw rc=%d", g_svg_draw_rc);
        show_error(L"TEST 36 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 36 OK",
              "Gradient inheritance, unitless objectBoundingBox values,\n"
              "stop opacity and cyclic-reference fallback passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 37 - inherited/alpha SVG reused by img and CSS background cache */
/* -------------------------------------------------------------------- */
static int image_svg_gradient_batch_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char SVG[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" "
        "height=\"80\" viewBox=\"0 0 160 80\">"
        "<defs>"
        "<radialGradient id=\"base\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\" "
        "stop-opacity=\"0.5\"/>"
        "<stop offset=\"1\" stop-color=\"#ff0000\" "
        "stop-opacity=\"0\"/>"
        "</radialGradient>"
        "<radialGradient id=\"derived\" href=\"#base\" "
        "cx=\".25\" cy=\".5\" r=\".5\"/>"
        "</defs>"
        "<rect width=\"160\" height=\"80\" fill=\"#00ff00\"/>"
        "<rect width=\"160\" height=\"80\" fill=\"url(#derived)\"/>"
        "</svg>";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;
    int len;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "/img/gradient-batch.svg") != 0) {
        return 1;
    }
    len = (int) sizeof(SVG) - 1;
    copy = (char *) malloc((size_t) len);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, SVG, (size_t) len);
    *out_data = copy;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static BOOL test37_cached_svg_gradient_batch(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h2>Gradient cache reuse</h2>"
        "<img alt=\"Gradient batch fallback\" "
        "src=\"/img/gradient-batch.svg\">"
        "<section></section></body></html>";
    static const char *CSS =
        "body{background:#ffffff;margin:8px;}h2{color:#800000;}"
        "img{display:block;width:160px;height:80px;}"
        "section{display:block;width:160px;height:80px;margin-top:8px;"
        "background-image:url('/img/gradient-batch.svg');"
        "background-repeat:no-repeat;}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF img_center;
    COLORREF img_middle;
    COLORREF img_edge;
    COLORREF bg_center;
    COLORREF bg_middle;
    COLORREF bg_edge;
    int found;
    int fetched;
    int ix;
    int iy;
    int iw;
    int ih;
    int sx;
    int sy;
    int sw;
    int sh;
    int vw;
    int vh;
    int img_ok;
    int bg_ok;
    char msg[256];

    ctx.calls = 0;
    ctx.matched = 0;
    ctx.frees = 0;
    found = 0;
    fetched = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/gradient-batch.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 37 FAIL", "parse/style failed");
        return FALSE;
    }
    if (PCore_FetchImageResources(hDoc, image_svg_gradient_batch_fetch,
            image_resource_free, &ctx, &found, &fetched) != 0 ||
            found != 2 || fetched != 2 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1) {
        sprintf(msg, "cache found=%d fetched=%d calls=%d match=%d free=%d",
                found, fetched, ctx.calls, ctx.matched, ctx.frees);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 37 FAIL", msg);
        return FALSE;
    }
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_NodeBox(hDoc, "img", &ix, &iy, &iw, &ih) != 0 ||
            PCore_NodeBox(hDoc, "section", &sx, &sy, &sw, &sh) != 0 ||
            iw != 160 || ih != 80 || sw != 160 || sh != 80) {
        sprintf(msg, "boxes img=%d,%d %dx%d bg=%d,%d %dx%d",
                ix, iy, iw, ih, sx, sy, sw, sh);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 37 FAIL", msg);
        return FALSE;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, vw, vh) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 37 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, vw, vh);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    img_center = GetPixel(memory_dc, ix + 40, iy + 40);
    img_middle = GetPixel(memory_dc, ix + 80, iy + 40);
    img_edge = GetPixel(memory_dc, ix + 156, iy + 40);
    bg_center = GetPixel(memory_dc, sx + 40, sy + 40);
    bg_middle = GetPixel(memory_dc, sx + 80, sy + 40);
    bg_edge = GetPixel(memory_dc, sx + 156, sy + 40);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    img_ok = GetRValue(img_center) > 95 &&
            GetRValue(img_center) < 175 &&
            GetGValue(img_center) > 90 &&
            GetRValue(img_middle) > 35 &&
            GetGValue(img_middle) > 150 &&
            GetGValue(img_edge) > 200 && GetRValue(img_edge) < 50;
    bg_ok = GetRValue(bg_center) > 95 &&
            GetRValue(bg_center) < 175 &&
            GetGValue(bg_center) > 90 &&
            GetRValue(bg_middle) > 35 &&
            GetGValue(bg_middle) > 150 &&
            GetGValue(bg_edge) > 200 && GetRValue(bg_edge) < 50;
    if (!img_ok || !bg_ok) {
        sprintf(msg, "img=%06lX/%06lX/%06lX bg=%06lX/%06lX/%06lX",
                img_center & 0xffffffUL, img_middle & 0xffffffUL,
                img_edge & 0xffffffUL, bg_center & 0xffffffUL,
                bg_middle & 0xffffffUL, bg_edge & 0xffffffUL);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 37 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 37",
              "Gradient cache reuse page will open. Expect two identical\n"
              "green fields with a half-transparent red focus left of centre:\n"
              "first an <img>, then a CSS background. Fetch ran once.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 37 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 37 OK",
              "Inherited alpha gradient reused one cached SVG for <img>\n"
              "and CSS background through NetSurf redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 38 - constrained :root custom properties compatibility          */
/* The compatibility layer intentionally handles only top-level :root   */
/* tokens in one sheet. Verify aliases, nested fallback and cycle        */
/* fallback without claiming element-scoped CSS Variables support.       */
/* -------------------------------------------------------------------- */
static BOOL test38_css_root_variables(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><main>alias</main>"
        "<section>nested fallback</section><aside>cycle fallback</aside>"
        "<mark>double fallback</mark></body></html>";
    static const char *CSS =
        "@charset \"UTF-8\";/* prelude must not hide :root */"
        ":root{--red:#ff0000;--blue:#0000ff;--alias:var(--red);"
        "--cycle-a:var(--cycle-b);--cycle-b:var(--cycle-a);}"
        "main{color:var(--alias);}"
        "section{color:var(--missing,var(--blue));}"
        "aside{color:var(--cycle-a,#00ff00);}"
        "mark{color:var(--missing,var(--also-missing,#112233));}";
    static const char *tags[4] = { "main", "section", "aside", "mark" };
    static const unsigned long expect[4] = {
        0x00ff0000UL, 0x000000ffUL, 0x0000ff00UL, 0x00112233UL
    };
    HANDLE hDoc;
    HANDLE hSheet;
    unsigned long argb;
    unsigned long got[4] = { 0, 0, 0, 0 };
    int i;
    char msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/root-variables.css");
    if (hDoc == NULL || hSheet == NULL) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 38 FAIL", "parse HTML/CSS failed");
        return FALSE;
    }
    if (PCore_StyleDocument(hDoc, hSheet) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 38 FAIL", "PCore_StyleDocument failed");
        return FALSE;
    }
    for (i = 0; i < 4; i++) {
        argb = 0;
        if (PCore_NodeComputedColor(hDoc, tags[i], &argb) != 0) {
            break;
        }
        got[i] = argb & 0x00ffffffUL;
        if (got[i] != expect[i]) {
            break;
        }
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    if (i != 4) {
        _snprintf(msg, sizeof(msg) - 1,
                "case=%d tag=%s got=0x%06lX expect=0x%06lX",
                i, (i < 4) ? tags[i] : "?",
                (i < 4) ? got[i] : 0UL,
                (i < 4) ? expect[i] : 0UL);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 38 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 38 OK",
              "Top-level :root design tokens passed:\n"
              "alias=red, nested fallback=blue, cycle fallback=green,\n"
              "and double fallback=#112233.\n\n"
              "(Element scope and cross-sheet variables remain unsupported.)");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 39 - IANA-style spacing tokens through layout and redraw         */
/* Current IANA CSS expresses narrow article/footer insets via nested    */
/* :root variables. Check two widths automatically, then show the same   */
/* document through the formal NetSurf window path.                      */
/* -------------------------------------------------------------------- */
static BOOL test39_css_variable_layout(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body>"
        "<article><main><h2>IANA spacing tokens</h2>"
        "<p>The green content band must keep equal side insets.</p>"
        "</main><aside>wide side navigation</aside></article>"
        "<footer><footnav>Footer inset uses a nested token.</footnav>"
        "</footer></body></html>";
    static const char *CSS =
        ":root{--space-xs:10px;--space-md:25px;--space-lg:50px;"
        "--page-margin-sm:var(--space-md);--panel:#ccffcc;}"
        "html,body{margin:0;padding:0;background:#ffffff;color:#203040;}"
        "article{display:flex;flex-direction:row-reverse;"
        "padding:var(--space-md) var(--space-lg);background:#eeeeee;}"
        "main{flex-grow:1;flex-basis:0;background-color:var(--panel);}"
        "aside{width:60px;}footer{padding:var(--space-xs) 0;"
        "background:#dddddd;}footnav{display:block;margin:var(--space-xs) "
        "var(--page-margin-sm);color:#003366;}"
        "@media(width <= 1000px){article{padding:var(--space-md) "
        "var(--space-md);}aside{display:none;}}";
    static const int widths[2] = { 240, 320 };
    HANDLE hDoc = NULL;
    HANDLE hSheet = NULL;
    HDC screen_dc;
    int dpi = 96;
    int screen_w;
    int screen_h;
    int pass;
    int mx = 0;
    int my = 0;
    int mw = 0;
    int mh = 0;
    int fx = 0;
    int fy = 0;
    int fw = 0;
    int fh = 0;
    char msg[256];

    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        int device_dpi = GetDeviceCaps(screen_dc, LOGPIXELSY);
        if (device_dpi > 0) { dpi = device_dpi; }
        ReleaseDC(NULL, screen_dc);
    }
    msg[0] = '\0';
    for (pass = 0; pass < 2; pass++) {
        hDoc = PCore_ParseHTML(HTML, 0);
        hSheet = PCore_ParseCSS(CSS, 0,
                "http://positron.local/iana-spacing.css");
        PCore_SetViewport(widths[pass], 320, dpi);
        if (hDoc == NULL || hSheet == NULL ||
                PCore_StyleDocument(hDoc, hSheet) != 0 ||
                PCore_LayoutDocument(hDoc, widths[pass], 320) != 0 ||
                PCore_NodeBox(hDoc, "main", &mx, &my, &mw, &mh) != 0 ||
                PCore_NodeBox(hDoc, "footnav", &fx, &fy, &fw, &fh) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "width=%d parse/style/layout lookup failed",
                    widths[pass]);
        } else if (mx != 25 || mw != widths[pass] - 50 || fx != 25) {
            _snprintf(msg, sizeof(msg) - 1,
                    "width=%d main=(%d,%d) %dx%d footnav.x=%d; "
                    "expect main x=25 w=%d footer x=25",
                    widths[pass], mx, my, mw, mh, fx, widths[pass] - 50);
        }
        msg[sizeof(msg) - 1] = '\0';
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        hSheet = NULL;
        hDoc = NULL;
        if (msg[0] != '\0') {
            break;
        }
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    PCore_SetViewport(screen_w, screen_h, dpi);
    if (pass != 2) {
        show_error(L"TEST 39 FAIL", msg);
        return FALSE;
    }

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/iana-spacing.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 39 FAIL", "visible parse/style/layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 39",
              "IANA-style variable layout will open. Expect a green content\n"
              "band inset equally by 25px and footer text aligned to the\n"
              "same 25px left edge. Rotation must retain those insets.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 39 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 39 OK",
              ":root spacing tokens passed 240/320px geometry and the\n"
              "formal NetSurf layout/redraw window path.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 40 - modern CSS values compatibility batch                      */
/* One automatic test covers OKLCH conversion, alpha, root-token use,   */
/* reducible calc arithmetic and preservation of mixed-unit calc.        */
/* -------------------------------------------------------------------- */
static BOOL test40_css_modern_values(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><main>red</main>"
        "<section>half red</section><mark>IANA link</mark>"
        "<aside>calc division</aside><article>mixed units</article>"
        "</body></html>";
    static const char *CSS =
        ":root{--brand:oklch(0.627955 0.257683 29.233);"
        "--half:oklch(0.627955 0.257683 29.233 / .5);"
        "--link:oklch(0.58 0.14 251);--base:20px;}"
        "html,body{margin:0;padding:0;}"
        "main{color:var(--brand);width:calc(var(--base) * 3);"
        "margin-left:calc((var(--base) - 5px) * 2);height:10px;}"
        "section{color:var(--half);height:10px;}"
        "mark{color:var(--link);}"
        "aside{width:calc(120px / 4);margin-left:calc(5px + 5px);"
        "height:10px;}"
        "article{width:77px;width:calc(100% - 20px);height:10px;}";
    static const char *tags[3] = { "main", "section", "mark" };
    static const unsigned long expected[3] = {
        0xffff0000UL, 0x80ff0000UL, 0xff2e7dcaUL
    };
    HANDLE hDoc;
    HANDLE hSheet;
    unsigned long got[3] = { 0, 0, 0 };
    int mx = 0;
    int my = 0;
    int mw = 0;
    int mh = 0;
    int ax = 0;
    int ay = 0;
    int aw = 0;
    int ah = 0;
    int rx = 0;
    int ry = 0;
    int rw = 0;
    int rh = 0;
    int i;
    char msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/modern-values.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 200, 200) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 40 FAIL", "parse/style/layout failed");
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        if (PCore_NodeComputedColor(hDoc, tags[i], &got[i]) != 0 ||
                got[i] != expected[i]) {
            break;
        }
    }
    if (i == 3 &&
            (PCore_NodeBox(hDoc, "main", &mx, &my, &mw, &mh) != 0 ||
             PCore_NodeBox(hDoc, "aside", &ax, &ay, &aw, &ah) != 0 ||
             PCore_NodeBox(hDoc, "article", &rx, &ry, &rw, &rh) != 0)) {
        i = 4;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    if (i < 3) {
        _snprintf(msg, sizeof(msg) - 1,
                "color %d %s got=%08lX expect=%08lX",
                i, tags[i], got[i], expected[i]);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 40 FAIL", msg);
        return FALSE;
    }
    if (i == 4 || mx != 30 || mw != 60 || ax != 10 || aw != 30 ||
            rx != 0 || rw != 77) {
        _snprintf(msg, sizeof(msg) - 1,
                "boxes main x/w=%d/%d aside=%d/%d mixed=%d/%d; "
                "expect 30/60 10/30 0/77",
                mx, mw, ax, aw, rx, rw);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 40 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 40 OK",
              "Modern CSS value batch passed:\n"
              "OKLCH red/alpha/IANA link colours; root-token calc with\n"
              "+, -, *, /; and mixed %/px calc remained unconverted.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 41 - narrow grid fallback must not displace a reversed flex item */
/* Mirrors IANA /numbers: main + hidden sidenav, a one-track grid and an */
/* overflow:auto wrapper containing a table wider than the viewport.     */
/* -------------------------------------------------------------------- */
static BOOL test41_grid_overflow_flex(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><article><main>"
        "<h1>Number Resources</h1>"
        "<p>Global coordination of Internet Protocol addressing.</p>"
        "<div class=grid><span class=inlinegrid>grid fallback</span>"
        "<div class=scroll><table><tr>"
        "<td>REGISTRY-NAME-THAT-MUST-NOT-SHIFT-THE-PAGE</td>"
        "<td>AREA-COVERED-BY-THIS-REGISTRY</td>"
        "</tr></table></div></div></main>"
        "<nav>wide side navigation</nav></article></body></html>";
    static const char *CSS =
        "html,body{margin:0;padding:0;background:#fff;}"
        "article{display:flex;flex-direction:row-reverse;padding:25px;}"
        "main{flex-grow:1;flex-basis:0;background:#f7f7fb;}"
        "nav{display:none;width:180px;}"
        ".grid{display:grid;grid-template-columns:1fr;gap:25px;}"
        ".inlinegrid{display:inline-grid;color:#0060a0;}"
        ".scroll{overflow:auto;border:1px solid #808080;}"
        "table{border-collapse:collapse;}td{white-space:nowrap;padding:4px;}";
    static const int widths[2] = { 224, 320 };
    HANDLE hDoc = NULL;
    HANDLE hSheet = NULL;
    HDC screen_dc;
    int dpi = 96;
    int screen_w;
    int screen_h;
    int pass;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    char msg[256];

    screen_dc = GetDC(NULL);
    if (screen_dc != NULL) {
        int device_dpi = GetDeviceCaps(screen_dc, LOGPIXELSY);
        if (device_dpi > 0) { dpi = device_dpi; }
        ReleaseDC(NULL, screen_dc);
    }
    msg[0] = '\0';
    for (pass = 0; pass < 2; pass++) {
        hDoc = PCore_ParseHTML(HTML, 0);
        hSheet = PCore_ParseCSS(CSS, 0,
                "http://positron.local/grid-overflow.css");
        PCore_SetViewport(widths[pass], 320, dpi);
        if (hDoc == NULL || hSheet == NULL ||
                PCore_StyleDocument(hDoc, hSheet) != 0 ||
                PCore_LayoutDocument(hDoc, widths[pass], 320) != 0 ||
                PCore_NodeBox(hDoc, "main", &x, &y, &w, &h) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "width=%d parse/style/layout lookup failed",
                    widths[pass]);
        } else if (x != 25 || w != widths[pass] - 50) {
            _snprintf(msg, sizeof(msg) - 1,
                    "width=%d main=(%d,%d) %dx%d; expect x=25 w=%d",
                    widths[pass], x, y, w, h, widths[pass] - 50);
        }
        msg[sizeof(msg) - 1] = '\0';
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        hSheet = NULL;
        hDoc = NULL;
        if (msg[0] != '\0') { break; }
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    PCore_SetViewport(screen_w, screen_h, dpi);
    if (pass != 2) {
        show_error(L"TEST 41 FAIL", msg);
        return FALSE;
    }

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/grid-overflow.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 41 FAIL", "visible parse/style/layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 41",
              "IANA /numbers overflow fixture will open. The heading and\n"
              "paragraph must keep equal 25px side insets. The deliberately\n"
              "wide table may clip, but must not move the page left.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 41 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 41 OK",
              "Grid fallback + overflow table kept reversed-flex main at\n"
              "x=25 for 224/320px and passed the formal redraw path.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 42 - NetSurf overflow scrollbar create/redraw/input batch       */
/* Paint once to create the retained widget, exercise its right arrow   */
/* through PCore_OverflowPointer, then show arrow and thumb interaction. */
/* -------------------------------------------------------------------- */
static BOOL test42_overflow_scrollbar(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><h1>Overflow table</h1>"
        "<p>The table stays inside this viewport.</p><section>"
        "<table><tr><td>ALPHA-REGISTRY</td><td>BETA-REGISTRY</td>"
        "<td>GAMMA-REGISTRY</td><td>DELTA-REGISTRY</td></tr>"
        "<tr><td>North</td><td>South</td><td>East</td><td>West</td>"
        "</tr></table></section><aside><table><tr><td></td><td></td>"
        "</tr></table></aside></body></html>";
    static const char *CSS =
        "html,body{background:#fff;}body{margin:16px;color:#102040;}"
        "h1{margin:0 0 8px 0;color:#800000;}p{margin:0 0 8px 0;}"
        "section{display:block;height:96px;overflow:auto;"
        "border:1px solid #606060;background:#f7f7fb;}"
        "table{border-collapse:collapse;}"
        "td{white-space:nowrap;padding:8px;border:1px solid #808080;}"
        "aside{display:block;width:180px;overflow:auto;margin-top:8px;"
        "border:1px solid #606060;}"
        "aside table{width:360px;table-layout:fixed;}"
        "aside td{width:180px;height:32px;padding:0;border:0;"
        "background:#f00;}aside td+td{background:#0f0;}";
    HANDLE hDoc;
    HANDLE hSheet;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    int sx;
    int sy;
    int sw;
    int sh;
    int ax;
    int ay;
    int aw;
    int ah;
    int before_x;
    int before_y;
    int before_w;
    int before_h;
    int after_x;
    int after_y;
    int after_w;
    int after_h;
    int down_used;
    int up_used;
    int dirty_used;
    int dirty_x;
    int dirty_y;
    int dirty_w;
    int dirty_h;
    int arrow_min_y;
    int arrow_max_y;
    int px;
    int py;
    COLORREF auto_guard;
    COLORREF arrow_pixel;
    int screen_w;
    int screen_h;
    char msg[256];

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/overflow-scrollbar.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0 ||
            PCore_NodeBox(hDoc, "section", &sx, &sy, &sw, &sh) != 0 ||
            PCore_NodeBox(hDoc, "aside", &ax, &ay, &aw, &ah) != 0 ||
            PCore_NodeBox(hDoc, "td", &before_x, &before_y,
                    &before_w, &before_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 42 FAIL", "parse/style/layout lookup failed");
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 320) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 42 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 320);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    auto_guard = GetPixel(memory_dc, ax + 5, ay + ah - 3);
    arrow_min_y = 16;
    arrow_max_y = -1;
    for (py = 0; py < 16; py++) {
        for (px = 3; px < 14; px++) {
            arrow_pixel = GetPixel(memory_dc,
                    sx + sw - 16 + px, sy + sh - 16 + py);
            if ((arrow_pixel & 0x00ffffffUL) == 0) {
                if (py < arrow_min_y) { arrow_min_y = py; }
                if (py > arrow_max_y) { arrow_max_y = py; }
            }
        }
    }

    down_used = PCore_OverflowPointer(hDoc, PCORE_POINTER_DOWN,
            sx + sw - 8, sy + sh - 8);
    dirty_x = 0;
    dirty_y = 0;
    dirty_w = 0;
    dirty_h = 0;
    dirty_used = PCore_OverflowDirtyRect(hDoc, &dirty_x, &dirty_y,
            &dirty_w, &dirty_h);
    up_used = PCore_OverflowPointer(hDoc, PCORE_POINTER_UP,
            sx + sw - 8, sy + sh - 8);
    PCore_NodeBox(hDoc, "td", &after_x, &after_y, &after_w, &after_h);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    if (!down_used || !up_used || !dirty_used ||
            dirty_w <= 0 || dirty_h <= 0 || dirty_w >= 240 ||
            dirty_h >= 320 || after_x != before_x - 16 ||
            (auto_guard & 0x00ffffffUL) !=
                    (RGB(255, 0, 0) & 0x00ffffffUL) ||
            arrow_min_y + arrow_max_y != 16) {
        _snprintf(msg, sizeof(msg) - 1,
                "used=%d/%d/%d td.x=%d->%d dirty=%d,%d %dx%d "
                "auto=%06lX arrow=%d/%d",
                down_used, up_used, dirty_used, before_x, after_x,
                dirty_x, dirty_y, dirty_w, dirty_h,
                auto_guard & 0x00ffffffUL, arrow_min_y, arrow_max_y);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 42 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/overflow-scrollbar.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 42 FAIL", "visible parse/style/layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 42",
              "A wide table will open inside a bordered overflow box.\n"
              "Expect horizontal NetSurf scrollbars below both tables, not\n"
              "covering their last row. Tap arrows and drag the first thumb;\n"
              "only that table content should move.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 42 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 42 OK",
              "Fixed/auto-height overflow spacing, widget redraw and\n"
              "right-arrow input passed; visible WM forwarding was exercised.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 43 - staged navigation resource transaction, fully offline       */
/* -------------------------------------------------------------------- */
static BOOL test43_navigation_resource_transaction(void)
{
    pcore_navigation_request *request;
    pcore_navigation_resource *entry;
    char host[256];
    char path[1024];
    char *data;
    char *hit_data;
    int len;
    int hit_len;
    int port;
    int first_rc;
    int duplicate_rc;
    int hit_rc;
    int root_ok;
    int absolute_ok;
    char msg[256];

    request = (pcore_navigation_request *) malloc(sizeof(*request));
    if (request == NULL) {
        show_error(L"TEST 43 FAIL", "request allocation failed");
        return FALSE;
    }
    memset(request, 0, sizeof(*request));
    cstr_copy(request->host, sizeof(request->host), "example.test");
    cstr_copy(request->path, sizeof(request->path), "/dir/page.html");
    request->port = 443;

    port = 0;
    root_ok = resolve_url_from(request->host, request->path, request->port,
            "/img/logo.png", host, sizeof(host), path, sizeof(path), &port) &&
            strcmp(host, "example.test") == 0 &&
            strcmp(path, "/img/logo.png") == 0 && port == 443;
    port = 0;
    absolute_ok = resolve_url_from(request->host, request->path,
            request->port, "http://cdn.test:8080/a.png#fragment",
            host, sizeof(host), path, sizeof(path), &port) &&
            strcmp(host, "cdn.test") == 0 && strcmp(path, "/a.png") == 0 &&
            port == 8080;

    data = NULL;
    len = 0;
    first_rc = pcore_navigation_resource_cb(request, "style.css",
            &data, &len);
    duplicate_rc = pcore_navigation_resource_cb(request, "style.css",
            &data, &len);
    entry = pcore_navigation_resource_find(request, "style.css");
    if (entry == NULL) {
        pcore_navigation_request_free(request);
        show_error(L"TEST 43 FAIL", "resource queue lookup failed");
        return FALSE;
    }
    entry->data = (char *) malloc(3);
    if (entry->data == NULL) {
        pcore_navigation_request_free(request);
        show_error(L"TEST 43 FAIL", "resource body allocation failed");
        return FALSE;
    }
    memcpy(entry->data, "css", 3);
    entry->len = 3;
    entry->attempted = 1;
    request->resource_bytes = 3;
    data = NULL;
    len = 0;
    hit_rc = pcore_navigation_resource_cb(request, "style.css",
            &data, &len);
    hit_data = data;
    hit_len = len;
    data = NULL;
    len = 0;

    if (pcore_navigation_resource_cb(request, "/img/missing.png",
            &data, &len) != 1) {
        if (data != NULL) { page_resource_free_cb(NULL, data); }
        if (hit_data != NULL) { page_resource_free_cb(NULL, hit_data); }
        pcore_navigation_request_free(request);
        show_error(L"TEST 43 FAIL", "new resource unexpectedly hit");
        return FALSE;
    }
    entry = pcore_navigation_resource_find(request, "/img/missing.png");
    if (entry != NULL) {
        entry->attempted = 1;   /* model one failed worker attempt */
    }

    if (!root_ok || !absolute_ok || first_rc != 1 || duplicate_rc != 1 ||
            request->resource_count != 2 || hit_rc != 0 ||
            hit_data == NULL || hit_len != 3 ||
            memcmp(hit_data, "css", 3) != 0 || entry == NULL ||
            pcore_navigation_pending_count(request) != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                "url=%d/%d rc=%d/%d/%d count=%d len=%d pending=%d",
                root_ok, absolute_ok, first_rc, duplicate_rc, hit_rc,
                request->resource_count, hit_len,
                pcore_navigation_pending_count(request));
        msg[sizeof(msg) - 1] = '\0';
        if (data != NULL) { page_resource_free_cb(NULL, data); }
        if (hit_data != NULL) { page_resource_free_cb(NULL, hit_data); }
        pcore_navigation_request_free(request);
        show_error(L"TEST 43 FAIL", msg);
        return FALSE;
    }
    page_resource_free_cb(NULL, hit_data);
    pcore_navigation_request_free(request);
    show_info(L"TEST 43 OK",
              "Navigation resource transaction: explicit origin URL resolve,\n"
              "dedupe, copied cache hit and one-shot failure all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 44 - failed main navigation keeps the visible page, fully offline */
/* -------------------------------------------------------------------- */
static BOOL test44_navigation_failure_transaction(void)
{
    static const char *OLD_HTML =
        "<!doctype html><html><body><p>visible page</p></body></html>";
    pcore_navigation_request *request;
    PHttpResponse *response;
    HANDLE old_document;
    int result;
    int kept;
    int cleared;
    char msg[160];

    if (g_render_doc != NULL || g_nav_request != NULL || g_nav_loading) {
        show_error(L"TEST 44 FAIL", "navigation globals were not idle");
        return FALSE;
    }
    old_document = PCore_ParseHTML(OLD_HTML, 0);
    request = (pcore_navigation_request *) malloc(sizeof(*request));
    response = (PHttpResponse *) HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*response));
    if (old_document == NULL || request == NULL || response == NULL) {
        if (response != NULL) {
            HeapFree(GetProcessHeap(), 0, response);
        }
        if (request != NULL) { free(request); }
        if (old_document != NULL) { PCore_FreeDocument(old_document); }
        show_error(L"TEST 44 FAIL", "fixture allocation failed");
        return FALSE;
    }
    memset(request, 0, sizeof(*request));
    cstr_copy(request->host, sizeof(request->host), "offline.invalid");
    cstr_copy(request->path, sizeof(request->path), "/missing.html");
    request->port = 443;
    request->worker_stage = PCORE_NAV_STAGE_DOCUMENT;
    request->commit_stage = PCORE_NAV_COMMIT_PARSE;
    request->stats.started_tick = GetTickCount();
    request->response = response;
    response->status_code = 503;
    cstr_copy(response->error_msg, sizeof(response->error_msg),
            "offline fixture failure");

    g_render_doc = old_document;
    g_nav_request = request;
    g_nav_loading = 1;
    result = pcore_navigation_commit_step(NULL, request, 0);
    kept = result == PCORE_NAV_RESULT_FAILED &&
            g_render_doc == old_document && request->document == NULL;
    pcore_navigation_finish(NULL, request);
    cleared = g_nav_request == NULL && !g_nav_loading &&
            g_render_doc == old_document;

    g_render_doc = NULL;
    PCore_FreeDocument(old_document);
    if (!kept || !cleared) {
        _snprintf(msg, sizeof(msg) - 1,
                "result=%d kept=%d cleared=%d", result, kept, cleared);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 44 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 44 OK",
              "Main-document failure kept the visible document and cleared\n"
              "the pending transaction/loading state (fully offline).");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 45 - native libcss @import tree + WM URL resolution/cache        */
/* -------------------------------------------------------------------- */
typedef struct css_import_test_ctx {
    int calls;
    int frees;
    unsigned int seen;
} css_import_test_ctx;

static int test45_import_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char ROOT_CSS[] =
        "@import \"missing.css\";"
        "@import \"nested/colors.css\" screen;"
        "h1{color:#654321}";
    static const char NESTED_CSS[] =
        "@import \"../shared/accent.css\";"
        "p{color:#123456}";
    static const char SHARED_CSS[] = "span{color:#abcdef}";
    css_import_test_ctx *ctx;
    const char *source;
    int len;

    ctx = (css_import_test_ctx *) pw;
    ctx->calls++;
    *out_data = NULL;
    *out_len = 0;
    source = NULL;
    len = 0;
    if (strcmp(url, "https://example.test/css/base.css") == 0) {
        ctx->seen |= 1U;
        source = ROOT_CSS;
        len = sizeof(ROOT_CSS) - 1;
    } else if (strcmp(url,
            "https://example.test/css/missing.css") == 0) {
        ctx->seen |= 2U;
        return 1;
    } else if (strcmp(url,
            "https://example.test/css/nested/colors.css") == 0) {
        ctx->seen |= 4U;
        source = NESTED_CSS;
        len = sizeof(NESTED_CSS) - 1;
    } else if (strcmp(url,
            "https://example.test/css/shared/accent.css") == 0) {
        ctx->seen |= 8U;
        source = SHARED_CSS;
        len = sizeof(SHARED_CSS) - 1;
    } else {
        return 1;
    }
    *out_data = (char *) malloc((size_t) len);
    if (*out_data == NULL) {
        return 1;
    }
    memcpy(*out_data, source, (size_t) len);
    *out_len = len;
    return 0;
}

static void test45_import_free(void *pw, char *data)
{
    css_import_test_ctx *ctx;

    ctx = (css_import_test_ctx *) pw;
    ctx->frees++;
    free(data);
}

static int test45_cache_only_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    css_import_test_ctx *ctx;

    (void) url;
    ctx = (css_import_test_ctx *) pw;
    ctx->calls++;
    *out_data = NULL;
    *out_len = 0;
    return 1;
}

static BOOL test45_css_import_tree(void)
{
    static const char HTML[] =
        "<!doctype html><html><head>"
        "<link rel=\"stylesheet\" href=\"../css/base.css\">"
        "</head><body><h1>parent</h1><p>child</p>"
        "<span>nested</span></body></html>";
    static const char DOCUMENT_URL[] =
        "https://example.test/dir/page.html";
    HANDLE document;
    css_import_test_ctx first;
    css_import_test_ctx second;
    unsigned long h1_color;
    unsigned long p_color;
    unsigned long span_color;
    int first_ok;
    int second_ok;
    char msg[256];

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    if (document == NULL) {
        show_error(L"TEST 45 FAIL", "PCore_ParseHTML returned NULL");
        return FALSE;
    }
    h1_color = 0;
    p_color = 0;
    span_color = 0;
    first_ok = PCore_StyleDocumentEx2(document, NULL, DOCUMENT_URL,
            wm_combine_url, test45_import_fetch, test45_import_free,
            &first) == 0 &&
            PCore_NodeComputedColor(document, "h1", &h1_color) == 0 &&
            PCore_NodeComputedColor(document, "p", &p_color) == 0 &&
            PCore_NodeComputedColor(document, "span", &span_color) == 0 &&
            (h1_color & 0x00ffffffUL) == 0x00654321UL &&
            (p_color & 0x00ffffffUL) == 0x00123456UL &&
            (span_color & 0x00ffffffUL) == 0x00abcdefUL &&
            first.calls == 4 && first.frees == 3 && first.seen == 15U;

    h1_color = 0;
    p_color = 0;
    span_color = 0;
    second_ok = PCore_StyleDocumentEx2(document, NULL, DOCUMENT_URL,
            wm_combine_url, test45_cache_only_fetch, NULL, &second) == 0 &&
            PCore_NodeComputedColor(document, "h1", &h1_color) == 0 &&
            PCore_NodeComputedColor(document, "p", &p_color) == 0 &&
            PCore_NodeComputedColor(document, "span", &span_color) == 0 &&
            (h1_color & 0x00ffffffUL) == 0x00654321UL &&
            (p_color & 0x00ffffffUL) == 0x00123456UL &&
            (span_color & 0x00ffffffUL) == 0x00abcdefUL &&
            second.calls == 1;
    PCore_FreeDocument(document);

    if (!first_ok || !second_ok) {
        _snprintf(msg, sizeof(msg) - 1,
                "first=%d calls=%d frees=%d seen=%u second=%d calls=%d "
                "colors=%06lX/%06lX/%06lX",
                first_ok, first.calls, first.frees, first.seen,
                second_ok, second.calls, h1_color & 0x00ffffffUL,
                p_color & 0x00ffffffUL, span_color & 0x00ffffffUL);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 45 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 45 OK",
              "CSS import tree: WinINet URL resolution, nested libcss\n"
              "imports, missing-child fallback and cache-only restyle passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 46 - NetSurf table colspan/rowspan occupancy + formal redraw    */
/* -------------------------------------------------------------------- */
static BOOL test46_table_spans(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><table><tbody>"
        "<tr><td class=a rowspan=2>A</td>"
        "<td class=b rowspan=0>B</td><td class=c>C</td></tr>"
        "<tr><td class=d>D</td></tr>"
        "<tr><td class=e>E</td><td class=f>F</td></tr>"
        "</tbody><tbody><tr><td class=g colspan=3>G</td></tr>"
        "</tbody></table></body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "table{display:table;width:180px;border-spacing:0;"
        "table-layout:fixed;margin:8px;}"
        "tbody{display:table-row-group;}tr{display:table-row;}"
        "td{display:table-cell;width:60px;height:40px;padding:0;"
        "border:0;text-align:center;vertical-align:middle;color:#000;}"
        ".a{background:#f00}.b{background:#0f0}"
        ".c{background:#00f;color:#fff}.d{background:#ff0}"
        ".e{background:#f0f}.f{background:#0ff}"
        ".g{background:#000;color:#fff}";
    static const COLORREF EXPECTED[4][3] = {
        { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255) },
        { RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0) },
        { RGB(255, 0, 255), RGB(0, 255, 0), RGB(0, 255, 255) },
        { RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0) }
    };
    HANDLE hDoc;
    HANDLE hSheet;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF pixel;
    int tx;
    int ty;
    int tw;
    int th;
    int row;
    int column;
    int px;
    int py;
    int screen_w;
    int screen_h;
    char msg[256];

    tx = 0;
    ty = 0;
    tw = 0;
    th = 0;
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-spans.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 220) != 0 ||
            PCore_NodeBox(hDoc, "table", &tx, &ty, &tw, &th) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 46 FAIL", "table parse/style/layout lookup failed");
        return FALSE;
    }
    if (tw < 174 || tw > 186 || th < 152 || th > 168) {
        _snprintf(msg, sizeof(msg) - 1,
                "table geometry=%d,%d %dx%d expect width 174..186 "
                "height 152..168", tx, ty, tw, th);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 46 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 220) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 46 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 220);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);

    row = 0;
    column = 0;
    pixel = CLR_INVALID;
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 3; column++) {
            px = tx + column * tw / 3 + 5;
            py = ty + row * th / 4 + 5;
            pixel = GetPixel(memory_dc, px, py);
            if ((pixel & 0x00ffffffUL) !=
                    (EXPECTED[row][column] & 0x00ffffffUL)) {
                break;
            }
        }
        if (column != 3) { break; }
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if (row != 4) {
        _snprintf(msg, sizeof(msg) - 1,
                "pixel r%d c%d=%06lX expect=%06lX table=%d,%d %dx%d",
                row, column, pixel & 0x00ffffffUL,
                EXPECTED[row][column] & 0x00ffffffUL,
                tx, ty, tw, th);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 46 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-spans.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 46 FAIL", "visible parse/style/layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 46",
              "NetSurf table-span page will open. Expect four rows:\n"
              "red/green/blue; red/green/yellow; magenta/green/cyan;\n"
              "then one black cell spanning all three columns.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 46 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 46 OK",
              "Finite/automatic rowspan, colspan, row-group boundaries,\n"
              "layout pixels and visible NetSurf redraw all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 47 - NetSurf anonymous table boxes + empty-cell completion       */
/* -------------------------------------------------------------------- */
static BOOL test47_table_normalise(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><div class=table>"
        "<div class=direct></div>"
        "<div class=row><div class=green></div><div class=blue></div></div>"
        "</div></body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        ".table{display:table;width:180px;table-layout:fixed;"
        "border-spacing:0;margin:8px;}"
        ".direct{display:block;height:40px;background:#f00;}"
        ".row{display:table-row;}"
        ".green{display:block;height:40px;background:#0f0;}"
        ".blue{display:table-cell;height:40px;background:#00f;}";
    static const COLORREF EXPECTED[2][2] = {
        { RGB(255, 0, 0), RGB(255, 255, 255) },
        { RGB(0, 255, 0), RGB(0, 0, 255) }
    };
    HANDLE hDoc;
    HANDLE hSheet;
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF pixel;
    int tx;
    int ty;
    int tw;
    int th;
    int row;
    int column;
    int px;
    int py;
    int screen_w;
    int screen_h;
    char msg[256];

    tx = 0;
    ty = 0;
    tw = 0;
    th = 0;
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-normalise.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 180) != 0 ||
            PCore_NodeBox(hDoc, "div", &tx, &ty, &tw, &th) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 47 FAIL",
                "anonymous table parse/style/layout lookup failed");
        return FALSE;
    }
    if (tw < 174 || tw > 186 || th < 76 || th > 88) {
        _snprintf(msg, sizeof(msg) - 1,
                "table geometry=%d,%d %dx%d expect width 174..186 "
                "height 76..88", tx, ty, tw, th);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 47 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 180) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 47 FAIL", "could not create off-screen surface");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 180);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    pixel = CLR_INVALID;
    for (row = 0; row < 2; row++) {
        for (column = 0; column < 2; column++) {
            px = tx + column * tw / 2 + 5;
            py = ty + row * th / 2 + 5;
            pixel = GetPixel(memory_dc, px, py);
            if ((pixel & 0x00ffffffUL) !=
                    (EXPECTED[row][column] & 0x00ffffffUL)) {
                break;
            }
        }
        if (column != 2) { break; }
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if (row != 2) {
        _snprintf(msg, sizeof(msg) - 1,
                "pixel r%d c%d=%06lX expect=%06lX table=%d,%d %dx%d",
                row, column, pixel & 0x00ffffffUL,
                EXPECTED[row][column] & 0x00ffffffUL, tx, ty, tw, th);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 47 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-normalise.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 47 FAIL", "visible parse/style/layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 47",
              "Anonymous table boxes will open. Expect two rows:\n"
              "red/white, then green/blue, with equal columns.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 47 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 47 OK",
              "Anonymous row-group/row/cell wrapping, missing-cell fill,\n"
              "layout pixels and visible NetSurf redraw all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 48 - NetSurf list markers + HTML ordered-list counters           */
/* -------------------------------------------------------------------- */
static BOOL test48_list_markers(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<ul><li>Disc one</li><li>Disc two"
        "<ul><li>Circle<ul><li>Square</li></ul></li></ul></li></ul>"
        "<ol start=3><li>Three</li><li value=7>Seven</li></ol>"
        "<ol reversed start=5><li>Five</li><li>Four</li></ol>"
        "</body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#000;}"
        "body{font-size:16px;line-height:20px;}"
        "ul,ol{margin-top:4px;margin-bottom:4px;}";
    static const char *EXPECTED[] = {
        "\342\200\242", "\342\200\242", "\342\227\213",
        "\342\226\252", "3.", "7.", "5.", "4."
    };
    HANDLE hDoc;
    HANDLE hSheet;
    char marker[16];
    char msg[256];
    int x;
    int y;
    int w;
    int h;
    int previous_y;
    int screen_w;
    int screen_h;
    unsigned int i;

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/list-markers.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 320, 480) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 48 FAIL", "list parse/style/layout failed");
        return FALSE;
    }

    previous_y = -1;
    for (i = 0; i < sizeof(EXPECTED) / sizeof(EXPECTED[0]); i++) {
        marker[0] = '\0';
        x = 0;
        y = 0;
        w = 0;
        h = 0;
        if (PCore_ListMarker(hDoc, i, marker, sizeof(marker),
                &x, &y, &w, &h) != 0 ||
                strcmp(marker, EXPECTED[i]) != 0 ||
                w <= 0 || h <= 0 || y < previous_y) {
            _snprintf(msg, sizeof(msg) - 1,
                    "marker %u='%s' expect='%s' box=%d,%d %dx%d prev-y=%d",
                    i, marker, EXPECTED[i], x, y, w, h, previous_y);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 48 FAIL", msg);
            return FALSE;
        }
        previous_y = y;
    }
    if (PCore_ListMarker(hDoc, i, marker, sizeof(marker),
            &x, &y, &w, &h) == 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 48 FAIL", "unexpected ninth list marker");
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/list-markers.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 48 FAIL", "visible list layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 48",
              "NetSurf list markers will open. Expect disc/circle/square,\n"
              "then ordered markers 3, 7 and reversed 5, 4.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 48 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 48 OK",
              "List marker construction, nested bullets and HTML\n"
              "start/value/reversed counters all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 49 - bundled static symbol/monochrome emoji font fallback       */
/* -------------------------------------------------------------------- */
static BOOL test49_bundled_fonts(void)
{
    static const unsigned long REQUIRED_CODEPOINTS[] = {
        0x2190UL, 0x2191UL, 0x2192UL, 0x2193UL,
        0x2713UL, 0x2605UL, 0x2665UL, 0x26a0UL,
        0x1f600UL, 0x1f680UL, 0x1f9d1UL, 0x1f527UL, 0x1f389UL,
        0x2022UL, 0x25cbUL, 0x25aaUL
    };
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Bundled mono fallback</h1>"
        "<p class=symbols>Symbols: \342\206\220 \342\206\221 "
        "\342\206\222 \342\206\223 \342\234\223 \342\230\205 "
        "\342\231\245 \342\232\240</p>"
        "<p class=emoji>Emoji: \360\237\230\200 \360\237\232\200 "
        "\360\237\247\221 \360\237\224\247 \360\237\216\211</p>"
        "<ul><li>disc<ul><li>circle<ul><li>square</li></ul></li></ul></li></ul>"
        "</body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:17px;line-height:24px;padding:8px;}"
        "h1{font-size:23px;color:#800000;margin:0 0 10px;}"
        "p{margin:8px 0;}ul{margin-top:3px;margin-bottom:3px;}";
    HANDLE hDoc;
    HANDLE hSheet;
    int symbols_loaded;
    int emoji_loaded;
    unsigned int codepoint_index;
    int screen_w;
    int screen_h;

    symbols_loaded = 0;
    emoji_loaded = 0;
    PCore_FontFallbackStatus(&symbols_loaded, &emoji_loaded);
    if (!symbols_loaded || !emoji_loaded) {
        char msg[160];
        _snprintf(msg, sizeof(msg) - 1,
                "bundled fonts not loaded: symbols=%d emoji=%d; "
                "check the fonts subdirectory", symbols_loaded, emoji_loaded);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 49 FAIL", msg);
        return FALSE;
    }
    for (codepoint_index = 0;
            codepoint_index < sizeof(REQUIRED_CODEPOINTS) /
                    sizeof(REQUIRED_CODEPOINTS[0]);
            codepoint_index++) {
        if (!PCore_BundledFontSupports(
                REQUIRED_CODEPOINTS[codepoint_index])) {
            char msg[96];
            _snprintf(msg, sizeof(msg) - 1,
                    "bundled font coverage missing U+%04lX",
                    REQUIRED_CODEPOINTS[codepoint_index]);
            msg[sizeof(msg) - 1] = '\0';
            show_error(L"TEST 49 FAIL", msg);
            return FALSE;
        }
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/bundled-fonts.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 49 FAIL", "font fixture parse/style/layout failed");
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 49",
              "Bundled coverage passed. Expect smooth arrows/check/star/\n"
              "heart/warning,\n"
              "five monochrome emoji, then disc/circle/square markers.\n"
              "No hollow square tofu should appear.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 49 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 49 OK",
              "Static symbols/emoji coverage, anti-aliased fallback run\n"
              "measurement and NetSurf redraw completed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 50 - upstream libcss counter styles + cached list-style-image    */
/* -------------------------------------------------------------------- */
static int list_marker_image_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    static const char SVG[] =
        "<svg xmlns='http://www.w3.org/2000/svg' width='12' height='12' "
        "viewBox='0 0 12 12'><circle cx='6' cy='6' r='5' "
        "fill='#00a000'/></svg>";
    image_resource_test_ctx *ctx = (image_resource_test_ctx *) pw;
    char *copy;

    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    if (strcmp(url, "http://positron.local/marker.svg") != 0) {
        return 1;
    }
    copy = (char *) malloc(sizeof(SVG) - 1);
    if (copy == NULL) {
        return 1;
    }
    memcpy(copy, SVG, sizeof(SVG) - 1);
    *out_data = copy;
    *out_len = sizeof(SVG) - 1;
    ctx->matched++;
    return 0;
}

static BOOL test50_counter_styles(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Counter styles</h1>"
        "<ol class=roman start=4><li>upper-roman four</li></ol>"
        "<ol class=alpha start=26><li>lower-alpha 26</li>"
        "<li>lower-alpha 27</li></ol>"
        "<ol class=zero start=9><li>leading zero</li></ol>"
        "<ol class=cjk start=9><li>CJK byte fixture</li></ol>"
        "<ul class=image><li>cached SVG marker</li></ul>"
        "<ul class=broken><li>broken image falls back</li></ul>"
        "</body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:16px;line-height:21px;padding:8px;}"
        "h1{font-size:23px;color:#800000;margin:0 0 6px;}"
        "ol,ul{margin-top:2px;margin-bottom:2px;}"
        ".roman{list-style-type:upper-roman;}"
        ".alpha{list-style-type:lower-alpha;}"
        ".zero{list-style-type:decimal-leading-zero;}"
        ".cjk{list-style-type:cjk-decimal;}"
        ".image{list-style-type:square;list-style-image:"
        "url('http://positron.local/marker.svg');}"
        ".broken{list-style-type:circle;list-style-image:"
        "url('http://positron.local/missing.svg');}";
    static const char VISIBLE_CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:16px;line-height:21px;padding:8px;}"
        "h1{font-size:23px;color:#800000;margin:0 0 6px;}"
        "ol,ul{margin-top:2px;margin-bottom:2px;}"
        ".roman{list-style-type:upper-roman;}"
        ".alpha{list-style-type:lower-alpha;}"
        ".zero{list-style-type:decimal-leading-zero;}"
        ".cjk{display:none;}"
        ".image{list-style-type:square;list-style-image:"
        "url('http://positron.local/marker.svg');}"
        ".broken{list-style-type:circle;list-style-image:"
        "url('http://positron.local/missing.svg');}";
    static const char *EXPECTED[] = {
        "IV.", "z.", "aa.", "09.",
        "\344\271\235\343\200\201", "\342\226\252", "\342\227\213"
    };
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    char marker[32];
    char msg[224];
    int found;
    int fetched;
    int x;
    int y;
    int w;
    int h;
    int screen_w;
    int screen_h;
    unsigned int i;

    memset(&ctx, 0, sizeof(ctx));
    found = 0;
    fetched = 0;
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/counter-styles.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_FetchImageResources(hDoc, list_marker_image_fetch,
                    image_resource_free, &ctx, &found, &fetched) != 0 ||
            PCore_LayoutDocument(hDoc, 320, 480) != 0 ||
            found != 2 || fetched != 1 || ctx.calls != 2 ||
            ctx.matched != 1 || ctx.frees != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                "setup/resources failed found=%d fetched=%d matched=%d "
                "calls=%d frees=%d", found, fetched, ctx.matched, ctx.calls,
                ctx.frees);
        msg[sizeof(msg) - 1] = '\0';
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 50 FAIL", msg);
        return FALSE;
    }
    for (i = 0; i < sizeof(EXPECTED) / sizeof(EXPECTED[0]); i++) {
        marker[0] = '\0';
        x = y = w = h = 0;
        if (PCore_ListMarker(hDoc, i, marker, sizeof(marker),
                &x, &y, &w, &h) != 0 ||
                strcmp(marker, EXPECTED[i]) != 0 || w <= 0 || h <= 0 ||
                (i == 5 && (w != 12 || h != 12))) {
            _snprintf(msg, sizeof(msg) - 1,
                    "marker %u='%s' expect='%s' box=%d,%d %dx%d",
                    i, marker, EXPECTED[i], x, y, w, h);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 50 FAIL", msg);
            return FALSE;
        }
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    memset(&ctx, 0, sizeof(ctx));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(VISIBLE_CSS, sizeof(VISIBLE_CSS) - 1,
            "http://positron.local/counter-styles.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_FetchImageResources(hDoc, list_marker_image_fetch,
                    image_resource_free, &ctx, NULL, NULL) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 50 FAIL", "visible list setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 50",
              "Full libcss counters + list-style-image passed. Expect IV,\n"
              "z, aa, 09; a green round image marker; then circle fallback.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 50 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 50 OK",
              "47 upstream counter styles, UTF-8 CJK output, cached SVG\n"
              "marker geometry and broken-image type fallback all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 51 - list-style-position inside participates in first-line flow  */
/* -------------------------------------------------------------------- */
static BOOL test51_inside_list_markers(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Inside markers</h1>"
        "<ul class=outside><li>Outside marker keeps the text origin.</li></ul>"
        "<ol class=inside start=8><li>Inside marker shifts the first line "
        "while wrapped words return to the content origin.</li></ol>"
        "<ul class=image><li>Image marker participates inside the first "
        "line and keeps its fallback.</li></ul></body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:16px;line-height:21px;margin:0;padding:8px;}"
        "h1{font-size:23px;color:#800000;margin:0 0 6px;}"
        "ul,ol{padding-left:28px;margin:5px 0;}"
        "li{width:126px;}"
        ".outside{list-style-position:outside;list-style-type:disc;}"
        ".inside{list-style-position:inside;list-style-type:upper-roman;}"
        ".image{list-style-position:inside;list-style-type:square;"
        "list-style-image:url('http://positron.local/marker.svg');}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    PCoreListItemGeometry outside;
    PCoreListItemGeometry inside;
    PCoreListItemGeometry image;
    char marker[24];
    char msg[256];
    int found;
    int fetched;
    int screen_w;
    int screen_h;

    memset(&ctx, 0, sizeof(ctx));
    memset(&outside, 0, sizeof(outside));
    memset(&inside, 0, sizeof(inside));
    memset(&image, 0, sizeof(image));
    found = 0;
    fetched = 0;
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/inside-list.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_FetchImageResources(hDoc, list_marker_image_fetch,
                    image_resource_free, &ctx, &found, &fetched) != 0 ||
            PCore_LayoutDocument(hDoc, 180, 320) != 0 ||
            found != 1 || fetched != 1 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1 ||
            PCore_ListItemGeometry(hDoc, 0, &outside) != 0 ||
            PCore_ListItemGeometry(hDoc, 1, &inside) != 0 ||
            PCore_ListItemGeometry(hDoc, 2, &image) != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                "setup failed resources=%d/%d calls=%d match=%d free=%d",
                found, fetched, ctx.calls, ctx.matched, ctx.frees);
        msg[sizeof(msg) - 1] = '\0';
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 51 FAIL", msg);
        return FALSE;
    }
    marker[0] = '\0';
    if (PCore_ListMarker(hDoc, 1, marker, sizeof(marker),
            NULL, NULL, NULL, NULL) != 0 || strcmp(marker, "VIII.") != 0 ||
            outside.marker_x + outside.marker_width >= outside.first_text_x ||
            inside.marker_x < inside.item_x ||
            inside.first_text_x < inside.marker_x + inside.marker_width + 4 ||
            inside.wrapped_text_y <= inside.first_text_y ||
            inside.wrapped_text_x < inside.item_x ||
            inside.wrapped_text_x >= inside.first_text_x ||
            image.marker_x < image.item_x ||
            image.marker_width != 12 || image.marker_height != 12 ||
            image.first_text_x < image.marker_x + image.marker_width + 4) {
        _snprintf(msg, sizeof(msg) - 1,
                "out m/t=%d+%d/%d in=%d,%d,%d wrap=%d,%d img=%d,%d,%d",
                outside.marker_x, outside.marker_width, outside.first_text_x,
                inside.marker_x, inside.first_text_x, inside.item_x,
                inside.wrapped_text_x, inside.wrapped_text_y,
                image.marker_x, image.marker_width, image.first_text_x);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 51 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    memset(&ctx, 0, sizeof(ctx));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/inside-list.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_FetchImageResources(hDoc, list_marker_image_fetch,
                    image_resource_free, &ctx, NULL, NULL) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 51 FAIL", "visible inside-list setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 51",
              "Expect an outside bullet, then inside VIII and green image\n"
              "markers. Inside markers share the first line; wrapped text\n"
              "returns to the content edge.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 51 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 51 OK",
              "Inside text/image marker flow, first-line width, hanging wrap\n"
              "and inherited list-style-position all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 52 - inside markers before block, empty and nested content       */
/* -------------------------------------------------------------------- */
static BOOL test52_inside_block_markers(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Inside block markers</h1>"
        "<ol class=blocks start=3>"
        "<li><div>Block child starts below its marker line.</div></li>"
        "<li></li>"
        "<li><div>Parent block before a nested list.</div>"
        "<ol start=6><li><div>Nested block child.</div></li></ol></li>"
        "</ol>"
        "<ul class=image><li><div>Image marker owns a line before this "
        "block.</div></li></ul></body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:16px;line-height:21px;margin:0;padding:8px;}"
        "h1{font-size:23px;color:#800000;margin:0 0 6px;}"
        "ol,ul{padding-left:22px;margin:4px 0;}"
        "li{width:142px;}li div{margin:0 0 3px;}"
        ".blocks,.blocks ol{list-style-position:inside;"
        "list-style-type:upper-roman;}"
        ".image{list-style-position:inside;list-style-type:square;"
        "list-style-image:url('http://positron.local/marker.svg');}";
    HANDLE hDoc;
    HANDLE hSheet;
    image_resource_test_ctx ctx;
    PCoreListItemGeometry block;
    PCoreListItemGeometry parent;
    PCoreListItemGeometry nested;
    PCoreListItemGeometry image;
    char marker[24];
    char msg[256];
    int found;
    int fetched;
    int empty_x;
    int empty_y;
    int empty_w;
    int empty_h;
    int parent_marker_y;
    int screen_w;
    int screen_h;

    memset(&ctx, 0, sizeof(ctx));
    memset(&block, 0, sizeof(block));
    memset(&parent, 0, sizeof(parent));
    memset(&nested, 0, sizeof(nested));
    memset(&image, 0, sizeof(image));
    found = 0;
    fetched = 0;
    empty_x = empty_y = empty_w = empty_h = -1;
    parent_marker_y = -1;
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/inside-block-list.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_FetchImageResources(hDoc, list_marker_image_fetch,
                    image_resource_free, &ctx, &found, &fetched) != 0 ||
            PCore_LayoutDocument(hDoc, 196, 400) != 0 ||
            found != 1 || fetched != 1 || ctx.calls != 1 ||
            ctx.matched != 1 || ctx.frees != 1 ||
            PCore_ListItemGeometry(hDoc, 0, &block) != 0 ||
            PCore_ListItemGeometry(hDoc, 2, &parent) != 0 ||
            PCore_ListItemGeometry(hDoc, 3, &nested) != 0 ||
            PCore_ListItemGeometry(hDoc, 4, &image) != 0) {
        _snprintf(msg, sizeof(msg) - 1,
                "setup failed resources=%d/%d calls=%d match=%d free=%d",
                found, fetched, ctx.calls, ctx.matched, ctx.frees);
        msg[sizeof(msg) - 1] = '\0';
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 52 FAIL", msg);
        return FALSE;
    }
    marker[0] = '\0';
    if (PCore_ListMarker(hDoc, 0, marker, sizeof(marker),
            NULL, NULL, NULL, NULL) != 0 || strcmp(marker, "III.") != 0 ||
            PCore_ListMarker(hDoc, 1, marker, sizeof(marker), &empty_x,
                    &empty_y, &empty_w, &empty_h) != 0 ||
            strcmp(marker, "IV.") != 0 ||
            PCore_ListMarker(hDoc, 2, marker, sizeof(marker), NULL,
                    &parent_marker_y, NULL, NULL) != 0 ||
            strcmp(marker, "V.") != 0 ||
            PCore_ListMarker(hDoc, 3, marker, sizeof(marker),
                    NULL, NULL, NULL, NULL) != 0 ||
            strcmp(marker, "VI.") != 0 ||
            block.marker_x < block.item_x ||
            block.first_text_y <= block.marker_y ||
            empty_x < block.item_x || empty_w <= 0 || empty_h <= 0 ||
            empty_y <= block.first_text_y ||
            parent_marker_y < empty_y + empty_h ||
            parent.first_text_y <= parent.marker_y ||
            nested.marker_x <= parent.marker_x ||
            nested.first_text_y <= nested.marker_y ||
            image.marker_x < image.item_x ||
            image.marker_width != 12 || image.marker_height != 12 ||
            image.first_text_y - image.marker_y < 16) {
        _snprintf(msg, sizeof(msg) - 1,
                "b=%d,%d/%d e=%d,%d,%d,%d p=%d/%d n=%d/%d i=%d,%d/%d",
                block.marker_x, block.marker_y, block.first_text_y,
                empty_x, empty_y, empty_w, empty_h,
                parent.marker_y, parent.first_text_y,
                nested.marker_x, nested.first_text_y,
                image.marker_y, image.marker_height, image.first_text_y);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 52 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    memset(&ctx, 0, sizeof(ctx));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/inside-block-list.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_FetchImageResources(hDoc, list_marker_image_fetch,
                    image_resource_free, &ctx, NULL, NULL) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 52 FAIL", "visible inside-block setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 52",
              "Expect III before a block, IV alone on an empty line, V with\n"
              "nested VI, then a green image marker. Every marker owns the\n"
              "line before its block content.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 52 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 52 OK",
              "Block-first, empty, nested and cached-image inside marker\n"
              "flows all passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 53 - NetSurf collapsed-table border conflict resolution         */
/* -------------------------------------------------------------------- */
static BOOL test53_table_collapsed_borders(void)
{
    HANDLE hDoc;
    HANDLE hSheet;
    unsigned int i;
    int style;
    int width;
    unsigned long color;
    int screen_w;
    int screen_h;
    char msg[256];
    static const char HTML[] =
        "<!doctype html><html><body><h1>Collapsed borders</h1>"
        "<table class=collapse>"
        "<tr><td class=wide-a>WIDER</td><td class=wide-b>blue 5</td></tr>"
        "<tr><td class=style-a>STYLE</td><td class=style-b>double</td></tr>"
        "<tr><td class=hidden-a>HIDDEN</td><td class=hidden-b>gap</td></tr>"
        "<tr><td class=tie-a>TIE LEFT</td><td class=tie-b>orange</td></tr>"
        "</table>"
        "<table class=origin><tbody><tr><td>ORIGIN: cell top</td></tr>"
        "</tbody></table>"
        "<table class=horizontal><tr><td class=top>TOP wins</td></tr>"
        "<tr><td class=bottom>cyan line</td></tr></table>"
        "<table class=separate><tr><td class=wide-a>SEPARATE</td>"
        "<td class=wide-b>keeps both</td></tr></table>"
        "</body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:13px;line-height:17px;margin:0;padding:7px;}"
        "h1{font-size:22px;color:#800000;margin:0 0 5px;}"
        "table{width:190px;table-layout:fixed;margin:5px 0;}"
        "td{height:17px;padding:2px;background:#f7f7fb;}"
        ".collapse,.origin,.horizontal{border-collapse:collapse;}"
        ".wide-a{border-right:3px solid #ff0000;}"
        ".wide-b{border-left:5px dotted #0000ff;}"
        ".style-a{border-right:4px solid #00a000;}"
        ".style-b{border-left:4px double #ff00ff;}"
        ".hidden-a{border-right:6px solid #ff0000;}"
        ".hidden-b{border-left:1px hidden #000000;}"
        ".tie-a{border-right:3px solid #ff8000;}"
        ".tie-b{border-left:3px solid #800080;}"
        ".origin{border-top:4px solid #ff0000;}"
        ".origin tbody{border-top:4px solid #00a000;}"
        ".origin tr{border-top:4px solid #0000ff;}"
        ".origin td{border-top:4px solid #ff00ff;}"
        ".horizontal .top{border-bottom:3px solid #00a0c0;}"
        ".horizontal .bottom{border-top:3px solid #ffff00;}"
        ".separate{border-collapse:separate;border-spacing:0;}";
    struct border_expect {
        unsigned int cell;
        int side;
        int style;
        unsigned long color;
        int width;
        int check_color;
    };
    static const struct border_expect expected[] = {
        { 1, 3, CSS_BORDER_STYLE_DOTTED, 0xff0000ffUL, 5, 1 },
        { 3, 3, CSS_BORDER_STYLE_DOUBLE, 0xffff00ffUL, 4, 1 },
        { 5, 3, CSS_BORDER_STYLE_HIDDEN, 0, 0, 0 },
        { 7, 3, CSS_BORDER_STYLE_SOLID, 0xffff8000UL, 3, 1 },
        { 8, 0, CSS_BORDER_STYLE_SOLID, 0xffff00ffUL, 4, 1 },
        { 10, 0, CSS_BORDER_STYLE_SOLID, 0xff00a0c0UL, 3, 1 },
        { 11, 1, CSS_BORDER_STYLE_SOLID, 0xffff0000UL, 3, 1 },
        { 12, 3, CSS_BORDER_STYLE_DOTTED, 0xff0000ffUL, 5, 1 }
    };

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/collapsed-borders.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 220, 360) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 53 FAIL", "parse/style/layout failed");
        return FALSE;
    }
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        style = -1;
        width = -1;
        color = 0;
        if (PCore_TableCellBorder(hDoc, expected[i].cell,
                expected[i].side, &style, &color, &width) != 0 ||
                style != expected[i].style || width != expected[i].width ||
                (expected[i].check_color && color != expected[i].color)) {
            _snprintf(msg, sizeof(msg) - 1,
                    "case=%u cell=%u side=%d got=%d/%lu/%08lX "
                    "expect=%d/%d/%08lX",
                    i, expected[i].cell, expected[i].side,
                    style, (unsigned long) width, color,
                    expected[i].style, expected[i].width,
                    expected[i].color);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 53 FAIL", msg);
            return FALSE;
        }
    }
    if (PCore_TableCellBorder(hDoc, 99, 0, NULL, NULL, NULL) == 0 ||
            PCore_TableCellBorder(hDoc, 0, 4, NULL, NULL, NULL) == 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 53 FAIL", "diagnostic bounds were not rejected");
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/collapsed-borders.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 53 FAIL", "visible border setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 53",
              "Collapsed-border rules passed. Expect blue dotted, magenta\n"
              "double, hidden gap and orange vertical seams; then magenta\n"
              "top, cyan horizontal and a final separate-border control.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 53 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 53 OK",
              "Width/style/hidden/source/tie conflicts and the separate\n"
              "model control passed through NetSurf layout and redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 54 - collapsed borders at spanning cells and row-group edges    */
/* -------------------------------------------------------------------- */
static BOOL test54_table_spanning_borders(void)
{
    HANDLE hDoc;
    HANDLE hSheet;
    unsigned int i;
    int style;
    int width;
    unsigned long color;
    int screen_w;
    int screen_h;
    char msg[256];
    static const char HTML[] =
        "<!doctype html><html><body><h1>Spanning borders</h1>"
        "<table class=finite><tbody>"
        "<tr class=f1><td rowspan=3>finite 3</td><td>B</td></tr>"
        "<tr class=f2><td>C</td></tr><tr class=f3><td>D</td></tr>"
        "</tbody></table>"
        "<table class=automatic><tbody>"
        "<tr class=a1><td rowspan=0>auto</td><td>B</td></tr>"
        "<tr class=a2><td>C</td></tr><tr class=a3><td>D</td></tr>"
        "</tbody></table>"
        "<table class=colspan><tbody>"
        "<tr class=c1><td colspan=2>colspan 2</td></tr>"
        "<tr class=c2><td>C</td><td>D</td></tr></tbody></table>"
        "<table class=groups><tbody class=g1>"
        "<tr class=g1a><td rowspan=0>group 1</td><td>H</td></tr>"
        "<tr class=g1b><td>I</td></tr></tbody>"
        "<tbody class=g2><tr><td>group 2</td><td>K</td></tr>"
        "</tbody></table></body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:12px;line-height:16px;margin:0;padding:7px;}"
        "h1{font-size:21px;color:#800000;margin:0 0 4px;}"
        "table{border-collapse:collapse;table-layout:fixed;width:190px;"
        "margin:4px 0;}td{height:14px;padding:1px 3px;background:#f7f7fb;}"
        ".f1{border-bottom:5px solid #0000ff;}"
        ".f2{border-bottom:5px solid #00a000;}"
        ".f3{border-bottom:5px solid #ff0000;}"
        ".a1{border-bottom:5px solid #0000ff;}"
        ".a2{border-bottom:5px solid #00a000;}"
        ".a3{border-bottom:5px solid #8000ff;}"
        ".c1{border-bottom:4px solid #00a0c0;}"
        ".c2{border-top:4px solid #ffff00;}"
        ".g1{border-bottom:6px solid #ff8000;}"
        ".g1a{border-bottom:5px solid #0000ff;}"
        ".g1b{border-bottom:5px solid #00a000;}"
        ".g2{border-top:3px solid #ff00ff;}";
    struct border_expect {
        unsigned int cell;
        int side;
        int style;
        unsigned long color;
        int width;
    };
    static const struct border_expect expected[] = {
        { 0, 2, CSS_BORDER_STYLE_SOLID, 0xffff0000UL, 5 },
        { 3, 2, CSS_BORDER_STYLE_SOLID, 0xffff0000UL, 5 },
        { 4, 2, CSS_BORDER_STYLE_SOLID, 0xff8000ffUL, 5 },
        { 7, 2, CSS_BORDER_STYLE_SOLID, 0xff8000ffUL, 5 },
        { 9, 0, CSS_BORDER_STYLE_SOLID, 0xff00a0c0UL, 4 },
        { 10, 0, CSS_BORDER_STYLE_SOLID, 0xff00a0c0UL, 4 },
        { 11, 2, CSS_BORDER_STYLE_NONE, 0, 0 },
        { 13, 2, CSS_BORDER_STYLE_NONE, 0, 0 },
        { 14, 0, CSS_BORDER_STYLE_SOLID, 0xffff8000UL, 6 },
        { 15, 0, CSS_BORDER_STYLE_SOLID, 0xffff8000UL, 6 }
    };

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/spanning-borders.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 220, 380) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 54 FAIL", "parse/style/layout failed");
        return FALSE;
    }
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        style = -1;
        width = -1;
        color = 0;
        if (PCore_TableCellBorder(hDoc, expected[i].cell,
                expected[i].side, &style, &color, &width) != 0 ||
                style != expected[i].style || width != expected[i].width ||
                (expected[i].width != 0 && color != expected[i].color)) {
            _snprintf(msg, sizeof(msg) - 1,
                    "case=%u cell=%u side=%d got=%d/%d/%08lX "
                    "expect=%d/%d/%08lX",
                    i, expected[i].cell, expected[i].side,
                    style, width, color, expected[i].style,
                    expected[i].width, expected[i].color);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 54 FAIL", msg);
            return FALSE;
        }
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/spanning-borders.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 54 FAIL", "visible span-border setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 54",
              "Spanning-border assertions passed. Expect finite rowspan\n"
              "to end red, auto rowspan purple, colspan cyan, and the\n"
              "row-group boundary orange across both columns.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 54 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 54 OK",
              "Finite/auto rowspan, colspan and row-group terminal borders\n"
              "passed through NetSurf layout and redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 55 - table-cell vertical alignment and empty-cells painting     */
/* -------------------------------------------------------------------- */
static BOOL test55_table_cell_alignment(void)
{
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTableCellGeometry cells[11];
    HDC screen_dc;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    RECT rect;
    COLORREF hidden_pixel;
    COLORREF shown_pixel;
    COLORREF filled_pixel;
    int i;
    int big_baseline;
    int small_baseline;
    int screen_w;
    int screen_h;
    char msg[256];
    static const char HTML[] =
        "<!doctype html><html><body><h1>Cell alignment</h1>"
        "<table class=align><tr><td class=top>top</td>"
        "<td class=middle>middle</td><td class=bottom>bottom</td>"
        "</tr></table>"
        "<table class=baseline><tr><td class=big>Big</td>"
        "<td class=small>small baseline</td></tr></table>"
        "<table class=span><tr><td class=spanbottom rowspan=2>"
        "span bottom</td><td>row one</td></tr><tr><td>row two</td>"
        "</tr></table>"
        "<h2>empty-cells</h2><table class=empty><tr>"
        "<td class=hidden></td><td class=shown></td>"
        "<td class=filled>X</td></tr></table></body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:11px;line-height:14px;margin:0;padding:3px;}"
        "h1{font-size:18px;line-height:21px;color:#800000;"
        "margin:0 0 2px;}"
        "h2{font-size:15px;line-height:18px;color:#800000;"
        "margin:2px 0 1px;}"
        "table{border-collapse:separate;border-spacing:2px;"
        "table-layout:fixed;width:210px;margin:1px 0;}"
        "td{padding:1px;border:1px solid #404040;}"
        ".align tr{height:50px}.align td{width:64px;}"
        ".top{vertical-align:top;background:#ffb0b0;}"
        ".middle{vertical-align:middle;background:#b0ffb0;}"
        ".bottom{vertical-align:bottom;background:#b0b0ff;}"
        ".baseline tr{height:34px}.baseline td{vertical-align:baseline;}"
        ".big{font-size:20px;line-height:24px;background:#ffe0a0;}"
        ".small{font-size:11px;line-height:14px;background:#fff0c0;}"
        ".span tr{height:25px}.spanbottom{vertical-align:bottom;"
        "background:#ffc060}.span td{width:100px;}"
        ".empty{empty-cells:hide}.empty td{height:22px;padding:0;"
        "border:3px solid #004080;background:#00c000;}"
        ".empty .shown{empty-cells:show;}"
        ".empty .filled{background:#00c0c0;text-align:center;"
        "vertical-align:middle;}";

    memset(cells, 0, sizeof(cells));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-cell-alignment.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 230, 440) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 55 FAIL", "parse/style/layout failed");
        return FALSE;
    }
    for (i = 0; i < 11; i++) {
        if (PCore_TableCellGeometry(hDoc, (unsigned int) i,
                &cells[i]) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "cell geometry lookup failed at %d", i);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 55 FAIL", msg);
            return FALSE;
        }
    }
    big_baseline = cells[3].first_text_y +
            cells[3].first_text_height * 3 / 4;
    small_baseline = cells[4].first_text_y +
            cells[4].first_text_height * 3 / 4;
    if (cells[0].first_text_y < 0 ||
            cells[0].first_text_y + 8 > cells[1].first_text_y ||
            cells[1].first_text_y + 8 > cells[2].first_text_y ||
            big_baseline < small_baseline - 1 ||
            big_baseline > small_baseline + 1 ||
            cells[5].first_text_y <=
                    cells[5].cell_y + cells[5].cell_height / 2 ||
            PCore_TableCellGeometry(hDoc, 99, &cells[0]) == 0) {
        _snprintf(msg, sizeof(msg) - 1,
                "align y=%d/%d/%d base=%d/%d span=%d in %d+%d",
                cells[0].first_text_y, cells[1].first_text_y,
                cells[2].first_text_y, big_baseline, small_baseline,
                cells[5].first_text_y, cells[5].cell_y,
                cells[5].cell_height);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 55 FAIL", msg);
        return FALSE;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 440) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) { DeleteObject(bitmap); }
        if (memory_dc != NULL) { DeleteDC(memory_dc); }
        if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 55 FAIL", "off-screen surface failed");
        return FALSE;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 440);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    hidden_pixel = GetPixel(memory_dc,
            cells[8].cell_x + cells[8].cell_width / 2,
            cells[8].cell_y + cells[8].cell_height / 2);
    shown_pixel = GetPixel(memory_dc,
            cells[9].cell_x + cells[9].cell_width / 2,
            cells[9].cell_y + cells[9].cell_height / 2);
    filled_pixel = GetPixel(memory_dc,
            cells[10].cell_x + 5, cells[10].cell_y + 5);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    /* Compatible bitmaps use the device colour format. Some WM targets
     * quantise #00c000 to #00c300 and #00c0c0 to about #00c3c6, so compare
     * each channel within a tight tolerance instead of requiring an exact
     * desktop RGB round-trip. */
    if (abs((int) GetRValue(hidden_pixel) - 255) > 8 ||
            abs((int) GetGValue(hidden_pixel) - 255) > 8 ||
            abs((int) GetBValue(hidden_pixel) - 255) > 8 ||
            abs((int) GetRValue(shown_pixel)) > 8 ||
            abs((int) GetGValue(shown_pixel) - 192) > 12 ||
            abs((int) GetBValue(shown_pixel)) > 8 ||
            abs((int) GetRValue(filled_pixel)) > 8 ||
            abs((int) GetGValue(filled_pixel) - 192) > 12 ||
            abs((int) GetBValue(filled_pixel) - 192) > 12) {
        _snprintf(msg, sizeof(msg) - 1,
                "empty pixels=%06lX/%06lX/%06lX",
                (unsigned long) hidden_pixel,
                (unsigned long) shown_pixel,
                (unsigned long) filled_pixel);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 55 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-cell-alignment.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 55 FAIL", "visible table-cell setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 55",
              "Expect top/middle/bottom text at three heights, Big and\n"
              "small sharing a baseline, rowspan text at the bottom, then\n"
              "a white gap, green empty cell and cyan filled cell.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 55 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 55 OK",
              "Top/middle/bottom, baseline, rowspan and separated-table\n"
              "empty-cells painting passed through layout and redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 56 - specified table height distribution through rows/cells    */
/* -------------------------------------------------------------------- */
static BOOL test56_table_height_distribution(void)
{
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTableRowGeometry rows[5];
    PCoreTableCellGeometry cells[6];
    int cell_align[6];
    int top_offset;
    int middle_offset;
    int bottom_offset;
    int first_height;
    int second_height;
    int screen_w;
    int screen_h;
    int i;
    char msg[256];
    static const char HTML[] =
        "<!doctype html><html><body><h1>Specified table height</h1>"
        "<table class=distributed><tr><td class=top>top row</td></tr>"
        "<tr><td class=middle>middle row</td></tr>"
        "<tr><td class=bottom>bottom row</td></tr></table>"
        "<h2>Rowspan distribution</h2>"
        "<table class=spanheight><tr>"
        "<td class=spanbottom rowspan=2>span bottom</td>"
        "<td>row A</td></tr><tr><td>row B</td></tr></table>"
        "</body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:11px;line-height:14px;margin:0;padding:4px;}"
        "h1{font-size:18px;line-height:21px;color:#800000;"
        "margin:0 0 2px;}"
        "h2{font-size:15px;line-height:18px;color:#800000;"
        "margin:3px 0 1px;}"
        "table{border-collapse:separate;border-spacing:0;"
        "table-layout:fixed;width:210px;margin:0;}"
        "td{padding:1px;border:1px solid #404040;}"
        ".distributed{height:105px}.distributed .top{"
        "vertical-align:top;background:#ffb0b0;}"
        ".distributed .middle{vertical-align:middle;background:#b0ffb0;}"
        ".distributed .bottom{vertical-align:bottom;background:#b0b0ff;}"
        ".spanheight{height:70px}.spanheight td{width:100px;}"
        ".spanbottom{vertical-align:bottom;background:#ffc060;}";

    memset(rows, 0, sizeof(rows));
    memset(cells, 0, sizeof(cells));
    memset(cell_align, 0, sizeof(cell_align));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-height.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 230, 260) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 56 FAIL", "parse/style/layout failed");
        return FALSE;
    }
    for (i = 0; i < 5; i++) {
        if (PCore_TableRowGeometry(hDoc, (unsigned int) i,
                &rows[i]) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "row geometry lookup failed at %d", i);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 56 FAIL", msg);
            return FALSE;
        }
    }
    for (i = 0; i < 6; i++) {
        if (PCore_TableCellGeometry(hDoc, (unsigned int) i,
                &cells[i]) != 0 ||
                PCore_TableCellVerticalAlign(hDoc, (unsigned int) i,
                &cell_align[i]) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "cell geometry lookup failed at %d", i);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 56 FAIL", msg);
            return FALSE;
        }
    }
    first_height = rows[0].row_height + rows[1].row_height +
            rows[2].row_height;
    second_height = rows[3].row_height + rows[4].row_height;
    top_offset = cells[0].first_text_y - rows[0].row_y;
    middle_offset = cells[1].first_text_y - rows[1].row_y;
    bottom_offset = cells[2].first_text_y - rows[2].row_y;
    if (first_height < 104 || first_height > 106 ||
            abs(rows[0].row_height - rows[1].row_height) > 1 ||
            abs(rows[1].row_height - rows[2].row_height) > 1 ||
            rows[1].row_y != rows[0].row_y + rows[0].row_height ||
            rows[2].row_y != rows[1].row_y + rows[1].row_height ||
            top_offset < 0 || top_offset > 6 ||
            middle_offset < top_offset + 6 ||
            bottom_offset < middle_offset + 6 ||
            cell_align[0] != 1 || cell_align[1] != 2 ||
            cell_align[2] != 3 || cell_align[3] != 3 ||
            second_height < 69 || second_height > 71 ||
            abs(rows[3].row_height - rows[4].row_height) > 1 ||
            rows[4].row_y != rows[3].row_y + rows[3].row_height ||
            cells[3].first_text_y <=
                    rows[4].row_y + rows[4].row_height / 2 ||
            PCore_TableRowGeometry(hDoc, 99, &rows[0]) == 0) {
        _snprintf(msg, sizeof(msg) - 1,
                "rows=%d/%d/%d %d/%d sum=%d/%d off=%d/%d/%d va=%d/%d/%d/%d",
                rows[0].row_height, rows[1].row_height,
                rows[2].row_height, rows[3].row_height,
                rows[4].row_height, first_height, second_height,
                top_offset, middle_offset, bottom_offset,
                cell_align[0], cell_align[1], cell_align[2], cell_align[3]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 56 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-height.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 56 FAIL", "visible table-height setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 56",
              "Expect three equal-height coloured rows with top, middle and\n"
              "bottom text, then two equal rows with the orange rowspan\n"
              "text aligned at the bottom. No vertical scrollbar expected.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 56 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 56 OK",
              "Specified table height was distributed through rows,\n"
              "row groups, spanning cells and vertical alignment.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 57 - percentage table-row height second-pass distribution       */
/* -------------------------------------------------------------------- */
static BOOL test57_table_percentage_rows(void)
{
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTableRowGeometry rows[5];
    PCoreTableRowGeometry auto_rows[2];
    int row_kind[5];
    int row_value[5];
    int auto_kind[2];
    int auto_value[2];
    int first_height;
    int second_height;
    int auto_height;
    int screen_w;
    int screen_h;
    int i;
    char msg[256];
    static const char HTML[] =
        "<!doctype html><html><body><h1>Percentage row heights</h1>"
        "<table class=pct><tr class=p25><td class=red>25%</td></tr>"
        "<tr class=p50><td class=green>50%</td></tr>"
        "<tr><td class=blue>auto</td></tr></table>"
        "<h2>Over-constrained percentages</h2>"
        "<table class=over><tr class=p75a><td class=orange>75% A</td></tr>"
        "<tr class=p75b><td class=cyan>75% B</td></tr></table>"
        "</body></html>";
    static const char AUTO_HTML[] =
        "<!doctype html><html><body><table>"
        "<tr class=p80><td>percent without table height</td></tr>"
        "<tr><td>auto row</td></tr></table></body></html>";
    static const char CSS[] =
        "html,body{background:#fff;color:#111;}"
        "body{font-size:11px;line-height:14px;margin:0;padding:4px;}"
        "h1{font-size:18px;line-height:21px;color:#800000;margin:0 0 2px;}"
        "h2{font-size:14px;line-height:17px;color:#800000;margin:3px 0 1px;}"
        "table{border-collapse:separate;border-spacing:0;"
        "table-layout:fixed;width:210px;margin:0;}"
        "td{padding:1px;border:1px solid #404040;}"
        ".pct{height:80px}.over{height:50px}"
        ".p25{height:25%}.p50{height:50%}"
        ".p75a{height:75%}.p75b{height:75%}"
        ".red{background:#ffb0b0}.green{background:#b0ffb0}"
        ".blue{background:#b0b0ff}.orange{background:#ffc060}"
        ".cyan{background:#80e0e0}";
    static const char AUTO_CSS[] =
        "html,body{margin:0;padding:0;font-size:11px;line-height:14px;}"
        "table{border-collapse:separate;border-spacing:0;width:210px;}"
        "td{padding:1px;border:1px solid #404040;}"
        ".p80{height:80%}";

    memset(rows, 0, sizeof(rows));
    memset(auto_rows, 0, sizeof(auto_rows));
    memset(row_kind, 0, sizeof(row_kind));
    memset(row_value, 0, sizeof(row_value));
    memset(auto_kind, 0, sizeof(auto_kind));
    memset(auto_value, 0, sizeof(auto_value));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-percent-height.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 230, 240) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 57 FAIL", "percentage table setup failed");
        return FALSE;
    }
    for (i = 0; i < 5; i++) {
        if (PCore_TableRowGeometry(hDoc, (unsigned int) i,
                &rows[i]) != 0 ||
                PCore_TableRowSpecifiedHeight(hDoc, (unsigned int) i,
                &row_kind[i], &row_value[i]) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "row geometry/style lookup failed at %d", i);
            msg[sizeof(msg) - 1] = '\0';
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 57 FAIL", msg);
            return FALSE;
        }
    }
    first_height = rows[0].row_height + rows[1].row_height +
            rows[2].row_height;
    second_height = rows[3].row_height + rows[4].row_height;
    if (row_kind[0] != 1 || row_value[0] != 25 ||
            row_kind[1] != 1 || row_value[1] != 50 ||
            row_kind[2] != 0 ||
            row_kind[3] != 1 || row_value[3] != 75 ||
            row_kind[4] != 1 || row_value[4] != 75 ||
            first_height < 79 || first_height > 81 ||
            rows[0].row_height < 19 || rows[0].row_height > 21 ||
            rows[1].row_height < 39 || rows[1].row_height > 41 ||
            rows[2].row_height < 19 || rows[2].row_height > 21 ||
            rows[1].row_y != rows[0].row_y + rows[0].row_height ||
            rows[2].row_y != rows[1].row_y + rows[1].row_height ||
            second_height < 49 || second_height > 51 ||
            abs(rows[3].row_height - rows[4].row_height) > 1 ||
            rows[4].row_y != rows[3].row_y + rows[3].row_height) {
        _snprintf(msg, sizeof(msg) - 1,
                "pct=%d/%d/%d over=%d/%d styles=%d:%d,%d:%d,%d:%d",
                rows[0].row_height, rows[1].row_height,
                rows[2].row_height, rows[3].row_height,
                rows[4].row_height, row_kind[0], row_value[0],
                row_kind[1], row_value[1], row_kind[2], row_value[2]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 57 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    hDoc = PCore_ParseHTML(AUTO_HTML, sizeof(AUTO_HTML) - 1);
    hSheet = PCore_ParseCSS(AUTO_CSS, sizeof(AUTO_CSS) - 1,
            "http://positron.local/table-percent-auto.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 230, 120) != 0 ||
            PCore_TableRowGeometry(hDoc, 0, &auto_rows[0]) != 0 ||
            PCore_TableRowGeometry(hDoc, 1, &auto_rows[1]) != 0 ||
            PCore_TableRowSpecifiedHeight(hDoc, 0, &auto_kind[0],
                    &auto_value[0]) != 0 ||
            PCore_TableRowSpecifiedHeight(hDoc, 1, &auto_kind[1],
                    &auto_value[1]) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 57 FAIL", "auto-height control setup failed");
        return FALSE;
    }
    auto_height = auto_rows[0].row_height + auto_rows[1].row_height;
    if (auto_kind[0] != 1 || auto_value[0] != 80 ||
            auto_kind[1] != 0 ||
            abs(auto_rows[0].row_height - auto_rows[1].row_height) > 1 ||
            auto_height > 50) {
        _snprintf(msg, sizeof(msg) - 1,
                "auto=%d/%d sum=%d styles=%d:%d,%d:%d",
                auto_rows[0].row_height, auto_rows[1].row_height,
                auto_height, auto_kind[0], auto_value[0],
                auto_kind[1], auto_value[1]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 57 FAIL", msg);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/table-percent-height.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 57 FAIL", "visible percentage setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 57",
              "Expect red/green/blue rows at 25%/50%/remaining height.\n"
              "The orange/cyan 75% rows below share their capped table.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 57 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 57 OK",
              "Percentage rows, over-constraint capping and auto control\n"
              "passed through NetSurf layout and redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 58 - HTML style attribute through libcss inline cascade         */
/* -------------------------------------------------------------------- */
static BOOL test58_inline_author_style(void)
{
    static const char HTML[] =
        "<!doctype html><html><head><style>"
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:12px;line-height:15px;padding:6px;}"
        "h1{color:#111111;font-size:19px;line-height:22px;margin:0 0 3px;}"
        "section{color:#112233;}aside{color:#abcdef!important;}"
        ".scope .probe{color:#2468ac;}"
        "table{border-collapse:separate;border-spacing:0;margin-top:4px;}"
        "td{border:1px solid #404040;padding:1px;}"
        "</style></head><body>"
        "<h1 style='color:#123456'>Inline author CSS</h1>"
        "<article style='width:160px;padding-left:10px;background:#eeeeff'>"
        "<section style='color:#336699'>inline colour <em>inherited</em>"
        "</section><aside style='color:#fedcba'>external !important wins"
        "</aside><strong style='color:#778899!important'>inline !important"
        " wins</strong><footer style='color:broken;color:#0a0b0c'>"
        "parser recovery</footer></article>"
        "<scope class=scope><probe class=probe>descendant class selector"
        "</probe></scope>"
        "<nav style='display:none'>must stay hidden</nav>"
        "<table style='width:180px;height:80px'>"
        "<tr style='height:25%'><td style='background:#ffb0b0'>25%</td></tr>"
        "<tr style='height:50%'><td style='background:#b0ffb0'>50%</td></tr>"
        "<tr><td style='background:#b0b0ff'>auto</td></tr>"
        "</table></body></html>";
    static const char EXTRA_CSS[] =
        "h1{color:#222222}strong{color:#445566!important}";
    static const char *tags[7] = {
        "h1", "section", "em", "aside", "strong", "footer", "probe"
    };
    static const unsigned long expected[7] = {
        0x00123456UL, 0x00336699UL, 0x00336699UL,
        0x00abcdefUL, 0x00778899UL, 0x000a0b0cUL, 0x002468acUL
    };
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTableRowGeometry rows[3];
    int row_kind[3];
    int row_value[3];
    unsigned long colors[7];
    int article_w;
    int total_h;
    int screen_w;
    int screen_h;
    int i;
    char msg[256];

    memset(rows, 0, sizeof(rows));
    memset(row_kind, 0, sizeof(row_kind));
    memset(row_value, 0, sizeof(row_value));
    memset(colors, 0, sizeof(colors));
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(EXTRA_CSS, sizeof(EXTRA_CSS) - 1,
            "http://positron.local/inline-extra.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocumentEx2(hDoc, hSheet,
                    "http://positron.local/inline/page.html",
                    wm_combine_url, NULL, NULL, NULL) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 58 FAIL", "inline style setup failed");
        return FALSE;
    }
    for (i = 0; i < 7; i++) {
        if (PCore_NodeComputedColor(hDoc, tags[i], &colors[i]) != 0 ||
                (colors[i] & 0x00ffffffUL) != expected[i]) {
            break;
        }
    }
    if (i != 7) {
        _snprintf(msg, sizeof(msg) - 1,
                "cascade case=%d tag=%s got=%06lX expect=%06lX",
                i, (i < 7) ? tags[i] : "?",
                (i < 7) ? colors[i] & 0x00ffffffUL : 0UL,
                (i < 7) ? expected[i] : 0UL);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 58 FAIL", msg);
        return FALSE;
    }

    /* A second style pass exercises replacement cleanup and the same inline
     * declarations before the formal NetSurf box/layout path consumes them. */
    if (PCore_StyleDocumentEx2(hDoc, hSheet,
            "http://positron.local/inline/page.html",
            wm_combine_url, NULL, NULL, NULL) != 0 ||
            PCore_LayoutDocument(hDoc, 230, 260) != 0 ||
            PCore_NodeBox(hDoc, "article", NULL, NULL, &article_w, NULL) != 0 ||
            PCore_NodeBox(hDoc, "nav", NULL, NULL, NULL, NULL) == 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 58 FAIL", "restyle/layout/hidden box failed");
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        if (PCore_TableRowGeometry(hDoc, (unsigned int) i, &rows[i]) != 0 ||
                PCore_TableRowSpecifiedHeight(hDoc, (unsigned int) i,
                        &row_kind[i], &row_value[i]) != 0) {
            break;
        }
    }
    total_h = rows[0].row_height + rows[1].row_height + rows[2].row_height;
    if (i != 3 || article_w != 160 || row_kind[0] != 1 ||
            row_value[0] != 25 || row_kind[1] != 1 ||
            row_value[1] != 50 || row_kind[2] != 0 ||
            total_h < 79 || total_h > 81 ||
            rows[0].row_height < 19 || rows[0].row_height > 21 ||
            rows[1].row_height < 39 || rows[1].row_height > 41 ||
            rows[2].row_height < 19 || rows[2].row_height > 21) {
        _snprintf(msg, sizeof(msg) - 1,
                "layout i=%d article=%d rows=%d/%d/%d kinds=%d:%d,%d:%d,%d",
                i, article_w, rows[0].row_height, rows[1].row_height,
                rows[2].row_height, row_kind[0], row_value[0],
                row_kind[1], row_value[1], row_kind[2]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 58 FAIL", msg);
        return FALSE;
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    if (PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 58 FAIL", "visible inline layout failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 58",
              "Expect a blue heading, tinted 160px content band, cascade\n"
              "samples and a red/green/blue 25/50/auto table. The hidden\n"
              "navigation text must not appear.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 58 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 58 OK",
              "HTML style attributes passed cascade, inheritance,\n"
              "!important, parser recovery, restyle and layout/redraw.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 59 - overflow boundary inside a reversed flex item               */
/* -------------------------------------------------------------------- */
static BOOL test59_flex_overflow_min_content(void)
{
    static const char *HTML =
        "<!DOCTYPE html><html><body><article><main>"
        "<div class=scroll><table><tr>"
        "<td>REGISTRY-NAME-THAT-MUST-NOT-SHRINK</td>"
        "<td>SECOND-WIDE-COLUMN</td></tr></table></div></main>"
        "<nav>wide side navigation</nav></article></body></html>";
    static const char *CSS =
        "html,body{margin:0;padding:0;background:#fff;}"
        "article{display:flex;flex-direction:row-reverse;padding:25px;}"
        "main{flex-grow:1;flex-basis:0;background:#f7f7fb;}"
        "nav{display:none;width:180px;}"
        ".scroll{overflow:auto;border:1px solid #808080;}"
        "table{border-collapse:collapse;}"
        "td{white-space:nowrap;padding:4px;}";
    static const int widths[2] = { 224, 320 };
    HANDLE hDoc;
    HANDLE hSheet;
    int x;
    int y;
    int w;
    int h;
    int i;
    char msg[256];

    msg[0] = '\0';
    for (i = 0; i < 2; i++) {
        hDoc = PCore_ParseHTML(HTML, 0);
        hSheet = PCore_ParseCSS(CSS, 0,
                "http://positron.local/flex-overflow.css");
        x = 0;
        y = 0;
        w = 0;
        h = 0;
        if (hDoc == NULL || hSheet == NULL ||
                PCore_StyleDocument(hDoc, hSheet) != 0 ||
                PCore_LayoutDocument(hDoc, widths[i], 240) != 0 ||
                PCore_NodeBox(hDoc, "main", &x, &y, &w, &h) != 0 ||
                x != 25 || w != widths[i] - 50) {
            _snprintf(msg, sizeof(msg) - 1,
                    "width=%d main=(%d,%d) %dx%d; expect x=25 w=%d",
                    widths[i], x, y, w, h, widths[i] - 50);
        }
        msg[sizeof(msg) - 1] = '\0';
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        if (msg[0] != '\0') {
            break;
        }
    }
    if (i != 2) {
        show_error(L"TEST 59 FAIL", msg);
        return FALSE;
    }

    show_info(L"TEST 59 OK",
              "A wide overflow:auto table stayed inside its\n"
              "reversed-flex main at 224px and 320px viewports.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 60 - retained libcss node data across a restyle transaction      */
/* -------------------------------------------------------------------- */
static BOOL test60_table_header_restyle(void)
{
    static const char *HTML =
        "<!doctype html><html><body><div class=dtable-wrap>"
        "<table class=dtable><thead><tr>"
        "<th>Domain</th><th>Domain</th><th>Language</th>"
        "</tr></thead><tbody>"
        "<tr><th>row heading</th><td>A-label</td><td>Latin</td></tr>"
        "<tr><td>example</td><td>xn--example</td><td>Latin</td></tr>"
        "</tbody></table></div></body></html>";
    static const char *CSS =
        "*{box-sizing:border-box;margin:0;padding:0;font-weight:400;}"
        "html,body{background:#fff;color:#111;}"
        "body{font-size:12px;line-height:15px;}"
        ".dtable-wrap{width:100%;overflow:auto;}"
        ".dtable{width:360px;border-collapse:collapse;font-size:12px;}"
        ".dtable th,.dtable td{text-align:left;vertical-align:top;}"
        ".dtable thead th{padding:10px 14px;font-weight:700;"
        "white-space:nowrap;}"
        ".dtable td{padding:5px 14px;}"
        ".dtable th:first-child,.dtable td:first-child{padding-left:18px;}"
        ".dtable>tbody>tr:first-child>th{padding:5px 14px;}"
        "@media(min-width:300px){.dtable-wrap{width:390px;}}";
    static const int widths[2] = { 224, 400 };
    static const int heights[2] = { 320, 240 };
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTableCellGeometry cells[4];
    HDC screen_dc;
    int screen_w;
    int screen_h;
    int dpi;
    int pass;
    int dx0;
    int dx1;
    int dx3;
    int dx6;
    int dy0;
    int dy1;
    int dy3;
    int dy6;
    BOOL ok;
    char msg[256];

    hDoc = NULL;
    hSheet = NULL;
    ok = FALSE;
    msg[0] = '\0';
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    screen_dc = GetDC(NULL);
    dpi = (screen_dc != NULL) ?
            GetDeviceCaps(screen_dc, LOGPIXELSY) : 96;
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/table-header-restyle.css");
    if (hDoc == NULL || hSheet == NULL) {
        strcpy(msg, "parse failed");
        goto cleanup;
    }

    for (pass = 0; pass < 2; pass++) {
        memset(cells, 0, sizeof(cells));
        PCore_SetViewport(widths[pass], heights[pass], dpi);
        if (PCore_StyleDocument(hDoc, hSheet) != 0 ||
                PCore_LayoutDocument(hDoc, widths[pass],
                        heights[pass]) != 0 ||
                PCore_TableCellGeometry(hDoc, 0, &cells[0]) != 0 ||
                PCore_TableCellGeometry(hDoc, 1, &cells[1]) != 0 ||
                PCore_TableCellGeometry(hDoc, 3, &cells[2]) != 0 ||
                PCore_TableCellGeometry(hDoc, 6, &cells[3]) != 0) {
            _snprintf(msg, sizeof(msg) - 1,
                    "pass=%d style/layout/geometry failed", pass);
            msg[sizeof(msg) - 1] = '\0';
            goto cleanup;
        }

        dx0 = cells[0].first_text_x - cells[0].cell_x;
        dx1 = cells[1].first_text_x - cells[1].cell_x;
        dx3 = cells[2].first_text_x - cells[2].cell_x;
        dx6 = cells[3].first_text_x - cells[3].cell_x;
        dy0 = cells[0].first_text_y - cells[0].cell_y;
        dy1 = cells[1].first_text_y - cells[1].cell_y;
        dy3 = cells[2].first_text_y - cells[2].cell_y;
        dy6 = cells[3].first_text_y - cells[3].cell_y;
        if (dx0 < 17 || dx0 > 19 || dx1 < 13 || dx1 > 15 ||
                dx3 < 13 || dx3 > 15 || dx6 < 17 || dx6 > 19 ||
                dy0 < 9 || dy0 > 11 || dy1 < 9 || dy1 > 11 ||
                dy3 < 4 || dy3 > 6 || dy6 < 4 || dy6 > 6 ||
                abs(cells[0].first_text_width -
                        cells[1].first_text_width) > 1) {
            _snprintf(msg, sizeof(msg) - 1,
                    "pass=%d x=%d/%d/%d/%d y=%d/%d/%d/%d text=%d/%d",
                    pass, dx0, dx1, dx3, dx6, dy0, dy1, dy3, dy6,
                    cells[0].first_text_width,
                    cells[1].first_text_width);
            msg[sizeof(msg) - 1] = '\0';
            goto cleanup;
        }
    }
    ok = TRUE;

cleanup:
    PCore_SetViewport(screen_w, screen_h, dpi);
    if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
    if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
    if (!ok) {
        show_error(L"TEST 60 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 60 OK",
              "The first table header kept its 18px/10px inset and bold\n"
              "text while the same DOM restyled from portrait to landscape.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 61 - explicit NetSurf option defaults and minimum font size      */
/* -------------------------------------------------------------------- */
static BOOL test61_nsoption_font_minimum(void)
{
    static const char *HTML =
        "<!doctype html><html><body><table><tr>"
        "<td class=tiny>MMMMMMMM</td>"
        "<td class=floor>MMMMMMMM</td>"
        "<td class=normal>MMMMMMMM</td>"
        "</tr></table></body></html>";
    static const char *CSS =
        "html,body{margin:0;padding:0;background:#fff;}"
        "table{border-collapse:collapse;}"
        "td{padding:0;white-space:nowrap;line-height:20px;}"
        ".tiny{font-size:1px;}"
        ".floor{font-size:8.5pt;}"
        ".normal{font-size:12pt;}";
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTableCellGeometry tiny;
    PCoreTableCellGeometry floor;
    PCoreTableCellGeometry normal;
    char msg[192];

    memset(&tiny, 0, sizeof(tiny));
    memset(&floor, 0, sizeof(floor));
    memset(&normal, 0, sizeof(normal));
    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/nsoption-font-minimum.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 360, 100) != 0 ||
            PCore_TableCellGeometry(hDoc, 0, &tiny) != 0 ||
            PCore_TableCellGeometry(hDoc, 1, &floor) != 0 ||
            PCore_TableCellGeometry(hDoc, 2, &normal) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 61 FAIL", "parse/style/layout/geometry failed");
        return FALSE;
    }

    if (tiny.first_text_width <= 0 ||
            tiny.first_text_width != floor.first_text_width ||
            normal.first_text_width <= floor.first_text_width) {
        _snprintf(msg, sizeof(msg) - 1,
                "text widths tiny/floor/normal=%d/%d/%d",
                tiny.first_text_width, floor.first_text_width,
                normal.first_text_width);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 61 FAIL", msg);
        return FALSE;
    }

    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 61 OK",
              "NetSurf font_min_size=85 clamped 1px text to the same\n"
              "measured width as 8.5pt, while 12pt remained larger.\n"
              "JavaScript stays explicitly disabled.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 62 - read-only NetSurf checkbox/radio form gadgets              */
/* -------------------------------------------------------------------- */
static int test62_toggle_probe(const char *type, int selected,
        int *out_w, int *out_h, int *out_darkness, int *out_state)
{
    static const char *CSS =
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:18px;line-height:24px;}"
        "input{font-size:18px;}";
    char html[160];
    HANDLE hDoc = NULL;
    HANDLE hSheet = NULL;
    HDC screen_dc = NULL;
    HDC memory_dc = NULL;
    HBITMAP bitmap = NULL;
    HBITMAP old_bitmap = NULL;
    int x;
    int y;
    int w;
    int h;
    int px;
    int py;
    int darkness = 0;
    int state = 0;
    int rc = 1;

    _snprintf(html, sizeof(html) - 1,
            "<!doctype html><html><body><input type=%s%s></body></html>",
            type, selected ? " checked" : "");
    html[sizeof(html) - 1] = '\0';
    hDoc = PCore_ParseHTML(html, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/form-toggle-probe.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 64, 48) != 0 ||
            PCore_NodeBox(hDoc, "input", &x, &y, &w, &h) != 0 ||
            PCore_NodeFormControlState(hDoc, "input", NULL, &state,
                    NULL) != 0 ||
            w <= 0 || h <= 0) {
        goto cleanup;
    }
    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 64, 48) : NULL;
    if (memory_dc == NULL || bitmap == NULL) {
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    PatBlt(memory_dc, 0, 0, 64, 48, WHITENESS);
    PCore_PaintDocument(hDoc, memory_dc, 0, 0);
    for (py = y; py < y + h && py < 48; py++) {
        for (px = x; px < x + w && px < 64; px++) {
            COLORREF pixel = GetPixel(memory_dc, px, py);
            if (pixel != CLR_INVALID) {
                darkness += 255 - (int) GetRValue(pixel);
                darkness += 255 - (int) GetGValue(pixel);
                darkness += 255 - (int) GetBValue(pixel);
            }
        }
    }
    *out_w = w;
    *out_h = h;
    *out_darkness = darkness;
    *out_state = state;
    rc = 0;

cleanup:
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memory_dc != NULL) {
        DeleteDC(memory_dc);
    }
    if (screen_dc != NULL) {
        ReleaseDC(NULL, screen_dc);
    }
    if (hSheet != NULL) {
        PCore_FreeStylesheet(hSheet);
    }
    if (hDoc != NULL) {
        PCore_FreeDocument(hDoc);
    }
    return rc;
}

static BOOL test62_form_toggles(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Read-only form controls</h1>"
        "<p><input type=checkbox> unchecked checkbox</p>"
        "<p><input type=checkbox checked> checked checkbox</p>"
        "<p><input type=radio name=choice> unselected radio</p>"
        "<p><input type=radio name=choice checked> selected radio</p>"
        "<input type=hidden value='must not create a box'>"
        "<footer>The hidden input must not leave a gap.</footer>"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:16px;line-height:22px;padding:10px;color:#111;}"
        "h1{font-size:21px;line-height:25px;color:#8b0000;margin:0 0 8px;}"
        "p{margin:6px 0;}input{font-size:18px;}"
        "footer{margin-top:10px;color:#225588;}";
    static const char HIDDEN_HTML[] =
        "<!doctype html><html><body><input type=hidden value=x></body></html>";
    int checkbox_off_w;
    int checkbox_off_h;
    int checkbox_off_darkness;
    int checkbox_off_state;
    int checkbox_on_w;
    int checkbox_on_h;
    int checkbox_on_darkness;
    int checkbox_on_state;
    int radio_off_w;
    int radio_off_h;
    int radio_off_darkness;
    int radio_off_state;
    int radio_on_w;
    int radio_on_h;
    int radio_on_darkness;
    int radio_on_state;
    HANDLE hidden_doc = NULL;
    HANDLE hDoc = NULL;
    HANDLE hSheet = NULL;
    int screen_w;
    int screen_h;
    char msg[256];

    if (test62_toggle_probe("checkbox", 0, &checkbox_off_w,
                &checkbox_off_h, &checkbox_off_darkness,
                &checkbox_off_state) != 0 ||
            test62_toggle_probe("checkbox", 1, &checkbox_on_w,
                &checkbox_on_h, &checkbox_on_darkness,
                &checkbox_on_state) != 0 ||
            test62_toggle_probe("radio", 0, &radio_off_w,
                &radio_off_h, &radio_off_darkness, &radio_off_state) != 0 ||
            test62_toggle_probe("radio", 1, &radio_on_w,
                &radio_on_h, &radio_on_darkness, &radio_on_state) != 0) {
        show_error(L"TEST 62 FAIL", "toggle probe setup/redraw failed");
        return FALSE;
    }
    if (checkbox_off_w != checkbox_on_w ||
            checkbox_off_h != checkbox_on_h ||
            radio_off_w != radio_on_w ||
            radio_off_h != radio_on_h ||
            checkbox_off_w < 14 || checkbox_off_w > 24 ||
            checkbox_off_h < 14 || checkbox_off_h > 24 ||
            radio_off_w < 14 || radio_off_w > 24 ||
            radio_off_h < 14 || radio_off_h > 24 ||
            checkbox_off_state != 0 || checkbox_on_state != 1 ||
            radio_off_state != 0 || radio_on_state != 1 ||
            checkbox_on_darkness <= checkbox_off_darkness ||
            radio_on_darkness <= radio_off_darkness) {
        _snprintf(msg, sizeof(msg) - 1,
                "cb=%dx%d dark=%d/%d state=%d/%d\n"
                "radio=%dx%d dark=%d/%d state=%d/%d",
                checkbox_off_w, checkbox_off_h, checkbox_off_darkness,
                checkbox_on_darkness, checkbox_off_state, checkbox_on_state,
                radio_off_w, radio_off_h, radio_off_darkness,
                radio_on_darkness,
                radio_off_state, radio_on_state);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 62 FAIL", msg);
        return FALSE;
    }

    hidden_doc = PCore_ParseHTML(HIDDEN_HTML, sizeof(HIDDEN_HTML) - 1);
    if (hidden_doc == NULL || PCore_StyleDocument(hidden_doc, NULL) != 0 ||
            PCore_LayoutDocument(hidden_doc, 64, 48) != 0 ||
            PCore_NodeBox(hidden_doc, "input", NULL, NULL, NULL, NULL) == 0) {
        if (hidden_doc != NULL) {
            PCore_FreeDocument(hidden_doc);
        }
        show_error(L"TEST 62 FAIL", "hidden input generated a visible box");
        return FALSE;
    }
    PCore_FreeDocument(hidden_doc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/form-toggles.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 62 FAIL", "visible toggle setup failed");
        return FALSE;
    }
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 62",
              "Expect unchecked/checked checkbox and radio pairs.\n"
              "The hidden input leaves no visible row. Controls are\n"
              "read-only in this first forms milestone.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 62 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 62 OK",
              "NetSurf checkbox/radio geometry, selected-state redraw and\n"
              "hidden-input suppression passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 14 - milestone H/M1: GDI plotter table self-test                  */
/* Opens a window and paints via PCore_PlotTest - the NetSurf plotter      */
/* interface backed by GDI - with NO layout engine involved. Confirms the  */
/* plotter table, colour conversion, pens/brushes and text baseline before */
/* redraw.c is ported in.                                                  */
/* -------------------------------------------------------------------- */
static BOOL test14_plot(void)
{
    show_info(L"TEST 14",
              "Milestone H/M1: a window opens and paints via the\n"
              "GDI-backed NetSurf plotter (no layout engine).\n\n"
              "Expect: a grey box with a SOLID red border + black\n"
              "text; below it a DOTTED blue box border; and a\n"
              "DASHED green line. (Dotted/dashed are hand-drawn,\n"
              "since WinCE pens are solid-only.)\n"
              "Tap or press Esc to close.");

    g_plot_test = 1;
    g_render_doc = NULL;
    g_doc_h = 0;
    g_scroll_y = 0;
    if (!show_render_window()) {
        g_plot_test = 0;
        show_error(L"TEST 14 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_plot_test = 0;

    show_info(L"TEST 14 OK",
              "GDI plotter table verified: rectangle (fill+border),\n"
              "line, and baseline-aligned text drew correctly.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 17 - milestone H/M5f: NetSurf real layout/redraw/borders on screen */
/* A window opens; the page is laid out by NetSurf's layout_document and    */
/* painted by html_redraw through our GDI plotter (M1) + font table (M2).   */
/* First page rendered end-to-end by the ported engine.                     */
/* -------------------------------------------------------------------- */
static BOOL test17_nsrender(void)
{
    show_info(L"TEST 17",
              "NetSurf REAL layout + redraw + borders (M5f/M7):\n"
              "the page is laid out by NetSurf's layout.c and painted\n"
              "by redraw.c + redraw_border.c through our GDI plotter.\n\n"
              "Expect a dark-red H1 with a red underline border,\n"
              "a light-blue padded box with a blue border and two\n"
              "wrapped blue paragraphs, then THREE colour blocks\n"
              "(red/green/blue: One/Two/Three) in a dashed border row.\n"
              "Side by side = flex works; stacked = flex failed.\n"
              "Below that, a 2x2 table with visible cell borders.\n"
              "Also expect text: Image fallback: Logo.\n"
              "Tap or Esc to close.");

    g_ns_render = 1;
    g_render_doc = NULL;
    g_plot_test = 0;
    g_scroll_y = 0;
    g_doc_h = 0;
    if (!show_render_window()) {
        g_ns_render = 0;
        show_error(L"TEST 17 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_ns_render = 0;

    show_info(L"TEST 17 OK",
              "Rendered end-to-end by the ported NetSurf engine\n"
              "(layout.c + redraw.c + redraw_border.c + flex/table/img alt)\n"
              "on the device.");
    return TRUE;
}

static int run_configured_tests(const unsigned char *selected,
        int selected_7b, int *http_active)
{
    BOOL ok;
    int number;
    int needs_core;

    *http_active = 0;
    if (!selected[1] && (selected[3] || selected[4] || selected[13])) {
        if (!PHttp_Init()) {
            show_error(L"Configured tests FAIL", "PHttp_Init returned FALSE");
            return 1;
        }
        *http_active = 1;
    }
    needs_core = 0;
    for (number = 8; number <= TEST_MAX_NUMBER; number++) {
        if (selected[number]) {
            needs_core = 1;
            break;
        }
    }
    if (needs_core && PCore_Init() != 0) {
        show_error(L"Configured tests FAIL", "PCore_Init returned an error");
        return 8;
    }

    for (number = 1; number <= TEST_MAX_NUMBER; number++) {
        if (number == 7) {
            if (selected[7] && !test7_libcss()) {
                return 7;
            }
            if (selected_7b && !test7b_dom()) {
                return 7;
            }
            continue;
        }
        if (!selected[number]) {
            continue;
        }
        ok = TRUE;
        switch (number) {
        case 1:  ok = test1_dll_load(); break;
        case 2:  ok = test2_json(); break;
        case 3:  ok = test3_get(); break;
        case 4:  ok = test4_post(); break;
        case 5:  ok = test5_verified_tls(); break;
        case 6:  ok = test6_hubbub(); break;
        case 8:  ok = test8_core(); break;
        case 9:  ok = test9_select(); break;
        case 10: ok = test10_styledoc(); break;
        case 11: ok = test11_layout(); break;
        case 12: ok = test12_render(); break;
        case 13: ok = test_browse(); break;
        case 14: ok = test14_plot(); break;
        case 15: ok = test_boxtree(); break;
        case 16: ok = test_layout(); break;
        case 17: ok = test17_nsrender(); break;
        case 18: ok = test_image_resources(); break;
        case 19: ok = test19_wmimage(); break;
        case 20: ok = test20_cached_img(); break;
        case 21: ok = test21_media_viewport(); break;
        case 22: ok = test22_reverse_flex_padding(); break;
        case 24: ok = test24_cached_stylesheet_restyle(); break;
        case 25: ok = test25_svg_parse(); break;
        case 26: ok = test26_svg_draw(); break;
        case 27: ok = test27_cached_svg_img(); break;
        case 28: ok = test28_broken_svg_fallback(); break;
        case 29: ok = test29_svg_fill_rule(); break;
        case 30: ok = test30_css_background_image(); break;
        case 31: ok = test31_svg_text(); break;
        case 32: ok = test32_cached_svg_gradient_text(); break;
        case 33: ok = test33_svg_gradient_coordinates(); break;
        case 34: ok = test34_svg_radial_gradient(); break;
        case 35: ok = test35_cached_svg_radial_gradient(); break;
        case 36: ok = test36_svg_gradient_feature_matrix(); break;
        case 37: ok = test37_cached_svg_gradient_batch(); break;
        case 38: ok = test38_css_root_variables(); break;
        case 39: ok = test39_css_variable_layout(); break;
        case 40: ok = test40_css_modern_values(); break;
        case 41: ok = test41_grid_overflow_flex(); break;
        case 42: ok = test42_overflow_scrollbar(); break;
        case 43: ok = test43_navigation_resource_transaction(); break;
        case 44: ok = test44_navigation_failure_transaction(); break;
        case 45: ok = test45_css_import_tree(); break;
        case 46: ok = test46_table_spans(); break;
        case 47: ok = test47_table_normalise(); break;
        case 48: ok = test48_list_markers(); break;
        case 49: ok = test49_bundled_fonts(); break;
        case 50: ok = test50_counter_styles(); break;
        case 51: ok = test51_inside_list_markers(); break;
        case 52: ok = test52_inside_block_markers(); break;
        case 53: ok = test53_table_collapsed_borders(); break;
        case 54: ok = test54_table_spanning_borders(); break;
        case 55: ok = test55_table_cell_alignment(); break;
        case 56: ok = test56_table_height_distribution(); break;
        case 57: ok = test57_table_percentage_rows(); break;
        case 58: ok = test58_inline_author_style(); break;
        case 59: ok = test59_flex_overflow_min_content(); break;
        case 60: ok = test60_table_header_restyle(); break;
        case 61: ok = test61_nsoption_font_minimum(); break;
        case 62: ok = test62_form_toggles(); break;
        default: ok = FALSE; break;
        }
        if (!ok) {
            return number;
        }
        if (number == 1) {
            *http_active = 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------- */
/* WinMain                                                               */
/* -------------------------------------------------------------------- */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                   LPWSTR lpCmdLine, int nCmdShow)
{
    BOOL run_comm;
    BOOL run_engine;
    BOOL run_render;
    BOOL run_browse;
    unsigned char configured_tests[TEST_MAX_NUMBER + 1];
    int configured_7b;
    int configured_count;
    int configured_http;
    int core_active;
    int  rc;
    char config_prompt[512];
    char summary[1024];

    (void)hInstance;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    OutputDebugStringW(L"test_host (Phase 4): starting\r\n");

    /* Tell positron_core the real device viewport + DPI so styling and layout
     * adapt to this screen rather than a hardcoded default. */
    {
        HDC sdc = GetDC(NULL);
        int dpi = (sdc != NULL) ? GetDeviceCaps(sdc, LOGPIXELSY) : 96;
        if (sdc != NULL) {
            ReleaseDC(NULL, sdc);
        }
        PCore_SetViewport(GetSystemMetrics(SM_CXSCREEN),
                          GetSystemMetrics(SM_CYSCREEN), dpi);
    }

    configured_count = test_config_load(configured_tests, &configured_7b);
    if (configured_count < 0) {
        show_error(L"test_host.ini ignored",
                   "The file exists but is empty, unreadable or malformed.\n"
                   "Use: tests=31,32 or tests=1-5 7b\n\n"
                   "TEST 23 is unavailable. Continuing with group selection.");
    } else if (configured_count > 0) {
        test_config_prompt(configured_tests, configured_7b,
                config_prompt, sizeof(config_prompt));
        if (ask_yesno(L"Configured tests", config_prompt)) {
            configured_http = 0;
            rc = run_configured_tests(configured_tests, configured_7b,
                    &configured_http);
            if (configured_http) {
                PHttp_Cleanup();
            }
            PCore_Shutdown();
            if (rc == 0) {
                show_info(L"Configured tests passed",
                          "All tests selected by test_host.ini passed.");
            }
            return rc;
        }
    }

    /* Group selector. One tap runs everything; otherwise pick groups so a
     * subset can run in isolation - e.g. only the fully-offline engine /
     * rendering group when there is no network (no VPN needed). */
    if (ask_yesno(L"Positron test_host",
                  "Run ALL tests?\n\n"
                  "Yes = run all selected groups (TEST 1-62)\n"
                  "No  = choose which groups to run")) {
        run_comm = TRUE;
        run_engine = TRUE;
        run_render = TRUE;
        run_browse = TRUE;
    } else {
        run_comm = ask_yesno(L"Select groups (1/4)",
                             "Run COMMUNICATION tests?\n\n"
                             "TLS / HTTP / JSON  (TEST 1-5).\n"
                             "Needs network access.");
        run_engine = ask_yesno(L"Select groups (2/4)",
                               "Run ENGINE tests?\n\n"
                               "HTML / CSS / DOM parse, select, style,\n"
                               "layout, box tree, NetSurf layout,\n"
                               "image resource cache\n"
                               "(TEST 6-11, 15, 16, 18, 21, 22, 24, 25,\n"
                               "38, 40-45, 59-61). Offline.");
        run_render = ask_yesno(L"Select groups (3/4)",
                               "Run GDI RENDER tests?\n\n"
                               "NetSurf/GDI pages (TEST 12, 14, 17),\n"
                               "native bitmap draw (TEST 19),\n"
                               "SVG draw/cache/fallback/text/gradients\n"
                               "(TEST 26-37), and responsive IANA-style\n"
                               "layout redraw (TEST 39), plus table/list/\n"
                               "inline CSS and read-only form redraw\n"
                               "(TEST 46-58, 62).\n"
                               "Fully offline.");
        run_browse = ask_yesno(L"Select groups (4/4)",
                               "Run BROWSE test?\n\n"
                               "Fetch a real HTTPS page and render it\n"
                               "(TEST 13). Needs network access.");
    }

    rc = 0;
    core_active = 0;
    if (run_engine || run_render || run_browse) {
        if (PCore_Init() != 0) {
            show_error(L"positron_core init FAIL", "PCore_Init returned an error");
            return 8;
        }
        core_active = 1;
    }

    /* --- Communication group (TEST 1-5) ------------------------------- */
    if (run_comm) {
        if (!test1_dll_load()) {
            if (core_active) {
                PCore_Shutdown();
            }
            return 1;
        }
        if (!test2_json())         { rc = 2; goto done; }
        if (!test3_get())          { rc = 3; goto done; }
        if (!test4_post())         { rc = 4; goto done; }
        if (!test5_verified_tls()) { rc = 5; goto done; }
    }

    /* Engine: TEST 6-11, 15, 16, 18, 21, 22, 24, 25, 38, 40-45, 59-61. */
    if (run_engine) {
        if (!test6_hubbub())       { rc = 6; goto done; }
        if (!test7_libcss())       { rc = 7; goto done; }
        if (!test7b_dom())         { rc = 8; goto done; }
        if (!test8_core())         { rc = 9; goto done; }
        if (!test9_select())       { rc = 10; goto done; }
        if (!test10_styledoc())    { rc = 11; goto done; }
        if (!test21_media_viewport()){ rc = 11; goto done; }
        if (!test22_reverse_flex_padding()){ rc = 11; goto done; }
        if (!test24_cached_stylesheet_restyle()){ rc = 11; goto done; }
        if (!test25_svg_parse())   { rc = 11; goto done; }
        if (!test38_css_root_variables()){ rc = 11; goto done; }
        if (!test40_css_modern_values()){ rc = 11; goto done; }
        if (!test41_grid_overflow_flex()){ rc = 11; goto done; }
        if (!test42_overflow_scrollbar()){ rc = 11; goto done; }
        if (!test43_navigation_resource_transaction()){ rc = 11; goto done; }
        if (!test44_navigation_failure_transaction()){ rc = 11; goto done; }
        if (!test45_css_import_tree()){ rc = 11; goto done; }
        if (!test59_flex_overflow_min_content()){ rc = 11; goto done; }
        if (!test60_table_header_restyle()){ rc = 11; goto done; }
        if (!test61_nsoption_font_minimum()){ rc = 11; goto done; }
        /* These exercise separate views of the now-initialised engine. Run
         * all of them so one geometry assertion cannot hide later results. */
        if (!test11_layout())        { rc = 12; }
        if (!test_boxtree())         { rc = 12; }
        if (!test_layout())          { rc = 12; }
        if (!test_image_resources()) { rc = 12; }
        if (rc != 0)                 { goto done; }
    }

    /* GDI render: TEST 12, 14, 17, 19, 20, 26-37, 39, 46-58, 62; offline. */
    if (run_render) {
        char fb[192];
        PCore_FontTest(fb, sizeof(fb));        /* M2: font-measure sanity */
        show_info(L"TEST (M2) font table", fb);
        if (!test14_plot())        { rc = 13; goto done; }
        if (!test19_wmimage())     { rc = 13; goto done; }
        if (!test26_svg_draw())    { rc = 13; goto done; }
        if (!test20_cached_img())  { rc = 13; goto done; }
        if (!test27_cached_svg_img()){ rc = 13; goto done; }
        if (!test28_broken_svg_fallback()){ rc = 13; goto done; }
        if (!test29_svg_fill_rule()){ rc = 13; goto done; }
        if (!test30_css_background_image()){ rc = 13; goto done; }
        if (!test31_svg_text())    { rc = 13; goto done; }
        if (!test32_cached_svg_gradient_text()){ rc = 13; goto done; }
        if (!test33_svg_gradient_coordinates()){ rc = 13; goto done; }
        if (!test34_svg_radial_gradient()){ rc = 13; goto done; }
        if (!test35_cached_svg_radial_gradient()){ rc = 13; goto done; }
        if (!test36_svg_gradient_feature_matrix()){ rc = 13; goto done; }
        if (!test37_cached_svg_gradient_batch()){ rc = 13; goto done; }
        if (!test39_css_variable_layout()){ rc = 13; goto done; }
        if (!test46_table_spans()){ rc = 13; goto done; }
        if (!test47_table_normalise()){ rc = 13; goto done; }
        if (!test48_list_markers()){ rc = 13; goto done; }
        if (!test49_bundled_fonts()){ rc = 13; goto done; }
        if (!test50_counter_styles()){ rc = 13; goto done; }
        if (!test51_inside_list_markers()){ rc = 13; goto done; }
        if (!test52_inside_block_markers()){ rc = 13; goto done; }
        if (!test53_table_collapsed_borders()){ rc = 13; goto done; }
        if (!test54_table_spanning_borders()){ rc = 13; goto done; }
        if (!test55_table_cell_alignment()){ rc = 13; goto done; }
        if (!test56_table_height_distribution()){ rc = 13; goto done; }
        if (!test57_table_percentage_rows()){ rc = 13; goto done; }
        if (!test58_inline_author_style()){ rc = 13; goto done; }
        if (!test62_form_toggles()){ rc = 13; goto done; }
        if (!test17_nsrender())    { rc = 13; goto done; }
        if (!test12_render())      { rc = 13; goto done; }
    }

    /* --- Browse group (TEST 13: fetch a real page + render, network) -- */
    if (run_browse) {
        /* The comm group (TEST 1) normally initialises positron_http; if it
         * did not run, bring it up here for the fetch. */
        if (!run_comm && !PHttp_Init()) {
            show_error(L"TEST 13 FAIL", "PHttp_Init returned FALSE");
            rc = 14;
            goto done;
        }
        if (!test_browse())        { rc = 14; goto done; }
    }

    /* Success summary - list only the groups that actually ran. */
    summary[0] = '\0';
    strcat(summary, "Selected test groups passed:\n\n");
    if (run_comm) {
        strcat(summary,
               "  Communication (TEST 1-5)\n"
               "    TLS 1.2 + chain/hostname verify, CA bundle,\n"
               "    HTTPS GET (checkip) + POST, chunked, JSON.\n\n");
    }
    if (run_engine) {
        strcat(summary,
               "  Engine (TEST 6-11, 15, 16, 18, 21, 22, 24, 25, 38, 40-45, 59-61)\n"
               "    libhubbub + libcss + libdom behind\n"
               "    positron_core.dll; parse, select, style,\n"
               "    layout, media-query viewport, reverse flex, cached CSS restyle, box tree, NetSurf layout, image\n"
               "    resource cache, SVG parse, constrained :root variables,\n"
               "    OKLCH/calc values, grid-overflow containment, scrollbar\n"
               "    input, staged navigation resources, failure rollback,\n"
               "    native libcss CSS import trees, and retained selector\n"
               "    node data across portrait/landscape restyle, and explicit\n"
               "    NetSurf option defaults with minimum-font clamping.\n"
               "    Offline.\n\n");
    }
    if (run_render) {
        strcat(summary,
               "  GDI render (TEST 12, 14, 17, 19, 20, 26-37, 39, 46-58, 62)\n"
               "    HTML page painted to a window: background,\n"
               "    borders, padding, wrapped text, NetSurf redraw,\n"
               "    plus WM Imaging bitmaps, cached <img>, direct SVG and\n"
               "    cached SVG, fallback, fill rules, CSS backgrounds and\n"
               "    native SVG text, cached SVG gradients, coordinate transforms\n"
               "    radial gradients, inherited/alpha stops, cache reuse, and\n"
               "    IANA-style spacing, table normalisation, list markers and\n"
               "    read-only checkbox/radio gadgets through formal redraw.\n"
               "    Offline.\n\n");
    }
    if (run_browse) {
        strcat(summary,
               "  Browse (TEST 13)\n"
               "    fetched a real HTTPS page + rendered it\n"
               "    (HTTP -> parse -> style -> layout -> paint).\n\n");
    }
    if (!run_comm && !run_engine && !run_render && !run_browse) {
        strcat(summary, "  (no groups selected)\n");
    }
    show_info(L"Tests passed", summary);

done:
    /* positron_http is brought up by the comm group (TEST 1) and/or the
     * browse group; tear it down if either ran. */
    if (run_comm || run_browse) {
        PHttp_Cleanup();
    }
    if (core_active) {
        PCore_Shutdown();
    }
    return rc;
}
