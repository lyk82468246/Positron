/*
 * positron_db.h - bounded SQLite and REST synchronization boundary.
 *
 * The public API deliberately does not expose sqlite3 types.  A database
 * handle is owned by the caller and must be used from one owner thread.
 * Network I/O remains a host responsibility: callers build a request with
 * PDb_SyncBuildRequest(), send it through positron_http.dll, and apply the
 * response with PDb_SyncApplyResponse().
 */

#ifndef POSITRON_DB_H
#define POSITRON_DB_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_DB_EXPORTS
#  define PDB_API __declspec(dllexport)
#else
#  define PDB_API __declspec(dllimport)
#endif

typedef struct PDb PDb;
typedef struct PDbStmt PDbStmt;
typedef PDb* PDbHandle;
typedef PDbStmt* PDbStmtHandle;

enum {
    PDB_OK = 0,
    PDB_ERROR = -1,
    PDB_INVALID_ARGUMENT = -2,
    PDB_NOMEM = -3,
    PDB_BUSY = -4,
    PDB_NOT_SUPPORTED = -5,
    PDB_NOT_FOUND = -6,
    PDB_LIMIT = -7,
    PDB_CONFLICT = -8,
    PDB_SCHEMA_MISMATCH = -9,
    PDB_NETWORK = -10,
    PDB_BUFFER_TOO_SMALL = -11,
    PDB_STATE = -12,
    PDB_SQL_REJECTED = -13
};

enum {
    PDB_OPEN_LOCAL_FULL_SQL = 1,
    PDB_OPEN_SYNC = 2
};

enum {
    PDB_STEP_ROW = 100,
    PDB_STEP_DONE = 101
};

enum {
    PDB_VALUE_NULL = 0,
    PDB_VALUE_INTEGER = 1,
    PDB_VALUE_REAL = 2,
    PDB_VALUE_TEXT = 3,
    PDB_VALUE_BLOB = 4
};

enum {
    PDB_CONFLICT_ACCEPT_SERVER = 0,
    PDB_CONFLICT_RETRY_LOCAL = 1,
    PDB_CONFLICT_DISCARD = 2
};

#define PDB_SQL_MAX_BYTES       32768
#define PDB_SYNC_MAX_BODY_BYTES 1048576
#define PDB_SYNC_MAX_TABLES     32
#define PDB_SYNC_MAX_COLUMNS    64
#define PDB_SYNC_NAME_MAX       96
#define PDB_SYNC_CLIENT_MAX     96

typedef struct PDbSyncColumn {
    const char* name;
    int value_type;
} PDbSyncColumn;

#define PDB_ERROR_INFO_VERSION 1
#define PDB_CONNECTION_STATE_VERSION 1
#define PDB_RESTRICTED_VERSION 1
#define PDB_RESTRICTED_SQL_BYTES 2048
#define PDB_RESTRICTED_MAX_PARAMS 16
#define PDB_RESTRICTED_MAX_STATEMENTS 8
#define PDB_RESTRICTED_MAX_ROWS 16
#define PDB_RESTRICTED_MAX_COLUMNS 16
#define PDB_RESTRICTED_TEXT_BYTES 4096
#define PDB_RESTRICTED_VM_MAX 1000000UL
#define PDB_RESTRICTED_QUERY 1
#define PDB_RESTRICTED_EXECUTE 2
#define PDB_ERROR_MESSAGE_BYTES 256

enum {
    PDB_ERROR_CATEGORY_NONE = 0,
    PDB_ERROR_CATEGORY_ARGUMENT = 1,
    PDB_ERROR_CATEGORY_STATE = 2,
    PDB_ERROR_CATEGORY_LIMIT = 3,
    PDB_ERROR_CATEGORY_MEMORY = 4,
    PDB_ERROR_CATEGORY_BUSY = 5,
    PDB_ERROR_CATEGORY_SQL = 6,
    PDB_ERROR_CATEGORY_CONSTRAINT = 7,
    PDB_ERROR_CATEGORY_READONLY = 8,
    PDB_ERROR_CATEGORY_FULL = 9,
    PDB_ERROR_CATEGORY_IO = 10,
    PDB_ERROR_CATEGORY_CORRUPT = 11,
    PDB_ERROR_CATEGORY_CANNOT_OPEN = 12,
    PDB_ERROR_CATEGORY_CANCELLED = 13,
    PDB_ERROR_CATEGORY_AUTHORIZATION = 14,
    PDB_ERROR_CATEGORY_SCHEMA = 15,
    PDB_ERROR_CATEGORY_NETWORK = 16,
    PDB_ERROR_CATEGORY_NOT_FOUND = 17,
    PDB_ERROR_CATEGORY_UNSUPPORTED = 18,
    PDB_ERROR_CATEGORY_CONFLICT = 19,
    PDB_ERROR_CATEGORY_OTHER = 20
};

