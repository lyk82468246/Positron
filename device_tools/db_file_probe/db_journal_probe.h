/* Test consumer only. Real SQLite spill/journal; no synthetic file writes.
 * Coordinator reads/copies owned files but never opens the database. */
#define PROBE_JOURNAL_EXIT 0x5044424AUL
#define PROBE_JOURNAL_DB_MAX 524288UL
#define PROBE_JOURNAL_MAX 1048576UL

static BOOL probe_journal_role(const WCHAR* role)
{
    return wcscmp(role, L"journal-seed") == 0 ||
            wcscmp(role, L"journal-writer") == 0 ||
            wcscmp(role, L"journal-recover") == 0 ||
            wcscmp(role, L"journal-verify") == 0;
}

static BOOL probe_journal_path(const WCHAR* root, WCHAR* path)
{
    wcscpy(path, root);
    return probe_append(path, MAX_PATH,
            L"\\\x4e2d\x6587\x76ee\x5f55\\\x6570\x636e\x5e93.sqlite");
}

static BOOL probe_journal_rows(PDbHandle db)
{
    PDbStmtHandle stmt;
    const unsigned char* bytes;
    int row;
    int index;
    int result;
    BOOL ok;

    stmt = NULL;
    ok = PDb_Prepare(db, "SELECT id,payload FROM journal_samples ORDER BY id",
            &stmt) == PDB_OK;
    for (row = 1; ok && row <= 64; ++row) {
        ok = PDb_Step(stmt) == PDB_STEP_ROW &&
                PDb_ColumnType(stmt, 0) == PDB_VALUE_INTEGER &&
                PDb_ColumnInt64(stmt, 0) == row &&
                PDb_ColumnType(stmt, 1) == PDB_VALUE_BLOB &&
                PDb_ColumnBytes(stmt, 1) == 2048;
        bytes = ok ? (const unsigned char*)PDb_ColumnBlob(stmt, 1) : NULL;
        ok = ok && bytes != NULL;
        for (index = 0; ok && index < 2048; ++index) {
            ok = bytes[index] == 0x41;
        }
    }
    if (ok) {
        ok = PDb_Step(stmt) == PDB_STEP_DONE;
    }
    result = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
    return ok && result == PDB_OK;
}

static BOOL probe_journal_seed(PDbHandle db)
{
    unsigned char payload[2048];
    PDbStmtHandle stmt;
    int row;
    int finalized;
    BOOL ok;

    stmt = NULL;
    memset(payload, 0x41, sizeof(payload));
    ok = PDb_ApplyMigration(db, 2, "CREATE TABLE journal_samples("
            "id INTEGER PRIMARY KEY,payload BLOB NOT NULL)") == PDB_OK &&
            PDb_Begin(db) == PDB_OK;
    for (row = 1; ok && row <= 64; ++row) {
        ok = PDb_Prepare(db, "INSERT INTO journal_samples VALUES(?1,?2)",
                &stmt) == PDB_OK && PDb_BindInt64(stmt, 1, row) == PDB_OK &&
                PDb_BindBlob(stmt, 2, payload, sizeof(payload)) == PDB_OK &&
                PDb_Step(stmt) == PDB_STEP_DONE;
        finalized = stmt == NULL ? PDB_OK : PDb_Finalize(stmt);
        stmt = NULL;
        ok = ok && finalized == PDB_OK;
    }
    return ok && PDb_Commit(db) == PDB_OK && probe_journal_rows(db) &&
            probe_write("journal_seed=PASS rows=64 blob_bytes=2048 schema=2\r\n");
}

