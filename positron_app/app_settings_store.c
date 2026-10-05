#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "app_settings_store.h"
#include "app_debug.h"

/* No import library dependency until the DB production device gates pass. */
typedef struct AppSettingsDbApi {
    int (*open)(const char *, int, PDbHandle *, PDbErrorInfo *);
    void (*close)(PDbHandle);
    int (*prepare)(PDbHandle, const char *, PDbStmtHandle *);
    int (*bind_text)(PDbStmtHandle, int, const char *, int);
    int (*step)(PDbStmtHandle);
    int (*finalize)(PDbStmtHandle);
    int (*column_type)(PDbStmtHandle, int);
    __int64 (*column_int)(PDbStmtHandle, int);
    const char *(*column_text)(PDbStmtHandle, int);
    int (*column_bytes)(PDbStmtHandle, int);
    int (*begin)(PDbHandle);
    int (*commit)(PDbHandle);
    int (*rollback)(PDbHandle);
    int (*migration)(PDbHandle, int, const char *);
    int (*error)(PDbHandle, PDbErrorInfo *);
    int (*state)(PDbHandle, PDbConnectionState *);
} AppSettingsDbApi;

typedef struct AppSettingsJob {
    unsigned long request_id;
    unsigned long tab_id;
    unsigned long generation;
    int operation;
    AppSettingsStartPage start_page;
} AppSettingsJob;

struct AppSettingsStore {
    DWORD owner_thread;
    HANDLE worker;
    HANDLE wake;
    CRITICAL_SECTION lock;
    int closing;
    unsigned long sequence;
    int outstanding;
    int job_head;
    int job_count;
    int result_head;
    int result_count;
    AppSettingsJob jobs[APP_SETTINGS_QUEUE_MAX];
    AppSettingsResult results[APP_SETTINGS_QUEUE_MAX];
    WCHAR dll_path[APP_SETTINGS_PATH_MAX];
    char database_path[APP_SETTINGS_PATH_MAX];
};

static const char g_settings_schema[] =
    "CREATE TABLE app_settings ("
    "id INTEGER PRIMARY KEY CHECK(id=1),"
    "schema_version INTEGER NOT NULL CHECK(schema_version=1),"
    "startup_page TEXT NOT NULL CHECK(startup_page IN ("
    "'positron://newtab','positron://welcome','positron://controls')));"
    "INSERT INTO app_settings VALUES(1,1,'positron://newtab');";

static const char g_settings_read_sql[] =
    "SELECT schema_version,startup_page FROM app_settings WHERE id=1";
static const char g_settings_write_sql[] =
    "UPDATE app_settings SET startup_page=?1 WHERE id=1 AND schema_version=1";
static const char g_settings_exists_sql[] =
    "SELECT count(*) FROM sqlite_master WHERE type='table' "
    "AND name='app_settings'";

const char *AppSettingsStore_StartPageUrl(AppSettingsStartPage page)
{
    switch (page) {
    case APP_SETTINGS_START_NEWTAB: return "positron://newtab";
    case APP_SETTINGS_START_WELCOME: return "positron://welcome";
    case APP_SETTINGS_START_CONTROLS: return "positron://controls";
    default: return NULL;
    }
}

static void app_settings_result_init(AppSettingsResult *result,
        const AppSettingsJob *job)
{
    memset(result, 0, sizeof(*result));
    result->request_id = job->request_id;
    result->tab_id = job->tab_id;
    result->generation = job->generation;
    result->operation = job->operation;
    result->start_page = APP_SETTINGS_START_NEWTAB;
    result->error.size = sizeof(result->error);
    result->error.version = PDB_ERROR_INFO_VERSION;
    result->state.size = sizeof(result->state);
    result->state.version = PDB_CONNECTION_STATE_VERSION;
}

