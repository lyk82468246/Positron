#include <stdlib.h>
#include <string.h>
#include "app_runtime_app.h"

struct AppRuntimePage {
    AppRuntimeApp *app;
    PRuntimeServiceQueueOptions options;
    PRuntimeServiceQueueHandle queue;
    LONG state; /* 0 free, 1 creating, 2 ready, -1 failed */
    LONG released;
};

struct AppRuntimeApp {
    DWORD ui_thread;
    HANDLE thread;
    HANDLE wake;
    HANDLE complete;
    CRITICAL_SECTION serial;
    LONG stopping;
    LONG reading;
    LONG grant_requested;
    LONG state;
    int result;
    char directory[1024];
    /* Published once before state OPEN, immutable thereafter. */
    char manifest[PRUNTIME_MANIFEST_BYTES];
    unsigned int manifest_bytes;
    PRuntimeManifestInfo info;
    /* UI-owned lexical snapshot, never used by worker. */
    PRuntimeManifestHandle resolver;
    int accepted;
    /* Single copied command, protected by serial + event hand-off. */
    char base[1024];
    char reference[1024];
    void *buffer;
    unsigned int capacity;
    unsigned int bytes;
    PRuntimeResourceReference resolved;
    int read_result;
    /* Four host bindings; Runtime owns all request/result queue semantics. */
    AppRuntimePage pages[PRUNTIME_SERVICE_MAX_QUEUES];
};

static int app_runtime_app_cancelled(void *parameter)
{
    AppRuntimeApp *app;

    app = (AppRuntimeApp *) parameter;
    return (int) InterlockedCompareExchange(&app->stopping, 0, 0);
}

/* No host lock is held across DB work, queue calls or provider I/O. Slots are
 * published with Interlocked; UI releases its final alias before worker close. */
static int app_runtime_app_pages(AppRuntimeApp *app, PRuntimeStorageHandle storage)
{
    unsigned int i;
    unsigned int processed;
    AppRuntimePage *page;
    LONG state;
    int busy;
    int rc;

    busy = 0;
    for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; ++i) {
        page = &app->pages[i];
        state = InterlockedCompareExchange(&page->state, 0, 0);
        if (!state) continue;
        if (InterlockedCompareExchange(&page->released, 0, 0)) {
            if (page->queue != NULL) {
                (void) PRuntime_ServiceQueueRevoke(page->queue);
                if (PRuntime_ServiceQueueClose(&page->queue) != PRUNTIME_OK) {
                    busy = 1;
                    continue;
                }
            }
            InterlockedExchange(&page->state, 0);
            continue;
        }
        busy = 1;
        if (state == 1) {
            if (PRuntime_ServiceQueueCreate(storage, &page->options,
                    &page->queue) == PRUNTIME_OK)
                InterlockedExchange(&page->state, 2);
            else InterlockedExchange(&page->state, -1);
        } else if (state == 2 && !app_runtime_app_cancelled(app)) {
            processed = 0;
            rc = PRuntime_ServiceQueueProcessOne(page->queue, &processed);
            if (rc == PRUNTIME_OK && processed) SetEvent(app->wake);
            else if (rc != PRUNTIME_NOT_FOUND && rc != PRUNTIME_OK) {
                app->result = rc;
                InterlockedExchange(&page->state, -1);
                InterlockedExchange(&app->state, APP_RUNTIME_FAILED);
            }
        }
    }
    return busy;
}

