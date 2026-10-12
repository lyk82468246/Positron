#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "app_downloads.h"
#include "app_url_router.h"
#include "../positron_http/positron_http.h"

typedef struct AppDownloadTask {
    int used;
    int phase; /* 0 queued ACK; 1 receiving ACK; 2 transfer; 3 publish ACK;
                * 4 publish worker; 5 terminal ACK; 6 journal failure. */
    int cancel;
    int owns_temporary;
    unsigned long request_id;
    unsigned long order;
    HANDLE worker;
    HANDLE http;
    HANDLE file;
    CRITICAL_SECTION lock;
    AppDownloadRecord record;
    WCHAR path[APP_DOWNLOAD_PATH_MAX];
    WCHAR temporary[APP_DOWNLOAD_PATH_MAX];
} AppDownloadTask;

struct AppDownloads {
    DWORD owner;
    AppSettingsStore *store;
    int closing;
    int error;
    unsigned long revision;
    unsigned long sequence;
    AppDownloadTask tasks[APP_DOWNLOAD_TASK_MAX];
};

static int app_download_headers(void *context, const PHttpStreamResult *metadata,
        const char *headers, unsigned long bytes)
{
    AppDownloadTask *task;
    int reject;

    task = (AppDownloadTask *) context;
    (void) headers;
    (void) bytes;
    EnterCriticalSection(&task->lock);
    task->record.http_status = metadata->status_code;
    task->record.total = metadata->total;
    strcpy(task->record.final_url, metadata->final_url);
    /* Partial Content without our Range request must not be published as a
     * complete object; normal non-2xx is a transport success, not a download. */
    reject = task->cancel || metadata->status_code < 200 ||
            metadata->status_code >= 300 || metadata->status_code == 206 ||
            metadata->total > APP_DOWNLOAD_BODY_LIMIT;
    LeaveCriticalSection(&task->lock);
    return reject;
}

static int app_download_sink(void *context, const unsigned char *block,
        unsigned long bytes, __int64 received, __int64 total)
{
    AppDownloadTask *task;
    DWORD written;
    DWORD error;
    int cancelled;

    task = (AppDownloadTask *) context;
    EnterCriticalSection(&task->lock);
    cancelled = task->cancel;
    LeaveCriticalSection(&task->lock);
    if (cancelled) return 1;
    written = 0;
    if (!WriteFile(task->file, block, bytes, &written, NULL) || written != bytes) {
        error = GetLastError();
        EnterCriticalSection(&task->lock);
        task->record.error = (int) error;
        if (task->record.error == 0) task->record.error = ERROR_WRITE_FAULT;
        LeaveCriticalSection(&task->lock);
        return 1;
    }
    EnterCriticalSection(&task->lock);
    task->record.received = received;
    task->record.total = total;
    LeaveCriticalSection(&task->lock);
    return 0;
}

static void app_download_remove_temporary(AppDownloadTask *task)
{
    /* Only an exact path created with CREATE_NEW by this task, never a
     * startup leftover or user-selected final file. Leave failed deletion. */
    if (task->owns_temporary && DeleteFileW(task->temporary))
        task->owns_temporary = 0;
}

