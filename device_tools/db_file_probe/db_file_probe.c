/* ARMV4I-only DB file fixture consumer. Never compile product sources here.
 * The coordinator does not open a DB. Each phase runs in a new process.
 * No production data, private SQLite metadata, reset or arbitrary PID access. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "positron_db.h"

#define PROBE_CHILD_TIMEOUT_MS 60000UL
#define PROBE_PATH_BYTES (MAX_PATH * 3)
#define PROBE_MARKER "DB_FILE_FIXTURE_V1\r\n"
#define PROBE_SCHEMA_SQL "CREATE TABLE samples(" \
        "id INTEGER PRIMARY KEY,label TEXT NOT NULL,payload BLOB NOT NULL)"

static const char g_committed_text[] =
        "\xe4\xb8\xad\xe6\x96\x87\xe8\xaf\xbb\xe5\x86\x99";
static const char g_second_text[] =
        "B: \xe8\xbf\x9b\xe7\xa8\x8b\xe6\x8f\x90\xe4\xba\xa4";
static const unsigned char g_blob[] = {0, 1, 127, 128, 255, 0};
static HANDLE g_log = INVALID_HANDLE_VALUE;
static const char* g_phase = "startup";
static HANDLE g_children[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
static DWORD g_child_pids[6];
static const WCHAR* g_child_roles[6];
static BOOL g_child_waited[6];
static BOOL g_locks = FALSE;
static BOOL g_journal = FALSE;
static const WCHAR* g_storage = L"sd";

static BOOL probe_write(const char* text)
{
    DWORD written;
    DWORD length;

    if (g_log == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    length = (DWORD)strlen(text);
    written = 0;
    return WriteFile(g_log, text, length, &written, NULL) &&
            written == length && FlushFileBuffers(g_log);
}

static BOOL probe_utf8(const WCHAR* path, char* result, int capacity)
{
    return WideCharToMultiByte(CP_UTF8, 0, path, -1, result, capacity,
            NULL, NULL) != 0;
}

static BOOL probe_append(WCHAR* path, int capacity, const WCHAR* suffix)
{
    if (wcslen(path) + wcslen(suffix) + 1 > (size_t)capacity) {
        return FALSE;
    }
    wcscat(path, suffix);
    return TRUE;
}

static BOOL probe_package_path(WCHAR* executable, WCHAR* directory)
{
    static const WCHAR sd_prefix[] =
            L"\\Storage Card\\Temp\\Positron-device-gate\\db-file-";
    WCHAR* slash;
    const WCHAR* leaf;
    DWORD length;

    length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return FALSE;
    }
    wcscpy(directory, executable);
    slash = wcsrchr(directory, L'\\');
    if (slash == NULL) {
        return FALSE;
    }
    *slash = L'\0';
    if (wcsncmp(directory, sd_prefix, wcslen(sd_prefix)) == 0) {
        leaf = directory + wcslen(sd_prefix);
    } else {
        return FALSE;
    }
    /* Exactly one gate-owned directory, not a parent or arbitrary path. */
    if (*leaf == L'\0' || wcsstr(leaf, L"..") != NULL) {
        return FALSE;
    }
    while (*leaf != L'\0') {
        if (!((*leaf >= L'a' && *leaf <= L'z') ||
                (*leaf >= L'0' && *leaf <= L'9') || *leaf == L'-')) {
            return FALSE;
        }
        ++leaf;
    }
    return TRUE;
}

static BOOL probe_leaf_valid(const WCHAR* leaf)
{
    int digits;
    int part;

    if (wcsncmp(leaf, L"fixture-", 8) != 0) {
        return FALSE;
    }
    leaf += 8;
    for (part = 0; part < 2; ++part) {
        digits = 0;
        while (*leaf >= L'0' && *leaf <= L'9') {
            ++digits;
            ++leaf;
        }
        if (digits == 0 || digits > 10) {
            return FALSE;
        }
        if (part == 0) {
            if (*leaf++ != L'-') {
                return FALSE;
            }
        }
    }
    return *leaf == L'\0';
}

