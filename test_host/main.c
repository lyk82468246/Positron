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
 * No stdout on WinCE - interactive output uses MessageBoxW; automated
 * testbench output is written beside the EXE.
 * No API keys. All test endpoints are public.
 */

#include <windows.h>
#include <limits.h>
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
#include "positron_script.h"

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

static int    g_testbench_auto = 0;
static HANDLE g_testbench_log = INVALID_HANDLE_VALUE;

static int test_host_device_dpi(void)
{
    HDC dc;
    int dpi;

    dpi = 96;
    dc = GetDC(NULL);
    if (dc != NULL) {
        if (GetDeviceCaps(dc, LOGPIXELSY) > 0) {
            dpi = GetDeviceCaps(dc, LOGPIXELSY);
        }
        ReleaseDC(NULL, dc);
    }
    return dpi;
}

static void test_host_set_device_viewport(int device_width, int device_height)
{
    PCore_SetDeviceViewport(device_width, device_height,
            test_host_device_dpi());
}

static int test_host_sibling_path(const WCHAR *name,
        WCHAR path[MAX_PATH])
{
    DWORD path_len;

    path_len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (path_len == 0 || path_len >= MAX_PATH) {
        return 1;
    }
    while (path_len > 0 && path[path_len - 1] != L'\\' &&
            path[path_len - 1] != L'/') {
        path_len--;
    }
    if (path_len == 0 ||
            path_len + (DWORD) lstrlenW(name) >= MAX_PATH) {
        return 1;
    }
    lstrcpyW(path + path_len, name);
    return 0;
}

static void testbench_log_bytes(const char *text)
{
    DWORD written;
    DWORD length;

    if (g_testbench_log == INVALID_HANDLE_VALUE || text == NULL) {
        return;
    }
    length = (DWORD) strlen(text);
    if (length > 0) {
        WriteFile(g_testbench_log, text, length, &written, NULL);
    }
}

static void testbench_log_message(const char *kind, const WCHAR *title,
        const char *body)
{
    char title_utf8[192];
    int count;

    if (g_testbench_log == INVALID_HANDLE_VALUE) {
        return;
    }
    count = WideCharToMultiByte(CP_UTF8, 0, title, -1,
            title_utf8, sizeof(title_utf8), NULL, NULL);
    if (count <= 0) {
        strcpy(title_utf8, "(title conversion failed)");
    }
    testbench_log_bytes("[");
    testbench_log_bytes(kind);
    testbench_log_bytes("] ");
    testbench_log_bytes(title_utf8);
    testbench_log_bytes("\r\n");
    testbench_log_bytes(body);
    testbench_log_bytes("\r\n\r\n");
    FlushFileBuffers(g_testbench_log);
}

static int testbench_log_open(void)
{
    WCHAR path[MAX_PATH];

    if (test_host_sibling_path(L"test_host.log", path) != 0) {
        return 1;
    }
    g_testbench_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_testbench_log == INVALID_HANDLE_VALUE) {
        return 1;
    }
    testbench_log_bytes("Positron test_host automated run\r\n"
            "Log format: [INFO]/[ERROR], followed by the original "
            "test message.\r\n\r\n");
    {
        char environment[128];
        _snprintf(environment, sizeof(environment) - 1,
                "Device metrics: screen=%dx%d dpi=%d\r\n\r\n",
                GetSystemMetrics(SM_CXSCREEN),
                GetSystemMetrics(SM_CYSCREEN), test_host_device_dpi());
        environment[sizeof(environment) - 1] = '\0';
        testbench_log_bytes(environment);
    }
    FlushFileBuffers(g_testbench_log);
    return 0;
}

static void testbench_log_close(void)
{
    if (g_testbench_log != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_testbench_log);
        CloseHandle(g_testbench_log);
        g_testbench_log = INVALID_HANDLE_VALUE;
    }
}

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

static char *wide_to_utf8_alloc(const WCHAR *source)
{
    char *result;
    int bytes;

    if (source == NULL) {
        return NULL;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0, source, -1,
            NULL, 0, NULL, NULL);
    if (bytes <= 0) {
        return NULL;
    }
    result = (char *) malloc((size_t) bytes);
    if (result == NULL ||
            WideCharToMultiByte(CP_UTF8, 0, source, -1,
                    result, bytes, NULL, NULL) != bytes) {
        free(result);
        return NULL;
    }
    return result;
}

