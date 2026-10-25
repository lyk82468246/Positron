/* Native WM file/lease adapter. No manifest/URL/SQL policy lives here. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "app_runtime_platform.h"
#include "app_debug.h"

#ifndef FILE_ATTRIBUTE_REPARSE_POINT
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#endif
#define APP_RUNTIME_FILES 128

typedef struct AppRuntimeFile {
    WCHAR path[MAX_PATH];
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
} AppRuntimeFile;

typedef struct AppRuntimeRoot {
    WCHAR path[MAX_PATH];
    AppRuntimeFile *files;
    unsigned int count;
} AppRuntimeRoot;

typedef struct AppRuntimeLocation {
    HANDLE anchor;
    WCHAR root[MAX_PATH];
} AppRuntimeLocation;

static void app_runtime_native_error(const char *operation,
        const WCHAR *path, DWORD error)
{
#ifdef _DEBUG
    char utf8[1024];
    char line[1280];
    DWORD parent_attributes;
    WCHAR parent[MAX_PATH];
    WCHAR *separator;

    utf8[0] = '\0';
    (void) WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8,
            sizeof(utf8), NULL, NULL);
    wcsncpy(parent, path, MAX_PATH - 1);
    parent[MAX_PATH - 1] = L'\0';
    separator = wcsrchr(parent, L'\\');
    if (separator != NULL && separator != parent) *separator = L'\0';
    parent_attributes = GetFileAttributesW(parent);
    _snprintf(line, sizeof(line) - 1,
            "positron runtime-native op=%s error=%lu parent_attrs=%lu path=%s\r\n",
            operation, (unsigned long) error,
            (unsigned long) parent_attributes, utf8);
    line[sizeof(line) - 1] = '\0';
    AppDebug_Log(line);
#else
    (void) operation;
    (void) path;
    (void) error;
#endif
}

static int app_runtime_wide(const char *text, unsigned int bytes,
        WCHAR *output, int capacity)
{
    int count;

    if (text == NULL || bytes == 0 || bytes > 1023 ||
            memchr(text, 0, bytes) != NULL) return PRUNTIME_INVALID;
    count = MultiByteToWideChar(CP_UTF8, 0, text, (int) bytes,
            output, capacity - 1);
    if (count <= 0) return PRUNTIME_INVALID;
    output[count] = L'\0';
    return PRUNTIME_OK;
}

/* Walk real filesystem entries. CeGetCanonicalPathName alone is lexical;
 * enumerated long names reject aliases, wildcards, links and native streams.
 * Only local rooted paths are accepted; no UNC, relative/device namespace. */
