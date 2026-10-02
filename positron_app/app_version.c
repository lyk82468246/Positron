#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include "app_version.h"
#ifdef _DEBUG
#include "app_build_time.h"
#include "app_debug.h"
#endif

/* Validate the existing CAB's four numeric components without losing its
 * original spelling (including leading zeroes). Never render arbitrary or
 * unterminated registry bytes, a packaging placeholder, or a partial value. */
static int app_version_registry_text(DWORD type, const WCHAR *value,
        DWORD bytes, char *out_text, unsigned int capacity)
{
    unsigned int count;
    unsigned int i;
    unsigned int digits;
    unsigned int parts;
    unsigned long number;

    if (out_text == NULL || capacity == 0) return 1;
    out_text[0] = '\0';
    if (type != REG_SZ || value == NULL || bytes < 2 * sizeof(WCHAR) ||
            bytes > APP_VERSION_TEXT_MAX * sizeof(WCHAR) ||
            bytes % sizeof(WCHAR) != 0) return 1;
    count = (unsigned int) (bytes / sizeof(WCHAR));
    if (value[count - 1] != L'\0' || count > capacity) return 1;
    digits = 0;
    parts = 1;
    number = 0;
    for (i = 0; i < count - 1; i++) {
        if (value[i] >= L'0' && value[i] <= L'9') {
            if (++digits > 5) return 1;
            number = number * 10 + (unsigned long) (value[i] - L'0');
            if (number > 65535UL) return 1;
        } else if (value[i] == L'.' && digits != 0 && parts < 4) {
            parts++;
            digits = 0;
            number = 0;
        } else {
            return 1;
        }
    }
    if (parts != 4 || digits == 0) return 1;
    for (i = 0; i < count; i++) out_text[i] = (char) value[i];
    return 0;
}

int AppVersion_Get(char *out_text, unsigned int capacity)
{
#ifdef _DEBUG
    unsigned int length;
#else
    HKEY key;
    WCHAR value[APP_VERSION_TEXT_MAX];
    DWORD type;
    DWORD bytes;
    LONG status;
#endif

    if (out_text == NULL || capacity == 0) return 1;
    out_text[0] = '\0';
#ifdef _DEBUG
    length = (unsigned int) strlen(APP_DEBUG_BUILD_TIME);
    if (length >= capacity) return 1;
    memcpy(out_text, APP_DEBUG_BUILD_TIME, length + 1);
    return 0;
#else
    key = NULL;
    status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Positron", 0,
            KEY_READ, &key);
    if (status != ERROR_SUCCESS) return 1;
    memset(value, 0xff, sizeof(value));
    type = 0;
    bytes = sizeof(value);
    status = RegQueryValueExW(key, L"Version", NULL, &type,
            (LPBYTE) value, &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return 1;
    return app_version_registry_text(type, value, bytes, out_text, capacity);
#endif
}

#ifdef _DEBUG
int AppVersion_DebugCheck(void)
{
    static const WCHAR *invalid[] = {
        L"", L"1.2.3", L"1.2.3.4.5", L"1..3.4", L"1.2.3.",
        L"65536.1.2.3", L"000001.2.3.4", L"1.2.-3.4",
        L"__POSITRON_CAB_VERSION__", L"1.2.3.4\n", L"1.2.3.x"
    };
    static const WCHAR valid[] = L"2026.10.02.01";
    static const WCHAR embedded_null[] = L"1.2.3.4\0junk";
    char first[APP_VERSION_TEXT_MAX];
    char second[APP_VERSION_TEXT_MAX];
    char message[160];
    unsigned int i;
    DWORD bytes;

    if (AppVersion_Get(first, sizeof(first)) != 0 || strlen(first) != 19 ||
            first[4] != '-' || first[7] != '-' || first[10] != ' ' ||
            first[13] != ':' || first[16] != ':' ||
            AppVersion_Get(second, sizeof(second)) != 0 ||
            strcmp(first, second) != 0) return 1;
    for (i = 0; i < 19; i++) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
        if (first[i] < '0' || first[i] > '9') return 1;
    }
    strcpy(second, "unchanged");
    if (AppVersion_Get(second, 19) == 0 || second[0] != '\0' ||
            AppVersion_Get(NULL, 10) == 0 ||
            AppVersion_Get(second, 0) == 0) return 1;
    bytes = sizeof(valid);
    if (app_version_registry_text(REG_SZ, valid, bytes, second,
            sizeof(second)) != 0 || strcmp(second, "2026.10.02.01") != 0 ||
            app_version_registry_text(REG_SZ, L"0.65535.1.2",
            sizeof(L"0.65535.1.2"), second, sizeof(second)) != 0) return 1;
    if (app_version_registry_text(REG_DWORD, valid, bytes, second,
            sizeof(second)) == 0 || second[0] != '\0' ||
            app_version_registry_text(REG_SZ, valid, bytes - 1, second,
            sizeof(second)) == 0 ||
            app_version_registry_text(REG_SZ, valid, bytes - sizeof(WCHAR),
            second, sizeof(second)) == 0 ||
            app_version_registry_text(REG_SZ, valid, bytes, second, 5) == 0 ||
            app_version_registry_text(REG_SZ, embedded_null,
            sizeof(embedded_null), second, sizeof(second)) == 0 ||
            app_version_registry_text(REG_SZ, valid,
            (APP_VERSION_TEXT_MAX + 1) * sizeof(WCHAR), second,
            sizeof(second)) == 0) return 1;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        bytes = (DWORD) ((wcslen(invalid[i]) + 1) * sizeof(WCHAR));
        strcpy(second, "unchanged");
        if (app_version_registry_text(REG_SZ, invalid[i], bytes, second,
                sizeof(second)) == 0 || second[0] != '\0') return 1;
    }
    _snprintf(message, sizeof(message) - 1,
            "positron app-version selftest OK build=%s\r\n", first);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
    return 0;
}
#endif
