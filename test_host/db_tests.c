/* test_host DB contract tests.  The product implementation remains in
 * positron_db.dll; this file only supplies fixtures and assertions. */

#include <windows.h>
#include <string.h>

#include "positron_db.h"

static BOOL db_test_query_name(PDbHandle db, char* output, int capacity)
{
    PDbStmtHandle stmt;
    int rc;
    const char* value;

    rc = PDb_Prepare(db, "SELECT name FROM records WHERE id=1", &stmt);
    if (rc != PDB_OK) {
        return FALSE;
    }
    rc = PDb_Step(stmt);
    if (rc != PDB_STEP_ROW) {
        PDb_Finalize(stmt);
        return FALSE;
    }
    value = PDb_ColumnText(stmt, 0);
    if (value == NULL || (int)strlen(value) >= capacity) {
        PDb_Finalize(stmt);
        return FALSE;
    }
    strcpy(output, value);
    PDb_Finalize(stmt);
    return TRUE;
}

static BOOL db_test_query_text_name(PDbHandle db, const char* key,
        char* output, int capacity)
{
    PDbStmtHandle stmt;
    int rc;
    const char* value;

    if (db == NULL || key == NULL || output == NULL || capacity <= 0) {
        return FALSE;
    }
    stmt = NULL;
    rc = PDb_Prepare(db,
            "SELECT name FROM text_records WHERE key=?1", &stmt);
    if (rc == PDB_OK) {
        rc = PDb_BindText(stmt, 1, key, (int)strlen(key));
    }
    if (rc == PDB_OK) {
        rc = PDb_Step(stmt);
    }
    if (rc != PDB_STEP_ROW) {
        if (stmt != NULL) {
            PDb_Finalize(stmt);
        }
        return FALSE;
    }
    value = PDb_ColumnText(stmt, 0);
    if (value == NULL || (int)strlen(value) >= capacity) {
        PDb_Finalize(stmt);
        return FALSE;
    }
    strcpy(output, value);
    PDb_Finalize(stmt);
    return TRUE;
}

static BOOL db_test_query_transaction_count(PDbHandle db,
        __int64 expected)
{
    PDbStmtHandle stmt;
    int rc;

    stmt = NULL;
    rc = PDb_Prepare(db, "SELECT COUNT(*) FROM tx_values", &stmt);
    if (rc == PDB_OK) {
        rc = PDb_Step(stmt);
    }
    if (rc != PDB_STEP_ROW || PDb_ColumnInt64(stmt, 0) != expected) {
        if (stmt != NULL) {
            PDb_Finalize(stmt);
        }
        return FALSE;
    }
    PDb_Finalize(stmt);
    return TRUE;
}

static BOOL db_test_text_key_and_delete(void)
{
    static const PDbSyncColumn columns[] = {
        { "key", PDB_VALUE_TEXT },
        { "name", PDB_VALUE_TEXT }
    };
    static const char accepted_insert[] =
        "{\"schema_version\":1,\"schema_hash\":\"text-v1\","
        "\"accepted\":[{\"op_id\":\"text-client:1\","
        "\"version\":\"1\"}],\"conflicts\":[],"
        "\"changes\":[],\"next_cursor\":\"1\"}";
    static const char pulled[] =
        "{\"schema_version\":1,\"schema_hash\":\"text-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[{"
        "\"entity\":\"text_records\",\"key\":\"alpha\","
        "\"version\":\"2\",\"deleted\":false,\"values\":{"
        "\"key\":{\"t\":\"s\",\"v\":\"alpha\"},"
        "\"name\":{\"t\":\"s\",\"v\":\"remote-text\"}}}],"
        "\"next_cursor\":\"2\"}";
    static const char accepted_delete[] =
        "{\"schema_version\":1,\"schema_hash\":\"text-v1\","
        "\"accepted\":[{\"op_id\":\"text-client:2\","
        "\"version\":\"3\"}],\"conflicts\":[],"
        "\"changes\":[],\"next_cursor\":\"3\"}";
    PDbHandle db;
    char request[4096];
    char name[64];
    int request_length;
    int rc;

    db = NULL;
    rc = PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &db);
    if (rc == PDB_OK) {
        rc = PDb_ApplyMigration(db, 1,
                "CREATE TABLE text_records(key TEXT PRIMARY KEY NOT NULL,"
                "name TEXT)");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncConfigure(db, "text-client", 1, "text-v1");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncRegisterTable(db, "text_records", "key", columns, 2);
    }
    if (rc == PDB_OK) {
        rc = PDb_Exec(db,
                "INSERT INTO text_records(key,name) "
                "VALUES('alpha','local-text')");
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(db) != 1 ||
            PDb_SyncBuildRequest(db, request, sizeof(request),
            &request_length) != PDB_OK ||
            strstr(request, "\"entity\":\"text_records\"") == NULL ||
            strstr(request, "\"key\":\"alpha\"") == NULL ||
            strstr(request, "\"action\":\"upsert\"") == NULL ||
            strstr(request, "\"values\":null") != NULL ||
            PDb_SyncApplyResponse(db, 200, accepted_insert,
            (int)strlen(accepted_insert)) != PDB_OK ||
            PDb_SyncApplyResponse(db, 200, pulled,
            (int)strlen(pulled)) != PDB_OK ||
            !db_test_query_text_name(db, "alpha", name, sizeof(name)) ||
            strcmp(name, "remote-text") != 0) {
        if (db != NULL) {
            PDb_Close(db);
        }
        return FALSE;
    }
    rc = PDb_Exec(db, "DELETE FROM text_records WHERE key='alpha'");
    if (rc == PDB_OK) {
        rc = PDb_SyncBuildRequest(db, request, sizeof(request),
                &request_length);
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(db) != 1 ||
            strstr(request, "\"action\":\"delete\"") == NULL ||
            strstr(request, "\"base_version\":\"2\"") == NULL ||
            strstr(request, "\"values\":null") == NULL ||
            PDb_SyncApplyResponse(db, 200, accepted_delete,
            (int)strlen(accepted_delete)) != PDB_OK ||
            PDb_SyncPendingCount(db) != 0 ||
            db_test_query_text_name(db, "alpha", name, sizeof(name))) {
        if (db != NULL) {
            PDb_Close(db);
        }
        return FALSE;
    }
    PDb_Close(db);
    return TRUE;
}

