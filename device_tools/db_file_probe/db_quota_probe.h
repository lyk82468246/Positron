/* Test consumer only: a real file page quota, never volume exhaustion.
 * No injected VFS failures, private metadata or product implementation. */
#define PROBE_QUOTA_PAGES 32

static BOOL probe_quota_role(const WCHAR* role)
{
    return wcscmp(role, L"quota-writer") == 0 ||
            wcscmp(role, L"quota-reopen") == 0 ||
            wcscmp(role, L"quota-verify") == 0;
}

static BOOL probe_quota_native_truncate(const WCHAR* root)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    unsigned char bytes[4096];
    DWORD written;
    DWORD error;
    DWORD size;
    DWORD high;
    BOOL success;
    char line[192];

    /* A separate, new scratch file diagnoses the same guest filesystem API.
     * Never truncate, rewrite or delete the SQLite database to repair it. */
    wcscpy(path, root);
    if (!probe_append(path, MAX_PATH, L"\\quota-native-truncate.bin")) {
        return FALSE;
    }
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    memset(bytes, 0x41, sizeof(bytes));
    written = 0;
    success = WriteFile(file, bytes, sizeof(bytes), &written, NULL) &&
            written == sizeof(bytes) && FlushFileBuffers(file);
    if (!success) {
        CloseHandle(file);
        return FALSE;
    }
    success = SetFilePointer(file, 2048, NULL, FILE_BEGIN) == 2048;
    if (success) {
        success = SetEndOfFile(file);
    }
    error = success ? 0 : GetLastError();
    if (success) {
        success = FlushFileBuffers(file);
        if (!success) {
            error = GetLastError();
        }
    }
    high = 0;
    size = GetFileSize(file, &high);
    CloseHandle(file);
    _snprintf(line, sizeof(line) - 1,
            "quota_native_truncate success=%d win32=%lu bytes=%lu high=%lu requested=2048\r\n",
            success ? 1 : 0, (unsigned long)error, (unsigned long)size,
            (unsigned long)high);
    line[sizeof(line) - 1] = '\0';
    return probe_write(line);
}

static BOOL probe_quota_blob(PDbHandle db, BOOL insert)
{
    PDbStmtHandle stmt;
    const void* bytes;
    BOOL ok;
    int finalized;

    stmt = NULL;
    ok = PDb_Prepare(db, insert ? "INSERT INTO quota_samples VALUES(1,?1)" :
            "SELECT payload FROM quota_samples WHERE id=1", &stmt) == PDB_OK;
    if (ok && insert) {
        ok = PDb_BindBlob(stmt, 1, g_blob, sizeof(g_blob)) == PDB_OK &&
                PDb_Step(stmt) == PDB_STEP_DONE;
    } else if (ok) {
        ok = PDb_Step(stmt) == PDB_STEP_ROW &&
                PDb_ColumnType(stmt, 0) == PDB_VALUE_BLOB &&
                PDb_ColumnBytes(stmt, 0) == sizeof(g_blob);
        bytes = ok ? PDb_ColumnBlob(stmt, 0) : NULL;
        ok = ok && bytes != NULL && memcmp(bytes, g_blob, sizeof(g_blob)) == 0 &&
                PDb_Step(stmt) == PDB_STEP_DONE;
    }
    finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return ok && finalized == PDB_OK;
}

static BOOL probe_quota_full(PDbHandle db)
{
    PDbStmtHandle stmt;
    PDbErrorInfo error;
    PDbConnectionState state;
    int result;
    int finalized;
    BOOL ok;
    char line[256];

    stmt = NULL;
    g_phase = "quota_step_full";
    if (PDb_Begin(db) != PDB_OK ||
            PDb_Exec(db, "UPDATE samples SET label='quota-uncommitted'") != PDB_OK ||
            !probe_quota_blob(db, TRUE) ||
            PDb_Prepare(db, "INSERT INTO quota_samples VALUES(2,zeroblob(262144))",
                    &stmt) != PDB_OK) {
        return FALSE;
    }
    result = PDb_Step(stmt);
    memset(&error, 0, sizeof(error));
    error.size = sizeof(error);
    error.version = PDB_ERROR_INFO_VERSION;
    memset(&state, 0, sizeof(state));
    state.size = sizeof(state);
    state.version = PDB_CONNECTION_STATE_VERSION;
    /* Capture the root failure before Finalize or any other status call.
     * Do not issue ROLLBACK to hide a wrong automatic rollback state. */
    ok = result == PDB_ERROR && PDb_GetErrorInfo(db, &error) == PDB_OK &&
            error.result == PDB_ERROR && error.category == PDB_ERROR_CATEGORY_FULL &&
            error.sqlite_code == 13 && (error.sqlite_extended_code & 255) == 13 &&
            error.transaction_active == 0 && error.cleanup_attempted == 0 &&
            error.cleanup_result == PDB_OK && error.message[0] != '\0' &&
            PDb_GetConnectionState(db, &state) == PDB_OK &&
            state.transaction_active == 0 && state.transaction_state == PDB_TRANSACTION_NONE &&
            state.statement_count == 1;
    if (ok) {
        _snprintf(line, sizeof(line) - 1,
                "quota_full stage=step result=%d category=%d native=%d extended=%d "
                "active=0 txn=0 statements=1 cleanup=0 cleanup_result=0\r\n",
                result, error.category, error.sqlite_code, error.sqlite_extended_code);
        line[sizeof(line) - 1] = '\0';
        ok = probe_write(line);
    }
    finalized = PDb_Finalize(stmt);
    return ok && finalized == PDB_ERROR && probe_state_idle(db) &&
            probe_write("quota_finalize=PASS result=-1 active=0 txn=0 statements=0\r\n");
}