static int app_runtime_canonical(const WCHAR *input, WCHAR *output,
        int directory)
{
    WCHAR component[MAX_PATH];
    WCHAR probe[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    const WCHAR *cursor;
    unsigned int length;
    unsigned int count;
    int last;

    if (input == NULL || input[0] != L'\\' || input[1] == L'\\' ||
            input[1] == L'\0' || wcslen(input) >= MAX_PATH)
        return PRUNTIME_INVALID;
    probe[0] = L'\0';
    cursor = input + 1;
    while (*cursor != L'\0') {
        count = 0;
        while (*cursor && *cursor != L'\\') {
            if (*cursor < 32 || wcschr(L"/:*?\"<>|", *cursor) != NULL ||
                    count >= MAX_PATH - 1) return PRUNTIME_INVALID;
            component[count++] = *cursor++;
        }
        component[count] = L'\0';
        if (count == 0 || component[count - 1] == L'.' ||
                component[count - 1] == L' ') return PRUNTIME_INVALID;
        last = *cursor == L'\0' || cursor[1] == L'\0';
        length = (unsigned int) wcslen(probe);
        if (length + count + 2 > MAX_PATH) return PRUNTIME_LIMIT;
        probe[length++] = L'\\';
        lstrcpyW(probe + length, component);
        search = FindFirstFileW(probe, &found);
        if (search == INVALID_HANDLE_VALUE) {
            app_runtime_native_error("find", probe, GetLastError());
            return PRUNTIME_IO;
        }
        FindClose(search);
        if (_wcsicmp(component, found.cFileName) != 0 ||
                (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                ((!last || directory) &&
                !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ||
                (last && !directory &&
                (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))) {
            app_runtime_native_error("canonical-entry", probe, found.dwFileAttributes);
            return PRUNTIME_IO;
        }
        if (length + wcslen(found.cFileName) >= MAX_PATH) return PRUNTIME_LIMIT;
        lstrcpyW(probe + length, found.cFileName);
        if (*cursor) ++cursor;
    }
    lstrcpyW(output, probe);
    return PRUNTIME_OK;
}

static int app_runtime_file_unchanged(AppRuntimeFile *file)
{
    BY_HANDLE_FILE_INFORMATION now;

    if (!GetFileInformationByHandle(file->file, &now)) return 0;
    return now.dwFileAttributes == file->info.dwFileAttributes &&
            now.nFileSizeHigh == file->info.nFileSizeHigh &&
            now.nFileSizeLow == file->info.nFileSizeLow &&
            now.nFileIndexHigh == file->info.nFileIndexHigh &&
            now.nFileIndexLow == file->info.nFileIndexLow &&
            now.dwVolumeSerialNumber == file->info.dwVolumeSerialNumber &&
            now.dwOID == file->info.dwOID &&
            !memcmp(&now.ftLastWriteTime, &file->info.ftLastWriteTime,
            sizeof(FILETIME));
}

/* Pin every admitted regular file without write/delete sharing. New files
 * cannot appear in this instance's catalog. No author-controlled file open
 * occurs after admission; all later bytes come from these retained handles. */
static int app_runtime_scan(AppRuntimeRoot *root, const WCHAR *directory,
        unsigned int depth)
{
    WIN32_FIND_DATAW found;
    WCHAR path[MAX_PATH];
    WCHAR verified[MAX_PATH];
    HANDLE search;
    AppRuntimeFile *file;
    DWORD error;
    int rc;

    if (depth > 16 || wcslen(directory) + 3 >= MAX_PATH) return PRUNTIME_LIMIT;
    lstrcpyW(path, directory);
    lstrcatW(path, L"\\*");
    search = FindFirstFileW(path, &found);
    if (search == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        app_runtime_native_error("scan", path, error);
        return error == ERROR_FILE_NOT_FOUND ? PRUNTIME_OK : PRUNTIME_IO;
    }
    rc = PRUNTIME_OK;
    do {
        if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L".."))
            continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            app_runtime_native_error("scan-link", directory, found.dwFileAttributes);
            rc = PRUNTIME_IO;
            break;
        }
        if (wcslen(directory) + wcslen(found.cFileName) + 2 >= MAX_PATH) {
            rc = PRUNTIME_LIMIT;
            break;
        }
        lstrcpyW(path, directory);
        lstrcatW(path, L"\\");
        lstrcatW(path, found.cFileName);
        rc = app_runtime_canonical(path, verified,
                (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        if (rc != PRUNTIME_OK || wcscmp(path, verified)) {
            app_runtime_native_error("scan-canonical", path, (DWORD) rc);
            rc = PRUNTIME_IO;
            break;
        }
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            rc = app_runtime_scan(root, path, depth + 1);
            if (rc != PRUNTIME_OK) break;
            continue;
        }
        if (root->count == APP_RUNTIME_FILES) { rc = PRUNTIME_LIMIT; break; }
        file = &root->files[root->count];
        file->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file->file == INVALID_HANDLE_VALUE) {
            app_runtime_native_error("pin", path, GetLastError());
            rc = PRUNTIME_IO;
            break;
        }
        ++root->count;
        lstrcpyW(file->path, path);
        if (!GetFileInformationByHandle(file->file, &file->info) ||
                file->info.nNumberOfLinks > 1 ||
                (file->info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT |
                FILE_ATTRIBUTE_DIRECTORY)) ||
                file->info.nFileSizeHigh != found.nFileSizeHigh ||
                file->info.nFileSizeLow != found.nFileSizeLow ||
                (found.dwOID && file->info.dwOID != found.dwOID)) {
#ifdef _DEBUG
            char detail[384];

            _snprintf(detail, sizeof(detail) - 1,
                    "positron runtime-pin-info links=%lu attrs=%lu "
                    "size=%lu,%lu found_size=%lu,%lu oid=%lu found_oid=%lu\r\n",
                    file->info.nNumberOfLinks, file->info.dwFileAttributes,
                    file->info.nFileSizeHigh, file->info.nFileSizeLow,
                    found.nFileSizeHigh, found.nFileSizeLow,
                    file->info.dwOID, found.dwOID);
            detail[sizeof(detail) - 1] = '\0';
            AppDebug_Log(detail);
#endif
            app_runtime_native_error("pin-info", path, GetLastError());
            rc = PRUNTIME_IO;
            break;
        }
    } while (FindNextFileW(search, &found));
    if (rc == PRUNTIME_OK && GetLastError() != ERROR_NO_MORE_FILES) {
        app_runtime_native_error("scan-end", directory, GetLastError());
        rc = PRUNTIME_IO;
    }
    FindClose(search);
    return rc;
}