static int app_settings_db_load(HMODULE module, AppSettingsDbApi *api)
{
    /* Preserve the public cdecl calling convention (not WINAPI). */
    api->open = (int (*)(const char *, int, PDbHandle *, PDbErrorInfo *))
            GetProcAddress(module, TEXT("PDb_OpenUtf8Ex"));
    api->close = (void (*)(PDbHandle))
            GetProcAddress(module, TEXT("PDb_Close"));
    api->prepare = (int (*)(PDbHandle, const char *, PDbStmtHandle *))
            GetProcAddress(module, TEXT("PDb_Prepare"));
    api->bind_text = (int (*)(PDbStmtHandle, int, const char *, int))
            GetProcAddress(module, TEXT("PDb_BindText"));
    api->step = (int (*)(PDbStmtHandle))
            GetProcAddress(module, TEXT("PDb_Step"));
    api->finalize = (int (*)(PDbStmtHandle))
            GetProcAddress(module, TEXT("PDb_Finalize"));
    api->column_type = (int (*)(PDbStmtHandle, int))
            GetProcAddress(module, TEXT("PDb_ColumnType"));
    api->column_int = (__int64 (*)(PDbStmtHandle, int))
            GetProcAddress(module, TEXT("PDb_ColumnInt64"));
    api->column_text = (const char *(*)(PDbStmtHandle, int))
            GetProcAddress(module, TEXT("PDb_ColumnText"));
    api->column_bytes = (int (*)(PDbStmtHandle, int))
            GetProcAddress(module, TEXT("PDb_ColumnBytes"));
    api->begin = (int (*)(PDbHandle))
            GetProcAddress(module, TEXT("PDb_Begin"));
    api->commit = (int (*)(PDbHandle))
            GetProcAddress(module, TEXT("PDb_Commit"));
    api->rollback = (int (*)(PDbHandle))
            GetProcAddress(module, TEXT("PDb_Rollback"));
    api->migration = (int (*)(PDbHandle, int, const char *))
            GetProcAddress(module, TEXT("PDb_ApplyMigration"));
    api->error = (int (*)(PDbHandle, PDbErrorInfo *))
            GetProcAddress(module, TEXT("PDb_GetErrorInfo"));
    api->state = (int (*)(PDbHandle, PDbConnectionState *))
            GetProcAddress(module, TEXT("PDb_GetConnectionState"));
    return api->open != NULL && api->close != NULL &&
            api->prepare != NULL && api->bind_text != NULL &&
            api->step != NULL && api->finalize != NULL &&
            api->column_type != NULL && api->column_int != NULL &&
            api->column_text != NULL && api->column_bytes != NULL &&
            api->begin != NULL && api->commit != NULL &&
            api->rollback != NULL && api->migration != NULL &&
            api->error != NULL && api->state != NULL;
}

static void app_settings_db_failure(AppSettingsDbApi *api, PDbHandle db,
        int rc, AppSettingsResult *result)
{
    result->result = APP_SETTINGS_DB_FAILED;
    result->db_result = rc;
    (void) api->error(db, &result->error);
}

static void app_settings_finish_statement(AppSettingsDbApi *api,
        PDbHandle db, PDbStmtHandle statement, AppSettingsResult *result)
{
    int rc;

    if (statement == NULL) return;
    rc = api->finalize(statement);
    if (rc != PDB_OK && result->result == APP_SETTINGS_OK)
        app_settings_db_failure(api, db, rc, result);
}

static void app_settings_read(AppSettingsDbApi *api, PDbHandle db,
        AppSettingsResult *result)
{
    PDbStmtHandle statement;
    const char *text;
    const char *url;
    int bytes;
    int page;
    int matched;
    int rc;

    statement = NULL;
    rc = api->prepare(db, g_settings_read_sql, &statement);
    if (rc == PDB_OK) rc = api->step(statement);
    if (rc != PDB_STEP_ROW) {
        if (rc == PDB_STEP_DONE) result->result = APP_SETTINGS_BAD_DATA;
        else app_settings_db_failure(api, db, rc, result);
        app_settings_finish_statement(api, db, statement, result);
        return;
    }
    if (api->column_type(statement, 0) != PDB_VALUE_INTEGER ||
            api->column_int(statement, 0) != 1 ||
            api->column_type(statement, 1) != PDB_VALUE_TEXT) {
        result->result = APP_SETTINGS_BAD_DATA;
    } else {
        text = api->column_text(statement, 1);
        bytes = api->column_bytes(statement, 1);
        matched = 0;
        for (page = APP_SETTINGS_START_NEWTAB;
                page <= APP_SETTINGS_START_CONTROLS; ++page) {
            url = AppSettingsStore_StartPageUrl((AppSettingsStartPage) page);
            if (text != NULL && bytes == (int) strlen(url) &&
                    memcmp(text, url, (size_t) bytes) == 0) {
                result->start_page = (AppSettingsStartPage) page;
                matched = 1;
                break;
            }
        }
        if (!matched) result->result = APP_SETTINGS_BAD_DATA;
    }
    if (result->result == APP_SETTINGS_OK) {
        rc = api->step(statement);
        if (rc == PDB_STEP_ROW) result->result = APP_SETTINGS_BAD_DATA;
        else if (rc != PDB_STEP_DONE)
            app_settings_db_failure(api, db, rc, result);
    }
    app_settings_finish_statement(api, db, statement, result);
}