enum {
    PDB_TRANSACTION_NONE = 0,
    PDB_TRANSACTION_READ = 1,
    PDB_TRANSACTION_WRITE = 2
};

/* Caller-owned POD snapshots. Initialize size=sizeof(struct), version=1.
 * Invalid size/version leaves the entire output unchanged. Larger buffers
 * are accepted; only the v1 prefix is written (size is returned as v1 size).
 * Native codes are diagnostic SQLite integers, not new public return codes;
 * both are zero for wrapper-only failures. message is copied, never borrowed.
 * transaction_active is captured at failed-call return, not a live query.
 * cleanup_result reports a secondary automatic rollback failure without
 * replacing the original result/category/native codes/message. */
typedef struct PDbErrorInfo {
    unsigned int size;
    unsigned int version;
    int result;
    int category;
    int sqlite_code;
    int sqlite_extended_code;
    int transaction_active;
    int cleanup_attempted;
    int cleanup_result;
    char message[PDB_ERROR_MESSAGE_BYTES];
} PDbErrorInfo;

typedef struct PDbConnectionState {
    unsigned int size;
    unsigned int version;
    int transaction_active;
    int transaction_state;
    int statement_count;
} PDbConnectionState;

/* Optional caller-owned open error output. With valid outputs, a failed
 * open sets *outDb=NULL; an invalid error header leaves all outputs unchanged.
 * On success the snapshot is NONE. No process/thread-global error slot. */
PDB_API int PDb_OpenUtf8Ex(const char* path, int mode, PDbHandle* outDb,
        PDbErrorInfo* outError);
/* Sticky last failure from status-returning operations (including invalid
 * arguments with a live owner). Success, column sentinel getters, and all
 * diagnostic queries preserve it. ClearError is the only explicit reset.
 * NULL handles cannot store errors; closed/freed handles must never be used.
 * Queries run on the owning thread and never claim the connection is safe
 * to retry, or that an I/O/corruption error permits deleting the database. */
PDB_API int PDb_GetErrorInfo(PDbHandle db, PDbErrorInfo* outError);
PDB_API int PDb_ClearError(PDbHandle db);
PDB_API int PDb_GetConnectionState(PDbHandle db,
        PDbConnectionState* outState);

/* Open or create a UTF-8 database file.  ":memory:" is supported for
 * host-side contract tests. */
PDB_API int PDb_OpenUtf8(const char* path, int mode, PDbHandle* outDb);
/* Close the database. Any outstanding statements are finalized first. */
PDB_API void PDb_Close(PDbHandle db);

/* Execute SQL. Local-full mode accepts the SQLite script range; sync mode
 * accepts one statement and reserves schema changes for migrations. */
PDB_API int PDb_Exec(PDbHandle db, const char* sql);
PDB_API int PDb_Prepare(PDbHandle db, const char* sql,
        PDbStmtHandle* outStmt);
PDB_API int PDb_BindNull(PDbStmtHandle stmt, int index);
PDB_API int PDb_BindInt64(PDbStmtHandle stmt, int index, __int64 value);
PDB_API int PDb_BindDouble(PDbStmtHandle stmt, int index, double value);
PDB_API int PDb_BindText(PDbStmtHandle stmt, int index,
        const char* text, int length);
/* length == 0 binds an empty BLOB, including when data is NULL.
 * Use BindNull to bind SQL NULL. Nonempty data is copied during binding. */
PDB_API int PDb_BindBlob(PDbStmtHandle stmt, int index,
        const void* data, int length);
PDB_API int PDb_Step(PDbStmtHandle stmt);
PDB_API int PDb_Finalize(PDbStmtHandle stmt);

PDB_API int PDb_ColumnCount(PDbStmtHandle stmt);
PDB_API int PDb_ColumnType(PDbStmtHandle stmt, int index);
PDB_API __int64 PDb_ColumnInt64(PDbStmtHandle stmt, int index);
PDB_API double PDb_ColumnDouble(PDbStmtHandle stmt, int index);
PDB_API const char* PDb_ColumnText(PDbStmtHandle stmt, int index);
PDB_API const void* PDb_ColumnBlob(PDbStmtHandle stmt, int index);
PDB_API int PDb_ColumnBytes(PDbStmtHandle stmt, int index);

/* State follows SQLite autocommit, including transactions started by local
 * SQL. COMMIT failure may leave the transaction active; query live state.
 * Migration owns its transaction and attempts rollback after failure. */