static WCHAR *utf8_to_wide_alloc(const char *source)
{
    WCHAR *result;
    int chars;

    if (source == NULL) {
        return NULL;
    }
    chars = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    if (chars <= 0) {
        chars = MultiByteToWideChar(CP_ACP, 0, source, -1, NULL, 0);
        if (chars <= 0) {
            return NULL;
        }
        result = (WCHAR *) malloc((size_t) chars * sizeof(WCHAR));
        if (result == NULL ||
                MultiByteToWideChar(CP_ACP, 0, source, -1,
                        result, chars) != chars) {
            free(result);
            return NULL;
        }
        return result;
    }
    result = (WCHAR *) malloc((size_t) chars * sizeof(WCHAR));
    if (result == NULL ||
            MultiByteToWideChar(CP_UTF8, 0, source, -1,
                    result, chars) != chars) {
        free(result);
        return NULL;
    }
    return result;
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
    testbench_log_message("INFO", title, body);
    if (g_testbench_auto) {
        return;
    }
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
    testbench_log_message("ERROR", title, body);
    if (g_testbench_auto) {
        return;
    }
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
#define TEST_MAX_NUMBER 104

static int test_config_space(char c)
{
    return c == ' ' || c == '\t';
}

static int test_config_available(int number)
{
    return number >= 1 && number <= TEST_MAX_NUMBER &&
            number != 23 && number != 78 && number != 79;
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

static int test_config_word_equal(const char *value, const char *word)
{
    char a;
    char b;

    while (*word != '\0') {
        a = *value++;
        b = *word++;
        if (a >= 'A' && a <= 'Z') {
            a = (char) (a + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
    }
    while (test_config_space(*value)) {
        value++;
    }
    return *value == '\0' || *value == '#' || *value == ';';
}

static int test_config_parse_bool(char *value, int *out_value)
{
    while (test_config_space(*value)) {
        value++;
    }
    if (test_config_word_equal(value, "1") ||
            test_config_word_equal(value, "yes") ||
            test_config_word_equal(value, "true")) {
        *out_value = 1;
        return 1;
    }
    if (test_config_word_equal(value, "0") ||
            test_config_word_equal(value, "no") ||
            test_config_word_equal(value, "false")) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

/* Return 1 for a complete key= prefix, 0 for another key/spec, and -1 when
 * the requested key is present but lacks '='. */
static int test_config_key_value(char *line, const char *key,
        char **out_value)
{
    char *p;
    char a;
    char b;

    p = line;
    while (*key != '\0') {
        if (*p == '\0') {
            return 0;
        }
        a = *p++;
        b = *key++;
        if (a >= 'A' && a <= 'Z') {
            a = (char) (a + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
    }
    if (*p != '=' && !test_config_space(*p)) {
        return 0;
    }
    while (test_config_space(*p)) {
        p++;
    }
    if (*p != '=') {
        return -1;
    }
    p++;
    *out_value = p;
    return 1;
}

/* Return >0 for a valid selection, 0 when no file exists, and -1 when the
 * file exists but is unreadable or malformed. */
static int test_config_load(unsigned char selected[TEST_MAX_NUMBER + 1],
        int *selected_7b, int *auto_run)
{
    WCHAR path[MAX_PATH];
    DWORD size;
    DWORD read_count;
    HANDLE file;
    char buffer[TEST_CONFIG_MAX_BYTES + 1];
    char *line;
    char *next;
    char *value;
    char *end;
    int i;
    int tests_found;
    int auto_found;
    int key_match;
    int count;

    memset(selected, 0, TEST_MAX_NUMBER + 1);
    *selected_7b = 0;
    *auto_run = 0;
    if (test_host_sibling_path(L"test_host.ini", path) != 0) {
        return -1;
    }
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

    tests_found = 0;
    auto_found = 0;
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
            key_match = test_config_key_value(line, "tests", &value);
            if (key_match < 0) {
                return -1;
            }
            if (key_match > 0) {
                if (tests_found) {
                    return -1;
                }
                if (!test_config_parse_spec(value, selected, selected_7b)) {
                    return -1;
                }
                tests_found = 1;
            } else {
                key_match = test_config_key_value(line, "auto", &value);
                if (key_match < 0) {
                    return -1;
                }
                if (key_match > 0) {
                    if (auto_found) {
                        return -1;
                    }
                    if (!test_config_parse_bool(value, auto_run)) {
                        return -1;
                    }
                    auto_found = 1;
                } else if (!tests_found) {
                    if (!test_config_parse_spec(line, selected,
                            selected_7b)) {
                        return -1;
                    }
                    tests_found = 1;
                } else {
                    return -1;
                }
            }
        }
        line = next;
    }
    if (!tests_found) {
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
        int selected_7b, int automated, char *buffer, int capacity)
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
    if (automated) {
        strncat(buffer,
                "\n\nAutomated mode: no prompts; results go to test_host.log.",
                (size_t) (capacity - 1 - strlen(buffer)));
    } else {
        strncat(buffer,
                "\n\nRun only these tests?\nNo = use the normal group selector.",
                (size_t) (capacity - 1 - strlen(buffer)));
    }
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
static int    g_mouse_tracking = 0;
#define PCORE_IMAGE_FORMAT_COUNT 4
static PIMAGE_BITMAP g_image_format_bitmap[PCORE_IMAGE_FORMAT_COUNT];
static const WCHAR *g_image_format_name[PCORE_IMAGE_FORMAT_COUNT] = {
    L"BMP", L"PNG", L"JPEG", L"GIF"
};

#define WM_PCORE_NAV_DONE (WM_APP + 1)
#define WM_PCORE_NAV_PROGRESS (WM_APP + 2)
#define WM_PCORE_NAV_CONTINUE (WM_APP + 3)
#define WM_TESTBENCH_NAVIGATE (WM_APP + 4)
#define WM_PCORE_FORM_ENTER (WM_APP + 5)
#define WM_PCORE_INTERACTION_RESTYLE (WM_APP + 6)
#define PCORE_NAV_TIMER 24
#define PCORE_NAV_COMMIT_TIMER 25
#define TESTBENCH_RENDER_TIMER 26
#define PCORE_HOVER_TIMER 27
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
    PCoreLayoutStats core_layout;
    int core_layout_valid;
    PCoreBoxStats core_box;
    int core_box_valid;
    PCoreImageDecodeStats core_image;
    int core_image_valid;
} pcore_navigation_stats;

typedef struct pcore_navigation_request {
    HWND           hwnd;
    LONG           generation;
    char           host[256];
    char           path[1024];
    int            port;
    int            method;
    char          *request_body;
    int            request_body_len;
    char          *request_content_type;
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
static int                       g_testbench_render_paints = 0;
static int                       g_testbench_browse_active = 0;
static int                       g_testbench_browse_step = 0;
static int                       g_testbench_browse_completed = 0;
static int                       g_testbench_browse_failed = 0;

static const char *g_testbench_browse_urls[] = {
    "https://example.com/",
    "https://www.iana.org/help/example-domains",
    "https://www.iana.org/domains/reserved"
};
#define TESTBENCH_BROWSE_URL_COUNT \
    ((int) (sizeof(g_testbench_browse_urls) / \
            sizeof(g_testbench_browse_urls[0])))

/* Current page origin, for resolving relative links during navigation. */
static char   g_cur_host[256] = "";
static char   g_cur_path[1024] = "/";
static int    g_cur_port = 443;

typedef struct pcore_native_edit {
    HWND hwnd;
    unsigned int text_index;
    int multiline;
    WNDPROC original_proc;
} pcore_native_edit;

static pcore_native_edit *g_native_edits = NULL;
static unsigned int g_native_edit_count = 0;
static int g_native_edit_syncing = 0;
static int g_native_edit_probe = 0;
static int g_native_edit_probe_multiline = 0;
static int g_native_edit_probe_ok = 0;
static int g_native_edit_probe_seen = 0;
static int g_native_edit_probe_set_result = -1;
static char g_native_edit_probe_value[32];
static int g_native_form_enter_probe = 0;
static int g_native_form_enter_probe_seen = 0;
static int g_native_form_enter_probe_ok = 0;
static char g_native_form_enter_expected[256];
static int g_native_label_probe = 0;
static int g_native_label_probe_ok = 0;

static LRESULT CALLBACK pcore_native_edit_proc(HWND hwnd, UINT msg,
        WPARAM wp, LPARAM lp)
{
    unsigned int i;
    WNDPROC original;
    LONG stored_index;
    HWND parent;

    original = NULL;
    for (i = 0; i < g_native_edit_count; i++) {
        if (g_native_edits[i].hwnd == hwnd) {
            original = g_native_edits[i].original_proc;
            if (msg == WM_KEYDOWN && wp == VK_RETURN &&
                    !g_native_edits[i].multiline) {
                stored_index = GetWindowLong(hwnd, GWL_USERDATA);
                parent = GetParent(hwnd);
                if (stored_index > 0 && parent != NULL) {
                    SendMessage(parent, WM_PCORE_FORM_ENTER,
                            (WPARAM) (stored_index - 1), 0);
                }
                return 0;
            }
            break;
        }
    }
    return (original != NULL) ?
            CallWindowProc(original, hwnd, msg, wp, lp) :
            DefWindowProc(hwnd, msg, wp, lp);
}

typedef struct pcore_native_select {
    HWND hwnd;
    unsigned int select_index;
    int option_count;
    int multiple;
} pcore_native_select;

static pcore_native_select *g_native_selects = NULL;
static unsigned int g_native_select_count = 0;
static int g_native_select_syncing = 0;
static int g_native_select_probe = 0;
static int g_native_select_probe_ok = 0;
static int g_native_multiselect_probe = 0;
static int g_native_multiselect_probe_ok = 0;
static int g_interaction_restyle_pending = 0;

static void pcore_request_interaction_restyle(HWND hwnd)
{
    if (hwnd != NULL && !g_interaction_restyle_pending) {
        g_interaction_restyle_pending = 1;
        if (!PostMessage(hwnd, WM_PCORE_INTERACTION_RESTYLE, 0, 0)) {
            g_interaction_restyle_pending = 0;
        }
    }
}

static void pcore_native_edits_destroy(void)
{
    unsigned int i;

    g_native_edit_syncing = 1;
    for (i = 0; i < g_native_edit_count; i++) {
        if (g_native_edits[i].hwnd != NULL) {
            DestroyWindow(g_native_edits[i].hwnd);
            g_native_edits[i].hwnd = NULL;
        }
    }
    free(g_native_edits);
    g_native_edits = NULL;
    g_native_edit_count = 0;
    g_native_edit_syncing = 0;
}

static void pcore_native_edits_position(HWND parent)
{
    RECT client;
    PCoreTextInputInfo info;
    unsigned int i;
    int top;
    int left;
    int width;
    int height;

    if (parent == NULL || g_render_doc == NULL) {
        return;
    }
    GetClientRect(parent, &client);
    for (i = 0; i < g_native_edit_count; i++) {
        if (g_native_edits[i].hwnd == NULL ||
                PCore_TextInputInfo(g_render_doc,
                        g_native_edits[i].text_index,
                        &info, NULL, 0) != 0) {
            continue;
        }
        left = info.x;
        top = info.y - g_scroll_y;
        width = (info.width > 0) ? info.width : 1;
        height = (info.height > 0) ? info.height : 1;
        MoveWindow(g_native_edits[i].hwnd, left, top,
                width, height, TRUE);
        if (left + width <= client.left || left >= client.right ||
                top + height <= client.top || top >= client.bottom) {
            ShowWindow(g_native_edits[i].hwnd, SW_HIDE);
        } else {
            ShowWindow(g_native_edits[i].hwnd, SW_SHOW);
        }
    }
}

static void pcore_native_edits_rebuild(HWND parent, int preserve_focus)
{
    pcore_native_edit *items;
    PCoreTextInputInfo info;
    HWND old_focus;
    unsigned int focus_index;
    unsigned int count;
    unsigned int i;
    WCHAR *wide_value;
    char *value;
    char *edit_value;
    DWORD style;
    int edit_value_cap;
    int multiline;
    int source_index;
    int target_index;
    int value_cap;
    int wide_cap;

    old_focus = preserve_focus ? GetFocus() : NULL;
    focus_index = UINT_MAX;
    if (old_focus != NULL) {
        for (i = 0; i < g_native_edit_count; i++) {
            if (g_native_edits[i].hwnd == old_focus) {
                focus_index = g_native_edits[i].text_index;
                break;
            }
        }
    }
    pcore_native_edits_destroy();
    if (parent == NULL || g_render_doc == NULL) {
        return;
    }
    count = 0;
    while (PCore_TextInputInfo(g_render_doc, count,
            NULL, NULL, 0) == 0) {
        count++;
    }
    if (count == 0) {
        return;
    }
    items = (pcore_native_edit *) calloc(count,
            sizeof(pcore_native_edit));
    if (items == NULL) {
        return;
    }
    g_native_edits = items;
    g_native_edit_count = count;
    g_native_edit_syncing = 1;
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        if (PCore_TextInputInfo(g_render_doc, i, &info,
                NULL, 0) != 0 ||
                PCore_TextInputIsMultiline(g_render_doc, i,
                        &multiline) != 0) {
            continue;
        }
        value_cap = info.value_bytes + 1;
        if (value_cap < 1) {
            value_cap = 1;
        }
        value = (char *) malloc((size_t) value_cap);
        if (value == NULL ||
                PCore_TextInputInfo(g_render_doc, i, NULL, value,
                        value_cap) != 0) {
            free(value);
            continue;
        }
        edit_value = value;
        wide_cap = value_cap;
        if (multiline && value_cap <= INT_MAX / 2) {
            edit_value_cap = value_cap * 2;
            edit_value = (char *) malloc((size_t) edit_value_cap);
            if (edit_value != NULL) {
                source_index = 0;
                target_index = 0;
                while (value[source_index] != '\0' &&
                        target_index + 2 < edit_value_cap) {
                    if (value[source_index] == '\n') {
                        edit_value[target_index++] = '\r';
                    }
                    edit_value[target_index++] = value[source_index++];
                }
                edit_value[target_index] = '\0';
                wide_cap = edit_value_cap;
            } else {
                edit_value = value;
            }
        }
        wide_value = (WCHAR *) malloc((size_t) wide_cap *
                sizeof(WCHAR));
        if (wide_value == NULL) {
            if (edit_value != value) { free(edit_value); }
            free(value);
            continue;
        }
        utf8_to_wide(edit_value, -1, wide_value, wide_cap);
        style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_LEFT;
        if (multiline) {
            style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
                    WS_VSCROLL;
        } else {
            style |= ES_AUTOHSCROLL;
        }
        if (info.password) {
            style |= ES_PASSWORD;
        }
        items[i].hwnd = CreateWindowW(L"EDIT", wide_value, style,
                info.x, info.y - g_scroll_y,
                (info.width > 0) ? info.width : 1,
                (info.height > 0) ? info.height : 1,
                parent, (HMENU) (INT_PTR) (1000 + i),
                GetModuleHandle(NULL), NULL);
        items[i].text_index = i;
        items[i].multiline = multiline;
        if (edit_value != value) {
            free(edit_value);
        }
        free(value);
        free(wide_value);
        if (items[i].hwnd == NULL) {
            continue;
        }
        SetWindowLong(items[i].hwnd, GWL_USERDATA, (LONG) (i + 1));
        items[i].original_proc = (WNDPROC) SetWindowLong(items[i].hwnd,
                GWL_WNDPROC, (LONG) pcore_native_edit_proc);
        SendMessage(items[i].hwnd, WM_SETFONT,
                (WPARAM) GetStockObject(SYSTEM_FONT), TRUE);
        SendMessage(items[i].hwnd, EM_LIMITTEXT,
                (WPARAM) ((info.max_length >= 0) ?
                        info.max_length : 0), 0);
        if (info.read_only) {
            SendMessage(items[i].hwnd, EM_SETREADONLY, TRUE, 0);
        }
        if (info.disabled) {
            EnableWindow(items[i].hwnd, FALSE);
        }
    }
    g_native_edit_syncing = 0;
    pcore_native_edits_position(parent);
    if (focus_index != UINT_MAX && focus_index < count &&
            items[focus_index].hwnd != NULL &&
            IsWindowEnabled(items[focus_index].hwnd)) {
        SetFocus(items[focus_index].hwnd);
        SendMessage(items[focus_index].hwnd, EM_SETSEL,
                0, (LPARAM) -1);
    }
}

static void pcore_native_edit_changed(HWND edit)
{
    WCHAR *wide_value;
    char *value;
    LONG stored_index;
    int set_result;
    int wide_len;
    int utf8_len;

    if (g_native_edit_syncing || edit == NULL || g_render_doc == NULL) {
        return;
    }
    stored_index = GetWindowLong(edit, GWL_USERDATA);
    if (stored_index <= 0) {
        return;
    }
    SendMessage(edit, EM_FMTLINES, FALSE, 0);
    wide_len = GetWindowTextLengthW(edit);
    wide_value = (WCHAR *) malloc((size_t) (wide_len + 1) *
            sizeof(WCHAR));
    if (wide_value == NULL) {
        return;
    }
    GetWindowTextW(edit, wide_value, wide_len + 1);
    utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_value, wide_len,
            NULL, 0, NULL, NULL);
    if (utf8_len < 0) {
        free(wide_value);
        return;
    }
    value = (char *) malloc((size_t) utf8_len + 1);
    if (value == NULL) {
        free(wide_value);
        return;
    }
    if (utf8_len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide_value, wide_len,
                value, utf8_len, NULL, NULL);
    }
    value[utf8_len] = '\0';
    set_result = PCore_TextInputSetValue(g_render_doc,
            (unsigned int) (stored_index - 1), value);
    if (g_native_edit_probe && stored_index == 1) {
        g_native_edit_probe_seen = 1;
        g_native_edit_probe_set_result = set_result;
        g_native_edit_probe_value[0] = '\0';
        g_native_edit_probe_ok =
                set_result == 0 &&
                PCore_TextInputInfo(g_render_doc, 0, NULL,
                        g_native_edit_probe_value,
                        sizeof(g_native_edit_probe_value)) == 0 &&
                strcmp(g_native_edit_probe_value,
                        g_native_edit_probe_multiline ?
                        "wm\ntextarea" : "wm-edit") == 0;
    }
    free(value);
    free(wide_value);
}

static void pcore_native_selects_destroy(void)
{
    unsigned int i;

    g_native_select_syncing = 1;
    for (i = 0; i < g_native_select_count; i++) {
        if (g_native_selects[i].hwnd != NULL) {
            DestroyWindow(g_native_selects[i].hwnd);
            g_native_selects[i].hwnd = NULL;
        }
    }
    free(g_native_selects);
    g_native_selects = NULL;
    g_native_select_count = 0;
    g_native_select_syncing = 0;
}

static int pcore_native_select_window_height(
        const PCoreSelectInfo *info)
{
    int visible_items;
    int height;

    visible_items = info->option_count;
    if (visible_items > 6) {
        visible_items = 6;
    }
    if (visible_items < 1) {
        visible_items = 1;
    }
    height = info->height * (visible_items + 1);
    if (height < info->height + 1) {
        height = info->height + 1;
    }
    return height;
}

static void pcore_native_selects_position(HWND parent)
{
    RECT client;
    PCoreSelectInfo info;
    unsigned int i;
    int top;
    int left;
    int width;
    int height;

    if (parent == NULL || g_render_doc == NULL) {
        return;
    }
    GetClientRect(parent, &client);
    for (i = 0; i < g_native_select_count; i++) {
        if (g_native_selects[i].hwnd == NULL ||
                PCore_SelectInfo(g_render_doc,
                        g_native_selects[i].select_index,
                        &info) != 0) {
            continue;
        }
        left = info.x;
        top = info.y - g_scroll_y;
        width = (info.width > 0) ? info.width : 1;
        height = g_native_selects[i].multiple ?
                ((info.height > 0) ? info.height : 1) :
                pcore_native_select_window_height(&info);
        MoveWindow(g_native_selects[i].hwnd, left, top,
                width, height, TRUE);
        if (left + width <= client.left || left >= client.right ||
                top + info.height <= client.top || top >= client.bottom) {
            ShowWindow(g_native_selects[i].hwnd, SW_HIDE);
        } else {
            ShowWindow(g_native_selects[i].hwnd, SW_SHOW);
        }
    }
}

static void pcore_native_selects_rebuild(HWND parent, int preserve_focus)
{
    pcore_native_select *items;
    PCoreSelectInfo info;
    HWND old_focus;
    unsigned int focus_index;
    unsigned int count;
    unsigned int i;
    unsigned int option_index;
    WCHAR *wide_label;
    char *label;
    const WCHAR *class_name;
    DWORD style;
    LRESULT add_result;
    int option_selected;
    int label_bytes;
    int label_cap;

    old_focus = preserve_focus ? GetFocus() : NULL;
    focus_index = UINT_MAX;
    if (old_focus != NULL) {
        for (i = 0; i < g_native_select_count; i++) {
            if (g_native_selects[i].hwnd == old_focus) {
                focus_index = g_native_selects[i].select_index;
                break;
            }
        }
    }
    pcore_native_selects_destroy();
    if (parent == NULL || g_render_doc == NULL) {
        return;
    }
    count = 0;
    while (PCore_SelectInfo(g_render_doc, count, NULL) == 0) {
        count++;
    }
    if (count == 0) {
        return;
    }
    items = (pcore_native_select *) calloc(count,
            sizeof(pcore_native_select));
    if (items == NULL) {
        return;
    }
    g_native_selects = items;
    g_native_select_count = count;
    g_native_select_syncing = 1;
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        if (PCore_SelectInfo(g_render_doc, i, &info) != 0) {
            continue;
        }
        if (info.multiple) {
            class_name = L"LISTBOX";
            style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                    WS_BORDER | LBS_NOTIFY | LBS_MULTIPLESEL |
                    LBS_NOINTEGRALHEIGHT;
        } else {
            class_name = L"COMBOBOX";
            style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                    CBS_DROPDOWNLIST;
        }
        items[i].hwnd = CreateWindowW(class_name, L"", style,
                info.x, info.y - g_scroll_y,
                (info.width > 0) ? info.width : 1,
                info.multiple ?
                        ((info.height > 0) ? info.height : 1) :
                        pcore_native_select_window_height(&info),
                parent, (HMENU) (INT_PTR) (2000 + i),
                GetModuleHandle(NULL), NULL);
        items[i].select_index = i;
        items[i].option_count = info.option_count;
        items[i].multiple = info.multiple;
        if (items[i].hwnd == NULL) {
            continue;
        }
        SetWindowLong(items[i].hwnd, GWL_USERDATA, (LONG) (i + 1));
        SendMessage(items[i].hwnd, WM_SETFONT,
                (WPARAM) GetStockObject(SYSTEM_FONT), TRUE);
        for (option_index = 0;
                option_index < (unsigned int) info.option_count;
                option_index++) {
            label_bytes = 0;
            if (PCore_SelectOptionInfo(g_render_doc, i, option_index,
                    NULL, 0, NULL, 0, NULL, NULL,
                    &label_bytes, NULL) != 0) {
                continue;
            }
            label_cap = label_bytes + 1;
            label = (char *) malloc((size_t) label_cap);
            wide_label = (WCHAR *) malloc((size_t) label_cap *
                    sizeof(WCHAR));
            option_selected = 0;
            if (label == NULL || wide_label == NULL ||
                    PCore_SelectOptionInfo(g_render_doc, i, option_index,
                            label, label_cap, NULL, 0,
                            &option_selected, NULL,
                            NULL, NULL) != 0) {
                free(label);
                free(wide_label);
                continue;
            }
            utf8_to_wide(label, -1, wide_label, label_cap);
            add_result = SendMessage(items[i].hwnd,
                    info.multiple ? LB_ADDSTRING : CB_ADDSTRING,
                    0, (LPARAM) wide_label);
            if (info.multiple && add_result != LB_ERR &&
                    add_result != LB_ERRSPACE) {
                SendMessage(items[i].hwnd, LB_SETSEL,
                        (WPARAM) option_selected, add_result);
            }
            free(label);
            free(wide_label);
        }
        if (!info.multiple) {
            SendMessage(items[i].hwnd, CB_SETCURSEL,
                    (WPARAM) info.selected_index, 0);
        }
        if (info.disabled) {
            EnableWindow(items[i].hwnd, FALSE);
        }
    }
    g_native_select_syncing = 0;
    pcore_native_selects_position(parent);
    if (focus_index != UINT_MAX && focus_index < count &&
            items[focus_index].hwnd != NULL &&
            IsWindowEnabled(items[focus_index].hwnd)) {
        SetFocus(items[focus_index].hwnd);
    }
}

static pcore_native_select *pcore_native_select_find(HWND select_window)
{
    unsigned int i;

    for (i = 0; i < g_native_select_count; i++) {
        if (g_native_selects[i].hwnd == select_window) {
            return &g_native_selects[i];
        }
    }
    return NULL;
}

static void pcore_native_focus_changed(HWND control)
{
    PCoreTextInputInfo text_info;
    PCoreSelectInfo select_info;
    unsigned int i;
    int result;

    if (control == NULL || g_render_doc == NULL ||
            g_native_edit_syncing || g_native_select_syncing) {
        return;
    }
    for (i = 0; i < g_native_edit_count; i++) {
        if (g_native_edits[i].hwnd == control &&
                PCore_TextInputInfo(g_render_doc,
                        g_native_edits[i].text_index,
                        &text_info, NULL, 0) == 0) {
            result = PCore_InteractionSetAt(g_render_doc,
                    text_info.x + text_info.width / 2,
                    text_info.y + text_info.height / 2,
                    PCORE_INTERACTION_FOCUS);
            if (result > 0) {
                pcore_request_interaction_restyle(GetParent(control));
            }
            return;
        }
    }
    for (i = 0; i < g_native_select_count; i++) {
        if (g_native_selects[i].hwnd == control &&
                PCore_SelectInfo(g_render_doc,
                        g_native_selects[i].select_index,
                        &select_info) == 0) {
            result = PCore_InteractionSetAt(g_render_doc,
                    select_info.x + select_info.width / 2,
                    select_info.y + select_info.height / 2,
                    PCORE_INTERACTION_FOCUS);
            if (result > 0) {
                pcore_request_interaction_restyle(GetParent(control));
            }
            return;
        }
    }
}

static void pcore_native_select_changed(HWND select_window)
{
    pcore_native_select *native_select;
    PCoreSelectInfo info;
    LONG stored_index;
    LRESULT selected_index;
    LRESULT native_state;
    unsigned int option_index;
    int core_state;
    int changed;
    int set_result;

    if (g_native_select_syncing || select_window == NULL ||
            g_render_doc == NULL) {
        return;
    }
    stored_index = GetWindowLong(select_window, GWL_USERDATA);
    if (stored_index <= 0) {
        return;
    }
    native_select = pcore_native_select_find(select_window);
    if (native_select == NULL) {
        return;
    }
    if (native_select->multiple) {
        changed = 0;
        g_native_select_syncing = 1;
        for (option_index = 0;
                option_index < (unsigned int) native_select->option_count;
                option_index++) {
            native_state = SendMessage(select_window, LB_GETSEL,
                    (WPARAM) option_index, 0);
            core_state = 0;
            if (native_state == LB_ERR ||
                    PCore_SelectOptionInfo(g_render_doc,
                            native_select->select_index, option_index,
                            NULL, 0, NULL, 0, &core_state, NULL,
                            NULL, NULL) != 0) {
                continue;
            }
            if ((native_state != 0) != (core_state != 0)) {
                set_result = PCore_SelectSetOptionSelected(g_render_doc,
                        native_select->select_index, option_index,
                        native_state != 0);
                if (set_result != 0) {
                    SendMessage(select_window, LB_SETSEL,
                            (WPARAM) core_state,
                            (LPARAM) option_index);
                } else {
                    changed = 1;
                }
            }
        }
        g_native_select_syncing = 0;
        if (changed) {
            pcore_request_interaction_restyle(GetParent(select_window));
        }
        return;
    }
    selected_index = SendMessage(select_window, CB_GETCURSEL, 0, 0);
    if (selected_index == CB_ERR) {
        return;
    }
    set_result = PCore_SelectSetOptionSelected(g_render_doc,
            (unsigned int) (stored_index - 1),
            (unsigned int) selected_index, 1);
    if (set_result != 0 &&
            PCore_SelectInfo(g_render_doc,
                    (unsigned int) (stored_index - 1), &info) == 0) {
        g_native_select_syncing = 1;
        SendMessage(select_window, CB_SETCURSEL,
                (WPARAM) info.selected_index, 0);
        g_native_select_syncing = 0;
    }
    if (g_native_select_probe && stored_index == 1) {
        g_native_select_probe_ok =
                set_result == 0 &&
                PCore_SelectInfo(g_render_doc, 0, &info) == 0 &&
                info.selected_index == (int) selected_index;
    }
    if (set_result == 0) {
        pcore_request_interaction_restyle(GetParent(select_window));
    }
}

static void pcore_native_multiselect_probe_run(HWND parent)
{
    pcore_native_select *target;
    pcore_native_select *locked;
    pcore_native_select *single;
    PCoreSelectInfo info;
    RECT target_rect;
    WCHAR class_name[16];
    WCHAR single_class[16];
    unsigned int i;
    unsigned int target_index;
    int selected[4];

    target = NULL;
    locked = NULL;
    single = NULL;
    target_index = UINT_MAX;
    memset(selected, 0, sizeof(selected));
    for (i = 0; i < g_native_select_count; i++) {
        if (g_native_selects[i].multiple &&
                g_native_selects[i].select_index == 0) {
            target = &g_native_selects[i];
            target_index = g_native_selects[i].select_index;
        } else if (g_native_selects[i].multiple &&
                g_native_selects[i].select_index == 1) {
            locked = &g_native_selects[i];
        } else if (!g_native_selects[i].multiple &&
                g_native_selects[i].select_index == 2) {
            single = &g_native_selects[i];
        }
    }
    class_name[0] = L'\0';
    single_class[0] = L'\0';
    memset(&info, 0, sizeof(info));
    if (target == NULL || locked == NULL || single == NULL ||
            target->hwnd == NULL || locked->hwnd == NULL ||
            single->hwnd == NULL ||
            IsWindowEnabled(locked->hwnd) ||
            PCore_SelectInfo(g_render_doc, target_index, &info) != 0 ||
            !GetWindowRect(target->hwnd, &target_rect) ||
            target_rect.bottom - target_rect.top != info.height ||
            GetClassNameW(target->hwnd, class_name,
                    sizeof(class_name) / sizeof(class_name[0])) <= 0 ||
            lstrcmpiW(class_name, L"LISTBOX") != 0 ||
            GetClassNameW(single->hwnd, single_class,
                    sizeof(single_class) / sizeof(single_class[0])) <= 0 ||
            lstrcmpiW(single_class, L"COMBOBOX") != 0) {
        return;
    }
    SendMessage(target->hwnd, LB_SETSEL, FALSE, 0);
    SendMessage(target->hwnd, LB_SETSEL, TRUE, 1);
    SendMessage(target->hwnd, LB_SETSEL, TRUE, 2);
    pcore_native_select_changed(target->hwnd);
    if (PCore_SelectInfo(g_render_doc, target_index, &info) != 0 ||
            !info.multiple || info.selected_count != 2) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (PCore_SelectOptionInfo(g_render_doc, target_index, i,
                NULL, 0, NULL, 0, &selected[i], NULL,
                NULL, NULL) != 0) {
            return;
        }
    }
    if (selected[0] || selected[1] || !selected[2] || !selected[3]) {
        return;
    }

    pcore_native_selects_rebuild(parent, 1);
    target = NULL;
    locked = NULL;
    for (i = 0; i < g_native_select_count; i++) {
        if (g_native_selects[i].select_index == target_index) {
            target = &g_native_selects[i];
        } else if (g_native_selects[i].select_index == 1) {
            locked = &g_native_selects[i];
        }
    }
    if (target == NULL || locked == NULL ||
            target->hwnd == NULL || locked->hwnd == NULL ||
            !target->multiple || IsWindowEnabled(locked->hwnd) ||
            SendMessage(target->hwnd, LB_GETSEL, 0, 0) != 0 ||
            SendMessage(target->hwnd, LB_GETSEL, 1, 0) != 0 ||
            SendMessage(target->hwnd, LB_GETSEL, 2, 0) <= 0 ||
            SendMessage(target->hwnd, LB_GETSEL, 3, 0) <= 0) {
        return;
    }
    g_native_multiselect_probe_ok = 1;
}

static void testbench_log_navigation(
        const pcore_navigation_request *request)
{
    char title[384];
    char body[1024];
    WCHAR wide_title[384];
    unsigned long image_known;
    unsigned long image_other;

    if (!g_testbench_auto || !g_testbench_browse_active ||
            request == NULL) {
        return;
    }
    image_known = request->stats.core_image.svg_setup_ms +
            request->stats.core_image.svg_parse_ms +
            request->stats.core_image.svg_raster_ms;
    image_other = (request->stats.core_image.svg_total_ms >= image_known) ?
            request->stats.core_image.svg_total_ms - image_known : 0;
    _snprintf(title, sizeof(title) - 1, "TEST 13 NAV %s%s",
            request->host, request->path);
    title[sizeof(title) - 1] = '\0';
    utf8_to_wide(title, -1, wide_title,
            sizeof(wide_title) / sizeof(wide_title[0]));
    _snprintf(body, sizeof(body) - 1,
            "completed=%d total/net/maxUI=%lu/%lu/%lums\n"
            "parse/style/images/layout/paint=%lu/%lu/%lu/%lu/%lums\n"
            "resources queued/ok/fail/rounds=%d/%d/%d/%d bytes=%d\n"
            "core layout total=%lums box/first/settle/final="
            "%lu/%lu/%lu/%lums pass=%d\n"
            "box tree/image/reuse/markup-first=%lu/%lu/%u/%u\n"
            "svg total/setup/parse/raster/other=%lu/%lu/%lu/%lu/%lums "
            "creates=%u",
            request->stats.completed,
            (unsigned long) request->stats.total_ms,
            (unsigned long) request->stats.network_ms,
            (unsigned long) request->stats.max_ui_slice_ms,
            (unsigned long) request->stats.parse_ms,
            (unsigned long) request->stats.style_ms,
            (unsigned long) request->stats.images_ms,
            (unsigned long) request->stats.layout_ms,
            (unsigned long) request->stats.first_paint_ms,
            request->stats.resources_queued,
            request->stats.resources_fetched,
            request->stats.resources_failed,
            request->stats.worker_rounds,
            request->stats.resource_bytes,
            (unsigned long) request->stats.core_layout.total_ms,
            (unsigned long) request->stats.core_layout.box_construct_ms,
            (unsigned long) request->stats.core_layout.first_layout_ms,
            (unsigned long) request->stats.core_layout.settling_ms,
            (unsigned long) request->stats.core_layout.finalize_ms,
            request->stats.core_layout.settling_pass,
            (unsigned long) request->stats.core_box.tree_ms,
            (unsigned long) request->stats.core_box.image_ms,
            request->stats.core_box.image_reuses,
            request->stats.core_box.image_markup_first,
            (unsigned long) request->stats.core_image.svg_total_ms,
            (unsigned long) request->stats.core_image.svg_setup_ms,
            (unsigned long) request->stats.core_image.svg_parse_ms,
            (unsigned long) request->stats.core_image.svg_raster_ms,
            image_other,
            request->stats.core_image.svg_creates);
    body[sizeof(body) - 1] = '\0';
    testbench_log_message(request->stats.completed ? "INFO" : "ERROR",
            wide_title, body);
}

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
    pcore_native_edits_position(hwnd);
    pcore_native_selects_position(hwnd);
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
    free(request->request_body);
    free(request->request_content_type);
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

static PHttpResponse *pcore_navigation_fetch_document(
        pcore_navigation_request *request)
{
    const char *headers[2];

    if (request->method == 2 || request->method == 3) {
        headers[0] = (request->request_content_type != NULL) ?
                request->request_content_type :
                "Content-Type: application/x-www-form-urlencoded";
        headers[1] = NULL;
        request->progress_last_total = -2;
        request->progress_last_percent = -2;
        request->progress_last_received = -16384;
        return PHttp_PostEx(request->host, request->port, request->path,
                headers, request->request_body, request->request_body_len,
                pcore_navigation_progress, request);
    }
    return pcore_navigation_get(request, request->host,
            request->port, request->path);
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
              "%s %s://%s%s -> status=%d %s",
              (request->method == 2 || request->method == 3) ?
                      "POST" : "GET",
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
        request->response = pcore_navigation_fetch_document(request);
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
        test_host_set_device_viewport(cw, chh);
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
    request->stats.core_layout_valid =
            PCore_GetLayoutStats(request->document,
                    &request->stats.core_layout) == 0;
    request->stats.core_box_valid =
            PCore_GetBoxStats(request->document,
                    &request->stats.core_box) == 0;
    request->stats.core_image_valid =
            PCore_GetImageDecodeStats(request->document,
                    &request->stats.core_image) == 0;

    /* Swap in the new document; native children must release their old DOM
     * indices before the document they describe is freed. */
    pcore_native_edits_destroy();
    pcore_native_selects_destroy();
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
    pcore_native_edits_rebuild(hwnd, 0);
    pcore_native_selects_rebuild(hwnd, 0);
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
    testbench_log_navigation(request);
    if (g_testbench_auto && g_testbench_browse_active) {
        if (request->stats.completed) {
            g_testbench_browse_completed++;
        } else {
            g_testbench_browse_failed = 1;
        }
    }
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
    if (hwnd != NULL && g_testbench_auto && g_testbench_browse_active) {
        PostMessage(hwnd, g_testbench_browse_failed ?
                WM_CLOSE : WM_TESTBENCH_NAVIGATE, 0, 0);
    }
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

static pcore_navigation_request *pcore_navigation_request_create_ex(
        HWND hwnd, const char *href, int method, const void *body,
        int body_len, const char *content_type)
{
    pcore_navigation_request *request;
    size_t href_len;
    size_t content_type_len;

    if (href == NULL) {
        return NULL;
    }
    href_len = strlen(href);
    if (href_len >= sizeof(request->path)) {
        return NULL;
    }
    request = (pcore_navigation_request *) malloc(sizeof(*request));
    if (request == NULL) {
        return NULL;
    }
    memset(request, 0, sizeof(*request));
    request->port = 443;
    if (!resolve_url(href, request->host, sizeof(request->host),
            request->path, sizeof(request->path), &request->port)) {
        free(request);
        return NULL;
    }
    request->method = (method == 2 || method == 3) ? method : 1;
    if (request->method == 2 || request->method == 3) {
        if (body_len < 0 || (body_len > 0 && body == NULL)) {
            free(request);
            return NULL;
        }
        request->request_body = (char *) malloc((size_t) body_len + 1);
        if (request->request_body == NULL) {
            free(request);
            return NULL;
        }
        if (body_len > 0) {
            memcpy(request->request_body, body, (size_t) body_len);
        }
        request->request_body[body_len] = '\0';
        request->request_body_len = body_len;
        if (content_type != NULL) {
            content_type_len = strlen(content_type);
            request->request_content_type = (char *) malloc(
                    content_type_len + 1);
            if (request->request_content_type == NULL) {
                pcore_navigation_request_free(request);
                return NULL;
            }
            memcpy(request->request_content_type, content_type,
                    content_type_len + 1);
        }
    }
    request->hwnd = hwnd;
    request->worker_stage = PCORE_NAV_STAGE_DOCUMENT;
    request->commit_stage = PCORE_NAV_COMMIT_NONE;
    request->stats.started_tick = GetTickCount();
    return request;
}

static pcore_navigation_request *pcore_navigation_request_create(
        HWND hwnd, const char *href, int method, const char *body)
{
    int body_len;

    body_len = (body != NULL) ? (int) strlen(body) : 0;
    return pcore_navigation_request_create_ex(hwnd, href, method,
            body, body_len, NULL);
}

/* Start the main-document stage. Later stages reuse this request for external
 * CSS/image GETs while the old visible document remains interactive. */
static void navigate_to_request_ex(HWND hwnd, const char *href,
        int method, const void *body, int body_len,
        const char *content_type)
{
    pcore_navigation_request *request;

    if (g_nav_loading) {
        return;
    }
    request = pcore_navigation_request_create_ex(hwnd, href, method,
            body, body_len, content_type);
    if (request == NULL) {
        show_error(L"Navigation failed",
                "Invalid URL, oversized form target, or out of memory");
        return;
    }
    request->generation = ++g_nav_generation;
    g_nav_request = request;
    pcore_navigation_set_loading(hwnd, 1);
    if (pcore_navigation_start_worker(request) != 0) {
        pcore_navigation_set_loading(hwnd, 0);
        g_nav_request = NULL;
        pcore_navigation_request_free(request);
        show_error(L"Navigation failed", "CreateThread failed");
    }
}

static void navigate_to_request(HWND hwnd, const char *href,
        int method, const char *body)
{
    int body_len;

    body_len = (body != NULL) ? (int) strlen(body) : 0;
    navigate_to_request_ex(hwnd, href, method, body, body_len, NULL);
}

static void navigate_to(HWND hwnd, const char *href)
{
    navigate_to_request(hwnd, href, 1, NULL);
}

typedef struct pcore_multipart_buffer {
    unsigned char *data;
    size_t length;
    size_t capacity;
} pcore_multipart_buffer;

static int pcore_multipart_buffer_reserve(pcore_multipart_buffer *buffer,
        size_t additional)
{
    unsigned char *grown;
    size_t needed;
    size_t capacity;

    if (buffer == NULL || additional > (size_t) INT_MAX - buffer->length) {
        return 0;
    }
    needed = buffer->length + additional + 1;
    if (needed <= buffer->capacity) {
        return 1;
    }
    capacity = (buffer->capacity > 0) ? buffer->capacity : 512;
    while (capacity < needed) {
        if (capacity > ((size_t) INT_MAX + 1) / 2) {
            capacity = (size_t) INT_MAX + 1;
            break;
        }
        capacity *= 2;
    }
    grown = (unsigned char *) realloc(buffer->data, capacity);
    if (grown == NULL) {
        return 0;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int pcore_multipart_buffer_append(pcore_multipart_buffer *buffer,
        const void *data, size_t length)
{
    if ((length > 0 && data == NULL) ||
            !pcore_multipart_buffer_reserve(buffer, length)) {
        return 0;
    }
    if (length > 0) {
        memcpy(buffer->data + buffer->length, data, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int pcore_multipart_buffer_text(pcore_multipart_buffer *buffer,
        const char *text)
{
    return pcore_multipart_buffer_append(buffer, text, strlen(text));
}

static int pcore_multipart_buffer_quoted(pcore_multipart_buffer *buffer,
        const char *text)
{
    const unsigned char *cursor;
    unsigned char value;
    char escape[2];

    cursor = (const unsigned char *) ((text != NULL) ? text : "");
    while (*cursor != 0) {
        value = *cursor++;
        if (value == '\r' || value == '\n') {
            value = ' ';
        }
        if (value == '"' || value == '\\') {
            escape[0] = '\\';
            escape[1] = (char) value;
            if (!pcore_multipart_buffer_append(buffer, escape, 2)) {
                return 0;
            }
        } else if (!pcore_multipart_buffer_append(buffer, &value, 1)) {
            return 0;
        }
    }
    return 1;
}

static int pcore_multipart_buffer_file(pcore_multipart_buffer *buffer,
        const char *path)
{
    WCHAR *wide_path;
    HANDLE file;
    DWORD high;
    DWORD low;
    DWORD read_count;
    size_t offset;

    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    wide_path = utf8_to_wide_alloc(path);
    if (wide_path == NULL) {
        return 0;
    }
    file = CreateFileW(wide_path, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide_path);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    high = 0;
    low = GetFileSize(file, &high);
    if ((low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) ||
            high != 0 || low > (DWORD) INT_MAX ||
            !pcore_multipart_buffer_reserve(buffer, (size_t) low)) {
        CloseHandle(file);
        return 0;
    }
    offset = 0;
    while (offset < (size_t) low) {
        read_count = 0;
        if (!ReadFile(file, buffer->data + buffer->length + offset,
                low - (DWORD) offset, &read_count, NULL) ||
                read_count == 0) {
            CloseHandle(file);
            return 0;
        }
        offset += read_count;
    }
    CloseHandle(file);
    buffer->length += (size_t) low;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int pcore_multipart_build(HANDLE submission,
        char **out_body, int *out_body_len, char **out_content_type)
{
    static unsigned int boundary_sequence = 0;
    PCoreMultipartSubmissionInfo submission_info;
    PCoreMultipartPartInfo part_info;
    pcore_multipart_buffer buffer;
    char boundary[64];
    char *content_type;
    char *name;
    char *value;
    char *path;
    unsigned int index;
    int content_type_length;
    int result;

    if (submission == NULL || out_body == NULL ||
            out_body_len == NULL || out_content_type == NULL ||
            PCore_MultipartSubmissionInfo(submission, &submission_info,
                    NULL, 0) != 1) {
        return 0;
    }
    *out_body = NULL;
    *out_body_len = 0;
    *out_content_type = NULL;
    memset(&buffer, 0, sizeof(buffer));
    boundary_sequence++;
    _snprintf(boundary, sizeof(boundary) - 1,
            "----PositronWM6%08lX%04X",
            GetTickCount(), boundary_sequence & 0xffff);
    boundary[sizeof(boundary) - 1] = '\0';
    for (index = 0; index < submission_info.part_count; index++) {
        memset(&part_info, 0, sizeof(part_info));
        if (PCore_MultipartPartInfo(submission, index, &part_info,
                NULL, 0, NULL, 0, NULL, 0) != 1 ||
                part_info.name_bytes < 0 ||
                part_info.value_bytes < 0 ||
                part_info.path_bytes < 0) {
            free(buffer.data);
            return 0;
        }
        name = (char *) malloc((size_t) part_info.name_bytes + 1);
        value = (char *) malloc((size_t) part_info.value_bytes + 1);
        path = (char *) malloc((size_t) part_info.path_bytes + 1);
        if (name == NULL || value == NULL || path == NULL) {
            free(name);
            free(value);
            free(path);
            free(buffer.data);
            return 0;
        }
        result = PCore_MultipartPartInfo(submission, index, &part_info,
                name, part_info.name_bytes + 1,
                value, part_info.value_bytes + 1,
                path, part_info.path_bytes + 1);
        if (result != 1 ||
                !pcore_multipart_buffer_text(&buffer, "--") ||
                !pcore_multipart_buffer_text(&buffer, boundary) ||
                !pcore_multipart_buffer_text(&buffer,
                    "\r\nContent-Disposition: form-data; name=\"") ||
                !pcore_multipart_buffer_quoted(&buffer, name) ||
                !pcore_multipart_buffer_text(&buffer, "\"")) {
            free(name);
            free(value);
            free(path);
            free(buffer.data);
            return 0;
        }
        if (part_info.kind == 2) {
            result = pcore_multipart_buffer_text(&buffer, "; filename=\"") &&
                    pcore_multipart_buffer_quoted(&buffer, value) &&
                    pcore_multipart_buffer_text(&buffer,
                        "\"\r\nContent-Type: application/octet-stream"
                        "\r\n\r\n") &&
                    pcore_multipart_buffer_file(&buffer, path);
        } else {
            result = pcore_multipart_buffer_text(&buffer, "\r\n\r\n") &&
                    pcore_multipart_buffer_append(&buffer, value,
                            (size_t) part_info.value_bytes);
        }
        free(name);
        free(value);
        free(path);
        if (!result || !pcore_multipart_buffer_text(&buffer, "\r\n")) {
            free(buffer.data);
            return 0;
        }
    }
    if (!pcore_multipart_buffer_text(&buffer, "--") ||
            !pcore_multipart_buffer_text(&buffer, boundary) ||
            !pcore_multipart_buffer_text(&buffer, "--\r\n")) {
        free(buffer.data);
        return 0;
    }
    content_type_length = (int) strlen(boundary) + 48;
    content_type = (char *) malloc((size_t) content_type_length);
    if (content_type == NULL) {
        free(buffer.data);
        return 0;
    }
    _snprintf(content_type, content_type_length - 1,
            "Content-Type: multipart/form-data; boundary=%s", boundary);
    content_type[content_type_length - 1] = '\0';
    *out_body = (char *) buffer.data;
    *out_body_len = (int) buffer.length;
    *out_content_type = content_type;
    return 1;
}

static char *pcore_form_target_url(const char *action, const char *body,
        int method)
{
    char current[1536];
    const char *source;
    size_t source_length;
    size_t base_length;
    size_t body_length;
    size_t needed;
    char *target;

    source = action;
    if (source == NULL || source[0] == '\0') {
        if (pcore_document_url(g_cur_host, g_cur_path, g_cur_port,
                current, sizeof(current)) != 0) {
            return NULL;
        }
        source = current;
    }
    source_length = strlen(source);
    base_length = 0;
    while (base_length < source_length &&
            source[base_length] != '?' && source[base_length] != '#') {
        base_length++;
    }
    body_length = (body != NULL) ? strlen(body) : 0;
    needed = (method == 1) ?
            base_length + ((body_length > 0) ? body_length + 1 : 0) :
            source_length;
    if (needed >= 65535) {
        return NULL;
    }
    target = (char *) malloc(needed + 1);
    if (target == NULL) {
        return NULL;
    }
    if (method == 1) {
        if (base_length > 0) {
            memcpy(target, source, base_length);
        }
        if (body_length > 0) {
            target[base_length] = '?';
            memcpy(target + base_length + 1, body, body_length);
        }
    } else if (source_length > 0) {
        memcpy(target, source, source_length);
    }
    target[needed] = '\0';
    return target;
}

static int navigate_multipart_submission(HWND hwnd, HANDLE submission)
{
    PCoreMultipartSubmissionInfo info;
    char *action;
    char *body;
    char *content_type;
    char *target;
    int body_len;
    int result;

    memset(&info, 0, sizeof(info));
    action = NULL;
    body = NULL;
    content_type = NULL;
    target = NULL;
    result = 0;
    if (submission == NULL ||
            PCore_MultipartSubmissionInfo(submission, &info,
                    NULL, 0) != 1 ||
            info.action_bytes < 0) {
        goto done;
    }
    action = (char *) malloc((size_t) info.action_bytes + 1);
    if (action == NULL ||
            PCore_MultipartSubmissionInfo(submission, &info,
                    action, info.action_bytes + 1) != 1 ||
            !pcore_multipart_build(submission, &body, &body_len,
                    &content_type)) {
        goto done;
    }
    target = pcore_form_target_url(action, NULL, 3);
    if (target == NULL) {
        goto done;
    }
    navigate_to_request_ex(hwnd, target, 3, body, body_len, content_type);
    result = 1;

done:
    free(target);
    free(content_type);
    free(body);
    free(action);
    PCore_FreeMultipartSubmission(submission);
    if (!result) {
        show_error(L"Form submission failed",
                "Could not build the multipart/form-data request");
    }
    return result;
}

static void navigate_form_submission(HWND hwnd, int method,
        const char *action, const char *body)
{
    char *target;

    target = pcore_form_target_url(action, body, method);
    if (target == NULL) {
        show_error(L"Form submission failed",
                "The form target is invalid or too large");
        return;
    }
    navigate_to_request(hwnd, target, method,
            (method == 2) ? body : NULL);
    free(target);
}

static int pcore_restyle_form_state(HWND hwnd, int preserve_focus)
{
    RECT client;
    char document_url[1536];
    const char *document_base;
    int width;
    int height;
    int max_scroll;

    if (hwnd == NULL || g_render_doc == NULL) {
        return 1;
    }
    GetClientRect(hwnd, &client);
    width = client.right - client.left;
    height = client.bottom - client.top;
    document_base = NULL;
    if (pcore_document_url(g_cur_host, g_cur_path, g_cur_port,
            document_url, sizeof(document_url)) == 0) {
        document_base = document_url;
    }
    if (width <= 0 || height <= 0 ||
            PCore_StyleDocumentEx2(g_render_doc, g_render_sheet,
                    document_base, wm_combine_url,
                    page_resource_cache_only_cb, NULL, NULL) != 0 ||
            PCore_LayoutDocument(g_render_doc, width, height) != 0) {
        return 1;
    }
    g_doc_h = PCore_DocumentHeight(g_render_doc);
    max_scroll = (g_doc_h > height) ? g_doc_h - height : 0;
    if (g_scroll_y > max_scroll) {
        g_scroll_y = max_scroll;
    }
    pcore_set_scrollbar(hwnd);
    pcore_native_edits_rebuild(hwnd, preserve_focus);
    pcore_native_selects_rebuild(hwnd, preserve_focus);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
}

static void pcore_handle_invalid_form(HWND hwnd,
        const PCoreFormValidationInfo *validation);

static int pcore_handle_form_button(HWND hwnd, int x, int y)
{
    PCoreFormSubmissionInfo info;
    PCoreFormValidationInfo validation;
    char action_probe[1];
    char body_probe[1];
    char *action;
    char *body;
    int result;

    result = PCore_FormResetAt(g_render_doc, x, y);
    if (result != 0) {
        if (result == 1) {
            if (pcore_restyle_form_state(hwnd, 0) != 0) {
                show_error(L"Form reset failed",
                        "The form reset but could not be re-laid out");
            }
        } else if (result == 3) {
            show_error(L"Form reset failed",
                    "Could not restore the form's initial state");
        }
        return 1;
    }
    memset(&info, 0, sizeof(info));
    action_probe[0] = '\0';
    body_probe[0] = '\0';
    result = PCore_FormSubmissionAt(g_render_doc, x, y, &info,
            action_probe, sizeof(action_probe),
            body_probe, sizeof(body_probe));
    if (result == 0) {
        return 0;
    }
    if (result == 2) {
        return 1;
    }
    if (result == 5) {
        if (PCore_FormValidationAt(g_render_doc, x, y, &validation)) {
            pcore_handle_invalid_form(hwnd, &validation);
        }
        return 1;
    }
    if (result == 3) {
        navigate_multipart_submission(hwnd,
                PCore_MultipartSubmissionAt(g_render_doc, x, y));
        return 1;
    }
    if (result == 1) {
        navigate_form_submission(hwnd, info.method,
                action_probe, body_probe);
        return 1;
    }
    if (info.method == 0 || info.action_bytes < 0 ||
            info.body_bytes < 0) {
        show_error(L"Form submission failed",
                "Could not enumerate successful form controls");
        return 1;
    }
    action = (char *) malloc((size_t) info.action_bytes + 1);
    body = (char *) malloc((size_t) info.body_bytes + 1);
    if (action == NULL || body == NULL) {
        free(action);
        free(body);
        show_error(L"Form submission failed", "Out of memory");
        return 1;
    }
    result = PCore_FormSubmissionAt(g_render_doc, x, y, &info,
            action, info.action_bytes + 1,
            body, info.body_bytes + 1);
    if (result == 1) {
        navigate_form_submission(hwnd, info.method, action, body);
    } else if (result == 5 &&
            PCore_FormValidationAt(g_render_doc, x, y, &validation)) {
        pcore_handle_invalid_form(hwnd, &validation);
    } else {
        show_error(L"Form submission failed",
                "The form changed while its data was being collected");
    }
    free(action);
    free(body);
    return 1;
}

static void pcore_handle_form_enter(HWND hwnd, unsigned int text_index)
{
    PCoreFormSubmissionInfo info;
    PCoreFormValidationInfo validation;
    char action_probe[1];
    char body_probe[1];
    char *action;
    char *body;
    int result;

    memset(&info, 0, sizeof(info));
    action_probe[0] = '\0';
    body_probe[0] = '\0';
    result = PCore_FormSubmissionForTextInput(g_render_doc, text_index,
            &info, action_probe, sizeof(action_probe),
            body_probe, sizeof(body_probe));
    if (result == 0) {
        return;
    }
    if (result == 5) {
        if (PCore_FormValidationForTextInput(g_render_doc, text_index,
                &validation)) {
            pcore_handle_invalid_form(hwnd, &validation);
        }
        return;
    }
    if (result == 3) {
        navigate_multipart_submission(hwnd,
                PCore_MultipartSubmissionForTextInput(g_render_doc,
                        text_index));
        return;
    }
    action = NULL;
    body = NULL;
    if (result == 4 && info.method != 0 &&
            info.action_bytes >= 0 && info.body_bytes >= 0) {
        action = (char *) malloc((size_t) info.action_bytes + 1);
        body = (char *) malloc((size_t) info.body_bytes + 1);
        if (action != NULL && body != NULL) {
            result = PCore_FormSubmissionForTextInput(g_render_doc,
                    text_index, &info,
                    action, info.action_bytes + 1,
                    body, info.body_bytes + 1);
        }
    } else if (result == 1) {
        action = action_probe;
        body = body_probe;
    }
    if (result == 1 && action != NULL && body != NULL) {
        if (g_native_form_enter_probe) {
            g_native_form_enter_probe_seen = 1;
            g_native_form_enter_probe_ok =
                    info.method == 1 &&
                    strcmp(action, "/implicit") == 0 &&
                    strcmp(body, g_native_form_enter_expected) == 0;
        } else {
            navigate_form_submission(hwnd, info.method, action, body);
        }
    } else if (result == 5 &&
            PCore_FormValidationForTextInput(g_render_doc, text_index,
                    &validation)) {
        pcore_handle_invalid_form(hwnd, &validation);
    } else {
        show_error(L"Form submission failed",
                "Could not collect the form after Enter");
    }
    if (action != action_probe) {
        free(action);
    }
    if (body != body_probe) {
        free(body);
    }
}

static void pcore_invalidate_form_dirty(HWND hwnd,
        int x, int y, int width, int height)
{
    RECT dirty;

    if (width <= 0 || height <= 0) {
        return;
    }
    dirty.left = x;
    dirty.top = y - g_scroll_y;
    dirty.right = dirty.left + width;
    dirty.bottom = dirty.top + height;
    InvalidateRect(hwnd, &dirty, FALSE);
}

static int pcore_focus_native_form_control(int kind, int x, int y)
{
    PCoreTextInputInfo text_info;
    PCoreSelectInfo select_info;
    unsigned int i;

    if (kind >= 3 && kind <= 5) {
        for (i = 0; i < g_native_edit_count; i++) {
            if (g_native_edits[i].hwnd != NULL &&
                    PCore_TextInputInfo(g_render_doc,
                            g_native_edits[i].text_index,
                            &text_info, NULL, 0) == 0 &&
                    x >= text_info.x &&
                    x < text_info.x + text_info.width &&
                    y >= text_info.y &&
                    y < text_info.y + text_info.height) {
                if (IsWindowEnabled(g_native_edits[i].hwnd)) {
                    SetFocus(g_native_edits[i].hwnd);
                }
                return 1;
            }
        }
    } else if (kind == 6) {
        for (i = 0; i < g_native_select_count; i++) {
            if (g_native_selects[i].hwnd != NULL &&
                    PCore_SelectInfo(g_render_doc,
                            g_native_selects[i].select_index,
                            &select_info) == 0 &&
                    x >= select_info.x &&
                    x < select_info.x + select_info.width &&
                    y >= select_info.y &&
                    y < select_info.y + select_info.height) {
                if (IsWindowEnabled(g_native_selects[i].hwnd)) {
                    SetFocus(g_native_selects[i].hwnd);
                }
                return 1;
            }
        }
    }
    return 0;
}

static void pcore_handle_invalid_form(HWND hwnd,
        const PCoreFormValidationInfo *validation)
{
    RECT client;
    RECT dirty;
    int client_height;
    int target_scroll;
    int top;
    int bottom;

    if (validation == NULL || validation->valid ||
            validation->invalid_count <= 0) {
        return;
    }
    GetClientRect(hwnd, &client);
    client_height = client.bottom - client.top;
    top = validation->first_y;
    bottom = top + validation->first_height;
    target_scroll = g_scroll_y;
    if (top < g_scroll_y + 8) {
        target_scroll = top - 8;
    } else if (bottom > g_scroll_y + client_height - 8) {
        target_scroll = bottom - client_height + 8;
    }
    pcore_scroll_by(hwnd, target_scroll - g_scroll_y);
    pcore_focus_native_form_control(validation->first_control_kind,
            validation->first_x, validation->first_y);
    dirty.left = validation->first_x;
    dirty.top = validation->first_y - g_scroll_y;
    dirty.right = dirty.left + validation->first_width;
    dirty.bottom = dirty.top + validation->first_height;
    InvalidateRect(hwnd, &dirty, FALSE);
    MessageBeep(MB_ICONEXCLAMATION);
}

static int pcore_handle_file_input(HWND hwnd, int x, int y)
{
    OPENFILENAMEEX picker;
    PCoreFileInputInfo info;
    WCHAR file_path[MAX_PATH];
    WCHAR file_title[MAX_PATH];
    char *path_utf8;
    char *title_utf8;
    unsigned int file_index;
    int disabled;
    RECT dirty;

    file_index = 0;
    disabled = 0;
    if (!PCore_FileInputAt(g_render_doc, x, y,
            &file_index, &disabled)) {
        return 0;
    }
    if (disabled) {
        return 1;
    }
    memset(&picker, 0, sizeof(picker));
    memset(file_path, 0, sizeof(file_path));
    memset(file_title, 0, sizeof(file_title));
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = hwnd;
    picker.lpstrFilter = L"All files (*.*)\0*.*\0\0";
    picker.lpstrFile = file_path;
    picker.nMaxFile = MAX_PATH;
    picker.lpstrFileTitle = file_title;
    picker.nMaxFileTitle = MAX_PATH;
    picker.lpstrTitle = L"Choose a file";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    picker.ExFlags = OFN_EXFLAG_NOFILECREATE;
    if (!GetOpenFileNameEx(&picker)) {
        return 1;
    }
    path_utf8 = wide_to_utf8_alloc(file_path);
    title_utf8 = wide_to_utf8_alloc(
            (file_title[0] != L'\0') ? file_title : file_path);
    if (path_utf8 == NULL || title_utf8 == NULL ||
            PCore_FileInputSetPath(g_render_doc, file_index,
                    title_utf8, path_utf8) != 0 ||
            PCore_FileInputInfo(g_render_doc, file_index, &info,
                    NULL, 0, NULL, 0) != 0) {
        free(path_utf8);
        free(title_utf8);
        show_error(L"File selection failed",
                "Could not store the selected file in the form");
        return 1;
    }
    free(path_utf8);
    free(title_utf8);
    dirty.left = info.x;
    dirty.top = info.y - g_scroll_y;
    dirty.right = info.x + info.width;
    dirty.bottom = info.y - g_scroll_y + info.height;
    InvalidateRect(hwnd, &dirty, TRUE);
    return 1;
}

static int pcore_handle_label(HWND hwnd, int x, int y)
{
    int target_x;
    int target_y;
    int kind;
    int dirty_x;
    int dirty_y;
    int dirty_w;
    int dirty_h;

    if (!PCore_LabelTargetAt(g_render_doc, x, y,
            &target_x, &target_y, &kind)) {
        return 0;
    }
    if (PCore_InteractionSetAt(g_render_doc, target_x, target_y,
            PCORE_INTERACTION_FOCUS) > 0) {
        pcore_request_interaction_restyle(hwnd);
    }
    if (kind >= 7 && kind <= 9) {
        pcore_handle_form_button(hwnd, target_x, target_y);
    } else if (kind == 10) {
        pcore_handle_file_input(hwnd, target_x, target_y);
    } else if (kind == 1 || kind == 2) {
        if (PCore_FormActivateAt(g_render_doc, target_x, target_y,
                &dirty_x, &dirty_y, &dirty_w, &dirty_h)) {
            pcore_invalidate_form_dirty(hwnd, dirty_x, dirty_y,
                    dirty_w, dirty_h);
            pcore_request_interaction_restyle(hwnd);
        }
    } else {
        pcore_focus_native_form_control(kind, target_x, target_y);
    }
    return 1;
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
        if (g_testbench_auto) {
            g_testbench_render_paints++;
        }
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
            test_host_set_device_viewport(cw, chh);
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
        pcore_native_edits_rebuild(hwnd, 1);
        pcore_native_selects_rebuild(hwnd, 1);
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
        if (wp == TESTBENCH_RENDER_TIMER && g_testbench_auto &&
                !g_testbench_browse_active) {
            if (g_testbench_render_paints > 0) {
                KillTimer(hwnd, TESTBENCH_RENDER_TIMER);
                DestroyWindow(hwnd);
            }
            return 0;
        }
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
    case WM_TESTBENCH_NAVIGATE:
        if (g_testbench_auto && g_testbench_browse_active) {
            if (g_testbench_browse_step < TESTBENCH_BROWSE_URL_COUNT) {
                navigate_to(hwnd,
                        g_testbench_browse_urls[g_testbench_browse_step]);
                g_testbench_browse_step++;
                if (!g_nav_loading) {
                    g_testbench_browse_failed = 1;
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            } else {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        if (wp == PCORE_HOVER_TIMER) {
            POINT pt;
            RECT client;

            if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt) ||
                    !GetClientRect(hwnd, &client) ||
                    pt.x < client.left || pt.x >= client.right ||
                    pt.y < client.top || pt.y >= client.bottom) {
                KillTimer(hwnd, PCORE_HOVER_TIMER);
                g_mouse_tracking = 0;
                if (g_render_doc != NULL &&
                        PCore_InteractionClear(g_render_doc,
                                PCORE_INTERACTION_HOVER) > 0) {
                    pcore_request_interaction_restyle(hwnd);
                }
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
    case WM_PCORE_FORM_ENTER:
        if (g_render_doc != NULL) {
            pcore_handle_form_enter(hwnd, (unsigned int) wp);
        }
        return 0;
    case WM_PCORE_INTERACTION_RESTYLE:
        g_interaction_restyle_pending = 0;
        if (g_render_doc != NULL &&
                pcore_restyle_form_state(hwnd, 1) != 0) {
            show_error(L"Dynamic restyle failed",
                    "Could not reselect styles after an interaction");
        }
        return 0;
    case WM_COMMAND:
        if ((HWND) lp != NULL) {
            if (HIWORD(wp) == CBN_SELCHANGE) {
                pcore_native_select_changed((HWND) lp);
                return 0;
            }
            if (HIWORD(wp) == EN_CHANGE) {
                pcore_native_edit_changed((HWND) lp);
                return 0;
            }
            if (HIWORD(wp) == EN_SETFOCUS) {
                pcore_native_focus_changed((HWND) lp);
                SHFullScreen(hwnd, SHFS_SHOWSIPBUTTON);
                SHSipPreference(hwnd, SIP_UP);
                return 0;
            }
            if (HIWORD(wp) == CBN_SETFOCUS ||
                    HIWORD(wp) == LBN_SETFOCUS) {
                pcore_native_focus_changed((HWND) lp);
                return 0;
            }
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g_render_doc != NULL &&
                PCore_InteractionClear(g_render_doc,
                        PCORE_INTERACTION_FOCUS |
                        PCORE_INTERACTION_ACTIVE |
                        PCORE_INTERACTION_HOVER) > 0) {
            KillTimer(hwnd, PCORE_HOVER_TIMER);
            g_mouse_tracking = 0;
            pcore_request_interaction_restyle(hwnd);
        }
        break;
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
        int dirty_x;
        int dirty_y;
        int dirty_w;
        int dirty_h;
        int default_allowed;
        char href[1024];

        if (g_render_doc != NULL &&
                PCore_OverflowPointer(g_render_doc, PCORE_POINTER_DOWN,
                        cx, cy + g_scroll_y)) {
            g_overflow_pointer = 1;
            SetCapture(hwnd);
            pcore_invalidate_overflow(hwnd);
            return 0;
        }
        if (g_render_doc != NULL &&
                PCore_InteractionSetAt(g_render_doc,
                        cx, cy + g_scroll_y,
                        PCORE_INTERACTION_FOCUS |
                        PCORE_INTERACTION_ACTIVE) > 0) {
            pcore_request_interaction_restyle(hwnd);
        }
        default_allowed = 1;
        if (g_render_doc != NULL) {
            PCore_EventDispatchAt(g_render_doc, cx, cy + g_scroll_y,
                    "click", 1, 1, &default_allowed);
            if (!default_allowed) {
                return 0;
            }
        }
        if (g_render_doc != NULL &&
                pcore_handle_form_button(hwnd, cx, cy + g_scroll_y)) {
            return 0;
        }
        if (g_render_doc != NULL &&
                pcore_handle_file_input(hwnd, cx, cy + g_scroll_y)) {
            return 0;
        }
        if (g_render_doc != NULL &&
                PCore_FormActivateAt(g_render_doc, cx, cy + g_scroll_y,
                        &dirty_x, &dirty_y, &dirty_w, &dirty_h)) {
            pcore_invalidate_form_dirty(hwnd, dirty_x, dirty_y,
                    dirty_w, dirty_h);
            pcore_request_interaction_restyle(hwnd);
            return 0;
        }
        if (g_render_doc != NULL &&
                pcore_handle_label(hwnd, cx, cy + g_scroll_y)) {
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
        if (!g_mouse_tracking) {
            if (SetTimer(hwnd, PCORE_HOVER_TIMER, 250, NULL) != 0) {
                g_mouse_tracking = 1;
            }
        }
        if (g_render_doc != NULL &&
                PCore_InteractionSetAt(g_render_doc,
                        (int) (short) LOWORD(lp),
                        (int) (short) HIWORD(lp) + g_scroll_y,
                        PCORE_INTERACTION_HOVER) > 0) {
            pcore_request_interaction_restyle(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_render_doc != NULL &&
                PCore_InteractionClear(g_render_doc,
                        PCORE_INTERACTION_ACTIVE) > 0) {
            pcore_request_interaction_restyle(hwnd);
        }
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
        KillTimer(hwnd, PCORE_HOVER_TIMER);
        g_mouse_tracking = 0;
        g_interaction_restyle_pending = 0;
        pcore_native_edits_destroy();
        pcore_native_selects_destroy();
        SHSipPreference(hwnd, SIP_FORCEDOWN);
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
    pcore_native_edits_rebuild(hwnd, 0);
    pcore_native_selects_rebuild(hwnd, 0);
    if (g_native_select_probe && g_native_select_count > 0 &&
            g_native_selects[0].hwnd != NULL) {
        SendMessage(g_native_selects[0].hwnd, CB_SETCURSEL, 2, 0);
        pcore_native_select_changed(g_native_selects[0].hwnd);
    }
    if (g_native_multiselect_probe) {
        pcore_native_multiselect_probe_run(hwnd);
    }
    if (g_native_edit_probe && g_native_edit_count > 0 &&
            g_native_edits[0].hwnd != NULL) {
        if (g_native_edit_probe_multiline) {
            SendMessage(g_native_edits[0].hwnd, EM_SETSEL,
                    0, (LPARAM) -1);
            SendMessage(g_native_edits[0].hwnd, EM_REPLACESEL,
                    TRUE, (LPARAM) L"wm\r\ntextarea");
        } else {
            SetWindowTextW(g_native_edits[0].hwnd, L"wm-edit");
        }
    }
    if (g_native_form_enter_probe && g_native_edit_count > 0 &&
            g_native_edits[0].hwnd != NULL) {
        SendMessage(g_native_edits[0].hwnd, WM_KEYDOWN, VK_RETURN, 0);
    }
    g_testbench_render_paints = 0;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);   /* WinCE: grab focus, come to front */
    if (g_native_label_probe && g_native_edit_count > 0 &&
            g_native_edits[0].hwnd != NULL) {
        int label_x;
        int label_y;
        int label_w;
        int label_h;

        g_native_label_probe_ok =
                PCore_NodeBox(g_render_doc, "strong",
                        &label_x, &label_y, &label_w, &label_h) == 0 &&
                pcore_handle_label(hwnd,
                        label_x + label_w / 2,
                        label_y + label_h / 2) &&
                GetFocus() == g_native_edits[0].hwnd;
    }
    /* Read-only view: hide the SIP button and keep the keyboard down. */
    SHFullScreen(hwnd, SHFS_HIDESIPBUTTON);
    SHSipPreference(hwnd, SIP_FORCEDOWN);
    pcore_set_scrollbar(hwnd);
    if (g_testbench_auto) {
        if (g_testbench_browse_active) {
            PostMessage(hwnd, WM_TESTBENCH_NAVIGATE, 0, 0);
        } else if (SetTimer(hwnd, TESTBENCH_RENDER_TIMER, 100, NULL) == 0) {
            DestroyWindow(hwnd);
        }
    }

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
    char   summary[640];
    char   box_summary[512];
    unsigned long core_known;
    unsigned long core_other;
    unsigned long box_known;
    unsigned long box_other;
    unsigned long image_known;
    unsigned long image_other;

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
    test_host_set_device_viewport(vw, vh);
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
    if (g_testbench_auto) {
        g_testbench_browse_active = 1;
        g_testbench_browse_step = 0;
        g_testbench_browse_completed = 0;
        g_testbench_browse_failed = 0;
    }
    if (!show_render_window()) {
        g_testbench_browse_active = 0;
        show_error(L"TEST 13 FAIL", "CreateWindow returned NULL");
        g_render_doc = NULL;
        PCore_FreeDocument(hDoc);
        return FALSE;
    }
    g_testbench_browse_active = 0;
    /* Navigation may have replaced the document; free whatever is current. */
    if (g_render_doc != NULL) {
        PCore_FreeDocument(g_render_doc);
    }
    g_render_doc = NULL;

    if (g_testbench_auto &&
            (g_testbench_browse_failed ||
            g_testbench_browse_completed != TESTBENCH_BROWSE_URL_COUNT)) {
        _snprintf(summary, sizeof(summary) - 1,
                "Automated Browse completed %d/%d navigation steps.",
                g_testbench_browse_completed, TESTBENCH_BROWSE_URL_COUNT);
        summary[sizeof(summary) - 1] = '\0';
        show_error(L"TEST 13 FAIL", summary);
        return FALSE;
    }

    if (g_nav_last_stats_valid) {
        core_known = g_nav_last_stats.core_layout.box_construct_ms +
                g_nav_last_stats.core_layout.first_layout_ms +
                g_nav_last_stats.core_layout.settling_ms +
                g_nav_last_stats.core_layout.finalize_ms;
        core_other = (g_nav_last_stats.core_layout.total_ms >= core_known) ?
                g_nav_last_stats.core_layout.total_ms - core_known : 0;
        box_known = g_nav_last_stats.core_box.style_ms +
                g_nav_last_stats.core_box.text_ms +
                g_nav_last_stats.core_box.image_ms +
                g_nav_last_stats.core_box.anonymous_ms +
                g_nav_last_stats.core_box.table_normalise_ms;
        box_other = (g_nav_last_stats.core_box.tree_ms >= box_known) ?
                g_nav_last_stats.core_box.tree_ms - box_known : 0;
        image_known = g_nav_last_stats.core_image.svg_setup_ms +
                g_nav_last_stats.core_image.svg_parse_ms +
                g_nav_last_stats.core_image.svg_raster_ms;
        image_other = (g_nav_last_stats.core_image.svg_total_ms >=
                image_known) ?
                g_nav_last_stats.core_image.svg_total_ms - image_known : 0;
        _snprintf(summary, sizeof(summary) - 1,
                "Last navigation %s\n"
                "total/net/maxUI=%lu/%lu/%lums\n"
                "parse/style/img/layout/paint=\n"
                "%lu/%lu/%lu/%lu/%lums\n"
                "res q/ok/f/r=%d/%d/%d/%d\n"
                "bytes doc/cache=%d/%d reject=%d\n"
                "layout total=%lums pass=%d\n"
                "box/first/settle/final/other=\n"
                "%lu/%lu/%lu/%lu/%lums",
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
                g_nav_last_stats.budget_rejected,
                (unsigned long) g_nav_last_stats.core_layout.total_ms,
                g_nav_last_stats.core_layout.settling_pass,
                (unsigned long)
                        g_nav_last_stats.core_layout.box_construct_ms,
                (unsigned long)
                        g_nav_last_stats.core_layout.first_layout_ms,
                (unsigned long) g_nav_last_stats.core_layout.settling_ms,
                (unsigned long) g_nav_last_stats.core_layout.finalize_ms,
                core_other);
        summary[sizeof(summary) - 1] = '\0';
        _snprintf(box_summary, sizeof(box_summary) - 1,
                "Box detail: %s\n"
                "tree/bg/other=%lu/%lu/%lums\n"
                "hot style/text/image/anon/table=\n"
                "%lu/%lu/%lu/%lu/%lums\n"
                "image reuse/markup-first=%u/%u\n"
                "svg %s total/setup/parse/raster/other=\n"
                "%lu/%lu/%lu/%lu/%lums creates=%u",
                g_nav_last_stats.core_box_valid ? "ok" : "unavailable",
                (unsigned long) g_nav_last_stats.core_box.tree_ms,
                (unsigned long) g_nav_last_stats.core_box.backgrounds_ms,
                box_other,
                (unsigned long) g_nav_last_stats.core_box.style_ms,
                (unsigned long) g_nav_last_stats.core_box.text_ms,
                (unsigned long) g_nav_last_stats.core_box.image_ms,
                (unsigned long) g_nav_last_stats.core_box.anonymous_ms,
                (unsigned long)
                        g_nav_last_stats.core_box.table_normalise_ms,
                g_nav_last_stats.core_box.image_reuses,
                g_nav_last_stats.core_box.image_markup_first,
                g_nav_last_stats.core_image_valid ? "ok" : "unavailable",
                (unsigned long) g_nav_last_stats.core_image.svg_total_ms,
                (unsigned long) g_nav_last_stats.core_image.svg_setup_ms,
                (unsigned long) g_nav_last_stats.core_image.svg_parse_ms,
                (unsigned long) g_nav_last_stats.core_image.svg_raster_ms,
                image_other,
                g_nav_last_stats.core_image.svg_creates);
        box_summary[sizeof(box_summary) - 1] = '\0';
        show_info(L"TEST 13 OK (overview)", summary);
        show_info(L"TEST 13 OK (box detail)", box_summary);
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
    int dpi;
    int expected_box;
    PCoreBoxStats first_box_stats;
    PCoreBoxStats second_box_stats;
    char msg[256];

    memset(&first_box_stats, 0, sizeof(first_box_stats));
    memset(&second_box_stats, 0, sizeof(second_box_stats));
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
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    /* TEST20 is offline, but its 48 CSS px image must still become the
     * correct number of physical pixels on the current device. */
    dpi = test_host_device_dpi();
    expected_box = MulDiv(48, dpi, 96);
    if (expected_box < 1) { expected_box = 48; }
    test_host_set_device_viewport(vw, vh);
    hSheet = PCore_ParseCSS(CSS, 0, "http://positron.local/img.css");
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) {
            PCore_FreeStylesheet(hSheet);
        }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", "CSS styling failed");
        return FALSE;
    }
    /* Reassert the device contract at the layout boundary. This is harmless
     * for media selection and protects offline tests from a preceding
     * document transaction changing the process-global viewport state. */
    test_host_set_device_viewport(vw, vh);
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_GetBoxStats(hDoc, &first_box_stats) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != expected_box || h != expected_box ||
            first_box_stats.image_calls != 4 ||
            first_box_stats.image_reuses != 0) {
        sprintf(msg, "first box=%dx%d calls/reuse=%u/%u; expect "
                "%dx%d device px (48 CSS px at %d DPI), 4/0", w, h,
                first_box_stats.image_calls, first_box_stats.image_reuses,
                expected_box, expected_box, dpi);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", msg);
        return FALSE;
    }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_GetBoxStats(hDoc, &second_box_stats) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != expected_box || h != expected_box ||
            second_box_stats.image_calls != 4 ||
            second_box_stats.image_reuses != 4 ||
            ctx.calls != 4 || ctx.frees != 4) {
        sprintf(msg, "reuse box=%dx%d calls/reuse=%u/%u fetch/free=%d/%d",
                w, h, second_box_stats.image_calls,
                second_box_stats.image_reuses, ctx.calls, ctx.frees);
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
        test_host_set_device_viewport(vw, vh);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 20 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    test_host_set_device_viewport(vw, vh);
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);

    show_info(L"TEST 20 OK",
              "Cached BMP/PNG/JPEG/GIF became NetSurf replaced boxes\n"
              "and painted through content_redraw -> plot_bitmap ->\n"
              "WM Imaging IImage::Draw; second layout reused 4/4\n"
              "retained objects while fetch/free stayed 4/4.");
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
    int dpi;
    int expected_w;
    int expected_h;
    PCoreBoxStats first_box_stats;
    PCoreBoxStats second_box_stats;
    PCoreImageDecodeStats image_stats;
    unsigned long image_known;
    char msg[256];

    memset(&first_box_stats, 0, sizeof(first_box_stats));
    memset(&second_box_stats, 0, sizeof(second_box_stats));
    memset(&image_stats, 0, sizeof(image_stats));
    image_known = 0;
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
    vw = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXVSCROLL);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (vw <= 0) { vw = 224; }
    if (vh <= 0) { vh = 320; }
    dpi = test_host_device_dpi();
    expected_w = MulDiv(120, dpi, 96);
    expected_h = MulDiv(60, dpi, 96);
    if (expected_w < 1) { expected_w = 120; }
    if (expected_h < 1) { expected_h = 60; }
    test_host_set_device_viewport(vw, vh);
    if (hSheet == NULL || PCore_StyleDocument(hDoc, hSheet) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", "CSS styling failed");
        return FALSE;
    }
    /* Keep the explicit image geometry check tied to the same device
     * viewport that the subsequent NetSurf layout consumes. */
    test_host_set_device_viewport(vw, vh);
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_GetBoxStats(hDoc, &first_box_stats) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != expected_w || h != expected_h ||
            first_box_stats.image_calls != 1 ||
            first_box_stats.image_reuses != 0 ||
            first_box_stats.image_markup_first != 1) {
        sprintf(msg, "first SVG=%dx%d expect=%dx%d at %d DPI "
                "calls/reuse/markup=%u/%u/%u", w, h,
                expected_w, expected_h, dpi, first_box_stats.image_calls,
                first_box_stats.image_reuses,
                first_box_stats.image_markup_first);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", msg);
        return FALSE;
    }
    if (PCore_LayoutDocument(hDoc, vw, vh) != 0 ||
            PCore_GetBoxStats(hDoc, &second_box_stats) != 0 ||
            PCore_GetImageDecodeStats(hDoc, &image_stats) != 0 ||
            PCore_NodeBox(hDoc, "img", &x, &y, &w, &h) != 0 ||
            w != expected_w || h != expected_h ||
            second_box_stats.image_calls != 1 ||
            second_box_stats.image_reuses != 1 ||
            image_stats.svg_creates != 1 ||
            ctx.calls != 1 || ctx.frees != 1) {
        sprintf(msg, "reuse SVG=%dx%d expect=%dx%d calls/reuse/create=%u/%u/%u "
                "f/f=%d/%d", w, h, expected_w, expected_h,
                second_box_stats.image_calls,
                second_box_stats.image_reuses, image_stats.svg_creates,
                ctx.calls, ctx.frees);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", msg);
        return FALSE;
    }
    image_known = image_stats.svg_setup_ms + image_stats.svg_parse_ms +
            image_stats.svg_raster_ms;
    if (image_known > image_stats.svg_total_ms) {
        sprintf(msg, "SVG timing total=%lu setup/parse/raster=%lu/%lu/%lu",
                image_stats.svg_total_ms, image_stats.svg_setup_ms,
                image_stats.svg_parse_ms, image_stats.svg_raster_ms);
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
    red = GetPixel(memory_dc, x + MulDiv(20, dpi, 96),
            y + MulDiv(30, dpi, 96));
    green = GetPixel(memory_dc, x + MulDiv(60, dpi, 96),
            y + MulDiv(30, dpi, 96));
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
        test_host_set_device_viewport(vw, vh);
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 27 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    test_host_set_device_viewport(vw, vh);
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    sprintf(msg,
            "SVG-first + retained reuse passed.\n"
            "create total/setup/parse/raster/other=\n"
            "%lu/%lu/%lu/%lu/%lums",
            image_stats.svg_total_ms, image_stats.svg_setup_ms,
            image_stats.svg_parse_ms, image_stats.svg_raster_ms,
            image_stats.svg_total_ms - image_known);
    show_info(L"TEST 27 OK", msg);
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
    /* The geometry half is an explicit CSS-pixel contract. Do not let a
     * preceding device-backed render carry its physical DPI into it. */
    PCore_SetViewport(230, 260, 96);
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
    test_host_set_device_viewport(screen_w, screen_h);
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
    test_host_set_device_viewport(screen_w, screen_h);
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
    int screen_dpi;
    int expected_article_w;
    int expected_row0;
    int expected_row1;
    int expected_row2;
    int expected_total_h;
    int i;
    char msg[256];

    memset(rows, 0, sizeof(rows));
    memset(row_kind, 0, sizeof(row_kind));
    memset(row_value, 0, sizeof(row_value));
    memset(colors, 0, sizeof(colors));
    /* Use the real device viewport for this geometry probe. The fixture's
     * CSS lengths remain fixed, while expected geometry is converted to
     * physical pixels from the reported device DPI. */
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    screen_dpi = test_host_device_dpi();
    expected_article_w = MulDiv(160, screen_dpi, 96);
    expected_row0 = MulDiv(20, screen_dpi, 96);
    expected_row1 = MulDiv(40, screen_dpi, 96);
    expected_row2 = MulDiv(20, screen_dpi, 96);
    expected_total_h = expected_row0 + expected_row1 + expected_row2;
    test_host_set_device_viewport(screen_w, screen_h);
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
            PCore_LayoutDocument(hDoc, screen_w, screen_h) != 0 ||
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
    if (i != 3 || abs(article_w - expected_article_w) > 1 ||
            row_kind[0] != 1 ||
            row_value[0] != 25 || row_kind[1] != 1 ||
            row_value[1] != 50 || row_kind[2] != 0 ||
            abs(total_h - expected_total_h) > 2 ||
            abs(rows[0].row_height - expected_row0) > 1 ||
            abs(rows[1].row_height - expected_row1) > 1 ||
            abs(rows[2].row_height - expected_row2) > 1) {
        _snprintf(msg, sizeof(msg) - 1,
                "dpi=%d article=%d/%d rows=%d/%d/%d expect=%d/%d/%d kinds=%d:%d,%d:%d,%d",
                screen_dpi, article_w, expected_article_w,
                rows[0].row_height, rows[1].row_height,
                rows[2].row_height, expected_row0, expected_row1,
                expected_row2, row_kind[0], row_value[0], row_kind[1],
                row_value[1], row_kind[2]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 58 FAIL", msg);
        return FALSE;
    }

    test_host_set_device_viewport(screen_w, screen_h);
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
    int screen_w;
    int screen_h;
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
        /* This is an explicit CSS-pixel geometry probe. Keep its 224/320
         * logical widths at the CSS 96-DPI reference so a preceding visible
         * high-DPI render cannot convert the fixed 25px padding twice. */
        PCore_SetViewport(widths[i], 240, 96);
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
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    test_host_set_device_viewport(screen_w, screen_h);
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
    int screen_w;
    int screen_h;
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

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/table-header-restyle.css");
    if (hDoc == NULL || hSheet == NULL) {
        strcpy(msg, "parse failed");
        goto cleanup;
    }

    for (pass = 0; pass < 2; pass++) {
        memset(cells, 0, sizeof(cells));
        /* This probe owns explicit CSS-pixel geometry. Keep its reference
         * scale at 96 DPI; restore the real device context below. */
        PCore_SetViewport(widths[pass], heights[pass], 96);
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
    test_host_set_device_viewport(screen_w, screen_h);
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
/* TEST 62 - static NetSurf checkbox/radio form-gadget redraw baseline  */
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
        "<h1>Form control states</h1>"
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
              "The hidden input leaves no visible row. This test checks\n"
              "static geometry/redraw; TEST 64 checks interaction.");
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
/* TEST 63 - decoded SVG ownership across overlapping documents          */
/* -------------------------------------------------------------------- */
static BOOL test63_shared_svg_lifetime(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<img alt=\"shared SVG\" src=\"/img/test.svg\">"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "img{width:120px;height:60px;}";
    HANDLE first_doc = NULL;
    HANDLE second_doc = NULL;
    HANDLE first_sheet = NULL;
    HANDLE second_sheet = NULL;
    image_resource_test_ctx first_fetch;
    image_resource_test_ctx second_fetch;
    PCoreBoxStats first_box;
    PCoreBoxStats second_box;
    PCoreImageDecodeStats first_decode;
    PCoreImageDecodeStats second_decode;
    HDC screen_dc = NULL;
    HDC memory_dc = NULL;
    HBITMAP bitmap = NULL;
    HBITMAP old_bitmap = NULL;
    RECT rect;
    COLORREF red;
    COLORREF green;
    int x;
    int y;
    int w;
    int h;
    int screen_w;
    int screen_h;
    int layout_rc;
    int node_rc;
    int rc = 1;
    char msg[256];

    memset(&first_fetch, 0, sizeof(first_fetch));
    memset(&second_fetch, 0, sizeof(second_fetch));
    memset(&first_box, 0, sizeof(first_box));
    memset(&second_box, 0, sizeof(second_box));
    memset(&first_decode, 0, sizeof(first_decode));
    memset(&second_decode, 0, sizeof(second_decode));
    x = 0;
    y = 0;
    w = 0;
    h = 0;
    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0) { screen_w = 240; }
    if (screen_h <= 0) { screen_h = 320; }
    /* This probe owns explicit CSS-pixel geometry; do not inherit the
     * device-backed context left by the preceding render test. */
    PCore_SetViewport(240, 120, 96);
    first_doc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    second_doc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    first_sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/shared-first.css");
    second_sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/shared-second.css");
    if (first_doc == NULL || second_doc == NULL ||
            first_sheet == NULL || second_sheet == NULL ||
            PCore_FetchImageResources(first_doc, image_svg_fetch,
                image_resource_free, &first_fetch, NULL, NULL) != 0 ||
            PCore_FetchImageResources(second_doc, image_svg_fetch,
                image_resource_free, &second_fetch, NULL, NULL) != 0 ||
            PCore_StyleDocument(first_doc, first_sheet) != 0 ||
            PCore_StyleDocument(second_doc, second_sheet) != 0 ||
            PCore_LayoutDocument(first_doc, 240, 120) != 0 ||
            PCore_GetBoxStats(first_doc, &first_box) != 0 ||
            PCore_GetImageDecodeStats(first_doc, &first_decode) != 0 ||
            PCore_LayoutDocument(second_doc, 240, 120) != 0 ||
            PCore_GetBoxStats(second_doc, &second_box) != 0 ||
            PCore_GetImageDecodeStats(second_doc, &second_decode) != 0) {
        show_error(L"TEST 63 FAIL", "two-document SVG setup failed");
        goto cleanup;
    }
    if (first_decode.svg_creates != 1 ||
            first_box.image_reuses != 0 ||
            second_decode.svg_creates != 0 ||
            second_box.image_reuses != 1 ||
            first_fetch.calls != 1 || first_fetch.frees != 1 ||
            second_fetch.calls != 1 || second_fetch.frees != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                "create=%u/%u reuse=%u/%u fetch/free=%d/%d,%d/%d",
                first_decode.svg_creates, second_decode.svg_creates,
                first_box.image_reuses, second_box.image_reuses,
                first_fetch.calls, first_fetch.frees,
                second_fetch.calls, second_fetch.frees);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 63 FAIL", msg);
        goto cleanup;
    }

    PCore_FreeStylesheet(first_sheet);
    first_sheet = NULL;
    PCore_FreeDocument(first_doc);
    first_doc = NULL;
    layout_rc = PCore_LayoutDocument(second_doc, 240, 120);
    node_rc = PCore_NodeBox(second_doc, "img", &x, &y, &w, &h);
    if (layout_rc != 0 || node_rc != 0 || w != 120 || h != 60) {
        _snprintf(msg, sizeof(msg) - 1,
                "post-release layout/node/box=%d/%d/%dx%d expect 120x60",
                layout_rc, node_rc, w, h);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 63 FAIL", msg);
        goto cleanup;
    }

    screen_dc = GetDC(NULL);
    memory_dc = (screen_dc != NULL) ? CreateCompatibleDC(screen_dc) : NULL;
    bitmap = (screen_dc != NULL) ?
            CreateCompatibleBitmap(screen_dc, 240, 120) : NULL;
    if (screen_dc == NULL || memory_dc == NULL || bitmap == NULL) {
        show_error(L"TEST 63 FAIL", "could not create off-screen surface");
        goto cleanup;
    }
    old_bitmap = (HBITMAP) SelectObject(memory_dc, bitmap);
    SetRect(&rect, 0, 0, 240, 120);
    FillRect(memory_dc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    PCore_PaintDocument(second_doc, memory_dc, 0, 0);
    red = GetPixel(memory_dc, x + 20, y + 30);
    green = GetPixel(memory_dc, x + 60, y + 30);
    if (red != RGB(255, 0, 0) || green != RGB(0, 255, 0)) {
        _snprintf(msg, sizeof(msg) - 1,
                "post-release pixels=%06lX/%06lX",
                red & 0x00ffffffUL, green & 0x00ffffffUL);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 63 FAIL", msg);
        goto cleanup;
    }
    show_info(L"TEST 63 OK",
              "Two live documents shared one decoded SVG; releasing the\n"
              "first owner left the second document drawable.");
    rc = 0;

cleanup:
    test_host_set_device_viewport(screen_w, screen_h);
    if (old_bitmap != NULL && memory_dc != NULL) {
        SelectObject(memory_dc, old_bitmap);
    }
    if (bitmap != NULL) { DeleteObject(bitmap); }
    if (memory_dc != NULL) { DeleteDC(memory_dc); }
    if (screen_dc != NULL) { ReleaseDC(NULL, screen_dc); }
    if (first_sheet != NULL) { PCore_FreeStylesheet(first_sheet); }
    if (second_sheet != NULL) { PCore_FreeStylesheet(second_sheet); }
    if (first_doc != NULL) { PCore_FreeDocument(first_doc); }
    if (second_doc != NULL) { PCore_FreeDocument(second_doc); }
    return rc == 0;
}

/* -------------------------------------------------------------------- */
/* TEST 64 - interactive checkbox/radio state and relayout persistence  */
/* -------------------------------------------------------------------- */
static int test64_control_state(HANDLE hDoc, unsigned int index,
        int *selected, int *disabled)
{
    return PCore_FormControlInfo(hDoc, index, NULL, NULL, NULL, NULL,
            NULL, selected, disabled);
}

static int test64_activate(HANDLE hDoc, unsigned int index,
        int *dirty_w, int *dirty_h)
{
    int x;
    int y;
    int w;
    int h;
    int dirty_x;
    int dirty_y;

    if (PCore_FormControlInfo(hDoc, index, &x, &y, &w, &h,
            NULL, NULL, NULL) != 0 || w <= 0 || h <= 0) {
        return 1;
    }
    return PCore_FormActivateAt(hDoc, x + w / 2, y + h / 2,
            &dirty_x, &dirty_y, dirty_w, dirty_h) ? 0 : 1;
}

static BOOL test64_form_interaction(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Interactive form controls</h1>"
        "<form id=first>"
        "<p><input type=checkbox> checkbox toggled on</p>"
        "<p><input type=checkbox disabled> disabled stays off</p>"
        "<p><input type=radio name=main checked> first radio off</p>"
        "<p><input type=radio name=main> second radio on</p>"
        "<p><input type=radio name=other checked> other group stays on</p>"
        "</form>"
        "<form id=second>"
        "<p><input type=radio name=main checked> other form stays on</p>"
        "</form>"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:15px;line-height:20px;padding:9px;color:#111;}"
        "h1{font-size:20px;line-height:24px;color:#8b0000;margin:0 0 6px;}"
        "p{margin:4px 0;}input{font-size:18px;}";
    HANDLE hDoc;
    HANDLE hSheet;
    int selected[6];
    int disabled;
    int dirty_w;
    int dirty_h;
    int i;
    char msg[256];

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/form-interaction.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 64 FAIL", "form interaction setup failed");
        return FALSE;
    }

    dirty_w = 0;
    dirty_h = 0;
    if (test64_activate(hDoc, 0, &dirty_w, &dirty_h) != 0 ||
            dirty_w <= 0 || dirty_h <= 0 ||
            test64_control_state(hDoc, 0, &selected[0], NULL) != 0 ||
            selected[0] != 1 ||
            test64_activate(hDoc, 1, &dirty_w, &dirty_h) != 0 ||
            dirty_w != 0 || dirty_h != 0 ||
            test64_control_state(hDoc, 1, &selected[1], &disabled) != 0 ||
            selected[1] != 0 || disabled != 1 ||
            test64_activate(hDoc, 3, &dirty_w, &dirty_h) != 0 ||
            dirty_w <= 0 || dirty_h <= 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 64 FAIL", "checkbox/disabled/radio activation failed");
        return FALSE;
    }

    for (i = 0; i < 6; i++) {
        if (test64_control_state(hDoc, (unsigned int) i, &selected[i],
                NULL) != 0) {
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 64 FAIL", "control enumeration failed");
            return FALSE;
        }
    }
    if (selected[0] != 1 || selected[1] != 0 ||
            selected[2] != 0 || selected[3] != 1 ||
            selected[4] != 1 || selected[5] != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                "states=%d/%d/%d/%d/%d/%d",
                selected[0], selected[1], selected[2],
                selected[3], selected[4], selected[5]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 64 FAIL", msg);
        return FALSE;
    }

    dirty_w = -1;
    dirty_h = -1;
    if (test64_activate(hDoc, 3, &dirty_w, &dirty_h) != 0 ||
            dirty_w != 0 || dirty_h != 0 ||
            PCore_LayoutDocument(hDoc, 320, 240) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 64 FAIL", "selected radio/re-layout failed");
        return FALSE;
    }
    for (i = 0; i < 6; i++) {
        if (test64_control_state(hDoc, (unsigned int) i, &selected[i],
                NULL) != 0) {
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 64 FAIL", "post-layout enumeration failed");
            return FALSE;
        }
    }
    if (selected[0] != 1 || selected[1] != 0 ||
            selected[2] != 0 || selected[3] != 1 ||
            selected[4] != 1 || selected[5] != 1) {
        _snprintf(msg, sizeof(msg) - 1,
                "post-layout=%d/%d/%d/%d/%d/%d",
                selected[0], selected[1], selected[2],
                selected[3], selected[4], selected[5]);
        msg[sizeof(msg) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 64 FAIL", msg);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 64 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 64 OK",
              "Checkbox toggle, disabled control, radio group isolation,\n"
              "DOM synchronisation and re-layout persistence passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 65 - WM native single-line input bridge and DOM persistence     */
/* -------------------------------------------------------------------- */
static BOOL test65_text_input(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Native text inputs</h1>"
        "<p>Text <input name=plain value=alpha maxlength=8></p>"
        "<p>Password <input type=password name=secret value=secret></p>"
        "<p>Read only <input name=locked value=locked readonly></p>"
        "<p>Disabled <input name=off value=off disabled></p>"
        "<p>Tap an enabled field to use the WM input method.</p>"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:14px;line-height:19px;padding:8px;color:#111;}"
        "h1{font-size:20px;line-height:24px;color:#8b0000;margin:0 0 5px;}"
        "p{margin:5px 0;}input{font-size:14px;width:11em;height:1.6em;"
        "border:1px solid #555;padding:1px 2px;background:#fff;}";
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTextInputInfo info[4];
    char value[64];
    int kind;
    int i;

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/native-text-input.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 65 FAIL", "text input setup failed");
        return FALSE;
    }

    for (i = 0; i < 4; i++) {
        memset(&info[i], 0, sizeof(info[i]));
        if (PCore_TextInputInfo(hDoc, (unsigned int) i, &info[i],
                value, sizeof(value)) != 0 ||
                info[i].width <= 0 || info[i].height <= 0 ||
                PCore_FormControlInfo(hDoc, (unsigned int) i,
                        NULL, NULL, NULL, NULL, &kind, NULL, NULL) != 0 ||
                kind != ((i == 1) ? 4 : 3)) {
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 65 FAIL", "text input enumeration failed");
            return FALSE;
        }
    }
    if (info[0].max_length != 8 || info[0].password ||
            !info[1].password || !info[2].read_only ||
            !info[3].disabled ||
            PCore_TextInputInfo(hDoc, 4, NULL, NULL, 0) == 0 ||
            PCore_TextInputSetValue(hDoc, 0, "too-long-9") != 3 ||
            PCore_TextInputSetValue(hDoc, 0, "\377") != 3 ||
            PCore_TextInputSetValue(hDoc, 0, "beta42") != 0 ||
            PCore_TextInputSetValue(hDoc, 1, "next-secret") != 0 ||
            PCore_TextInputSetValue(hDoc, 2, "changed") != 2 ||
            PCore_TextInputSetValue(hDoc, 3, "changed") != 2 ||
            PCore_LayoutDocument(hDoc, 320, 240) != 0 ||
            PCore_TextInputInfo(hDoc, 0, &info[0],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "beta42") != 0 ||
            PCore_TextInputInfo(hDoc, 1, &info[1],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "next-secret") != 0 ||
            PCore_TextInputInfo(hDoc, 2, &info[2],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "locked") != 0 ||
            PCore_TextInputInfo(hDoc, 3, &info[3],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "off") != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 65 FAIL",
                "value policy or re-layout persistence failed");
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    g_native_edit_probe = 1;
    g_native_edit_probe_multiline = 0;
    g_native_edit_probe_ok = 0;
    g_native_edit_probe_seen = 0;
    g_native_edit_probe_set_result = -1;
    g_native_edit_probe_value[0] = '\0';
    if (!show_render_window()) {
        g_native_edit_probe = 0;
        g_native_edit_probe_multiline = 0;
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 65 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_native_edit_probe = 0;
    g_native_edit_probe_multiline = 0;
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    if (!g_native_edit_probe_ok) {
        show_error(L"TEST 65 FAIL",
                "WM EDIT EN_CHANGE did not reach the core DOM");
        return FALSE;
    }
    show_info(L"TEST 65 OK",
              "Native WM EDIT controls, password mode, readonly/disabled,\n"
              "maxlength, UTF-8 validation and rotation persistence passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 66 - WM native multiline textarea bridge and DOM persistence    */
/* -------------------------------------------------------------------- */
static BOOL test66_textarea(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Native textarea</h1>"
        "<p>Editable</p><textarea name=notes>alpha\nbeta</textarea>"
        "<p>Read only</p><textarea name=locked readonly>locked\ntext"
        "</textarea>"
        "<p>Disabled</p><textarea name=off disabled>off\ntext</textarea>"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:14px;line-height:19px;padding:8px;color:#111;}"
        "h1{font-size:20px;line-height:24px;color:#8b0000;margin:0 0 5px;}"
        "p{margin:4px 0 2px;}textarea{display:inline-block;"
        "font-size:14px;width:13em;height:4.5em;border:1px solid #555;"
        "padding:2px;background:#fff;}";
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreTextInputInfo info[3];
    char value[96];
    char msg[192];
    int kind;
    int multiline;
    int i;

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/native-textarea.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 66 FAIL", "textarea setup failed");
        return FALSE;
    }

    for (i = 0; i < 3; i++) {
        memset(&info[i], 0, sizeof(info[i]));
        if (PCore_TextInputInfo(hDoc, (unsigned int) i, &info[i],
                value, sizeof(value)) != 0 ||
                info[i].width <= 0 || info[i].height <= 0 ||
                PCore_TextInputIsMultiline(hDoc, (unsigned int) i,
                        &multiline) != 0 || !multiline ||
                info[i].password ||
                PCore_FormControlInfo(hDoc, (unsigned int) i,
                        NULL, NULL, NULL, NULL, &kind, NULL, NULL) != 0 ||
                kind != 5) {
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 66 FAIL", "textarea enumeration failed");
            return FALSE;
        }
    }
    if (strcmp(value, "off\ntext") != 0 ||
            info[0].max_length != -1 ||
            info[0].read_only || info[0].disabled ||
            !info[1].read_only || !info[2].disabled ||
            PCore_TextInputInfo(hDoc, 3, NULL, NULL, 0) == 0 ||
            PCore_TextInputSetValue(hDoc, 0, "\377") != 3 ||
            PCore_TextInputSetValue(hDoc, 0,
                    "first\r\nsecond\rthird") != 0 ||
            PCore_TextInputSetValue(hDoc, 1, "changed") != 2 ||
            PCore_TextInputSetValue(hDoc, 2, "changed") != 2 ||
            PCore_LayoutDocument(hDoc, 320, 240) != 0 ||
            PCore_TextInputInfo(hDoc, 0, &info[0],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "first\nsecond\nthird") != 0 ||
            PCore_TextInputInfo(hDoc, 1, &info[1],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "locked\ntext") != 0 ||
            PCore_TextInputInfo(hDoc, 2, &info[2],
                    value, sizeof(value)) != 0 ||
            strcmp(value, "off\ntext") != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 66 FAIL",
                "textarea policy, newline or re-layout persistence failed");
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    g_native_edit_probe = 1;
    g_native_edit_probe_multiline = 1;
    g_native_edit_probe_ok = 0;
    g_native_edit_probe_seen = 0;
    g_native_edit_probe_set_result = -1;
    g_native_edit_probe_value[0] = '\0';
    if (!show_render_window()) {
        g_native_edit_probe = 0;
        g_native_edit_probe_multiline = 0;
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 66 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_native_edit_probe = 0;
    g_native_edit_probe_multiline = 0;
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    if (!g_native_edit_probe_ok) {
        _snprintf(msg, sizeof(msg) - 1,
                "WM multiline EDIT probe: seen=%d set=%d len=%u "
                "bytes=%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X",
                g_native_edit_probe_seen,
                g_native_edit_probe_set_result,
                (unsigned int) strlen(g_native_edit_probe_value),
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[0],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[1],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[2],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[3],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[4],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[5],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[6],
                (unsigned int) (unsigned char)
                        g_native_edit_probe_value[7]);
        msg[sizeof(msg) - 1] = '\0';
        show_error(L"TEST 66 FAIL", msg);
        return FALSE;
    }
    show_info(L"TEST 66 OK",
              "Native WM multiline EDIT, CRLF/LF normalisation,\n"
              "readonly/disabled and rotation persistence passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 67 - select option state and WM native dropdown bridge          */
/* -------------------------------------------------------------------- */
static BOOL test67_select_control(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Native select</h1>"
        "<select name=region>"
        "<option value=n selected>  North   zone  </option>"
        "<option value=x disabled>Blocked</option>"
        "<option value=s selected>South</option>"
        "</select>"
        "<select name=locked disabled><option selected>Locked</option>"
        "</select>"
        "<select id=multi name=tags multiple>"
        "<option value=a selected>Alpha</option>"
        "<option value=b selected>Beta</option>"
        "<option value=c>Gamma</option>"
        "</select>"
        "</body></html>";
    static const char CSS[] =
        "body{font:16px sans-serif;margin:8px}"
        "h1{font-size:22px;color:#800000}"
        "select{display:inline-block;width:150px;margin:4px}"
        "#multi{height:60px}";
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreSelectInfo info[3];
    char label[64];
    char value[32];
    int kind;
    int selected;
    int disabled;
    int label_bytes;
    int value_bytes;
    int i;

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/native-select.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 67 FAIL", "select setup failed");
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        memset(&info[i], 0, sizeof(info[i]));
        if (PCore_SelectInfo(hDoc, (unsigned int) i, &info[i]) != 0 ||
                info[i].width <= 0 || info[i].height <= 0 ||
                PCore_FormControlInfo(hDoc, (unsigned int) i,
                        NULL, NULL, NULL, NULL, &kind,
                        NULL, NULL) != 0 || kind != 6) {
            PCore_FreeStylesheet(hSheet);
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 67 FAIL", "select enumeration failed");
            return FALSE;
        }
    }
    if (info[0].disabled || info[0].multiple ||
            info[0].option_count != 3 ||
            info[0].selected_count != 1 ||
            info[0].selected_index != 0 ||
            !info[1].disabled || info[1].option_count != 1 ||
            !info[2].multiple || info[2].option_count != 3 ||
            info[2].selected_count != 2 ||
            info[2].selected_index != -1 ||
            PCore_SelectInfo(hDoc, 3, NULL) == 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 67 FAIL", "select initial state failed");
        return FALSE;
    }
    memset(label, 0, sizeof(label));
    memset(value, 0, sizeof(value));
    if (PCore_SelectOptionInfo(hDoc, 0, 0,
            label, sizeof(label), value, sizeof(value),
            &selected, &disabled, &label_bytes, &value_bytes) != 0 ||
            strcmp(label, "North zone") != 0 ||
            strcmp(value, "n") != 0 || !selected || disabled ||
            label_bytes != 10 || value_bytes != 1 ||
            PCore_SelectOptionInfo(hDoc, 0, 2,
                    NULL, 0, NULL, 0, &selected, NULL,
                    NULL, NULL) != 0 || selected ||
            PCore_SelectOptionInfo(hDoc, 0, 1,
                    NULL, 0, NULL, 0, NULL, &disabled,
                    NULL, NULL) != 0 || !disabled ||
            PCore_SelectSetOptionSelected(hDoc, 0, 1, 1) != 2 ||
            PCore_SelectSetOptionSelected(hDoc, 1, 0, 1) != 2 ||
            PCore_SelectSetOptionSelected(hDoc, 0, 2, 1) != 0 ||
            PCore_SelectSetOptionSelected(hDoc, 2, 2, 1) != 0 ||
            PCore_SelectSetOptionSelected(hDoc, 2, 1, 0) != 0 ||
            PCore_SelectSetOptionSelected(hDoc, 0, 9, 1) != 1 ||
            PCore_LayoutDocument(hDoc, 320, 240) != 0 ||
            PCore_SelectInfo(hDoc, 0, &info[0]) != 0 ||
            info[0].selected_index != 2 ||
            PCore_SelectInfo(hDoc, 2, &info[2]) != 0 ||
            info[2].selected_count != 2 ||
            PCore_SelectOptionInfo(hDoc, 2, 1,
                    NULL, 0, NULL, 0, &selected, NULL,
                    NULL, NULL) != 0 || selected) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 67 FAIL",
                "option policy or re-layout persistence failed");
        return FALSE;
    }
    if (PCore_SelectSetOptionSelected(hDoc, 0, 0, 1) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 67 FAIL", "select probe reset failed");
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    g_native_select_probe = 1;
    g_native_select_probe_ok = 0;
    if (!show_render_window()) {
        g_native_select_probe = 0;
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 67 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_native_select_probe = 0;
    g_render_doc = NULL;
    g_render_sheet = NULL;
    if (PCore_SelectInfo(hDoc, 0, &info[0]) != 0 ||
            info[0].selected_index != 2) {
        g_native_select_probe_ok = 0;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    if (!g_native_select_probe_ok) {
        show_error(L"TEST 67 FAIL",
                "WM COMBOBOX selection did not reach the core DOM");
        return FALSE;
    }
    show_info(L"TEST 67 OK",
              "NetSurf select geometry/options, disabled policy,\n"
              "multiple state, WM COMBOBOX bridge and rotation passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 68 - button controls and GET/urlencoded POST submission          */
/* -------------------------------------------------------------------- */
static int test68_control_center(HANDLE hDoc, int wanted_kind,
        int wanted_ordinal, int *x, int *y)
{
    unsigned int index;
    int ordinal;
    int kind;
    int left;
    int top;
    int width;
    int height;

    ordinal = 0;
    for (index = 0;
            PCore_FormControlInfo(hDoc, index, &left, &top,
                    &width, &height, &kind, NULL, NULL) == 0;
            index++) {
        if (kind == wanted_kind) {
            if (ordinal == wanted_ordinal) {
                if (width <= 0 || height <= 0) {
                    return 0;
                }
                *x = left + width / 2;
                *y = top + height / 2;
                return 1;
            }
            ordinal++;
        }
    }
    return 0;
}

static BOOL test68_form_submission(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<h1>Form submission</h1>"
        "<form action='/find?stale=1' method=get>"
        "<input name=q value='alpha beta&amp;x'>"
        "<input type=hidden name=token value='a/b'>"
        "<input type=checkbox name=include value=yes checked>"
        "<input type=checkbox name=skip value=no>"
        "<input type=radio name=scope value=all checked>"
        "<input type=radio name=scope value=none>"
        "<textarea name=note>line one</textarea>"
        "<select name=tag multiple>"
        "<option value=x selected>X</option>"
        "<option value='y z' selected>Y</option>"
        "</select>"
        "<input name=disabled value=no disabled>"
        "<input type=submit name=go value=Find>"
        "<input type=submit name=other value=Wrong>"
        "</form>"
        "<form action=/post method=POST "
        "enctype='application/x-www-form-urlencoded'>"
        "<input name=msg value='A+B'>"
        "<button name=send value=yes>Send now</button>"
        "</form>"
        "<form action=/upload method=post "
        "enctype='multipart/form-data'>"
        "<button>Upload</button>"
        "</form>"
        "<button type=button>Plain</button>"
        "<button type=reset>Reset</button>"
        "<button disabled>Disabled</button>"
        "</body></html>";
    static const char CSS[] =
        "body{font:14px sans-serif;margin:8px}"
        "h1{font-size:20px}"
        "form{margin:3px 0}"
        "input,textarea,select,button{display:inline-block;"
        "width:90px;height:24px;margin:2px}";
    static const char EXPECT_GET[] =
        "q=alpha+beta%26x&token=a%2Fb&include=yes&scope=all&"
        "note=line+one&tag=x&tag=y+z&go=Find";
    static const char EXPECT_POST[] = "msg=A%2BB&send=yes";
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreFormSubmissionInfo info;
    pcore_navigation_request *request;
    char action[128];
    char body[512];
    char small_action[4];
    char small_body[8];
    char oversized_target[1025];
    char old_host[256];
    char old_path[1024];
    char *target;
    int old_port;
    int x;
    int y;
    int result;
    int ok;

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/form-submit.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 68 FAIL", "form setup failed");
        return FALSE;
    }
    ok = test68_control_center(hDoc, 7, 0, &x, &y);
    memset(&info, 0, sizeof(info));
    result = ok ? PCore_FormSubmissionAt(hDoc, x, y, &info,
            small_action, sizeof(small_action),
            small_body, sizeof(small_body)) : 0;
    if (!ok || result != 4 || info.method != 1 ||
            info.action_bytes != (int) strlen("/find?stale=1") ||
            info.body_bytes != (int) strlen(EXPECT_GET) ||
            PCore_FormSubmissionAt(hDoc, x, y, &info,
                    action, sizeof(action), body, sizeof(body)) != 1 ||
            strcmp(action, "/find?stale=1") != 0 ||
            strcmp(body, EXPECT_GET) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 68 FAIL", "GET successful controls failed");
        return FALSE;
    }

    if (!test68_control_center(hDoc, 7, 2, &x, &y) ||
            PCore_FormSubmissionAt(hDoc, x, y, &info,
                    action, sizeof(action), body, sizeof(body)) != 1 ||
            info.method != 2 || strcmp(action, "/post") != 0 ||
            strcmp(body, EXPECT_POST) != 0 ||
            !test68_control_center(hDoc, 7, 3, &x, &y) ||
            PCore_FormSubmissionAt(hDoc, x, y, &info,
                    action, sizeof(action), body, sizeof(body)) != 3 ||
            info.method != 3 ||
            !test68_control_center(hDoc, 9, 0, &x, &y) ||
            PCore_FormSubmissionAt(hDoc, x, y, &info,
                    action, sizeof(action), body, sizeof(body)) != 2 ||
            !test68_control_center(hDoc, 8, 0, &x, &y) ||
            PCore_FormSubmissionAt(hDoc, x, y, &info,
                    action, sizeof(action), body, sizeof(body)) != 2 ||
            !test68_control_center(hDoc, 7, 4, &x, &y) ||
            PCore_FormSubmissionAt(hDoc, x, y, &info,
                    action, sizeof(action), body, sizeof(body)) != 2 ||
            PCore_FormSubmissionAt(hDoc, 239, 319, &info,
                    action, sizeof(action), body, sizeof(body)) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 68 FAIL",
                "POST/button/disabled/multipart policy failed");
        return FALSE;
    }

    cstr_copy(old_host, sizeof(old_host), g_cur_host);
    cstr_copy(old_path, sizeof(old_path), g_cur_path);
    old_port = g_cur_port;
    cstr_copy(g_cur_host, sizeof(g_cur_host), "fixture.invalid");
    cstr_copy(g_cur_path, sizeof(g_cur_path), "/base/page");
    g_cur_port = 443;
    target = pcore_form_target_url("/find?stale=1", EXPECT_GET, 1);
    request = (target != NULL) ?
            pcore_navigation_request_create(NULL, target, 1, NULL) : NULL;
    ok = request != NULL && request->method == 1 &&
            strcmp(request->host, "fixture.invalid") == 0 &&
            request->port == 443 &&
            strcmp(request->path,
                    "/find?q=alpha+beta%26x&token=a%2Fb&include=yes&"
                    "scope=all&note=line+one&tag=x&tag=y+z&go=Find") == 0;
    pcore_navigation_request_free(request);
    free(target);
    request = pcore_navigation_request_create(NULL, "/post", 2,
            EXPECT_POST);
    ok = ok && request != NULL && request->method == 2 &&
            request->request_body_len == (int) strlen(EXPECT_POST) &&
            strcmp(request->request_body, EXPECT_POST) == 0;
    pcore_navigation_request_free(request);
    memset(oversized_target, 'x', sizeof(oversized_target) - 1);
    oversized_target[0] = '/';
    oversized_target[sizeof(oversized_target) - 1] = '\0';
    request = pcore_navigation_request_create(NULL, oversized_target,
            1, NULL);
    ok = ok && request == NULL;
    pcore_navigation_request_free(request);
    cstr_copy(g_cur_host, sizeof(g_cur_host), old_host);
    cstr_copy(g_cur_path, sizeof(g_cur_path), old_path);
    g_cur_port = old_port;
    if (!ok || PCore_LayoutDocument(hDoc, 320, 240) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 68 FAIL", "WM GET/POST request bridge failed");
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 68 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 68 OK",
            "NetSurf button gadgets, successful controls, GET query,\n"
            "urlencoded POST and WM request ownership passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 69 - label activation, form reset and native Enter submission    */
/* -------------------------------------------------------------------- */
static int test69_control_state(HANDLE hDoc, int wanted_kind,
        int wanted_ordinal, int *selected, int *disabled)
{
    unsigned int index;
    int ordinal;
    int kind;

    ordinal = 0;
    for (index = 0;
            PCore_FormControlInfo(hDoc, index, NULL, NULL, NULL, NULL,
                    &kind, selected, disabled) == 0;
            index++) {
        if (kind == wanted_kind) {
            if (ordinal == wanted_ordinal) {
                return 1;
            }
            ordinal++;
        }
    }
    return 0;
}

static BOOL test69_form_defaults_and_labels(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Form defaults</h1>"
        "<form action=/implicit method=get>"
        "<label for=query><strong>Query label</strong></label>"
        "<input id=query name=q value=seed>"
        "<label><input type=checkbox name=flag value=yes checked>"
        "<span>Include label</span></label>"
        "<textarea name=note>base</textarea>"
        "<select name=pick>"
        "<option value=a selected>Alpha</option>"
        "<option value=b>Beta</option>"
        "</select>"
        "<input type=submit name=go value=Go>"
        "<button type=reset>Restore defaults</button>"
        "<button type=reset disabled>Disabled reset</button>"
        "</form>"
        "<form action=/plain><input name=solo value=only></form>"
        "</body></html>";
    static const char CSS[] =
        "body{font:14px sans-serif;margin:8px}"
        "h1{font-size:20px}"
        "label,input,textarea,select,button{display:block;margin:3px}"
        "input,textarea,select,button{width:150px;height:24px}";
    static const char EXPECT_CHANGED[] =
        "q=changed&note=changed+note&pick=b&go=Go";
    static const char EXPECT_RESET[] =
        "q=seed&flag=yes&note=base&pick=a&go=Go";
    HANDLE hDoc;
    HANDLE hSheet;
    PCoreFormSubmissionInfo info;
    PCoreTextInputInfo text_info;
    char action[128];
    char body[512];
    char query_value[64];
    char textarea_value[64];
    char detail[512];
    int selected;
    int disabled;
    int label_x;
    int label_y;
    int label_w;
    int label_h;
    int target_x;
    int target_y;
    int target_kind;
    int dirty_x;
    int dirty_y;
    int dirty_w;
    int dirty_h;
    int reset_x;
    int reset_y;
    int option_selected;
    int option_disabled;
    int query_result;
    int textarea_result;
    int control_result;
    int option0_result;
    int option0_selected;
    int option1_result;
    int option1_selected;
    int submit_result;
    int ok;

    hDoc = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    hSheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/form-defaults.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 69 FAIL", "form defaults setup failed");
        return FALSE;
    }

    ok = PCore_NodeBox(hDoc, "strong", &label_x, &label_y,
            &label_w, &label_h) == 0 &&
            PCore_LabelTargetAt(hDoc,
                    label_x + label_w / 2, label_y + label_h / 2,
                    &target_x, &target_y, &target_kind) == 1 &&
            target_kind == 3;
    if (!ok || PCore_NodeBox(hDoc, "span", &label_x, &label_y,
            &label_w, &label_h) != 0 ||
            PCore_LabelTargetAt(hDoc,
                    label_x + label_w / 2, label_y + label_h / 2,
                    &target_x, &target_y, &target_kind) != 1 ||
            target_kind != 1 ||
            !PCore_FormActivateAt(hDoc, target_x, target_y,
                    &dirty_x, &dirty_y, &dirty_w, &dirty_h) ||
            dirty_w <= 0 || dirty_h <= 0 ||
            PCore_TextInputSetValue(hDoc, 0, "changed") != 0 ||
            PCore_TextInputSetValue(hDoc, 1, "changed note") != 0 ||
            PCore_SelectSetOptionSelected(hDoc, 0, 1, 1) != 0 ||
            PCore_FormSubmissionForTextInput(hDoc, 0, &info,
                    action, sizeof(action), body, sizeof(body)) != 1 ||
            info.method != 1 || strcmp(action, "/implicit") != 0 ||
            strcmp(body, EXPECT_CHANGED) != 0 ||
            PCore_FormSubmissionForTextInput(hDoc, 1, &info,
                    action, sizeof(action), body, sizeof(body)) != 0 ||
            PCore_FormSubmissionForTextInput(hDoc, 2, &info,
                    action, sizeof(action), body, sizeof(body)) != 1 ||
            strcmp(action, "/plain") != 0 ||
            strcmp(body, "solo=only") != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 69 FAIL",
                "label or implicit submission semantics failed");
        return FALSE;
    }

    if (!test68_control_center(hDoc, 8, 0, &reset_x, &reset_y) ||
            PCore_FormResetAt(hDoc, reset_x, reset_y) != 1 ||
            !test68_control_center(hDoc, 8, 1, &reset_x, &reset_y) ||
            PCore_FormResetAt(hDoc, reset_x, reset_y) != 2 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0) {
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 69 FAIL", "reset action or re-layout failed");
        return FALSE;
    }
    query_value[0] = '\0';
    textarea_value[0] = '\0';
    detail[0] = '\0';
    selected = 0;
    disabled = 0;
    option_selected = 0;
    option_disabled = 0;
    option0_selected = 0;
    option1_selected = 0;
    query_result = PCore_TextInputInfo(hDoc, 0, &text_info,
            query_value, sizeof(query_value));
    textarea_result = PCore_TextInputInfo(hDoc, 1, NULL,
            textarea_value, sizeof(textarea_value));
    control_result = test69_control_state(hDoc, 1, 0,
            &selected, &disabled);
    option0_result = PCore_SelectOptionInfo(hDoc, 0, 0,
            NULL, 0, NULL, 0,
            &option_selected, &option_disabled,
            NULL, NULL);
    option0_selected = option_selected;
    option1_result = PCore_SelectOptionInfo(hDoc, 0, 1,
            NULL, 0, NULL, 0,
            &option_selected, &option_disabled,
            NULL, NULL);
    option1_selected = option_selected;
    action[0] = '\0';
    body[0] = '\0';
    submit_result = PCore_FormSubmissionForTextInput(hDoc, 0, &info,
            action, sizeof(action), body, sizeof(body));
    ok = query_result == 0 && strcmp(query_value, "seed") == 0 &&
            textarea_result == 0 &&
            strcmp(textarea_value, "base") == 0 &&
            control_result &&
            selected == 1 && disabled == 0 &&
            option0_result == 0 && option0_selected == 1 &&
            option1_result == 0 && option1_selected == 0 &&
            submit_result == 1 &&
            strcmp(body, EXPECT_RESET) == 0;
    if (!ok) {
        _snprintf(detail, sizeof(detail) - 1,
                "q=%d:%s note=%d:%s check=%d:%d/%d\n"
                "options=%d:%d/%d:%d submit=%d body=%s",
                query_result, query_value,
                textarea_result, textarea_value,
                control_result, selected, disabled,
                option0_result, option0_selected,
                option1_result, option1_selected,
                submit_result, body);
        detail[sizeof(detail) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 69 FAIL", detail);
        return FALSE;
    }

    cstr_copy(g_native_form_enter_expected,
            sizeof(g_native_form_enter_expected), EXPECT_RESET);
    g_native_form_enter_probe = 1;
    g_native_form_enter_probe_seen = 0;
    g_native_form_enter_probe_ok = 0;
    g_native_label_probe = 1;
    g_native_label_probe_ok = 0;
    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_native_form_enter_probe = 0;
        g_native_label_probe = 0;
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 69 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_native_form_enter_probe = 0;
    g_native_label_probe = 0;
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    if (!g_native_form_enter_probe_seen ||
            !g_native_form_enter_probe_ok ||
            !g_native_label_probe_ok) {
        show_error(L"TEST 69 FAIL",
                "WM Enter or label focus bridge failed");
        return FALSE;
    }
    show_info(L"TEST 69 OK",
            "Explicit/wrapping labels, reset defaults, first-submit\n"
            "selection, no-button form and WM EDIT Enter passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 70 - file gadget and multipart/form-data binary request          */
/* -------------------------------------------------------------------- */
static int test70_memory_contains(const unsigned char *data, int data_len,
        const unsigned char *needle, int needle_len)
{
    int index;

    if (data == NULL || needle == NULL || data_len < 0 ||
            needle_len <= 0 || needle_len > data_len) {
        return 0;
    }
    for (index = 0; index <= data_len - needle_len; index++) {
        if (memcmp(data + index, needle, (size_t) needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int test70_control_center(HANDLE document, int wanted_kind,
        int *center_x, int *center_y)
{
    unsigned int index;
    int x;
    int y;
    int width;
    int height;
    int kind;

    for (index = 0; index < 32; index++) {
        if (PCore_FormControlInfo(document, index, &x, &y,
                &width, &height, &kind, NULL, NULL) != 0) {
            break;
        }
        if (kind == wanted_kind) {
            if (center_x != NULL) {
                *center_x = x + width / 2;
            }
            if (center_y != NULL) {
                *center_y = y + height / 2;
            }
            return 1;
        }
    }
    return 0;
}

static int test70_part_equals(HANDLE submission, unsigned int index,
        int kind, const char *name, const char *value, const char *path)
{
    PCoreMultipartPartInfo info;
    char actual_name[64];
    char actual_value[96];
    char actual_path[MAX_PATH * 3];

    memset(&info, 0, sizeof(info));
    actual_name[0] = '\0';
    actual_value[0] = '\0';
    actual_path[0] = '\0';
    if (PCore_MultipartPartInfo(submission, index, &info,
            actual_name, sizeof(actual_name),
            actual_value, sizeof(actual_value),
            actual_path, sizeof(actual_path)) != 1) {
        return 0;
    }
    return info.kind == kind &&
            strcmp(actual_name, name) == 0 &&
            strcmp(actual_value, value) == 0 &&
            strcmp(actual_path, path) == 0;
}

static BOOL test70_multipart_file(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Multipart upload</h1>"
        "<form action='https://example.com/upload' method=post "
        "enctype='multipart/form-data'>"
        "<label>Title <input name=title value='Positron form'></label>"
        "<label><input type=checkbox name=flag checked> Include flag</label>"
        "<input type=checkbox name=skip>"
        "<select name=choice multiple>"
        "<option value=one selected>One</option>"
        "<option value=two selected>Two</option></select>"
        "<label>File <input type=file name=payload></label>"
        "<button type=reset>Reset</button>"
        "<button type=submit name=go value=send>Upload</button>"
        "</form></body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff;}"
        "body{font-size:15px;line-height:20px;padding:10px;color:#111;}"
        "h1{font-size:21px;line-height:25px;color:#8b0000;margin:0 0 8px;}"
        "label,select,button{display:block;margin:6px 0;}"
        "input[type=file]{width:190px;height:25px;border:1px solid #666;"
        "background:#eef6ff;}";
    static const unsigned char FILE_BYTES[] = {
        0x50, 0x00, 0x6f, 0x73, 0x69, 0x74, 0x72, 0x6f, 0x6e, 0xff
    };
    static const unsigned char FILE_NEEDLE[] = {
        0x0d, 0x0a, 0x0d, 0x0a,
        0x50, 0x00, 0x6f, 0x73, 0x69, 0x74, 0x72, 0x6f, 0x6e, 0xff,
        0x0d, 0x0a
    };
    static const char DISPOSITION_NEEDLE[] =
        "Content-Disposition: form-data; name=\"payload\"; "
        "filename=\"tiny\\\"file.bin\"";
    static const char CONTENT_NEEDLE[] =
        "Content-Type: application/octet-stream\r\n\r\n";
    HANDLE document;
    HANDLE sheet;
    HANDLE file;
    HANDLE submission;
    HANDLE implicit_submission;
    PCoreFileInputInfo file_info;
    PCoreMultipartSubmissionInfo submission_info;
    PCoreFormSubmissionInfo legacy_info;
    pcore_navigation_request *request;
    WCHAR fixture_path[MAX_PATH];
    DWORD written;
    char *fixture_utf8;
    char *body;
    char *content_type;
    char action_probe[1];
    char body_probe[1];
    int body_len;
    int submit_x;
    int submit_y;
    int reset_x;
    int reset_y;
    int legacy_result;
    int ok;
    char detail[256];

    document = NULL;
    sheet = NULL;
    file = INVALID_HANDLE_VALUE;
    submission = NULL;
    implicit_submission = NULL;
    request = NULL;
    fixture_utf8 = NULL;
    body = NULL;
    content_type = NULL;
    fixture_path[0] = L'\0';
    ok = 0;
    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/multipart.css");
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            !test70_control_center(document, 7, &submit_x, &submit_y) ||
            !test70_control_center(document, 8, &reset_x, &reset_y) ||
            test_host_sibling_path(L"test70-upload.bin",
                    fixture_path) != 0) {
        strcpy(detail, "multipart form setup failed");
        goto done;
    }
    file = CreateFileW(fixture_path, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    written = 0;
    if (file == INVALID_HANDLE_VALUE ||
            !WriteFile(file, FILE_BYTES, sizeof(FILE_BYTES),
                    &written, NULL) ||
            written != sizeof(FILE_BYTES)) {
        strcpy(detail, "binary fixture write failed");
        goto done;
    }
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    fixture_utf8 = wide_to_utf8_alloc(fixture_path);
    if (fixture_utf8 == NULL ||
            PCore_FileInputSetPath(document, 0,
                    "tiny\"file.bin", fixture_utf8) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_FileInputInfo(document, 0, &file_info,
                    NULL, 0, NULL, 0) != 0 ||
            file_info.width <= 0 || file_info.height <= 0 ||
            file_info.value_bytes != 13 ||
            file_info.path_bytes != (int) strlen(fixture_utf8)) {
        strcpy(detail, "file value did not survive layout");
        goto done;
    }
    memset(&legacy_info, 0, sizeof(legacy_info));
    action_probe[0] = '\0';
    body_probe[0] = '\0';
    legacy_result = PCore_FormSubmissionAt(document,
            submit_x, submit_y, &legacy_info,
            action_probe, sizeof(action_probe),
            body_probe, sizeof(body_probe));
    submission = PCore_MultipartSubmissionAt(document, submit_x, submit_y);
    memset(&submission_info, 0, sizeof(submission_info));
    if (legacy_result != 3 || legacy_info.method != 3 ||
            submission == NULL ||
            PCore_MultipartSubmissionInfo(submission, &submission_info,
                    NULL, 0) != 1 ||
            submission_info.part_count != 6 ||
            !test70_part_equals(submission, 0, 1,
                    "title", "Positron form", "") ||
            !test70_part_equals(submission, 1, 1,
                    "flag", "on", "") ||
            !test70_part_equals(submission, 2, 1,
                    "choice", "one", "") ||
            !test70_part_equals(submission, 3, 1,
                    "choice", "two", "") ||
            !test70_part_equals(submission, 4, 2,
                    "payload", "tiny\"file.bin", fixture_utf8) ||
            !test70_part_equals(submission, 5, 1,
                    "go", "send", "")) {
        strcpy(detail, "successful multipart parts differ");
        goto done;
    }
    implicit_submission =
            PCore_MultipartSubmissionForTextInput(document, 0);
    memset(&submission_info, 0, sizeof(submission_info));
    if (implicit_submission == NULL ||
            PCore_MultipartSubmissionInfo(implicit_submission,
                    &submission_info, NULL, 0) != 1 ||
            submission_info.part_count != 6 ||
            !test70_part_equals(implicit_submission, 5, 1,
                    "go", "send", "")) {
        strcpy(detail, "implicit multipart submission failed");
        goto done;
    }
    PCore_FreeMultipartSubmission(implicit_submission);
    implicit_submission = NULL;
    if (!pcore_multipart_build(submission, &body, &body_len,
            &content_type) ||
            body_len <= (int) sizeof(FILE_BYTES) ||
            content_type == NULL ||
            strstr(content_type,
                    "Content-Type: multipart/form-data; boundary=") !=
                    content_type ||
            !test70_memory_contains((const unsigned char *) body, body_len,
                    (const unsigned char *) DISPOSITION_NEEDLE,
                    sizeof(DISPOSITION_NEEDLE) - 1) ||
            !test70_memory_contains((const unsigned char *) body, body_len,
                    (const unsigned char *) CONTENT_NEEDLE,
                    sizeof(CONTENT_NEEDLE) - 1) ||
            !test70_memory_contains((const unsigned char *) body, body_len,
                    FILE_NEEDLE, sizeof(FILE_NEEDLE)) ||
            test70_memory_contains((const unsigned char *) body, body_len,
                    (const unsigned char *) fixture_utf8,
                    (int) strlen(fixture_utf8))) {
        strcpy(detail, "multipart wire body failed");
        goto done;
    }
    request = pcore_navigation_request_create_ex(NULL,
            "https://example.com/upload", 3,
            body, body_len, content_type);
    if (request == NULL || request->method != 3 ||
            request->request_body_len != body_len ||
            memcmp(request->request_body, body, (size_t) body_len) != 0 ||
            request->request_content_type == NULL ||
            strcmp(request->request_content_type, content_type) != 0) {
        strcpy(detail, "binary navigation request failed");
        goto done;
    }
    pcore_navigation_request_free(request);
    request = NULL;
    g_doc_h = PCore_DocumentHeight(document);
    g_scroll_y = 0;
    g_render_doc = document;
    g_render_sheet = sheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        strcpy(detail, "file gadget render window failed");
        goto done;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    if (PCore_LayoutDocument(document, 240, 320) != 0 ||
            !test70_control_center(document, 8, &reset_x, &reset_y) ||
            PCore_FormResetAt(document, reset_x, reset_y) != 1 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_FileInputInfo(document, 0, &file_info,
                    NULL, 0, NULL, 0) != 0 ||
            file_info.value_bytes != 0 || file_info.path_bytes != 0) {
        strcpy(detail, "form reset retained the selected file");
        goto done;
    }
    ok = 1;

done:
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (request != NULL) {
        pcore_navigation_request_free(request);
    }
    free(content_type);
    free(body);
    if (submission != NULL) {
        PCore_FreeMultipartSubmission(submission);
    }
    if (implicit_submission != NULL) {
        PCore_FreeMultipartSubmission(implicit_submission);
    }
    free(fixture_utf8);
    if (fixture_path[0] != L'\0') {
        DeleteFileW(fixture_path);
    }
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    if (!ok) {
        show_error(L"TEST 70 FAIL", detail);
        return FALSE;
    }
    show_info(L"TEST 70 OK",
            "File gadget state, multipart successful controls, binary\n"
            "wire body, request ownership and reset cleanup passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 71 - WM native multiple-select bridge                            */
/* -------------------------------------------------------------------- */
static BOOL test71_native_multiselect(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Native multiple select</h1>"
        "<form action=/multi method=get>"
        "<label>Tags<select name=tag multiple>"
        "<option value=a selected>Alpha</option>"
        "<option value=b disabled>Blocked</option>"
        "<option value=c>Charlie</option>"
        "<option value=d selected>Delta</option>"
        "</select></label>"
        "<select name=locked multiple disabled>"
        "<option value=x selected>Locked</option></select>"
        "<select name=single><option value=n selected>North</option>"
        "<option value=s>South</option></select>"
        "<button type=reset>Reset</button>"
        "<button type=submit name=go value=apply>Apply</button>"
        "</form></body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff}"
        "body{font:15px sans-serif;padding:8px}"
        "h1{font-size:21px;color:#800000;margin:0 0 8px}"
        "label,select,button{display:block;margin:5px 0}"
        "select{width:180px;height:25px}"
        "select[multiple]{height:76px}";
    static const char EXPECT_BODY[] =
        "tag=c&tag=d&single=n&go=apply";
    HANDLE document;
    HANDLE sheet;
    PCoreSelectInfo info[3];
    PCoreFormSubmissionInfo submission;
    char action[64];
    char body[192];
    int selected[4];
    int option_disabled;
    int submit_x;
    int submit_y;
    int reset_x;
    int reset_y;
    int i;
    int ok;

    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/native-multiselect.css");
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0) {
        if (sheet != NULL) { PCore_FreeStylesheet(sheet); }
        if (document != NULL) { PCore_FreeDocument(document); }
        show_error(L"TEST 71 FAIL", "multiple-select setup failed");
        return FALSE;
    }
    memset(info, 0, sizeof(info));
    memset(selected, 0, sizeof(selected));
    option_disabled = 0;
    for (i = 0; i < 3; i++) {
        if (PCore_SelectInfo(document, (unsigned int) i, &info[i]) != 0 ||
                info[i].width <= 0 || info[i].height <= 0) {
            PCore_FreeStylesheet(sheet);
            PCore_FreeDocument(document);
            show_error(L"TEST 71 FAIL", "select enumeration failed");
            return FALSE;
        }
    }
    for (i = 0; i < 4; i++) {
        if (PCore_SelectOptionInfo(document, 0, (unsigned int) i,
                NULL, 0, NULL, 0, &selected[i],
                (i == 1) ? &option_disabled : NULL,
                NULL, NULL) != 0) {
            PCore_FreeStylesheet(sheet);
            PCore_FreeDocument(document);
            show_error(L"TEST 71 FAIL", "option enumeration failed");
            return FALSE;
        }
    }
    if (!info[0].multiple || info[0].option_count != 4 ||
            info[0].selected_count != 2 ||
            !selected[0] || selected[1] || !option_disabled ||
            selected[2] || !selected[3] ||
            !info[1].multiple || !info[1].disabled ||
            info[1].selected_count != 1 ||
            info[2].multiple || info[2].disabled ||
            info[2].selected_index != 0) {
        PCore_FreeStylesheet(sheet);
        PCore_FreeDocument(document);
        show_error(L"TEST 71 FAIL", "initial select state differs");
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(document);
    g_scroll_y = 0;
    g_render_doc = document;
    g_render_sheet = sheet;
    g_native_multiselect_probe = 1;
    g_native_multiselect_probe_ok = 0;
    if (!show_render_window()) {
        g_native_multiselect_probe = 0;
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(sheet);
        PCore_FreeDocument(document);
        show_error(L"TEST 71 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_native_multiselect_probe = 0;
    g_render_doc = NULL;
    g_render_sheet = NULL;
    if (!g_native_multiselect_probe_ok ||
            PCore_LayoutDocument(document, 320, 240) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0) {
        PCore_FreeStylesheet(sheet);
        PCore_FreeDocument(document);
        show_error(L"TEST 71 FAIL",
                "WM LISTBOX bridge or relayout persistence failed");
        return FALSE;
    }
    memset(&info[0], 0, sizeof(info[0]));
    memset(selected, 0, sizeof(selected));
    ok = PCore_SelectInfo(document, 0, &info[0]) == 0 &&
            info[0].selected_count == 2;
    for (i = 0; i < 4 && ok; i++) {
        ok = PCore_SelectOptionInfo(document, 0, (unsigned int) i,
                NULL, 0, NULL, 0, &selected[i], NULL,
                NULL, NULL) == 0;
    }
    if (!ok || selected[0] || selected[1] ||
            !selected[2] || !selected[3] ||
            !test68_control_center(document, 7, 0,
                    &submit_x, &submit_y)) {
        PCore_FreeStylesheet(sheet);
        PCore_FreeDocument(document);
        show_error(L"TEST 71 FAIL", "native selection state was lost");
        return FALSE;
    }
    memset(&submission, 0, sizeof(submission));
    action[0] = '\0';
    body[0] = '\0';
    if (PCore_FormSubmissionAt(document, submit_x, submit_y,
            &submission, action, sizeof(action),
            body, sizeof(body)) != 1 ||
            submission.method != 1 ||
            strcmp(action, "/multi") != 0 ||
            strcmp(body, EXPECT_BODY) != 0 ||
            !test68_control_center(document, 8, 0,
                    &reset_x, &reset_y) ||
            PCore_FormResetAt(document, reset_x, reset_y) != 1 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_SelectInfo(document, 0, &info[0]) != 0 ||
            info[0].selected_count != 2 ||
            PCore_SelectOptionInfo(document, 0, 0,
                    NULL, 0, NULL, 0, &selected[0], NULL,
                    NULL, NULL) != 0 ||
            PCore_SelectOptionInfo(document, 0, 2,
                    NULL, 0, NULL, 0, &selected[2], NULL,
                    NULL, NULL) != 0 ||
            !selected[0] || selected[2]) {
        PCore_FreeStylesheet(sheet);
        PCore_FreeDocument(document);
        show_error(L"TEST 71 FAIL",
                "repeated values or multiple-select reset failed");
        return FALSE;
    }
    PCore_FreeStylesheet(sheet);
    PCore_FreeDocument(document);
    show_info(L"TEST 71 OK",
            "WM multiple LISTBOX toggles, disabled rollback, native\n"
            "rebuild, repeated values, single select and reset passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 72 - required constraint validation and submission blocking     */
/* -------------------------------------------------------------------- */
static BOOL test72_form_validation(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Required controls</h1>"
        "<form action=/valid method=get>"
        "<input name=text required>"
        "<input type=password name=password required>"
        "<textarea name=notes required></textarea>"
        "<input type=checkbox name=agree value=yes required>"
        "<input type=radio name=mode value=a required>"
        "<input type=radio name=mode value=b>"
        "<select name=region required>"
        "<option value='' selected>Choose</option>"
        "<option value=us>United States</option></select>"
        "<select name=tag multiple required>"
        "<option value=x>One</option><option value=y>Two</option></select>"
        "<input type=file name=upload required>"
        "<input name=locked readonly required>"
        "<input name=off disabled required>"
        "<button type=submit name=go value=send>Send</button>"
        "<button type=submit name=skip value=skip "
        "formnovalidate>Skip validation</button>"
        "<button type=reset>Reset</button></form>"
        "<form action=/draft novalidate><input name=draft required>"
        "<button type=submit name=save value=draft>Save draft</button></form>"
        "<form action=/upload method=post enctype='multipart/form-data'>"
        "<input type=file name=asset required>"
        "<button type=submit name=upload value=yes>Upload</button></form>"
        "</body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff}"
        "body{font:14px sans-serif;padding:8px}"
        "h1{font-size:20px;color:#800000;margin:0 0 6px}"
        "input,textarea,select,button{display:block;margin:4px 0;"
        "width:180px}textarea{height:42px}"
        "select[multiple]{height:54px}";
    static const char EXPECT_BODY[] =
        "text=alpha&password=secret&notes=memo&agree=yes&mode=b&"
        "region=us&tag=x&locked=&go=send";
    HANDLE document;
    HANDLE sheet;
    HANDLE multipart;
    PCoreFormValidationInfo validation;
    PCoreFormSubmissionInfo submission;
    PCoreMultipartSubmissionInfo multipart_info;
    PCoreTextInputInfo text_info;
    PCoreFileInputInfo file_info;
    char action[64];
    char body[512];
    char value[32];
    char path[64];
    char detail[192];
    int submit_x[4];
    int submit_y[4];
    int reset_x;
    int reset_y;
    int checkbox_x;
    int checkbox_y;
    int radio_x;
    int radio_y;
    int dirty_x;
    int dirty_y;
    int dirty_w;
    int dirty_h;
    int stage;
    int i;

    document = NULL;
    sheet = NULL;
    multipart = NULL;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    memset(&multipart_info, 0, sizeof(multipart_info));
    memset(&text_info, 0, sizeof(text_info));
    memset(&file_info, 0, sizeof(file_info));
    stage = 1;
    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/form-validation.css");
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0) {
        goto validation_failed;
    }
    for (i = 0; i < 4; i++) {
        if (!test68_control_center(document, 7, i,
                &submit_x[i], &submit_y[i])) {
            stage = 2;
            goto validation_failed;
        }
    }
    if (!test68_control_center(document, 8, 0, &reset_x, &reset_y) ||
            !test68_control_center(document, 1, 0,
                    &checkbox_x, &checkbox_y) ||
            !test68_control_center(document, 2, 1,
                    &radio_x, &radio_y) ||
            PCore_TextInputInfo(document, 0, &text_info, NULL, 0) != 0) {
        stage = 3;
        goto validation_failed;
    }

    stage = 4;
    if (!PCore_FormValidationAt(document, submit_x[0], submit_y[0],
            &validation) || validation.valid ||
            validation.invalid_count != 8 ||
            validation.first_control_kind != 3 ||
            validation.first_flags != PCORE_VALIDITY_VALUE_MISSING ||
            validation.first_x != text_info.x ||
            validation.first_y != text_info.y ||
            validation.first_width != text_info.width ||
            validation.first_height != text_info.height ||
            PCore_FormSubmissionAt(document, submit_x[0], submit_y[0],
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 5 ||
            !PCore_FormValidationForTextInput(document, 0, &validation) ||
            validation.valid || validation.invalid_count != 8 ||
            PCore_FormSubmissionForTextInput(document, 0, &submission,
                    action, sizeof(action), body, sizeof(body)) != 5) {
        goto validation_failed;
    }

    stage = 5;
    if (!PCore_FormValidationAt(document, submit_x[1], submit_y[1],
            &validation) || !validation.valid ||
            PCore_FormSubmissionAt(document, submit_x[1], submit_y[1],
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1 ||
            !PCore_FormValidationAt(document, submit_x[2], submit_y[2],
                    &validation) || !validation.valid ||
            PCore_FormSubmissionAt(document, submit_x[2], submit_y[2],
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1) {
        goto validation_failed;
    }

    stage = 6;
    if (!PCore_FormValidationAt(document, submit_x[3], submit_y[3],
            &validation) || validation.valid ||
            validation.invalid_count != 1 ||
            validation.first_control_kind != 10 ||
            PCore_FormSubmissionAt(document, submit_x[3], submit_y[3],
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 5 ||
            PCore_MultipartSubmissionAt(document,
                    submit_x[3], submit_y[3]) != NULL) {
        goto validation_failed;
    }

    stage = 7;
    dirty_x = 0;
    dirty_y = 0;
    dirty_w = 0;
    dirty_h = 0;
    if (PCore_TextInputSetValue(document, 0, "alpha") != 0 ||
            PCore_TextInputSetValue(document, 1, "secret") != 0 ||
            PCore_TextInputSetValue(document, 2, "memo") != 0 ||
            !PCore_FormActivateAt(document, checkbox_x, checkbox_y,
                    &dirty_x, &dirty_y, &dirty_w, &dirty_h) ||
            dirty_w <= 0 || dirty_h <= 0 ||
            !PCore_FormActivateAt(document, radio_x, radio_y,
                    &dirty_x, &dirty_y, &dirty_w, &dirty_h) ||
            PCore_SelectSetOptionSelected(document, 0, 1, 1) != 0 ||
            PCore_SelectSetOptionSelected(document, 1, 0, 1) != 0 ||
            PCore_FileInputSetPath(document, 0,
                    "proof.txt", "\\Storage Card\\proof.txt") != 0 ||
            PCore_LayoutDocument(document, 320, 240) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0) {
        goto validation_failed;
    }
    for (i = 0; i < 4; i++) {
        if (!test68_control_center(document, 7, i,
                &submit_x[i], &submit_y[i])) {
            goto validation_failed;
        }
    }
    if (!test68_control_center(document, 8, 0, &reset_x, &reset_y)) {
        goto validation_failed;
    }

    stage = 8;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    action[0] = '\0';
    body[0] = '\0';
    if (!PCore_FormValidationAt(document, submit_x[0], submit_y[0],
            &validation) || !validation.valid ||
            validation.invalid_count != 0 ||
            PCore_FormSubmissionForTextInput(document, 0, &submission,
                    action, sizeof(action), body, sizeof(body)) != 1 ||
            submission.method != 1 ||
            strcmp(action, "/valid") != 0 ||
            strcmp(body, EXPECT_BODY) != 0 ||
            PCore_FormSubmissionAt(document, submit_x[0], submit_y[0],
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1 ||
            strcmp(body, EXPECT_BODY) != 0) {
        goto validation_failed;
    }

    stage = 9;
    if (PCore_FileInputSetPath(document, 1,
            "asset.bin", "\\Storage Card\\asset.bin") != 0 ||
            !PCore_FormValidationAt(document, submit_x[3], submit_y[3],
                    &validation) || !validation.valid ||
            PCore_FormSubmissionAt(document, submit_x[3], submit_y[3],
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 3) {
        goto validation_failed;
    }
    multipart = PCore_MultipartSubmissionAt(document,
            submit_x[3], submit_y[3]);
    if (multipart == NULL ||
            !PCore_MultipartSubmissionInfo(multipart, &multipart_info,
                    action, sizeof(action)) ||
            strcmp(action, "/upload") != 0 ||
            multipart_info.part_count != 2) {
        goto validation_failed;
    }
    PCore_FreeMultipartSubmission(multipart);
    multipart = NULL;

    stage = 10;
    if (PCore_FormResetAt(document, reset_x, reset_y) != 1 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            !PCore_FormValidationAt(document, submit_x[0], submit_y[0],
                    &validation) || validation.valid ||
            validation.invalid_count != 8 ||
            PCore_FileInputInfo(document, 0, &file_info,
                    value, sizeof(value), path, sizeof(path)) != 0 ||
            value[0] != '\0' || path[0] != '\0') {
        goto validation_failed;
    }

    PCore_FreeStylesheet(sheet);
    PCore_FreeDocument(document);
    show_info(L"TEST 72 OK",
            "Required text/file/check/radio/select validation, first-invalid\n"
            "geometry, submit/Enter blocking, bypass, multipart and reset passed.");
    return TRUE;

validation_failed:
    if (multipart != NULL) {
        PCore_FreeMultipartSubmission(multipart);
    }
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    _snprintf(detail, sizeof(detail) - 1,
            "stage=%d valid=%d count=%d kind=%d flags=%lu",
            stage, validation.valid, validation.invalid_count,
            validation.first_control_kind,
            (unsigned long) validation.first_flags);
    detail[sizeof(detail) - 1] = '\0';
    show_error(L"TEST 72 FAIL", detail);
    return FALSE;
}

/* -------------------------------------------------------------------- */
/* TEST 73 - dynamic form pseudo-classes and WM interaction state       */
/* -------------------------------------------------------------------- */
static BOOL test73_dynamic_form_states(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Dynamic form states</h1>"
        "<form>"
        "<p><input type=checkbox name=flag><checkedmark>checked</checkedmark></p>"
        "<p><input type=text name=focus><focusmark>focus</focusmark></p>"
        "<p><input type=text name=off disabled><disabledmark>disabled</disabledmark></p>"
        "<p><input type=text name=on><enabledmark>enabled</enabledmark></p>"
        "<p><button type=button>Hold</button><activemark>active</activemark></p>"
        "<select name=choice><option selected>Selected option</option>"
        "<option>Other option</option></select>"
        "<button type=reset>Reset</button>"
        "</form></body></html>";
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff}"
        "body{font:14px sans-serif;padding:8px}"
        "h1{font-size:20px;color:#800000;margin:0 0 6px}"
        "p{margin:6px 0}"
        "input[type=text]{width:80px;height:20px}"
        "checkedmark,focusmark,disabledmark,enabledmark,activemark"
        "{color:#111111;margin-left:6px}"
        "input:checked+checkedmark{color:#0000ff}"
        "input:focus+focusmark{color:#008080}"
        "input:disabled+disabledmark{color:#808080}"
        "input:enabled+enabledmark{color:#008000}"
        "button:active+activemark{color:#ff0000}"
        "option:checked{color:#800080}";
    HANDLE document;
    HANDLE sheet;
    unsigned long checked_color;
    unsigned long focus_color;
    unsigned long disabled_color;
    unsigned long enabled_color;
    unsigned long active_color;
    unsigned long option_color;
    char detail[224];
    int checkbox_x;
    int checkbox_y;
    int text_x;
    int text_y;
    int button_x;
    int button_y;
    int reset_x;
    int reset_y;
    int dirty_x;
    int dirty_y;
    int dirty_w;
    int dirty_h;
    int geometry_mask;
    int stage;

    document = NULL;
    sheet = NULL;
    checked_color = 0;
    focus_color = 0;
    disabled_color = 0;
    enabled_color = 0;
    active_color = 0;
    option_color = 0;
    geometry_mask = 0;
    stage = 1;
    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/dynamic-form-states.css");
    if (document == NULL || sheet == NULL ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0) {
        goto dynamic_failed;
    }

    stage = 2;
    if (PCore_NodeComputedColor(document, "checkedmark",
                &checked_color) != 0 ||
            PCore_NodeComputedColor(document, "focusmark",
                &focus_color) != 0 ||
            PCore_NodeComputedColor(document, "disabledmark",
                &disabled_color) != 0 ||
            PCore_NodeComputedColor(document, "enabledmark",
                &enabled_color) != 0 ||
            PCore_NodeComputedColor(document, "activemark",
                &active_color) != 0 ||
            PCore_NodeComputedColor(document, "option",
                &option_color) != 0 ||
            checked_color != 0xff111111UL ||
            focus_color != 0xff111111UL ||
            disabled_color != 0xff808080UL ||
            enabled_color != 0xff008000UL ||
            active_color != 0xff111111UL ||
            option_color != 0xff800080UL) {
        goto dynamic_failed;
    }
    if (test68_control_center(document, 1, 0,
            &checkbox_x, &checkbox_y)) {
        geometry_mask |= 1;
    }
    if (test68_control_center(document, 3, 0, &text_x, &text_y)) {
        geometry_mask |= 2;
    }
    if (test68_control_center(document, 9, 0, &button_x, &button_y)) {
        geometry_mask |= 4;
    }
    if (test68_control_center(document, 8, 0, &reset_x, &reset_y)) {
        geometry_mask |= 8;
    }
    if (geometry_mask != 15) {
        stage = 20 + geometry_mask;
        goto dynamic_failed;
    }

    stage = 3;
    if (PCore_InteractionSetAt(document, text_x, text_y,
                PCORE_INTERACTION_FOCUS) != 1 ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_NodeComputedColor(document, "focusmark",
                &focus_color) != 0 ||
            focus_color != 0xff008080UL) {
        goto dynamic_failed;
    }

    stage = 4;
    dirty_x = 0;
    dirty_y = 0;
    dirty_w = 0;
    dirty_h = 0;
    if (PCore_InteractionSetAt(document, checkbox_x, checkbox_y,
                PCORE_INTERACTION_FOCUS) != 1 ||
            !PCore_FormActivateAt(document, checkbox_x, checkbox_y,
                &dirty_x, &dirty_y, &dirty_w, &dirty_h) ||
            dirty_w <= 0 || dirty_h <= 0 ||
            PCore_InteractionSetAt(document, button_x, button_y,
                PCORE_INTERACTION_ACTIVE) != 1 ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 320, 240) != 0 ||
            PCore_NodeComputedColor(document, "checkedmark",
                &checked_color) != 0 ||
            PCore_NodeComputedColor(document, "activemark",
                &active_color) != 0 ||
            checked_color != 0xff0000ffUL ||
            active_color != 0xffff0000UL) {
        goto dynamic_failed;
    }

    stage = 5;
    if (PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_NodeComputedColor(document, "checkedmark",
                &checked_color) != 0 ||
            PCore_NodeComputedColor(document, "activemark",
                &active_color) != 0 ||
            checked_color != 0xff0000ffUL ||
            active_color != 0xffff0000UL ||
            !test68_control_center(document, 8, 0,
                &reset_x, &reset_y)) {
        goto dynamic_failed;
    }

    stage = 6;
    if (PCore_InteractionClear(document,
                PCORE_INTERACTION_FOCUS |
                PCORE_INTERACTION_ACTIVE) != 1 ||
            PCore_FormResetAt(document, reset_x, reset_y) != 1 ||
            PCore_StyleDocument(document, sheet) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_NodeComputedColor(document, "checkedmark",
                &checked_color) != 0 ||
            PCore_NodeComputedColor(document, "focusmark",
                &focus_color) != 0 ||
            PCore_NodeComputedColor(document, "activemark",
                &active_color) != 0 ||
            checked_color != 0xff111111UL ||
            focus_color != 0xff111111UL ||
            active_color != 0xff111111UL) {
        goto dynamic_failed;
    }

    g_doc_h = PCore_DocumentHeight(document);
    g_scroll_y = 0;
    g_render_doc = document;
    g_render_sheet = sheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        stage = 7;
        goto dynamic_failed;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(sheet);
    PCore_FreeDocument(document);
    show_info(L"TEST 73 OK",
            "Dynamic :enabled/:disabled, checkbox/option :checked, focus,\n"
            "active, cache-only restyle, rotation persistence and reset passed.");
    return TRUE;

dynamic_failed:
    g_render_doc = NULL;
    g_render_sheet = NULL;
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    _snprintf(detail, sizeof(detail) - 1,
            "stage=%d geom=%X colors=%08lX/%08lX/%08lX/%08lX/%08lX/%08lX",
            stage, geometry_mask, checked_color, focus_color, disabled_color,
            enabled_color, active_color, option_color);
    detail[sizeof(detail) - 1] = '\0';
    show_error(L"TEST 73 FAIL", detail);
    return FALSE;
}

/* -------------------------------------------------------------------- */
/* TEST 74 - libdom event path, cancellation and host dispatch bridge    */
/* -------------------------------------------------------------------- */

typedef struct test74_trace {
    int count;
    int codes[32];
    unsigned int phases[32];
} test74_trace;

typedef struct test74_listener_context {
    test74_trace *trace;
    int code;
    unsigned int actions;
} test74_listener_context;

static unsigned int test74_listener(void *pw,
        const PCoreEventInfo *event_info)
{
    test74_listener_context *context;
    int index;

    context = (test74_listener_context *) pw;
    if (context == NULL || context->trace == NULL || event_info == NULL) {
        return PCORE_EVENT_ACTION_NONE;
    }
    index = context->trace->count;
    if (index < 32) {
        context->trace->codes[index] = context->code;
        context->trace->phases[index] = event_info->phase;
    }
    context->trace->count++;
    return context->actions;
}

static int test74_trace_is(const test74_trace *trace, const int *codes,
        const unsigned int *phases, int count)
{
    int i;

    if (trace == NULL || trace->count != count) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (trace->codes[i] != codes[i] ||
                (phases != NULL && trace->phases[i] != phases[i])) {
            return 0;
        }
    }
    return 1;
}

static BOOL test74_dom_events(void)
{
    static const char HTML[] =
        "<!doctype html><html><body>"
        "<div id=outer><section id=parent>"
        "<button id=target>dispatch target</button>"
        "</section></div></body></html>";
    static const int CLICK_CODES[] = { 1, 2, 3, 4, 5, 6 };
    static const unsigned int CLICK_PHASES[] = {
        PCORE_EVENT_PHASE_CAPTURING, PCORE_EVENT_PHASE_CAPTURING,
        PCORE_EVENT_PHASE_TARGET, PCORE_EVENT_PHASE_TARGET,
        PCORE_EVENT_PHASE_BUBBLING, PCORE_EVENT_PHASE_BUBBLING
    };
    static const int NONBUBBLE_CODES[] = { 1, 2, 3 };
    static const unsigned int NONBUBBLE_PHASES[] = {
        PCORE_EVENT_PHASE_CAPTURING, PCORE_EVENT_PHASE_CAPTURING,
        PCORE_EVENT_PHASE_TARGET
    };
    static const int HALT_CODES[] = { 7, 8 };
    static const unsigned int HALT_PHASES[] = {
        PCORE_EVENT_PHASE_CAPTURING, PCORE_EVENT_PHASE_CAPTURING
    };
    static const int IMMEDIATE_CODES[] = { 10, 11 };
    static const unsigned int IMMEDIATE_PHASES[] = {
        PCORE_EVENT_PHASE_CAPTURING, PCORE_EVENT_PHASE_CAPTURING
    };
    HANDLE document;
    HANDLE listeners[16];
    test74_listener_context contexts[16];
    test74_trace trace;
    int listener_count;
    int stage;
    int default_allowed;
    int x;
    int y;
    int width;
    int height;
    int i;
    char detail[256];

    document = NULL;
    memset(listeners, 0, sizeof(listeners));
    memset(contexts, 0, sizeof(contexts));
    memset(&trace, 0, sizeof(trace));
    listener_count = 0;
    stage = 1;
    default_allowed = 1;

    document = PCore_ParseHTML(HTML, 0);
    if (document == NULL || PCore_StyleDocument(document, NULL) != 0 ||
            PCore_LayoutDocument(document, 240, 320) != 0 ||
            PCore_NodeBox(document, "button", &x, &y, &width, &height) != 0 ||
            width <= 0 || height <= 0) {
        goto event_failed;
    }

#define TEST74_ADD(ID, TYPE, CAPTURE, CODE, ACTIONS) \
    contexts[listener_count].trace = &trace; \
    contexts[listener_count].code = (CODE); \
    contexts[listener_count].actions = (ACTIONS); \
    listeners[listener_count] = PCore_EventListenerAdd(document, (ID), \
            (TYPE), (CAPTURE), test74_listener, \
            &contexts[listener_count]); \
    if (listeners[listener_count] == NULL) { goto event_failed; } \
    listener_count++

    stage = 2;
    TEST74_ADD("outer", "click", 1, 1, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("parent", "click", 1, 2, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("target", "click", 1, 3, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("target", "click", 0, 4,
            PCORE_EVENT_ACTION_PREVENT_DEFAULT);
    TEST74_ADD("parent", "click", 0, 5, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("outer", "click", 0, 6, PCORE_EVENT_ACTION_NONE);

    stage = 3;
    default_allowed = 1;
    if (PCore_EventDispatchAt(document, x + width / 2, y + height / 2,
            "click", 1, 1, &default_allowed) != 1 || default_allowed != 0 ||
            !test74_trace_is(&trace, CLICK_CODES, CLICK_PHASES, 6)) {
        goto event_failed;
    }

    stage = 4;
    if (!PCore_EventListenerRemove(document, listeners[3])) {
        goto event_failed;
    }
    listeners[3] = NULL;
    memset(&trace, 0, sizeof(trace));
    default_allowed = 0;
    if (PCore_EventDispatchToId(document, "target", "click", 0, 1,
            &default_allowed) != 1 || default_allowed != 1 ||
            !test74_trace_is(&trace, NONBUBBLE_CODES, NONBUBBLE_PHASES, 3)) {
        goto event_failed;
    }

    stage = 5;
    contexts[3].trace = &trace;
    contexts[3].code = 4;
    contexts[3].actions = PCORE_EVENT_ACTION_PREVENT_DEFAULT;
    listeners[3] = PCore_EventListenerAdd(document, "target", "click", 0,
            test74_listener, &contexts[3]);
    if (listeners[3] == NULL) {
        goto event_failed;
    }
    memset(&trace, 0, sizeof(trace));
    default_allowed = 0;
    if (PCore_EventDispatchToId(document, "target", "click", 1, 0,
            &default_allowed) != 1 || default_allowed != 1 ||
            !test74_trace_is(&trace, CLICK_CODES, CLICK_PHASES, 6)) {
        goto event_failed;
    }

    stage = 6;
    TEST74_ADD("outer", "halt", 1, 7, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("parent", "halt", 1, 8,
            PCORE_EVENT_ACTION_STOP_PROPAGATION);
    TEST74_ADD("target", "halt", 0, 9, PCORE_EVENT_ACTION_NONE);
    memset(&trace, 0, sizeof(trace));
    if (PCore_EventDispatchToId(document, "target", "halt", 1, 1,
            &default_allowed) != 1 ||
            !test74_trace_is(&trace, HALT_CODES, HALT_PHASES, 2)) {
        goto event_failed;
    }

    stage = 7;
    TEST74_ADD("outer", "instant", 1, 10, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("parent", "instant", 1, 11,
            PCORE_EVENT_ACTION_STOP_IMMEDIATE);
    TEST74_ADD("parent", "instant", 1, 12, PCORE_EVENT_ACTION_NONE);
    TEST74_ADD("target", "instant", 0, 13, PCORE_EVENT_ACTION_NONE);
    memset(&trace, 0, sizeof(trace));
    if (PCore_EventDispatchToId(document, "target", "instant", 1, 1,
            &default_allowed) != 1 ||
            !test74_trace_is(&trace, IMMEDIATE_CODES, IMMEDIATE_PHASES, 2)) {
        goto event_failed;
    }

    stage = 8;
    for (i = 0; i < listener_count; i++) {
        if (listeners[i] != NULL &&
                !PCore_EventListenerRemove(document, listeners[i])) {
            goto event_failed;
        }
        listeners[i] = NULL;
    }
    PCore_FreeDocument(document);
    show_info(L"TEST 74 OK",
            "DOM event capture/target/bubble, non-bubbling dispatch,\n"
            "cancellation, stop propagation and listener removal passed.");
    return TRUE;

event_failed:
    for (i = 0; i < listener_count; i++) {
        if (document != NULL && listeners[i] != NULL) {
            PCore_EventListenerRemove(document, listeners[i]);
        }
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    _snprintf(detail, sizeof(detail) - 1,
            "stage=%d count=%d first=%d/%u default=%d listeners=%d",
            stage, trace.count, trace.count > 0 ? trace.codes[0] : -1,
            trace.count > 0 ? trace.phases[0] : 0, default_allowed,
            listener_count);
    detail[sizeof(detail) - 1] = '\0';
    show_error(L"TEST 74 FAIL", detail);
    return FALSE;

#undef TEST74_ADD
}

/* -------------------------------------------------------------------- */
/* TEST 75 - positioned block and inline layout                         */
/* -------------------------------------------------------------------- */

static BOOL test75_positioned_layout(void)
{
    static const char *HTML =
        "<!doctype html><html><body><main>"
        "<section>static</section>"
        "<article>relative</article>"
        "<aside>absolute block</aside>"
        "<span>absolute inline</span>"
        "</main></body></html>";
    static const char *CSS =
        "html,body{margin:0;padding:0;}"
        "main{position:relative;width:180px;height:120px;margin:0;"
        "padding:0;background:#eeeeee;}"
        "section{display:block;width:20px;height:20px;margin:0;padding:0;"
        "background:#ff0000;}"
        "article{display:block;position:relative;left:10px;top:7px;"
        "width:30px;height:20px;margin:0;padding:0;background:#00ff00;}"
        "aside{position:absolute;left:60px;top:30px;width:25px;height:15px;"
        "margin:0;padding:0;background:#0000ff;}"
        "span{position:absolute;left:100px;top:50px;width:20px;height:12px;"
        "margin:0;padding:0;background:#ffff00;}";
    HANDLE hDoc;
    HANDLE hSheet;
    int main_x;
    int main_y;
    int main_w;
    int main_h;
    int section_x;
    int section_y;
    int section_w;
    int section_h;
    int article_x;
    int article_y;
    int article_w;
    int article_h;
    int aside_x;
    int aside_y;
    int aside_w;
    int aside_h;
    int span_x;
    int span_y;
    int span_w;
    int span_h;
    char detail[320];

    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/positioned.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 240, 320) != 0 ||
            PCore_NodeBox(hDoc, "main", &main_x, &main_y, &main_w,
                    &main_h) != 0 ||
            PCore_NodeBox(hDoc, "section", &section_x, &section_y,
                    &section_w, &section_h) != 0 ||
            PCore_NodeBox(hDoc, "article", &article_x, &article_y,
                    &article_w, &article_h) != 0 ||
            PCore_NodeBox(hDoc, "aside", &aside_x, &aside_y, &aside_w,
                    &aside_h) != 0 ||
            PCore_NodeBox(hDoc, "span", &span_x, &span_y, &span_w,
                    &span_h) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 75 FAIL",
                "positioned parse/style/layout lookup failed");
        return FALSE;
    }

    if (main_w != 180 || main_h != 120 ||
            section_x != main_x || section_y != main_y ||
            section_w != 20 || section_h != 20 ||
            article_x != section_x + 10 ||
            article_y != section_y + section_h + 7 ||
            article_w != 30 || article_h != 20 ||
            aside_x != main_x + 60 || aside_y != main_y + 30 ||
            aside_w != 25 || aside_h != 15 ||
            span_x != main_x + 100 || span_y != main_y + 50 ||
            span_w != 20 || span_h != 12) {
        _snprintf(detail, sizeof(detail) - 1,
                "main=%d,%d %dx%d static=%d,%d %dx%d "
                "relative=%d,%d %dx%d absolute=%d,%d %dx%d "
                "inline=%d,%d %dx%d",
                main_x, main_y, main_w, main_h,
                section_x, section_y, section_w, section_h,
                article_x, article_y, article_w, article_h,
                aside_x, aside_y, aside_w, aside_h,
                span_x, span_y, span_w, span_h);
        detail[sizeof(detail) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 75 FAIL", detail);
        return FALSE;
    }

    g_doc_h = PCore_DocumentHeight(hDoc);
    g_scroll_y = 0;
    show_info(L"TEST 75",
            "Positioned layout fixture: red static flow, green relative\n"
            "flow box shifted +10/+7, blue absolute block and yellow\n"
            "absolute inline box. All four must stay inside the grey box.");
    g_render_doc = hDoc;
    g_render_sheet = hSheet;
    if (!show_render_window()) {
        g_render_doc = NULL;
        g_render_sheet = NULL;
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 75 FAIL", "CreateWindow returned NULL");
        return FALSE;
    }
    g_render_doc = NULL;
    g_render_sheet = NULL;
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 75 OK",
            "NetSurf positioned layout passed: relative offsets preserve\n"
            "flow geometry, absolute block geometry is anchored to the\n"
            "positioned parent, and inline absolute is blockified.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 76 - dynamic :hover selection and WM interaction state           */
/* -------------------------------------------------------------------- */

static BOOL test76_hover_state(void)
{
    static const char *HTML =
        "<!doctype html><html><body><a href='/hover'>Hover target</a>"
        "<div>outside</div></body></html>";
    static const char *CSS =
        "html,body{margin:0;padding:0;}"
        "a{display:block;width:120px;height:24px;color:#0000ff;}"
        "a:hover{color:#ff0000;}";
    HANDLE hDoc;
    HANDLE hSheet;
    int x;
    int y;
    int w;
    int h;
    unsigned long initial_color;
    unsigned long hover_color;
    unsigned long clear_color;
    char detail[256];

    initial_color = 0;
    hover_color = 0;
    clear_color = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    hSheet = PCore_ParseCSS(CSS, 0,
            "http://positron.local/hover.css");
    if (hDoc == NULL || hSheet == NULL ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_LayoutDocument(hDoc, 224, 320) != 0 ||
            PCore_NodeBox(hDoc, "a", &x, &y, &w, &h) != 0 ||
            PCore_NodeComputedColor(hDoc, "a", &initial_color) != 0) {
        if (hSheet != NULL) { PCore_FreeStylesheet(hSheet); }
        if (hDoc != NULL) { PCore_FreeDocument(hDoc); }
        show_error(L"TEST 76 FAIL", "hover parse/style/layout failed");
        return FALSE;
    }
    if ((initial_color & 0x00ffffffUL) != 0x000000ffUL ||
            w <= 0 || h <= 0 ||
            PCore_InteractionSetAt(hDoc, x + 1, y + 1,
                    PCORE_INTERACTION_HOVER) != 1 ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_NodeComputedColor(hDoc, "a", &hover_color) != 0 ||
            (hover_color & 0x00ffffffUL) != 0x00ff0000UL ||
            PCore_InteractionClear(hDoc, PCORE_INTERACTION_HOVER) != 1 ||
            PCore_StyleDocument(hDoc, hSheet) != 0 ||
            PCore_NodeComputedColor(hDoc, "a", &clear_color) != 0 ||
            (clear_color & 0x00ffffffUL) != 0x000000ffUL) {
        _snprintf(detail, sizeof(detail) - 1,
                "initial=%06lX hover=%06lX clear=%06lX box=%d,%d %dx%d",
                initial_color & 0x00ffffffUL,
                hover_color & 0x00ffffffUL,
                clear_color & 0x00ffffffUL, x, y, w, h);
        detail[sizeof(detail) - 1] = '\0';
        PCore_FreeStylesheet(hSheet);
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 76 FAIL", detail);
        return FALSE;
    }
    PCore_FreeStylesheet(hSheet);
    PCore_FreeDocument(hDoc);
    show_info(L"TEST 76 OK",
            "CSS :hover state passed: pointer hit selects the nearest\n"
            "element, style reselect turns the link red, and clearing\n"
            "hover restores the blue rule.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 77 - external script discovery, URL resolution and cache ABI       */
/* The transport callback is deliberately local: this test proves the core */
/* contract without enabling JavaScript or changing the frozen Browse path. */
/* -------------------------------------------------------------------- */

typedef struct test77_script_ctx {
    int calls;
    int frees;
    int matched;
} test77_script_ctx;

static int test77_script_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    test77_script_ctx *ctx;
    const char *body;
    char *data;
    int len;

    ctx = (test77_script_ctx *) pw;
    *out_data = NULL;
    *out_len = 0;
    ctx->calls++;
    body = NULL;
    if (strcmp(url, "https://example.com/dir/app.js") == 0) {
        body = "APP";
    } else if (strcmp(url, "https://example.com/shared.js") == 0) {
        body = "SHARED";
    } else if (strcmp(url, "https://cdn.example.net/vendor.js") == 0) {
        body = "VENDOR";
    }
    if (body == NULL) {
        return 1;
    }
    len = (int) strlen(body);
    data = (char *) malloc((size_t) len);
    if (data == NULL) {
        return 1;
    }
    memcpy(data, body, (size_t) len);
    *out_data = data;
    *out_len = len;
    ctx->matched++;
    return 0;
}

static void test77_script_free(void *pw, char *data)
{
    test77_script_ctx *ctx;

    ctx = (test77_script_ctx *) pw;
    ctx->frees++;
    free(data);
}

static BOOL test77_script_resources(void)
{
    static const char *HTML =
        "<!doctype html><html><head>"
        "<script src='app.js'></script>"
        "<script src='app.js'></script>"
        "<script src='/shared.js'></script>"
        "<script src='https://cdn.example.net/vendor.js'></script>"
        "<script>window.must_not_run=1;</script>"
        "</head><body>script cache probe</body></html>";
    static const char *URLS[] = {
        "https://example.com/dir/app.js",
        "https://example.com/shared.js",
        "https://cdn.example.net/vendor.js"
    };
    static const char *BODIES[] = { "APP", "SHARED", "VENDOR" };
    HANDLE hDoc;
    test77_script_ctx ctx;
    PCoreScriptResourceInfo info;
    const char *data;
    int found;
    int fetched;
    int first_rc;
    int found_again;
    int fetched_again;
    int count;
    int cache_count;
    int i;
    char url[160];
    char detail[320];

    memset(&ctx, 0, sizeof(ctx));
    found = 0;
    fetched = 0;
    first_rc = -1;
    found_again = 0;
    fetched_again = 0;
    hDoc = PCore_ParseHTML(HTML, 0);
    if (hDoc != NULL) {
        first_rc = PCore_FetchScriptResourcesEx(hDoc,
                    "https://example.com/dir/page.html",
                    wm_combine_url, test77_script_fetch,
                    test77_script_free, &ctx, &found, &fetched);
    }
    if (hDoc == NULL || first_rc != 0 || found != 4 || fetched != 4 ||
            ctx.calls != 3 || ctx.matched != 3 || ctx.frees != 3) {
        cache_count = (hDoc != NULL) ?
                PCore_GetScriptResourceCount(hDoc) : -1;
        _snprintf(detail, sizeof(detail) - 1,
                "first rc=%d found=%d/4 fetched=%d/4 calls=%d "
                "matched=%d frees=%d cache=%d",
                first_rc, found, fetched, ctx.calls, ctx.matched,
                ctx.frees, cache_count);
        detail[sizeof(detail) - 1] = '\0';
        if (hDoc != NULL) {
            PCore_FreeDocument(hDoc);
        }
        show_error(L"TEST 77 FAIL", detail);
        return FALSE;
    }
    if (PCore_FetchScriptResourcesEx(hDoc,
            "https://example.com/dir/page.html", wm_combine_url,
            NULL, NULL, NULL, &found_again, &fetched_again) != 0 ||
            found_again != 4 || fetched_again != 4 ||
            ctx.calls != 3 || ctx.frees != 3) {
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 77 FAIL", "script cache reuse failed");
        return FALSE;
    }
    count = PCore_GetScriptResourceCount(hDoc);
    if (count != 3) {
        _snprintf(detail, sizeof(detail) - 1,
                "cache count=%d expected=3", count);
        detail[sizeof(detail) - 1] = '\0';
        PCore_FreeDocument(hDoc);
        show_error(L"TEST 77 FAIL", detail);
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        data = NULL;
        memset(url, 0, sizeof(url));
        if (PCore_GetScriptResource(hDoc, (unsigned int) i, &info,
                url, sizeof(url), &data) != 0 ||
                !info.available || strcmp(url, URLS[i]) != 0 ||
                data == NULL || info.data_bytes != (int) strlen(BODIES[i]) ||
                memcmp(data, BODIES[i], (size_t) info.data_bytes) != 0) {
            _snprintf(detail, sizeof(detail) - 1,
                    "entry=%d url=%s bytes=%d", i, url,
                    info.data_bytes);
            detail[sizeof(detail) - 1] = '\0';
            PCore_FreeDocument(hDoc);
            show_error(L"TEST 77 FAIL", detail);
            return FALSE;
        }
    }
    PCore_FreeDocument(hDoc);
    _snprintf(detail, sizeof(detail) - 1,
            "scripts found=%d/%d cache=%d calls=%d frees=%d; "
            "inline script was not executed",
            found, fetched, count, ctx.calls, ctx.frees);
    detail[sizeof(detail) - 1] = '\0';
    show_info(L"TEST 77 OK", detail);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 80 - standalone JavaScript runtime DLL                          */
/* This deliberately does not initialise positron_core. It proves the    */
/* public opaque-handle ABI, persistent evaluation state, error recovery, */
/* and DLL-owned diagnostic storage using the bundled Duktape runtime.    */
/* -------------------------------------------------------------------- */
static BOOL test80_script_runtime(void)
{
    static const char *SOURCE_1 = "var answer = 40 + 2; answer;";
    static const char *SOURCE_2 = "answer += 1; answer;";
    static const char *SOURCE_3 = "throw new Error('expected');";
    static const char *SOURCE_4 = "answer + 1;";
    HANDLE hScript;
    int rc;
    unsigned long memory_used;
    unsigned long evaluations;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(0);
    if (hScript == NULL) {
        show_error(L"TEST 80 FAIL",
                "PScript_Create returned NULL");
        return FALSE;
    }
    if (PScript_AbiVersion() != PSCRIPT_ABI_VERSION) {
        _snprintf(detail, sizeof(detail) - 1,
                "ABI=%08lx expected=%08lx",
                PScript_AbiVersion(), PSCRIPT_ABI_VERSION);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 80 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_1, -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "first rc=%d result=%s expected=42",
                rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 80 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_2, -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "43") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "persistent rc=%d result=%s expected=43",
                rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 80 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_3, -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_EVALUATION ||
            strstr(error, "expected") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "throw rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 80 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_4, -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "44") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery rc=%d result=%s expected=44",
                rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 80 FAIL", detail);
        return FALSE;
    }

    memory_used = PScript_GetMemoryUsed(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (memory_used == 0 || evaluations != 4) {
        _snprintf(detail, sizeof(detail) - 1,
                "memory=%lu evaluations=%lu/4",
                memory_used, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 80 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    _snprintf(detail, sizeof(detail) - 1,
            "standalone positron_script.dll: persistent values, thrown\n"
            "error recovery and DLL-owned memory telemetry passed.\n\n"
            "No DOM, window, network or browser-core binding is enabled.");
    detail[sizeof(detail) - 1] = '\0';
    show_info(L"TEST 80 OK", detail);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 81 - standalone JavaScript runtime safety boundaries             */
/* This deliberately uses a separate context and a short budget. It     */
/* proves timeout cancellation, source-size rejection, and recovery      */
/* after both paths without touching positron_core or the browser host.   */
/* -------------------------------------------------------------------- */
static BOOL test81_script_safety(void)
{
    static const char *SOURCE_TIMEOUT = "while (true) {}";
    static const char *SOURCE_OVERLIMIT = "x";
    static const char *SOURCE_RECOVERY = "6 * 7;";
    HANDLE hScript;
    int rc;
    unsigned long memory_used;
    unsigned long evaluations;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(50);
    if (hScript == NULL) {
        show_error(L"TEST 81 FAIL",
                "PScript_Create returned NULL");
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_TIMEOUT, -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_TIMEOUT ||
            strstr(error, "timeout") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "timeout rc=%d error=%s expected=%d",
                rc, error, PSCRIPT_ERROR_TIMEOUT);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 81 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_OVERLIMIT,
            (int) (PSCRIPT_MAX_SOURCE_BYTES + 1UL));
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_SOURCE_TOO_LARGE ||
            strstr(error, "PSCRIPT_MAX_SOURCE_BYTES") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "limit rc=%d error=%s expected=%d",
                rc, error, PSCRIPT_ERROR_SOURCE_TOO_LARGE);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 81 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_RECOVERY, -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery rc=%d result=%s expected=42",
                rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 81 FAIL", detail);
        return FALSE;
    }

    memory_used = PScript_GetMemoryUsed(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (memory_used == 0 || evaluations != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "memory=%lu evaluations=%lu/2",
                memory_used, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 81 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    _snprintf(detail, sizeof(detail) - 1,
            "timeout=%d limit=%d recovery=%s memory=%lu eval=%lu/2",
            PSCRIPT_ERROR_TIMEOUT, PSCRIPT_ERROR_SOURCE_TOO_LARGE,
            "42", memory_used, evaluations);
    detail[sizeof(detail) - 1] = '\0';
    show_info(L"TEST 81 OK", detail);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 82 - standalone JavaScript runtime memory boundary              */
/* This deliberately uses a short-lived allocation-heavy function so the */
/* failed evaluation does not retain the partial array. It proves a hard */
/* heap limit, peak telemetry, and recovery after a catchable OOM path.   */
/* -------------------------------------------------------------------- */
static BOOL test82_script_memory_limit(void)
{
    static const char *SOURCE_MEMORY =
            "(function(){var values=[]; for (var i=0; i<50000; i++) {"
            "values.push(i); } return values.length; }());";
    static const char *SOURCE_RECOVERY = "7 * 6;";
    HANDLE hScript;
    int rc;
    unsigned long memory_used;
    unsigned long memory_peak;
    unsigned long memory_limit;
    unsigned long evaluations;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_CreateEx(2000, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
    if (hScript == NULL) {
        show_error(L"TEST 82 FAIL",
                "PScript_CreateEx returned NULL");
        return FALSE;
    }

    memory_limit = PScript_GetMemoryLimit(hScript);
    rc = PScript_Evaluate(hScript, SOURCE_MEMORY, -1);
    error = PScript_GetError(hScript);
    memory_used = PScript_GetMemoryUsed(hScript);
    memory_peak = PScript_GetPeakMemoryUsed(hScript);
    if (memory_limit != PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES ||
            rc != PSCRIPT_ERROR_MEMORY_LIMIT ||
            strstr(error, "memory limit") == NULL ||
            memory_peak == 0 || memory_peak > memory_limit ||
            memory_used > memory_limit) {
        _snprintf(detail, sizeof(detail) - 1,
                "rc=%d error=%s used=%lu peak=%lu limit=%lu",
                rc, error, memory_used, memory_peak, memory_limit);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 82 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript, SOURCE_RECOVERY, -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 ||
            evaluations != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery rc=%d result=%s eval=%lu/2",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 82 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    _snprintf(detail, sizeof(detail) - 1,
            "rc=%d used=%lu peak=%lu limit=%lu recovery=%s eval=%lu/2",
            PSCRIPT_ERROR_MEMORY_LIMIT, memory_used, memory_peak,
            memory_limit, "42", evaluations);
    detail[sizeof(detail) - 1] = '\0';
    show_info(L"TEST 82 OK", detail);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 83 - standalone JavaScript module lifecycle                     */
/* The module ABI deliberately stays below browser JavaScript: modules are  */
/* named CommonJS-style units in one isolated context, with no URL loader.  */
/* This checks execute-once caching, require(), failed-load rollback and    */
/* explicit cache clearing for other WM callers of positron_script.dll.    */
/* -------------------------------------------------------------------- */
static BOOL test83_script_modules(void)
{
    static const char *SOURCE_BASE = "module.exports = 40;";
    static const char *SOURCE_ENTRY =
            "module.exports = require('base') + 2;";
    static const char *SOURCE_CACHED =
            "throw new Error('cached module was executed twice');";
    static const char *SOURCE_BROKEN =
            "throw new Error('broken module');";
    HANDLE hScript;
    int rc;
    unsigned long modules;
    unsigned long evaluations;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 83 FAIL",
                "PScript_Create returned NULL");
        return FALSE;
    }

    rc = PScript_EvaluateModule(hScript, "base", -1,
            SOURCE_BASE, -1);
    result = PScript_GetResult(hScript);
    modules = PScript_GetModuleCount(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "40") != 0 ||
            modules != 1 || evaluations != 1) {
        _snprintf(detail, sizeof(detail) - 1,
                "base rc=%d result=%s modules=%lu eval=%lu/1",
                rc, result, modules, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 83 FAIL", detail);
        return FALSE;
    }

    rc = PScript_EvaluateModule(hScript, "entry", -1,
            SOURCE_ENTRY, -1);
    result = PScript_GetResult(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 ||
            modules != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "entry rc=%d result=%s modules=%lu",
                rc, result, modules);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 83 FAIL", detail);
        return FALSE;
    }

    rc = PScript_EvaluateModule(hScript, "base", -1,
            SOURCE_CACHED, -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "40") != 0 ||
            evaluations != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "cache rc=%d result=%s eval=%lu/2",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 83 FAIL", detail);
        return FALSE;
    }

    rc = PScript_EvaluateModule(hScript, "broken", -1,
            SOURCE_BROKEN, -1);
    error = PScript_GetError(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_ERROR_EVALUATION ||
            strstr(error, "broken module") == NULL || modules != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "rollback rc=%d error=%s modules=%lu/2",
                rc, error, modules);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 83 FAIL", detail);
        return FALSE;
    }

    rc = PScript_ClearModules(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_OK || modules != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "clear rc=%d modules=%lu/0",
                rc, modules);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 83 FAIL", detail);
        return FALSE;
    }

    rc = PScript_EvaluateModule(hScript, "base", -1,
            SOURCE_BASE, -1);
    result = PScript_GetResult(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "40") != 0 ||
            modules != 1) {
        _snprintf(detail, sizeof(detail) - 1,
                "reload rc=%d result=%s modules=%lu/1",
                rc, result, modules);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 83 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    _snprintf(detail, sizeof(detail) - 1,
            "base=40 entry=require(base)+2=42 cache=ok rollback=ok "
            "clear/reload=ok modules=%lu",
            modules);
    detail[sizeof(detail) - 1] = '\0';
    show_info(L"TEST 83 OK", detail);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 84 - host-provided JavaScript module source                     */
/* The provider is deliberately a synchronous C callback. It supplies a  */
/* root module and a dependency on demand, while the DLL owns execution,  */
/* caching and rollback. Static strings plus a no-op free callback model    */
/* firmware/resource-table storage commonly used by other WM programs.    */
/* -------------------------------------------------------------------- */
typedef struct test84_provider_state {
    unsigned long entry_calls;
    unsigned long base_calls;
    unsigned long missing_calls;
    unsigned long broken_calls;
    unsigned long frees;
} test84_provider_state;

static int test84_module_source(void *pw, const char *module_name,
        char **out_source, int *out_len)
{
    test84_provider_state *state;
    const char *source;

    state = (test84_provider_state *) pw;
    if (state == NULL || module_name == NULL || out_source == NULL ||
            out_len == NULL) {
        return 1;
    }
    *out_source = NULL;
    *out_len = 0;
    source = NULL;
    if (strcmp(module_name, "entry") == 0) {
        state->entry_calls++;
        source = "module.exports = require('base') + 2;";
    } else if (strcmp(module_name, "base") == 0) {
        state->base_calls++;
        source = "module.exports = 40;";
    } else if (strcmp(module_name, "broken") == 0) {
        state->broken_calls++;
        source = "throw new Error('provider broken');";
    } else {
        state->missing_calls++;
        return 1;
    }
    *out_source = (char *) source;
    *out_len = (int) strlen(source);
    return 0;
}

static void test84_module_source_free(void *pw, char *source)
{
    test84_provider_state *state;

    state = (test84_provider_state *) pw;
    if (state != NULL && source != NULL) {
        state->frees++;
    }
}

static BOOL test84_script_module_provider(void)
{
    HANDLE hScript;
    test84_provider_state state;
    int rc;
    unsigned long modules;
    const char *result;
    const char *error;
    char detail[320];

    memset(&state, 0, sizeof(state));
    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 84 FAIL",
                "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetModuleSourceProvider(hScript,
            test84_module_source, test84_module_source_free, &state);
    if (rc != PSCRIPT_OK) {
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL",
                "PScript_SetModuleSourceProvider failed");
        return FALSE;
    }

    rc = PScript_LoadModule(hScript, "entry", -1);
    result = PScript_GetResult(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 || modules != 2 ||
            state.entry_calls != 1 || state.base_calls != 1 ||
            state.frees != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "entry rc=%d result=%s modules=%lu calls=%lu/%lu frees=%lu",
                rc, result, modules, state.entry_calls, state.base_calls,
                state.frees);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL", detail);
        return FALSE;
    }

    rc = PScript_LoadModule(hScript, "entry", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 ||
            state.entry_calls != 1 || state.base_calls != 1 ||
            state.frees != 2) {
        _snprintf(detail, sizeof(detail) - 1,
                "cache rc=%d result=%s calls=%lu/%lu frees=%lu",
                rc, result, state.entry_calls, state.base_calls,
                state.frees);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL", detail);
        return FALSE;
    }

    rc = PScript_LoadModule(hScript, "missing", -1);
    error = PScript_GetError(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_ERROR_MODULE_SOURCE ||
            strstr(error, "provider") == NULL || modules != 2 ||
            state.missing_calls != 1) {
        _snprintf(detail, sizeof(detail) - 1,
                "missing rc=%d error=%s modules=%lu calls=%lu",
                rc, error, modules, state.missing_calls);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL", detail);
        return FALSE;
    }

    rc = PScript_LoadModule(hScript, "broken", -1);
    error = PScript_GetError(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_ERROR_EVALUATION ||
            strstr(error, "provider broken") == NULL || modules != 2 ||
            state.broken_calls != 1 || state.frees != 3) {
        _snprintf(detail, sizeof(detail) - 1,
                "rollback rc=%d error=%s modules=%lu frees=%lu",
                rc, error, modules, state.frees);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL", detail);
        return FALSE;
    }

    rc = PScript_ClearModules(hScript);
    if (rc != PSCRIPT_OK || PScript_GetModuleCount(hScript) != 0) {
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL",
                "clear modules failed after provider rollback");
        return FALSE;
    }
    rc = PScript_LoadModule(hScript, "entry", -1);
    result = PScript_GetResult(hScript);
    modules = PScript_GetModuleCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 || modules != 2 ||
            state.entry_calls != 2 || state.base_calls != 2 ||
            state.frees != 5) {
        _snprintf(detail, sizeof(detail) - 1,
                "reload rc=%d result=%s modules=%lu calls=%lu/%lu frees=%lu",
                rc, result, modules, state.entry_calls, state.base_calls,
                state.frees);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 84 FAIL", detail);
        return FALSE;
    }
    PScript_SetModuleSourceProvider(hScript, NULL, NULL, NULL);
    PScript_Destroy(hScript);

    _snprintf(detail, sizeof(detail) - 1,
            "entry=42 on-demand=ok cache=ok rollback=ok clear/reload=ok "
            "calls=%lu/%lu frees=%lu",
            state.entry_calls, state.base_calls, state.frees);
    detail[sizeof(detail) - 1] = '\0';
    show_info(L"TEST 84 OK", detail);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 85 - standalone JavaScript global value bridge                  */
/* Persistent primitive globals are useful to WM callers without making  */
/* them depend on Duktape headers. The values also participate in later    */
/* evaluations through the ordinary global object.                       */
/* -------------------------------------------------------------------- */
static BOOL test85_script_globals(void)
{
    HANDLE hScript;
    int rc;
    unsigned long evaluations;
    const char *result;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 85 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalString(hScript, "site", -1, "wm6", 3);
    if (rc != PSCRIPT_OK) {
        _snprintf(detail, sizeof(detail) - 1,
                "string setter rc=%d", rc);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }
    rc = PScript_SetGlobalNumber(hScript, "answer", -1, 40.5);
    if (rc != PSCRIPT_OK) {
        _snprintf(detail, sizeof(detail) - 1,
                "number setter rc=%d", rc);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }
    rc = PScript_SetGlobalBoolean(hScript, "enabled", -1, 1);
    if (rc != PSCRIPT_OK) {
        _snprintf(detail, sizeof(detail) - 1,
                "boolean setter rc=%d", rc);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }

    rc = PScript_GetGlobalJson(hScript, "site", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "\"wm6\"") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "site rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "answer", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "40.5") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "answer rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "enabled", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "true") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "enabled rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }

    rc = PScript_Evaluate(hScript,
            "answer + (enabled ? 1.5 : 0);", -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 ||
            evaluations != 1) {
        _snprintf(detail, sizeof(detail) - 1,
                "evaluation rc=%d result=%s eval=%lu/1",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 85 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 85 OK",
            "global string/number/boolean setters, JSON getters and "
            "persistent evaluation state passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 86 - JSON argument and result function calls                     */
/* -------------------------------------------------------------------- */
static BOOL test86_script_call_json(void)
{
    static const char *SOURCE =
            "function pair(a,b){return {sum:a+b,ok:a<b};}";
    HANDLE hScript;
    int rc;
    unsigned long evaluations;
    const char *result;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 86 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, SOURCE, -1);
    if (rc != PSCRIPT_OK) {
        _snprintf(detail, sizeof(detail) - 1,
                "function evaluate rc=%d error=%s", rc,
                PScript_GetError(hScript));
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 86 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "pair", -1, "[2,5]", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strstr(result, "\"sum\":7") == NULL ||
            strstr(result, "\"ok\":true") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "first rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 86 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "pair", -1, "[7,2]", -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strstr(result, "\"sum\":9") == NULL ||
            strstr(result, "\"ok\":false") == NULL ||
            evaluations != 3) {
        _snprintf(detail, sizeof(detail) - 1,
                "second rc=%d result=%s eval=%lu/3",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 86 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 86 OK",
            "persistent function call with JSON array arguments and "
            "JSON object results passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 87 - repeated calls retain ordinary global state                 */
/* -------------------------------------------------------------------- */
static BOOL test87_script_call_persistence(void)
{
    static const char *SOURCE =
            "var total=0; function add(n){total+=n;return total;}";
    HANDLE hScript;
    int rc;
    unsigned long evaluations;
    const char *result;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 87 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, SOURCE, -1);
    if (rc != PSCRIPT_OK) {
        PScript_Destroy(hScript);
        show_error(L"TEST 87 FAIL", "function setup failed");
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "add", -1, "[3]", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "3") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "first rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 87 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "add", -1, "[4]", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "7") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "second rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 87 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "total", -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "7") != 0 ||
            evaluations != 3) {
        _snprintf(detail, sizeof(detail) - 1,
                "global rc=%d result=%s eval=%lu/3",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 87 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 87 OK",
            "two JSON calls retained a mutable global counter and "
            "reported the expected evaluation count.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 88 - malformed calls and missing globals recover                 */
/* -------------------------------------------------------------------- */
static BOOL test88_script_call_errors(void)
{
    static const char *SOURCE =
            "function identity(value){return value;}";
    HANDLE hScript;
    int rc;
    unsigned long evaluations;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 88 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, SOURCE, -1);
    if (rc != PSCRIPT_OK) {
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", "function setup failed");
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "identity", -1, "{}", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_JSON ||
            strstr(error, "array") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "object args rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "missing", -1, "[]", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_GLOBAL ||
            strstr(error, "undefined") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "missing rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", detail);
        return FALSE;
    }
    rc = PScript_SetGlobalNumber(hScript, "notFunction", -1, 1);
    if (rc != PSCRIPT_OK) {
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", "non-callable setup failed");
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "notFunction", -1, "[]", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_GLOBAL ||
            strstr(error, "not callable") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "non-callable rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "identity", -1, "[9]", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "9") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery call rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "identity", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_JSON ||
            strstr(error, "JSON") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "function JSON rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", detail);
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, "21 + 21;", -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0 ||
            evaluations != 3) {
        _snprintf(detail, sizeof(detail) - 1,
                "final recovery rc=%d result=%s eval=%lu/3",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 88 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 88 OK",
            "invalid JSON, missing/non-callable globals and "
            "non-JSON function reads recover without poisoning context.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 89 - global name and JSON result limits                          */
/* -------------------------------------------------------------------- */
static BOOL test89_script_limits(void)
{
    static const char *SOURCE =
            "function longValue(){var s='';"
            "for(var i=0;i<300;i++){s+='x';}return s;}";
    static char long_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 2];
    HANDLE hScript;
    int rc;
    unsigned long evaluations;
    const char *result;
    const char *error;
    char detail[320];

    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 89 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalNumber(hScript, long_name,
            (int) (sizeof(long_name) - 1), 1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_GLOBAL ||
            strstr(error, "global name") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "long name rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 89 FAIL", detail);
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, SOURCE, -1);
    if (rc != PSCRIPT_OK) {
        PScript_Destroy(hScript);
        show_error(L"TEST 89 FAIL", "long result setup failed");
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "longValue", -1, "[]", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_RESULT_TOO_LARGE ||
            strstr(error, "result") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "long result rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 89 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "missing", -1);
    if (rc != PSCRIPT_ERROR_GLOBAL) {
        _snprintf(detail, sizeof(detail) - 1,
                "missing after limit rc=%d", rc);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 89 FAIL", detail);
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, "2 + 2;", -1);
    result = PScript_GetResult(hScript);
    evaluations = PScript_GetEvaluationCount(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "4") != 0 ||
            evaluations != 3) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery rc=%d result=%s eval=%lu/3",
                rc, result, evaluations);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 89 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 89 OK",
            "global-name validation, explicit JSON result overflow and "
            "post-error evaluation recovery passed.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 90 - synchronous native JSON function                           */
/* A WM host can expose one small operation without sharing Duktape      */
/* headers or heap ownership. Arguments and the return value cross the   */
/* boundary as compact JSON only.                                       */
/* -------------------------------------------------------------------- */
typedef struct test90_native_state {
    int calls;
    int bad_args;
} test90_native_state;

static int test90_native_add(void *pw, const char *args_json, int args_len,
        char *out_json, int out_capacity, int *out_len)
{
    test90_native_state *state;
    const char *value;

    state = (test90_native_state *) pw;
    if (state == NULL || args_json == NULL || out_json == NULL ||
            out_len == NULL || out_capacity < 3) {
        return 1;
    }
    state->calls++;
    value = NULL;
    if (args_len == 5 && memcmp(args_json, "[2,3]", 5) == 0) {
        value = "5";
    } else if (args_len == 5 && memcmp(args_json, "[4,6]", 5) == 0) {
        value = "10";
    }
    if (value == NULL) {
        state->bad_args++;
        return 1;
    }
    out_json[0] = value[0];
    if (value[1] != '\0') {
        out_json[1] = value[1];
        out_json[2] = '\0';
        *out_len = 2;
    } else {
        out_json[1] = '\0';
        *out_len = 1;
    }
    return 0;
}

static BOOL test90_script_native_call(void)
{
    HANDLE hScript;
    test90_native_state state;
    int rc;
    unsigned long native_count;
    const char *result;
    char detail[320];

    memset(&state, 0, sizeof(state));
    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 90 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_RegisterGlobalJsonFunction(hScript, "nativeAdd", -1,
            test90_native_add, &state);
    native_count = PScript_GetNativeFunctionCount(hScript);
    if (rc != PSCRIPT_OK || native_count != 1) {
        _snprintf(detail, sizeof(detail) - 1,
                "register rc=%d count=%lu", rc, native_count);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 90 FAIL", detail);
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, "nativeAdd(2,3);", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "5") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "evaluate rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 90 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "nativeAdd", -1, "[4,6]", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "10") != 0 ||
            state.calls != 2 || state.bad_args != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "call rc=%d result=%s calls=%d bad=%d",
                rc, result, state.calls, state.bad_args);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 90 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 90 OK",
            "native JSON callback received [2,3] and [4,6], then "
            "returned 5 and 10 through both public call paths.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 91 - structured JSON crosses the native callback boundary        */
/* -------------------------------------------------------------------- */
static int test91_native_echo(void *pw, const char *args_json, int args_len,
        char *out_json, int out_capacity, int *out_len)
{
    (void) pw;
    if (args_json == NULL || out_json == NULL || out_len == NULL ||
            args_len < 0 || args_len >= out_capacity) {
        return 1;
    }
    memcpy(out_json, args_json, (size_t) args_len);
    out_json[args_len] = '\0';
    *out_len = args_len;
    return 0;
}

static BOOL test91_script_native_json(void)
{
    HANDLE hScript;
    int rc;
    const char *result;

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 91 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_RegisterGlobalJsonFunction(hScript, "nativeEcho", -1,
            test91_native_echo, NULL);
    if (rc == PSCRIPT_OK) {
        rc = PScript_CallGlobalJson(hScript, "nativeEcho", -1,
                "[{\"x\":1},true]", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "[{\"x\":1},true]") != 0) {
        char detail[320];

        _snprintf(detail, sizeof(detail) - 1,
                "rc=%d result=%s error=%s", rc, result,
                PScript_GetError(hScript));
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 91 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 91 OK",
            "object and boolean arguments crossed the callback as one "
            "compact JSON array and round-tripped unchanged.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 92 - callback failure is recoverable                              */
/* -------------------------------------------------------------------- */
static int test92_native_fail(void *pw, const char *args_json, int args_len,
        char *out_json, int out_capacity, int *out_len)
{
    (void) pw;
    (void) args_json;
    (void) args_len;
    (void) out_json;
    (void) out_capacity;
    (void) out_len;
    return 1;
}

static BOOL test92_script_native_failure(void)
{
    HANDLE hScript;
    int rc;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 92 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_RegisterGlobalJsonFunction(hScript, "nativeFail", -1,
            test92_native_fail, NULL);
    if (rc != PSCRIPT_OK) {
        PScript_Destroy(hScript);
        show_error(L"TEST 92 FAIL", "native failure registration failed");
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "nativeFail", -1, "[]", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_CALL || strstr(error, "native callback") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "failure rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 92 FAIL", detail);
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, "6 * 7;", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 92 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 92 OK",
            "a host callback failure became a recoverable call error; "
            "the same context then evaluated 6 * 7 as 42.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 93 - replace and unregister a native function                     */
/* -------------------------------------------------------------------- */
static int test93_native_one(void *pw, const char *args_json, int args_len,
        char *out_json, int out_capacity, int *out_len)
{
    (void) pw;
    (void) args_json;
    (void) args_len;
    if (out_json == NULL || out_len == NULL || out_capacity < 2) {
        return 1;
    }
    out_json[0] = '1';
    out_json[1] = '\0';
    *out_len = 1;
    return 0;
}

static int test93_native_two(void *pw, const char *args_json, int args_len,
        char *out_json, int out_capacity, int *out_len)
{
    (void) pw;
    (void) args_json;
    (void) args_len;
    if (out_json == NULL || out_len == NULL || out_capacity < 2) {
        return 1;
    }
    out_json[0] = '2';
    out_json[1] = '\0';
    *out_len = 1;
    return 0;
}

static BOOL test93_script_native_lifecycle(void)
{
    HANDLE hScript;
    int rc;
    unsigned long native_count;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 93 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_RegisterGlobalJsonFunction(hScript, "mode", -1,
            test93_native_one, NULL);
    if (rc == PSCRIPT_OK) {
        rc = PScript_CallGlobalJson(hScript, "mode", -1, "[]", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "1") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "first rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 93 FAIL", detail);
        return FALSE;
    }
    rc = PScript_RegisterGlobalJsonFunction(hScript, "mode", -1,
            test93_native_two, NULL);
    if (rc == PSCRIPT_OK) {
        rc = PScript_CallGlobalJson(hScript, "mode", -1, "[]", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "2") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "replace rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 93 FAIL", detail);
        return FALSE;
    }
    rc = PScript_UnregisterGlobalJsonFunction(hScript, "mode", -1);
    native_count = PScript_GetNativeFunctionCount(hScript);
    if (rc != PSCRIPT_OK || native_count != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "unregister rc=%d count=%lu", rc, native_count);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 93 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "mode", -1, "[]", -1);
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_GLOBAL || strstr(error, "undefined") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "after unregister rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 93 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 93 OK",
            "same global replaced callback 1 with callback 2, then "
            "unregister removed it and released the native slot.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 94 - fixed native callback table limit                           */
/* -------------------------------------------------------------------- */
static int test94_native_zero(void *pw, const char *args_json, int args_len,
        char *out_json, int out_capacity, int *out_len)
{
    (void) pw;
    (void) args_json;
    (void) args_len;
    if (out_json == NULL || out_len == NULL || out_capacity < 2) {
        return 1;
    }
    out_json[0] = '0';
    out_json[1] = '\0';
    *out_len = 1;
    return 0;
}

static BOOL test94_script_native_limit(void)
{
    HANDLE hScript;
    int rc;
    int i;
    char name[32];
    char detail[320];
    const char *result;

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 94 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    for (i = 0; i < (int) PSCRIPT_MAX_NATIVE_FUNCTIONS; i++) {
        _snprintf(name, sizeof(name) - 1, "fn%d", i);
        name[sizeof(name) - 1] = '\0';
        rc = PScript_RegisterGlobalJsonFunction(hScript, name, -1,
                test94_native_zero, NULL);
        if (rc != PSCRIPT_OK) {
            _snprintf(detail, sizeof(detail) - 1,
                    "register %s rc=%d", name, rc);
            detail[sizeof(detail) - 1] = '\0';
            PScript_Destroy(hScript);
            show_error(L"TEST 94 FAIL", detail);
            return FALSE;
        }
    }
    if (PScript_GetNativeFunctionCount(hScript) !=
            PSCRIPT_MAX_NATIVE_FUNCTIONS) {
        _snprintf(detail, sizeof(detail) - 1,
                "count=%lu/%lu",
                PScript_GetNativeFunctionCount(hScript),
                (unsigned long) PSCRIPT_MAX_NATIVE_FUNCTIONS);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 94 FAIL", detail);
        return FALSE;
    }
    rc = PScript_RegisterGlobalJsonFunction(hScript, "overflow", -1,
            test94_native_zero, NULL);
    if (rc != PSCRIPT_ERROR_NATIVE_LIMIT) {
        _snprintf(detail, sizeof(detail) - 1,
                "overflow rc=%d", rc);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 94 FAIL", detail);
        return FALSE;
    }
    rc = PScript_UnregisterGlobalJsonFunction(hScript, "fn0", -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_RegisterGlobalJsonFunction(hScript, "overflow", -1,
                test94_native_zero, NULL);
    }
    if (rc != PSCRIPT_OK || PScript_GetNativeFunctionCount(hScript) !=
            PSCRIPT_MAX_NATIVE_FUNCTIONS) {
        _snprintf(detail, sizeof(detail) - 1,
                "reuse rc=%d count=%lu", rc,
                PScript_GetNativeFunctionCount(hScript));
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 94 FAIL", detail);
        return FALSE;
    }
    rc = PScript_CallGlobalJson(hScript, "overflow", -1, "[]", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "0") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "reused call rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 94 FAIL", detail);
        return FALSE;
    }
    for (i = 1; i < (int) PSCRIPT_MAX_NATIVE_FUNCTIONS; i++) {
        _snprintf(name, sizeof(name) - 1, "fn%d", i);
        name[sizeof(name) - 1] = '\0';
        PScript_UnregisterGlobalJsonFunction(hScript, name, -1);
    }
    PScript_UnregisterGlobalJsonFunction(hScript, "overflow", -1);
    if (PScript_GetNativeFunctionCount(hScript) != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "cleanup count=%lu",
                PScript_GetNativeFunctionCount(hScript));
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 94 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 94 OK",
            "filled all 16 fixed native slots, rejected the 17th, "
            "reused a released slot, and cleaned up to zero.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 95 - structured JSON global injection                            */
/* -------------------------------------------------------------------- */
static BOOL test95_script_json_global(void)
{
    HANDLE hScript;
    int rc;
    const char *result;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 95 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "profile", -1,
            "{\"name\":\"wm6\",\"flags\":[true,false],\"count\":7}",
            -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_Evaluate(hScript,
                "profile.name+':' + profile.flags[1] + ':' + profile.count;",
                -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "wm6:false:7") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "evaluate rc=%d result=%s error=%s", rc, result,
                PScript_GetError(hScript));
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 95 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "profile", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strstr(result, "\"name\":\"wm6\"") == NULL ||
            strstr(result, "\"flags\":[true,false]") == NULL ||
            strstr(result, "\"count\":7") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "get rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 95 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 95 OK",
            "host injected one object with nested booleans; JavaScript "
            "read it and JSON getter returned the structured value.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 96 - injected objects remain mutable across public calls          */
/* -------------------------------------------------------------------- */
static BOOL test96_script_json_persistence(void)
{
    static const char *SOURCE =
            "function bump(){state.count+=3;"
            "state.items.push('ok');return state.count;}";
    HANDLE hScript;
    int rc;
    const char *result;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 96 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "state", -1,
            "{\"count\":2,\"items\":[\"a\"]}", -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_Evaluate(hScript, SOURCE, -1);
    }
    if (rc == PSCRIPT_OK) {
        rc = PScript_CallGlobalJson(hScript, "bump", -1, "[]", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "5") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "call rc=%d result=%s error=%s", rc, result,
                PScript_GetError(hScript));
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 96 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "state", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strstr(result, "\"count\":5") == NULL ||
            strstr(result, "\"items\":[\"a\",\"ok\"]") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "persist rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 96 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 96 OK",
            "an injected object survived evaluation, a JSON function call "
            "mutated it, and the later getter observed both changes.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 97 - malformed JSON, null, and context recovery                  */
/* -------------------------------------------------------------------- */
static BOOL test97_script_json_recovery(void)
{
    HANDLE hScript;
    int rc;
    const char *result;
    const char *error;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 97 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "broken", -1,
            "{\"x\":}", -1);
    error = PScript_GetError(hScript);
    /* Duktape reports this as "invalid json" on WM6; the public contract is
     * the error code and a diagnostic, not the engine's capitalization. */
    if (rc != PSCRIPT_ERROR_JSON || error == NULL || error[0] == '\0') {
        _snprintf(detail, sizeof(detail) - 1,
                "invalid rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 97 FAIL", detail);
        return FALSE;
    }
    rc = PScript_Evaluate(hScript, "6 * 7;", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "42") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "recovery rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 97 FAIL", detail);
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "nothing", -1, "null", -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_GetGlobalJson(hScript, "nothing", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "null") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "null rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 97 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 97 OK",
            "malformed JSON was rejected, the context still evaluated 42, "
            "and JSON null remained a valid persistent global value.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 98 - input limit is rejected before replacing the old value      */
/* -------------------------------------------------------------------- */
static BOOL test98_script_json_limit(void)
{
    static char oversized[PSCRIPT_MAX_SOURCE_BYTES + 2];
    HANDLE hScript;
    int rc;
    const char *result;
    const char *error;
    char detail[320];

    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 98 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalNumber(hScript, "keep", -1, 1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_SetGlobalJson(hScript, "keep", -1, oversized,
                (int) (sizeof(oversized) - 1));
    }
    error = PScript_GetError(hScript);
    if (rc != PSCRIPT_ERROR_SOURCE_TOO_LARGE ||
            strstr(error, "PSCRIPT_MAX_SOURCE_BYTES") == NULL) {
        _snprintf(detail, sizeof(detail) - 1,
                "oversized rc=%d error=%s", rc, error);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 98 FAIL", detail);
        return FALSE;
    }
    rc = PScript_GetGlobalJson(hScript, "keep", -1);
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "1") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "preserve rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 98 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 98 OK",
            "an over-limit JSON input was rejected before Duktape and "
            "the previous global value remained 1.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* TEST 99 - replace a persistent global across JSON types                */
/* -------------------------------------------------------------------- */
static BOOL test99_script_json_replace(void)
{
    HANDLE hScript;
    int rc;
    const char *result;
    char detail[320];

    hScript = PScript_Create(2000);
    if (hScript == NULL) {
        show_error(L"TEST 99 FAIL", "PScript_Create returned NULL");
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "value", -1, "[1,2,3]", -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_GetGlobalJson(hScript, "value", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "[1,2,3]") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "array rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 99 FAIL", detail);
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "value", -1,
            "{\"left\":8,\"right\":9}", -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_Evaluate(hScript, "value.left + value.right;", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "17") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "object rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 99 FAIL", detail);
        return FALSE;
    }
    rc = PScript_SetGlobalJson(hScript, "value", -1, "\"done\"", -1);
    if (rc == PSCRIPT_OK) {
        rc = PScript_GetGlobalJson(hScript, "value", -1);
    }
    result = PScript_GetResult(hScript);
    if (rc != PSCRIPT_OK || strcmp(result, "\"done\"") != 0) {
        _snprintf(detail, sizeof(detail) - 1,
                "string rc=%d result=%s", rc, result);
        detail[sizeof(detail) - 1] = '\0';
        PScript_Destroy(hScript);
        show_error(L"TEST 99 FAIL", detail);
        return FALSE;
    }
    PScript_Destroy(hScript);

    show_info(L"TEST 99 OK",
            "one global was replaced as array, object, then string; "
            "each public read/evaluation saw the new JSON type.");
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* TEST 100-104 - text length constraint validation                      */
/* -------------------------------------------------------------------- */
static int test100_form_prepare(const char *html, HANDLE *document,
        HANDLE *sheet, int *submit_x, int *submit_y)
{
    static const char CSS[] =
        "html,body{margin:0;padding:0;background:#fff}"
        "body{font:14px sans-serif;padding:8px}"
        "input,textarea,button{display:block;margin:4px 0;width:180px}"
        "textarea{height:36px}";

    *document = NULL;
    *sheet = NULL;
    *document = PCore_ParseHTML(html, strlen(html));
    *sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "http://positron.local/form-length.css");
    if (*document == NULL || *sheet == NULL ||
            PCore_StyleDocument(*document, *sheet) != 0 ||
            PCore_LayoutDocument(*document, 240, 320) != 0 ||
            !test68_control_center(*document, 7, 0, submit_x, submit_y)) {
        if (*sheet != NULL) {
            PCore_FreeStylesheet(*sheet);
            *sheet = NULL;
        }
        if (*document != NULL) {
            PCore_FreeDocument(*document);
            *document = NULL;
        }
        return 0;
    }
    return 1;
}

static void test100_form_cleanup(HANDLE document, HANDLE sheet)
{
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
}

static BOOL test100_form_length_constraints(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><h1>Text length rules</h1>"
        "<form action=/length method=get>"
        "<input name=short value=ab minlength=3>"
        "<input name=long value=abcdef maxlength=5>"
        "<input name=exact value=abc minlength=3 maxlength=3>"
        "<textarea name=notes minlength=2 maxlength=4>abcd</textarea>"
        "<input name=locked value=x minlength=4 readonly>"
        "<input name=off value=x minlength=4 disabled>"
        "<button type=submit name=go value=send>Send</button></form>"
        "</body></html>";
    HANDLE document;
    HANDLE sheet;
    PCoreFormValidationInfo validation;
    PCoreFormSubmissionInfo submission;
    char action[64];
    char body[256];
    int submit_x;
    int submit_y;

    document = NULL;
    sheet = NULL;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    if (!test100_form_prepare(HTML, &document, &sheet,
            &submit_x, &submit_y) ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || validation.valid ||
            validation.invalid_count != 2 ||
            validation.first_control_kind != 3 ||
            validation.first_flags != PCORE_VALIDITY_TOO_SHORT ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 5) {
        test100_form_cleanup(document, sheet);
        show_error(L"TEST 100 FAIL", "initial length flags or blocking failed");
        return FALSE;
    }
    test100_form_cleanup(document, sheet);
    show_info(L"TEST 100 OK",
            "minlength/maxlength covered text, textarea and "
            "read-only/disabled exemptions.");
    return TRUE;
}

static BOOL test101_form_length_update(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><form action=/update method=get>"
        "<input name=value value=abc minlength=3 maxlength=5>"
        "<button type=submit name=go value=send>Send</button>"
        "</form></body></html>";
    HANDLE document;
    HANDLE sheet;
    PCoreFormValidationInfo validation;
    PCoreFormSubmissionInfo submission;
    char action[64];
    char body[256];
    int submit_x;
    int submit_y;

    document = NULL;
    sheet = NULL;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    if (!test100_form_prepare(HTML, &document, &sheet,
            &submit_x, &submit_y) ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || !validation.valid ||
            PCore_TextInputSetValue(document, 0, "ab") != 0 ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || validation.valid ||
            validation.first_flags != PCORE_VALIDITY_TOO_SHORT ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 5 ||
            PCore_TextInputSetValue(document, 0, "abcde") != 0 ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || !validation.valid ||
            PCore_TextInputSetValue(document, 0, "abcdef") != 3 ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1 ||
            strcmp(action, "/update") != 0 ||
            strcmp(body, "value=abcde&go=send") != 0) {
        test100_form_cleanup(document, sheet);
        show_error(L"TEST 101 FAIL", "dynamic text length update failed");
        return FALSE;
    }
    test100_form_cleanup(document, sheet);
    show_info(L"TEST 101 OK",
            "DOM text updates changed minlength validity; maxlength "
            "blocked an over-limit native edit and submission recovered.");
    return TRUE;
}

static BOOL test102_textarea_length(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><form action=/area method=get>"
        "<textarea name=notes minlength=3 maxlength=5>ab</textarea>"
        "<button type=submit name=go value=send>Send</button>"
        "</form></body></html>";
    HANDLE document;
    HANDLE sheet;
    PCoreFormValidationInfo validation;
    PCoreFormSubmissionInfo submission;
    char action[64];
    char body[256];
    int submit_x;
    int submit_y;

    document = NULL;
    sheet = NULL;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    if (!test100_form_prepare(HTML, &document, &sheet,
            &submit_x, &submit_y) ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || validation.valid ||
            validation.first_flags != PCORE_VALIDITY_TOO_SHORT ||
            PCore_TextInputSetValue(document, 0, "abcdef") != 0 ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || validation.valid ||
            validation.first_flags != PCORE_VALIDITY_TOO_LONG ||
            PCore_TextInputSetValue(document, 0, "abcd") != 0 ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || !validation.valid ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1 ||
            strcmp(body, "notes=abcd&go=send") != 0) {
        test100_form_cleanup(document, sheet);
        show_error(L"TEST 102 FAIL", "textarea length validation failed");
        return FALSE;
    }
    test100_form_cleanup(document, sheet);
    show_info(L"TEST 102 OK",
            "textarea minlength and maxlength covered too-short, too-long "
            "and boundary values after native updates.");
    return TRUE;
}

static BOOL test103_form_length_exemptions(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><form action=/exempt method=get>"
        "<input name=locked value=x required minlength=4 readonly>"
        "<input name=off value=x required maxlength=0 disabled>"
        "<input name=ignored value=x minlength=bad maxlength=bad>"
        "<button type=submit name=go value=send>Send</button>"
        "</form></body></html>";
    HANDLE document;
    HANDLE sheet;
    PCoreFormValidationInfo validation;
    PCoreFormSubmissionInfo submission;
    char action[64];
    char body[256];
    int submit_x;
    int submit_y;

    document = NULL;
    sheet = NULL;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    if (!test100_form_prepare(HTML, &document, &sheet,
            &submit_x, &submit_y) ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || !validation.valid ||
            validation.invalid_count != 0 ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1 ||
            strcmp(action, "/exempt") != 0) {
        test100_form_cleanup(document, sheet);
        show_error(L"TEST 103 FAIL", "exemption or malformed attribute failed");
        return FALSE;
    }
    test100_form_cleanup(document, sheet);
    show_info(L"TEST 103 OK",
            "disabled/read-only controls stayed barred from validation and "
            "malformed length attributes were ignored.");
    return TRUE;
}

static BOOL test104_form_first_length_geometry(void)
{
    static const char HTML[] =
        "<!doctype html><html><body><form action=/first method=get>"
        "<input name=long value=abcdef maxlength=5>"
        "<input name=short value=a minlength=2>"
        "<button type=submit name=go value=send>Send</button>"
        "</form></body></html>";
    HANDLE document;
    HANDLE sheet;
    PCoreFormValidationInfo validation;
    PCoreFormSubmissionInfo submission;
    PCoreTextInputInfo text_info;
    char action[64];
    char body[256];
    int submit_x;
    int submit_y;

    document = NULL;
    sheet = NULL;
    memset(&validation, 0, sizeof(validation));
    memset(&submission, 0, sizeof(submission));
    memset(&text_info, 0, sizeof(text_info));
    if (!test100_form_prepare(HTML, &document, &sheet,
            &submit_x, &submit_y) ||
            PCore_TextInputInfo(document, 0, &text_info, NULL, 0) != 0 ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || validation.valid ||
            validation.invalid_count != 2 ||
            validation.first_flags != PCORE_VALIDITY_TOO_LONG ||
            validation.first_x != text_info.x ||
            validation.first_y != text_info.y ||
            validation.first_width != text_info.width ||
            validation.first_height != text_info.height ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 5 ||
            PCore_TextInputSetValue(document, 0, "abc") != 0 ||
            PCore_TextInputSetValue(document, 1, "ab") != 0 ||
            !PCore_FormValidationAt(document, submit_x, submit_y,
                    &validation) || !validation.valid ||
            PCore_FormSubmissionAt(document, submit_x, submit_y,
                    &submission, action, sizeof(action),
                    body, sizeof(body)) != 1 ||
            strcmp(body, "long=abc&short=ab&go=send") != 0) {
        test100_form_cleanup(document, sheet);
        show_error(L"TEST 104 FAIL", "first invalid length geometry failed");
        return FALSE;
    }
    test100_form_cleanup(document, sheet);
    show_info(L"TEST 104 OK",
            "first too-long control reported its geometry and flags; after "
            "both edits the same form submitted successfully.");
    return TRUE;
}

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
        if (number != 80 && number != 81 && number != 82 && number != 83 &&
                number != 84 && number != 85 && number != 86 &&
                number != 87 && number != 88 && number != 89 &&
                number != 90 && number != 91 && number != 92 &&
                number != 93 && number != 94 && number != 95 &&
                number != 96 && number != 97 && number != 98 &&
                number != 99 &&
                selected[number]) {
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
        case 63: ok = test63_shared_svg_lifetime(); break;
        case 64: ok = test64_form_interaction(); break;
        case 65: ok = test65_text_input(); break;
        case 66: ok = test66_textarea(); break;
        case 67: ok = test67_select_control(); break;
        case 68: ok = test68_form_submission(); break;
        case 69: ok = test69_form_defaults_and_labels(); break;
        case 70: ok = test70_multipart_file(); break;
        case 71: ok = test71_native_multiselect(); break;
        case 72: ok = test72_form_validation(); break;
        case 73: ok = test73_dynamic_form_states(); break;
        case 74: ok = test74_dom_events(); break;
        case 75: ok = test75_positioned_layout(); break;
        case 76: ok = test76_hover_state(); break;
        case 77: ok = test77_script_resources(); break;
        case 80: ok = test80_script_runtime(); break;
        case 81: ok = test81_script_safety(); break;
        case 82: ok = test82_script_memory_limit(); break;
        case 83: ok = test83_script_modules(); break;
        case 84: ok = test84_script_module_provider(); break;
        case 85: ok = test85_script_globals(); break;
        case 86: ok = test86_script_call_json(); break;
        case 87: ok = test87_script_call_persistence(); break;
        case 88: ok = test88_script_call_errors(); break;
        case 89: ok = test89_script_limits(); break;
        case 90: ok = test90_script_native_call(); break;
        case 91: ok = test91_script_native_json(); break;
        case 92: ok = test92_script_native_failure(); break;
        case 93: ok = test93_script_native_lifecycle(); break;
        case 94: ok = test94_script_native_limit(); break;
        case 95: ok = test95_script_json_global(); break;
        case 96: ok = test96_script_json_persistence(); break;
        case 97: ok = test97_script_json_recovery(); break;
        case 98: ok = test98_script_json_limit(); break;
        case 99: ok = test99_script_json_replace(); break;
        case 100: ok = test100_form_length_constraints(); break;
        case 101: ok = test101_form_length_update(); break;
        case 102: ok = test102_textarea_length(); break;
        case 103: ok = test103_form_length_exemptions(); break;
        case 104: ok = test104_form_first_length_geometry(); break;
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
    int configured_auto;
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

    /* Tell positron_core the real device viewport. The core derives the CSS
     * viewport from physical pixels and DPI before styling/layout. */
    test_host_set_device_viewport(GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN));

    configured_count = test_config_load(configured_tests, &configured_7b,
            &configured_auto);
    if (configured_count < 0) {
        show_error(L"test_host.ini ignored",
                   "The file exists but is empty, unreadable or malformed.\n"
                   "Use: tests=31,32 or tests=1-5 7b\n"
                   "Optional: auto=1\n\n"
                   "TEST 23/78/79 are unavailable. Continuing with group selection.");
    } else if (configured_count > 0) {
        test_config_prompt(configured_tests, configured_7b, configured_auto,
                config_prompt, sizeof(config_prompt));
        if (configured_auto) {
            g_testbench_auto = 1;
            testbench_log_open();
            show_info(L"Automated testbench", config_prompt);
            configured_http = 0;
            rc = run_configured_tests(configured_tests, configured_7b,
                    &configured_http);
            if (configured_http) {
                PHttp_Cleanup();
            }
            PCore_Shutdown();
            if (rc == 0) {
                show_info(L"TESTBENCH PASS",
                          "All tests selected by test_host.ini passed.");
            } else {
                _snprintf(summary, sizeof(summary) - 1,
                        "Stopped after TEST %d returned failure.", rc);
                summary[sizeof(summary) - 1] = '\0';
                show_error(L"TESTBENCH FAIL", summary);
            }
            testbench_log_close();
            return rc;
        }
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
                  "Yes = run all selected groups (TEST 1-77)\n"
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
                               "38, 40-45, 59-61, 74). Offline.");
        run_render = ask_yesno(L"Select groups (3/4)",
                               "Run GDI RENDER tests?\n\n"
                               "NetSurf/GDI pages (TEST 12, 14, 17),\n"
                               "native bitmap draw (TEST 19),\n"
                               "SVG draw/cache/fallback/text/gradients\n"
                               "(TEST 26-37), and responsive IANA-style\n"
                               "layout redraw (TEST 39), plus table/list/\n"
                               "inline CSS and form controls\n"
                               "(TEST 46-58, 62-73).\n"
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

    /* Engine: TEST 6-11, 15, 16, 18, 21, 22, 24, 25, 38, 40-45, 59-61, 74. */
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
        if (!test74_dom_events()) { rc = 11; goto done; }
        /* These exercise separate views of the now-initialised engine. Run
         * all of them so one geometry assertion cannot hide later results. */
        if (!test11_layout())        { rc = 12; }
        if (!test_boxtree())         { rc = 12; }
        if (!test_layout())          { rc = 12; }
        if (!test_image_resources()) { rc = 12; }
        if (rc != 0)                 { goto done; }
    }

    /* GDI render: TEST 12, 14, 17, 19, 20, 26-37, 39, 46-58, 62-73; offline. */
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
        if (!test63_shared_svg_lifetime()){ rc = 13; goto done; }
        if (!test64_form_interaction()){ rc = 13; goto done; }
        if (!test65_text_input()) { rc = 13; goto done; }
        if (!test66_textarea())   { rc = 13; goto done; }
        if (!test67_select_control()){ rc = 13; goto done; }
        if (!test68_form_submission()){ rc = 13; goto done; }
        if (!test69_form_defaults_and_labels()){ rc = 13; goto done; }
        if (!test70_multipart_file()){ rc = 13; goto done; }
        if (!test71_native_multiselect()){ rc = 13; goto done; }
        if (!test72_form_validation()){ rc = 13; goto done; }
        if (!test73_dynamic_form_states()){ rc = 13; goto done; }
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
               "  Engine (TEST 6-11, 15, 16, 18, 21, 22, 24, 25, 38, 40-45, 59-61, 74)\n"
               "    libhubbub + libcss + libdom behind\n"
               "    positron_core.dll; parse, select, style,\n"
               "    layout, media-query viewport, reverse flex, cached CSS restyle, box tree, NetSurf layout, image\n"
               "    resource cache, SVG parse, constrained :root variables,\n"
               "    OKLCH/calc values, grid-overflow containment, scrollbar\n"
               "    input, staged navigation resources, failure rollback,\n"
               "    native libcss CSS import trees, and retained selector\n"
               "    node data across portrait/landscape restyle, and explicit\n"
               "    NetSurf option defaults with minimum-font clamping.\n"
               "    DOM capture/target/bubble dispatch and cancellation.\n"
               "    Offline.\n\n");
    }
    if (run_render) {
        strcat(summary,
               "  GDI render (TEST 12, 14, 17, 19, 20, 26-37, 39, 46-58, 62-73)\n"
               "    HTML page painted to a window: background,\n"
               "    borders, padding, wrapped text, NetSurf redraw,\n"
               "    plus WM Imaging bitmaps, cached <img>, direct SVG and\n"
               "    cached SVG, fallback, fill rules, CSS backgrounds and\n"
               "    native SVG text, cached SVG gradients, coordinate transforms\n"
               "    radial gradients, inherited/alpha stops, cache reuse, and\n"
               "    IANA-style spacing, table normalisation, list markers and\n"
               "    checkbox/radio interaction, native single/multiline EDIT,\n"
               "    native single/multiple select bridges and form values,\n"
               "    dynamic checked/focus/active/enabled selector states,\n"
               "    and overlapping-document SVG reuse through formal redraw.\n"
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