static DWORD WINAPI app_runtime_app_worker(void *parameter)
{
    AppRuntimeApp *app;
    AppRuntimePlatform platform;
    PRuntimeDirectoryProvider provider;
    PRuntimeDirectoryHandle directory;
    PRuntimeStorageHandle storage;
    PRuntimeStorageProvider storage_provider;
    PRuntimeStorageOptions storage_options;
    PRuntimeResourceReference resolved;
    int rc;

    app = (AppRuntimeApp *) parameter;
    directory = NULL;
    storage = NULL;
    rc = AppRuntimePlatform_Init(&platform);
    if (rc == PRUNTIME_OK) {
        AppRuntimePlatform_DirectoryProvider(&platform, &provider);
        rc = PRuntime_DevelopmentDirectoryOpen(app->directory,
                (unsigned int) strlen(app->directory), &provider, &directory);
    }
    app->info.size = sizeof(app->info);
    app->info.version = PRUNTIME_VERSION;
    if (rc == PRUNTIME_OK) rc = PRuntime_DirectoryGetInfo(directory, &app->info);
    resolved.size = sizeof(resolved);
    resolved.version = PRUNTIME_VERSION;
    if (rc == PRUNTIME_OK) rc = PRuntime_DirectoryReadResource(directory,
            NULL, 0, "/manifest.json", 14, app->manifest,
            sizeof(app->manifest), &app->manifest_bytes, &resolved);
    app->result = rc;
    if (rc != PRUNTIME_OK) {
        if (directory != NULL) (void) PRuntime_DirectoryClose(&directory);
        InterlockedExchange(&app->state, APP_RUNTIME_FAILED);
        return 0;
    }
    InterlockedExchange(&app->state, APP_RUNTIME_OPEN);
    for (;;) {
        /* ProcessOne self-wakes after progress: coalesced submissions drain
         * without an idle timer, while each pass also services resource I/O. */
        WaitForSingleObject(app->wake, INFINITE);
        if (app_runtime_app_cancelled(app)) {
            app->read_result = PRUNTIME_CANCELLED;
            SetEvent(app->complete);
            if (app_runtime_app_pages(app, storage)) continue;
            break;
        }
        if (InterlockedExchange(&app->grant_requested, 0)) {
            memset(&storage_options, 0, sizeof(storage_options));
            storage_options.size = sizeof(storage_options);
            storage_options.version = PRUNTIME_VERSION;
            storage_options.granted_permissions = PRUNTIME_PERMISSION_SQL;
            storage_options.vm_budget = 1000000;
            storage_options.cancelled = app_runtime_app_cancelled;
            storage_options.user_data = app;
            AppRuntimePlatform_StorageProvider(&platform, &storage_provider);
            rc = PRuntime_DevelopmentStorageOpen(directory, &storage_options,
                    &storage_provider, &storage, NULL);
            app->result = rc;
            InterlockedExchange(&app->state, rc == PRUNTIME_OK ?
                    APP_RUNTIME_READY : APP_RUNTIME_FAILED);
        }
        /* Producer holds serial until complete. Events publish both the
         * command copy and result; no live directory handle leaves here. */
        if (InterlockedExchange(&app->reading, 0)) {
            app->resolved.size = sizeof(app->resolved);
            app->resolved.version = PRUNTIME_VERSION;
            app->read_result = PRuntime_DirectoryReadResource(directory,
                    app->base[0] ? app->base : NULL,
                    (unsigned int) strlen(app->base), app->reference,
                    (unsigned int) strlen(app->reference), app->buffer,
                    app->capacity, &app->bytes, &app->resolved);
            SetEvent(app->complete);
        }
        (void) app_runtime_app_pages(app, storage);
    }
    (void) PRuntime_StorageClose(&storage);
    (void) PRuntime_DirectoryClose(&directory);
    return 0;
}

AppRuntimeApp *AppRuntimeApp_Start(const char *directory)
{
    AppRuntimeApp *app;

    if (directory == NULL || !directory[0] || strlen(directory) > PRUNTIME_ROOT_BYTES)
        return NULL;
    app = (AppRuntimeApp *) calloc(1, sizeof(*app));
    if (app == NULL) return NULL;
    app->ui_thread = GetCurrentThreadId();
    strcpy(app->directory, directory);
    InitializeCriticalSection(&app->serial);
    app->wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    app->complete = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (app->wake != NULL && app->complete != NULL)
        app->thread = CreateThread(NULL, 0, app_runtime_app_worker, app, 0, NULL);
    if (app->thread == NULL) {
        if (app->wake != NULL) CloseHandle(app->wake);
        if (app->complete != NULL) CloseHandle(app->complete);
        DeleteCriticalSection(&app->serial);
        free(app);
        return NULL;
    }
    return app;
}

int AppRuntimeApp_State(AppRuntimeApp *app)
{
    return app == NULL ? APP_RUNTIME_FAILED :
            (int) InterlockedCompareExchange(&app->state, 0, 0);
}

