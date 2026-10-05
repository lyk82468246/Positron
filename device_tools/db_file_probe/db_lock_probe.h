/* Test-only fixture helpers included by db_file_probe.c, not a public API.
 * File markers are CREATE_NEW in the exact coordinator-owned fixture.
 * Sleeps only poll explicit, flushed peer handshakes; never assume a lock
 * exists because an arbitrary amount of time has elapsed. */
#define PROBE_HANDSHAKE_MS 15000UL

static BOOL probe_lock_role(const WCHAR* role)
{
    return wcscmp(role, L"rw-reader") == 0 ||
            wcscmp(role, L"rw-writer") == 0 ||
            wcscmp(role, L"ww-owner") == 0 ||
            wcscmp(role, L"ww-contender") == 0 ||
            wcscmp(role, L"lock-verify") == 0;
}

static BOOL probe_signal(const WCHAR* root, const WCHAR* phase)
{
    WCHAR path[MAX_PATH];
    HANDLE marker;
    DWORD written;
    BOOL ok;

    wcscpy(path, root);
    if (!probe_append(path, MAX_PATH, L"\\") ||
            !probe_append(path, MAX_PATH, phase)) {
        return FALSE;
    }
    marker = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (marker == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    written = 0;
    ok = WriteFile(marker, "READY\r\n", 7, &written, NULL) && written == 7 &&
            FlushFileBuffers(marker);
    CloseHandle(marker);
    return ok;
}

static BOOL probe_await(const WCHAR* root, const WCHAR* phase)
{
    WCHAR path[MAX_PATH];
    HANDLE marker;
    DWORD start;
    DWORD count;
    char bytes[8];
    BOOL ok;

    wcscpy(path, root);
    if (!probe_append(path, MAX_PATH, L"\\") ||
            !probe_append(path, MAX_PATH, phase)) {
        return FALSE;
    }
    start = GetTickCount();
    do {
        marker = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (marker != INVALID_HANDLE_VALUE) {
            count = 0;
            ok = ReadFile(marker, bytes, sizeof(bytes), &count, NULL) &&
                    count == 7 && memcmp(bytes, "READY\r\n", 7) == 0;
            CloseHandle(marker);
            if (ok) {
                return TRUE;
            }
        }
        Sleep(25);
    } while ((DWORD)(GetTickCount() - start) < PROBE_HANDSHAKE_MS);
    return FALSE;
}

static BOOL probe_lock_state(PDbHandle db, int active, int transaction)
{
    PDbConnectionState state;

    memset(&state, 0, sizeof(state));
    state.size = sizeof(state);
    state.version = PDB_CONNECTION_STATE_VERSION;
    return PDb_GetConnectionState(db, &state) == PDB_OK &&
            state.transaction_active == active &&
            state.transaction_state == transaction && state.statement_count == 0;
}

static BOOL probe_busy(PDbHandle db, int result, const char* phase,
        int active, int transaction)
{
    PDbErrorInfo error;
    char line[256];

    memset(&error, 0, sizeof(error));
    error.size = sizeof(error);
    error.version = PDB_ERROR_INFO_VERSION;
    if (result != PDB_BUSY || PDb_GetErrorInfo(db, &error) != PDB_OK ||
            error.result != PDB_BUSY || error.category != PDB_ERROR_CATEGORY_BUSY ||
            error.sqlite_code != 5 || error.transaction_active != active ||
            error.cleanup_attempted != 0 ||
            !probe_lock_state(db, active, transaction)) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1,
            "lock_busy phase=%s result=%d category=%d native=%d extended=%d "
            "active=%d txn=%d statements=0 cleanup=0\r\n", phase, result,
            error.category, error.sqlite_code, error.sqlite_extended_code,
            active, transaction);
    line[sizeof(line) - 1] = '\0';
    return probe_write(line);
}

