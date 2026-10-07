#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "app_settings_store.h"
#include "app_debug.h"
#include "app_url_router.h"

/* No import library dependency until the DB production device gates pass. */
typedef struct AppSettingsDbApi {
    int (*open)(const char *, int, PDbHandle *, PDbErrorInfo *);
    void (*close)(PDbHandle);
    int (*prepare)(PDbHandle, const char *, PDbStmtHandle *);
    int (*bind_text)(PDbStmtHandle, int, const char *, int);
    int (*bind_int)(PDbStmtHandle, int, __int64);
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
    int full_values;
    AppSettingsValues values;
    __int64 before_id;
    __int64 visited_utc;
    char url[APP_VISITS_URL_MAX];
    char title[APP_VISITS_TITLE_MAX];
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
    /* Worker scratch stays on the heap alongside the bounded queue. */
    AppSettingsJob active_job;
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

/* Keep the v1 row for the existing fixed-page UI adapter. Full preferences
 * have one owner and one versioned table; migration copies the old choice. */
static const char g_preferences_schema[] =
    "CREATE TABLE app_preferences (id INTEGER PRIMARY KEY CHECK(id=1),"
    "startup_url TEXT NOT NULL CHECK(length(startup_url)>0 AND length(startup_url)<2048),"
    "language INTEGER NOT NULL CHECK(language BETWEEN 0 AND 2),"
    "javascript_enabled INTEGER NOT NULL CHECK(javascript_enabled IN (0,1)));"
    "INSERT INTO app_preferences SELECT id,startup_page,0,1 FROM app_settings;";
static const char g_preferences_exists_sql[] =
    "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='app_preferences'";
static const char g_preferences_read_sql[] =
    "SELECT startup_url,language,javascript_enabled FROM app_preferences WHERE id=1";
static const char g_preferences_write_sql[] =
    "UPDATE app_preferences SET startup_url=?1,language=?2,javascript_enabled=?3 WHERE id=1";
static const char g_preferences_legacy_write_sql[] =
    "UPDATE app_preferences SET startup_url=?1 WHERE id=1";

static const char g_visits_exists_sql[] =
    "SELECT count(*) FROM sqlite_master WHERE type='table' "
    "AND name='app_visits'";
static const char g_visits_schema[] =
    "CREATE TABLE app_visits (id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "url TEXT,title TEXT,visited_utc INTEGER);";
static const char g_visits_add_sql[] =
    "INSERT INTO app_visits(url,title,visited_utc) VALUES(?1,?2,?3)";
static const char g_visits_prune_sql[] =
    "DELETE FROM app_visits WHERE id IN "
    "(SELECT id FROM app_visits ORDER BY id DESC LIMIT -1 OFFSET 500)";
static const char g_visits_read_sql[] =
    "SELECT id,visited_utc,url,title FROM app_visits "
    "WHERE (?1=0 OR id<?1) ORDER BY id DESC LIMIT 17";
static const char g_visits_clear_sql[] = "DELETE FROM app_visits";

void AppSettingsResult_Release(AppSettingsResult *result)
{
    if (result == NULL) return;
    free(result->visits);
    result->visits = NULL;
}

/* Reject embedded NULs, overlong encodings, surrogates and non-Unicode
 * scalar values. bytes excludes the terminator and is already bounded. */
