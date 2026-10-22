/* Only fixtures/platform callbacks/assertions; execution lives in DB DLL. */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "positron_db.h"

static const char* restricted_error = "Restricted DB fixture failed";
const char* test1363_db_restricted_last_error(void) { return restricted_error; }
const char* test1366_db_migration_last_error(void) { return restricted_error; }
typedef struct RestrictedFixture {
    PDbHandle db;
    volatile LONG cancelled;
    int rows;
    int reject;
    int reentry;
    int cancel_on_row;
    int typed;
    HANDLE progress_seen;
    int polls;
    __int64 last_integer;
} RestrictedFixture;
static int restricted_cancel(void* data)
{
    RestrictedFixture* f;
    f = (RestrictedFixture*)data;
    /* Test-only checkpoint observation, not application work or DB reentry.
     * First three polls precede execution; fifth is inside the long VM. */
    if (f->progress_seen != NULL && ++f->polls == 5) SetEvent(f->progress_seen);
    return InterlockedCompareExchange((LONG*)&f->cancelled, 0, 0) != 0;
}
static DWORD WINAPI restricted_cancel_worker(void* data)
{
    RestrictedFixture* f;
    f = (RestrictedFixture*)data;
    if (WaitForSingleObject(f->progress_seen, 5000) == WAIT_OBJECT_0) {
        InterlockedExchange((LONG*)&f->cancelled, 1);
    }
    return 0;
}
static int restricted_row(void* data, int index,
        const PDbRestrictedValue* values, int columns)
{
    RestrictedFixture* f;
    f = (RestrictedFixture*)data;
    (void)index;
    f->rows++;
    if (columns > 0 && values[0].type == PDB_VALUE_INTEGER) f->last_integer = values[0].integer;
    if (f->reentry) {
        if (PDb_Exec(f->db, "DROP TABLE notes") != PDB_STATE) return PDB_STATE;
        PDb_Close(f->db); /* prohibited callback close must not free it */
    }
    if (f->typed && (columns != 4 || values[0].type != PDB_VALUE_INTEGER ||
            values[0].integer != (((__int64)1 << 60) + 3) ||
            values[1].type != PDB_VALUE_REAL || values[1].real != 1.25 ||
            values[2].type != PDB_VALUE_TEXT || values[2].text_bytes != 6 ||
            memcmp(values[2].text, "\xe4\xb8\xad\xe6\x96\x87", 6) ||
            values[3].type != PDB_VALUE_NULL)) return PDB_STATE;
    if (f->cancel_on_row) InterlockedExchange((LONG*)&f->cancelled, 1);
    return f->reject;
}
static void restricted_statement(PDbRestrictedStatement* s, const char* sql, int kind)
{
    memset(s, 0, sizeof(*s)); s->sql = sql; s->sql_bytes = (int)strlen(sql); s->kind = kind;
}
static int restricted_count(PDbHandle db)
{
    PDbStmtHandle stmt;
    int count;
    stmt = NULL; count = -1;
    if (PDb_Prepare(db, "SELECT count(*) FROM notes", &stmt) == PDB_OK &&
            PDb_Step(stmt) == PDB_STEP_ROW) count = (int)PDb_ColumnInt64(stmt, 0);
    if (stmt != NULL) PDb_Finalize(stmt);
    return count;
}
static int restricted_run(PDbHandle db, PDbRestrictedStatement* s, int count,
        PDbRestrictedOptions* o, PDbRestrictedReport* r, int expected)
{
    PDbRestrictedReport before;
    PDbConnectionState state;
    int rc;
    before = *r;
    rc = PDb_ExecuteRestricted(db, s, count, o, r);
    if (rc != expected || (rc != PDB_OK && memcmp(&before, r, sizeof(before)))) return 0;
    memset(&state, 0, sizeof(state)); state.size = sizeof(state); state.version = 1;
    return PDb_GetConnectionState(db, &state) == PDB_OK && !state.transaction_active && state.statement_count == 0;
}
#define REQUIRE(test, why) do { if (!(test)) { restricted_error = why; goto done; } } while (0)
BOOL test1363_db_restricted_contract(void)
{
    static const char* const denied[] = {
        "ATTACH ':memory:' AS other", "DETACH main", "PRAGMA user_version",
        "PRAGMA writable_schema=1", "BEGIN", "COMMIT", "ROLLBACK",
        "SAVEPOINT x", "RELEASE x", "CREATE TABLE stolen(x)",
        "DROP TABLE notes", "ALTER TABLE notes ADD COLUMN y", "VACUUM",
        "CREATE TEMP TABLE t(x)", "SELECT * FROM __PDB_meta",
        "DELETE FROM __pdb_meta", "SELECT * FROM sqlite_master",
        "SELECT * FROM sqlite_schema", "SELECT load_extension('x')",
        "SELECT * FROM pragma_table_info('notes')", "SELECT randomblob(100)",
        "SELECT * FROM leak", "INSERT INTO trapped VALUES(1)",
        "SELECT 1; DELETE FROM notes", "UPDATE notes SET body='x' RETURNING body"
    };
    PDbHandle db;
    PDbHandle sync;
    PDbStmtHandle old_stmt;
    PDbRestrictedOptions o;
    PDbRestrictedReport r;
    PDbRestrictedStatement s[8];
    PDbRestrictedValue params[4];
    PDbErrorInfo error;
    PDbErrorInfo sticky;
    RestrictedFixture f;
    HANDLE cancel_thread;
    unsigned int i;
    BOOL ok;
    db = sync = NULL; old_stmt = NULL; ok = FALSE; cancel_thread = NULL;
    memset(&f, 0, sizeof(f)); memset(&o, 0, sizeof(o)); memset(&r, 0, sizeof(r));
    o.size = sizeof(o); o.version = 1; o.vm_budget = PDB_RESTRICTED_VM_MAX;
    o.cancelled = restricted_cancel; o.row = restricted_row; o.user_data = &f;
    r.size = sizeof(r); r.version = 1;
    REQUIRE(PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK, "open local");
    f.db = db;
    REQUIRE(PDb_ApplyMigration(db, 1,
        "CREATE TABLE notes(id INTEGER PRIMARY KEY,body TEXT);"
        "CREATE VIEW leak AS SELECT * FROM __pdb_meta;"
        "CREATE TABLE trapped(x);CREATE TRIGGER trap AFTER INSERT ON trapped "
        "BEGIN DELETE FROM __pdb_meta; END;"
        "CREATE TABLE parent(x PRIMARY KEY);CREATE TABLE child(x REFERENCES parent(x) "
        "DEFERRABLE INITIALLY DEFERRED);") == PDB_OK, "trusted migration fixture");
    for (i = 0; i < sizeof(denied) / sizeof(denied[0]); i++) {
        restricted_statement(s, denied[i], strstr(denied[i], "SELECT") == denied[i] ?
                PDB_RESTRICTED_QUERY : PDB_RESTRICTED_EXECUTE);
        REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_SQL_REJECTED), denied[i]);
    }
    restricted_statement(s, "SELECT ?1,?2,?3,?4", PDB_RESTRICTED_QUERY);
    memset(params, 0, sizeof(params)); params[0].type = PDB_VALUE_INTEGER;
    params[0].integer = ((__int64)1 << 60) + 3;
    params[1].type = PDB_VALUE_REAL; params[1].real = 1.25;
    params[2].type = PDB_VALUE_TEXT; params[2].text = "\xe4\xb8\xad\xe6\x96\x87"; params[2].text_bytes = 6;
    s[0].params = params; s[0].param_count = 4; f.typed = f.reentry = 1;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_OK) && r.row_count == 1 && f.rows == 1, "typed/reentry/close");
    f.typed = f.reentry = 0;
    memset(&error, 0, sizeof(error)); error.size = sizeof(error); error.version = 1;
    REQUIRE(PDb_GetErrorInfo(db, &error) == PDB_OK && error.result == PDB_SQL_REJECTED, "success preserves sticky failure");
    sticky = error;
    restricted_statement(s, "INSERT INTO notes VALUES(?1,?2)", PDB_RESTRICTED_EXECUTE);
    params[0].integer = 1; params[1] = params[2]; s[0].params = params; s[0].param_count = 2;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_OK) && r.changes == 1 && restricted_count(db) == 1, "bound insert commit");
    REQUIRE(PDb_GetErrorInfo(db, &error) == PDB_OK && !memcmp(&error, &sticky, sizeof(error)), "sticky preservation");
    restricted_statement(s, "INSERT INTO child VALUES(1)", PDB_RESTRICTED_EXECUTE);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_ERROR) &&
            PDb_GetErrorInfo(db, &error) == PDB_OK && error.category == PDB_ERROR_CATEGORY_CONSTRAINT &&
            error.cleanup_attempted && error.cleanup_result == PDB_OK, "COMMIT failure owned rollback");
    restricted_statement(s, "SELECT count(*) FROM child", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_OK) && f.last_integer == 0, "no deferred-constraint partial write");
    restricted_statement(s, "INSERT INTO notes VALUES(2,'provisional')", PDB_RESTRICTED_EXECUTE);
    restricted_statement(s + 1, "INSERT INTO notes VALUES(1,'duplicate')", PDB_RESTRICTED_EXECUTE);
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_ERROR) && restricted_count(db) == 1, "batch constraint rollback");
    restricted_statement(s + 1, "SELECT body FROM notes", PDB_RESTRICTED_QUERY);
    f.reject = PDB_LIMIT;
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_LIMIT) && restricted_count(db) == 1, "result rejection rollback");
    f.reject = 0; f.cancel_on_row = 1;
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_STATE) && restricted_count(db) == 1, "row cancel before commit");
    f.cancel_on_row = 0;
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_STATE), "pre-cancel no work");
    InterlockedExchange((LONG*)&f.cancelled, 0);
    restricted_statement(s + 1, "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<17) SELECT x FROM n", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_LIMIT) && restricted_count(db) == 1, "17 rows rollback, never truncate");
    restricted_statement(s + 1, "SELECT printf('%5000s','x')", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_LIMIT) && restricted_count(db) == 1, "text result budget rollback");
    restricted_statement(s + 1, "SELECT x'00'", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_NOT_SUPPORTED) && restricted_count(db) == 1, "BLOB result rollback");
    restricted_statement(s + 1, "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n) SELECT sum(x) FROM n", PDB_RESTRICTED_QUERY);
    o.vm_budget = 1000;
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_LIMIT) && restricted_count(db) == 1, "VM budget rollback");
    REQUIRE(PDb_GetErrorInfo(db, &error) == PDB_OK && error.category == PDB_ERROR_CATEGORY_LIMIT, "VM failure category");
    f.progress_seen = CreateEvent(NULL, TRUE, FALSE, NULL); f.polls = 0;
    REQUIRE(f.progress_seen != NULL, "checkpoint event");
    cancel_thread = CreateThread(NULL, 0, restricted_cancel_worker, &f, 0, NULL);
    REQUIRE(cancel_thread != NULL, "cancel flag worker");
    SetThreadPriority(cancel_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    o.vm_budget = PDB_RESTRICTED_VM_MAX;
    REQUIRE(restricted_run(db, s + 1, 1, &o, &r, PDB_STATE) && f.polls >= 5,
            "running VM observes atomic cancellation");
    REQUIRE(WaitForSingleObject(cancel_thread, 5000) == WAIT_OBJECT_0 &&
            PDb_GetErrorInfo(db, &error) == PDB_OK && error.category == PDB_ERROR_CATEGORY_CANCELLED,
            "cancel worker joined and classified");
    CloseHandle(cancel_thread); cancel_thread = NULL;
    CloseHandle(f.progress_seen); f.progress_seen = NULL;
    InterlockedExchange((LONG*)&f.cancelled, 0);
    restricted_statement(s, "SELECT 1", PDB_RESTRICTED_QUERY); s[1] = s[0]; o.vm_budget = 100;
    REQUIRE(restricted_run(db, s, 2, &o, &r, PDB_LIMIT), "batch shares short-VM reserve");
    o.vm_budget = PDB_RESTRICTED_VM_MAX;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_OK), "usable after interruption");
    restricted_statement(s, "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<16) SELECT x FROM n", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_OK) && r.row_count == 16 && f.last_integer == 16, "exact row capacity");
    restricted_statement(s, "SELECT 1", PDB_RESTRICTED_QUERY);
    for (i = 1; i < 8; i++) s[i] = s[0];
    REQUIRE(restricted_run(db, s, 8, &o, &r, PDB_OK) && r.statement_count == 8, "exact batch capacity");
    REQUIRE(restricted_run(db, s, 9, &o, &r, PDB_INVALID_ARGUMENT), "batch overflow before array read");
    restricted_statement(s, "SELECT 1e999", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_NOT_SUPPORTED), "nonfinite result rejected");
    restricted_statement(s, "SELECT CAST(x'c0af' AS TEXT)", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_NOT_SUPPORTED), "invalid UTF8 result rejected");
    restricted_statement(s, "SELECT 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_LIMIT), "column cap");
    restricted_statement(s, "SELECT ?1", PDB_RESTRICTED_QUERY);
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_SQL_REJECTED), "exact bind count");
    s[0].param_count = 17; s[0].params = params;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_LIMIT), "bind count overflow before array read");
    s[0].params = params; s[0].param_count = 1; params[0].type = PDB_VALUE_BLOB;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_NOT_SUPPORTED), "BLOB bind rejected");
    params[0].type = PDB_VALUE_TEXT; params[0].text = "\xc0\xaf"; params[0].text_bytes = 2;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_INVALID_ARGUMENT), "invalid UTF8 bind rejected");
    restricted_statement(s, "SELECT 1", PDB_RESTRICTED_QUERY);
    o.version = 2;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_INVALID_ARGUMENT), "options version fail closed"); o.version = 1;
    s[0].sql_bytes = PDB_RESTRICTED_SQL_BYTES + 1;
    REQUIRE(restricted_run(db, s, 1, &o, &r, PDB_LIMIT), "SQL byte cap before read");
    restricted_statement(s, "SELECT 1", PDB_RESTRICTED_QUERY);
    REQUIRE(PDb_Prepare(db, "SELECT 1", &old_stmt) == PDB_OK &&
            PDb_ExecuteRestricted(db, s, 1, &o, &r) == PDB_STATE, "outstanding statement guard");
    PDb_Finalize(old_stmt); old_stmt = NULL;
    REQUIRE(PDb_Begin(db) == PDB_OK && PDb_ExecuteRestricted(db, s, 1, &o, &r) == PDB_STATE &&
            PDb_Rollback(db) == PDB_OK, "outer transaction untouched");
    REQUIRE(PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &sync) == PDB_OK &&
            PDb_ExecuteRestricted(sync, s, 1, &o, &r) == PDB_STATE, "sync mode remains separate");
    REQUIRE(PDb_Exec(db, "PRAGMA user_version=3") == PDB_OK &&
            PDb_Exec(db, "ATTACH ':memory:' AS legacy; DETACH legacy;") == PDB_OK, "legacy local-full unchanged");
    REQUIRE(PDb_ApplyMigration(db, 2, "CREATE TABLE migration_survives(x)") == PDB_OK, "trusted migration remains usable");
    ok = TRUE;