static BOOL db_test_conflict_resolution(int action)
{
    static const PDbSyncColumn columns[] = {
        { "id", PDB_VALUE_INTEGER },
        { "name", PDB_VALUE_TEXT }
    };
    static const char accepted_insert[] =
        "{\"schema_version\":1,\"schema_hash\":\"conflict-v1\","
        "\"accepted\":[{\"op_id\":\"conflict-client:1\","
        "\"version\":\"1\"}],\"conflicts\":[],"
        "\"changes\":[],\"next_cursor\":\"1\"}";
    static const char pulled[] =
        "{\"schema_version\":1,\"schema_hash\":\"conflict-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[{"
        "\"entity\":\"records\",\"key\":\"1\","
        "\"version\":\"2\",\"deleted\":false,\"values\":{"
        "\"id\":{\"t\":\"i\",\"v\":\"1\"},"
        "\"name\":{\"t\":\"s\",\"v\":\"base\"}}}],"
        "\"next_cursor\":\"2\"}";
    static const char conflict[] =
        "{\"schema_version\":1,\"schema_hash\":\"conflict-v1\","
        "\"accepted\":[],\"conflicts\":[{"
        "\"op_id\":\"conflict-client:2\",\"entity\":\"records\","
        "\"key\":\"1\",\"server_version\":\"3\","
        "\"deleted\":false,\"values\":{"
        "\"id\":{\"t\":\"i\",\"v\":\"1\"},"
        "\"name\":{\"t\":\"s\",\"v\":\"server\"}}}],"
        "\"changes\":[],\"next_cursor\":\"3\"}";
    PDbHandle db;
    char name[64];
    int rc;

    db = NULL;
    rc = PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &db);
    if (rc == PDB_OK) {
        rc = PDb_ApplyMigration(db, 1,
                "CREATE TABLE records(id INTEGER PRIMARY KEY,name TEXT)");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncConfigure(db, "conflict-client", 1, "conflict-v1");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncRegisterTable(db, "records", "id", columns, 2);
    }
    if (rc == PDB_OK) {
        rc = PDb_Exec(db,
                "INSERT INTO records(id,name) VALUES(1,'initial')");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncApplyResponse(db, 200, accepted_insert,
                (int)strlen(accepted_insert));
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncApplyResponse(db, 200, pulled,
                (int)strlen(pulled));
    }
    if (rc == PDB_OK) {
        rc = PDb_Exec(db,
                "UPDATE records SET name='local-conflict' WHERE id=1");
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(db) != 1 ||
            PDb_SyncApplyResponse(db, 200, conflict,
            (int)strlen(conflict)) != PDB_OK ||
            PDb_SyncConflictCount(db) != 1 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "server") != 0 ||
            PDb_SyncResolveConflict(db, 1, action) != PDB_OK ||
            PDb_SyncConflictCount(db) != 0 ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "server") != 0) {
        if (db != NULL) {
            PDb_Close(db);
        }
        return FALSE;
    }
    PDb_Close(db);
    return TRUE;
}