static DWORD WINAPI app_download_worker(LPVOID context)
{
    AppDownloadTask *task;
    PHttpStreamOptions options;
    PHttpStreamResult *metadata;
    HANDLE request;
    int rc;
    int cancelled;

    task = (AppDownloadTask *) context;
    if (task->phase == 4) {
        /* PUBLISHING already committed. MoveFile never replaces an existing
         * destination; a target created during transfer is a safe failure. */
        rc = MoveFileW(task->temporary, task->path);
        EnterCriticalSection(&task->lock);
        if (rc) {
            task->owns_temporary = 0;
            task->record.state = APP_DOWNLOAD_COMPLETE;
        } else {
            task->record.error = (int) GetLastError();
            task->record.state = APP_DOWNLOAD_FAILED;
        }
        LeaveCriticalSection(&task->lock);
        if (!rc) app_download_remove_temporary(task);
        return 0;
    }
    task->file = CreateFileW(task->temporary, GENERIC_WRITE, 0, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (task->file == INVALID_HANDLE_VALUE) {
        EnterCriticalSection(&task->lock);
        task->record.error = (int) GetLastError();
        task->record.state = APP_DOWNLOAD_FAILED;
        LeaveCriticalSection(&task->lock);
        return 0;
    }
    task->owns_temporary = 1;
    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.version = PHTTP_STREAM_VERSION;
    options.timeout_ms = APP_DOWNLOAD_TIMEOUT_MS;
    options.body_limit = APP_DOWNLOAD_BODY_LIMIT;
    options.headers = app_download_headers;
    options.sink = app_download_sink;
    options.user_data = task;
    request = NULL;
    rc = PHttp_StreamCreateGet(task->record.url, NULL, &options, &request);
    EnterCriticalSection(&task->lock);
    task->http = request;
    cancelled = task->cancel;
    if (cancelled && request != NULL) (void) PHttp_StreamCancel(request);
    LeaveCriticalSection(&task->lock);
    if (rc == PHTTP_STREAM_OK) rc = PHttp_StreamGet(request);
    metadata = (PHttpStreamResult *) calloc(1, sizeof(*metadata));
    if (metadata != NULL && request != NULL) {
        metadata->size = sizeof(*metadata);
        metadata->version = PHTTP_STREAM_VERSION;
        if (PHttp_StreamGetResult(request, metadata) == PHTTP_STREAM_OK) {
            EnterCriticalSection(&task->lock);
            task->record.received = metadata->received;
            task->record.total = metadata->total;
            task->record.http_status = metadata->status_code;
            strcpy(task->record.final_url, metadata->final_url);
            LeaveCriticalSection(&task->lock);
        }
    }
    free(metadata);
    if (rc == PHTTP_STREAM_OK && !FlushFileBuffers(task->file)) {
        EnterCriticalSection(&task->lock);
        task->record.error = (int) GetLastError();
        LeaveCriticalSection(&task->lock);
        rc = PHTTP_STREAM_SINK;
    }
    if (!CloseHandle(task->file) && rc == PHTTP_STREAM_OK) {
        EnterCriticalSection(&task->lock);
        task->record.error = (int) GetLastError();
        LeaveCriticalSection(&task->lock);
        rc = PHTTP_STREAM_SINK;
    }
    task->file = INVALID_HANDLE_VALUE;
    EnterCriticalSection(&task->lock);
    if (task->cancel || rc == PHTTP_STREAM_CANCELLED)
        task->record.state = APP_DOWNLOAD_CANCELLED;
    else if (rc == PHTTP_STREAM_OK && task->record.http_status >= 200 &&
            task->record.http_status < 300 && task->record.http_status != 206)
        task->record.state = APP_DOWNLOAD_PUBLISHING;
    else {
        task->record.state = APP_DOWNLOAD_FAILED;
        if (task->record.error == 0) task->record.error = rc;
    }
    LeaveCriticalSection(&task->lock);
    if (task->record.state != APP_DOWNLOAD_PUBLISHING) app_download_remove_temporary(task);
    return 0;
}

const char *AppDownloads_StateText(int state, int chinese)
{
    static const char *english[] = {
        "Queued", "Downloading", "Publishing file", "Completed",
        "Cancelled", "Failed", "Interrupted"
    };
    static const char *simplified[] = {
        "\346\216\222\351\230\237\344\270\255", "\344\270\213\350\275\275\344\270\255", "\345\217\221\345\270\203\346\226\207\344\273\266\344\270\255", "\345\267\262\345\256\214\346\210\220",
        "\345\267\262\345\217\226\346\266\210", "\345\244\261\350\264\245", "\345\267\262\344\270\255\346\226\255"
    };
    if (state < 0 || state > 6) return chinese ? "\346\234\252\347\237\245" : "Unknown";
    return chinese ? simplified[state] : english[state];
}

AppDownloads *AppDownloads_Create(AppSettingsStore *store)
{
    AppDownloads *downloads;
    int slot;

    if (store == NULL) return NULL;
    downloads = (AppDownloads *) calloc(1, sizeof(*downloads));
    if (downloads == NULL) return NULL;
    downloads->owner = GetCurrentThreadId();
    downloads->store = store;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot)
        InitializeCriticalSection(&downloads->tasks[slot].lock);
    return downloads;
}

