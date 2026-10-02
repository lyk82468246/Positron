/* Application policy and presentation only. Core renders the resulting HTML;
 * Browser remains the owner of the navigation stack. No I/O or script session. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "app_internal_pages.h"
#include "positron_browser.h"
#include "positron_core.h"
#include "app_debug.h"
#include "app_version.h"
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

static int app_internal_os_text(const WCHAR *platform, unsigned int count,
        char *output, unsigned int capacity)
{
    char type[384];
    int written;

    if (output == NULL || capacity == 0) return 1;
    output[0] = '\0';
    if (app_internal_system_text(platform, count, type, sizeof(type))) return 1;
    /* Report the WM family on the recognised mobile platforms. PocketPC
     * alone does not distinguish Classic/Professional, or WM 6.0/6.1/6.5.
     * Unknown CE platforms retain their actual name, never a WM6 SDK label. */
    if (_wcsicmp(platform, L"PocketPC") == 0 ||
            _wcsicmp(platform, L"Smartphone") == 0) {
        written = _snprintf(output, capacity, "Windows Mobile (%s)", type);
    } else {
        written = _snprintf(output, capacity, "%s", type);
    }
    if (written < 0 || (unsigned int) written >= capacity) {
        output[0] = '\0';
        return 1;
    }
    return 0;
}