PDB_API int PDb_Begin(PDbHandle db);
PDB_API int PDb_Commit(PDbHandle db);
PDB_API int PDb_Rollback(PDbHandle db);
/* Request cancellation on the owning thread.  A pending request is consumed
 * by the next Exec or unfinished Step before SQL runs, returning PDB_STATE.
 * Later executions remain usable; this does not permit cross-thread handles. */
PDB_API int PDb_Cancel(PDbHandle db);
PDB_API int PDb_GetLastError(PDbHandle db, char* buffer, int capacity);
/* Compatibility spelling retained for callers that used the initial draft. */
PDB_API int PDb_CopyLastError(PDbHandle db, char* buffer, int capacity);

/* Schema changes are owned by the application.  The script is executed as
 * one transaction and the stored schema version advances only on commit. */
PDB_API int PDb_ApplyMigration(PDbHandle db, int version,
        const char* sql_script);

/* Opt-in, owner-thread-only restricted local SQL, never implicit on old APIs.
 * Requires local-full, no transaction/outstanding statements. One owned
 * transaction per call; success only after COMMIT, failure attempts ROLLBACK
 * with the existing sticky/cleanup diagnostic contract. Caller outputs stay
 * unchanged on failure; provisional row callbacks MUST be buffered privately
 * and discarded on any non-OK return. No JS/UI publication before success.
 * Authorizer permits only main-database SELECT/ordinary DML and a documented
 * safe function allowlist, including inside views/triggers; no DDL, temp,
 * PRAGMA, ATTACH, host transactions, extension or private __pdb_/sqlite_ data.
 * Trusted schema migrations remain separate host operations, never SQL input
 * from the unprivileged request. This is not a malicious-code/VFS sandbox.
 * All inputs are borrowed synchronously. SQL is explicit-length UTF-8 with
 * no embedded NUL, one statement plus whitespace; binds by SQLite index,
 * exact parameter count. Text is length-delimited UTF-8; BLOB/nonfinite real
 * unsupported. QUERY must be readonly; EXECUTE must not return columns.
 * <=8 statements, SQL <=2048 bytes each, <=16 params/columns, <=16 total rows;
 * <=4096 total result text bytes. No truncation. Runtime separately bounds
 * request/result JSON to 4096 bytes and encodes int64 as decimal, not double.
 * VM budget shared across the batch, 100..1000000 in multiples of 100.
 * SQLite progress checkpoints are conservative work accounting, not a hard
 * wall-clock/memory/VFS interruption guarantee. Cancellation is checked before
 * work, at checkpoints/row boundaries and before commit. Cancel callback only
 * reads host atomic state; it and row callbacks must not reenter/close/cleanup
 * DB, pump messages, block or expose provisional data. No cross-thread handle
 * calls. Limit -> PDB_LIMIT, cancellation -> PDB_STATE/CANCELLED category.
 * Unpreparable SQL returns SQL_REJECTED with its native SQL diagnostic;
 * actual authorizer denials have AUTHORIZATION category. No message parsing.
 * No callbacks after return. Old PDb_Cancel behavior unchanged. */
typedef struct PDbRestrictedValue {
    int type; /* NULL/INTEGER/REAL/TEXT from PDB_VALUE_*. */
    __int64 integer;
    double real;
    const char* text;
    int text_bytes;
} PDbRestrictedValue;
typedef struct PDbRestrictedStatement {
    const char* sql;
    int sql_bytes;
    int kind;
    const PDbRestrictedValue* params;
    int param_count;
} PDbRestrictedStatement;
typedef int (*PDbRestrictedCancelFn)(void* user_data);
/* Return PDB_OK to accept a provisional row, PDB_LIMIT for result overflow,
 * any other nonzero value aborts as PDB_STATE. Values/text are borrowed only
 * until callback return. statement_index is zero-based. */
typedef int (*PDbRestrictedRowFn)(void* user_data, int statement_index,
        const PDbRestrictedValue* values, int column_count);
typedef struct PDbRestrictedOptions {
    unsigned int size;
    unsigned int version;
    unsigned long vm_budget;
    PDbRestrictedCancelFn cancelled;
    PDbRestrictedRowFn row;
    void* user_data;
} PDbRestrictedOptions;
typedef struct PDbRestrictedReport {
    unsigned int size;
    unsigned int version;
    int statement_count;
    int row_count;
    __int64 changes; /* committed total changes, including triggers. */
} PDbRestrictedReport;
PDB_API int PDb_ExecuteRestricted(PDbHandle db,
        const PDbRestrictedStatement* statements, int statement_count,
        const PDbRestrictedOptions* options, PDbRestrictedReport* report);