done:
    if (cancel_thread != NULL) {
        SetEvent(f.progress_seen);
        WaitForSingleObject(cancel_thread, INFINITE); /* bounded worker, never terminate it */
        CloseHandle(cancel_thread);
    }
    if (f.progress_seen != NULL) CloseHandle(f.progress_seen);
    if (old_stmt != NULL) PDb_Finalize(old_stmt);
    PDb_Close(sync); PDb_Close(db);
    return ok;
}

typedef struct MigrationCancellation {
    PDbHandle db;
    int polls;
    int reentry_ok;
} MigrationCancellation;
static int migration_cancel_checkpoint(void* data)
{
    MigrationCancellation* f;
    int version;
    f = (MigrationCancellation*)data; f->polls++;
    version = -91;
    if (PDb_GetMigrationVersion(f->db, &version) != PDB_STATE || version != -91 ||
            PDb_Exec(f->db, "DELETE FROM notes") != PDB_STATE) f->reentry_ok = 0;
    PDb_Close(f->db); /* active migration must remain alive */
    return f->polls >= 8;
}
static int migration_run(PDbHandle db, int expected, int target, const char* sql,
        PDbRestrictedOptions* options, PDbMigrationReport* report, int wanted)
{
    PDbMigrationReport before;
    PDbConnectionState state;
    int rc;
    before = *report;
    rc = PDb_ApplyMigrationRestricted(db, expected, target, sql, (int)strlen(sql), options, report);
    if (rc != wanted || (rc != PDB_OK && memcmp(&before, report, sizeof(before)))) return 0;
    state.size = sizeof(state); state.version = 1;
    return PDb_GetConnectionState(db, &state) == PDB_OK && !state.transaction_active && !state.statement_count;
}
BOOL test1366_db_migration_contract(void)
{
    static const char* const denied[] = {
        "UPDATE __pdb_meta SET value='900'", "DELETE FROM __PDB_META",
        "CREATE TABLE __PDB_secret(x)", "CREATE INDEX __PDB_idx ON notes(body)",
        "CREATE INDEX steal ON __pdb_meta(value)", "SELECT * FROM __pdb_meta",
        "INSERT INTO sqlite_master(type,name) VALUES('table','stolen')",
        "CREATE TABLE leak AS SELECT rowid FROM sqlite_master", "SELECT rowid FROM sqlite_master",
        "CREATE TABLE copy AS SELECT 1", "SELECT 1", "INSERT INTO notes VALUES(98,'x') RETURNING id",
        "CREATE TEMP TABLE t(x)", "ATTACH ':memory:' AS extra", "DETACH main", "PRAGMA user_version=1",
        "PRAGMA writable_schema=1", "BEGIN", "COMMIT", "ROLLBACK", "SAVEPOINT x", "RELEASE x",
        "ALTER TABLE notes ADD COLUMN bad", "DROP TABLE notes", "CREATE VIEW v AS SELECT * FROM notes",
        "CREATE TRIGGER t AFTER INSERT ON notes BEGIN DELETE FROM notes; END",
        "VACUUM", "REINDEX", "ANALYZE", "SELECT load_extension('x')",
        "CREATE TABLE fk(x REFERENCES __pdb_meta(key));INSERT INTO fk VALUES('schema_version')"
    };
    static const char* const corrupt[] = { "''", "'-1'", "'+1'", "'01'", "'2147483648'", "'1garbage'",
        "x'32'", "x'310032'", "'1234567890123456789012345678901234567890'" };
    PDbHandle db;
    PDbHandle other;
    PDbStmtHandle stmt;
    PDbRestrictedOptions options;
    PDbMigrationReport report;
    PDbMigrationReport before;
    PDbErrorInfo error;
    RestrictedFixture f;
    MigrationCancellation cancelling;
    char sql[4096];
    unsigned int i;
    int version;
    BOOL ok;
    db = other = NULL; stmt = NULL; ok = FALSE;
    memset(&f, 0, sizeof(f)); memset(&options, 0, sizeof(options)); memset(&report, 0, sizeof(report));
    options.size = sizeof(options); options.version = 1; options.vm_budget = PDB_RESTRICTED_VM_MAX;
    options.cancelled = restricted_cancel; options.user_data = &f;
    report.size = sizeof(report); report.version = 1;
    REQUIRE(PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK, "migration local connection");
    f.db = db; version = -91;
    REQUIRE(PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 0, "missing schema version is zero");
    REQUIRE(migration_run(db, 0, 1, "/* schema */CREATE TABLE notes(id INTEGER PRIMARY KEY,body TEXT UNIQUE);"
        "CREATE TABLE tags(name TEXT PRIMARY KEY);CREATE INDEX notes_id ON notes(id);INSERT INTO notes VALUES(1,'first'); -- end\n",
        &options, &report, PDB_OK) && report.statement_count == 4 && report.previous_version == 0 && report.current_version == 1,
        "CREATE TABLE/UNIQUE/TEXT primary/index/DML atomically admitted");
    REQUIRE(migration_run(db, 1, 2, "INSERT INTO notes SELECT 2,'second';CREATE UNIQUE INDEX tags_name ON tags(name)",
        &options, &report, PDB_OK) && report.statement_count == 2, "next manifest version with ordinary DML subquery");
    for (i = 0; i < sizeof(denied) / sizeof(denied[0]); i++) {
        _snprintf(sql, sizeof(sql), "INSERT INTO notes VALUES(99,'provisional');%s", denied[i]);
        REQUIRE(migration_run(db, 2, 3, sql, &options, &report, PDB_SQL_REJECTED) && restricted_count(db) == 2 &&
            PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 2, denied[i]);
    }
    REQUIRE(migration_run(db, 0, 1, "CREATE TABLE replay(x)", &options, &report, PDB_SCHEMA_MISMATCH), "old version rejected");
    REQUIRE(migration_run(db, 3, 4, "CREATE TABLE future(x)", &options, &report, PDB_SCHEMA_MISMATCH), "future expected rejected");
    REQUIRE(migration_run(db, 2, 4, "CREATE TABLE gap(x)", &options, &report, PDB_INVALID_ARGUMENT), "version gap rejected");
    REQUIRE(migration_run(db, 2, 3, " -- only comment\n /* empty */", &options, &report, PDB_SQL_REJECTED), "empty migration not applied");
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE rolled(x);INSERT INTO notes VALUES(1,'duplicate')", &options, &report, PDB_ERROR) &&
        PDb_Prepare(db, "SELECT * FROM rolled", &stmt) != PDB_OK && !stmt && restricted_count(db) == 2, "DDL and DML rollback together");
    REQUIRE(PDb_Exec(db, "CREATE TABLE parent(x PRIMARY KEY);CREATE TABLE child(x REFERENCES parent(x) DEFERRABLE INITIALLY DEFERRED)") == PDB_OK,
        "trusted constraint fixture");
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE provisional(x);INSERT INTO child VALUES(1)", &options, &report, PDB_ERROR), "COMMIT failure rollback");
    error.size = sizeof(error); error.version = 1;
    REQUIRE(PDb_GetErrorInfo(db, &error) == PDB_OK && error.category == PDB_ERROR_CATEGORY_CONSTRAINT &&
        error.cleanup_attempted && error.cleanup_result == PDB_OK && PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 2,
        "commit error preserves schema and cleanup diagnostic");
    options.vm_budget = 2000;
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE budget(x);WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n) INSERT INTO budget SELECT x FROM n",
        &options, &report, PDB_LIMIT) && PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 2, "shared VM budget rolls schema back");
    REQUIRE(PDb_Prepare(db, "SELECT * FROM budget", &stmt) != PDB_OK && !stmt, "budget failure leaves no table");
    options.vm_budget = PDB_RESTRICTED_VM_MAX; f.cancelled = 1;
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE cancelled(x)", &options, &report, PDB_STATE), "pre-cancel no mutation"); f.cancelled = 0;
    memset(&cancelling, 0, sizeof(cancelling)); cancelling.db = db; cancelling.reentry_ok = 1;
    options.cancelled = migration_cancel_checkpoint; options.user_data = &cancelling;
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE cancel_vm(x);WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n) INSERT INTO cancel_vm SELECT x FROM n",
        &options, &report, PDB_STATE) && cancelling.polls >= 8 && cancelling.reentry_ok &&
        PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 2, "running cancellation/reentry/close guards and rollback");
    REQUIRE(PDb_Prepare(db, "SELECT * FROM cancel_vm", &stmt) != PDB_OK && !stmt, "cancel leaves no provisional schema");
    options.cancelled = restricted_cancel; options.user_data = &f;
    strcpy(sql, "");
    for (i = 0; i < 65; i++) strcat(sql, "UPDATE notes SET body=body WHERE id=1;");
    REQUIRE(migration_run(db, 2, 3, sql, &options, &report, PDB_LIMIT) && PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 2,
        "statement count overflow rolls back");
    options.row = restricted_row;
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE nope(x)", &options, &report, PDB_INVALID_ARGUMENT), "no author result callback"); options.row = NULL;
    options.version = 2;
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE nope(x)", &options, &report, PDB_INVALID_ARGUMENT), "options version guard"); options.version = 1;
    report.version = 2;
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE nope(x)", &options, &report, PDB_INVALID_ARGUMENT), "report version canary"); report.version = 1;
    before = report;
    REQUIRE(PDb_ApplyMigrationRestricted(db, 2, 3, "a\0b", 3, &options, &report) == PDB_INVALID_ARGUMENT &&
        !memcmp(&before, &report, sizeof(report)), "embedded SQL NUL canary");
    REQUIRE(PDb_ApplyMigrationRestricted(db, 2, 3, "\xc0\xaf", 2, &options, &report) == PDB_INVALID_ARGUMENT &&
        !memcmp(&before, &report, sizeof(report)), "invalid SQL UTF-8 before transaction");
    REQUIRE(PDb_ApplyMigrationRestricted(db, 2, 3, "x", PDB_SQL_MAX_BYTES + 1, &options, &report) == PDB_LIMIT,
        "SQL budget before read");
    REQUIRE(PDb_Prepare(db, "SELECT 1", &stmt) == PDB_OK && PDb_GetMigrationVersion(db, &version) == PDB_STATE &&
        PDb_ApplyMigrationRestricted(db, 2, 3, "CREATE TABLE nope(x)", 20, &options, &report) == PDB_STATE, "outstanding statement guard");
    PDb_Finalize(stmt); stmt = NULL;
    REQUIRE(PDb_Begin(db) == PDB_OK && PDb_GetMigrationVersion(db, &version) == PDB_STATE && PDb_Rollback(db) == PDB_OK, "outer transaction untouched");
    REQUIRE(PDb_Exec(db, "UPDATE __pdb_meta SET value=NULL WHERE key='schema_version'") != PDB_OK &&
        PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 2, "metadata NOT NULL constraint preserved");
    for (i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); i++) {
        _snprintf(sql, sizeof(sql), "UPDATE __pdb_meta SET value=%s WHERE key='schema_version'", corrupt[i]);
        REQUIRE(PDb_Exec(db, sql) == PDB_OK, "corrupt metadata fixture"); version = 91;
        REQUIRE(PDb_GetMigrationVersion(db, &version) == PDB_SCHEMA_MISMATCH && version == 91 &&
            migration_run(db, 2, 3, "CREATE TABLE nope(x)", &options, &report, PDB_SCHEMA_MISMATCH), corrupt[i]);
    }
    REQUIRE(PDb_Exec(db, "UPDATE __pdb_meta SET value='2' WHERE key='schema_version'") == PDB_OK, "restore metadata");
    REQUIRE(migration_run(db, 2, 3, "CREATE TABLE final_schema(x)", &options, &report, PDB_OK) &&
        PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 3, "usable after failures");
    REQUIRE(PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &other) == PDB_OK && PDb_GetMigrationVersion(other, &version) == PDB_STATE &&
        PDb_ApplyMigrationRestricted(other, 0, 1, "CREATE TABLE nope(x)", 20, &options, &report) == PDB_STATE, "sync remains separate");
    REQUIRE(PDb_ApplyMigration(db, 4, "CREATE TABLE legacy_still_works(x)") == PDB_OK &&
        PDb_GetMigrationVersion(db, &version) == PDB_OK && version == 4, "legacy migration ABI and behavior unchanged");
    ok = TRUE;
done:
    if (stmt) PDb_Finalize(stmt);
    PDb_Close(other); PDb_Close(db); return ok;
}