static BOOL probe_fixture_root(const WCHAR* package, const WCHAR* leaf,
        BOOL coordinator, WCHAR* root)
{
    DWORD attributes;

    if (wcscmp(g_storage, L"sd") == 0) {
        wcscpy(root, package);
    } else if (wcscmp(g_storage, L"internal") == 0) {
        wcscpy(root, L"\\Temp\\Positron-device-gate\\db-file-fixtures");
        if (coordinator && !CreateDirectoryW(root, NULL)) {
            attributes = GetFileAttributesW(root);
            if (attributes == 0xffffffffUL ||
                    !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                return FALSE;
            }
        }
    } else {
        return FALSE;
    }
    return probe_append(root, MAX_PATH, L"\\") &&
            probe_append(root, MAX_PATH, leaf);
}

static BOOL probe_marker(const WCHAR* root, BOOL create)
{
    WCHAR path[MAX_PATH];
    HANDLE marker;
    char contents[sizeof(PROBE_MARKER)];
    DWORD count;
    BOOL ok;

    wcscpy(path, root);
    if (!probe_append(path, MAX_PATH, L"\\owner.marker")) {
        return FALSE;
    }
    marker = CreateFileW(path, create ? GENERIC_WRITE : GENERIC_READ,
            FILE_SHARE_READ, NULL, create ? CREATE_NEW : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (marker == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    count = 0;
    if (create) {
        ok = WriteFile(marker, PROBE_MARKER, sizeof(PROBE_MARKER) - 1,
                &count, NULL) && count == sizeof(PROBE_MARKER) - 1 &&
                FlushFileBuffers(marker);
    } else {
        ok = ReadFile(marker, contents, sizeof(contents), &count, NULL) &&
                count == sizeof(PROBE_MARKER) - 1 &&
                memcmp(contents, PROBE_MARKER, count) == 0;
    }
    CloseHandle(marker);
    return ok;
}

static BOOL probe_identity(const WCHAR* executable, const WCHAR* package)
{
    HMODULE module;
    WCHAR actual[MAX_PATH];
    WCHAR expected[MAX_PATH];
    char text[PROBE_PATH_BYTES];
    char line[PROBE_PATH_BYTES + 128];

    module = GetModuleHandleW(L"positron_db.dll");
    if (module == NULL || GetModuleFileNameW(module, actual, MAX_PATH) == 0) {
        return FALSE;
    }
    wcscpy(expected, package);
    if (!probe_append(expected, MAX_PATH, L"\\positron_db.dll") ||
            lstrcmpiW(actual, expected) != 0 ||
            !probe_utf8(executable, text, sizeof(text))) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1, "process pid=%lu path=%s\r\n",
            (unsigned long)GetCurrentProcessId(), text);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line) || !probe_utf8(actual, text, sizeof(text))) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1, "db_module_path=%s\r\n", text);
    line[sizeof(line) - 1] = '\0';
    return probe_write(line);
}

static void probe_failure(PDbHandle db)
{
    PDbErrorInfo error;
    PDbConnectionState state;
    char line[640];

    memset(&error, 0, sizeof(error));
    error.size = sizeof(error);
    error.version = PDB_ERROR_INFO_VERSION;
    memset(&state, 0, sizeof(state));
    state.size = sizeof(state);
    state.version = PDB_CONNECTION_STATE_VERSION;
    if (db != NULL) {
        PDb_GetErrorInfo(db, &error);
        PDb_GetConnectionState(db, &state);
    }
    _snprintf(line, sizeof(line) - 1,
            "FAIL phase=%s win32=%lu result=%d category=%d native=%d "
            "extended=%d active=%d txn=%d statements=%d message=%s\r\n",
            g_phase, (unsigned long)GetLastError(), error.result,
            error.category, error.sqlite_code, error.sqlite_extended_code,
            state.transaction_active, state.transaction_state,
            state.statement_count, error.message);
    line[sizeof(line) - 1] = '\0';
    probe_write(line);
}