static BOOL db_test_response_failures(void)
{
    static const PDbSyncColumn columns[] = {
        { "id", PDB_VALUE_INTEGER },
        { "name", PDB_VALUE_TEXT }
    };
    static const char accepted[] =
        "{\"schema_version\":1,\"schema_hash\":\"failure-v1\","
        "\"accepted\":[{\"op_id\":\"failure-client:1\","
        "\"version\":\"1\"}],\"conflicts\":[],"
        "\"changes\":[],\"next_cursor\":\"1\"}";
    static const char pulled[] =
        "{\"schema_version\":1,\"schema_hash\":\"failure-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[{"
        "\"entity\":\"records\",\"key\":\"1\","
        "\"version\":\"2\",\"deleted\":false,\"values\":{"
        "\"id\":{\"t\":\"i\",\"v\":\"1\"},"
        "\"name\":{\"t\":\"s\",\"v\":\"stable\"}}}],"
        "\"next_cursor\":\"2\"}";
    static const char schema_hash_mismatch[] =
        "{\"schema_version\":1,\"schema_hash\":\"other-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[],"
        "\"next_cursor\":\"3\"}";
    static const char schema_version_mismatch[] =
        "{\"schema_version\":2,\"schema_hash\":\"failure-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[],"
        "\"next_cursor\":\"3\"}";
    static const char cursor_backwards[] =
        "{\"schema_version\":1,\"schema_hash\":\"failure-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[],"
        "\"next_cursor\":\"1\"}";
    static const char typed_value_failure[] =
        "{\"schema_version\":1,\"schema_hash\":\"failure-v1\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":[{"
        "\"entity\":\"records\",\"key\":\"1\","
        "\"version\":\"3\",\"deleted\":false,\"values\":{"
        "\"id\":{\"t\":\"i\",\"v\":\"1\"},"
        "\"name\":{\"t\":\"x\",\"v\":\"invalid\"}}}],"
        "\"next_cursor\":\"3\"}";
    static const char missing_array[] =
        "{\"schema_version\":1,\"schema_hash\":\"failure-v1\","
        "\"accepted\":[],\"conflicts\":[],"
        "\"next_cursor\":\"3\"}";
    PDbHandle db;
    char name[64];
    char request[4096];
    char many_changes[4096];
    int index;
    int request_length;
    int rc;

    strcpy(many_changes,
            "{\"schema_version\":1,\"schema_hash\":\"failure-v1\","
            "\"accepted\":[],\"conflicts\":[],\"changes\":[");
    for (index = 0; index < 257; ++index) {
        if (index != 0) {
            strcat(many_changes, ",");
        }
        strcat(many_changes, "{}");
    }
    strcat(many_changes, "],\"next_cursor\":\"3\"}");

    db = NULL;
    rc = PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &db);
    if (rc == PDB_OK) {
        rc = PDb_ApplyMigration(db, 1,
                "CREATE TABLE records(id INTEGER PRIMARY KEY,name TEXT)");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncConfigure(db, "failure-client", 1, "failure-v1");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncRegisterTable(db, "records", "id", columns, 2);
    }
    if (rc == PDB_OK) {
        rc = PDb_Exec(db,
                "INSERT INTO records(id,name) VALUES(1,'initial')");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncApplyResponse(db, 200, accepted,
                (int)strlen(accepted));
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncApplyResponse(db, 200, pulled,
                (int)strlen(pulled));
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        if (db != NULL) {
            PDb_Close(db);
        }
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, schema_hash_mismatch,
            (int)strlen(schema_hash_mismatch)) != PDB_SCHEMA_MISMATCH ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, schema_version_mismatch,
            (int)strlen(schema_version_mismatch)) != PDB_SCHEMA_MISMATCH ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, cursor_backwards,
            (int)strlen(cursor_backwards)) == PDB_OK ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, typed_value_failure,
            (int)strlen(typed_value_failure)) == PDB_OK ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, missing_array,
            (int)strlen(missing_array)) == PDB_OK ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, many_changes,
            (int)strlen(many_changes)) != PDB_LIMIT ||
            PDb_SyncPendingCount(db) != 0 ||
            !db_test_query_name(db, name, sizeof(name)) ||
            strcmp(name, "stable") != 0) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_Exec(db,
            "UPDATE records SET name='after-failure' WHERE id=1") !=
            PDB_OK || PDb_SyncPendingCount(db) != 1 ||
            PDb_SyncBuildRequest(db, request, sizeof(request),
            &request_length) != PDB_OK ||
            strstr(request, "\"cursor\":\"2\"") == NULL ||
            strstr(request, "\"base_version\":\"2\"") == NULL ||
            strstr(request, "failure-client:2") == NULL) {
        PDb_Close(db);
        return FALSE;
    }
    PDb_Close(db);
    return TRUE;
}

