/* EXE-private asynchronous settings and persistent visit storage. */
#ifndef POSITRON_APP_SETTINGS_STORE_H
#define POSITRON_APP_SETTINGS_STORE_H

#include <windows.h>
#include "../positron_db/positron_db.h"

#define APP_SETTINGS_QUEUE_MAX 8
#define APP_SETTINGS_PATH_MAX 1024
#define APP_VISITS_PAGE_MAX 16
#define APP_VISITS_URL_MAX 2048
#define APP_VISITS_TITLE_MAX 256

typedef struct AppVisitRecord {
    __int64 id;
    __int64 visited_utc;
    char url[APP_VISITS_URL_MAX];
    char title[APP_VISITS_TITLE_MAX];
} AppVisitRecord;

typedef struct AppVisitSnapshot {
    int count;
    int has_more;
    AppVisitRecord entries[APP_VISITS_PAGE_MAX];
} AppVisitSnapshot;

typedef struct AppSettingsStore AppSettingsStore;

typedef enum AppSettingsStartPage {
    APP_SETTINGS_START_NEWTAB = 0,
    APP_SETTINGS_START_WELCOME,
    APP_SETTINGS_START_CONTROLS
} AppSettingsStartPage;

enum {
    APP_SETTINGS_OK = 0,
    APP_SETTINGS_PENDING = 1,
    APP_SETTINGS_QUEUE_FULL = 2,
    APP_SETTINGS_INVALID = -1,
    APP_SETTINGS_DB_FAILED = -2,
    APP_SETTINGS_BAD_DATA = -3,
    APP_SETTINGS_CLOSING = -4,
    APP_SETTINGS_PLATFORM_FAILED = -5,
    APP_SETTINGS_DLL_UNAVAILABLE = -6
};

enum {
    APP_SETTINGS_LOAD = 1,
    APP_SETTINGS_SAVE = 2,
    APP_VISITS_ADD = 3,
    APP_VISITS_LOAD = 4,
    APP_VISITS_CLEAR = 5
};

/* Pure-data result, copied to the UI owner. No DB/statement/page handles.
 * Startup is request 1 (LOAD, tab/generation=0); later IDs never wrap/reuse.
 * A SAVE is successful only after COMMIT. Failed values must not update UI.
 * tab/generation are echoed routing metadata, not authorization; an accepted
 * SAVE still drains if its originating page closes. The UI must reject stale
 * recipients itself. error is the root DB failure, state is AFTER cleanup;
 * rollback_result is separate and never overwrites the original error. */
typedef struct AppSettingsResult {
    unsigned long request_id;
    unsigned long tab_id;
    unsigned long generation;
    int operation;
    int result;
    int db_result;
    int rollback_result;
    int state_valid;
    PDbConnectionState state;
    PDbErrorInfo error;
    AppSettingsStartPage start_page;
    AppVisitSnapshot *visits;
} AppSettingsResult;

const char *AppSettingsStore_StartPageUrl(AppSettingsStartPage page);

/* All entry points belong to the creating/UI thread. The worker alone loads
 * the explicitly named absolute DLL, opens/migrates/queries/closes the DB and
 * unloads the DLL. Paths are copied; no directory creation or fallback.
 * Database path is absolute UTF-8, or :memory: for isolated Debug fixtures.
 * The production EXE resolves positron.db beside its own executable. */
int AppSettingsStore_Create(const WCHAR *dll_path, const char *database_path,
        AppSettingsStore **out_store);
/* Both job and completion storage count toward the eight-request budget.
 * Queue full/invalid/closing leaves out_request_id unchanged. */
int AppSettingsStore_Submit(AppSettingsStore *store, int operation,
        AppSettingsStartPage page, unsigned long tab_id,
        unsigned long generation, unsigned long *out_request_id);
/* Copied UTF-8 inputs: URL nonempty, title may be empty; both must fit with
 * their terminator. No URL interpretation or title truncation. UTC is Unix
 * seconds >= 0. Insert and oldest-ID pruning to 500 rows commit together. */
int AppSettingsStore_AddVisit(AppSettingsStore *store, const char *url,
        const char *title, __int64 visited_utc, unsigned long *out_request_id);
/* Latest first, or IDs strictly below a positive cursor; has_more reports
 * a seventeenth row. tab/generation are echoed only, as with Submit. */
int AppSettingsStore_ReadVisits(AppSettingsStore *store, __int64 before_id,
        unsigned long tab_id, unsigned long generation,
        unsigned long *out_request_id);
int AppSettingsStore_ClearVisits(AppSettingsStore *store, unsigned long tab_id,
        unsigned long generation, unsigned long *out_request_id);
/* Poll transfers the heap snapshot (successful visits LOAD only). Release
 * every consumed/discarded result before reusing it. Idempotent on an
 * initialized result; do not shallow-copy an owned snapshot. No DLL calls. */
void AppSettingsResult_Release(AppSettingsResult *result);
/* One nonblocking FIFO result per poll. PENDING leaves output unchanged. */
int AppSettingsStore_Poll(AppSettingsStore *store, AppSettingsResult *out);
/* Stop admission and drain accepted jobs. No UI wait, TerminateThread or
 * cross-thread PDb_Cancel. Poll can continue during drain. TryDestroy returns
 * PENDING until the worker has closed the DB/DLL, then discards unpolled
 * results, frees their snapshots, releases the service and sets *store=NULL. */
int AppSettingsStore_RequestClose(AppSettingsStore *store);
int AppSettingsStore_TryDestroy(AppSettingsStore **store);

#ifdef _DEBUG
int AppSettingsStore_DebugCheck(void);
int AppVisitStore_DebugLiveCheck(int mode);
#endif
#endif