static BOOL probe_insert(PDbHandle db, __int64 id, const char* text)
{
    PDbStmtHandle stmt;
    int result;
    int finalized;

    stmt = NULL;
    result = PDb_Prepare(db,
            "INSERT INTO samples(id,label,payload) VALUES(?1,?2,?3)", &stmt);
    if (result == PDB_OK) {
        result = PDb_BindInt64(stmt, 1, id);
    }
    if (result == PDB_OK) {
        result = PDb_BindText(stmt, 2, text, (int)strlen(text));
    }
    if (result == PDB_OK) {
        result = PDb_BindBlob(stmt, 3, g_blob, sizeof(g_blob));
    }
    if (result == PDB_OK) {
        result = PDb_Step(stmt);
    }
    finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return result == PDB_STEP_DONE && finalized == PDB_OK;
}

static BOOL probe_row(PDbHandle db, __int64 id, const char* expected)
{
    PDbStmtHandle stmt;
    const char* text;
    const void* blob;
    BOOL ok;
    int finalized;

    stmt = NULL;
    ok = PDb_Prepare(db,
            "SELECT label,payload FROM samples WHERE id=?1", &stmt) == PDB_OK &&
            PDb_BindInt64(stmt, 1, id) == PDB_OK &&
            PDb_Step(stmt) == PDB_STEP_ROW;
    if (ok) {
        text = PDb_ColumnText(stmt, 0);
        ok = text != NULL && PDb_ColumnType(stmt, 0) == PDB_VALUE_TEXT &&
                PDb_ColumnBytes(stmt, 0) == (int)strlen(expected) &&
                memcmp(text, expected, strlen(expected)) == 0;
    }
    if (ok) {
        blob = PDb_ColumnBlob(stmt, 1);
        ok = blob != NULL && PDb_ColumnType(stmt, 1) == PDB_VALUE_BLOB &&
                PDb_ColumnBytes(stmt, 1) == sizeof(g_blob) &&
                memcmp(blob, g_blob, sizeof(g_blob)) == 0 &&
                PDb_Step(stmt) == PDB_STEP_DONE;
    }
    finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return ok && finalized == PDB_OK;
}

static BOOL probe_scalar(PDbHandle db, const char* sql, __int64 expected)
{
    PDbStmtHandle stmt;
    BOOL ok;
    int finalized;

    stmt = NULL;
    ok = PDb_Prepare(db, sql, &stmt) == PDB_OK &&
            PDb_Step(stmt) == PDB_STEP_ROW &&
            PDb_ColumnInt64(stmt, 0) == expected &&
            PDb_Step(stmt) == PDB_STEP_DONE;
    finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return ok && finalized == PDB_OK;
}

static BOOL probe_integrity(PDbHandle db)
{
    PDbStmtHandle stmt;
    const char* result;
    BOOL ok;
    int finalized;

    stmt = NULL;
    ok = PDb_Prepare(db, "PRAGMA integrity_check", &stmt) == PDB_OK &&
            PDb_Step(stmt) == PDB_STEP_ROW;
    if (ok) {
        result = PDb_ColumnText(stmt, 0);
        ok = result != NULL && strcmp(result, "ok") == 0 &&
                PDb_Step(stmt) == PDB_STEP_DONE;
    }
    finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return ok && finalized == PDB_OK;
}

static BOOL probe_state_idle(PDbHandle db)
{
    PDbConnectionState state;

    memset(&state, 0, sizeof(state));
    state.size = sizeof(state);
    state.version = PDB_CONNECTION_STATE_VERSION;
    return PDb_GetConnectionState(db, &state) == PDB_OK &&
            !state.transaction_active && state.statement_count == 0;
}