int AppRuntimeApp_Result(AppRuntimeApp *app)
{
    if (app == NULL || AppRuntimeApp_State(app) == APP_RUNTIME_OPENING)
        return PRUNTIME_STATE;
    return app->result;
}

int AppRuntimeApp_GrantSql(AppRuntimeApp *app)
{
    if (app == NULL || app->ui_thread != GetCurrentThreadId() || !app->accepted ||
            app->stopping || AppRuntimeApp_State(app) != APP_RUNTIME_OPEN ||
            app->info.requested_permissions != PRUNTIME_PERMISSION_SQL) return 1;
    InterlockedExchange(&app->state, APP_RUNTIME_STORAGE_OPENING);
    InterlockedExchange(&app->grant_requested, 1);
    SetEvent(app->wake);
    return 0;
}

int AppRuntimeApp_SqlReady(AppRuntimeApp *app)
{
    return app != NULL && !app_runtime_app_cancelled(app) &&
            AppRuntimeApp_State(app) == APP_RUNTIME_READY;
}

int AppRuntimeApp_RequiresSql(AppRuntimeApp *app)
{
    return app != NULL && app->accepted &&
            (app->info.requested_permissions & PRUNTIME_PERMISSION_SQL) != 0;
}

int AppRuntimeApp_Accept(AppRuntimeApp *app, PRuntimeManifestInfo *info,
        char *entry, unsigned int capacity)
{
    PRuntimeResourceReference resolved;
    int rc;

    if (app == NULL || app->ui_thread != GetCurrentThreadId() ||
            AppRuntimeApp_State(app) != APP_RUNTIME_OPEN ||
            info == NULL || entry == NULL || !capacity || app->stopping)
        return PRUNTIME_STATE;
    if (app->resolver == NULL) {
        rc = PRuntime_ManifestCreate(app->manifest, app->manifest_bytes, &app->resolver);
        if (rc != PRUNTIME_OK) return rc;
    }
    resolved.size = sizeof(resolved);
    resolved.version = PRUNTIME_VERSION;
    rc = PRuntime_ManifestResolveReference(app->resolver, NULL, 0, NULL, 0, &resolved);
    if (rc != PRUNTIME_OK) return rc;
    if (strlen(resolved.url) >= capacity) return PRUNTIME_LIMIT;
    *info = app->info;
    strcpy(entry, resolved.url);
    app->accepted = 1;
    return PRUNTIME_OK;
}

int AppRuntimeApp_Resolve(AppRuntimeApp *app, const char *base,
        const char *reference, char *url, int capacity)
{
    PRuntimeResourceReference resolved;
    int rc;

    if (app == NULL || app->ui_thread != GetCurrentThreadId() || !app->accepted ||
            app->stopping || reference == NULL || url == NULL || capacity <= 1)
        return 1;
    resolved.size = sizeof(resolved);
    resolved.version = PRUNTIME_VERSION;
    rc = PRuntime_ManifestResolveReference(app->resolver, base,
            base ? (unsigned int) strlen(base) : 0, reference,
            (unsigned int) strlen(reference), &resolved);
    if (rc != PRUNTIME_OK || strlen(resolved.url) >= (unsigned int) capacity) return 1;
    strcpy(url, resolved.url);
    return 0;
}

int AppRuntimeApp_Read(AppRuntimeApp *app, const char *base,
        const char *reference, void *buffer, unsigned int capacity,
        unsigned int *bytes, PRuntimeResourceReference *resolved)
{
    int rc;

    if (app == NULL || GetCurrentThreadId() == app->ui_thread || reference == NULL ||
            strlen(reference) >= sizeof(app->reference) ||
            (base != NULL && strlen(base) >= sizeof(app->base)) ||
            buffer == NULL || bytes == NULL || resolved == NULL ||
            resolved->size != sizeof(*resolved) || resolved->version != PRUNTIME_VERSION ||
            !capacity || capacity > PRUNTIME_RESOURCE_BYTES) return PRUNTIME_ARGUMENT;
    EnterCriticalSection(&app->serial);
    if (app->stopping || (AppRuntimeApp_State(app) != APP_RUNTIME_OPEN &&
            AppRuntimeApp_State(app) != APP_RUNTIME_READY)) {
        LeaveCriticalSection(&app->serial);
        return PRUNTIME_CANCELLED;
    }
    strcpy(app->base, base ? base : "");
    strcpy(app->reference, reference);
    app->buffer = buffer;
    app->capacity = capacity;
    InterlockedExchange(&app->reading, 1);
    SetEvent(app->wake);
    WaitForSingleObject(app->complete, INFINITE);
    rc = app->read_result;
    if (rc == PRUNTIME_OK) {
        *bytes = app->bytes;
        *resolved = app->resolved;
    }
    app->buffer = NULL;
    LeaveCriticalSection(&app->serial);
    return rc;
}

