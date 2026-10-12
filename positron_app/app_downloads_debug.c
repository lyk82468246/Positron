#ifdef _DEBUG
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "app_downloads.h"
#include "app_debug.h"
#include "../positron_http/positron_http.h"
#define DTEST_SIZE (2UL * 1024UL * 1024UL)
typedef struct DownloadServer {
    SOCKET listener;
    HANDLE thread;
    HANDLE stop;
    int status;
    int slow;
    char url[96];
} DownloadServer;

static int dtest_send(SOCKET socket, const char *bytes, int length)
{
    int n;
    while (length > 0) {
        n = send(socket, bytes, length, 0);
        if (n <= 0) return 0;
        length -= n;
        bytes += n;
    }
    return 1;
}
static DWORD WINAPI dtest_server(LPVOID context)
{
    DownloadServer *server;
    SOCKET client;
    fd_set set;
    struct timeval timeout;
    DWORD started;
    DWORD io_timeout;
    char buffer[4096];
    char header[128];
    int n;
    int count;
    int i;
    server = (DownloadServer *) context;
    started = GetTickCount();
    client = INVALID_SOCKET;
    while (GetTickCount() - started < 15000UL &&
            WaitForSingleObject(server->stop, 0) == WAIT_TIMEOUT) {
        FD_ZERO(&set);
        FD_SET(server->listener, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        if (select(0, &set, NULL, NULL, &timeout) > 0) {
            client = accept(server->listener, NULL, NULL);
            break;
        }
    }
    if (client == INVALID_SOCKET) return 1;
    io_timeout = 3000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char *) &io_timeout, sizeof(io_timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (char *) &io_timeout, sizeof(io_timeout));
    count = 0;
    do {
        n = recv(client, buffer + count, sizeof(buffer) - count - 1, 0);
        if (n <= 0) break;
        count += n;
        buffer[count] = 0;
    } while (strstr(buffer, "\r\n\r\n") == NULL && count < sizeof(buffer) - 1);
    _snprintf(header, sizeof(header) - 1,
            "HTTP/1.1 %d Test\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
            server->status, DTEST_SIZE);
    header[sizeof(header) - 1] = 0;
    if (count > 0 && dtest_send(client, header, (int) strlen(header))) {
        memset(buffer, 'D', sizeof(buffer));
        for (i = 0; i < DTEST_SIZE / sizeof(buffer); ++i) {
            if (WaitForSingleObject(server->stop, 0) != WAIT_TIMEOUT ||
                    !dtest_send(client, buffer, sizeof(buffer))) break;
            if (server->slow) Sleep(4);
        }
    }
    closesocket(client);
    return 0;
}
static int dtest_server_start(DownloadServer *server, int status)
{
    struct sockaddr_in address;
    int length;
    memset(server, 0, sizeof(*server));
    server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listener == INVALID_SOCKET) return 0;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server->listener, (struct sockaddr *) &address, sizeof(address)) ||
            listen(server->listener, 1)) { closesocket(server->listener); return 0; }
    length = sizeof(address);
    if (getsockname(server->listener, (struct sockaddr *) &address, &length)) {
        closesocket(server->listener); return 0;
    }
    _snprintf(server->url, sizeof(server->url), "http://127.0.0.1:%u/file", ntohs(address.sin_port));
    server->status = status;
    server->slow = 1;
    server->stop = CreateEvent(NULL, TRUE, FALSE, NULL);
    server->thread = server->stop == NULL ? NULL :
            CreateThread(NULL, 0, dtest_server, server, 0, NULL);
    return server->thread != NULL;
}
static int dtest_server_stop(DownloadServer *server)
{
    if (server->thread == NULL) return 1;
    SetEvent(server->stop);
    if (WaitForSingleObject(server->thread, 10000) != WAIT_OBJECT_0) return 0;
    CloseHandle(server->thread);
    CloseHandle(server->stop);
    closesocket(server->listener);
    server->thread = NULL;
    return 1;
}
static int dtest_result(AppSettingsStore *store, AppDownloads *downloads,
        unsigned long request, AppSettingsResult *out)
{
    AppSettingsResult result;
    DWORD start;
    start = GetTickCount();
    while (GetTickCount() - start < 15000UL) {
        if (AppSettingsStore_Poll(store, &result) == APP_SETTINGS_OK) {
            if (result.request_id == request) { *out = result; return 1; }
            AppDownloads_AcceptResult(downloads, &result);
            AppSettingsResult_Release(&result);
        }
        Sleep(5);
    }
    return 0;
}
static int dtest_finish(AppSettingsStore *store, AppDownloads *downloads,
        int cancel, int race, const WCHAR *path)
{
    AppDownloadRecord *records;
    AppSettingsResult result;
    HANDLE file;
    DWORD written;
    DWORD start;
    int active;
    int acted;
    int slot;
    int ok;
    records = (AppDownloadRecord *) calloc(APP_DOWNLOAD_TASK_MAX, sizeof(*records));
    if (records == NULL) return 0;
    start = GetTickCount();
    acted = ok = 0;
    while (GetTickCount() - start < 30000UL) {
        AppDownloads_Tick(downloads);
        while (AppSettingsStore_Poll(store, &result) == APP_SETTINGS_OK) {
            if (result.result != APP_SETTINGS_OK) { AppSettingsResult_Release(&result); goto done; }
            AppDownloads_AcceptResult(downloads, &result);
            AppSettingsResult_Release(&result);
        }
        if (AppDownloads_Snapshot(downloads, records) == 0) { ok = 1; break; }
        active = 0;
        for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) {
            if (records[slot].state == APP_DOWNLOAD_RECEIVING) active++;
            if (!acted && records[slot].received >= 8192 &&
                    records[slot].state == APP_DOWNLOAD_RECEIVING) {
                if (cancel) {
                    if (cancel == 2) AppDownloads_RequestClose(downloads);
                    else AppDownloads_Cancel(downloads, slot);
                    acted = 1;
                }
                if (race) {
                    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
                    if (file == INVALID_HANDLE_VALUE) goto done;
                    written = 0;
                    if (!WriteFile(file, "KEEP", 4, &written, NULL) || written != 4) {
                        CloseHandle(file); goto done;
                    }
                    CloseHandle(file);
                    acted = 1;
                }
            }
        }
        if (active > 1 || AppDownloads_LastError(downloads) != 0) goto done;
        Sleep(5);
    }
    if ((cancel || race) && !acted) ok = 0;