static BOOL db_test_transaction_and_limits(void)
{
    PDbHandle db;
    PDbHandle sync;
    PDbStmtHandle stmt;
    char error[128];
    char copied_error[128];
    char tiny[1];
    int rc;

    db = NULL;
    sync = NULL;
    stmt = NULL;
    rc = PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db);
    if (rc == PDB_OK) {
        rc = PDb_Exec(db,
                "CREATE TABLE tx_values(id INTEGER PRIMARY KEY,value TEXT)");
    }
    if (rc != PDB_OK || PDb_Begin(db) != PDB_OK ||
            PDb_Begin(db) != PDB_STATE ||
            PDb_Exec(db,
            "INSERT INTO tx_values(id,value) VALUES(1,'rolled-back')") !=
            PDB_OK || PDb_Rollback(db) != PDB_OK ||
            PDb_Commit(db) != PDB_STATE ||
            !db_test_query_transaction_count(db, 0)) {
        if (db != NULL) {
            PDb_Close(db);
        }
        return FALSE;
    }
    if (PDb_Begin(db) != PDB_OK ||
            PDb_Exec(db,
            "INSERT INTO tx_values(id,value) VALUES(1,'committed')") !=
            PDB_OK || PDb_Commit(db) != PDB_OK ||
            !db_test_query_transaction_count(db, 1)) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_Exec(db, "THIS IS NOT SQL") == PDB_OK ||
            PDb_GetLastError(db, error, sizeof(error)) != PDB_OK ||
            error[0] == '\0' ||
            PDb_CopyLastError(db, copied_error, sizeof(copied_error)) !=
            PDB_OK || strcmp(error, copied_error) != 0) {
        PDb_Close(db);
        return FALSE;
    }
    tiny[0] = 'x';
    if (PDb_GetLastError(db, tiny, sizeof(tiny)) != PDB_BUFFER_TOO_SMALL ||
            tiny[0] != 'x' || PDb_Cancel(NULL) != PDB_INVALID_ARGUMENT) {
        PDb_Close(db);
        return FALSE;
    }
    if (PDb_Prepare(db, "SELECT 1", &stmt) != PDB_OK ||
            PDb_Cancel(db) != PDB_OK || PDb_Step(stmt) == PDB_STEP_ROW) {
        if (stmt != NULL) {
            PDb_Finalize(stmt);
        }
        PDb_Close(db);
        return FALSE;
    }
    PDb_Finalize(stmt);
    stmt = NULL;
    PDb_Close(db);
    db = NULL;

    rc = PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &sync);
    if (rc != PDB_OK ||
            PDb_SyncApplyResponse(sync, 200, "{}",
            PDB_SYNC_MAX_BODY_BYTES + 1) != PDB_LIMIT ||
            PDb_SyncApplyResponse(sync, 200, NULL, 0) !=
            PDB_INVALID_ARGUMENT) {
        if (sync != NULL) {
            PDb_Close(sync);
        }
        return FALSE;
    }
    PDb_Close(sync);
    return TRUE;
}

static BOOL db_test_request_paging(void)
{
    static const PDbSyncColumn columns[] = {
        { "id", PDB_VALUE_INTEGER },
        { "name", PDB_VALUE_TEXT }
    };
    PDbHandle db;
    PDbStmtHandle stmt;
    char request[32768];
    int index;
    int request_length;
    int rc;

    db = NULL;
    stmt = NULL;
    rc = PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &db);
    if (rc == PDB_OK) {
        rc = PDb_ApplyMigration(db, 1,
                "CREATE TABLE page_records(id INTEGER PRIMARY KEY,name TEXT)");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncConfigure(db, "page-client", 1, "page-v1");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncRegisterTable(db, "page_records", "id", columns, 2);
    }
    for (index = 1; rc == PDB_OK && index <= 65; ++index) {
        stmt = NULL;
        rc = PDb_Prepare(db,
                "INSERT INTO page_records(id,name) VALUES(?1,?2)",
                &stmt);
        if (rc == PDB_OK) {
            rc = PDb_BindInt64(stmt, 1, index);
        }
        if (rc == PDB_OK) {
            rc = PDb_BindText(stmt, 2, "page", 4);
        }
        if (rc == PDB_OK && PDb_Step(stmt) != PDB_STEP_DONE) {
            rc = PDB_ERROR;
        }
        if (stmt != NULL) {
            PDb_Finalize(stmt);
            stmt = NULL;
        }
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(db) != 65 ||
            PDb_SyncBuildRequest(db, request, sizeof(request),
            &request_length) != PDB_OK || request_length <= 0 ||
            strstr(request, "\"push_more\":true") == NULL ||
            strstr(request, "\"pull_limit\":64") == NULL ||
            strstr(request, "page-client:64") == NULL ||
            strstr(request, "page-client:65") != NULL) {
        if (stmt != NULL) {
            PDb_Finalize(stmt);
        }
        if (db != NULL) {
            PDb_Close(db);
        }
        return FALSE;
    }
    PDb_Close(db);
    return TRUE;
}

