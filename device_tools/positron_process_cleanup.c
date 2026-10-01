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

static const WCHAR *g_positron_modules[] = {
    L"positron_tls.dll",
    L"positron_json.dll",
    L"positron_media.dll",
    L"positron_http.dll",
    L"positron_core.dll",
    L"positron_image.dll",
    L"positron_script.dll",
    L"positron_browser.dll",
    L"positron_db.dll"
};

static int is_positron_module(const WCHAR *name)
{
    int index;

    if (name == NULL) {
        return 0;
    }
    for (index = 0; index < (int) (sizeof(g_positron_modules) /
            sizeof(g_positron_modules[0])); index++) {
        if (lstrcmpiW(name, g_positron_modules[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

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

static int log_path(WCHAR path[MAX_PATH], const WCHAR *file_name)
{
    DWORD length;

    if (file_name == NULL) {
        return 1;
    }
    length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return 1;
    }
    while (length > 0 && path[length - 1] != L'\\' &&
            path[length - 1] != L'/') {
        length--;
    }
    if (length == 0 || length + lstrlenW(file_name) + 1 >= MAX_PATH) {
        return 1;
    }
    lstrcpyW(path + length, file_name);
    return 0;
}

static int audit_positron_modules(void)
{
    HANDLE process_snapshot;
    HANDLE module_snapshot;
    PROCESSENTRY32 process_entry;
    MODULEENTRY32 module_entry;
    BOOL process_ok;
    BOOL module_ok;
    int holders;
    int unavailable;
    DWORD error;
    char process_name[128];
    char module_name[128];
    char module_path[MAX_PATH * 3];
    char line[512];

    holders = 0;
    unavailable = 0;
    process_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (process_snapshot == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        unavailable++;
        _snprintf(line, sizeof(line) - 1,
                "module_audit_unavailable scope=process_snapshot error=%lu\r\n",
                (unsigned long) error);
        line[sizeof(line) - 1] = '\0';
        write_text(line);
        _snprintf(line, sizeof(line) - 1,
                "module_audit holders=%d unavailable=%d\r\n",
                holders, unavailable);
        line[sizeof(line) - 1] = '\0';
        write_text(line);
        return 1;
    }

    process_entry.dwSize = sizeof(process_entry);
    process_ok = Process32First(process_snapshot, &process_entry);
    if (!process_ok) {
        unavailable++;
        error = GetLastError();
        _snprintf(line, sizeof(line) - 1,
                "module_audit_unavailable scope=process_first error=%lu\r\n",
                (unsigned long) error);
        line[sizeof(line) - 1] = '\0';
        write_text(line);
    }
    while (process_ok) {
        module_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,
                process_entry.th32ProcessID);
        if (module_snapshot == INVALID_HANDLE_VALUE) {
            unavailable++;
            error = GetLastError();
            _snprintf(line, sizeof(line) - 1,
                    "module_audit_unavailable pid=%lu error=%lu\r\n",
                    (unsigned long) process_entry.th32ProcessID,
                    (unsigned long) error);
            line[sizeof(line) - 1] = '\0';
            write_text(line);
        } else {
            module_entry.dwSize = sizeof(module_entry);
            module_ok = Module32First(module_snapshot, &module_entry);
            if (!module_ok) {
                unavailable++;
                error = GetLastError();
                _snprintf(line, sizeof(line) - 1,
                        "module_audit_unavailable pid=%lu error=%lu\r\n",
                        (unsigned long) process_entry.th32ProcessID,
                        (unsigned long) error);
                line[sizeof(line) - 1] = '\0';
                write_text(line);
            }
            while (module_ok) {
                if (is_positron_module(module_entry.szModule)) {
                    process_name[0] = '\0';
                    module_name[0] = '\0';
                    module_path[0] = '\0';
                    if (WideCharToMultiByte(CP_UTF8, 0,
                            process_entry.szExeFile, -1, process_name,
                            sizeof(process_name), NULL, NULL) <= 0) {
                        strcpy(process_name, "unavailable");
                    }
                    if (WideCharToMultiByte(CP_UTF8, 0,
                            module_entry.szModule, -1, module_name,
                            sizeof(module_name), NULL, NULL) <= 0) {
                        strcpy(module_name, "unavailable");
                    }
                    if (WideCharToMultiByte(CP_UTF8, 0,
                            module_entry.szExePath, -1, module_path,
                            sizeof(module_path), NULL, NULL) <= 0) {
                        strcpy(module_path, "unavailable");
                    }
                    holders++;
                    _snprintf(line, sizeof(line) - 1,
                            "module_holder pid=%lu process=%s module=%s "
                            "path=%s\r\n",
                            (unsigned long) process_entry.th32ProcessID,
                            process_name, module_name, module_path);
                    line[sizeof(line) - 1] = '\0';
                    write_text(line);
                }
                module_ok = Module32Next(module_snapshot, &module_entry);
            }
            CloseToolhelp32Snapshot(module_snapshot);
        }
        process_ok = Process32Next(process_snapshot, &process_entry);
    }
    CloseToolhelp32Snapshot(process_snapshot);
    _snprintf(line, sizeof(line) - 1,
            "module_audit holders=%d unavailable=%d\r\n",
            holders, unavailable);
    line[sizeof(line) - 1] = '\0';
    write_text(line);
    return holders == 0 && unavailable == 0 ? 0 : 1;
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
    int audit_mode;
    int audit_result;

    (void) hInstance;
    (void) hPrev;
    (void) nCmdShow;
    g_log = INVALID_HANDLE_VALUE;
    audit_mode = lpCmdLine != NULL &&
            lstrcmpiW(lpCmdLine, L"--audit-modules") == 0;
    if (log_path(path, audit_mode ? L"module-audit.log" :
            L"process-cleanup.log") == 0) {
        g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (audit_mode) {
        write_text("Positron module audit v1\r\n");
        audit_result = audit_positron_modules();
        if (g_log != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(g_log);
            CloseHandle(g_log);
        }
        return audit_result;
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