int AppDownloads_Start(AppDownloads *downloads, const char *url, const WCHAR *path)
{
    AppDownloadTask *task;
    char normalized[APP_DOWNLOAD_URL_MAX];
    FILETIME time;
    SYSTEMTIME system_time;
    int slot;
    int bytes;
    AppUrlSchemeKind scheme;

    scheme = AppUrlRouter_ClassifyScheme(url);
    if (downloads == NULL || downloads->owner != GetCurrentThreadId() ||
            (scheme != APP_URL_SCHEME_HTTP && scheme != APP_URL_SCHEME_HTTPS) ||
            downloads->closing || downloads->sequence == ~0UL || path == NULL || path[0] != L'\\' ||
            wcslen(path) >= APP_DOWNLOAD_PATH_MAX - 40 ||
            AppUrlRouter_ResolveNetworkReference(NULL, url, normalized, sizeof(normalized)) != 0)
        return APP_SETTINGS_INVALID;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return APP_SETTINGS_INVALID;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot)
        if (downloads->tasks[slot].used && !_wcsicmp(downloads->tasks[slot].path, path))
            return APP_SETTINGS_INVALID;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot)
        if (!downloads->tasks[slot].used) break;
    if (slot == APP_DOWNLOAD_TASK_MAX) return APP_SETTINGS_QUEUE_FULL;
    task = &downloads->tasks[slot];
    memset(&task->record, 0, sizeof(task->record));
    bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, task->record.path,
            sizeof(task->record.path), NULL, NULL);
    if (bytes <= 0) return APP_SETTINGS_INVALID;
    wcscpy(task->path, path);
    strcpy(task->record.url, normalized);
    GetSystemTime(&system_time);
    if (!SystemTimeToFileTime(&system_time, &time)) return APP_SETTINGS_PLATFORM_FAILED;
    task->record.created_utc = (((__int64) time.dwHighDateTime << 32) |
            time.dwLowDateTime) / 10000000 - (__int64) 11644473600;
    task->record.total = -1;
    task->record.state = APP_DOWNLOAD_QUEUED;
    task->cancel = 0;
    task->phase = 0;
    task->request_id = 0;
    task->order = ++downloads->sequence;
    task->owns_temporary = 0;
    task->used = 1;
    downloads->error = 0;
    downloads->revision++;
    return APP_SETTINGS_OK;
}

void AppDownloads_Cancel(AppDownloads *downloads, int slot)
{
    AppDownloadTask *task;

    if (downloads == NULL || downloads->owner != GetCurrentThreadId() ||
            slot < 0 || slot >= APP_DOWNLOAD_TASK_MAX) return;
    task = &downloads->tasks[slot];
    if (!task->used || (task->phase >= 3 && task->phase <= 6)) return;
    EnterCriticalSection(&task->lock);
    task->cancel = 1;
    if (task->http != NULL) (void) PHttp_StreamCancel(task->http);
    LeaveCriticalSection(&task->lock);
}

void AppDownloads_AcceptResult(AppDownloads *downloads, const AppSettingsResult *result)
{
    AppDownloadTask *task;
    char suffix[48];
    int slot;

    if (downloads == NULL || result->operation != APP_DOWNLOADS_WRITE) return;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) {
        task = &downloads->tasks[slot];
        if (!task->used || task->request_id != result->request_id) continue;
        task->request_id = 0;
        if (result->result != APP_SETTINGS_OK || result->download == NULL) {
            downloads->error = result->result;
            task->phase = 6;
            task->record.state = APP_DOWNLOAD_FAILED;
            app_download_remove_temporary(task);
        } else if (task->phase == 0) {
            task->record.id = result->download->id;
            _snprintf(suffix, sizeof(suffix) - 1, ".positron-%I64d.part", task->record.id);
            suffix[sizeof(suffix) - 1] = 0;
            strcpy(task->record.temporary, task->record.path);
            if (strlen(task->record.temporary) + strlen(suffix) >= sizeof(task->record.temporary)) {
                task->record.state = APP_DOWNLOAD_FAILED;
                task->record.error = ERROR_FILENAME_EXCED_RANGE;
                task->phase = 5;
            } else {
                strcat(task->record.temporary, suffix);
                MultiByteToWideChar(CP_UTF8, 0, task->record.temporary, -1,
                        task->temporary, APP_DOWNLOAD_PATH_MAX);
                task->phase = 7; /* Journaled queue, waiting for single transport. */
            }
        } else if (task->phase == 1 || task->phase == 3) {
            if (task->phase == 1 && task->cancel) {
                task->phase = 5;
                task->record.state = APP_DOWNLOAD_CANCELLED;
            } else {
                task->phase = task->phase == 1 ? 2 : 4;
                task->worker = CreateThread(NULL, 0, app_download_worker, task, 0, NULL);
                if (task->worker == NULL) {
                    task->phase = 5;
                    task->record.state = APP_DOWNLOAD_FAILED;
                    task->record.error = (int) GetLastError();
                    app_download_remove_temporary(task);
                }
            }
        } else if (task->phase == 5) task->used = 0;
        downloads->revision++;
        break;
    }
}