static int app_visits_utf8(const char *text, int bytes)
{
    int index;
    int more;
    unsigned int code;
    unsigned int minimum;
    unsigned char ch;

    index = 0;
    while (index < bytes) {
        ch = (unsigned char) text[index++];
        if (ch == 0) return 0;
        if (ch < 0x80) continue;
        if (ch >= 0xc2 && ch <= 0xdf) {
            more = 1; code = ch & 0x1f; minimum = 0x80;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            more = 2; code = ch & 0x0f; minimum = 0x800;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            more = 3; code = ch & 7; minimum = 0x10000;
        } else return 0;
        if (more > bytes - index) return 0;
        while (more-- != 0) {
            ch = (unsigned char) text[index++];
            if ((ch & 0xc0) != 0x80) return 0;
            code = (code << 6) | (ch & 0x3f);
        }
        if (code < minimum || code > 0x10ffff ||
                (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}

static int app_visits_input(const char *text, int capacity, int nonempty)
{
    int bytes;

    if (text == NULL) return 0;
    for (bytes = 0; bytes < capacity && text[bytes] != '\0'; ++bytes) { }
    return bytes < capacity && (!nonempty || bytes != 0) &&
            app_visits_utf8(text, bytes);
}

const char *AppSettingsStore_StartPageUrl(AppSettingsStartPage page)
{
    switch (page) {
    case APP_SETTINGS_START_NEWTAB: return "positron://newtab";
    case APP_SETTINGS_START_WELCOME: return "positron://welcome";
    case APP_SETTINGS_START_CONTROLS: return "positron://controls";
    default: return NULL;
    }
}

void AppSettingsValues_Default(AppSettingsValues *values)
{
    if (values == NULL) return;
    memset(values, 0, sizeof(*values));
    strcpy(values->startup_url, "positron://newtab");
    values->language = APP_SETTINGS_LANGUAGE_SYSTEM;
    values->javascript_enabled = 1;
}

int AppSettingsValues_Normalize(const AppSettingsValues *input,
        AppSettingsValues *output)
{
    AppSettingsValues normalized;
    AppUrlSchemeKind scheme;
    const char *url;
    int page;
    int valid;

    if (input == NULL || output == NULL ||
            input->language < APP_SETTINGS_LANGUAGE_SYSTEM ||
            input->language > APP_SETTINGS_LANGUAGE_CHINESE ||
            (input->javascript_enabled != 0 && input->javascript_enabled != 1) ||
            !app_visits_input(input->startup_url, APP_VISITS_URL_MAX, 1))
        return APP_SETTINGS_INVALID;
    normalized = *input;
    valid = 0;
    for (page = APP_SETTINGS_START_NEWTAB; page <= APP_SETTINGS_START_CONTROLS;
            ++page) {
        url = AppSettingsStore_StartPageUrl((AppSettingsStartPage) page);
        if (!strcmp(input->startup_url, url)) valid = 1;
    }
    scheme = AppUrlRouter_ClassifyScheme(input->startup_url);
    if (!valid && (scheme == APP_URL_SCHEME_HTTP || scheme == APP_URL_SCHEME_HTTPS))
        valid = AppUrlRouter_ResolveNetworkReference(NULL, input->startup_url,
                normalized.startup_url, sizeof(normalized.startup_url)) == 0;
    if (!valid) return APP_SETTINGS_INVALID;
    *output = normalized;
    return APP_SETTINGS_OK;
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
    AppSettingsValues_Default(&result->values);
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
    api->bind_int = (int (*)(PDbStmtHandle, int, __int64))
            GetProcAddress(module, TEXT("PDb_BindInt64"));
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
            api->prepare != NULL && api->bind_text != NULL && api->bind_int != NULL &&
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

static void app_settings_read_legacy(AppSettingsDbApi *api, PDbHandle db,
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

static void app_settings_read(AppSettingsDbApi *api, PDbHandle db,
        AppSettingsResult *result)
{
    PDbStmtHandle statement;
    AppSettingsValues values;
    AppSettingsValues normalized;
    const char *text;
    __int64 language;
    __int64 javascript;
    int bytes;
    int rc;

    app_settings_read_legacy(api, db, result);
    if (result->result != APP_SETTINGS_OK) return;
    statement = NULL;
    rc = api->prepare(db, g_preferences_read_sql, &statement);
    if (rc == PDB_OK) rc = api->step(statement);
    if (rc != PDB_STEP_ROW) {
        if (rc == PDB_STEP_DONE) result->result = APP_SETTINGS_BAD_DATA;
        else app_settings_db_failure(api, db, rc, result);
        app_settings_finish_statement(api, db, statement, result);
        return;
    }
    AppSettingsValues_Default(&values);
    text = api->column_text(statement, 0);
    bytes = api->column_bytes(statement, 0);
    language = api->column_int(statement, 1);
    javascript = api->column_int(statement, 2);
    if (api->column_type(statement, 0) != PDB_VALUE_TEXT || text == NULL ||
            bytes <= 0 || bytes >= APP_VISITS_URL_MAX ||
            memchr(text, '\0', (size_t) bytes) != NULL ||
            api->column_type(statement, 1) != PDB_VALUE_INTEGER ||
            language < 0 || language > 2 ||
            api->column_type(statement, 2) != PDB_VALUE_INTEGER ||
            (javascript != 0 && javascript != 1)) {
        result->result = APP_SETTINGS_BAD_DATA;
    } else {
        memcpy(values.startup_url, text, (size_t) bytes);
        values.startup_url[bytes] = '\0';
        values.language = (AppSettingsLanguage) language;
        values.javascript_enabled = (int) javascript;
        if (AppSettingsValues_Normalize(&values, &normalized) != APP_SETTINGS_OK ||
                strcmp(values.startup_url, normalized.startup_url))
            result->result = APP_SETTINGS_BAD_DATA;
        else result->values = values;
    }
    if (result->result == APP_SETTINGS_OK) {
        rc = api->step(statement);
        if (rc == PDB_STEP_ROW) result->result = APP_SETTINGS_BAD_DATA;
        else if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
    }
    app_settings_finish_statement(api, db, statement, result);
}

static int app_settings_table_exists(AppSettingsDbApi *api, PDbHandle db,
        const char *sql, AppSettingsResult *result)
{
    PDbStmtHandle statement;
    int exists;
    int rc;

    statement = NULL;
    exists = 0;
    rc = api->prepare(db, sql, &statement);
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
    return exists;
}

static void app_settings_initialize(AppSettingsDbApi *api, PDbHandle db,
        AppSettingsResult *result)
{
    char script[sizeof(g_settings_schema) + sizeof(g_visits_schema) +
            sizeof(g_preferences_schema)];
    int exists;
    int visits_exist;
    int preferences_exist;
    int rc;

    exists = app_settings_table_exists(api, db, g_settings_exists_sql, result);
    if (result->result != APP_SETTINGS_OK) return;
    visits_exist = app_settings_table_exists(api, db, g_visits_exists_sql, result);
    if (result->result != APP_SETTINGS_OK) return;
    preferences_exist = app_settings_table_exists(api, db,
            g_preferences_exists_sql, result);
    if (result->result != APP_SETTINGS_OK) return;
    if ((visits_exist && !exists) || (preferences_exist && (!exists || !visits_exist))) {
        result->result = APP_SETTINGS_BAD_DATA;
        return;
    }
    /* One migration sequence, no private DB metadata access. Legacy row and
     * visits remain intact; v3 adds the atomic preference snapshot. */
    if (exists) {
        app_settings_read_legacy(api, db, result);
        if (result->result != APP_SETTINGS_OK) return;
    }
    script[0] = '\0';
    if (!exists) strcpy(script, g_settings_schema);
    if (!visits_exist) strcat(script, g_visits_schema);
    if (!preferences_exist) strcat(script, g_preferences_schema);
    rc = api->migration(db, 3, script);
    if (rc != PDB_OK) {
        app_settings_db_failure(api, db, rc, result);
        return;
    }
    app_settings_read(api, db, result);
}

static int app_visits_column_text(AppSettingsDbApi *api,
        PDbStmtHandle statement, int column, char *out, int capacity,
        int nonempty)
{
    const char *text;
    int bytes;

    if (api->column_type(statement, column) != PDB_VALUE_TEXT) return 0;
    text = api->column_text(statement, column);
    bytes = api->column_bytes(statement, column);
    if (text == NULL || bytes < 0 || bytes >= capacity ||
            (nonempty && bytes == 0) || !app_visits_utf8(text, bytes)) return 0;
    if (out != NULL) {
        memcpy(out, text, (size_t) bytes);
        out[bytes] = '\0';
    }
    return 1;
}

static void app_visits_read(AppSettingsDbApi *api, PDbHandle db,
        const AppSettingsJob *job, AppSettingsResult *result)
{
    PDbStmtHandle statement;
    AppVisitSnapshot *snapshot;
    AppVisitRecord *entry;
    __int64 id;
    __int64 previous;
    int rc;

    snapshot = (AppVisitSnapshot *) calloc(1, sizeof(*snapshot));
    if (snapshot == NULL) {
        result->result = APP_SETTINGS_PLATFORM_FAILED;
        return;
    }
    statement = NULL;
    previous = job->before_id;
    rc = api->prepare(db, g_visits_read_sql, &statement);
    if (rc == PDB_OK) rc = api->bind_int(statement, 1, job->before_id);
    if (rc == PDB_OK) rc = api->step(statement);
    while (rc == PDB_STEP_ROW) {
        id = api->column_int(statement, 0);
        entry = snapshot->count < APP_VISITS_PAGE_MAX ?
                &snapshot->entries[snapshot->count] : NULL;
        if (api->column_type(statement, 0) != PDB_VALUE_INTEGER || id <= 0 ||
                (previous != 0 && id >= previous) ||
                api->column_type(statement, 1) != PDB_VALUE_INTEGER ||
                api->column_int(statement, 1) < 0 ||
                !app_visits_column_text(api, statement, 2,
                    entry != NULL ? entry->url : NULL, APP_VISITS_URL_MAX, 1) ||
                !app_visits_column_text(api, statement, 3,
                    entry != NULL ? entry->title : NULL, APP_VISITS_TITLE_MAX, 0)) {
            result->result = APP_SETTINGS_BAD_DATA;
            break;
        }
        previous = id;
        if (entry == NULL) snapshot->has_more = 1;
        else {
            entry->id = id;
            entry->visited_utc = api->column_int(statement, 1);
            snapshot->count++;
        }
        rc = api->step(statement);
    }
    if (result->result == APP_SETTINGS_OK && rc != PDB_STEP_DONE)
        app_settings_db_failure(api, db, rc, result);
    app_settings_finish_statement(api, db, statement, result);
    if (result->result == APP_SETTINGS_OK) result->visits = snapshot;
    else free(snapshot);
}

static void app_visits_execute(AppSettingsDbApi *api, PDbHandle db,
        const char *sql, AppSettingsResult *result)
{
    PDbStmtHandle statement;
    int rc;

    statement = NULL;
    rc = api->prepare(db, sql, &statement);
    if (rc == PDB_OK) rc = api->step(statement);
    if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
    app_settings_finish_statement(api, db, statement, result);
}

static void app_visits_write(AppSettingsDbApi *api, PDbHandle db,
        const AppSettingsJob *job, AppSettingsResult *result)
{
    PDbStmtHandle statement;
    int rc;

    statement = NULL;
    rc = api->begin(db);
    if (rc != PDB_OK) {
        app_settings_db_failure(api, db, rc, result);
        return;
    }
    if (job->operation == APP_VISITS_ADD) {
        rc = api->prepare(db, g_visits_add_sql, &statement);
        if (rc == PDB_OK)
            rc = api->bind_text(statement, 1, job->url, (int) strlen(job->url));
        if (rc == PDB_OK)
            rc = api->bind_text(statement, 2, job->title, (int) strlen(job->title));
        if (rc == PDB_OK) rc = api->bind_int(statement, 3, job->visited_utc);
        if (rc == PDB_OK) rc = api->step(statement);
        if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
        app_settings_finish_statement(api, db, statement, result);
        if (result->result == APP_SETTINGS_OK)
            app_visits_execute(api, db, g_visits_prune_sql, result);
    } else app_visits_execute(api, db, g_visits_clear_sql, result);
    if (result->result == APP_SETTINGS_OK) {
        rc = api->commit(db);
        if (rc != PDB_OK) app_settings_db_failure(api, db, rc, result);
    }
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
    url = job->full_values ? job->values.startup_url :
            AppSettingsStore_StartPageUrl(job->start_page);
    rc = api->begin(db);
    if (!job->full_values) {
        if (rc == PDB_OK) rc = api->prepare(db, g_settings_write_sql, &statement);
        if (rc == PDB_OK) rc = api->bind_text(statement, 1, url, (int) strlen(url));
        if (rc == PDB_OK) rc = api->step(statement);
        if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
        app_settings_finish_statement(api, db, statement, result);
        statement = NULL;
    } else if (rc != PDB_OK) app_settings_db_failure(api, db, rc, result);
    if (result->result == APP_SETTINGS_OK) {
        rc = api->prepare(db, job->full_values ? g_preferences_write_sql :
                g_preferences_legacy_write_sql, &statement);
        if (rc == PDB_OK) rc = api->bind_text(statement, 1, url, (int) strlen(url));
        if (job->full_values && rc == PDB_OK)
            rc = api->bind_int(statement, 2, job->values.language);
        if (job->full_values && rc == PDB_OK)
            rc = api->bind_int(statement, 3, job->values.javascript_enabled);
        if (rc == PDB_OK) rc = api->step(statement);
        if (rc != PDB_STEP_DONE) app_settings_db_failure(api, db, rc, result);
        app_settings_finish_statement(api, db, statement, result);
    }
    if (result->result == APP_SETTINGS_OK) {
        /* Detect a missing/malformed row or a no-op UPDATE before COMMIT. */
        app_settings_read(api, db, result);
        if (result->result == APP_SETTINGS_OK &&
                (strcmp(result->values.startup_url, url) ||
                (job->full_values &&
                (result->values.language != job->values.language ||
                 result->values.javascript_enabled != job->values.javascript_enabled)) ||
                (!job->full_values && result->start_page != job->start_page)))
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
    AppSettingsJob *job;
    AppSettingsResult result;
    AppSettingsResult failure;
    int healthy;
    int stop;
    int found;
    int rc;

    store = (AppSettingsStore *) argument;
    job = &store->active_job;
    memset(&api, 0, sizeof(api));
    memset(job, 0, sizeof(*job));
    job->request_id = 1;
    job->operation = APP_SETTINGS_LOAD;
    app_settings_result_init(&result, job);
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
            *job = store->jobs[store->job_head];
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
        app_settings_result_init(&result, job);
        if (!healthy) {
            result.result = failure.result;
            result.db_result = failure.db_result;
            result.error = failure.error;
            result.state = failure.state;
            result.state_valid = failure.state_valid;
            result.rollback_result = failure.rollback_result;
        } else {
            if (job->operation == APP_SETTINGS_SAVE)
                app_settings_save(&api, db, job, &result);
            else if (job->operation == APP_VISITS_ADD || job->operation == APP_VISITS_CLEAR)
                app_visits_write(&api, db, job, &result);
            else if (job->operation == APP_VISITS_LOAD)
                app_visits_read(&api, db, job, &result);
            else app_settings_read(&api, db, &result);
            app_settings_cleanup_transaction(&api, db, &result);
            if (result.result != APP_SETTINGS_OK) {
                AppSettingsResult_Release(&result);
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

static int app_settings_enqueue(AppSettingsStore *store, int operation,
        AppSettingsStartPage page, unsigned long tab_id,
        unsigned long generation, const char *url, const char *title,
        __int64 visited_utc, __int64 before_id, const AppSettingsValues *values,
        unsigned long *out_request_id)
{
    AppSettingsJob *job;
    int tail;
    int result;

    if (!app_settings_owner(store) || out_request_id == NULL)
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
        memset(job, 0, sizeof(*job));
        job->request_id = ++store->sequence;
        job->tab_id = tab_id;
        job->generation = generation;
        job->operation = operation;
        job->start_page = page;
        if (values != NULL) {
            job->full_values = 1;
            job->values = *values;
        }
        job->before_id = before_id;
        job->visited_utc = visited_utc;
        if (url != NULL) strcpy(job->url, url);
        if (title != NULL) strcpy(job->title, title);
        store->job_count++;
        store->outstanding++;
        *out_request_id = job->request_id;
        SetEvent(store->wake);
    }
    LeaveCriticalSection(&store->lock);
    return result;
}

int AppSettingsStore_Submit(AppSettingsStore *store, int operation,
        AppSettingsStartPage page, unsigned long tab_id,
        unsigned long generation, unsigned long *out_request_id)
{
    if ((operation != APP_SETTINGS_LOAD && operation != APP_SETTINGS_SAVE) ||
            AppSettingsStore_StartPageUrl(page) == NULL)
        return APP_SETTINGS_INVALID;
    return app_settings_enqueue(store, operation, page, tab_id, generation,
            NULL, NULL, 0, 0, NULL, out_request_id);
}

int AppSettingsStore_SaveValues(AppSettingsStore *store,
        const AppSettingsValues *values, unsigned long tab_id,
        unsigned long generation, unsigned long *out_request_id)
{
    AppSettingsValues normalized;

    if (AppSettingsValues_Normalize(values, &normalized) != APP_SETTINGS_OK)
        return APP_SETTINGS_INVALID;
    return app_settings_enqueue(store, APP_SETTINGS_SAVE,
            APP_SETTINGS_START_NEWTAB, tab_id, generation, NULL, NULL, 0, 0,
            &normalized, out_request_id);
}

int AppSettingsStore_AddVisit(AppSettingsStore *store, const char *url,
        const char *title, __int64 visited_utc, unsigned long *out_request_id)
{
    if (!app_settings_owner(store) || visited_utc < 0 ||
            !app_visits_input(url, APP_VISITS_URL_MAX, 1) ||
            !app_visits_input(title, APP_VISITS_TITLE_MAX, 0))
        return APP_SETTINGS_INVALID;
    return app_settings_enqueue(store, APP_VISITS_ADD, APP_SETTINGS_START_NEWTAB,
            0, 0, url, title, visited_utc, 0, NULL, out_request_id);
}

int AppSettingsStore_ReadVisits(AppSettingsStore *store, __int64 before_id,
        unsigned long tab_id, unsigned long generation,
        unsigned long *out_request_id)
{
    if (before_id < 0) return APP_SETTINGS_INVALID;
    return app_settings_enqueue(store, APP_VISITS_LOAD, APP_SETTINGS_START_NEWTAB,
            tab_id, generation, NULL, NULL, 0, before_id, NULL, out_request_id);
}

int AppSettingsStore_ClearVisits(AppSettingsStore *store, unsigned long tab_id,
        unsigned long generation, unsigned long *out_request_id)
{
    return app_settings_enqueue(store, APP_VISITS_CLEAR, APP_SETTINGS_START_NEWTAB,
            tab_id, generation, NULL, NULL, 0, 0, NULL, out_request_id);
}

int AppSettingsStore_Poll(AppSettingsStore *store, AppSettingsResult *out)
{
    int result;

    if (!app_settings_owner(store) || out == NULL) return APP_SETTINGS_INVALID;
    EnterCriticalSection(&store->lock);
    result = APP_SETTINGS_PENDING;
    if (store->result_count != 0) {
        *out = store->results[store->result_head];
        store->results[store->result_head].visits = NULL;
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
    int index;

    if (store_pointer == NULL || !app_settings_owner(*store_pointer))
        return APP_SETTINGS_INVALID;
    store = *store_pointer;
    if (!store->closing) return APP_SETTINGS_INVALID;
    wait = WaitForSingleObject(store->worker, 0);
    if (wait == WAIT_TIMEOUT) return APP_SETTINGS_PENDING;
    if (wait != WAIT_OBJECT_0) return APP_SETTINGS_PLATFORM_FAILED;
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index)
        AppSettingsResult_Release(&store->results[index]);
    CloseHandle(store->worker);
    CloseHandle(store->wake);
    DeleteCriticalSection(&store->lock);
    free(store);
    *store_pointer = NULL;
    return APP_SETTINGS_OK;
}