static BOOL db_test_persistence(void)
{
    static const char path[] = "\\Temp\\positron-db-1321.sqlite";
    static const WCHAR delete_path[] = L"\\Temp\\positron-db-1321.sqlite";
    static const char accepted[] =
        "{\"schema_version\":1,\"schema_hash\":\"persist-v1\","
        "\"accepted\":[{\"op_id\":\"persist-client:1\","
        "\"version\":\"1\"}],\"conflicts\":[],"
        "\"changes\":[],\"next_cursor\":\"1\"}";
    static const PDbSyncColumn columns[] = {
        { "id", PDB_VALUE_INTEGER },
        { "name", PDB_VALUE_TEXT }
    };
    PDbHandle db;
    char request[2048];
    int request_length;
    int rc;

    DeleteFileW(delete_path);
    db = NULL;
    rc = PDb_OpenUtf8(path, PDB_OPEN_SYNC, &db);
    if (rc != PDB_OK || db == NULL) {
        return FALSE;
    }
    rc = PDb_ApplyMigration(db, 1,
            "CREATE TABLE persisted(id INTEGER PRIMARY KEY,name TEXT)");
    if (rc == PDB_OK) {
        rc = PDb_SyncConfigure(db, "persist-client", 1, "persist-v1");
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncRegisterTable(db, "persisted", "id", columns, 2);
    }
    if (rc == PDB_OK) {
        rc = PDb_Exec(db,
                "INSERT INTO persisted(id,name) VALUES(7,'saved')");
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(db) != 1) {
        PDb_Close(db);
        DeleteFileW(delete_path);
        return FALSE;
    }
    PDb_Close(db);
    db = NULL;
    rc = PDb_OpenUtf8(path, PDB_OPEN_SYNC, &db);
    if (rc == PDB_OK && db == NULL) {
        rc = PDB_ERROR;
    }
    if (rc == PDB_OK && PDb_SyncPendingCount(db) != 1) {
        rc = PDB_ERROR;
    }
    if (rc == PDB_OK) {
        rc = PDb_SyncBuildRequest(db, request, sizeof(request),
                &request_length);
    }
    if (rc != PDB_OK || request_length <= 0 ||
            strstr(request, "persist-client:1") == NULL) {
        if (db != NULL) {
            PDb_Close(db);
        }
        DeleteFileW(delete_path);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(db, 200, accepted,
            (int)strlen(accepted)) != PDB_OK ||
            PDb_SyncPendingCount(db) != 0) {
        PDb_Close(db);
        DeleteFileW(delete_path);
        return FALSE;
    }
    PDb_Close(db);
    db = NULL;
    rc = PDb_OpenUtf8(path, PDB_OPEN_SYNC, &db);
    if (rc == PDB_OK) {
        rc = PDb_SyncBuildRequest(db, request, sizeof(request),
                &request_length);
    }
    if (rc != PDB_OK || strstr(request, "\"cursor\":\"1\"") == NULL ||
            strstr(request, "\"push\":[]") == NULL) {
        if (db != NULL) {
            PDb_Close(db);
        }
        DeleteFileW(delete_path);
        return FALSE;
    }
    PDb_Close(db);
    DeleteFileW(delete_path);
    return TRUE;
}