static void app_settings_initialize(AppSettingsDbApi *api, PDbHandle db,
        AppSettingsResult *result)
{
    PDbStmtHandle statement;
    int exists;
    int rc;

    statement = NULL;
    exists = 0;
    rc = api->prepare(db, g_settings_exists_sql, &statement);
    if (rc == PDB_OK) rc = api->step(statement);
    if (rc != PDB_STEP_ROW) {
        app_settings_db_failure(api, db, rc, result);
    } else if (api->column_type(statement, 0) != PDB_VALUE_INTEGER ||
            api->column_int(statement, 0) < 0 ||
            api->column_int(statement, 0) > 1) {
        result->result = APP_SETTINGS_BAD_DATA;
    } else {
        exists = (int) api->column_int(statement, 0);
        rc = api->step(statement);
        if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
    }
    app_settings_finish_statement(api, db, statement, result);
    if (result->result != APP_SETTINGS_OK) return;
    /* ApplyMigration rejects a repeated nonempty script. Probe the app-owned
     * table, never read/write DLL-private __pdb_meta. Existing rows must pass
     * the v1 projection before the empty migration-version check. */
    if (exists) {
        app_settings_read(api, db, result);
        if (result->result != APP_SETTINGS_OK) return;
    }
    rc = api->migration(db, 1, exists ? "" : g_settings_schema);
    if (rc != PDB_OK) {
        app_settings_db_failure(api, db, rc, result);
        return;
    }
    app_settings_read(api, db, result);
}

static void app_settings_cleanup_transaction(AppSettingsDbApi *api,
        PDbHandle db, AppSettingsResult *result)
{
    result->state_valid = api->state(db, &result->state) == PDB_OK;
    if ((!result->state_valid || result->state.transaction_active) &&
            result->result == APP_SETTINGS_OK) {
        result->result = APP_SETTINGS_DB_FAILED;
        result->db_result = PDB_STATE;
    }
    if (result->result != APP_SETTINGS_OK && result->state_valid &&
            result->state.transaction_active) {
        result->rollback_result = api->rollback(db);
        result->state_valid = api->state(db, &result->state) == PDB_OK;
    }
}

static void app_settings_save(AppSettingsDbApi *api, PDbHandle db,
        const AppSettingsJob *job, AppSettingsResult *result)
{
    PDbStmtHandle statement;
    const char *url;
    int rc;

    statement = NULL;
    url = AppSettingsStore_StartPageUrl(job->start_page);
    rc = api->begin(db);
    if (rc == PDB_OK) rc = api->prepare(db, g_settings_write_sql, &statement);
    if (rc == PDB_OK) rc = api->bind_text(statement, 1, url, (int) strlen(url));
    if (rc == PDB_OK) rc = api->step(statement);
    if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
    app_settings_finish_statement(api, db, statement, result);
    if (result->result == APP_SETTINGS_OK) {
        /* Detect a missing/malformed row or a no-op UPDATE before COMMIT. */
        app_settings_read(api, db, result);
        if (result->result == APP_SETTINGS_OK &&
                result->start_page != job->start_page)
            result->result = APP_SETTINGS_BAD_DATA;
    }
    if (result->result == APP_SETTINGS_OK) {
        rc = api->commit(db);
        if (rc != PDB_OK) app_settings_db_failure(api, db, rc, result);
    }
}

static void app_settings_publish(AppSettingsStore *store,
        const AppSettingsResult *result)
{
    int tail;

    EnterCriticalSection(&store->lock);
    /* outstanding includes the executing job and unread completions: room
     * is guaranteed without waiting on the UI or dropping a committed ACK. */
    tail = (store->result_head + store->result_count) % APP_SETTINGS_QUEUE_MAX;
    store->results[tail] = *result;
    store->result_count++;
    LeaveCriticalSection(&store->lock);
}

