/* Explicit, opt-in EXE fixture. Entire implementation excluded from Release. */
#include "app_settings_store.h"
#include "app_debug.h"

#ifdef _DEBUG
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int app_settings_debug_next(AppSettingsStore *store,
        AppSettingsResult *result)
{
    DWORD started;
    int rc;

    started = GetTickCount();
    do {
        rc = AppSettingsStore_Poll(store, result);
        if (rc != APP_SETTINGS_PENDING) return rc;
        Sleep(5);
    } while (GetTickCount() - started < 30000UL);
    return APP_SETTINGS_PENDING;
}

static int app_settings_debug_close(AppSettingsStore **store)
{
    DWORD started;
    int rc;

    if (*store == NULL) return APP_SETTINGS_OK;
    if (AppSettingsStore_RequestClose(*store) != APP_SETTINGS_OK) return 1;
    started = GetTickCount();
    do {
        rc = AppSettingsStore_TryDestroy(store);
        if (rc != APP_SETTINGS_PENDING) return rc;
        Sleep(5);
    } while (GetTickCount() - started < 30000UL);
    return APP_SETTINGS_PENDING;
}

static int app_settings_debug_ready(AppSettingsStore *store,
        AppSettingsStartPage page)
{
    AppSettingsResult result;

    return app_settings_debug_next(store, &result) == APP_SETTINGS_OK &&
            result.request_id == 1 && result.operation == APP_SETTINGS_LOAD &&
            result.result == APP_SETTINGS_OK && result.start_page == page &&
            result.state_valid && !result.state.transaction_active &&
            result.state.statement_count == 0;
}

typedef struct AppSettingsDebugThread {
    AppSettingsStore *store;
    int passed;
} AppSettingsDebugThread;

static DWORD WINAPI app_settings_debug_foreign(LPVOID argument)
{
    AppSettingsDebugThread *probe;
    AppSettingsResult result;
    AppSettingsResult before;
    unsigned long request;

    probe = (AppSettingsDebugThread *) argument;
    memset(&result, 0x5a, sizeof(result));
    result.visits = NULL;
    before = result;
    request = 777;
    probe->passed = AppSettingsStore_Poll(probe->store, &result) ==
            APP_SETTINGS_INVALID && memcmp(&result, &before, sizeof(result)) == 0 &&
            AppSettingsStore_Submit(probe->store, APP_SETTINGS_SAVE,
            APP_SETTINGS_START_CONTROLS, 7, 9, &request) == APP_SETTINGS_INVALID &&
            request == 777 && AppSettingsStore_RequestClose(probe->store) ==
            APP_SETTINGS_INVALID;
    return 0;
}

/* Three separate EXE processes share only this dedicated fixture file.
 * Never use or remove the production positron.db, even in a gate package. */