static BOOL probe_open(const char* path, PDbHandle* outDb)
{
    PDbErrorInfo error;
    char line[384];
    int result;

    memset(&error, 0, sizeof(error));
    error.size = sizeof(error);
    error.version = PDB_ERROR_INFO_VERSION;
    result = PDb_OpenUtf8Ex(path, PDB_OPEN_LOCAL_FULL_SQL, outDb, &error);
    if (result != PDB_OK) {
        _snprintf(line, sizeof(line) - 1,
                "open_failure result=%d category=%d native=%d "
                "extended=%d message=%s\r\n", result, error.category,
                error.sqlite_code, error.sqlite_extended_code, error.message);
        line[sizeof(line) - 1] = '\0';
        probe_write(line);
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_child(const WCHAR* root, const WCHAR* role)
{
    WCHAR wide_path[MAX_PATH];
    char path[PROBE_PATH_BYTES];
    char line[PROBE_PATH_BYTES + 32];
    PDbHandle db;
    HANDLE fresh_file;
    BOOL create;
    BOOL verify_b;
    BOOL ok;

    db = NULL;
    ok = FALSE;
    create = wcscmp(role, L"create") == 0;
    verify_b = wcscmp(role, L"verify-b") == 0;
    wcscpy(wide_path, root);
    g_phase = "chinese_directory";
    if (!probe_append(wide_path, MAX_PATH,
            L"\\\x4e2d\x6587\x76ee\x5f55")) {
        goto child_done;
    }
    if (create && !CreateDirectoryW(wide_path, NULL)) {
        goto child_done;
    }
    if (!probe_append(wide_path, MAX_PATH,
            L"\\\x6570\x636e\x5e93.sqlite") ||
            !probe_utf8(wide_path, path, sizeof(path))) {
        goto child_done;
    }
    _snprintf(line, sizeof(line) - 1, "fixture_db_utf8=%s\r\n", path);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line)) {
        goto child_done;
    }
    g_phase = "file_identity";
    if (create) {
        /* Reserve this exact new filename atomically. WinCE attribute probes
         * do not provide a reliable missing-file last-error on every volume;
         * CREATE_NEW rejects any pre-existing file without depending on it. */
        fresh_file = CreateFileW(wide_path, GENERIC_WRITE, FILE_SHARE_READ,
                NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fresh_file == INVALID_HANDLE_VALUE) {
            goto child_done;
        }
        ok = FlushFileBuffers(fresh_file);
        CloseHandle(fresh_file);
        if (!ok || !probe_write("file_identity_guard=CREATE_NEW\r\n")) {
            ok = FALSE;
            goto child_done;
        }
        ok = FALSE;
    } else if (GetFileAttributesW(wide_path) == 0xffffffffUL) {
        goto child_done;
    }
    g_phase = "open_utf8_file";
    if (!probe_open(path, &db)) {
        goto child_done;
    }
    if (PDb_Exec(db, "PRAGMA max_page_count=512") != PDB_OK) {
        goto child_done;
    }
    if (create) {
        g_phase = "migration_and_committed_chinese";
        if (PDb_ApplyMigration(db, 1, PROBE_SCHEMA_SQL) != PDB_OK ||
                PDb_Begin(db) != PDB_OK ||
                !probe_insert(db, 1, g_committed_text) ||
                PDb_Commit(db) != PDB_OK) {
            goto child_done;
        }
        g_phase = "rolled_back_row";
        if (PDb_Begin(db) != PDB_OK ||
                !probe_insert(db, 2, "rollback-only") ||
                !probe_row(db, 2, "rollback-only") ||
                PDb_Rollback(db) != PDB_OK ||
                !probe_scalar(db, "SELECT COUNT(*) FROM samples", 1)) {
            goto child_done;
        }
        g_phase = "file_migration_script_failure";
        if (PDb_ApplyMigration(db, 2, "ALTER TABLE samples ADD COLUMN extra TEXT;"
                "UPDATE samples SET label='bad-migration'; SELECT FROM broken")
                != PDB_ERROR || !probe_state_idle(db) ||
                PDb_ApplyMigration(db, 1, "") != PDB_OK ||
                !probe_row(db, 1, g_committed_text) ||
                PDb_Exec(db, "SELECT extra FROM samples") != PDB_ERROR) {
            goto child_done;
        }
        if (!probe_write("migration_script_failure=atomic\r\n")) {
            goto child_done;
        }
        PDb_Close(db);
        db = NULL;
        g_phase = "same_process_reopen";
        if (!probe_open(path, &db)) {
            goto child_done;
        }
        if (PDb_Exec(db, "PRAGMA max_page_count=512") != PDB_OK) {
            goto child_done;
        }
    }
    g_phase = "exact_bytes_schema_and_integrity";
    if (PDb_ApplyMigration(db, 1, "") != PDB_OK ||
            PDb_Exec(db, "SELECT extra FROM samples") != PDB_ERROR ||
            !probe_row(db, 1, g_committed_text) ||
            !probe_scalar(db, "SELECT COUNT(*) FROM samples", verify_b ? 2 : 1) ||
            !probe_integrity(db) || !probe_state_idle(db)) {
        goto child_done;
    }
    if (!probe_write("exact_utf8_text=PASS exact_blob=PASS schema=PASS "
            "integrity=ok transaction_idle=1 statements=0\r\n")) {
        goto child_done;
    }
    if (!create && !verify_b) {
        g_phase = "second_process_commit";
        if (PDb_Begin(db) != PDB_OK || !probe_insert(db, 3, g_second_text) ||
                PDb_Commit(db) != PDB_OK || !probe_state_idle(db)) {
            goto child_done;
        }
    } else if (verify_b && !probe_row(db, 3, g_second_text)) {
        goto child_done;
    }
    ok = TRUE;
child_done:
    if (!ok) {
        probe_failure(db);
    }
    PDb_Close(db);
    return ok;
}