static void app_runtime_root_close(void *user, void *token)
{
    AppRuntimeRoot *root;
    unsigned int i;

    (void) user;
    root = (AppRuntimeRoot *) token;
    if (root == NULL) return;
    for (i = 0; i < root->count; ++i) CloseHandle(root->files[i].file);
    free(root->files);
    free(root);
}

static int app_runtime_root_open(void *user, const char *directory,
        unsigned int bytes, PRuntimeDirectoryRoot *output)
{
    WCHAR input[MAX_PATH];
    AppRuntimeRoot *root;
    int rc;
    int count;

    (void) user;
    rc = app_runtime_wide(directory, bytes, input, MAX_PATH);
    if (rc != PRUNTIME_OK) return rc;
    root = (AppRuntimeRoot *) calloc(1, sizeof(*root));
    if (root == NULL) return PRUNTIME_MEMORY;
    rc = app_runtime_canonical(input, root->path, 1);
    if (rc != PRUNTIME_OK) { free(root); return rc; }
    root->files = (AppRuntimeFile *) calloc(APP_RUNTIME_FILES, sizeof(*root->files));
    if (root->files == NULL) { free(root); return PRUNTIME_MEMORY; }
    rc = app_runtime_scan(root, root->path, 0);
    if (rc != PRUNTIME_OK) { app_runtime_root_close(NULL, root); return rc; }
    count = WideCharToMultiByte(CP_UTF8, 0, root->path, -1,
            output->canonical, sizeof(output->canonical), NULL, NULL);
    if (count <= 1) { app_runtime_root_close(NULL, root); return PRUNTIME_LIMIT; }
    output->canonical_bytes = (unsigned int) count - 1;
    output->root = root;
    return PRUNTIME_OK;
}

static int app_runtime_lease(void *user, const char *name, void **output)
{
    WCHAR wide[256];
    HANDLE handle;
    DWORD error;

    (void) user;
    if (app_runtime_wide(name, (unsigned int) strlen(name), wide, 256) != PRUNTIME_OK)
        return PRUNTIME_IO;
    SetLastError(0);
    handle = CreateMutexW(NULL, FALSE, wide);
    error = GetLastError();
    if (handle == NULL) return PRUNTIME_IO;
    if (error == ERROR_ALREADY_EXISTS) { CloseHandle(handle); return PRUNTIME_BUSY; }
    *output = handle;
    return PRUNTIME_OK;
}

static void app_runtime_release_lease(void *user, void *token)
{
    (void) user;
    if (token != NULL) CloseHandle((HANDLE) token);
}