static BOOL probe_rw_reader(PDbHandle db, const WCHAR* root)
{
    g_phase = "rw_reader_snapshot";
    if (!probe_signal(root, L"rw-reader-open") ||
            !probe_await(root, L"rw-writer-open") ||
            PDb_Exec(db, "BEGIN") != PDB_OK ||
            !probe_row(db, 1, g_committed_text) ||
            !probe_lock_state(db, 1, PDB_TRANSACTION_READ) ||
            !probe_signal(root, L"rw-read-held") ||
            !probe_await(root, L"rw-commit-busy") ||
            !probe_row(db, 1, g_committed_text) ||
            !probe_scalar(db, "SELECT count(*) FROM samples", 1) ||
            !probe_write("rw_old_snapshot=PASS\r\n") ||
            PDb_Commit(db) != PDB_OK || !probe_state_idle(db) ||
            !probe_signal(root, L"rw-read-released") ||
            !probe_await(root, L"rw-commit-done") ||
            PDb_Exec(db, "BEGIN") != PDB_OK ||
            !probe_row(db, 4, "rw-committed") ||
            !probe_scalar(db, "SELECT count(*) FROM samples", 2) ||
            PDb_Commit(db) != PDB_OK ||
            !probe_write("rw_new_snapshot=PASS\r\n")) {
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_rw_writer(PDbHandle db, const WCHAR* root)
{
    g_phase = "rw_commit_busy";
    if (!probe_signal(root, L"rw-writer-open") ||
            !probe_await(root, L"rw-read-held") || PDb_Begin(db) != PDB_OK ||
            !probe_insert(db, 4, "rw-committed") ||
            !probe_busy(db, PDb_Commit(db), "rw-commit", 1, PDB_TRANSACTION_WRITE) ||
            !probe_signal(root, L"rw-commit-busy") ||
            !probe_await(root, L"rw-read-released") ||
            PDb_Commit(db) != PDB_OK || !probe_state_idle(db) ||
            !probe_signal(root, L"rw-commit-done") ||
            !probe_write("rw_commit_retry=PASS\r\n")) {
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_ww_owner(PDbHandle db, const WCHAR* root)
{
    g_phase = "ww_owner";
    if (!probe_signal(root, L"ww-owner-open") ||
            !probe_await(root, L"ww-contender-open") ||
            PDb_Begin(db) != PDB_OK || !probe_insert(db, 5, "ww-owner") ||
            !probe_signal(root, L"ww-write-held") ||
            !probe_await(root, L"ww-write-busy") ||
            PDb_Commit(db) != PDB_OK || !probe_state_idle(db) ||
            !probe_signal(root, L"ww-write-released") ||
            !probe_write("ww_owner_commit=PASS\r\n")) {
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_ww_contender(PDbHandle db, const WCHAR* root)
{
    g_phase = "ww_begin_busy";
    if (!probe_signal(root, L"ww-contender-open") ||
            !probe_await(root, L"ww-write-held") ||
            !probe_busy(db, PDb_Begin(db), "ww-begin", 0, PDB_TRANSACTION_NONE) ||
            !probe_signal(root, L"ww-write-busy") ||
            !probe_await(root, L"ww-write-released") ||
            PDb_Begin(db) != PDB_OK || !probe_row(db, 5, "ww-owner") ||
            !probe_insert(db, 6, "ww-contender") || PDb_Commit(db) != PDB_OK ||
            !probe_write("ww_begin_retry=PASS\r\n")) {
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_lock_child(const WCHAR* root, const WCHAR* role)
{
    WCHAR wide_path[MAX_PATH];
    char path[PROBE_PATH_BYTES];
    char line[PROBE_PATH_BYTES + 32];
    PDbHandle db;
    BOOL ok;

    db = NULL;
    ok = FALSE;
    wcscpy(wide_path, root);
    g_phase = "lock_open";
    if (!probe_append(wide_path, MAX_PATH,
            L"\\\x4e2d\x6587\x76ee\x5f55\\\x6570\x636e\x5e93.sqlite") ||
            !probe_utf8(wide_path, path, sizeof(path))) {
        goto lock_done;
    }
    _snprintf(line, sizeof(line) - 1, "fixture_db_utf8=%s\r\n", path);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line) || !probe_open(path, &db) ||
            PDb_Exec(db, "PRAGMA max_page_count=512") != PDB_OK) {
        goto lock_done;
    }
    if (wcscmp(role, L"rw-reader") == 0) {
        ok = probe_rw_reader(db, root);
    } else if (wcscmp(role, L"rw-writer") == 0) {
        ok = probe_rw_writer(db, root);
    } else if (wcscmp(role, L"ww-owner") == 0) {
        ok = probe_ww_owner(db, root);
    } else if (wcscmp(role, L"ww-contender") == 0) {
        ok = probe_ww_contender(db, root);
    } else if (wcscmp(role, L"lock-verify") == 0) {
        g_phase = "lock_cold_verify";
        ok = probe_row(db, 1, g_committed_text) &&
                probe_row(db, 4, "rw-committed") &&
                probe_row(db, 5, "ww-owner") &&
                probe_row(db, 6, "ww-contender") &&
                probe_scalar(db, "SELECT count(*) FROM samples", 4) &&
                PDb_ApplyMigration(db, 1, "") == PDB_OK &&
                probe_write("locks_cold_reopen=PASS rows=4 schema=1\r\n");
    }
    if (ok) {
        ok = probe_integrity(db) && probe_state_idle(db) &&
                probe_write("lock_integrity=ok transaction_idle=1 statements=0\r\n");
    }
lock_done:
    if (!ok) {
        probe_failure(db);
    }
    PDb_Close(db);
    return ok;
}

static BOOL probe_lock_sequence(const WCHAR* executable, const WCHAR* leaf)
{
    DWORD child_pid;
    BOOL first;
    BOOL second;

    if (!probe_write("lock_suite=rw-ww-v1\r\n") ||
            !probe_spawn(executable, leaf, L"create", 0, &child_pid) ||
            !probe_launch(executable, leaf, L"rw-reader", 1, &child_pid) ||
            !probe_launch(executable, leaf, L"rw-writer", 2, &child_pid)) {
        return FALSE;
    }
    first = probe_wait(1);
    second = probe_wait(2);
    if (!first || !second ||
            !probe_launch(executable, leaf, L"ww-owner", 3, &child_pid) ||
            !probe_launch(executable, leaf, L"ww-contender", 4, &child_pid)) {
        return FALSE;
    }
    first = probe_wait(3);
    second = probe_wait(4);
    return first && second &&
            probe_spawn(executable, leaf, L"lock-verify", 5, &child_pid) &&
            probe_write("lock_processes=6 rw_pair=concurrent ww_pair=concurrent "
            "cold_verifier_after_all_exits=1\r\n");
}