int AppVisitStore_DebugLiveCheck(int mode)
{
    WCHAR dll[APP_SETTINGS_PATH_MAX];
    WCHAR path[APP_SETTINGS_PATH_MAX];
    WCHAR *slash;
    char database[APP_SETTINGS_PATH_MAX];
    char log[192];
    AppSettingsStore *store;
    AppSettingsResult result;
    unsigned long request;
    DWORD length;
    int i;
    int passed;
    int cleanup;

    store = NULL;
    passed = 0;
    memset(&result, 0, sizeof(result));
    length = GetModuleFileNameW(NULL, dll, APP_SETTINGS_PATH_MAX);
    if (!length || length >= APP_SETTINGS_PATH_MAX || mode < 1 || mode > 3)
        goto done;
    if (wcsncmp(dll, L"\\Storage Card\\Temp\\Positron-device-gate\\app-settings-",
            wcslen(L"\\Storage Card\\Temp\\Positron-device-gate\\app-settings-")))
        goto done;
    slash = wcsrchr(dll, L'\\');
    if (slash == NULL || slash - dll > APP_SETTINGS_PATH_MAX - 32) goto done;
    wcscpy(slash + 1, L"visits-fixture.db");
    wcscpy(path, dll);
    wcscpy(slash + 1, L"positron_db.dll");
    if ((GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) != (mode == 1))
        goto done;
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, database,
            sizeof(database), NULL, NULL) ||
            AppSettingsStore_Create(dll, database, &store) != APP_SETTINGS_OK ||
            !app_settings_debug_ready(store, mode == 1 ?
            APP_SETTINGS_START_NEWTAB : APP_SETTINGS_START_WELCOME)) goto done;
    if (mode == 1) {
        if (AppSettingsStore_Submit(store, APP_SETTINGS_SAVE,
                APP_SETTINGS_START_WELCOME, 1, 1, &request) != APP_SETTINGS_OK ||
                app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
                result.result != APP_SETTINGS_OK) goto done;
        for (i = 0; i < 18; ++i) {
            if (AppSettingsStore_AddVisit(store, "https://example.com/?saved=1",
                    "Saved visit", 1700000000 + i, &request) != APP_SETTINGS_OK ||
                    app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
                    result.result != APP_SETTINGS_OK) goto done;
        }
    } else {
        if (AppSettingsStore_ReadVisits(store, 0, 1, 1, &request) !=
                APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
                APP_SETTINGS_OK || result.result != APP_SETTINGS_OK ||
                result.visits == NULL || result.visits->count != 16 ||
                !result.visits->has_more || result.visits->entries[0].visited_utc !=
                1700000017) goto done;
        AppSettingsResult_Release(&result);
        if (mode == 3) {
            if (AppSettingsStore_ClearVisits(store, 1, 2, &request) !=
                    APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
                    APP_SETTINGS_OK || result.result != APP_SETTINGS_OK ||
                    app_settings_debug_close(&store) != APP_SETTINGS_OK ||
                    AppSettingsStore_Create(dll, database, &store) != APP_SETTINGS_OK ||
                    !app_settings_debug_ready(store, APP_SETTINGS_START_WELCOME) ||
                    AppSettingsStore_ReadVisits(store, 0, 1, 3, &request) !=
                    APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
                    APP_SETTINGS_OK || result.result != APP_SETTINGS_OK ||
                    result.visits == NULL || result.visits->count != 0) goto done;
        }
    }
    passed = 1;
done:
    AppSettingsResult_Release(&result);
    cleanup = app_settings_debug_close(&store);
    if (cleanup != APP_SETTINGS_OK) passed = 0;
    _snprintf(log, sizeof(log) - 1,
            "positron visits-live selftest %s mode=%d cleanup=%d production_enabled=0\r\n",
            passed ? "OK" : "FAILED", mode, cleanup);
    log[sizeof(log) - 1] = '\0';
    AppDebug_Log(log);
    return passed ? 0 : 1;
}