static int app_runtime_read(void *user, void *token, const char *key,
        unsigned int limit, PRuntimeBorrowedBytes *output)
{
    AppRuntimeRoot *root;
    AppRuntimeFile *file;
    WCHAR relative[MAX_PATH];
    WCHAR path[MAX_PATH];
    WCHAR verified[MAX_PATH];
    unsigned int i;
    DWORD read;
    unsigned int total;
    unsigned int size;
    char *data;
    int rc;

    (void) user;
    root = (AppRuntimeRoot *) token;
    if (root == NULL || PRuntime_ValidateResourcePath(key,
            (unsigned int) strlen(key)) != PRUNTIME_OK) return PRUNTIME_IO;
    rc = app_runtime_wide(key, (unsigned int) strlen(key), relative, MAX_PATH);
    if (rc != PRUNTIME_OK) return rc;
    for (i = 0; relative[i]; ++i) if (relative[i] == L'/') relative[i] = L'\\';
    if (wcslen(root->path) + wcslen(relative) + 2 >= MAX_PATH) return PRUNTIME_LIMIT;
    lstrcpyW(path, root->path);
    lstrcatW(path, L"\\");
    lstrcatW(path, relative);
    rc = app_runtime_canonical(path, verified, 0);
    if (rc != PRUNTIME_OK) return rc;
    file = NULL;
    for (i = 0; i < root->count; ++i)
        if (!wcscmp(verified, root->files[i].path)) { file = &root->files[i]; break; }
    if (file == NULL || !app_runtime_file_unchanged(file)) return PRUNTIME_IO;
    if (file->info.nFileSizeHigh || file->info.nFileSizeLow > limit)
        return PRUNTIME_LIMIT;
    size = file->info.nFileSizeLow;
    data = (char *) malloc(size ? size : 1);
    if (data == NULL) return PRUNTIME_MEMORY;
    if (SetFilePointer(file->file, 0, NULL, FILE_BEGIN) != 0) {
        free(data); return PRUNTIME_IO;
    }
    total = 0;
    while (total < size) {
        if (!ReadFile(file->file, data + total,
                size - total > 16384 ? 16384 : size - total, &read, NULL) || !read) {
            app_runtime_native_error("read", file->path, GetLastError());
            free(data); return PRUNTIME_IO;
        }
        total += read;
    }
    if (!app_runtime_file_unchanged(file)) { free(data); return PRUNTIME_IO; }
    output->data = data;
    output->bytes = size;
    output->token = data;
    return PRUNTIME_OK;
}

static void app_runtime_bytes_release(void *user, void *root,
        PRuntimeBorrowedBytes *bytes)
{
    (void) user;
    (void) root;
    free(bytes->token);
}

/* Each directory is verified by the same real-entry walk; EEXIST alone is
 * not success. Never retry a failed mkdir or switch to another volume. */
static int app_runtime_mkdir(const WCHAR *path)
{
    WCHAR verified[MAX_PATH];
    DWORD error;

    if (!CreateDirectoryW(path, NULL)) {
        error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            app_runtime_native_error("mkdir", path, error);
            return PRUNTIME_IO;
        }
    }
    if (app_runtime_canonical(path, verified, 1) != PRUNTIME_OK ||
            _wcsicmp(path, verified)) {
        app_runtime_native_error("mkdir-verify", path, GetLastError());
        return PRUNTIME_IO;
    }
    return PRUNTIME_OK;
}

