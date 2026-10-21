/* Application policy and presentation only. Core renders the resulting HTML;
 * Browser remains the owner of the navigation stack. No I/O or script session. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <winver.h>
#include "app_internal_pages.h"
#include "app_downloads.h"
#include "positron_browser.h"
#include "positron_core.h"
#include "app_debug.h"
#include "app_version.h"
#include "app_url_router.h"
#ifdef _DEBUG
#include "resource.h"
#endif

typedef struct AppInternalRegistration {
    const char *name;
    int page_kind;
    AppInternalRouteKind kind;
    const char *target;
} AppInternalRegistration;

static const AppInternalRegistration app_internal_registry[] = {
    { "welcome", APP_I18N_PAGE_WELCOME, APP_INTERNAL_PAGE, NULL },
    { "controls", APP_I18N_PAGE_CONTROLS, APP_INTERNAL_PAGE, NULL },
    { "about", APP_I18N_PAGE_ABOUT, APP_INTERNAL_PAGE, NULL },
    { "newtab", APP_I18N_PAGE_NEWTAB, APP_INTERNAL_PAGE, NULL },
    { "history", APP_I18N_PAGE_HISTORY, APP_INTERNAL_PAGE, NULL },
    { "downloads", APP_I18N_PAGE_DOWNLOADS, APP_INTERNAL_PAGE, NULL },
    { "settings", APP_I18N_PAGE_SETTINGS, APP_INTERNAL_PAGE, NULL },
    { "version", APP_I18N_PAGE_ABOUT, APP_INTERNAL_ALIAS,
        "positron://about#version" },
    { "system", APP_I18N_PAGE_ABOUT, APP_INTERNAL_ALIAS,
        "positron://about#system" },
    { "quit", 0, APP_INTERNAL_COMMAND, "positron://quit" }
};

static int app_internal_equal(const char *left, const char *right, size_t n)
{
    size_t i;
    unsigned char a;
    unsigned char b;

    for (i = 0; i < n; i++) {
        a = (unsigned char) left[i];
        b = (unsigned char) right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char) (a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char) (b + 'a' - 'A');
        if (a != b) return 0;
    }
    return 1;
}

int AppInternalPages_Resolve(const char *url, AppNavigationSource source,
        AppInternalRoute *out_route)
{
    const AppInternalRegistration *entry;
    const char *name;
    const char *tail;
    const char *fragment;
    size_t length;
    size_t name_length;
    size_t i;

    if (out_route == NULL) return 1;
    memset(out_route, 0, sizeof(*out_route));
    if (url == NULL) return 1;
    length = strlen(url);
    if (length < 11 || length >= APP_INTERNAL_URL_MAX ||
            !app_internal_equal(url, "positron://", 11)) return 1;
    for (i = 0; i < length; i++) {
        if ((unsigned char) url[i] <= 32 || (unsigned char) url[i] == 127 ||
                url[i] == '\\') return 1;
    }
    name = url + 11;
    tail = name;
    while (*tail != '\0' && *tail != '/' && *tail != '?' &&
            *tail != '#') tail++;
    name_length = (size_t) (tail - name);
    if (*tail == '/') tail++;
    for (i = 0; i < sizeof(app_internal_registry) /
            sizeof(app_internal_registry[0]); i++) {
        entry = &app_internal_registry[i];
        if (strlen(entry->name) != name_length ||
                !app_internal_equal(name, entry->name, name_length)) continue;
        if (entry->kind == APP_INTERNAL_COMMAND) {
            if (source != APP_NAV_SOURCE_ADDRESS || *tail != '\0') return 1;
        } else if (entry->kind == APP_INTERNAL_ALIAS) {
            if (*tail != '\0') return 1;
        } else if (entry->page_kind == APP_I18N_PAGE_WELCOME ||
                entry->page_kind == APP_I18N_PAGE_CONTROLS) {
            if (*tail != '\0' && *tail != '?' && *tail != '#') return 1;
        } else if (*tail != '\0') {
            if (entry->page_kind != APP_I18N_PAGE_ABOUT ||
                    *tail != '#') return 1;
            fragment = tail + 1;
            if (strlen(fragment) == 7 &&
                    app_internal_equal(fragment, "version", 7)) {
                tail = "#version";
            } else if (strlen(fragment) == 6 &&
                    app_internal_equal(fragment, "system", 6)) {
                tail = "#system";
            } else {
                return 1;
            }
        }
        if (entry->target != NULL) {
            strcpy(out_route->url, entry->target);
        } else {
            strcpy(out_route->url, "positron://");
            strcat(out_route->url, entry->name);
            strcat(out_route->url, tail);
        }
        out_route->kind = entry->kind;
        out_route->page_kind = entry->page_kind;
        return 0;
    }
    return 1;
}

typedef struct AppHtmlWriter {
    char *bytes;
    unsigned int used;
    int failed;
} AppHtmlWriter;

static void app_html_append(AppHtmlWriter *writer, const char *text)
{
    size_t count;

    if (writer->failed || text == NULL) return;
    count = strlen(text);
    if (count >= APP_INTERNAL_HTML_MAX - writer->used) {
        writer->failed = 1;
        return;
    }
    memcpy(writer->bytes + writer->used, text, count);
    writer->used += (unsigned int) count;
    writer->bytes[writer->used] = '\0';
}

static void app_html_escape(AppHtmlWriter *writer, const char *text)
{
    char byte[2];

    if (text == NULL) return;
    byte[1] = '\0';
    while (*text != '\0' && !writer->failed) {
        switch (*text) {
        case '&': app_html_append(writer, "&amp;"); break;
        case '<': app_html_append(writer, "&lt;"); break;
        case '>': app_html_append(writer, "&gt;"); break;
        case '"': app_html_append(writer, "&quot;"); break;
        case '\'': app_html_append(writer, "&#39;"); break;
        default:
            byte[0] = *text;
            app_html_append(writer, byte);
            break;
        }
        text++;
    }
}

static void app_html_row(AppHtmlWriter *writer, const char *label,
        const char *value)
{
    app_html_append(writer, "<p>");
    app_html_escape(writer, label);
    app_html_append(writer, ": ");
    app_html_escape(writer, value);
    app_html_append(writer, "</p>");
}

static void app_internal_dll_version(AppHtmlWriter *writer,
        const WCHAR *dll_name, const WCHAR *export_name, const char *label,
        int chinese, int encoded)
{
    typedef unsigned long (*AppAbiVersionFn)(void);
    HMODULE module;
    AppAbiVersionFn version_fn;
    unsigned long version;
    char value[64];

    module = GetModuleHandleW(dll_name);
    version_fn = module == NULL ? NULL :
            (AppAbiVersionFn) GetProcAddress(module, export_name);
    if (version_fn == NULL) {
        app_html_row(writer, label, chinese ?
                "\346\234\252\346\217\220\344\276\233" : "Not provided");
        return;
    }
    version = version_fn();
    if (encoded) {
        _snprintf(value, sizeof(value) - 1, "%lu.%lu",
                version >> 16, version & 65535UL);
    } else {
        _snprintf(value, sizeof(value) - 1, "%lu", version);
    }
    value[sizeof(value) - 1] = '\0';
    app_html_row(writer, label, value);
}

/* SPI strings are OEM input, not HTML or an inferred marketing version.
 * uiParam is a WCHAR count, as in the WM6 SDK FullScreen sample. Reject
 * empty, unterminated, malformed UTF-16 and control characters before UTF-8. */
static int app_internal_system_text(const WCHAR *text, unsigned int count,
        char *output, unsigned int capacity)
{
    unsigned int i;
    int required;

    if (output == NULL || capacity == 0) return 1;
    output[0] = '\0';
    if (text == NULL || count == 0) return 1;
    for (i = 0; i < count && text[i] != L'\0'; i++) {
        if (text[i] < 32 || text[i] == 127) return 1;
        if (text[i] >= 0xd800 && text[i] <= 0xdbff) {
            i++;
            if (i >= count || text[i] < 0xdc00 || text[i] > 0xdfff) return 1;
        } else if (text[i] >= 0xdc00 && text[i] <= 0xdfff) {
            return 1;
        }
    }
    if (i == 0 || i >= count) return 1;
    required = WideCharToMultiByte(CP_UTF8, 0, text, (int) i, NULL, 0,
            NULL, NULL);
    if (required <= 0 || (unsigned int) required >= capacity) return 1;
    if (WideCharToMultiByte(CP_UTF8, 0, text, (int) i, output, required,
            NULL, NULL) != required) {
        output[0] = '\0';
        return 1;
    }
    output[required] = '\0';
    return 0;
}

typedef enum AppSystemResult {
    APP_SYSTEM_OK,
    APP_SYSTEM_MISSING,
    APP_SYSTEM_UNSUPPORTED,
    APP_SYSTEM_FAILED,
    APP_SYSTEM_INVALID
} AppSystemResult;

static const char *app_internal_system_status(AppSystemResult result, int chinese)
{
    switch (result) {
    case APP_SYSTEM_MISSING:
        return chinese ? "\346\234\252\346\217\220\344\276\233" : "Not provided";
    case APP_SYSTEM_UNSUPPORTED:
        return chinese ? "API \344\270\215\345\217\257\347\224\250" : "API unavailable";
    case APP_SYSTEM_INVALID:
        return chinese ? "\346\227\240\346\225\210\346\225\260\346\215\256" : "Invalid data";
    default:
        return chinese ? "\346\237\245\350\257\242\345\244\261\350\264\245" : "Query failed";
    }
}

/* Registry byte counts are not WCHAR capacities: reject embedded NULs and
 * trailing garbage as well as malformed OEM text. AKU is kept verbatim; its
 * leading dot is not an OS release number and is never mapped to one. */
static int app_internal_registry_text(DWORD type, const WCHAR *value,
        DWORD bytes, char *output, unsigned int capacity)
{
    unsigned int count;
    unsigned int i;

    if (output == NULL || capacity == 0) return 1;
    output[0] = '\0';
    if (type != REG_SZ || value == NULL || bytes < 2 * sizeof(WCHAR) ||
            bytes > 128 * sizeof(WCHAR) || bytes % sizeof(WCHAR) != 0) return 1;
    count = (unsigned int) (bytes / sizeof(WCHAR));
    if (value[count - 1] != L'\0') return 1;
    for (i = 0; i < count - 1; i++) if (value[i] == L'\0') return 1;
    return app_internal_system_text(value, count, output, capacity);
}

/* Only system-version metadata is read; no enumeration, writes, or device IDs.
 * ProductName/OSVersion are optional OEM fields, not a universal WM contract. */
static AppSystemResult app_internal_registry_version(const WCHAR *name,
        char *output, unsigned int capacity)
{
    HKEY key;
    WCHAR value[128];
    DWORD type;
    DWORD bytes;
    LONG status;
    AppSystemResult result;
#ifdef _DEBUG
    char message[160];
    char field[64];
#endif

    if (output == NULL || capacity == 0 || name == NULL) return APP_SYSTEM_INVALID;
    output[0] = '\0';
    key = NULL;
    status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\Versions", 0, KEY_READ, &key);
    if (status == ERROR_SUCCESS) {
        memset(value, 0xff, sizeof(value));
        type = 0;
        bytes = sizeof(value);
        status = RegQueryValueExW(key, name, NULL, &type, (LPBYTE) value, &bytes);
        RegCloseKey(key);
    }
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        result = APP_SYSTEM_MISSING;
    } else if (status == ERROR_MORE_DATA) {
        result = APP_SYSTEM_INVALID;
    } else if (status != ERROR_SUCCESS) {
        result = APP_SYSTEM_FAILED;
    } else {
        result = app_internal_registry_text(type, value, bytes, output, capacity) ?
                APP_SYSTEM_INVALID : APP_SYSTEM_OK;
    }