int AppSettingsStore_DebugCheck(void)
{
    WCHAR module_path[APP_SETTINGS_PATH_MAX];
    WCHAR directory[APP_SETTINGS_PATH_MAX];
    WCHAR file_path[APP_SETTINGS_PATH_MAX];
    WCHAR journal_path[APP_SETTINGS_PATH_MAX];
    char database_path[APP_SETTINGS_PATH_MAX];
    char missing_path[APP_SETTINGS_PATH_MAX];
    AppSettingsStore *store;
    AppSettingsResult result;
    AppSettingsResult before;
    AppSettingsDebugThread *probe;
    HANDLE foreign;
    DWORD length;
    WCHAR *separator;
    unsigned long request;
    unsigned long requests[APP_SETTINGS_QUEUE_MAX];
    char log[256];
    int index;
    int phase;
    int passed;
    int owns_directory;
    int cleanup;

    store = NULL;
    memset(&result, 0, sizeof(result));
    foreign = NULL;
    probe = NULL;
    directory[0] = L'\0';
    file_path[0] = L'\0';
    journal_path[0] = L'\0';
    owns_directory = 0;
    passed = 0;
    phase = 1;
    length = GetModuleFileNameW(NULL, module_path, APP_SETTINGS_PATH_MAX);
    if (length == 0 || length >= APP_SETTINGS_PATH_MAX) goto done;
    separator = wcsrchr(module_path, L'\\');
    if (separator == NULL || separator - module_path >
            APP_SETTINGS_PATH_MAX - 32) goto done;
    wcscpy(separator + 1, L"positron_db.dll");
    /* Exclusive fresh directory: never reuse or clean another run's files. */
    _snwprintf(directory, APP_SETTINGS_PATH_MAX - 1,
            L"\\Temp\\Positron-settings-%lu-%lu",
            (unsigned long) GetCurrentProcessId(),
            (unsigned long) GetTickCount());
    directory[APP_SETTINGS_PATH_MAX - 1] = L'\0';
    if (!CreateDirectoryW(directory, NULL)) goto done;
    owns_directory = 1;
    _snwprintf(file_path, APP_SETTINGS_PATH_MAX - 1,
            L"%s\\\x8bbe\x7f6e.db", directory);
    file_path[APP_SETTINGS_PATH_MAX - 1] = L'\0';
    _snwprintf(journal_path, APP_SETTINGS_PATH_MAX - 1,
            L"%s-journal", file_path);
    journal_path[APP_SETTINGS_PATH_MAX - 1] = L'\0';
    if (WideCharToMultiByte(CP_UTF8, 0, file_path, -1, database_path,
            sizeof(database_path), NULL, NULL) == 0) goto done;
    if (AppSettingsStore_StartPageUrl((AppSettingsStartPage) -1) != NULL ||
            AppSettingsStore_Create(L"positron_db.dll", database_path, &store) !=
            APP_SETTINGS_INVALID || store != NULL) goto done;
    if (AppSettingsStore_Create(module_path, database_path, &store) !=
            APP_SETTINGS_OK || !app_settings_debug_ready(store,
            APP_SETTINGS_START_NEWTAB)) goto done;
    phase = 2;
    probe = (AppSettingsDebugThread *) calloc(1, sizeof(*probe));
    if (probe == NULL) goto done;
    probe->store = store;
    foreign = CreateThread(NULL, 0, app_settings_debug_foreign, probe, 0, NULL);
    if (foreign == NULL || WaitForSingleObject(foreign, 30000) !=
            WAIT_OBJECT_0 || !probe->passed) goto done;
    CloseHandle(foreign);
    foreign = NULL;
    memset(&result, 0x5a, sizeof(result));
    result.visits = NULL;
    before = result;
    if (AppSettingsStore_Poll(store, &result) != APP_SETTINGS_PENDING ||
            memcmp(&before, &result, sizeof(result)) != 0) goto done;
    request = 777;
    if (AppSettingsStore_Submit(store, APP_SETTINGS_SAVE,
            (AppSettingsStartPage) 3, 1, 2, &request) != APP_SETTINGS_INVALID ||
            request != 777) goto done;
    phase = 3;
    /* Unread completions continue to consume slots even if the worker runs
     * before this thread submits the next job: no scheduling assumption. */
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        if (AppSettingsStore_Submit(store, APP_SETTINGS_SAVE,
                (AppSettingsStartPage) (index % 3), 7, 9,
                &requests[index]) != APP_SETTINGS_OK ||
                requests[index] != (unsigned long) index + 2UL) goto done;
    }
    if (AppSettingsStore_Submit(store, APP_SETTINGS_LOAD,
            APP_SETTINGS_START_NEWTAB, 7, 9, &request) != APP_SETTINGS_QUEUE_FULL ||
            request != 777) goto done;
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        if (app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
                result.request_id != requests[index] || result.tab_id != 7 ||
                result.generation != 9 || result.operation != APP_SETTINGS_SAVE ||
                result.result != APP_SETTINGS_OK ||
                result.start_page != (AppSettingsStartPage) (index % 3) ||
                !result.state_valid || result.state.transaction_active ||
                result.state.statement_count != 0) goto done;
    }
    if (AppSettingsStore_Submit(store, APP_SETTINGS_LOAD,
            APP_SETTINGS_START_NEWTAB, 8, 10, &request) != APP_SETTINGS_OK ||
            app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
            result.request_id != request || result.tab_id != 8 ||
            result.generation != 10 || result.operation != APP_SETTINGS_LOAD ||
            result.result != APP_SETTINGS_OK ||
            result.start_page != APP_SETTINGS_START_WELCOME) goto done;
    phase = 4;
    for (index = 0; index < 17; ++index) {
        if (AppSettingsStore_AddVisit(store, "https://example.com/?x=<&",
                "\344\270\255\346\226\207 <&>", 1700000000 + index, &request) !=
                APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
                APP_SETTINGS_OK || result.result != APP_SETTINGS_OK ||
                result.operation != APP_VISITS_ADD) goto done;
    }
    if (app_settings_debug_close(&store) != APP_SETTINGS_OK ||
            AppSettingsStore_Create(module_path, database_path, &store) !=
            APP_SETTINGS_OK || !app_settings_debug_ready(store,
            APP_SETTINGS_START_WELCOME)) goto done;
    if (AppSettingsStore_ReadVisits(store, 0, 71, 90, &request) != APP_SETTINGS_OK ||
            app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
            result.result != APP_SETTINGS_OK || result.visits == NULL ||
            result.visits->count != 16 || !result.visits->has_more ||
            result.visits->entries[0].visited_utc != 1700000016 ||
            result.tab_id != 71 || result.generation != 90) goto done;
    requests[0] = (unsigned long) result.visits->entries[15].id;
    AppSettingsResult_Release(&result);
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_ReadVisits(store, requests[0], 71, 91, &request) !=
            APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
            APP_SETTINGS_OK || result.result != APP_SETTINGS_OK ||
            result.visits == NULL || result.visits->count != 1 ||
            result.visits->has_more || result.visits->entries[0].visited_utc !=
            1700000000) goto done;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_ClearVisits(store, 71, 92, &request) != APP_SETTINGS_OK ||
            app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
            result.result != APP_SETTINGS_OK ||
            AppSettingsStore_ReadVisits(store, 0, 71, 93, &request) !=
            APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
            APP_SETTINGS_OK || result.result != APP_SETTINGS_OK ||
            result.visits == NULL || result.visits->count != 0) goto done;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_Submit(store, APP_SETTINGS_LOAD,
            APP_SETTINGS_START_NEWTAB, 71, 94, &request) != APP_SETTINGS_OK ||
            app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
            result.result != APP_SETTINGS_OK ||
            result.start_page != APP_SETTINGS_START_WELCOME) goto done;
    AppDebug_Log("positron visits-storage selftest OK records=17 cursor=1 "
            "same_process_reopen=1 clear_preserves_settings=1\r\n");
    if (AppSettingsStore_Submit(store, APP_SETTINGS_SAVE,
            APP_SETTINGS_START_CONTROLS, 7, 10, &request) != APP_SETTINGS_OK ||
            AppSettingsStore_RequestClose(store) != APP_SETTINGS_OK) goto done;
    requests[0] = request;
    request = 777;
    if (AppSettingsStore_Submit(store, APP_SETTINGS_LOAD,
            APP_SETTINGS_START_NEWTAB, 7, 10, &request) != APP_SETTINGS_CLOSING ||
            request != 777 || app_settings_debug_next(store, &result) !=
            APP_SETTINGS_OK || result.request_id != requests[0] ||
            result.result != APP_SETTINGS_OK ||
            result.start_page != APP_SETTINGS_START_CONTROLS ||
            app_settings_debug_close(&store) != APP_SETTINGS_OK) goto done;
    /* New worker/handle, same process; do NOT label this a process restart. */
    if (AppSettingsStore_Create(module_path, database_path, &store) !=
            APP_SETTINGS_OK || !app_settings_debug_ready(store,
            APP_SETTINGS_START_CONTROLS) ||
            app_settings_debug_close(&store) != APP_SETTINGS_OK) goto done;
    phase = 5;
    _snprintf(missing_path, sizeof(missing_path) - 1,
            "%s\\missing\\settings.db", database_path);
    missing_path[sizeof(missing_path) - 1] = '\0';
    if (AppSettingsStore_Create(module_path, missing_path, &store) !=
            APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
            APP_SETTINGS_OK || result.result != APP_SETTINGS_DB_FAILED ||
            result.error.category != PDB_ERROR_CATEGORY_CANNOT_OPEN) goto done;
    if (AppSettingsStore_Submit(store, APP_SETTINGS_SAVE,
            APP_SETTINGS_START_CONTROLS, 7, 11, &request) != APP_SETTINGS_OK ||
            app_settings_debug_next(store, &result) != APP_SETTINGS_OK ||
            result.request_id != request || result.result != APP_SETTINGS_DB_FAILED ||
            result.error.category != PDB_ERROR_CATEGORY_CANNOT_OPEN ||
            app_settings_debug_close(&store) != APP_SETTINGS_OK) goto done;
    phase = 6;
    /* Missing DLL, not a second copy or a mismatched installed module. */
    wcscpy(separator + 1, L"positron_db-absent.dll");
    if (GetFileAttributesW(module_path) != INVALID_FILE_ATTRIBUTES) goto done;
    if (AppSettingsStore_Create(module_path, ":memory:", &store) !=
            APP_SETTINGS_OK || app_settings_debug_next(store, &result) !=
            APP_SETTINGS_OK || result.result != APP_SETTINGS_DLL_UNAVAILABLE ||
            app_settings_debug_close(&store) != APP_SETTINGS_OK) goto done;
    passed = 1;