static int app_runtime_location_open(void *user, const char *key,
        PRuntimeStorageLocation *output)
{
    AppRuntimePlatform *platform;
    AppRuntimeLocation *location;
    WCHAR relative[MAX_PATH];
    WCHAR path[MAX_PATH];
    WCHAR *cursor;
    WIN32_FIND_DATAW found;
    HANDLE search;
    DWORD error;
    int rc;
    int count;

    platform = (AppRuntimePlatform *) user;
    if (platform == NULL || PRuntime_ValidateResourcePath(key,
            (unsigned int) strlen(key)) != PRUNTIME_OK) {
        AppDebug_Log("positron runtime-native op=data-key-invalid\r\n");
        return PRUNTIME_IO;
    }
    if (app_runtime_wide(key, (unsigned int) strlen(key), relative,
            MAX_PATH) != PRUNTIME_OK) return PRUNTIME_IO;
    if (wcslen(platform->data_root) + wcslen(relative) + 2 >= MAX_PATH)
        return PRUNTIME_LIMIT;
    rc = app_runtime_mkdir(platform->data_root);
    if (rc != PRUNTIME_OK) return rc;
    lstrcpyW(path, platform->data_root);
    lstrcatW(path, L"\\");
    count = (int) wcslen(path);
    lstrcatW(path, relative);
    for (cursor = path + count; *cursor; ++cursor) {
        if (*cursor == L'/') {
            *cursor = L'\0';
            rc = app_runtime_mkdir(path);
            *cursor = L'\\';
            if (rc != PRUNTIME_OK) return rc;
        }
    }
    location = (AppRuntimeLocation *) calloc(1, sizeof(*location));
    if (location == NULL) return PRUNTIME_MEMORY;
    lstrcpyW(location->root, platform->data_root);
    /* Pin the data root with a private anchor; never touch app.db itself. */
    lstrcpyW(relative, platform->data_root);
    lstrcatW(relative, L"\\.positron-runtime-data");
    location->anchor = CreateFileW(relative, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (location->anchor == INVALID_HANDLE_VALUE) {
        app_runtime_native_error("data-anchor", relative, GetLastError());
        free(location); return PRUNTIME_IO;
    }
    /* Existing DB must be a real contained regular file, never a link/alias. */
    /* CE's attribute sentinel does not provide a reliable missing-file
     * last-error on the shared volume. Enumerate this exact leaf instead;
     * every parent has already been proved to be a real directory. */
    SetLastError(0);
    search = FindFirstFileW(path, &found);
    error = GetLastError();
    if (search != INVALID_HANDLE_VALUE) {
        WCHAR verified[MAX_PATH];

        FindClose(search);
        if (app_runtime_canonical(path, verified, 0) != PRUNTIME_OK ||
                _wcsicmp(path, verified)) {
            app_runtime_native_error("data-db-canonical", path, GetLastError());
            CloseHandle(location->anchor); free(location); return PRUNTIME_IO;
        }
    } else if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) {
        app_runtime_native_error("data-db-find", path, error);
        CloseHandle(location->anchor); free(location); return PRUNTIME_IO;
    }
    count = WideCharToMultiByte(CP_UTF8, 0, path, -1, output->path,
            sizeof(output->path), NULL, NULL);
    if (count <= 1) { CloseHandle(location->anchor); free(location); return PRUNTIME_LIMIT; }
    output->path_bytes = (unsigned int) count - 1;
    output->token = location;
    return PRUNTIME_OK;
}

static void app_runtime_location_close(void *user, void *token)
{
    AppRuntimeLocation *location;

    (void) user;
    location = (AppRuntimeLocation *) token;
    if (location == NULL) return;
    CloseHandle(location->anchor);
    free(location);
}

int AppRuntimePlatform_Init(AppRuntimePlatform *platform)
{
    WCHAR executable[MAX_PATH];
    WCHAR parent[MAX_PATH];
    WCHAR *separator;
    DWORD count;

    if (platform == NULL) return PRUNTIME_ARGUMENT;
    count = GetModuleFileNameW(NULL, executable, MAX_PATH);
    if (!count || count >= MAX_PATH) return PRUNTIME_IO;
    separator = wcsrchr(executable, L'\\');
    if (separator == NULL || separator == executable) return PRUNTIME_IO;
    *separator = L'\0';
    if (app_runtime_canonical(executable, parent, 1) != PRUNTIME_OK ||
            wcslen(parent) + 32 >= MAX_PATH) return PRUNTIME_IO;
    lstrcpyW(platform->data_root, parent);
    lstrcatW(platform->data_root, L"\\data");
    return PRUNTIME_OK;
}

void AppRuntimePlatform_DirectoryProvider(AppRuntimePlatform *platform,
        PRuntimeDirectoryProvider *provider)
{
    memset(provider, 0, sizeof(*provider));
    provider->size = sizeof(*provider);
    provider->version = PRUNTIME_VERSION;
    provider->user_data = platform;
    provider->open_root = app_runtime_root_open;
    provider->close_root = app_runtime_root_close;
    provider->acquire_lock = app_runtime_lease;
    provider->release_lock = app_runtime_release_lease;
    provider->read = app_runtime_read;
    provider->release_bytes = app_runtime_bytes_release;
}

void AppRuntimePlatform_StorageProvider(AppRuntimePlatform *platform,
        PRuntimeStorageProvider *provider)
{
    memset(provider, 0, sizeof(*provider));
    provider->size = sizeof(*provider);
    provider->version = PRUNTIME_VERSION;
    provider->user_data = platform;
    provider->open_location = app_runtime_location_open;
    provider->close_location = app_runtime_location_close;
}
