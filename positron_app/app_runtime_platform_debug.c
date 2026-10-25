/* Explicit Debug conformance consumer, never a normal startup path. */
#include "app_runtime_platform.h"
#ifdef _DEBUG
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "app_debug.h"

typedef struct AppRuntimePlatformProbe {
    WCHAR directory[MAX_PATH];
    DWORD ui_thread;
    int result;
} AppRuntimePlatformProbe;

static DWORD WINAPI app_runtime_platform_probe(void *parameter)
{
    static const char query[] = "{\"version\":1,\"method\":\"query\","
            "\"sql\":\"SELECT body FROM notes WHERE id=1\",\"params\":[]}";
    static const char insert[] = "{\"version\":1,\"method\":\"execute\","
            "\"sql\":\"INSERT OR REPLACE INTO notes(id,body) VALUES(1,?)\","
            "\"params\":[{\"type\":\"text\",\"value\":\"provider-persist\"}]}";
    static const char denied[] = "{\"version\":1,\"method\":\"execute\","
            "\"sql\":\"ATTACH DATABASE 'other.db' AS other\",\"params\":[]}";
    static const char unicode_ref[] = "%E8%B5%84%E6%BA%90.txt";
    AppRuntimePlatformProbe *probe;
    AppRuntimePlatform platform;
    PRuntimeDirectoryProvider provider;
    PRuntimeStorageProvider storage_provider;
    PRuntimeDirectoryHandle directory;
    PRuntimeDirectoryHandle duplicate;
    PRuntimeStorageHandle storage;
    PRuntimeStorageOptions options;
    PRuntimeStorageError error;
    PRuntimeResourceReference reference;
    char root[1024];
    char *output;
    unsigned int written;
    int phase;
    int rc;
    int round;
    int result;
    char log[512];
    WCHAR manifest_path[MAX_PATH];
    HANDLE mutable;

    probe = (AppRuntimePlatformProbe *) parameter;
    directory = NULL;
    duplicate = NULL;
    storage = NULL;
    output = NULL;
    phase = 0;
    rc = PRUNTIME_STATE;
    result = 1;
    memset(&error, 0, sizeof(error));
    error.size = sizeof(error);
    error.version = PRUNTIME_VERSION;
    if (GetCurrentThreadId() == probe->ui_thread ||
            AppRuntimePlatform_Init(&platform) != PRUNTIME_OK) goto done;
    AppRuntimePlatform_DirectoryProvider(&platform, &provider);
    AppRuntimePlatform_StorageProvider(&platform, &storage_provider);
    if (!WideCharToMultiByte(CP_UTF8, 0, probe->directory, -1, root,
            sizeof(root), NULL, NULL)) goto done;
    output = (char *) malloc(8193);
    if (output == NULL) goto done;
    for (round = 0; round < 2; ++round) {
        phase = 1 + round * 10;
        rc = PRuntime_DevelopmentDirectoryOpen(root, (unsigned int) strlen(root),
                &provider, &directory);
        if (rc != PRUNTIME_OK) goto done;
        rc = PRuntime_DevelopmentDirectoryOpen(root, (unsigned int) strlen(root),
                &provider, &duplicate);
        if (rc != PRUNTIME_BUSY || duplicate != NULL) goto done;
        phase++;
        reference.size = sizeof(reference);
        reference.version = PRUNTIME_VERSION;
        rc = PRuntime_DirectoryReadResource(directory, NULL, 0, NULL, 0,
                output, 8192, &written, &reference);
        if (rc != PRUNTIME_OK || !written ||
                strcmp(reference.url, "app://com.positron.platform_probe/index.html")) goto done;
        phase++;
        lstrcpyW(manifest_path, probe->directory);
        lstrcatW(manifest_path, L"\\manifest.json");
        mutable = CreateFileW(manifest_path, GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (mutable != INVALID_HANDLE_VALUE) { CloseHandle(mutable); goto done; }
        if (GetLastError() != ERROR_SHARING_VIOLATION &&
                GetLastError() != ERROR_ACCESS_DENIED) goto done;
        phase++;
        rc = PRuntime_DirectoryReadResource(directory, NULL, 0,
                unicode_ref, sizeof(unicode_ref) - 1, output, 8192,
                &written, &reference);
        if (rc != PRUNTIME_OK || written < 16 || memcmp(output,
                "unicode-resource", 16)) goto done;
        written = 99;
        memset(output, 0x5a, 16);
        rc = PRuntime_DirectoryReadResource(directory, NULL, 0, "../outside", 10,
                output, 8192, &written, &reference);
        if (rc == PRUNTIME_OK || written != 99 || output[0] != 0x5a) goto done;
        phase++;
        memset(&options, 0, sizeof(options));
        options.size = sizeof(options);
        options.version = PRUNTIME_VERSION;
        options.vm_budget = 1000000;
        rc = PRuntime_DevelopmentStorageOpen(directory, &options, &storage_provider,
                &storage, &error);
        if (rc != PRUNTIME_DENIED || storage != NULL) goto done;
        options.granted_permissions = PRUNTIME_PERMISSION_SQL;
        rc = PRuntime_DevelopmentStorageOpen(directory, &options, &storage_provider,
                &storage, &error);
        if (rc != PRUNTIME_OK) goto done;
        phase++;
        if (round == 0) {
            rc = PRuntime_StorageExecuteJson(storage, insert, sizeof(insert) - 1,
                    output, 8193, &written);
            if (rc != PRUNTIME_OK) goto done;
        }
        rc = PRuntime_StorageExecuteJson(storage, query, sizeof(query) - 1,
                output, 8193, &written);
        if (rc != PRUNTIME_OK || strstr(output, "provider-persist") == NULL) goto done;
        rc = PRuntime_StorageExecuteJson(storage, denied, sizeof(denied) - 1,
                output, 8193, &written);
        if (rc == PRUNTIME_OK) goto done;
        if (PRuntime_StorageClose(&storage) != PRUNTIME_OK ||
                PRuntime_DirectoryClose(&directory) != PRUNTIME_OK) goto done;
    }
    result = 0;
done:
    if (result != 0) {
        if (storage != NULL) (void) PRuntime_StorageGetError(storage, &error);
        _snprintf(log, sizeof(log) - 1,
                "positron runtime-platform phase=%d result=%d db=%d category=%d "
                "sqlite=%d detail=%s\r\n", phase, rc, error.db_result,
                error.db_category, error.sqlite_code, error.message);
        log[sizeof(log) - 1] = '\0';
        AppDebug_Log(log);
    }
    if (storage != NULL) (void) PRuntime_StorageClose(&storage);
    if (directory != NULL) (void) PRuntime_DirectoryClose(&directory);
    if (duplicate != NULL) (void) PRuntime_DirectoryClose(&duplicate);
    free(output);
    probe->result = result;
    if (result == 0) AppDebug_Log("positron runtime-platform proof worker=1 "
            "opens=2 duplicate=busy pinned=1 unicode=1 traversal=denied "
            "grant=explicit sql=restricted reopen=exact\r\n");
    return (DWORD) result;
}

int AppRuntimePlatform_DebugCheck(const WCHAR *directory)
{
    AppRuntimePlatformProbe *probe;
    HANDLE worker;
    int result;

    if (directory == NULL || wcslen(directory) + 32 >= MAX_PATH) return 1;
    probe = (AppRuntimePlatformProbe *) calloc(1, sizeof(*probe));
    if (probe == NULL) return 1;
    lstrcpyW(probe->directory, directory);
    probe->ui_thread = GetCurrentThreadId();
    probe->result = 1;
    worker = CreateThread(NULL, 0, app_runtime_platform_probe, probe, 0, NULL);
    if (worker != NULL) {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    result = probe->result;
    free(probe);
    AppDebug_Log(result ? "runtime-platform FAIL exit=1\r\n" :
            "runtime-platform PASS exit=0\r\n");
    return result;
}
#endif