static BOOL probe_quota_pages(PDbHandle db)
{
    PDbStmtHandle stmt;
    __int64 pages;
    BOOL ok;
    int finalized;

    stmt = NULL;
    ok = PDb_Prepare(db, "PRAGMA page_count", &stmt) == PDB_OK &&
            PDb_Step(stmt) == PDB_STEP_ROW &&
            PDb_ColumnType(stmt, 0) == PDB_VALUE_INTEGER;
    pages = ok ? PDb_ColumnInt64(stmt, 0) : 0;
    ok = ok && pages > 0 && pages <= PROBE_QUOTA_PAGES &&
            PDb_Step(stmt) == PDB_STEP_DONE;
    finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return ok && finalized == PDB_OK;
}

static BOOL probe_quota_child(const WCHAR* root, const WCHAR* role)
{
    WCHAR wide_path[MAX_PATH];
    char path[PROBE_PATH_BYTES];
    char line[PROBE_PATH_BYTES + 32];
    PDbHandle db;
    BOOL ok;
    BOOL writer;
    BOOL verify;

    db = NULL;
    ok = FALSE;
    writer = wcscmp(role, L"quota-writer") == 0;
    verify = wcscmp(role, L"quota-verify") == 0;
    g_phase = "quota_open";
    if (!probe_journal_path(root, wide_path) ||
            !probe_utf8(wide_path, path, sizeof(path))) {
        goto quota_done;
    }
    _snprintf(line, sizeof(line) - 1, "fixture_db_utf8=%s\r\n", path);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line) || !probe_open(path, &db) ||
            !probe_scalar(db, "PRAGMA page_size", 4096) ||
            !probe_scalar(db, "PRAGMA synchronous", 2) ||
            !probe_scalar(db, "PRAGMA max_page_count=32", PROBE_QUOTA_PAGES)) {
        goto quota_done;
    }
    if (writer) {
        g_phase = "quota_schema_seed";
        if (!probe_quota_native_truncate(root) ||
                PDb_ApplyMigration(db, 2, "CREATE TABLE quota_samples("
                "id INTEGER PRIMARY KEY,payload BLOB NOT NULL)") != PDB_OK ||
                PDb_Exec(db, "PRAGMA cache_size=4") != PDB_OK ||
                PDb_Exec(db, "PRAGMA cache_spill=ON") != PDB_OK ||
                !probe_quota_full(db)) {
            goto quota_done;
        }
    }
    g_phase = "quota_exact_schema_data";
    /* Lower version rejection before idempotent current version prevents
     * repairing lost metadata with an empty higher migration. */
    if (PDb_ApplyMigration(db, 1, "") != PDB_STATE ||
            PDb_ApplyMigration(db, 2, "") != PDB_OK ||
            !probe_row(db, 1, g_committed_text) ||
            !probe_scalar(db, "SELECT count(*) FROM samples", verify ? 2 : 1) ||
            !probe_scalar(db, "SELECT count(*) FROM quota_samples", verify ? 1 : 0) ||
            !probe_quota_pages(db)) {
        goto quota_done;
    }
    if (writer) {
        ok = probe_write("quota_rollback=PASS prior_text_restored=1 prior_insert_absent=1 schema=2\r\n");
    } else if (verify) {
        ok = probe_row(db, 8, "after-full-reopen") && probe_quota_blob(db, FALSE) &&
                probe_write("quota_cold_verify=PASS sample_rows=2 quota_rows=1 schema=2\r\n");
    } else {
        g_phase = "quota_new_process_retry";
        ok = probe_write("quota_reopen=PASS sample_rows=1 quota_rows=0 schema=2\r\n") &&
                PDb_Begin(db) == PDB_OK &&
                probe_insert(db, 8, "after-full-reopen") && probe_quota_blob(db, TRUE) &&
                PDb_Commit(db) == PDB_OK &&
                probe_write("quota_retry_commit=PASS exact_blob=PASS\r\n");
    }
    ok = ok && probe_quota_pages(db) && probe_integrity(db) &&
            probe_lock_state(db, 0, PDB_TRANSACTION_NONE) &&
            probe_write("quota_integrity=ok active=0 txn=0 statements=0 page_cap=32 page_size=4096\r\n");
quota_done:
    if (!ok) {
        probe_failure(db);
    }
    PDb_Close(db);
    return ok;
}

static BOOL probe_quota_sequence(const WCHAR* executable, const WCHAR* leaf)
{
    DWORD child_pid;

    return probe_write("quota_suite=file-page-full-v1\r\n") &&
            probe_spawn(executable, leaf, L"create", 0, &child_pid) &&
            probe_spawn(executable, leaf, L"quota-writer", 1, &child_pid) &&
            probe_spawn(executable, leaf, L"quota-reopen", 2, &child_pid) &&
            probe_spawn(executable, leaf, L"quota-verify", 3, &child_pid) &&
            probe_write("quota_processes=4 writer_exited_before_reopen=1 "
                    "reopener_exited_before_verifier=1\r\n");
}
