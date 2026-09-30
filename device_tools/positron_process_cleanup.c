/*
 * Positron device-gate process cleanup helper.
 *
 * This executable has no Positron DLL imports.  The gate starts it only when
 * explicitly asked to clear stale positron.exe processes, before it starts the
 * test host.  Keeping it independent is important on Windows CE: a process
 * that imports positron_core.dll cannot reliably clean up an older process
 * before the loader chooses the global same-basename mapping.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static HANDLE g_log = INVALID_HANDLE_VALUE;

static void write_text(const char *text)
{
    DWORD written;

    if (g_log != INVALID_HANDLE_VALUE && text != NULL) {
        WriteFile(g_log, text, (DWORD) strlen(text), &written, NULL);
    }
}

static int is_gate_target(const WCHAR *name)
{
    static const WCHAR prefix[] = L"test_host-run-";
    int index;

    if (name == NULL || lstrcmpiW(name, L"positron.exe") == 0) {
        return name != NULL;
    }
    for (index = 0; prefix[index] != L'\0'; index++) {
        if (name[index] != prefix[index]) {
            return 0;
        }
    }
    return 1;
}

static int log_path(WCHAR path[MAX_PATH])
{
    DWORD length;

    length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return 1;
    }
    while (length > 0 && path[length - 1] != L'\\' &&
            path[length - 1] != L'/') {
        length--;
    }
    if (length == 0 || length + 22 >= MAX_PATH) {
        return 1;
    }
    lstrcpyW(path + length, L"process-cleanup.log");
    return 0;
}

static int terminate_window_target(DWORD current_pid, int *targets,
        int *failures)
{
    HWND window;
    DWORD process_id;
    HANDLE process;
    DWORD error;
    char line[256];

    window = FindWindowW(L"PositronBrowserWindow", NULL);
    if (window == NULL) {
        return 0;
    }
    process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == 0 || process_id == current_pid) {
        return 0;
    }
    (*targets)++;
    process = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
    if (process == NULL) {
        (*failures)++;
        error = GetLastError();
        _snprintf(line, sizeof(line) - 1,
                "window pid=%lu result=OPEN_FAILED error=%lu\r\n",
                (unsigned long) process_id, (unsigned long) error);
    } else if (!TerminateProcess(process, 0x50534F54UL)) {
        (*failures)++;
        error = GetLastError();
        CloseHandle(process);
        _snprintf(line, sizeof(line) - 1,
                "window pid=%lu result=TERMINATE_FAILED error=%lu\r\n",
                (unsigned long) process_id, (unsigned long) error);
    } else {
        CloseHandle(process);
        _snprintf(line, sizeof(line),
                "window pid=%lu result=TERMINATED\r\n",
                (unsigned long) process_id);
    }
    line[sizeof(line) - 1] = '\0';
    write_text(line);
    return 1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
        LPWSTR lpCmdLine, int nCmdShow)
{
    WCHAR path[MAX_PATH];
    HANDLE snapshot;
    PROCESSENTRY32 entry;
    BOOL ok;
    DWORD current_pid;
    int targets;
    int failures;
    HANDLE process;
    DWORD error;
    char line[256];

    (void) hInstance;
    (void) hPrev;
    (void) lpCmdLine;
    (void) nCmdShow;
    g_log = INVALID_HANDLE_VALUE;
    if (log_path(path) == 0) {
        g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    write_text("Positron process cleanup v1\r\n");
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        current_pid = GetCurrentProcessId();
        targets = 0;
        failures = 0;
        if (terminate_window_target(current_pid, &targets, &failures)) {
            _snprintf(line, sizeof(line) - 1,
                    "summary target_count=%d failed=%d "
                    "snapshot_error=%lu fallback=window\r\n",
                    targets, failures, (unsigned long) error);
        } else {
            _snprintf(line, sizeof(line) - 1,
                    "summary target_count=0 failed=1 snapshot_error=%lu\r\n",
                    (unsigned long) error);
        }
        line[sizeof(line) - 1] = '\0';
        write_text(line);
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
        }
        return failures == 0 && targets > 0 ? 0 : 1;
    }
    current_pid = GetCurrentProcessId();
    targets = 0;
    failures = 0;
    entry.dwSize = sizeof(entry);
    ok = Process32First(snapshot, &entry);
    while (ok) {
        if (entry.th32ProcessID != current_pid &&
                is_gate_target(entry.szExeFile)) {
            targets++;
            process = OpenProcess(PROCESS_TERMINATE, FALSE,
                    entry.th32ProcessID);
            if (process == NULL) {
                failures++;
                error = GetLastError();
                _snprintf(line, sizeof(line) - 1,
                        "target pid=%lu result=OPEN_FAILED error=%lu\r\n",
                        (unsigned long) entry.th32ProcessID,
                        (unsigned long) error);
            } else if (!TerminateProcess(process, 0x50534F54UL)) {
                failures++;
                error = GetLastError();
                CloseHandle(process);
                _snprintf(line, sizeof(line) - 1,
                        "target pid=%lu result=TERMINATE_FAILED error=%lu\r\n",
                        (unsigned long) entry.th32ProcessID,
                        (unsigned long) error);
            } else {
                CloseHandle(process);
                _snprintf(line, sizeof(line),
                        "target pid=%lu result=TERMINATED\r\n",
                        (unsigned long) entry.th32ProcessID);
            }
            line[sizeof(line) - 1] = '\0';
            write_text(line);
        }
        ok = Process32Next(snapshot, &entry);
    }
    CloseToolhelp32Snapshot(snapshot);
    _snprintf(line, sizeof(line) - 1,
            "summary target_count=%d failed=%d\r\n", targets, failures);
    line[sizeof(line) - 1] = '\0';
    write_text(line);
    if (g_log != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_log);
        CloseHandle(g_log);
    }
    return failures == 0 ? 0 : 1;
}
