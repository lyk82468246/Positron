#include <stdlib.h>
#include <string.h>
#include "app_runtime_app.h"

struct AppRuntimeApp {
    DWORD ui_thread;
    HANDLE thread;
    HANDLE wake;
    HANDLE complete;
    CRITICAL_SECTION serial;
    LONG stopping;
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
};

static DWORD WINAPI app_runtime_app_worker(void *parameter)
{
    AppRuntimeApp *app;
    AppRuntimePlatform platform;
    PRuntimeDirectoryProvider provider;
    PRuntimeDirectoryHandle directory;
    PRuntimeResourceReference resolved;
    int rc;

    app = (AppRuntimeApp *) parameter;
    directory = NULL;
    rc = AppRuntimePlatform_Init(&platform);
    if (rc == PRUNTIME_OK) {
        AppRuntimePlatform_DirectoryProvider(&platform, &provider);
        rc = PRuntime_DevelopmentDirectoryOpen(app->directory,
                (unsigned int) strlen(app->directory), &provider, &directory);
    }
    app->info.size = sizeof(app->info);
    app->info.version = PRUNTIME_VERSION;
    if (rc == PRUNTIME_OK) rc = PRuntime_DirectoryGetInfo(directory, &app->info);
    /* First resource-only vertical slice: no silent grant/ignored migration.
     * SQL apps must wait for page queues + SDK, before any author execution. */
    if (rc == PRUNTIME_OK && app->info.requested_permissions != 0)
        rc = PRUNTIME_DENIED;
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
        WaitForSingleObject(app->wake, INFINITE);
        if (InterlockedCompareExchange(&app->stopping, 0, 0)) {
            app->read_result = PRUNTIME_CANCELLED;
            SetEvent(app->complete);
            break;
        }
        /* Producer holds serial until complete. Events publish both the
         * command copy and result; no live directory handle leaves here. */
        app->resolved.size = sizeof(app->resolved);
        app->resolved.version = PRUNTIME_VERSION;
        app->read_result = PRuntime_DirectoryReadResource(directory,
                app->base[0] ? app->base : NULL,
                (unsigned int) strlen(app->base), app->reference,
                (unsigned int) strlen(app->reference), app->buffer,
                app->capacity, &app->bytes, &app->resolved);
        SetEvent(app->complete);
    }
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
    if (app->stopping || AppRuntimeApp_State(app) != APP_RUNTIME_OPEN) {
        LeaveCriticalSection(&app->serial);
        return PRUNTIME_CANCELLED;
    }
    strcpy(app->base, base ? base : "");
    strcpy(app->reference, reference);
    app->buffer = buffer;
    app->capacity = capacity;
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