BOOL test1321_db_contract(void)
{
    static const PDbSyncColumn columns[] = {
        { "id", PDB_VALUE_INTEGER },
        { "name", PDB_VALUE_TEXT },
        { "payload", PDB_VALUE_BLOB }
    };
    static const PDbSyncColumn composite_columns[] = {
        { "a", PDB_VALUE_INTEGER },
        { "b", PDB_VALUE_INTEGER },
        { "name", PDB_VALUE_TEXT }
    };
    static const char accepted[] =
        "{\"schema_version\":2,\"schema_hash\":\"schema-v2\","
        "\"accepted\":[{\"op_id\":\"device-1:1\",\"version\":\"1\"}],"
        "\"conflicts\":[],\"changes\":[],\"next_cursor\":\"1\"}";
    static const char pulled[] =
        "{\"schema_version\":2,\"schema_hash\":\"schema-v2\",\"accepted\":[],"
        "\"conflicts\":[],\"changes\":[{\"entity\":\"records\","
        "\"key\":\"1\",\"version\":\"2\",\"deleted\":false,"
        "\"values\":{\"id\":{\"t\":\"i\",\"v\":\"1\"},"
        "\"name\":{\"t\":\"s\",\"v\":\"remote\"},"
        "\"payload\":{\"t\":\"b\",\"v\":\"AQI=\"}}}],"
        "\"next_cursor\":\"2\"}";
    static const char conflict[] =
        "{\"schema_version\":2,\"schema_hash\":\"schema-v2\",\"accepted\":[],"
        "\"conflicts\":[{\"op_id\":\"device-1:2\","
        "\"entity\":\"records\",\"key\":\"1\","
        "\"server_version\":\"3\",\"deleted\":false,"
        "\"values\":{\"id\":{\"t\":\"i\",\"v\":\"1\"},"
        "\"name\":{\"t\":\"s\",\"v\":\"server\"},"
        "\"payload\":{\"t\":\"b\",\"v\":\"AwQ=\"}}}],"
        "\"changes\":[],\"next_cursor\":\"3\"}";
    static const char retry_accepted[] =
        "{\"schema_version\":2,\"schema_hash\":\"schema-v2\","
        "\"accepted\":[{\"op_id\":\"device-1:2\",\"version\":\"4\"}],"
        "\"conflicts\":[],\"changes\":[],\"next_cursor\":\"4\"}";
    static const char deleted[] =
        "{\"schema_version\":2,\"schema_hash\":\"schema-v2\","
        "\"accepted\":[],\"conflicts\":[],\"changes\":["
        "{\"entity\":\"records\",\"key\":\"1\",\"version\":\"5\","
        "\"deleted\":true,\"values\":null}],\"next_cursor\":\"5\"}";
    PDbHandle local;
    PDbHandle sync;
    PDbStmtHandle stmt;
    char request[8192];
    char name[64];
    int request_length;
    int conflict_length;
    int rc;
    __int64 minimum_integer;
    __int64 maximum_integer;
    char conflict_json[1024];
    unsigned char blob[2];

    if (!db_test_persistence() || !db_test_text_key_and_delete() ||
            !db_test_conflict_resolution(PDB_CONFLICT_ACCEPT_SERVER) ||
            !db_test_conflict_resolution(PDB_CONFLICT_DISCARD) ||
            !db_test_response_failures() ||
            !db_test_transaction_and_limits() ||
            !db_test_request_paging()) {
        return FALSE;
    }
    local = NULL;
    if (PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &local) != PDB_OK) {
        return FALSE;
    }
    if (PDb_Exec(local,
            "CREATE TABLE values_table(id INTEGER PRIMARY KEY,value TEXT)") !=
            PDB_OK || PDb_Exec(local,
            "INSERT INTO values_table(id,value) VALUES(1,'local')") != PDB_OK) {
        PDb_Close(local);
        return FALSE;
    }
    if (PDb_Exec(local,
            "CREATE TABLE script_values(id INTEGER PRIMARY KEY);"
            "INSERT INTO script_values(id) VALUES(1)") != PDB_OK) {
        PDb_Close(local);
        return FALSE;
    }
    if (PDb_Exec(local,
            "CREATE TABLE typed_values(id INTEGER PRIMARY KEY,big INTEGER,"
            "max_big INTEGER,real_value REAL,text_value TEXT,"
            "blob_value BLOB,null_value TEXT,empty_blob_value BLOB)") !=
            PDB_OK) {
        PDb_Close(local);
        return FALSE;
    }
    minimum_integer = -(__int64)0x7FFFFFFFFFFFFFFF - 1;
    maximum_integer = (__int64)0x7FFFFFFFFFFFFFFF;
    blob[0] = 0xA5;
    blob[1] = 0x5A;
    stmt = NULL;
    rc = PDb_Prepare(local,
            "INSERT INTO typed_values(id,big,max_big,real_value,text_value,"
            "blob_value,null_value,empty_blob_value) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", &stmt);
    if (rc == PDB_OK) {
        rc = PDb_BindInt64(stmt, 1, 1);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindInt64(stmt, 2, minimum_integer);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindInt64(stmt, 3, maximum_integer);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindDouble(stmt, 4, 3.25);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindText(stmt, 5, "utf8", 4);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindBlob(stmt, 6, blob, sizeof(blob));
    }
    if (rc == PDB_OK) {
        rc = PDb_BindNull(stmt, 7);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindBlob(stmt, 8, NULL, 0);
    }
    if (rc == PDB_OK && PDb_Step(stmt) == PDB_STEP_DONE) {
        rc = PDB_OK;
    } else if (rc == PDB_OK) {
        rc = PDB_ERROR;
    }
    PDb_Finalize(stmt);
    if (rc != PDB_OK) {
        PDb_Close(local);
        return FALSE;
    }
    stmt = NULL;
    rc = PDb_Prepare(local,
            "SELECT big,max_big,real_value,text_value,blob_value,"
            "null_value,empty_blob_value "
            "FROM typed_values WHERE id=1", &stmt);
    if (rc == PDB_OK) {
        rc = PDb_Step(stmt);
    }
    if (rc != PDB_STEP_ROW || PDb_ColumnCount(stmt) != 7 ||
            PDb_ColumnType(stmt, 0) != PDB_VALUE_INTEGER ||
            PDb_ColumnInt64(stmt, 0) != minimum_integer ||
            PDb_ColumnType(stmt, 1) != PDB_VALUE_INTEGER ||
            PDb_ColumnInt64(stmt, 1) != maximum_integer ||
            PDb_ColumnType(stmt, 2) != PDB_VALUE_REAL ||
            PDb_ColumnDouble(stmt, 2) != 3.25 ||
            PDb_ColumnType(stmt, 3) != PDB_VALUE_TEXT ||
            PDb_ColumnText(stmt, 3) == NULL ||
            strcmp(PDb_ColumnText(stmt, 3), "utf8") != 0 ||
            PDb_ColumnType(stmt, 4) != PDB_VALUE_BLOB ||
            PDb_ColumnBytes(stmt, 4) != (int)sizeof(blob) ||
            PDb_ColumnBlob(stmt, 4) == NULL ||
            memcmp(PDb_ColumnBlob(stmt, 4), blob, sizeof(blob)) != 0 ||
            PDb_ColumnType(stmt, 5) != PDB_VALUE_NULL ||
            PDb_ColumnType(stmt, 6) != PDB_VALUE_BLOB ||
            PDb_ColumnBytes(stmt, 6) != 0) {
        PDb_Finalize(stmt);
        PDb_Close(local);
        return FALSE;
    }
    PDb_Finalize(stmt);
    PDb_Close(local);

    sync = NULL;
    if (PDb_OpenUtf8(":memory:", PDB_OPEN_SYNC, &sync) != PDB_OK ||
            PDb_ApplyMigration(sync, 1,
            "CREATE TABLE records(id INTEGER PRIMARY KEY,name TEXT,payload BLOB)") !=
            PDB_OK) {
        if (sync != NULL) {
            PDb_Close(sync);
        }
        return FALSE;
    }
    if (PDb_Exec(sync, "CREATE TABLE rejected(id INTEGER PRIMARY KEY)") ==
            PDB_OK || PDb_Exec(sync,
            "ATTACH ':memory:' AS other") == PDB_OK ||
            PDb_Exec(sync, "PRAGMA user_version=1") == PDB_OK ||
            PDb_Exec(sync, "SAVEPOINT rejected") == PDB_OK ||
            PDb_Exec(sync,
            "CREATE VIRTUAL TABLE rejected_vtab USING fts5(value)") == PDB_OK ||
            PDb_ApplyMigration(sync, 2,
            "CREATE TABLE migration_bad(id INTEGER PRIMARY KEY); THIS IS NOT VALID") ==
            PDB_OK || PDb_Exec(sync,
            "SELECT count(*) FROM migration_bad") == PDB_OK ||
            PDb_ApplyMigration(sync, 2,
            "CREATE TABLE migration_ok(id INTEGER PRIMARY KEY);"
            "CREATE TABLE composite_records(a INTEGER,b INTEGER,name TEXT,"
            "PRIMARY KEY(a,b))") != PDB_OK ||
            PDb_SyncConfigure(sync, "device-1", 2, "schema-v2") != PDB_OK ||
            PDb_SyncRegisterTable(sync, "composite_records", "a",
            composite_columns, 3) != PDB_SCHEMA_MISMATCH ||
            PDb_SyncRegisterTable(sync, "records", "id", columns, 3) != PDB_OK) {
        if (sync != NULL) {
            PDb_Close(sync);
        }
        return FALSE;
    }
    rc = PDb_Prepare(sync,
            "INSERT INTO records(id,name,payload) VALUES(?1,?2,?3)", &stmt);
    if (rc == PDB_OK) {
        rc = PDb_BindInt64(stmt, 1, 1);
    }
    if (rc == PDB_OK) {
        rc = PDb_BindText(stmt, 2, "local", 5);
    }
    if (rc == PDB_OK) {
        blob[0] = 0x01;
        blob[1] = 0x02;
        rc = PDb_BindBlob(stmt, 3, blob, 2);
    }
    if (rc == PDB_OK) {
        rc = PDb_Step(stmt);
        if (rc == PDB_STEP_DONE) {
            rc = PDB_OK;
        }
    }
    PDb_Finalize(stmt);
    request[0] = 'x';
    if (PDb_SyncBuildRequest(sync, request, 1, &request_length) !=
            PDB_BUFFER_TOO_SMALL || request[0] != 'x') {
        PDb_Close(sync);
        return FALSE;
    }
    if (rc != PDB_OK || PDb_SyncPendingCount(sync) != 1 ||
            PDb_SyncBuildRequest(sync, NULL, 0, &request_length) != PDB_OK ||
            request_length <= 0 || request_length >= (int)sizeof(request) ||
            PDb_SyncBuildRequest(sync, request, sizeof(request),
            &request_length) != PDB_OK ||
            strstr(request, "device-1:1") == NULL ||
            strstr(request, "local") == NULL ||
            strstr(request, "\"payload\":{\"t\":\"b\",\"v\":\"AQI=\"}") == NULL ||
            strstr(request, "CREATE TABLE") != NULL ||
            strstr(request, "SELECT ") != NULL) {
        PDb_Close(sync);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(sync, 401, NULL, 0) != PDB_NETWORK ||
            PDb_SyncApplyResponse(sync, 500, accepted,
            (int)strlen(accepted)) != PDB_NETWORK ||
            PDb_SyncPendingCount(sync) != 1 ||
            PDb_SyncApplyResponse(sync, 200, "{", 1) == PDB_OK ||
            PDb_SyncPendingCount(sync) != 1) {
        PDb_Close(sync);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(sync, 200, accepted,
            (int)strlen(accepted)) != PDB_OK ||
            PDb_SyncPendingCount(sync) != 0 ||
            PDb_SyncApplyResponse(sync, 200, accepted,
            (int)strlen(accepted)) != PDB_OK ||
            PDb_SyncApplyResponse(sync, 200, pulled,
            (int)strlen(pulled)) != PDB_OK ||
            !db_test_query_name(sync, name, sizeof(name)) ||
            strcmp(name, "remote") != 0) {
        PDb_Close(sync);
        return FALSE;
    }
    stmt = NULL;
    rc = PDb_Prepare(sync,
            "SELECT payload FROM records WHERE id=1", &stmt);
    if (rc == PDB_OK) {
        rc = PDb_Step(stmt);
    }
    blob[0] = 0x03;
    blob[1] = 0x04;
    if (rc != PDB_STEP_ROW || PDb_ColumnCount(stmt) != 1 ||
            PDb_ColumnType(stmt, 0) != PDB_VALUE_BLOB ||
            PDb_ColumnBytes(stmt, 0) != (int)sizeof(blob) ||
            PDb_ColumnBlob(stmt, 0) == NULL ||
            memcmp(PDb_ColumnBlob(stmt, 0), blob, sizeof(blob)) != 0) {
        if (stmt != NULL) {
            PDb_Finalize(stmt);
        }
        PDb_Close(sync);
        return FALSE;
    }
    PDb_Finalize(stmt);
    if (PDb_Exec(sync, "UPDATE records SET name='local-edit' WHERE id=1") !=
            PDB_OK || PDb_SyncPendingCount(sync) != 1 ||
            PDb_SyncApplyResponse(sync, 200, conflict,
            (int)strlen(conflict)) != PDB_OK ||
            PDb_SyncConflictCount(sync) != 1 ||
            !db_test_query_name(sync, name, sizeof(name)) ||
            strcmp(name, "server") != 0) {
        PDb_Close(sync);
        return FALSE;
    }
    if (PDb_SyncCopyConflict(sync, 0, NULL, 0, &conflict_length) != PDB_OK ||
            conflict_length <= 0 || conflict_length >= (int)sizeof(conflict_json) ||
            PDb_SyncGetConflicts(sync, NULL, 0, &conflict_length) != PDB_OK ||
            conflict_length <= 0 || conflict_length >= (int)sizeof(conflict_json) ||
            PDb_SyncGetConflicts(sync, conflict_json, sizeof(conflict_json),
            &conflict_length) != PDB_OK ||
            strstr(conflict_json, "server_values") == NULL ||
            PDb_SyncCopyConflict(sync, 0, conflict_json,
            sizeof(conflict_json), &conflict_length) != PDB_OK ||
            strstr(conflict_json, "local-edit") == NULL ||
            strstr(conflict_json, "server") == NULL) {
        PDb_Close(sync);
        return FALSE;
    }
    if (PDb_SyncResolveConflict(sync, 1, PDB_CONFLICT_RETRY_LOCAL) != PDB_OK ||
            PDb_SyncPendingCount(sync) != 1 ||
            !db_test_query_name(sync, name, sizeof(name)) ||
            strcmp(name, "local-edit") != 0 ||
            PDb_SyncConflictCount(sync) != 0) {
        PDb_Close(sync);
        return FALSE;
    }
    if (PDb_SyncApplyResponse(sync, 200, retry_accepted,
            (int)strlen(retry_accepted)) != PDB_OK ||
            PDb_SyncPendingCount(sync) != 0 ||
            PDb_SyncApplyResponse(sync, 200, deleted,
            (int)strlen(deleted)) != PDB_OK ||
            db_test_query_name(sync, name, sizeof(name))) {
        PDb_Close(sync);
        return FALSE;
    }
    PDb_Close(sync);
    return TRUE;
}