static BOOL probe_journal_child(const WCHAR* root, const WCHAR* role)
{
    WCHAR wide_path[MAX_PATH];
    char path[PROBE_PATH_BYTES];
    char line[PROBE_PATH_BYTES + 32];
    PDbHandle db;
    BOOL ok;
    BOOL verify;

    db = NULL;
    ok = FALSE;
    verify = wcscmp(role, L"journal-verify") == 0;
    g_phase = "journal_open";
    if (!probe_journal_path(root, wide_path) ||
            !probe_utf8(wide_path, path, sizeof(path))) {
        goto journal_done;
    }
    _snprintf(line, sizeof(line) - 1, "fixture_db_utf8=%s\r\n", path);
    line[sizeof(line) - 1] = '\0';
    if (!probe_write(line) || !probe_open(path, &db) ||
            PDb_Exec(db, "PRAGMA max_page_count=128") != PDB_OK ||
            !probe_scalar(db, "PRAGMA synchronous", 2) ||
            !probe_scalar(db, "PRAGMA page_size", 4096)) {
        goto journal_done;
    }
    if (wcscmp(role, L"journal-seed") == 0) {
        g_phase = "journal_seed";
        ok = probe_journal_seed(db);
    } else if (wcscmp(role, L"journal-writer") == 0) {
        g_phase = "journal_dirty_spill";
        if (PDb_Exec(db, "PRAGMA cache_size=4") != PDB_OK ||
                PDb_Exec(db, "PRAGMA cache_spill=ON") != PDB_OK ||
                PDb_Begin(db) != PDB_OK ||
                PDb_Exec(db, "UPDATE samples SET label='uncommitted'") != PDB_OK ||
                PDb_Exec(db, "UPDATE journal_samples SET payload=zeroblob(2048)") != PDB_OK ||
                !probe_scalar(db, "SELECT count(*) FROM journal_samples "
                        "WHERE payload=zeroblob(2048)", 64) ||
                !probe_lock_state(db, 1, PDB_TRANSACTION_WRITE) ||
                !probe_write("journal_writer_ready active=1 txn=2 statements=0 "
                        "commit=0 rollback=0 close=0\r\n") ||
                !probe_signal(root, L"journal-writer-ready")) {
            goto journal_done;
        }
        /* No success/normal exit: coordinator must end this exact owned child
         * while its real write transaction is open. Deadline fails closed. */
        probe_await(root, L"journal-never-release");
        g_phase = "journal_writer_not_terminated";
        goto journal_done;
    } else {
        g_phase = "journal_recovery_exact_data";
        /* Check the lower version first: an empty higher migration could
         * otherwise repair a lost version instead of detecting it. */
        ok = PDb_ApplyMigration(db, 1, "") == PDB_STATE &&
                PDb_ApplyMigration(db, 2, "") == PDB_OK &&
                probe_journal_rows(db) && probe_row(db, 1, g_committed_text) &&
                probe_scalar(db, "SELECT count(*) FROM samples", verify ? 2 : 1);
        if (ok && verify) {
            ok = probe_row(db, 7, "after-journal-recovery") &&
                    probe_write("journal_cold_verify=PASS rows=64 sample_rows=2 schema=2\r\n");
        } else if (ok) {
            ok = probe_write("journal_recovered=PASS rows=64 sample_rows=1 schema=2\r\n") &&
                    PDb_Begin(db) == PDB_OK &&
                    probe_insert(db, 7, "after-journal-recovery") &&
                    PDb_Commit(db) == PDB_OK &&
                    probe_write("journal_post_recovery_commit=PASS\r\n");
        }
    }
    ok = ok && probe_integrity(db) && probe_state_idle(db) &&
            probe_write("journal_integrity=ok transaction_idle=1 statements=0\r\n");
journal_done:
    if (!ok) {
        probe_failure(db);
    }
    PDb_Close(db);
    return ok;
}

