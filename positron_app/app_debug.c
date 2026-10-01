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

static HANDLE g_app_debug_file = INVALID_HANDLE_VALUE;
static DWORD g_app_debug_bytes;
static CRITICAL_SECTION g_app_debug_lock;
static int g_app_debug_lock_ready;

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
    DWORD written;
    int length;

    if (message == NULL) {
        return;
    }
    length = (int) strlen(message);
    if (length <= 0 || length > APP_DEBUG_MAX_BYTES) {
        return;
    }
    if (!g_app_debug_lock_ready) {
        return;
    }
    EnterCriticalSection(&g_app_debug_lock);
    if (g_app_debug_file != INVALID_HANDLE_VALUE &&
            g_app_debug_bytes <= (DWORD) (APP_DEBUG_MAX_BYTES - length)) {
        if (!WriteFile(g_app_debug_file, message, (DWORD) length,
                &written, NULL) || written != (DWORD) length) {
            CloseHandle(g_app_debug_file);
            g_app_debug_file = INVALID_HANDLE_VALUE;
        } else {
            g_app_debug_bytes += written;
        }
    }
    LeaveCriticalSection(&g_app_debug_lock);
}

void AppDebug_BeginSession(void)
{
    char message[160];

    if (!g_app_debug_lock_ready) {
        InitializeCriticalSection(&g_app_debug_lock);
        g_app_debug_lock_ready = 1;
    }
    EnterCriticalSection(&g_app_debug_lock);
    if (g_app_debug_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_app_debug_file);
    }
    g_app_debug_bytes = 0;
    g_app_debug_file = CreateFileW(APP_DEBUG_LOG_PATH, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    LeaveCriticalSection(&g_app_debug_lock);
    _snprintf(message, sizeof(message) - 1,
            "positron debug-session pid=%lu tick=%lu\r\n",
            (unsigned long) GetCurrentProcessId(),
            (unsigned long) GetTickCount());
    message[sizeof(message) - 1] = '\0';
    app_debug_write_file(message);
}

void AppDebug_EndSession(void)
{
    if (!g_app_debug_lock_ready) {
        return;
    }
    EnterCriticalSection(&g_app_debug_lock);
    if (g_app_debug_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_app_debug_file);
        g_app_debug_file = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_app_debug_lock);
    /* Keep the lock valid for late diagnostics; no further file writes occur. */
}

void AppDebug_Log(const char *message)
{
    char line[1536];

    if (message == NULL) {
        return;
    }
    _snprintf(line, sizeof(line) - 1, "positron pid=%lu tick=%lu %s",
            (unsigned long) GetCurrentProcessId(),
            (unsigned long) GetTickCount(), message);
    line[sizeof(line) - 1] = '\0';
    app_debug_output_debugger(line);
    app_debug_write_file(line);
}

void AppDebug_LogElapsed(const char *phase, unsigned long started)
{
    char message[160];

    _snprintf(message, sizeof(message) - 1,
            "positron ui-phase phase=%s elapsed_ms=%lu\r\n", phase,
            (unsigned long) GetTickCount() - started);
    message[sizeof(message) - 1] = '\0';
    AppDebug_Log(message);
}

#endif /* _DEBUG */