done:
    free(records);
    return ok;
}
static int dtest_file(const WCHAR *path, int sentinel)
{
    HANDLE file;
    char buffer[4096];
    DWORD bytes;
    DWORD total;
    unsigned int i;
    int ok;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    total = 0;
    ok = 0;
    if (sentinel) {
        ok = ReadFile(file, buffer, sizeof(buffer), &bytes, NULL) &&
                bytes == 4 && !memcmp(buffer, "KEEP", 4);
    } else while (ReadFile(file, buffer, sizeof(buffer), &bytes, NULL)) {
        if (bytes == 0) { ok = total == DTEST_SIZE; break; }
        for (i = 0; i < bytes; ++i) if (buffer[i] != 'D') goto done;
        total += bytes;
    }
done:
    CloseHandle(file);
    return ok;
}
static int dtest_seed_v3(const WCHAR *dll, const char *path)
{
    static const char *schema =
        "CREATE TABLE app_settings(id INTEGER PRIMARY KEY,schema_version INTEGER,startup_page TEXT);"
        "INSERT INTO app_settings VALUES(1,1,'positron://welcome');"
        "CREATE TABLE app_visits(id INTEGER PRIMARY KEY AUTOINCREMENT,url TEXT,title TEXT,visited_utc INTEGER);"
        "INSERT INTO app_visits(url,title,visited_utc) VALUES('https://example.invalid/retained','Retained',1);"
        "CREATE TABLE app_preferences(id INTEGER PRIMARY KEY,startup_url TEXT,language INTEGER,javascript_enabled INTEGER);"
        "INSERT INTO app_preferences VALUES(1,'https://example.invalid/home',2,0);";
    HMODULE module;
    PDbHandle db;
    int (*open_db)(const char *, int, PDbHandle *);
    int (*migrate)(PDbHandle, int, const char *);
    void (*close_db)(PDbHandle);
    int ok;

    module = LoadLibraryW(dll);
    if (module == NULL) return 0;
    open_db = (int (*)(const char *, int, PDbHandle *)) GetProcAddress(module, TEXT("PDb_OpenUtf8"));
    migrate = (int (*)(PDbHandle, int, const char *)) GetProcAddress(module, TEXT("PDb_ApplyMigration"));
    close_db = (void (*)(PDbHandle)) GetProcAddress(module, TEXT("PDb_Close"));
    db = NULL;
    ok = open_db != NULL && migrate != NULL && close_db != NULL &&
            open_db(path, PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK && migrate(db, 3, schema) == PDB_OK;
    if (db != NULL) close_db(db);
    FreeLibrary(module);
    return ok;
}

static int dtest_list(const WCHAR *dll)
{
    AppSettingsStore *store;
    AppSettingsValues values;
    AppSettingsResult result;
    AppDownloadRecord *record;
    unsigned long request;
    __int64 before;
    DWORD start;
    int i;
    int ok;

    store = NULL;
    record = (AppDownloadRecord *) calloc(1, sizeof(*record));
    memset(&result, 0, sizeof(result));
    ok = 0;
    if (record == NULL || AppSettingsStore_Create(dll, ":memory:", &store) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, 1, &result) || result.result != APP_SETTINGS_OK) goto done;
    AppSettingsResult_Release(&result);
    AppSettingsValues_Default(&values);
    strcpy(values.startup_url, "positron://welcome");
    values.language = APP_SETTINGS_LANGUAGE_CHINESE;
    values.javascript_enabled = 0;
    if (AppSettingsStore_SaveValues(store, &values, 1, 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK) goto done;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_AddVisit(store, "https://example.invalid/visit", "Retained", 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK) goto done;
    AppSettingsResult_Release(&result);
    for (i = 0; i < 20; ++i) {
        memset(record, 0, sizeof(*record));
        strcpy(record->url, "https://example.invalid/?a=1&b=2");
        strcpy(record->path, "\\fixture\\<retained>&.bin");
        record->total = -1;
        if (AppSettingsStore_WriteDownload(store, record, &request) != APP_SETTINGS_OK ||
                !dtest_result(store, NULL, request, &result) ||
                result.result != APP_SETTINGS_OK || result.download == NULL) goto done;
        *record = *result.download;
        AppSettingsResult_Release(&result);
        record->state = APP_DOWNLOAD_FAILED;
        if (AppSettingsStore_WriteDownload(store, record, &request) != APP_SETTINGS_OK ||
                !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK) goto done;
        AppSettingsResult_Release(&result);
    }
    if (AppSettingsStore_ReadDownloads(store, 0, 1, 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK ||
            result.downloads == NULL || result.downloads->count != 16 || !result.downloads->has_more) goto done;
    before = result.downloads->entries[15].id;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_ReadDownloads(store, before, 1, 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK ||
            result.downloads == NULL || result.downloads->count != 4 || result.downloads->has_more) goto done;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_ClearDownloads(store, 1, 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK) goto done;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_Submit(store, APP_SETTINGS_LOAD, APP_SETTINGS_START_NEWTAB, 1, 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK ||
            strcmp(result.values.startup_url, values.startup_url) ||
            result.values.language != values.language || result.values.javascript_enabled != 0) goto done;
    AppSettingsResult_Release(&result);
    if (AppSettingsStore_ReadVisits(store, 0, 1, 1, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, request, &result) || result.result != APP_SETTINGS_OK ||
            result.visits == NULL || result.visits->count != 1) goto done;
    ok = 1;
done:
    AppSettingsResult_Release(&result);
    free(record);
    if (store != NULL) {
        AppSettingsStore_RequestClose(store);
        start = GetTickCount();
        while (AppSettingsStore_TryDestroy(&store) == APP_SETTINGS_PENDING &&
                GetTickCount() - start < 15000UL) Sleep(5);
        if (store != NULL) ok = 0;
    }
    if (ok) AppDebug_Log("downloads-list selftest OK pagination=16/4 settings=retained visits=retained\r\n");
    return ok;
}

int AppDownloads_DebugRun(int reopen)
{
    WCHAR executable[512];
    WCHAR dll[512];
    WCHAR path[512];
    WCHAR database_wide[512];
    WCHAR *slash;
    const WCHAR *prefix;
    char database[APP_SETTINGS_PATH_MAX];
    AppSettingsStore *store;
    AppSettingsStore *other;
    AppDownloads *downloads;
    AppSettingsResult result;
    AppDownloadRecord *record;
    DownloadServer server;
    AppSettingsValues defaults;
    WSADATA sockets;
    unsigned long request;
    DWORD start;
    int stage;
    int i;
    int rc;
    int counts[7];
    int http_initialized;
    int sockets_initialized;
    char message[128];
    store = NULL;
    other = NULL;
    downloads = NULL;
    record = NULL;
    memset(&server, 0, sizeof(server));
    memset(&result, 0, sizeof(result));
    stage = 0;
    rc = 1;
    http_initialized = sockets_initialized = 0;
    prefix = L"\\Storage Card\\Temp\\Positron-device-gate\\app-downloads-";
    if (!GetModuleFileNameW(NULL, executable, 512) ||
            wcsncmp(executable, prefix, wcslen(prefix))) goto done;
    slash = wcsrchr(executable, L'\\');
    if (slash == NULL) goto done;
    slash[1] = 0;
    wcscpy(dll, executable); wcscat(dll, L"positron_db.dll");
    wcscpy(database_wide, executable); wcscat(database_wide, L"positron.db");
    if (!reopen && GetFileAttributesW(database_wide) != INVALID_FILE_ATTRIBUTES) goto done;
    if (!WideCharToMultiByte(CP_UTF8, 0, database_wide, -1, database, sizeof(database), NULL, NULL) ||
            WSAStartup(MAKEWORD(2,2), &sockets) != 0) goto done;
    sockets_initialized = 1;
    if (!PHttp_Init()) goto done;
    http_initialized = 1;
    if (!reopen && !dtest_seed_v3(dll, database)) goto done;
    if (AppSettingsStore_Create(dll, database, &store) != APP_SETTINGS_OK ||
            !dtest_result(store, NULL, 1, &result) || result.result != APP_SETTINGS_OK ||
            strcmp(result.values.startup_url, "https://example.invalid/home") ||
            result.values.language != APP_SETTINGS_LANGUAGE_CHINESE || result.values.javascript_enabled != 0) goto done;
    AppSettingsResult_Release(&result);
    downloads = AppDownloads_Create(store);
    record = (AppDownloadRecord *) calloc(1, sizeof(*record));
    if (downloads == NULL || record == NULL) goto done;
    stage = 1;
    wcscpy(path, executable); wcscat(path, L"large-\x4e2d\x6587.bin");
    if (reopen) {
        if (!dtest_file(path, 0) ||
                AppSettingsStore_ReadDownloads(store, 0, 1, 1, &request) != APP_SETTINGS_OK ||
                !dtest_result(store, downloads, request, &result) ||
                result.result != APP_SETTINGS_OK || result.downloads == NULL) goto done;
        memset(counts, 0, sizeof(counts));
        for (i = 0; i < result.downloads->count; ++i) counts[result.downloads->entries[i].state]++;
        if (counts[3] != 1 || counts[4] != 4 || counts[5] != 2 || counts[6] != 1) goto done;
        AppSettingsResult_Release(&result);
        wcscpy(path, executable); wcscat(path, L"race.bin");
        if (!dtest_file(path, 1)) goto done;
        if (AppSettingsStore_ClearDownloads(store, 1, 1, &request) != APP_SETTINGS_OK ||
                !dtest_result(store, downloads, request, &result) || result.result != APP_SETTINGS_OK) goto done;
        AppSettingsResult_Release(&result);
        wcscpy(path, executable); wcscat(path, L"large-\x4e2d\x6587.bin");
        if (!dtest_file(path, 0)) goto done;
        if (AppSettingsStore_ReadVisits(store, 0, 1, 1, &request) != APP_SETTINGS_OK ||
                !dtest_result(store, downloads, request, &result) || result.result != APP_SETTINGS_OK ||
                result.visits == NULL || result.visits->count != 1) goto done;
        AppSettingsResult_Release(&result);
        AppSettingsValues_Default(&defaults);
        if (AppSettingsStore_SaveValues(store, &defaults, 1, 1, &request) != APP_SETTINGS_OK ||
                !dtest_result(store, downloads, request, &result) || result.result != APP_SETTINGS_OK) goto done;
        AppSettingsResult_Release(&result);
        stage = 8;
        rc = 0;
        goto done;
    }
    if (!dtest_list(dll) || AppSettingsStore_Create(dll, database, &other) != APP_SETTINGS_OK ||
            !dtest_result(other, NULL, 1, &result) ||
            result.result != APP_SETTINGS_DB_FAILED || result.db_result != PDB_BUSY) goto done;
    AppSettingsResult_Release(&result);
    AppSettingsStore_RequestClose(other);
    start = GetTickCount();
    while (AppSettingsStore_TryDestroy(&other) == APP_SETTINGS_PENDING &&
            GetTickCount() - start < 15000UL) Sleep(5);
    if (other != NULL) goto done;
    if (!dtest_server_start(&server, 200) ||
            AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK) goto done;
    wcscpy(path, executable); wcscat(path, L"queued.bin");
    if (AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK ||
            AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_INVALID) goto done;
    AppDownloads_Cancel(downloads, 1);
    if (!dtest_finish(store, downloads, 0, 0, path) || !dtest_server_stop(&server) ||
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) goto done;
    wcscpy(path, executable); wcscat(path, L"large-\x4e2d\x6587.bin");
    if (!dtest_file(path, 0) ||
            AppDownloads_Start(downloads, "http://127.0.0.1/", path) != APP_SETTINGS_INVALID) goto done;
    stage = 2;
    wcscpy(path, executable); wcscat(path, L"404.bin");
    if (!dtest_server_start(&server, 404) ||
            AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK ||
            !dtest_finish(store, downloads, 0, 0, path) || !dtest_server_stop(&server) ||
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) goto done;
    stage = 3;
    wcscpy(path, executable); wcscat(path, L"cancel.bin");
    if (!dtest_server_start(&server, 200) ||
            AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK ||
            !dtest_finish(store, downloads, 1, 0, path) || !dtest_server_stop(&server) ||
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) goto done;
    stage = 4;
    wcscpy(path, executable); wcscat(path, L"race.bin");
    if (!dtest_server_start(&server, 200) ||
            AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK ||
            !dtest_finish(store, downloads, 0, 1, path) || !dtest_server_stop(&server) ||
            !dtest_file(path, 1)) goto done;
    stage = 5;
    wcscpy(path, executable); wcscat(path, L"close-active.bin");
    if (!dtest_server_start(&server, 200) ||
            AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK) goto done;
    wcscpy(path, executable); wcscat(path, L"close-queued.bin");
    if (AppDownloads_Start(downloads, server.url, path) != APP_SETTINGS_OK ||
            !dtest_finish(store, downloads, 2, 0, path) || !dtest_server_stop(&server) ||
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) goto done;
    wcscpy(path, executable); wcscat(path, L"race.bin");
    strcpy(record->url, "https://example.invalid/interrupted");
    WideCharToMultiByte(CP_UTF8, 0, path, -1, record->path, sizeof(record->path), NULL, NULL);
    record->total = -1;
    if (AppSettingsStore_WriteDownload(store, record, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, downloads, request, &result) ||
            result.result != APP_SETTINGS_OK || result.download == NULL) goto done;
    *record = *result.download;
    AppSettingsResult_Release(&result);
    record->state = APP_DOWNLOAD_PUBLISHING;
    if (AppSettingsStore_WriteDownload(store, record, &request) != APP_SETTINGS_OK ||
            !dtest_result(store, downloads, request, &result) || result.result != APP_SETTINGS_OK) goto done;
    AppSettingsResult_Release(&result);
    stage = 6;
    rc = 0;
done:
    AppSettingsResult_Release(&result);
    if (other != NULL) {
        AppSettingsStore_RequestClose(other);
        start = GetTickCount();
        while (AppSettingsStore_TryDestroy(&other) == APP_SETTINGS_PENDING &&
                GetTickCount() - start < 15000UL) Sleep(5);
        if (other != NULL) rc = 1;
    }
    AppDownloads_RequestClose(downloads);
    start = GetTickCount();
    while (downloads != NULL && GetTickCount() - start < 15000UL) {
        AppDownloads_Tick(downloads);
        while (AppSettingsStore_Poll(store, &result) == APP_SETTINGS_OK) {
            AppDownloads_AcceptResult(downloads, &result);
            AppSettingsResult_Release(&result);
        }
        if (AppDownloads_TryDestroy(&downloads) == APP_SETTINGS_OK) break;
        Sleep(5);
    }
    if (downloads != NULL || !dtest_server_stop(&server)) rc = 1;
    if (store != NULL && downloads == NULL) {
        AppSettingsStore_RequestClose(store);
        start = GetTickCount();
        while (AppSettingsStore_TryDestroy(&store) == APP_SETTINGS_PENDING &&
                GetTickCount() - start < 15000UL) Sleep(5);
        if (store != NULL) rc = 1;
    }
    if (http_initialized && downloads == NULL) PHttp_Cleanup();
    if (sockets_initialized) WSACleanup();
    free(record);
    _snprintf(message, sizeof(message), "downloads-fixture %s mode=%d stage=%d large=2097152\r\n",
            rc == 0 ? "PASS" : "FAIL", reopen, stage);
    AppDebug_Log(message);
    return rc;
}
#endif