#define PDB_MIGRATION_VERSION 1
#define PDB_MIGRATION_MAX_STATEMENTS 64
#define PDB_LOCAL_IDENTITY_BYTES 2048
/* Additive trusted-native local storage identity guard. Local-full/idle only;
 * explicit nonempty UTF-8 <=2048 bytes, no NUL. BEGIN IMMEDIATE serializes an
 * exact private TEXT identity comparison. A missing identity can be installed
 * only in a pristine DB (only its mode metadata table/index, no other keys or
 * schema objects). Existing different/corrupt identity or unclaimed nonempty
 * DB => SCHEMA_MISMATCH, never rebind/adopt/delete it. No success output;
 * rollback preserves sticky diagnostics. Restricted authors cannot read or
 * alter this __pdb_meta key. Identity provenance, filesystem isolation and
 * lease remain Runtime/provider responsibilities, not publisher verification.
 * Does not change old open/full-SQL APIs or constrain trusted native SQL. */
PDB_API int PDb_BindLocalIdentity(PDbHandle db, const char* identity, int bytes);
typedef struct PDbMigrationReport {
    unsigned int size;
    unsigned int version;
    int previous_version;
    int current_version;
    int statement_count;
} PDbMigrationReport;
/* Additive opt-in prerequisite for Runtime manifest migrations. These APIs
 * do NOT authenticate an application, map storage or expose a JS service.
 * Local-full, idle/no statements/no outer transaction, same owner worker.
 * Strict stored version: missing=0; otherwise canonical decimal 0..INT_MAX,
 * no truncation/NUL/sign/leading zero. Getter output unchanged on failure. */
PDB_API int PDb_GetMigrationVersion(PDbHandle db, int* version);
/* One manifest file, explicit UTF-8 <=PDB_SQL_MAX_BYTES, <=64 nonempty SQL
 * statements; no binds, returned columns, or empty/comment-only migration.
 * expected>=0, target==expected+1. BEGIN IMMEDIATE protects the version check;
 * mismatched/future/corrupt version fails closed, no automatic downgrade.
 * v1 schema subset: main CREATE TABLE (not AS SELECT), CREATE INDEX including
 * ordinary UNIQUE constraints, plus ordinary INSERT/UPDATE/DELETE and their
 * subqueries. No ALTER/DROP/view/trigger/vtable/temp/ATTACH/PRAGMA/transaction/
 * extension or private __pdb_/sqlite_ object access. SQLite authorizer owns
 * admission, not keyword matching. Engine catalog maintenance is allowed only
 * for admitted CREATE operations; never author catalog DML or catalog queries.
 * Same function allowlist/work budget/cancel/reentry as ExecuteRestricted.
 * options.row must be NULL; options size/version and vm_budget are required.
 * COMMIT includes private schema version update, then publishes report;
 * any failure preserves report and attempts rollback with sticky diagnostics.
 * No callbacks survive return, no cross-thread cancel/handle or hard VFS bound.
 * Old ApplyMigration remains unchanged and is NOT a replacement for this guard. */
PDB_API int PDb_ApplyMigrationRestricted(PDbHandle db, int expected_version,
        int target_version, const char* sql, int sql_bytes,
        const PDbRestrictedOptions* options, PDbMigrationReport* report);

/* Configure the durable identity and schema contract used by sync requests.
 * schema_version must equal the latest applied migration version. */
PDB_API int PDb_SyncConfigure(PDbHandle db, const char* client_id,
        int schema_version, const char* schema_hash);

/* Register one table for row-level synchronization.  The column list must
 * include the primary key and every column sent to the server. */
PDB_API int PDb_SyncRegisterTable(PDbHandle db, const char* table_name,
        const char* primary_key, const PDbSyncColumn* columns,
        int column_count);

/* Size-probe and copy a bounded JSON sync request.  outLength receives the
 * number of bytes excluding the terminating NUL. */
PDB_API int PDb_SyncBuildRequest(PDbHandle db, char* buffer, int capacity,
        int* outLength);

/* Apply a successful REST response atomically.  Non-2xx status codes never
 * modify database state. */
PDB_API int PDb_SyncApplyResponse(PDbHandle db, int http_status,
        const char* body, int body_length);

PDB_API int PDb_SyncPendingCount(PDbHandle db);
PDB_API int PDb_SyncConflictCount(PDbHandle db);
/* Size-probe and copy all conflicts as a bounded JSON array. */
PDB_API int PDb_SyncGetConflicts(PDbHandle db, char* buffer, int capacity,
        int* outLength);
PDB_API int PDb_SyncCopyConflict(PDbHandle db, int index,
        char* buffer, int capacity, int* outLength);
PDB_API int PDb_SyncResolveConflict(PDbHandle db, int conflict_id,
        int action);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_DB_H */