#ifdef _DEBUG
    field[0] = '\0';
    (void) app_internal_system_text(name, 32, field, sizeof(field));
    _snprintf(message, sizeof(message) - 1,
            "positron system-registry field=%s result=%d error=%ld\r\n",
            field, (int) result, status);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
#endif
    return result;
}

static int app_internal_kernel_text(const OSVERSIONINFO *version,
        char *output, unsigned int capacity)
{
    const char *family;
    int written;

    if (output == NULL || capacity == 0) return 1;
    output[0] = '\0';
    if (version == NULL) return 1;
    switch (version->dwPlatformId) {
    case VER_PLATFORM_WIN32_CE: family = "Windows CE"; break;
    case VER_PLATFORM_WIN32_NT: family = "Windows NT"; break;
    case VER_PLATFORM_WIN32_WINDOWS: family = "Windows"; break;
    case VER_PLATFORM_WIN32s: family = "Win32s"; break;
    default: family = NULL; break;
    }
    if (family != NULL) {
        written = _snprintf(output, capacity, "%s %lu.%lu.%lu", family,
                version->dwMajorVersion, version->dwMinorVersion,
                version->dwBuildNumber);
    } else {
        written = _snprintf(output, capacity, "Platform ID %lu: %lu.%lu.%lu",
                version->dwPlatformId, version->dwMajorVersion,
                version->dwMinorVersion, version->dwBuildNumber);
    }
    if (written < 0 || (unsigned int) written >= capacity) {
        output[0] = '\0';
        return 1;
    }
    return 0;
}

/* Only fixed ROM component paths are queried. Version metadata is supplemental
 * component evidence, never a substitute for an OS product-identification API.
 * Optional coredll exports and a 64 KiB heap budget keep old/OEM ROMs safe. */
static int app_internal_resource_slice(const void *block, DWORD size,
        const void *value, unsigned int bytes)
{
    ULONG_PTR offset;

    if (block == NULL || value == NULL ||
            (ULONG_PTR) value < (ULONG_PTR) block) return 0;
    offset = (ULONG_PTR) value - (ULONG_PTR) block;
    return offset <= size && bytes <= size - offset;
}