static DWORD WINAPI app_settings_worker(LPVOID argument)
{
    AppSettingsStore *store;
    AppSettingsDbApi api;
    HMODULE module;
    PDbHandle db;
    AppSettingsJob job;
    AppSettingsResult result;
    AppSettingsResult failure;
    int healthy;
    int stop;
    int found;
    int rc;

    store = (AppSettingsStore *) argument;
    memset(&api, 0, sizeof(api));
    memset(&job, 0, sizeof(job));
    job.request_id = 1;
    job.operation = APP_SETTINGS_LOAD;
    app_settings_result_init(&result, &job);
    db = NULL;
    module = LoadLibraryW(store->dll_path);
    if (module == NULL || !app_settings_db_load(module, &api)) {
        result.result = APP_SETTINGS_DLL_UNAVAILABLE;
    } else {
        rc = api.open(store->database_path, PDB_OPEN_LOCAL_FULL_SQL,
                &db, &result.error);
        if (rc != PDB_OK) {
            result.result = APP_SETTINGS_DB_FAILED;
            result.db_result = rc;
        } else {
            app_settings_initialize(&api, db, &result);
            app_settings_cleanup_transaction(&api, db, &result);
        }
    }
    healthy = result.result == APP_SETTINGS_OK;
    failure = result;
    app_settings_publish(store, &result);
    for (;;) {
        EnterCriticalSection(&store->lock);
        found = store->job_count != 0;
        if (found) {
            job = store->jobs[store->job_head];
            store->job_head = (store->job_head + 1) % APP_SETTINGS_QUEUE_MAX;
            store->job_count--;
        }
        stop = store->closing && !found;
        if (!found && !stop) ResetEvent(store->wake);
        LeaveCriticalSection(&store->lock);
        if (stop) break;
        if (!found) {
            if (WaitForSingleObject(store->wake, INFINITE) != WAIT_OBJECT_0) {
                /* Refuse admission but settle all already accepted jobs. */
                healthy = 0;
                failure.result = APP_SETTINGS_PLATFORM_FAILED;
                EnterCriticalSection(&store->lock);
                store->closing = 1;
                LeaveCriticalSection(&store->lock);
            }
            continue;
        }
        app_settings_result_init(&result, &job);
        if (!healthy) {
            result.result = failure.result;
            result.db_result = failure.db_result;
            result.error = failure.error;
            result.state = failure.state;
            result.state_valid = failure.state_valid;
            result.rollback_result = failure.rollback_result;
        } else {
            if (job.operation == APP_SETTINGS_SAVE)
                app_settings_save(&api, db, &job, &result);
            else app_settings_read(&api, db, &result);
            app_settings_cleanup_transaction(&api, db, &result);
            if (result.result != APP_SETTINGS_OK) {
                /* Conservative failure policy: close/reopen explicitly;
                 * never silently retry, reset a value or delete the file. */
                healthy = 0;
                failure = result;
            }
        }
        app_settings_publish(store, &result);
    }
    if (db != NULL) api.close(db);
    if (module != NULL) FreeLibrary(module);
    return 0;
}

static int app_settings_owner(AppSettingsStore *store)
{
    return store != NULL && store->owner_thread == GetCurrentThreadId();
}