void AppDownloads_Tick(AppDownloads *downloads)
{
    AppDownloadTask *task;
    int slot;
    int active;
    int rc;
    int indices[APP_DOWNLOAD_TASK_MAX];
    int index;

    if (downloads == NULL) return;
    active = 0;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) {
        task = &downloads->tasks[slot];
        if (!task->used) continue;
        if (task->worker != NULL) {
            if (WaitForSingleObject(task->worker, 0) != WAIT_OBJECT_0) { active = 1; continue; }
            CloseHandle(task->worker);
            task->worker = NULL;
            if (task->http != NULL) {
                (void) PHttp_StreamClose(task->http);
                task->http = NULL;
            }
            if (task->phase == 2 && task->cancel &&
                    task->record.state == APP_DOWNLOAD_PUBLISHING) {
                task->record.state = APP_DOWNLOAD_CANCELLED;
                app_download_remove_temporary(task);
            }
            task->phase = task->record.state == APP_DOWNLOAD_PUBLISHING ? 3 : 5;
            downloads->revision++;
        }
        if (task->phase >= 1 && task->phase <= 4) active = 1;
    }
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) indices[slot] = slot;
    if (downloads->tasks[0].used && downloads->tasks[1].used &&
            downloads->tasks[0].order > downloads->tasks[1].order) {
        indices[0] = 1;
        indices[1] = 0;
    }
    for (index = 0; index < APP_DOWNLOAD_TASK_MAX; ++index) {
        slot = indices[index];
        task = &downloads->tasks[slot];
        if (!task->used || task->request_id != 0 || task->worker != NULL ||
                task->phase == 6) continue;
        if (task->phase == 7) {
            if (task->cancel) {
                task->phase = 5;
                task->record.state = APP_DOWNLOAD_CANCELLED;
            } else {
                if (active) continue;
                task->phase = 1;
                task->record.state = APP_DOWNLOAD_RECEIVING;
                active = 1;
            }
        }
        rc = AppSettingsStore_WriteDownload(downloads->store, &task->record, &task->request_id);
        if (rc != APP_SETTINGS_OK && rc != APP_SETTINGS_QUEUE_FULL) {
            downloads->error = rc;
            task->phase = 6;
            task->record.state = APP_DOWNLOAD_FAILED;
            app_download_remove_temporary(task);
            downloads->revision++;
        }
    }
}

int AppDownloads_Snapshot(AppDownloads *downloads, AppDownloadRecord records[APP_DOWNLOAD_TASK_MAX])
{
    int slot;
    int count;

    count = 0;
    memset(records, 0, APP_DOWNLOAD_TASK_MAX * sizeof(*records));
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) records[slot].state = -1;
    if (downloads == NULL) return 0;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) {
        if (!downloads->tasks[slot].used) continue;
        EnterCriticalSection(&downloads->tasks[slot].lock);
        records[slot] = downloads->tasks[slot].record;
        count++;
        LeaveCriticalSection(&downloads->tasks[slot].lock);
    }
    return count;
}

unsigned long AppDownloads_Revision(AppDownloads *downloads)
{ return downloads != NULL ? downloads->revision : 0; }
int AppDownloads_LastError(AppDownloads *downloads)
{ return downloads != NULL ? downloads->error : APP_SETTINGS_DB_FAILED; }

void AppDownloads_RequestClose(AppDownloads *downloads)
{
    int slot;

    if (downloads == NULL) return;
    downloads->closing = 1;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot) AppDownloads_Cancel(downloads, slot);
}

int AppDownloads_TryDestroy(AppDownloads **pointer)
{
    AppDownloads *downloads;
    int slot;

    if (pointer == NULL || *pointer == NULL) return APP_SETTINGS_OK;
    downloads = *pointer;
    if (!downloads->closing) return APP_SETTINGS_INVALID;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot)
        if (downloads->tasks[slot].used && downloads->tasks[slot].phase != 6)
            return APP_SETTINGS_PENDING;
    for (slot = 0; slot < APP_DOWNLOAD_TASK_MAX; ++slot)
        DeleteCriticalSection(&downloads->tasks[slot].lock);
    free(downloads);
    *pointer = NULL;
    return APP_SETTINGS_OK;
}