static BOOL probe_launch(const WCHAR* executable, const WCHAR* leaf,
        const WCHAR* role, int slot, DWORD* outPid)
{
    PROCESS_INFORMATION process;
    WCHAR command[128];
    char role_text[24];
    char line[192];
    int index;

    if (slot < 0 || slot >= 6 || g_children[slot] != NULL) {
        return FALSE;
    }
    wcscpy(command, L"--child ");
    if (!probe_append(command, 128, role) ||
            !probe_append(command, 128, L" ") ||
            !probe_append(command, 128, g_storage) ||
            !probe_append(command, 128, L" ") ||
            !probe_append(command, 128, leaf) ||
            !probe_utf8(role, role_text, sizeof(role_text))) {
        return FALSE;
    }
    memset(&process, 0, sizeof(process));
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, 0,
            NULL, NULL, NULL, &process)) {
        return FALSE;
    }
    *outPid = process.dwProcessId;
    /* Keep even exited process objects alive until all phases are complete,
     * avoiding PID reuse as an ambiguity in the fresh-process evidence. */
    g_children[slot] = process.hProcess;
    g_child_pids[slot] = process.dwProcessId;
    g_child_roles[slot] = role;
    CloseHandle(process.hThread);
    _snprintf(line, sizeof(line) - 1, "child role=%s pid=%lu created=1\r\n",
            role_text, (unsigned long)process.dwProcessId);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line) || *outPid == 0 ||
            *outPid == GetCurrentProcessId()) {
        return FALSE;
    }
    for (index = 0; index < slot; ++index) {
        if (g_children[index] != NULL && g_child_pids[index] == *outPid) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL probe_wait(int slot)
{
    char role_text[24];
    char line[192];
    DWORD waited;
    DWORD exit_code;
    BOOL ok;

    if (slot < 0 || slot >= 6 || g_children[slot] == NULL ||
            g_child_waited[slot] ||
            !probe_utf8(g_child_roles[slot], role_text, sizeof(role_text))) {
        return FALSE;
    }
    ok = TRUE;
    waited = WaitForSingleObject(g_children[slot], PROBE_CHILD_TIMEOUT_MS);
    exit_code = STILL_ACTIVE;
    if (waited == WAIT_TIMEOUT) {
        /* This is the exact still-owned handle returned by our CreateProcess.
         * Never OpenProcess an enumerated PID or terminate another program. */
        ok = FALSE;
        if (TerminateProcess(g_children[slot], 0x50444254UL)) {
            waited = WaitForSingleObject(g_children[slot], 5000);
        }
        probe_write("child_timeout=FAIL owned_handle_termination_only=1\r\n");
    }
    if (waited != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(g_children[slot], &exit_code) || exit_code != 0) {
        ok = FALSE;
    }
    _snprintf(line, sizeof(line) - 1,
            "child role=%s pid=%lu wait=%lu exit=%lu completed=%d\r\n",
            role_text, (unsigned long)g_child_pids[slot], (unsigned long)waited,
            (unsigned long)exit_code, ok ? 1 : 0);
    line[sizeof(line) - 1] = '\0';
    g_child_waited[slot] = waited == WAIT_OBJECT_0;
    return probe_write(line) && ok;
}

static BOOL probe_spawn(const WCHAR* executable, const WCHAR* leaf,
        const WCHAR* role, int slot, DWORD* outPid)
{
    return probe_launch(executable, leaf, role, slot, outPid) &&
            probe_wait(slot);
}

#include "db_lock_probe.h"
#include "db_journal_probe.h"

static BOOL probe_coordinator(const WCHAR* executable, const WCHAR* package)
{
    WCHAR leaf[64];
    WCHAR root[MAX_PATH];
    DWORD creator;
    DWORD reader;
    DWORD verifier;
    char root_utf8[PROBE_PATH_BYTES];
    char line[PROBE_PATH_BYTES + 256];

    _snwprintf(leaf, 63, L"fixture-%lu-%lu",
            (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    leaf[63] = L'\0';
    if (!probe_leaf_valid(leaf) ||
            !probe_fixture_root(package, leaf, TRUE, root) ||
            !CreateDirectoryW(root, NULL) || !probe_marker(root, TRUE)) {
        return FALSE;
    }
    if (!probe_utf8(root, root_utf8, sizeof(root_utf8))) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1, "fixture_storage=%s\r\nfixture_root=%s\r\n",
            wcscmp(g_storage, L"sd") == 0 ? "sd" : "internal", root_utf8);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line)) {
        return FALSE;
    }
    if (g_locks) {
        return probe_lock_sequence(executable, leaf);
    }
    if (g_journal) {
        return probe_journal_sequence(executable, leaf, root);
    }
    /* Do not start B before A's process handle is signalled with exit=0. */
    if (!probe_spawn(executable, leaf, L"create", 0, &creator) ||
            !probe_spawn(executable, leaf, L"read", 1, &reader) ||
            !probe_spawn(executable, leaf, L"verify-b", 2, &verifier)) {
        return FALSE;
    }
    if (creator == reader || reader == verifier || creator == verifier ||
            creator == GetCurrentProcessId() || reader == GetCurrentProcessId() ||
            verifier == GetCurrentProcessId()) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1,
            "fresh_processes=3 creator_pid=%lu reader_pid=%lu verifier_pid=%lu "
            "creator_exited_before_reader=1 reader_exited_before_verifier=1\r\n",
            (unsigned long)creator, (unsigned long)reader,
            (unsigned long)verifier);
    line[sizeof(line) - 1] = '\0';
    return probe_write(line);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPWSTR command, int show)
{
    WCHAR executable[MAX_PATH];
    WCHAR package[MAX_PATH];
    WCHAR root[MAX_PATH];
    WCHAR log_path[MAX_PATH];
    WCHAR arguments[128];
    WCHAR* role;
    WCHAR* storage;
    WCHAR* leaf;
    int child;
    BOOL coordinator;
    BOOL ok;

    (void)instance;
    (void)previous;
    (void)show;
    if (command == NULL || !probe_package_path(executable, package)) {
        return 2;
    }
    g_locks = wcscmp(command, L"--run-locks") == 0 ||
            wcscmp(command, L"--run-locks-internal") == 0;
    g_journal = wcscmp(command, L"--run-journal") == 0 ||
            wcscmp(command, L"--run-journal-internal") == 0;
    coordinator = g_locks || g_journal || wcscmp(command, L"--run-unicode") == 0 ||
            wcscmp(command, L"--run-unicode-internal") == 0;
    wcscpy(root, package);
    if (coordinator) {
        if (wcscmp(command, L"--run-unicode-internal") == 0 ||
                wcscmp(command, L"--run-locks-internal") == 0 ||
                wcscmp(command, L"--run-journal-internal") == 0) {
            g_storage = L"internal";
        }
        wcscpy(log_path, package);
        if (!probe_append(log_path, MAX_PATH, L"\\db-file-probe-") ||
                !probe_append(log_path, MAX_PATH, g_storage) ||
                !probe_append(log_path, MAX_PATH, L".log")) {
            return 2;
        }
        role = NULL;
    } else {
        if (wcslen(command) >= 128 ||
                wcsncmp(command, L"--child ", 8) != 0) {
            return 2;
        }
        wcscpy(arguments, command + 8);
        role = arguments;
        storage = wcschr(arguments, L' ');
        if (storage == NULL) {
            return 2;
        }
        *storage++ = L'\0';
        leaf = wcschr(storage, L' ');
        if (leaf == NULL) {
            return 2;
        }
        *leaf++ = L'\0';
        g_storage = storage;
        if ((wcscmp(role, L"create") != 0 && wcscmp(role, L"read") != 0 &&
                wcscmp(role, L"verify-b") != 0 && !probe_lock_role(role) &&
                !probe_journal_role(role)) ||
                !probe_leaf_valid(leaf) ||
                !probe_fixture_root(package, leaf, FALSE, root) ||
                !probe_marker(root, FALSE)) {
            return 2;
        }
        wcscpy(log_path, root);
        if (!probe_append(log_path, MAX_PATH, L"\\") ||
                !probe_append(log_path, MAX_PATH, role) ||
                !probe_append(log_path, MAX_PATH, L".log")) {
            return 2;
        }
    }
    /* Never overwrite evidence, including when a command is repeated. */
    g_log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE) {
        return 2;
    }
    ok = probe_write("DB_FILE_PROBE_V1\r\n") &&
            probe_identity(executable, package);
    if (ok) {
        if (coordinator) {
            ok = probe_coordinator(executable, package);
        } else {
            if (probe_journal_role(role)) {
                ok = probe_journal_child(root, role);
            } else {
                ok = probe_lock_role(role) ? probe_lock_child(root, role) :
                        probe_child(root, role);
            }
        }
    }
    /* On failure, let bounded child handshakes finish and reap every owned
     * child before reporting completion. Never leave a competitor running. */
    for (child = 0; child < 6; ++child) {
        if (g_children[child] != NULL && !g_child_waited[child]) {
            if (!probe_wait(child)) {
                ok = FALSE;
            }
        }
    }
    if (ok) {
        ok = probe_write(coordinator ? "DB_FILE_PROBE PASS\r\n" :
                "DB_FILE_CHILD PASS closed_before_exit=1\r\n");
    } else {
        probe_failure(NULL);
        probe_write(coordinator ? "DB_FILE_PROBE FAIL\r\n" :
                "DB_FILE_CHILD FAIL\r\n");
    }
    for (child = 0; child < 6; ++child) {
        if (g_children[child] != NULL) {
            CloseHandle(g_children[child]);
            g_children[child] = NULL;
        }
    }
    CloseHandle(g_log);
    g_log = INVALID_HANDLE_VALUE;
    return ok ? 0 : 1;
}