int AppSettingsStore_Create(const WCHAR *dll_path, const char *database_path,
        AppSettingsStore **out_store)
{
    AppSettingsStore *store;
    size_t dll_length;
    size_t path_length;

    if (dll_path == NULL || database_path == NULL || out_store == NULL ||
            dll_path[0] != L'\\' ||
            (database_path[0] != '\\' && strcmp(database_path, ":memory:") != 0))
        return APP_SETTINGS_INVALID;
    for (dll_length = 0; dll_length < APP_SETTINGS_PATH_MAX &&
            dll_path[dll_length] != L'\0'; ++dll_length) { }
    for (path_length = 0; path_length < APP_SETTINGS_PATH_MAX &&
            database_path[path_length] != '\0'; ++path_length) { }
    if (dll_length >= APP_SETTINGS_PATH_MAX || path_length >= APP_SETTINGS_PATH_MAX)
        return APP_SETTINGS_INVALID;
    store = (AppSettingsStore *) calloc(1, sizeof(*store));
    if (store == NULL) return APP_SETTINGS_PLATFORM_FAILED;
    store->owner_thread = GetCurrentThreadId();
    store->sequence = 1;
    store->outstanding = 1;
    memcpy(store->dll_path, dll_path, (dll_length + 1U) * sizeof(WCHAR));
    memcpy(store->database_path, database_path, path_length + 1U);
    InitializeCriticalSection(&store->lock);
    store->wake = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (store->wake == NULL) {
        DeleteCriticalSection(&store->lock);
        free(store);
        return APP_SETTINGS_PLATFORM_FAILED;
    }
    store->worker = CreateThread(NULL, 0, app_settings_worker, store, 0, NULL);
    if (store->worker == NULL) {
        CloseHandle(store->wake);
        DeleteCriticalSection(&store->lock);
        free(store);
        return APP_SETTINGS_PLATFORM_FAILED;
    }
    *out_store = store;
    return APP_SETTINGS_OK;
}

int AppSettingsStore_Submit(AppSettingsStore *store, int operation,
        AppSettingsStartPage page, unsigned long tab_id,
        unsigned long generation, unsigned long *out_request_id)
{
    AppSettingsJob *job;
    int tail;
    int result;

    if (!app_settings_owner(store) || out_request_id == NULL ||
            (operation != APP_SETTINGS_LOAD && operation != APP_SETTINGS_SAVE) ||
            AppSettingsStore_StartPageUrl(page) == NULL)
        return APP_SETTINGS_INVALID;
    EnterCriticalSection(&store->lock);
    result = APP_SETTINGS_OK;
    if (store->closing) result = APP_SETTINGS_CLOSING;
    else if (store->sequence == ~0UL) result = APP_SETTINGS_INVALID;
    else if (store->outstanding == APP_SETTINGS_QUEUE_MAX)
        result = APP_SETTINGS_QUEUE_FULL;
    if (result == APP_SETTINGS_OK) {
        tail = (store->job_head + store->job_count) % APP_SETTINGS_QUEUE_MAX;
        job = &store->jobs[tail];
        job->request_id = ++store->sequence;
        job->tab_id = tab_id;
        job->generation = generation;
        job->operation = operation;
        job->start_page = page;
        store->job_count++;
        store->outstanding++;
        *out_request_id = job->request_id;
        SetEvent(store->wake);
    }
    LeaveCriticalSection(&store->lock);
    return result;
}

int AppSettingsStore_Poll(AppSettingsStore *store, AppSettingsResult *out)
{
    int result;

    if (!app_settings_owner(store) || out == NULL) return APP_SETTINGS_INVALID;
    EnterCriticalSection(&store->lock);
    result = APP_SETTINGS_PENDING;
    if (store->result_count != 0) {
        *out = store->results[store->result_head];
        store->result_head = (store->result_head + 1) % APP_SETTINGS_QUEUE_MAX;
        store->result_count--;
        store->outstanding--;
        result = APP_SETTINGS_OK;
    }
    LeaveCriticalSection(&store->lock);
    return result;
}

int AppSettingsStore_RequestClose(AppSettingsStore *store)
{
    if (!app_settings_owner(store)) return APP_SETTINGS_INVALID;
    EnterCriticalSection(&store->lock);
    store->closing = 1;
    SetEvent(store->wake);
    LeaveCriticalSection(&store->lock);
    return APP_SETTINGS_OK;
}

int AppSettingsStore_TryDestroy(AppSettingsStore **store_pointer)
{
    AppSettingsStore *store;
    DWORD wait;

    if (store_pointer == NULL || !app_settings_owner(*store_pointer))
        return APP_SETTINGS_INVALID;
    store = *store_pointer;
    if (!store->closing) return APP_SETTINGS_INVALID;
    wait = WaitForSingleObject(store->worker, 0);
    if (wait == WAIT_TIMEOUT) return APP_SETTINGS_PENDING;
    if (wait != WAIT_OBJECT_0) return APP_SETTINGS_PLATFORM_FAILED;
    CloseHandle(store->worker);
    CloseHandle(store->wake);
    DeleteCriticalSection(&store->lock);
    free(store);
    *store_pointer = NULL;
    return APP_SETTINGS_OK;
}
