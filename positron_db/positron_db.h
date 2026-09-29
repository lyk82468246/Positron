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

PDB_API int PDb_Begin(PDbHandle db);
PDB_API int PDb_Commit(PDbHandle db);
PDB_API int PDb_Rollback(PDbHandle db);
PDB_API int PDb_Cancel(PDbHandle db);
PDB_API int PDb_GetLastError(PDbHandle db, char* buffer, int capacity);
/* Compatibility spelling retained for callers that used the initial draft. */
PDB_API int PDb_CopyLastError(PDbHandle db, char* buffer, int capacity);

/* Schema changes are owned by the application.  The script is executed as
 * one transaction and the stored schema version advances only on commit. */
PDB_API int PDb_ApplyMigration(PDbHandle db, int version,
        const char* sql_script);

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