AppRuntimePage *AppRuntimeApp_BeginPage(AppRuntimeApp *app,
        unsigned long tab_id, unsigned long generation)
{
    unsigned int i;
    AppRuntimePage *page;

    if (!AppRuntimeApp_SqlReady(app) || GetCurrentThreadId() != app->ui_thread ||
            !tab_id || !generation) return NULL;
    /* Only UI reserves free slots. Worker publishes free only after close. */
    for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; ++i) {
        page = &app->pages[i];
        if (InterlockedCompareExchange(&page->state, 0, 0)) continue;
        page->app = app;
        page->options.size = sizeof(page->options);
        page->options.version = PRUNTIME_VERSION;
        page->options.tab_id = tab_id;
        page->options.page_generation = generation;
        page->options.delivery_thread_id = app->ui_thread;
        page->queue = NULL;
        InterlockedExchange(&page->released, 0);
        InterlockedExchange(&page->state, 1);
        SetEvent(app->wake);
        return page;
    }
    return NULL;
}

int AppRuntimePage_Ready(AppRuntimePage *page)
{
    LONG state;

    if (page == NULL || GetCurrentThreadId() != page->app->ui_thread ||
            page->released || app_runtime_app_cancelled(page->app)) return -1;
    state = InterlockedCompareExchange(&page->state, 0, 0);
    return state == 1 ? 0 : (state == 2 ? 1 : -1);
}

int AppRuntimePage_Submit(AppRuntimePage *page, const PRuntimeServiceRequest *request)
{
    int rc;

    if (AppRuntimePage_Ready(page) != 1) return PRUNTIME_STATE;
    rc = PRuntime_ServiceQueueSubmit(page->queue, request);
    if (rc == PRUNTIME_OK) SetEvent(page->app->wake);
    return rc;
}

int AppRuntimePage_Take(AppRuntimePage *page, PRuntimeServiceResult *result)
{
    return AppRuntimePage_Ready(page) == 1 ?
            PRuntime_ServiceQueueTakeResult(page->queue, result) : PRUNTIME_STATE;
}

void AppRuntimePage_Cancel(AppRuntimePage *page, unsigned long bridge, unsigned long request)
{
    if (AppRuntimePage_Ready(page) == 1)
        (void) PRuntime_ServiceQueueCancel(page->queue, bridge, request);
}

void AppRuntimePage_Release(AppRuntimePage **slot)
{
    AppRuntimePage *page;

    if (slot == NULL || *slot == NULL) return;
    page = *slot;
    if (GetCurrentThreadId() != page->app->ui_thread) return;
    if (InterlockedCompareExchange(&page->state, 0, 0) == 2)
        (void) PRuntime_ServiceQueueRevoke(page->queue);
    *slot = NULL;
    InterlockedExchange(&page->released, 1);
    SetEvent(page->app->wake);
}

void AppRuntimeApp_RequestClose(AppRuntimeApp *app)
{
    if (app == NULL || app->ui_thread != GetCurrentThreadId()) return;
    InterlockedExchange(&app->stopping, 1);
    SetEvent(app->wake);
}

int AppRuntimeApp_TryDestroy(AppRuntimeApp **slot)
{
    AppRuntimeApp *app;

    if (slot == NULL) return 1;
    app = *slot;
    if (app == NULL) return 0;
    if (GetCurrentThreadId() != app->ui_thread || !app->stopping ||
            WaitForSingleObject(app->thread, 0) != WAIT_OBJECT_0) return 1;
    (void) PRuntime_ManifestClose(&app->resolver);
    CloseHandle(app->thread);
    CloseHandle(app->wake);
    CloseHandle(app->complete);
    DeleteCriticalSection(&app->serial);
    free(app);
    *slot = NULL;
    return 0;
}