static void app_internal_system(AppHtmlWriter *writer, int chinese)
{
    OSVERSIONINFO version;
    WCHAR platform[128];
    WCHAR oem[128];
    char kernel[96];
    char os[416];
    char device[384];
    const char *unavailable;
#ifdef _DEBUG
    char message[1024];
#endif

    unavailable = chinese ? "\344\270\215\345\217\257\347\224\250" : "Unavailable";
    strcpy(kernel, unavailable);
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    if (GetVersionEx(&version)) {
        _snprintf(kernel, sizeof(kernel) - 1, "Windows CE %lu.%lu (%lu)",
                version.dwMajorVersion, version.dwMinorVersion,
                version.dwBuildNumber);
        kernel[sizeof(kernel) - 1] = '\0';
    }
    memset(platform, 0xff, sizeof(platform));
    if (!SystemParametersInfo(SPI_GETPLATFORMTYPE,
            sizeof(platform) / sizeof(platform[0]), platform, 0) ||
            app_internal_os_text(platform,
            sizeof(platform) / sizeof(platform[0]), os, sizeof(os))) {
        strcpy(os, unavailable);
    }
    memset(oem, 0xff, sizeof(oem));
    if (!SystemParametersInfo(SPI_GETOEMINFO,
            sizeof(oem) / sizeof(oem[0]), oem, 0) ||
            app_internal_system_text(oem, sizeof(oem) / sizeof(oem[0]),
            device, sizeof(device))) {
        strcpy(device, unavailable);
    }
    app_html_row(writer, chinese ? "\345\206\205\346\240\270 (CE)" : "Kernel (CE)", kernel);
    app_html_row(writer, chinese ? "\346\223\215\344\275\234\347\263\273\347\273\237 (WM)" :
            "Operating system (WM)", os);
    app_html_row(writer, chinese ? "\350\256\276\345\244\207 / OEM" : "Device / OEM", device);
#ifdef _DEBUG
    _snprintf(message, sizeof(message) - 1,
            "positron system-info kernel=%s os=%s oem=%s\r\n", kernel, os, device);
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
        app_html_append(writer, "<p>");
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
    app_html_append(&writer, template_bytes);
    chinese = AppI18n_CurrentLanguage() == APP_LANGUAGE_ZH_CN;
    if (page_kind == APP_I18N_PAGE_ABOUT) {
        app_internal_about(&writer, data, chinese);
    } else if (page_kind == APP_I18N_PAGE_HISTORY) {
        app_internal_history(&writer, data->history, chinese);
    } else if (page_kind == APP_I18N_PAGE_SETTINGS) {
        app_html_row(&writer, chinese ? "\347\225\214\351\235\242\350\257\255\350\250\200" : "UI language",
                chinese ? "\347\256\200\344\275\223\344\270\255\346\226\207 (zh-CN)" : "English (en-US)");
        app_html_row(&writer, chinese ? "\344\270\273\351\241\265" : "Home", APP_URL_NEWTAB);
        app_html_row(&writer, "JavaScript", chinese ?
                "\347\275\221\347\273\234\351\241\265\345\220\257\347\224\250\346\234\211\347\225\214 classic script\357\274\233\345\206\205\351\203\250\351\241\265\344\270\215\346\211\247\350\241\214\350\204\232\346\234\254\343\200\202" :
                "Bounded classic scripts on network pages; none on internal pages.");
    }
    app_html_append(&writer, marker + strlen("<!--APP_CONTENT-->"));
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
        "nav-downloads", "nav-settings", "nav-welcome", "nav-controls"
    };
    PCoreFocusTargetInfo info;
    char candidate[32];
    unsigned int i;
    int history_index;
    int count;

    count = 0;
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
    char text[416];
    AppHtmlWriter writer;
    int result;

    if (app_internal_os_text(L"PocketPC", 9, text, sizeof(text)) ||
            strcmp(text, "Windows Mobile (PocketPC)") ||
            app_internal_os_text(L"smartphone", 11, text, sizeof(text)) ||
            strcmp(text, "Windows Mobile (smartphone)") ||
            app_internal_os_text(L"Custom CE", 10, text, sizeof(text)) ||
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
            !app_internal_os_text(L"PocketPC", 9, text, 24) || text[0] != '\0' ||
            !app_internal_system_text(NULL, 2, text, sizeof(text)) ||
            !app_internal_system_text(L"x", 0, text, sizeof(text)) ||
            !app_internal_system_text(L"x", 2, NULL, 2) ||
            !app_internal_system_text(L"x", 2, text, 0)) return 1;
    writer.bytes = (char *) malloc(APP_INTERNAL_HTML_MAX);
    if (writer.bytes == NULL) return 1;
    writer.used = 0;
    writer.failed = 0;
    writer.bytes[0] = '\0';
    app_internal_system(&writer, 0);
    result = writer.failed || strstr(writer.bytes, "Kernel (CE): ") == NULL ||
            strstr(writer.bytes, "Operating system (WM): ") == NULL;
    writer.used = 0;
    writer.bytes[0] = '\0';
    app_internal_system(&writer, 1);
    if (writer.failed || strstr(writer.bytes, "\345\206\205\346\240\270 (CE): ") == NULL ||
            strstr(writer.bytes, "\346\223\215\344\275\234\347\263\273\347\273\237 (WM): ") == NULL) result = 1;
    writer.used = 0;
    writer.bytes[0] = '\0';
    app_html_row(&writer, "Device / OEM", "OEM <&\"'>");
    if (writer.failed || strstr(writer.bytes,
            "OEM &lt;&amp;&quot;&#39;&gt;") == NULL) result = 1;
    free(writer.bytes);
    if (!result) AppDebug_Log("positron system-info selftest OK\r\n");
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

    result = 1;
    phase = 1;
    history = NULL;
    document = NULL;
    stylesheet = NULL;
    html = NULL;
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
        _snprintf(url, sizeof(url) - 1, "https://example.com/%u?x=\"<&'", i);
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
    PCore_FreeDocument(document);
    document = NULL;
    AppI18n_FreePage(html);
    html = NULL;
    if (PBrowser_HistoryCommitNavigation(history, "positron://history",
            PBROWSER_HISTORY_METHOD_GET, PBROWSER_HISTORY_TARGET_NEW) != 0 ||
            AppInternalPages_Build(APP_I18N_PAGE_HISTORY, &data, &html,
            &length) != 0 || strstr(html, "id=\"history-1\"") != NULL) goto done;
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
