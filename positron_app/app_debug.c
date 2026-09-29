/*
 * positron_app/app_debug.c - EXE-private Debug diagnostics.
 */

#include "app_debug.h"

#ifdef _DEBUG

#include <stdio.h>
#include <string.h>

#include <windows.h>

#define APP_DEBUG_LOG_PATH       L"\\Temp\\positron-debug.log"
#define APP_DEBUG_MAX_BYTES      (128 * 1024)

static int g_app_debug_file_started;

static void app_debug_output_debugger(const char *message)
{
    WCHAR wide_message[1024];
    int i;

    if (message == NULL) {
        return;
    }
    i = 0;
    while (message[i] != '\0' && i < (int) (sizeof(wide_message) /
            sizeof(wide_message[0])) - 1) {
        wide_message[i] = (WCHAR) (unsigned char) message[i];
        i++;
    }
    wide_message[i] = L'\0';
    OutputDebugStringW(wide_message);
}

static void app_debug_write_file(const char *message)
{
    HANDLE file;
    DWORD high;
    DWORD low;
    DWORD written;
    int length;
    DWORD disposition;

    if (message == NULL) {
        return;
    }
    length = (int) strlen(message);
    if (length <= 0 || length > APP_DEBUG_MAX_BYTES) {
        return;
    }
    disposition = g_app_debug_file_started ? OPEN_ALWAYS : CREATE_ALWAYS;
    file = CreateFileW(APP_DEBUG_LOG_PATH, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disposition,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (g_app_debug_file_started) {
        high = 0;
        low = GetFileSize(file, &high);
        if ((low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) ||
                high != 0 || low >= APP_DEBUG_MAX_BYTES ||
                low > (DWORD) (APP_DEBUG_MAX_BYTES - length) ||
                SetFilePointer(file, 0, NULL, FILE_END) ==
                INVALID_SET_FILE_POINTER) {
            CloseHandle(file);
            return;
        }
    }
    g_app_debug_file_started = 1;
    if (!WriteFile(file, message, (DWORD) length, &written, NULL) ||
            written != (DWORD) length) {
        CloseHandle(file);
        return;
    }
    CloseHandle(file);
}

void AppDebug_BeginSession(void)
{
    char message[160];

    g_app_debug_file_started = 0;
    _snprintf(message, sizeof(message) - 1,
            "positron debug-session pid=%lu tick=%lu\r\n",
            (unsigned long) GetCurrentProcessId(),
            (unsigned long) GetTickCount());
    message[sizeof(message) - 1] = '\0';
    app_debug_write_file(message);
}

void AppDebug_Log(const char *message)
{
    char line[1536];

    if (message == NULL) {
        return;
    }
    _snprintf(line, sizeof(line) - 1, "positron pid=%lu %s",
            (unsigned long) GetCurrentProcessId(), message);
    line[sizeof(line) - 1] = '\0';
    app_debug_output_debugger(line);
    app_debug_write_file(line);
}

#endif /* _DEBUG */