static AppSystemResult app_internal_component_version(const WCHAR *path,
        char *output, unsigned int capacity, int chinese)
{
    typedef DWORD (WINAPI *AppVersionSizeFn)(LPWSTR, LPDWORD);
    typedef BOOL (WINAPI *AppVersionReadFn)(LPWSTR, DWORD, DWORD, LPVOID);
    typedef BOOL (WINAPI *AppVersionQueryFn)(const LPVOID, LPWSTR, LPVOID *, PUINT);
    AppVersionSizeFn size_fn;
    AppVersionReadFn read_fn;
    AppVersionQueryFn query_fn;
    HMODULE module;
    DWORD size;
    DWORD ignored;
    void *block;
    void *value;
    UINT length;
    VS_FIXEDFILEINFO fixed;
    WORD languages[32];
    unsigned int language_count;
    unsigned int i;
    WCHAR key[80];
    char product[384];
    int written;
    AppSystemResult result;
    const char *stage;
    DWORD error;
#ifdef _DEBUG
    char message[256];
    char filename[96];
#endif

    if (output == NULL || capacity == 0) return APP_SYSTEM_INVALID;
    output[0] = '\0';
    if (path == NULL) return APP_SYSTEM_INVALID;
    block = NULL;
    error = 0;
    stage = "module";
    result = APP_SYSTEM_UNSUPPORTED;
    module = GetModuleHandleW(L"coredll.dll");
    if (module == NULL) { error = GetLastError(); goto done; }
    stage = "exports";
    size_fn = (AppVersionSizeFn) GetProcAddress(module, L"GetFileVersionInfoSizeW");
    read_fn = (AppVersionReadFn) GetProcAddress(module, L"GetFileVersionInfoW");
    query_fn = (AppVersionQueryFn) GetProcAddress(module, L"VerQueryValueW");
    if (size_fn == NULL || read_fn == NULL || query_fn == NULL) {
        error = GetLastError();
        goto done;
    }
    stage = "size";
    SetLastError(0);
    ignored = 0;
    size = size_fn((LPWSTR) path, &ignored);
    if (size == 0) {
        error = GetLastError();
        result = error == ERROR_RESOURCE_DATA_NOT_FOUND ||
                error == ERROR_RESOURCE_TYPE_NOT_FOUND ||
                error == ERROR_RESOURCE_NAME_NOT_FOUND ? APP_SYSTEM_MISSING : APP_SYSTEM_FAILED;
        goto done;
    }
    result = APP_SYSTEM_INVALID;
    if (size < sizeof(fixed) || size > 65536UL) goto done;
    stage = "allocation";
    result = APP_SYSTEM_FAILED;
    block = malloc(size);
    if (block == NULL) { error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    product[0] = '\0';
    stage = "read";
    SetLastError(0);
    if (!read_fn((LPWSTR) path, 0, size, block)) { error = GetLastError(); goto done; }
    stage = "root";
    result = APP_SYSTEM_INVALID;
    value = NULL;
    length = 0;
    if (!query_fn(block, L"\\", &value, &length) || length < sizeof(fixed) ||
            !app_internal_resource_slice(block, size, value, sizeof(fixed))) goto done;
    memcpy(&fixed, value, sizeof(fixed));
    if (fixed.dwSignature != VS_FFI_SIGNATURE) goto done;
    value = NULL;
    length = 0;
    if (query_fn(block, L"\\VarFileInfo\\Translation", &value, &length) &&
            length >= 4 && (length & 3U) == 0 &&
            app_internal_resource_slice(block, size, value, length)) {
        language_count = length / 4;
        if (language_count > 16) language_count = 16;
        memcpy(languages, value, language_count * 4);
        for (i = 0; i < language_count; i++) {
            _snwprintf(key, sizeof(key) / sizeof(key[0]),
                    L"\\StringFileInfo\\%04x%04x\\ProductName",
                    (unsigned int) languages[i * 2],
                    (unsigned int) languages[i * 2 + 1]);
            key[sizeof(key) / sizeof(key[0]) - 1] = L'\0';
            value = NULL;
            length = 0;
            if (query_fn(block, key, &value, &length) && length > 0 &&
                    length <= 384 && app_internal_resource_slice(block,
                    size, value, length * sizeof(WCHAR)) &&
                    !app_internal_system_text((const WCHAR *) value,
                    length, product, sizeof(product))) break;
        }
    }
    written = _snprintf(output, capacity, chinese ?
            "%s%s\346\226\207\344\273\266 %lu.%lu.%lu.%lu; \344\272\247\345\223\201 %lu.%lu.%lu.%lu" :
            "%s%sfile %lu.%lu.%lu.%lu; product %lu.%lu.%lu.%lu",
            product, product[0] == '\0' ? "" : "; ",
            fixed.dwFileVersionMS >> 16, fixed.dwFileVersionMS & 65535UL,
            fixed.dwFileVersionLS >> 16, fixed.dwFileVersionLS & 65535UL,
            fixed.dwProductVersionMS >> 16, fixed.dwProductVersionMS & 65535UL,
            fixed.dwProductVersionLS >> 16, fixed.dwProductVersionLS & 65535UL);
    stage = "format";
    if (written >= 0 && (unsigned int) written < capacity) result = APP_SYSTEM_OK;
done:
    free(block);
    if (result) output[0] = '\0';
#ifdef _DEBUG
    filename[0] = '\0';
    (void) app_internal_system_text(path, 64, filename, sizeof(filename));
    _snprintf(message, sizeof(message) - 1,
            "positron system-version-resource path=%s stage=%s result=%d error=%lu\r\n",
            filename, stage, (int) result, error);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
#else
    (void) stage;
    (void) error;
#endif
    return result;
}

static void app_internal_system(AppHtmlWriter *writer, int chinese)
{
    OSVERSIONINFO version;
    WCHAR platform[128];
    WCHAR oem[128];
    char kernel[96];
    char details[384];
    char os[384];
    char product[384];
    char os_version[384];
    char aku[384];
    char component[512];
    char device[384];
    const char *unavailable;
    int kernel_ok;
    AppSystemResult product_result;
    AppSystemResult version_result;
    AppSystemResult aku_result;
    AppSystemResult component_result;
#ifdef _DEBUG
    char message[1536];
#endif

    unavailable = chinese ? "\344\270\215\345\217\257\347\224\250" : "Unavailable";
    strcpy(kernel, unavailable);
    details[0] = '\0';
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    kernel_ok = GetVersionEx(&version) != 0;
    if (kernel_ok) {
        if (app_internal_kernel_text(&version, kernel, sizeof(kernel))) {
            strcpy(kernel, unavailable);
        }
        if (app_internal_system_text(version.szCSDVersion,
                sizeof(version.szCSDVersion) / sizeof(version.szCSDVersion[0]),
                details, sizeof(details))) details[0] = '\0';
    }
    memset(platform, 0xff, sizeof(platform));
    if (!SystemParametersInfo(SPI_GETPLATFORMTYPE,
            sizeof(platform) / sizeof(platform[0]), platform, 0) ||
            app_internal_system_text(platform,
            sizeof(platform) / sizeof(platform[0]), os, sizeof(os))) {
        strcpy(os, unavailable);
    }
    /* WM6.5.3 SDK documents SPI_GETPLATFORMVERSION as CE_MAJOR_VER /
     * CE_MINOR_VER regardless of platform, not a marketing release query.
     * Read actual OEM metadata instead; missing fields stay missing. */
    product_result = app_internal_registry_version(L"ProductName", product, sizeof(product));
    version_result = app_internal_registry_version(L"OSVersion", os_version, sizeof(os_version));
    aku_result = app_internal_registry_version(L"Aku", aku, sizeof(aku));
    if (product_result) strcpy(product, app_internal_system_status(product_result, chinese));
    if (version_result) strcpy(os_version, app_internal_system_status(version_result, chinese));
    if (aku_result) strcpy(aku, app_internal_system_status(aku_result, chinese));
    memset(oem, 0xff, sizeof(oem));
    if (!SystemParametersInfo(SPI_GETOEMINFO,
            sizeof(oem) / sizeof(oem[0]), oem, 0) ||
            app_internal_system_text(oem, sizeof(oem) / sizeof(oem[0]),
            device, sizeof(device))) {
        strcpy(device, unavailable);
    }
    app_html_row(writer, chinese ? "\345\206\205\346\240\270" : "Kernel", kernel);
    app_html_row(writer, chinese ? "\345\271\263\345\217\260\347\261\273\345\236\213" : "Platform type", os);
    app_html_row(writer, chinese ? "\346\233\264\346\226\260\345\214\205 (AKU)" : "Update package (AKU)", aku);
    app_html_row(writer, chinese ? "\350\256\276\345\244\207 / OEM" : "Device / OEM", device);
    app_html_row(writer, chinese ? "\346\223\215\344\275\234\347\263\273\347\273\237\344\272\247\345\223\201" : "OS product", product);
    app_html_row(writer, chinese ? "\346\223\215\344\275\234\347\263\273\347\273\237\345\217\221\350\241\214\347\211\210\346\234\254" : "OS release", os_version);
    if (details[0] != '\0') app_html_row(writer, chinese ?
            "\345\206\205\346\240\270\346\211\251\345\261\225\344\277\241\346\201\257" : "Kernel additional information", details);
    app_html_append(writer, chinese ?
            "<p>\344\272\247\345\223\201\345\222\214\345\217\221\350\241\214\347\211\210\346\234\254\344\273\205\346\230\276\347\244\272\347\263\273\347\273\237\346\217\220\344\276\233\347\232\204\345\205\203\346\225\260\346\215\256\357\274\214AKU \344\270\215\346\230\257 OS \345\217\221\350\241\214\347\211\210\346\234\254\343\200\202</p>" :
            "<p>Product/release names require system metadata; AKU is not an OS release.</p>");
    component_result = app_internal_component_version(L"\\Windows\\coredll.dll",
            component, sizeof(component), chinese);
    if (component_result) strcpy(component, app_internal_system_status(component_result, chinese));
    app_html_row(writer, chinese ? "\345\206\205\346\240\270\347\273\204\344\273\266 (coredll.dll)" :
            "Kernel component (coredll.dll)", component);
#ifdef _DEBUG
    _snprintf(message, sizeof(message) - 1,
            "positron system-component coredll=%s\r\n", component);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
    _snprintf(message, sizeof(message) - 1,
            "positron system-info kernel=%s details=%s platform-id=%lu platform=%s product=%s release=%s aku=%s oem=%s\r\n",
            kernel, details, version.dwPlatformId, os, product, os_version, aku, device);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
#endif
    component_result = app_internal_component_version(L"\\Windows\\aygshell.dll",
            component, sizeof(component), chinese);
    if (component_result) strcpy(component, app_internal_system_status(component_result, chinese));
    app_html_row(writer, chinese ? "\347\263\273\347\273\237\345\244\226\345\243\263\347\273\204\344\273\266 (aygshell.dll)" :
            "OS shell component (aygshell.dll)", component);
#ifdef _DEBUG
    _snprintf(message, sizeof(message) - 1,
            "positron system-component aygshell=%s\r\n", component);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
#endif
}

static void app_internal_about(AppHtmlWriter *writer,
        const AppInternalPageData *data, int chinese)
{
    MEMORYSTATUS memory;
    char value[128];

    app_html_append(writer, chinese ? "<h2 id=\"version\">\347\211\210\346\234\254</h2>" :
            "<h2 id=\"version\">Version</h2>");
    app_html_row(writer, chinese ? "\345\272\224\347\224\250\347\211\210\346\234\254" : "Application version",
            AppVersion_Get(value, sizeof(value)) == 0 ? value :
            (chinese ? "\346\234\252\346\217\220\344\276\233" : "Not provided"));
#ifdef _DEBUG
    app_html_row(writer, chinese ? "\346\236\204\345\273\272\347\261\273\345\236\213" : "Build", "Debug");
#else
    app_html_row(writer, chinese ? "\346\236\204\345\273\272\347\261\273\345\236\213" : "Build", "Release");
#endif
    _snprintf(value, sizeof(value) - 1, "%lu.%lu",
            PBrowser_AbiVersion() >> 16, PBrowser_AbiVersion() & 65535UL);
    value[sizeof(value) - 1] = '\0';
    app_html_row(writer, "positron_browser.dll ABI", value);
    app_internal_dll_version(writer, L"positron_script.dll",
            L"PScript_AbiVersion", "positron_script.dll ABI", chinese, 1);
    app_internal_dll_version(writer, L"positron_image.dll",
            L"PImage_GetAbiVersion", "positron_image.dll ABI", chinese, 1);
    app_internal_dll_version(writer, L"positron_media.dll",
            L"pm_abi_version", "positron_media.dll ABI", chinese, 1);
    app_internal_dll_version(writer, L"positron_tls.dll",
            L"PTls_GetAbiVersion", "positron_tls.dll ABI", chinese, 0);
    app_html_row(writer, chinese ? "\345\205\266\344\273\226 DLL \347\211\210\346\234\254" : "Other DLL versions",
            chinese ? "\346\234\252\346\217\220\344\276\233" : "Not provided");
    app_html_append(writer, chinese ? "<h2 id=\"system\">\347\263\273\347\273\237</h2>" :
            "<h2 id=\"system\">System</h2>");
    app_internal_system(writer, chinese);
    app_html_row(writer, chinese ? "\347\233\256\346\240\207\346\236\266\346\236\204" : "Target architecture",
            "ARMV4I");
    _snprintf(value, sizeof(value) - 1, "%d", data->dpi);
    value[sizeof(value) - 1] = '\0';
    app_html_row(writer, "DPI", value);
    _snprintf(value, sizeof(value) - 1, "%d x %d",
            data->viewport_width, data->viewport_height);
    value[sizeof(value) - 1] = '\0';
    app_html_row(writer, chinese ? "\350\247\206\345\217\243" : "Viewport", value);
    memset(&memory, 0, sizeof(memory));
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    if (memory.dwTotalPhys != 0) {
        _snprintf(value, sizeof(value) - 1, "%lu / %lu KiB",
                memory.dwAvailPhys / 1024UL, memory.dwTotalPhys / 1024UL);
        value[sizeof(value) - 1] = '\0';
    } else {
        strcpy(value, chinese ? "\344\270\215\345\217\257\347\224\250" : "Unavailable");
    }
    app_html_row(writer, chinese ? "\345\217\257\347\224\250 / \346\200\273\347\211\251\347\220\206\345\206\205\345\255\230" :
            "Available / total physical memory", value);
}

static void app_internal_history(AppHtmlWriter *writer, HANDLE history,
        int chinese)
{
    const char *url;
    AppInternalRoute route;
    char link_id[32];
    int count;
    int i;
    int shown;
    int clickable;

    count = PBrowser_HistoryCount(history);
    if (count > PBROWSER_HISTORY_MAX) count = PBROWSER_HISTORY_MAX;
    shown = 0;
    for (i = count - 1; i >= 0; i--) {
        url = PBrowser_HistoryEntryUrl(history, i);
        if (url == NULL) continue;
        clickable = 0;
        if (AppInternalPages_Resolve(url, APP_NAV_SOURCE_HISTORY,
                &route) == 0) {
            if (route.page_kind == APP_I18N_PAGE_HISTORY) continue;
            clickable = route.kind != APP_INTERNAL_COMMAND;
        } else if (strncmp(url, "https://", 8) == 0 ||
                strncmp(url, "http://", 7) == 0) {
            clickable = 1;
        }
        app_html_append(writer, "<p class=\"history-entry\">");
        if (clickable) {
            _snprintf(link_id, sizeof(link_id) - 1, "history-%d", i);
            link_id[sizeof(link_id) - 1] = '\0';
            app_html_append(writer, "<a id=\"");
            app_html_append(writer, link_id);
            app_html_append(writer, "\" href=\"");
            app_html_escape(writer, url);
            app_html_append(writer, "\">");
        }
        app_html_escape(writer, url);
        if (clickable) app_html_append(writer, "</a>");
        app_html_append(writer, "</p>");
        shown++;
    }
    if (shown == 0) {
        app_html_append(writer, chinese ?
                "<p>\345\275\223\345\211\215\346\262\241\346\234\211\345\217\257\346\230\276\347\244\272\347\232\204\345\257\274\350\210\252\350\256\260\345\275\225\343\200\202</p>" :
                "<p>No navigation entries to display.</p>");
    }
}

/* These texts replace template tokens; status zero retains the old fixture. */
static void app_internal_history_intro(AppHtmlWriter *writer, int status,
        int chinese)
{
    if (status == 0) {
        app_html_append(writer, chinese ?
                "<p>\350\277\231\351\207\214\345\261\225\347\244\272\345\275\223\345\211\215\345\257\274\350\210\252\346\240\210\357\274\214\345\214\205\346\213\254\344\273\215\345\217\257\345\211\215\350\277\233\347\232\204\350\256\260\345\275\225\343\200\202\346\234\200\345\244\232\344\277\235\347\225\231 16 \346\235\241\357\274\214\351\200\200\345\207\272\345\272\224\347\224\250\345\220\216\344\270\215\344\277\235\347\225\231\343\200\202</p><p>\346\211\223\345\274\200\350\256\260\345\275\225\344\274\232\345\217\221\350\265\267\344\270\200\346\254\241\346\226\260\347\232\204\345\257\274\350\210\252\343\200\202</p>" :
                "<p>This is the current navigation stack, including forward entries. At most 16 entries are kept; entries disappear when the application exits.</p><p>Opening an entry starts a new navigation.</p>");
    } else {
        app_html_append(writer, chinese ?
                "<p>\346\234\254\345\234\260\350\267\250\351\207\215\345\220\257\346\234\200\345\244\232\344\277\235\347\225\231 500 \346\235\241\350\256\277\351\227\256\350\256\260\345\275\225\357\274\214\346\257\217\351\241\265 16 \346\235\241\357\274\214\346\214\211\350\256\260\345\275\225 ID \344\273\216\346\226\260\345\210\260\346\227\247\346\216\222\345\210\227\343\200\202\346\211\223\345\274\200\350\256\260\345\275\225\344\274\232\345\217\221\350\265\267\344\270\200\346\254\241\346\226\260\347\232\204\345\257\274\350\210\252\343\200\202</p><p>\344\277\235\345\255\230\347\232\204 URL \345\214\205\345\220\253\346\237\245\350\257\242\345\217\202\346\225\260\357\274\214\345\217\257\350\203\275\345\220\253\346\234\211\351\232\220\347\247\201\344\277\241\346\201\257\343\200\202\350\203\275\350\256\277\351\227\256\346\234\254\345\234\260\345\255\230\345\202\250\347\232\204\344\272\272\345\217\257\350\203\275\350\257\273\345\217\226\350\277\231\344\272\233\350\256\260\345\275\225\343\200\202</p><p>\344\275\277\347\224\250\344\270\213\346\226\271\346\216\247\344\273\266\351\207\215\346\226\260\345\212\240\350\275\275\343\200\201\346\237\245\347\234\213\346\233\264\346\227\251\350\256\260\345\275\225\346\210\226\346\270\205\351\231\244\350\256\277\351\227\256\345\216\206\345\217\262\343\200\202\346\270\205\351\231\244\351\234\200\350\246\201\347\241\256\350\256\244\343\200\202</p>" :
                "<p>Up to 500 visits are retained locally across restarts, with 16 records per page, newest IDs first. Opening a record starts a new navigation.</p><p>Saved URLs include query strings and may contain private information. Anyone with access to local storage may read them.</p><p>Use the controls below to reload, view older records or clear saved visits. Clearing requires confirmation.</p>");
    }
}

static void app_internal_history_template(AppHtmlWriter *writer, char *text,
        int status, int chinese)
{
    char *token;
    char *end;
    char *title_token;
    const char *legacy_title;
    int is_title;

    legacy_title = chinese ? "\344\274\232\350\257\235\350\256\260\345\275\225" : "Session history";
    for (;;) {
        token = strstr(text, "<!--APP_HISTORY_INTRO-->");
        title_token = strstr(text, legacy_title);
        is_title = title_token != NULL &&
                (token == NULL || title_token < token);
        if (is_title) token = title_token;
        if (token == NULL) break;
        end = token + (is_title ? strlen(legacy_title) :
                strlen("<!--APP_HISTORY_INTRO-->"));
        *token = '\0';
        app_html_append(writer, text);
        if (is_title) {
            app_html_escape(writer, status == 0 ?
                    (chinese ? "\344\274\232\350\257\235\350\256\260\345\275\225" : "Session history") :
                    (chinese ? "\350\256\277\351\227\256\345\216\206\345\217\262" : "Visit history"));
        } else {
            app_internal_history_intro(writer, status, chinese);
        }
        text = end;
    }
    app_html_append(writer, text);
}

/* Snapshot strings are fixed arrays. Reject missing terminators before any
 * strlen, escaping or URL owner call; malformed data is not an empty store. */
static int app_internal_visits_valid(const AppVisitSnapshot *visits)
{
    const AppVisitRecord *record;
    int i;

    if (visits == NULL || visits->count < 0 ||
            visits->count > APP_VISITS_PAGE_MAX) return 0;
    for (i = 0; i < visits->count; i++) {
        record = &visits->entries[i];
        if (record->id <= 0 || (i > 0 &&
                record->id >= visits->entries[i - 1].id) ||
                memchr(record->url, 0, sizeof(record->url)) == NULL ||
                memchr(record->title, 0, sizeof(record->title)) == NULL)
            return 0;
    }
    return 1;
}

static void app_internal_visit_time(AppHtmlWriter *writer, __int64 utc,
        int chinese)
{
    FILETIME file_time;
    SYSTEMTIME time;
    char text[48];
    __int64 ticks;

    ticks = 0;
    if (utc >= 0 && utc <= (__int64) 253402300799)
        ticks = (utc + (__int64) 11644473600) * 10000000;
    file_time.dwLowDateTime = (DWORD) ticks;
    file_time.dwHighDateTime = (DWORD) (((unsigned __int64) ticks) >> 32);
    if (!ticks || !FileTimeToSystemTime(&file_time, &time)) {
        app_html_escape(writer, chinese ? "UTC \346\227\266\351\227\264\344\270\215\345\217\257\347\224\250" :
                "UTC time unavailable");
        return;
    }
    _snprintf(text, sizeof(text) - 1, "%04u-%02u-%02u %02u:%02u:%02u UTC",
            (unsigned int) time.wYear, (unsigned int) time.wMonth,
            (unsigned int) time.wDay, (unsigned int) time.wHour,
            (unsigned int) time.wMinute, (unsigned int) time.wSecond);
    text[sizeof(text) - 1] = '\0';
    app_html_escape(writer, text);
}

/* One retained Core record-list component for both visit and download data.
 * Routing/actions remain typed; no server text is treated as markup. */
static void app_internal_record_field(AppHtmlWriter *writer, const char *id,
        const char *field)
{
    app_html_append(writer, "<div id=\"");
    app_html_escape(writer, id);
    app_html_append(writer, "-");
    app_html_escape(writer, field);
    app_html_append(writer, "\">");
}

static void app_internal_record_header(AppHtmlWriter *writer, const char *link_id,
        const char *href, const char *title, const char *url, __int64 utc, int chinese)
{
    app_html_append(writer, "<div class=\"history-entry\">");
    app_internal_record_field(writer, link_id, "title");
    if (href != NULL) {
        app_html_append(writer, "<a id=\"");
        app_html_escape(writer, link_id);
        app_html_append(writer, "\" href=\"");
        app_html_escape(writer, href);
        app_html_append(writer, "\">");
    }
    app_html_escape(writer, title[0] != 0 ? title : url);
    if (href != NULL) app_html_append(writer, "</a>");
    app_html_append(writer, "</div>");
    app_internal_record_field(writer, link_id, "url");
    app_html_escape(writer, url);
    app_html_append(writer, "</div>");
    app_internal_record_field(writer, link_id, "time");
    app_internal_visit_time(writer, utc, chinese);
    app_html_append(writer, "</div>");
}

static void app_internal_downloads(AppHtmlWriter *writer,
        const AppInternalPageData *data, int chinese)
{
    const AppDownloadRecord *record;
    WCHAR path[APP_DOWNLOAD_PATH_MAX];
    char detail[192];
    char link_id[32];
    const PHttpProbeResult *checked;
    int prepare_state;
    int i;

    app_html_append(writer, chinese ? "<p>\344\270\213\350\275\275\345\234\260\345\235\200</p>" : "<p>Download URL</p>");
    app_html_append(writer, "<p><input id=\"download-url\" size=\"18\" maxlength=\"1023\" "
            "style=\"font-size:inherit;line-height:normal;padding:2px 0.3em;width:100%;box-sizing:border-box\" value=\"");
    app_html_escape(writer, data->download_url);
    app_html_append(writer, "\"></p><p><a id=\"download-start\" href=\"#app-download-start\">");
    app_html_escape(writer, chinese ? "\344\270\213\350\275\275" : "Download");
    app_html_append(writer, "</a></p><p><a id=\"download-probe-cancel\" href=\"#app-download-probe-cancel\">");
    app_html_escape(writer, chinese ? "\345\217\226\346\266\210\350\247\243\346\236\220" : "Cancel preparation");
    app_html_append(writer, "</a></p><p id=\"download-prepare-status\">");
    prepare_state = AppDownloadPrepare_State(data->download_prepare);
    app_html_escape(writer, AppDownloadPrepare_StateText(prepare_state, chinese));
    if (AppDownloadPrepare_Error(data->download_prepare) != 0) {
        app_html_append(writer, " ");
        app_html_escape(writer, AppDownloadPrepare_ErrorText(AppDownloadPrepare_Error(data->download_prepare), chinese));
        _snprintf(detail, sizeof(detail) - 1, " (%d, HTTP %d)",
                AppDownloadPrepare_Error(data->download_prepare), AppDownloadPrepare_HttpStatus(data->download_prepare));
        detail[sizeof(detail) - 1] = 0;
        app_html_escape(writer, detail);
    }
    app_html_append(writer, "</p>");
    checked = AppDownloadPrepare_Result(data->download_prepare);
    if (checked != NULL) {
        app_html_append(writer, "<p class=\"history-entry\">");
        app_html_escape(writer, checked->metadata.final_url);
        app_html_append(writer, "</p><p>");
        app_html_escape(writer, checked->metadata.content_type);
        if (checked->metadata.total < 0)
            app_html_escape(writer, chinese ? " | \345\244\247\345\260\217\346\234\252\347\237\245" : " | Size unknown");
        else {
            _snprintf(detail, sizeof(detail) - 1, chinese ? " | %I64d \345\255\227\350\212\202" : " | %I64d bytes", checked->metadata.total);
            detail[sizeof(detail) - 1] = 0;
            app_html_escape(writer, detail);
        }
        app_html_append(writer, "</p><p>");
        app_html_escape(writer, chinese ? "\346\226\207\344\273\266\345\220\215" : "File name");
        app_html_append(writer, "</p><p><input id=\"download-name\" maxlength=\"255\" "
                "style=\"font-size:inherit;line-height:normal;padding:2px 0.3em;width:100%;box-sizing:border-box\" value=\"");
        app_html_escape(writer, AppDownloadPrepare_Name(data->download_prepare));
        app_html_append(writer, "\"></p>");
    }
    app_html_append(writer, "<p id=\"download-progress\">");
    app_html_escape(writer, data->download_message);
    app_html_append(writer, "</p><p><a id=\"download-cancel-0\" href=\"#app-download-cancel-0\">");
    app_html_escape(writer, chinese ? "\345\217\226\346\266\210\344\273\273\345\212\241 1" : "Cancel task 1");
    app_html_append(writer, "</a> | <a id=\"download-cancel-1\" href=\"#app-download-cancel-1\">");
    app_html_escape(writer, chinese ? "\345\217\226\346\266\210\344\273\273\345\212\241 2" : "Cancel task 2");
    app_html_append(writer, "</a></p><p><a id=\"downloads-latest\" href=\"#app-download-latest\">");
    app_html_escape(writer, chinese ? "\351\207\215\346\226\260\350\257\273\345\217\226\346\234\200\346\226\260\350\256\260\345\275\225" : "Reload newest records");
    app_html_append(writer, "</a> | <a id=\"downloads-clear\" href=\"#app-download-clear\">");
    app_html_escape(writer, chinese ? "\346\270\205\351\231\244\345\267\262\347\273\223\346\235\237\350\256\260\345\275\225\357\274\210\344\270\215\345\210\240\351\231\244\346\226\207\344\273\266\357\274\211" : "Clear finished records (keep files)");
    app_html_append(writer, "</a></p>");
    if (data->downloads_status == 1) {
        app_html_append(writer, chinese ? "<p>\346\255\243\345\234\250\350\257\273\345\217\226\344\270\213\350\275\275\350\256\260\345\275\225\342\200\246</p>" : "<p>Loading download records...</p>");
        return;
    }
    if (data->downloads_status != 2 || data->downloads == NULL ||
            data->downloads->count < 0 || data->downloads->count > APP_DOWNLOAD_PAGE_MAX) {
        app_html_append(writer, chinese ? "<p>\345\255\230\345\202\250\344\270\215\345\217\257\347\224\250\346\210\226\350\257\273\345\217\226\345\244\261\350\264\245\357\274\214\344\270\215\344\273\243\350\241\250\350\256\260\345\275\225\344\270\272\347\251\272\343\200\202</p>" :
                "<p>Storage unavailable or read failed; this does not mean the records are empty.</p>");
        return;
    }
    if (data->downloads->has_more) {
        app_html_append(writer, "<p><a id=\"downloads-older\" href=\"#app-download-older\">");
        app_html_escape(writer, chinese ? "\346\233\264\346\227\251\347\232\204\350\256\260\345\275\225" : "Older records");
        app_html_append(writer, "</a></p>");
    }
    if (data->downloads->count == 0)
        app_html_append(writer, chinese ? "<p>\346\234\254\351\241\265\346\262\241\346\234\211\344\270\213\350\275\275\350\256\260\345\275\225\343\200\202</p>" : "<p>No download records on this page.</p>");
    for (i = 0; i < data->downloads->count; ++i) {
        record = &data->downloads->entries[i];
        if (record->id <= 0 || record->state < 0 || record->state > 6 ||
                memchr(record->url, 0, sizeof(record->url)) == NULL ||
                memchr(record->path, 0, sizeof(record->path)) == NULL ||
                memchr(record->temporary, 0, sizeof(record->temporary)) == NULL) {
            writer->failed = 1;
            return;
        }
        _snprintf(link_id, sizeof(link_id), "download-%d", i);
        /* Source is display-only: do not reopen a download as a web page. */
        app_internal_record_header(writer, link_id, NULL, record->path,
                record->url, record->created_utc, chinese);
        if (record->total < 0)
            _snprintf(detail, sizeof(detail) - 1,
                    chinese ? " | %I64d \345\255\227\350\212\202\357\274\210\346\200\273\351\207\217\346\234\252\347\237\245\357\274\211" : " | %I64d bytes (total unknown)",
                    record->received);
        else _snprintf(detail, sizeof(detail) - 1,
                chinese ? " | %I64d / %I64d \345\255\227\350\212\202" : " | %I64d / %I64d bytes",
                record->received, record->total);
        detail[sizeof(detail) - 1] = 0;
        app_internal_record_field(writer, link_id, "status");
        app_html_escape(writer, AppDownloads_StateText(record->state, chinese));
        app_html_escape(writer, detail);
        if (record->state == APP_DOWNLOAD_FAILED && record->http_status != 0 &&
                (record->http_status < 200 || record->http_status >= 300)) {
            _snprintf(detail, sizeof(detail) - 1, " | HTTP %d", record->http_status);
            detail[sizeof(detail) - 1] = 0;
            app_html_escape(writer, detail);
            app_html_escape(writer, record->http_status == 404 ?
                    (chinese ? "\357\274\210\346\234\215\345\212\241\345\231\250\344\270\212\346\262\241\346\234\211\350\257\245\345\234\260\345\235\200\357\274\211" : " (URL not found on server)") :
                    (chinese ? "\357\274\210\346\234\215\345\212\241\345\231\250\346\234\252\350\277\224\345\233\236\346\210\220\345\212\237\345\223\215\345\272\224\357\274\211" : " (server did not return a successful response)"));
        } else if (record->error == PHTTP_PROBE_CHANGED) {
            app_html_escape(writer, chinese ? " | \345\223\215\345\272\224\344\277\241\346\201\257\345\267\262\346\224\271\345\217\230\357\274\214\350\257\267\351\207\215\346\226\260\350\247\243\346\236\220\351\223\276\346\216\245\343\200\202" :
                    " | Response metadata changed; resolve the link again.");
        } else if (record->error != 0) {
            _snprintf(detail, sizeof(detail) - 1,
                    chinese ? " | HTTP %d | \351\224\231\350\257\257 %d" : " | HTTP %d | error %d",
                    record->http_status, record->error);
            detail[sizeof(detail) - 1] = 0;
            app_html_escape(writer, detail);
        }
        if (record->state == APP_DOWNLOAD_COMPLETE) {
            if (!MultiByteToWideChar(CP_UTF8, 0, record->path, -1, path, APP_DOWNLOAD_PATH_MAX) ||
                    GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
                app_html_escape(writer, chinese ? " | \344\277\235\345\255\230\346\226\207\344\273\266\344\270\215\345\217\257\347\224\250" : " | Saved file unavailable");
        } else if (record->state == APP_DOWNLOAD_INTERRUPTED) {
            app_html_escape(writer, chinese ? " | \350\257\267\346\240\270\345\257\271\344\277\235\345\255\230\350\267\257\345\276\204\357\274\233\344\270\215\344\274\232\350\207\252\345\212\250\346\201\242\345\244\215\346\210\226\345\210\240\351\231\244\346\226\207\344\273\266" :
                    " | Check the saved path; no automatic resume or file deletion");
            if (MultiByteToWideChar(CP_UTF8, 0, record->path, -1, path, APP_DOWNLOAD_PATH_MAX) &&
                    GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
                app_html_escape(writer, chinese ? " | \346\234\200\347\273\210\350\267\257\345\276\204\345\255\230\345\234\250\346\226\207\344\273\266\357\274\214\344\275\206\344\270\213\350\275\275\347\273\210\346\200\201\346\234\252\347\241\256\350\256\244" :
                        " | Final path exists, but completion is unconfirmed");
            else app_html_escape(writer, chinese ? " | \346\234\200\347\273\210\346\226\207\344\273\266\344\270\215\345\217\257\347\224\250" : " | Final file unavailable");
            if (record->temporary[0] != 0) {
                app_html_append(writer, "</div>");
                app_internal_record_field(writer, link_id, "temporary");
                app_html_escape(writer, chinese ? "\344\270\264\346\227\266\350\267\257\345\276\204\357\274\232" : "Temporary path: ");
                app_html_escape(writer, record->temporary);
                if (MultiByteToWideChar(CP_UTF8, 0, record->temporary, -1, path, APP_DOWNLOAD_PATH_MAX) &&
                        GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
                    app_html_escape(writer, chinese ? " | \345\255\230\345\234\250" : " | Exists");
                else app_html_escape(writer, chinese ? " | \344\270\215\345\217\257\347\224\250" : " | Unavailable");
            }
        }
        app_html_append(writer, "</div>");
        if (record->state >= APP_DOWNLOAD_COMPLETE) {
            _snprintf(detail, sizeof(detail) - 1,
                    "<p><a id=\"download-retry-%d\" href=\"#app-download-retry-%I64d\">", i, record->id);
            detail[sizeof(detail) - 1] = 0;
            app_html_append(writer, detail);
            app_html_escape(writer, chinese ? "\351\207\215\346\226\260\344\270\213\350\275\275" : "Download again");
            _snprintf(detail, sizeof(detail) - 1,
                    "</a> | <a id=\"download-delete-%d\" href=\"#app-download-delete-%I64d\">", i, record->id);
            detail[sizeof(detail) - 1] = 0;
            app_html_append(writer, detail);
            app_html_escape(writer, chinese ? "\345\210\240\351\231\244\350\256\260\345\275\225" : "Delete record");
            app_html_append(writer, "</a></p>");
        }
        app_html_append(writer, "</div>");
    }
}

static void app_internal_visits(AppHtmlWriter *writer,
        const AppInternalPageData *data, int chinese)
{
    const AppVisitSnapshot *visits;
    const AppVisitRecord *record;
    const char *url;
    AppUrlSchemeKind scheme;
    char resolved[APP_VISITS_URL_MAX];
    char link_id[32];
    char action[128];
    int clickable;
    int count;
    int shown;
    int i;

    visits = data->visits;
    app_html_append(writer, chinese ?
            "<p><a id=\"visits-latest\" href=\"#app-visits-latest\">\351\207\215\346\226\260\345\212\240\350\275\275\346\234\200\346\226\260\350\256\260\345\275\225</a></p><p><a id=\"visits-clear\" href=\"#app-visits-clear\">\346\270\205\351\231\244\350\256\277\351\227\256\345\216\206\345\217\262</a></p>" :
            "<p><a id=\"visits-latest\" href=\"#app-visits-latest\">Reload newest</a></p><p><a id=\"visits-clear\" href=\"#app-visits-clear\">Clear visit history</a></p>");
    if (data->visits_status == 2 && app_internal_visits_valid(visits) &&
            visits->has_more) {
        app_html_append(writer, chinese ?
                "<p><a id=\"visits-older\" href=\"#app-visits-older\">\346\233\264\346\227\251\347\232\204\350\256\260\345\275\225</a></p>" :
                "<p><a id=\"visits-older\" href=\"#app-visits-older\">Older records</a></p>");
    }
    app_html_append(writer, chinese ?
                "<h2>\345\267\262\344\277\235\345\255\230\347\232\204\350\256\277\351\227\256\350\256\260\345\275\225</h2>" :
                "<h2>Saved visits</h2>");
    if (data->visits_status == 1) {
        app_html_append(writer, chinese ?
                "<p>\346\255\243\345\234\250\345\212\240\350\275\275\345\267\262\344\277\235\345\255\230\347\232\204\350\256\277\351\227\256\345\216\206\345\217\262\342\200\246\342\200\246</p>" :
                "<p>Loading saved visit history...</p>");
    } else if (data->visits_status != 2 ||
            !app_internal_visits_valid(visits)) {
        app_html_append(writer, chinese ?
                "<p>\350\256\277\351\227\256\350\256\260\345\275\225\345\255\230\345\202\250\344\270\215\345\217\257\347\224\250\346\210\226\350\257\273\345\217\226\345\244\261\350\264\245\343\200\202\350\277\231\344\270\215\350\241\250\347\244\272\345\267\262\344\277\235\345\255\230\347\232\204\345\216\206\345\217\262\344\270\272\347\251\272\343\200\202\350\257\267\347\224\250\342\200\234\351\207\215\346\226\260\345\212\240\350\275\275\346\234\200\346\226\260\350\256\260\345\275\225\342\200\235\351\207\215\350\257\225\343\200\202</p>" :
                "<p>Visit storage is unavailable or could not be read. This does not mean the saved history is empty. Use Reload newest to retry.</p>");
    } else {
        for (i = 0; i < visits->count; i++) {
            record = &visits->entries[i];
            scheme = AppUrlRouter_ClassifyScheme(record->url);
            clickable = (scheme == APP_URL_SCHEME_HTTP ||
                    scheme == APP_URL_SCHEME_HTTPS) &&
                    AppUrlRouter_ResolveNetworkReference(NULL, record->url,
                    resolved, sizeof(resolved)) == 0;
            _snprintf(link_id, sizeof(link_id) - 1, "visit-%d", i);
            link_id[sizeof(link_id) - 1] = '\0';
            app_internal_record_header(writer, link_id, clickable ? record->url : NULL,
                    record->title, record->url, record->visited_utc, chinese);
            _snprintf(action, sizeof(action) - 1,
                    "<p><a id=\"visit-delete-%d\" href=\"#app-visits-delete-%I64d\">",
                    i, record->id);
            action[sizeof(action) - 1] = 0;
            app_html_append(writer, action);
            app_html_append(writer, chinese ? "\345\210\240\351\231\244\350\256\260\345\275\225" : "Delete record");
            app_html_append(writer, "</a></p>");
            app_html_append(writer, "</div>");
        }
        if (visits->count == 0) {
            app_html_append(writer, chinese ?
                "<p>\346\234\254\351\241\265\346\262\241\346\234\211\345\267\262\344\277\235\345\255\230\347\232\204\350\256\277\351\227\256\350\256\260\345\275\225\343\200\202</p>" :
                "<p>No saved visits on this page.</p>");
        }
    }
    app_html_append(writer, chinese ?
                "<h2>\344\274\232\350\257\235\345\257\274\350\210\252\346\240\210</h2><p>\345\275\223\345\211\215\346\240\207\347\255\276\351\241\265\347\232\204\345\257\274\350\210\252\346\240\210\345\214\205\345\220\253\345\211\215\350\277\233\350\256\260\345\275\225\357\274\214\344\270\215\344\274\232\346\214\201\344\271\205\344\277\235\345\255\230\343\200\202\345\205\263\351\227\255\346\240\207\347\255\276\351\241\265\346\210\226\351\200\200\345\207\272\345\272\224\347\224\250\345\220\216\346\266\210\345\244\261\357\274\233\344\277\235\345\255\230\347\232\204\350\256\277\351\227\256\350\256\260\345\275\225\344\270\215\344\274\232\346\201\242\345\244\215\345\257\274\350\210\252\346\240\210\343\200\202</p>" :
                "<h2>Session navigation stack</h2><p>This current-tab stack includes forward entries and is not persistent. It disappears on tab close or application exit; saved visits do not restore it.</p>");
    count = data->history == NULL ? 0 : PBrowser_HistoryCount(data->history);
    if (count > PBROWSER_HISTORY_MAX) count = PBROWSER_HISTORY_MAX;
    shown = 0;
    for (i = count - 1; i >= 0; i--) {
        url = PBrowser_HistoryEntryUrl(data->history, i);
        if (url == NULL) continue;
        app_html_append(writer, "<p class=\"history-entry\">");
        app_html_escape(writer, url);
        app_html_append(writer, "</p>");
        shown++;
    }
    if (shown == 0) {
        app_html_append(writer, chinese ?
                "<p>\345\275\223\345\211\215\346\262\241\346\234\211\345\217\257\346\230\276\347\244\272\347\232\204\345\257\274\350\210\252\350\256\260\345\275\225\343\200\202</p>" :
                "<p>No navigation entries to display.</p>");
    }
}

int AppInternalPages_Build(int page_kind, const AppInternalPageData *data,
        char **out_bytes, unsigned int *out_length)
{
    char *template_bytes;
    char *marker;
    unsigned int template_length;
    AppHtmlWriter writer;
    int chinese;

    if (out_bytes == NULL || out_length == NULL) return 1;
    *out_bytes = NULL;
    *out_length = 0;
    if (data == NULL) return 1;
    template_bytes = NULL;
    template_length = 0;
    if (AppI18n_LoadPage(page_kind, &template_bytes, &template_length) != 0)
        return 1;
    marker = strstr(template_bytes, "<!--APP_CONTENT-->");
    if (marker == NULL) {
        *out_bytes = template_bytes;
        *out_length = template_length;
        return 0;
    }
    writer.bytes = (char *) malloc(APP_INTERNAL_HTML_MAX);
    writer.used = 0;
    writer.failed = writer.bytes == NULL;
    if (writer.failed) {
        AppI18n_FreePage(template_bytes);
        return 1;
    }
    writer.bytes[0] = '\0';
    *marker = '\0';
    chinese = AppI18n_CurrentLanguage() == APP_LANGUAGE_ZH_CN;
    if (page_kind == APP_I18N_PAGE_HISTORY)
        app_internal_history_template(&writer, template_bytes,
                data->visits_status, chinese);
    else app_html_append(&writer, template_bytes);
    if (page_kind == APP_I18N_PAGE_ABOUT) {
        app_internal_about(&writer, data, chinese);
    } else if (page_kind == APP_I18N_PAGE_HISTORY) {
        if (data->visits_status == 0)
            app_internal_history(&writer, data->history, chinese);
        else app_internal_visits(&writer, data, chinese);
    } else if (page_kind == APP_I18N_PAGE_DOWNLOADS) {
        app_internal_downloads(&writer, data, chinese);
    } else if (page_kind == APP_I18N_PAGE_SETTINGS) {
        app_html_row(&writer, chinese ? "\347\225\214\351\235\242\350\257\255\350\250\200" : "UI language",
                chinese ? "\347\256\200\344\275\223\344\270\255\346\226\207 (zh-CN)" : "English (en-US)");
        app_html_row(&writer, "JavaScript", chinese ?
                "\347\275\221\351\241\265\347\255\226\347\225\245\347\224\250\344\272\216\345\220\216\347\273\255\345\257\274\350\210\252\357\274\233\350\256\276\347\275\256\351\241\265\344\273\215\346\211\247\350\241\214\345\206\205\345\265\214\350\204\232\346\234\254\343\200\202" :
                "Web policy applies to later navigations; Settings always permits its embedded code.");
    }
    if (page_kind == APP_I18N_PAGE_HISTORY)
        app_internal_history_template(&writer,
                marker + strlen("<!--APP_CONTENT-->"),
                data->visits_status, chinese);
    else app_html_append(&writer, marker + strlen("<!--APP_CONTENT-->"));
    AppI18n_FreePage(template_bytes);
    if (writer.failed) {
        free(writer.bytes);
        return 1;
    }
    *out_bytes = writer.bytes;
    *out_length = writer.used;
    return 0;
}

int AppInternalPages_FocusIds(HANDLE document,
        char ids[APP_INTERNAL_FOCUS_MAX][128])
{
    static const char *navigation_ids[] = {
        "nav-version", "nav-system", "nav-newtab", "nav-about", "nav-history",
        "nav-downloads", "nav-settings", "nav-welcome", "nav-controls",
        "visits-latest", "visits-older", "visits-clear",
        "download-start", "download-probe-cancel",
        "download-cancel-0", "download-cancel-1",
        "downloads-latest", "downloads-older", "downloads-clear"
    };
    PCoreFocusTargetInfo info;
    char candidate[32];
    unsigned int i;
    int history_index;
    int count;

    count = 0;
    /* Sixteen rows have two actions plus seven commands and seven directory links (46).
     * Keep a fixed 48-entry budget shared with the EXE host. */
    for (history_index = 0; history_index < APP_DOWNLOAD_PAGE_MAX; ++history_index) {
        for (i = 0; i < 2 && count < APP_INTERNAL_FOCUS_MAX; ++i) {
            _snprintf(candidate, sizeof(candidate) - 1,
                    i == 0 ? "download-retry-%d" : "download-delete-%d", history_index);
            candidate[sizeof(candidate) - 1] = 0;
            if (PCore_FocusTargetInfoById(document, candidate, &info) == 0 &&
                    info.kind == PCORE_FOCUS_TARGET_LINK) strcpy(ids[count++], candidate);
        }
    }
    for (history_index = 0; history_index < APP_VISITS_PAGE_MAX &&
            count < APP_INTERNAL_FOCUS_MAX; history_index++) {
        _snprintf(candidate, sizeof(candidate) - 1, "visit-%d", history_index);
        candidate[sizeof(candidate) - 1] = '\0';
        if (PCore_FocusTargetInfoById(document, candidate, &info) == 0 &&
                info.kind == PCORE_FOCUS_TARGET_LINK)
            strcpy(ids[count++], candidate);
        _snprintf(candidate, sizeof(candidate) - 1, "visit-delete-%d", history_index);
        candidate[sizeof(candidate) - 1] = 0;
        if (count < APP_INTERNAL_FOCUS_MAX &&
                PCore_FocusTargetInfoById(document, candidate, &info) == 0 &&
                info.kind == PCORE_FOCUS_TARGET_LINK) strcpy(ids[count++], candidate);
    }
    for (history_index = PBROWSER_HISTORY_MAX - 1;
            history_index >= 0 && count < APP_INTERNAL_FOCUS_MAX;
            history_index--) {
        _snprintf(candidate, sizeof(candidate) - 1, "history-%d", history_index);
        candidate[sizeof(candidate) - 1] = '\0';
        if (PCore_FocusTargetInfoById(document, candidate, &info) == 0 &&
                info.kind == PCORE_FOCUS_TARGET_LINK) strcpy(ids[count++],
                candidate);
    }
    for (i = 0; i < sizeof(navigation_ids) / sizeof(navigation_ids[0]) &&
            count < APP_INTERNAL_FOCUS_MAX; i++) {
        if (PCore_FocusTargetInfoById(document, navigation_ids[i],
                &info) == 0 && info.kind == PCORE_FOCUS_TARGET_LINK) {
            strcpy(ids[count++], navigation_ids[i]);
        }
    }
    return count;
}

#ifdef _DEBUG
/* EXE-private checks use isolated Browser handles, never the live page stack.
 * The entire fixture and its diagnostic text are absent from Release. */
static int app_internal_check_translations(void)
{
    static const UINT resource_ids[] = {
        IDR_APP_ABOUT_EN, IDR_APP_ABOUT_ZH,
        IDR_APP_NEWTAB_EN, IDR_APP_NEWTAB_ZH,
        IDR_APP_HISTORY_EN, IDR_APP_HISTORY_ZH,
        IDR_APP_DOWNLOADS_EN, IDR_APP_DOWNLOADS_ZH,
        IDR_APP_SETTINGS_EN, IDR_APP_SETTINGS_ZH
    };
    HRSRC resource;
    HGLOBAL loaded;
    HINSTANCE instance;
    HANDLE document;
    const char *bytes;
    DWORD length;
    unsigned int i;
    char title[128];

    instance = GetModuleHandleW(NULL);
    for (i = 0; i < sizeof(resource_ids) / sizeof(resource_ids[0]); i++) {
        resource = FindResource(instance, MAKEINTRESOURCE(resource_ids[i]),
                RT_RCDATA);
        if (resource == NULL) return 1;
        length = SizeofResource(instance, resource);
        loaded = LoadResource(instance, resource);
        bytes = loaded == NULL ? NULL : (const char *) LockResource(loaded);
        if (bytes == NULL || length < 3 || length >= APP_INTERNAL_HTML_MAX ||
                ((unsigned char) bytes[0] == 0xef &&
                (unsigned char) bytes[1] == 0xbb &&
                (unsigned char) bytes[2] == 0xbf)) return 1;
        document = PCore_ParseHTML(bytes, (unsigned int) length);
        if (document == NULL) return 1;
        title[0] = '\0';
        (void) PCore_DocumentTitle(document, title, sizeof(title), NULL);
        PCore_FreeDocument(document);
        if (title[0] == '\0' || ((i & 1U) &&
                (unsigned char) title[0] < 0x80)) return 1;
    }
    return 0;
}

static int app_internal_system_debug_check(void)
{
    static const WCHAR unterminated[] = { L'x', L'y' };
    static const WCHAR bad_high[] = { 0xd800, L'x', 0 };
    static const WCHAR bad_low[] = { 0xdc00, 0 };
    static const WCHAR embedded_null[] = L".5.3.0\0junk";
    char text[416];
    OSVERSIONINFO version;
    unsigned char block[16];
    AppHtmlWriter writer;
    int result;

    if (app_internal_registry_text(REG_SZ, L".5.3.0", sizeof(L".5.3.0"),
            text, sizeof(text)) || strcmp(text, ".5.3.0") ||
            app_internal_registry_text(REG_SZ, L"Windows Embedded Handheld",
            sizeof(L"Windows Embedded Handheld"), text, sizeof(text)) ||
            strcmp(text, "Windows Embedded Handheld") ||
            app_internal_registry_text(REG_SZ, L"\x4e2d\x6587", sizeof(L"\x4e2d\x6587"),
            text, sizeof(text)) || strcmp(text, "\344\270\255\346\226\207")) return 1;
    if (!app_internal_registry_text(REG_DWORD, L".5.3.0", sizeof(L".5.3.0"), text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, embedded_null, sizeof(embedded_null), text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, L".5.3.0", sizeof(L".5.3.0") - 1, text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, L".5.3.0", sizeof(L".5.3.0") - 2, text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, unterminated, sizeof(unterminated), text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, bad_high, sizeof(bad_high), text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, L"", sizeof(L""), text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, L"x\ny", sizeof(L"x\ny"), text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, L".5.3.0", sizeof(L".5.3.0"), text, 6) || text[0] != '\0' ||
            !app_internal_registry_text(REG_SZ, NULL, 4, text, sizeof(text)) ||
            !app_internal_registry_text(REG_SZ, L"x", 258, text, sizeof(text)) ||
            !strcmp(app_internal_system_status(APP_SYSTEM_MISSING, 0),
            app_internal_system_status(APP_SYSTEM_FAILED, 0)) ||
            !strcmp(app_internal_system_status(APP_SYSTEM_UNSUPPORTED, 1),
            app_internal_system_status(APP_SYSTEM_INVALID, 1))) return 1;
    if (app_internal_system_text(L"PocketPC", 9, text, sizeof(text)) ||
            strcmp(text, "PocketPC") ||
            app_internal_system_text(L"smartphone", 11, text, sizeof(text)) ||
            strcmp(text, "smartphone") ||
            app_internal_system_text(L"Windows Embedded Handheld", 26,
            text, sizeof(text)) || strcmp(text, "Windows Embedded Handheld") ||
            app_internal_system_text(L"Custom CE", 10, text, sizeof(text)) ||
            strcmp(text, "Custom CE") ||
            app_internal_system_text(L"\x4e2d\x6587", 3, text, sizeof(text)) ||
            strcmp(text, "\344\270\255\346\226\207") ||
            app_internal_system_text(L"x", 2, text, 2) || strcmp(text, "x")) return 1;
    if (!app_internal_system_text(L"", 1, text, sizeof(text)) ||
            !app_internal_system_text(unterminated, 2, text, sizeof(text)) ||
            !app_internal_system_text(bad_high, 3, text, sizeof(text)) ||
            !app_internal_system_text(bad_low, 2, text, sizeof(text)) ||
            !app_internal_system_text(L"x\ny", 4, text, sizeof(text)) ||
            !app_internal_system_text(L"x", 2, text, 1) ||
            !app_internal_system_text(L"PocketPC", 9, text, 8) || text[0] != '\0' ||
            !app_internal_system_text(NULL, 2, text, sizeof(text)) ||
            !app_internal_system_text(L"x", 0, text, sizeof(text)) ||
            !app_internal_system_text(L"x", 2, NULL, 2) ||
            !app_internal_system_text(L"x", 2, text, 0)) return 1;
    memset(&version, 0, sizeof(version));
    version.dwPlatformId = VER_PLATFORM_WIN32_CE;
    version.dwMajorVersion = 5;
    version.dwMinorVersion = 2;
    version.dwBuildNumber = 318;
    if (app_internal_kernel_text(&version, text, sizeof(text)) ||
            strcmp(text, "Windows CE 5.2.318")) return 1;
    version.dwPlatformId = VER_PLATFORM_WIN32_NT;
    if (app_internal_kernel_text(&version, text, sizeof(text)) ||
            strcmp(text, "Windows NT 5.2.318")) return 1;
    version.dwPlatformId = 99;
    if (app_internal_kernel_text(&version, text, sizeof(text)) ||
            strcmp(text, "Platform ID 99: 5.2.318") ||
            !app_internal_kernel_text(&version, text, 4) || text[0] != '\0' ||
            !app_internal_kernel_text(NULL, text, sizeof(text)) ||
            !app_internal_kernel_text(&version, NULL, 1) ||
            !app_internal_kernel_text(&version, text, 0)) return 1;
    if (!app_internal_resource_slice(block, sizeof(block), block, sizeof(block)) ||
            !app_internal_resource_slice(block, sizeof(block), block + 8, 8) ||
            app_internal_resource_slice(block, sizeof(block), block + 8, 9) ||
            app_internal_resource_slice(block, sizeof(block), block + 16, 1) ||
            app_internal_resource_slice(NULL, 0, block, 1) ||
            app_internal_resource_slice(block, sizeof(block), NULL, 1) ||
            !app_internal_component_version(NULL, text, sizeof(text), 0) ||
            !app_internal_component_version(L"\\Windows\\coredll.dll", NULL, 1, 0) ||
            !app_internal_component_version(L"\\Windows\\coredll.dll", text, 0, 0)) return 1;
    writer.bytes = (char *) malloc(APP_INTERNAL_HTML_MAX);
    if (writer.bytes == NULL) return 1;
    writer.used = 0;
    writer.failed = 0;
    writer.bytes[0] = '\0';
    app_internal_system(&writer, 0);
    result = writer.failed || strstr(writer.bytes, "Kernel: ") == NULL ||
            strstr(writer.bytes, "Platform type: ") == NULL ||
            strstr(writer.bytes, "Update package (AKU): ") == NULL ||
            strstr(writer.bytes, "OS product: ") == NULL ||
            strstr(writer.bytes, "OS release: ") == NULL ||
            strstr(writer.bytes, "(CE)") != NULL || strstr(writer.bytes, "(WM)") != NULL;
    writer.used = 0;
    writer.bytes[0] = '\0';
    app_internal_system(&writer, 1);
    if (writer.failed || strstr(writer.bytes, "\345\206\205\346\240\270: ") == NULL ||
            strstr(writer.bytes, "\345\271\263\345\217\260\347\261\273\345\236\213: ") == NULL ||
            strstr(writer.bytes, "\346\233\264\346\226\260\345\214\205 (AKU): ") == NULL ||
            strstr(writer.bytes, "(CE)") != NULL || strstr(writer.bytes, "(WM)") != NULL) result = 1;
    writer.used = 0;
    writer.bytes[0] = '\0';
    app_html_row(&writer, "Device / OEM", "OEM <&\"'>");
    if (writer.failed || strstr(writer.bytes,
            "OEM &lt;&amp;&quot;&#39;&gt;") == NULL) result = 1;
    free(writer.bytes);
    if (!result) AppDebug_Log("positron system-info selftest OK\r\n");
    return result;
}

/* Exercise the actual generated page, not a second HTML/CSS implementation.
 * Reflow must preserve both the displayed DOM text and the navigation URL. */
static int app_internal_wrap_debug_check(HANDLE document, HANDLE stylesheet,
        const char *id, const char *url, const char *title)
{
    static const int widths[] = { 240, 480, 240 };
    static const int dpis[] = { 96, 128, 192 };
    PCoreFocusTargetInfo focus;
    char text[APP_VISITS_URL_MAX];
    unsigned int i;
    unsigned int j;
    int result;

    result = 1;
    for (i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        for (j = 0; j < sizeof(widths) / sizeof(widths[0]); j++) {
            PCore_SetDeviceViewport(widths[j], 268, dpis[i]);
            if (PCore_StyleDocument(document, stylesheet) != 0 ||
                    PCore_LayoutDocument(document, widths[j], 268) != 0 ||
                    PCore_DocumentWidth(document) > widths[j] ||
                    PCore_FocusTargetInfoById(document, id, &focus) != 0 ||
                    focus.kind != PCORE_FOCUS_TARGET_LINK ||
                    PCore_NodeAttributeById(document, id, "href", text,
                    sizeof(text), NULL) != 0 || strcmp(text, url) ||
                    PCore_NodeTextContentById(document, id, text,
                    sizeof(text), NULL) != 0 || strcmp(text, title)) goto done;
        }
    }
    result = 0;
done:
    PCore_SetDeviceViewport(240, 268, 96);
    return result;
}

/* Real download records must have distinct natural block geometry, even
 * when a source URL wraps or ends exactly at the viewport's line edge. */
static int app_internal_records_debug_check(HANDLE stylesheet)
{
    static const char *ids[] = { "download-0-title", "download-0-url",
        "download-0-time", "download-0-status", "download-1-title",
        "download-1-url", "download-1-time", "download-1-status" };
    static const int widths[] = { 240, 480, 240 };
    static const int dpis[] = { 96, 128, 192 };
    AppInternalPageData data;
    AppDownloadSnapshot *downloads;
    HANDLE document;
    char *html;
    unsigned int length;
    unsigned int dpi;
    unsigned int width;
    unsigned int field;
    int x;
    int y;
    int w;
    int h;
    int bottom;
    int stage;
    int result;
    char text[APP_DOWNLOAD_URL_MAX];
    char message[160];
    char focus_ids[APP_INTERNAL_FOCUS_MAX][128];

    result = 1;
    document = NULL;
    html = NULL;
    dpi = width = field = 0;
    stage = 0;
    text[0] = 0;
    downloads = (AppDownloadSnapshot *) calloc(1, sizeof(*downloads));
    if (downloads == NULL) goto done;
    downloads->count = 2;
    downloads->entries[0].id = 2;
    downloads->entries[1].id = 1;
    for (field = 0; field < 2; ++field) {
        strcpy(downloads->entries[field].path, "\\My Documents\\download.bin");
        downloads->entries[field].created_utc = 1700000000;
        downloads->entries[field].state = APP_DOWNLOAD_FAILED;
        downloads->entries[field].http_status = 404;
        downloads->entries[field].error = -8;
        downloads->entries[field].total = field == 0 ? 6330 : -1;
    }
    strcpy(downloads->entries[0].url, "https://winworldpc.com/index.html");
    strcpy(downloads->entries[1].url, "http://example.com/index.html");
    memset(&data, 0, sizeof(data));
    data.dpi = 96;
    data.viewport_width = 240;
    data.viewport_height = 268;
    data.downloads = downloads;
    data.downloads_status = 2;
    if (AppInternalPages_Build(APP_I18N_PAGE_DOWNLOADS, &data, &html, &length) != 0 ||
            strstr(html, " | HTTP 404") == NULL || strstr(html, "error -8") != NULL)
        goto done;
    document = PCore_ParseHTML(html, length);
    if (document == NULL) goto done;
    for (dpi = 0; dpi < sizeof(dpis) / sizeof(dpis[0]); ++dpi) {
        for (width = 0; width < sizeof(widths) / sizeof(widths[0]); ++width) {
            stage = 1;
            PCore_SetDeviceViewport(widths[width], 268, dpis[dpi]);
            if (PCore_StyleDocument(document, stylesheet) != 0 ||
                    PCore_LayoutDocument(document, widths[width], 268) != 0) goto done;
            stage = 2;
            if (PCore_DocumentWidth(document) > widths[width]) {
                if (PCore_FormControlInfoById(document, "download-url", &x, &y,
                        &w, &h, NULL, NULL, NULL) == 0) {
                    _snprintf(message, sizeof(message) - 1,
                            "positron record-fields control x=%d y=%d w=%d h=%d\r\n",
                            x, y, w, h);
                    message[sizeof(message) - 1] = 0;
                    AppDebug_Log(message);
                }
                goto done;
            }
            stage = 3;
            bottom = 0;
            for (field = 0; field < sizeof(ids) / sizeof(ids[0]); ++field) {
                if (PCore_FragmentInfoById(document, ids[field], &x, &y, &w, &h) != 0 ||
                        w <= 0 || h <= 0 || y < bottom) goto done;
                bottom = y + h;
            }
            stage = 4;
            if (PCore_NodeTextContentById(document, "download-0-url", text,
                    sizeof(text), NULL) != 0 || strcmp(text, downloads->entries[0].url) ||
                    PCore_NodeTextContentById(document, "download-1-url", text,
                    sizeof(text), NULL) != 0 || strcmp(text, downloads->entries[1].url) ||
                    PCore_NodeTextContentById(document, "download-0-time", text,
                    sizeof(text), NULL) != 0 || strcmp(text, "2023-11-14 22:13:20 UTC"))
                goto done;
        }
    }
    /* Full page: every action remains keyboard reachable, including row 16. */
    PCore_FreeDocument(document);
    document = NULL;
    AppI18n_FreePage(html);
    html = NULL;
    for (stage = 2; stage < APP_DOWNLOAD_PAGE_MAX; ++stage)
        downloads->entries[stage] = downloads->entries[0];
    for (stage = 0; stage < APP_DOWNLOAD_PAGE_MAX; ++stage) downloads->entries[stage].id = stage + 1;
    downloads->count = APP_DOWNLOAD_PAGE_MAX;
    downloads->has_more = 1;
    PCore_SetDeviceViewport(240, 268, 96);
    if (AppInternalPages_Build(APP_I18N_PAGE_DOWNLOADS, &data, &html, &length) != 0) goto done;
    document = PCore_ParseHTML(html, length);
    if (document == NULL || PCore_StyleDocument(document, stylesheet) != 0 ||
            PCore_LayoutDocument(document, 240, 268) != 0 ||
            AppInternalPages_FocusIds(document, focus_ids) != 46 ||
            strcmp(focus_ids[31], "download-delete-15")) goto done;
    AppDebug_Log("download-record-focus selftest OK terminal_rows=16 actions=32 total=46\r\n");
    result = 0;
done:
    if (result != 0 && document != NULL) {
        _snprintf(message, sizeof(message) - 1,
                "positron record-fields detail stage=%d extent=%d text=%s\r\n",
                stage, PCore_DocumentWidth(document), text);
        message[sizeof(message) - 1] = 0;
        AppDebug_Log(message);
    }
    _snprintf(message, sizeof(message) - 1,
            "positron record-fields selftest %s dpi-index=%u width-index=%u field=%u\r\n",
            result == 0 ? "OK" : "FAILED", dpi, width, field);
    message[sizeof(message) - 1] = 0;
    AppDebug_Log(message);
    if (document != NULL) PCore_FreeDocument(document);
    AppI18n_FreePage(html);
    free(downloads);
    PCore_SetDeviceViewport(240, 268, 96);
    return result;
}

int AppInternalPages_DebugCheck(const char *css)
{
    static const char *valid[] = {
        "POSITRON://NEWTAB/", "positron://about", "positron://history/",
        "positron://downloads", "positron://settings", "positron://welcome",
        "positron://controls?mode=1", "positron://version/", "positron://system"
    };
    static const char *invalid[] = {
        "positron://", "positron://unknown", "positron://newtab//",
        "positron://about?x=1", "positron://about#other",
        "positron://version#version", "positron://quit?x=1",
        "positron://quit#x", "positron://about/path"
    };
    AppInternalRoute route;
    AppInternalPageData data;
    HANDLE history;
    HANDLE document;
    HANDLE stylesheet;
    char *html;
    unsigned int length;
    unsigned int i;
    int source;
    int page;
    int result;
    int phase;
    char url[128];
    char title[128];
    const char *first;
    const char *last;
    AppHtmlWriter writer;
    char budget_text[8];
    char focus_ids[APP_INTERNAL_FOCUS_MAX][128];
    AppVisitSnapshot *visits;

    result = 1;
    phase = 1;
    history = NULL;
    document = NULL;
    stylesheet = NULL;
    html = NULL;
    visits = NULL;
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        if (AppInternalPages_Resolve(valid[i], APP_NAV_SOURCE_DOCUMENT,
                &route) != 0 || route.kind == APP_INTERNAL_COMMAND) goto done;
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        if (AppInternalPages_Resolve(invalid[i], APP_NAV_SOURCE_ADDRESS,
                &route) == 0 || route.url[0] != '\0') goto done;
    }
    for (source = APP_NAV_SOURCE_DOCUMENT; source <= APP_NAV_SOURCE_REDIRECT;
            source++) {
        if ((AppInternalPages_Resolve("POSITRON://QUIT/",
                (AppNavigationSource) source, &route) == 0) !=
                (source == APP_NAV_SOURCE_ADDRESS)) goto done;
    }
    phase = 2;
    if (AppVersion_DebugCheck() != 0) goto done;
    if (app_internal_system_debug_check() != 0) goto done;
    if (app_internal_check_translations() != 0) goto done;
    memset(&data, 0, sizeof(data));
    data.dpi = 96;
    data.viewport_width = 240;
    data.viewport_height = 268;
    history = PBrowser_HistoryCreate();
    if (history == NULL) goto done;
    data.history = history;
    PCore_SetDeviceViewport(240, 268, 96);
    stylesheet = PCore_ParseCSS(css, 0, "https://positron.invalid/app.css");
    if (stylesheet == NULL) goto done;
    if (app_internal_records_debug_check(stylesheet) != 0) goto done;
    for (page = APP_I18N_PAGE_WELCOME; page <= APP_I18N_PAGE_SETTINGS;
            page++) {
        if (AppInternalPages_Build(page, &data, &html, &length) != 0 ||
                length == 0 || length >= APP_INTERNAL_HTML_MAX ||
                strstr(html, "<!--APP_CONTENT-->") != NULL) goto done;
        document = PCore_ParseHTML(html, length);
        AppI18n_FreePage(html);
        html = NULL;
        if (document == NULL || PCore_DocumentTitle(document, title,
                sizeof(title), NULL) != 0 || title[0] == '\0') goto done;
        if (page > APP_I18N_PAGE_CONTROLS &&
                (PCore_StyleDocument(document, stylesheet) != 0 ||
                PCore_LayoutDocument(document, 240, 268) != 0 ||
                AppInternalPages_FocusIds(document, focus_ids) < 7)) goto done;
        PCore_FreeDocument(document);
        document = NULL;
    }
    phase = 3;
    if (AppInternalPages_Resolve("positron://version", APP_NAV_SOURCE_ADDRESS,
            &route) != 0 || strcmp(route.url, "positron://about#version") ||
            PBrowser_HistoryCommitNavigation(history, route.url,
            PBROWSER_HISTORY_METHOD_GET, PBROWSER_HISTORY_TARGET_NEW) != 0 ||
            PBrowser_HistoryCount(history) != 1) goto done;
    PBrowser_HistoryReset(history);
    for (i = 0; i < PBROWSER_HISTORY_MAX; i++) {
        _snprintf(url, sizeof(url) - 1,
                "https://example.com/abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz%u?x=\"<&'", i);
        url[sizeof(url) - 1] = '\0';
        if (PBrowser_HistoryCommitNavigation(history, url,
                PBROWSER_HISTORY_METHOD_GET,
                PBROWSER_HISTORY_TARGET_NEW) != 0) goto done;
    }
    if (PBrowser_HistoryCommitTarget(history, 0) != 0 ||
            AppInternalPages_Build(APP_I18N_PAGE_HISTORY, &data, &html,
            &length) != 0) goto done;
    first = strstr(html, "id=\"history-15\"");
    last = strstr(html, "id=\"history-0\"");
    if (first == NULL || last == NULL || first >= last ||
            strstr(html, "?x=&quot;&lt;&amp;&#39;") == NULL ||
            PBrowser_HistoryIndex(history) != 0 ||
            PBrowser_HistoryCount(history) != PBROWSER_HISTORY_MAX) goto done;
    document = PCore_ParseHTML(html, length);
    if (document == NULL || PCore_StyleDocument(document, stylesheet) != 0 ||
            PCore_LayoutDocument(document, 240, 268) != 0 ||
            AppInternalPages_FocusIds(document, focus_ids) != 23 ||
            strcmp(focus_ids[0], "history-15") != 0) goto done;
    if (app_internal_wrap_debug_check(document, stylesheet, "history-15",
            PBrowser_HistoryEntryUrl(history, 15),
            PBrowser_HistoryEntryUrl(history, 15)) != 0) goto done;
    PCore_FreeDocument(document);
    document = NULL;
    AppI18n_FreePage(html);
    html = NULL;
    if (PBrowser_HistoryCommitNavigation(history, "positron://history",
            PBROWSER_HISTORY_METHOD_GET, PBROWSER_HISTORY_TARGET_NEW) != 0 ||
            AppInternalPages_Build(APP_I18N_PAGE_HISTORY, &data, &html,
            &length) != 0 || strstr(html, "id=\"history-1\"") != NULL) goto done;
    AppI18n_FreePage(html);
    html = NULL;
    visits = (AppVisitSnapshot *) calloc(1, sizeof(*visits));
    if (visits == NULL) goto done;
    visits->count = APP_VISITS_PAGE_MAX;
    visits->has_more = 1;
    for (i = 0; i < APP_VISITS_PAGE_MAX; ++i) {
        visits->entries[i].id = 20 - i;
        visits->entries[i].visited_utc = 1700000000;
        strcpy(visits->entries[i].url, "https://example.com/?x=\"<&'");
        strcpy(visits->entries[i].title, "Title <&>");
    }
    strcpy(visits->entries[0].url,
            "https://www.iana.org/help/example-domains?query=abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    strcpy(visits->entries[0].title,
            "LongTitleabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    data.visits = visits;
    data.visits_status = 2;
    if (AppInternalPages_Build(APP_I18N_PAGE_HISTORY, &data, &html,
            &length) != 0 || strstr(html, "Title &lt;&amp;&gt;") == NULL ||
            strstr(html, "2023-11-14 22:13:20 UTC") == NULL ||
            strstr(html, "id=\"history-") != NULL) goto done;
    document = PCore_ParseHTML(html, length);
    if (document == NULL || PCore_StyleDocument(document, stylesheet) != 0 ||
            PCore_LayoutDocument(document, 240, 268) != 0 ||
            AppInternalPages_FocusIds(document, focus_ids) != 42 ||
            strcmp(focus_ids[0], "visit-0") != 0) goto done;
    if (app_internal_wrap_debug_check(document, stylesheet, "visit-0",
            visits->entries[0].url, visits->entries[0].title) != 0) goto done;
    AppDebug_Log("positron history-wrap selftest OK dpi=96,128,192 widths=240,480,240 text=exact href=exact\r\n");
    AppDebug_Log("positron visits-page selftest OK escaped=1 focus=42 utc=1\r\n");
    PCore_FreeDocument(document);
    document = NULL;
    AppI18n_FreePage(html);
    html = NULL;
    /* The current entry is index 1 after truncating the forward stack. */
    phase = 4;
    writer.bytes = (char *) malloc(APP_INTERNAL_HTML_MAX);
    if (writer.bytes == NULL) goto done;
    writer.used = APP_INTERNAL_HTML_MAX - 2;
    writer.failed = 0;
    strcpy(budget_text, "too big");
    app_html_append(&writer, budget_text);
    free(writer.bytes);
    if (!writer.failed) goto done;
    result = 0;
done:
    AppI18n_FreePage(html);
    free(visits);
    if (document != NULL) PCore_FreeDocument(document);
    if (stylesheet != NULL) PCore_FreeStylesheet(stylesheet);
    if (history != NULL) PBrowser_HistoryDestroy(history);
    _snprintf(url, sizeof(url) - 1,
            "positron internal-pages selftest %s phase=%d\r\n",
            result == 0 ? "OK" : "FAILED", phase);
    url[sizeof(url) - 1] = '\0';
    AppDebug_Log(url);
    return result;
}
#endif