static BOOL probe_journal_copy(const WCHAR* source, const WCHAR* root,
        const WCHAR* name, DWORD limit)
{
    WCHAR destination[MAX_PATH];
    HANDLE input;
    HANDLE output;
    unsigned char bytes[4096];
    DWORD size;
    DWORD high;
    DWORD read;
    DWORD written;
    DWORD total;
    BOOL ok;

    wcscpy(destination, root);
    if (!probe_append(destination, MAX_PATH, L"\\") ||
            !probe_append(destination, MAX_PATH, name)) {
        return FALSE;
    }
    input = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    high = 0;
    size = GetFileSize(input, &high);
    ok = high == 0 && size > 0 && size <= limit;
    output = ok ? CreateFileW(destination, GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    ok = ok && output != INVALID_HANDLE_VALUE;
    total = 0;
    while (ok && total < size) {
        read = 0;
        written = 0;
        ok = ReadFile(input, bytes, sizeof(bytes), &read, NULL) && read > 0 &&
                read <= size - total && WriteFile(output, bytes, read,
                &written, NULL) && written == read;
        total += read;
    }
    if (ok) {
        ok = total == size && FlushFileBuffers(output);
    }
    if (output != INVALID_HANDLE_VALUE) {
        CloseHandle(output);
    }
    CloseHandle(input);
    return ok;
}

static unsigned long probe_journal_be32(const unsigned char* bytes)
{
    return ((unsigned long)bytes[0] << 24) | ((unsigned long)bytes[1] << 16) |
            ((unsigned long)bytes[2] << 8) | bytes[3];
}

static BOOL probe_journal_header(const WCHAR* path)
{
    static const unsigned char magic[8] = {0xd9,0xd5,0x05,0xf9,0x20,0xa1,0x63,0xd7};
    HANDLE file;
    unsigned char header[28];
    DWORD count;
    DWORD high;
    DWORD size;
    unsigned long records;
    unsigned long pages;
    unsigned long sector;
    BOOL ok;
    char line[256];

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    count = 0;
    high = 0;
    size = GetFileSize(file, &high);
    ok = high == 0 && size > 512 && size <= PROBE_JOURNAL_MAX &&
            ReadFile(file, header, sizeof(header), &count, NULL) &&
            count == sizeof(header) && memcmp(header, magic, sizeof(magic)) == 0;
    CloseHandle(file);
    if (!ok) {
        return FALSE;
    }
    records = probe_journal_be32(header + 8);
    pages = probe_journal_be32(header + 16);
    sector = probe_journal_be32(header + 20);
    if (records == 0 || records > 128 || pages == 0 || pages > 128 ||
            sector < 512 || sector > 65536 || (sector & (sector - 1)) != 0 ||
            probe_journal_be32(header + 24) != 4096 ||
            size < sector + records * 4104UL) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1, "hot_journal magic=valid bytes=%lu "
            "records=%lu original_pages=%lu sector=%lu page_size=4096 writer_exited=1\r\n",
            (unsigned long)size, records, pages, sector);
    line[sizeof(line) - 1] = '\0';
    return probe_write(line);
}

static BOOL probe_journal_sequence(const WCHAR* executable,
        const WCHAR* leaf, const WCHAR* root)
{
    WCHAR database[MAX_PATH];
    WCHAR journal[MAX_PATH];
    DWORD child_pid;
    DWORD waited;
    DWORD exit_code;
    char line[192];

    g_phase = "journal_seed_sequence";
    if (!probe_write("journal_suite=hot-rollback-v1\r\n") ||
            !probe_spawn(executable, leaf, L"create", 0, &child_pid) ||
            !probe_spawn(executable, leaf, L"journal-seed", 1, &child_pid) ||
            !probe_journal_path(root, database) ||
            !probe_journal_copy(database, root, L"before.sqlite", PROBE_JOURNAL_DB_MAX) ||
            !probe_launch(executable, leaf, L"journal-writer", 2, &child_pid) ||
            !probe_await(root, L"journal-writer-ready")) {
        return FALSE;
    }
    g_phase = "journal_owned_termination";
    /* Slot 2 was just created here. No OpenProcess, arbitrary PID or reset. */
    if (WaitForSingleObject(g_children[2], 0) != WAIT_TIMEOUT ||
            !TerminateProcess(g_children[2], PROBE_JOURNAL_EXIT)) {
        return FALSE;
    }
    waited = WaitForSingleObject(g_children[2], 5000);
    g_child_waited[2] = waited == WAIT_OBJECT_0;
    exit_code = STILL_ACTIVE;
    if (!g_child_waited[2] || !GetExitCodeProcess(g_children[2], &exit_code) ||
            exit_code != PROBE_JOURNAL_EXIT) {
        return FALSE;
    }
    _snprintf(line, sizeof(line) - 1,
            "child role=journal-writer pid=%lu wait=0 exit=%lu completed=1 terminated_owned=1\r\n",
            (unsigned long)g_child_pids[2], (unsigned long)exit_code);
    line[sizeof(line) - 1] = '\0';
    wcscpy(journal, database);
    g_phase = "journal_before_recovery_snapshot";
    if (!probe_write(line) || !probe_append(journal, MAX_PATH, L"-journal") ||
            !probe_journal_header(journal) ||
            !probe_journal_copy(database, root, L"hot.sqlite", PROBE_JOURNAL_DB_MAX) ||
            !probe_journal_copy(journal, root, L"hot.sqlite-journal", PROBE_JOURNAL_MAX) ||
            !probe_write("journal_snapshots=PASS before_and_hot_pair=1 recovery_not_started=1\r\n") ||
            !probe_spawn(executable, leaf, L"journal-recover", 3, &child_pid) ||
            !probe_spawn(executable, leaf, L"journal-verify", 4, &child_pid)) {
        return FALSE;
    }
    return probe_write("journal_processes=5 writer_terminated_before_recovery=1 "
            "snapshots_before_recovery=1 cold_verifier_after_recovery_exit=1\r\n");
}