done:
    AppSettingsResult_Release(&result);
    cleanup = APP_SETTINGS_OK;
    if (foreign != NULL) {
        /* The probe is heap-owned. On timeout retain it AND the store rather
         * than hang, destroy live state or let a worker touch a freed stack. */
        if (WaitForSingleObject(foreign, 0) != WAIT_OBJECT_0)
            cleanup = APP_SETTINGS_PLATFORM_FAILED;
        CloseHandle(foreign);
    }
    if (cleanup == APP_SETTINGS_OK && probe != NULL) free(probe);
    if (cleanup == APP_SETTINGS_OK) cleanup = app_settings_debug_close(&store);
    if (cleanup == APP_SETTINGS_OK && owns_directory) {
        if (file_path[0] != L'\0' &&
                GetFileAttributesW(file_path) != INVALID_FILE_ATTRIBUTES &&
                !DeleteFileW(file_path)) cleanup = APP_SETTINGS_PLATFORM_FAILED;
        if (journal_path[0] != L'\0' &&
                GetFileAttributesW(journal_path) != INVALID_FILE_ATTRIBUTES &&
                !DeleteFileW(journal_path)) cleanup = APP_SETTINGS_PLATFORM_FAILED;
        if (!RemoveDirectoryW(directory)) cleanup = APP_SETTINGS_PLATFORM_FAILED;
    }
    if (cleanup != APP_SETTINGS_OK) passed = 0;
    _snprintf(log, sizeof(log) - 1,
            "positron settings-storage selftest %s phase=%d cleanup=%d "
            "same_process_reopen=1 production_enabled=0\r\n",
            passed ? "OK" : "FAILED", phase, cleanup);
    log[sizeof(log) - 1] = '\0';
    AppDebug_Log(log);
    if (cleanup != APP_SETTINGS_OK) {
        /* Preserve failed cleanup evidence; never recursively delete it. */
        AppDebug_Log("positron settings-storage fixture retained under Temp\r\n");
    }
    return passed ? 0 : 1;
}
#endif
