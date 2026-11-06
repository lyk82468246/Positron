/* Native WM file/lease adapter. No manifest/URL/SQL policy lives here. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wincrypt.h>
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
    HANDLE lease;
    unsigned char *snapshot[APP_RUNTIME_FILES];
} AppRuntimeRoot;

typedef struct AppRuntimeLocation {
    HANDLE anchor;
    HANDLE database;
    HANDLE lease;
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

int AppRuntimePlatform_CanonicalPath(const WCHAR *path,WCHAR *canonical,int directory)
{
    return app_runtime_canonical(path,canonical,directory);
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

    root = (AppRuntimeRoot *) token;
    if (root == NULL) return;
    for (i = 0; i < root->count; ++i) {
        CloseHandle(root->files[i].file);
        free(root->snapshot[i]);
    }
    if(root->lease) CloseHandle(root->lease);
    if(user && ((AppRuntimePlatform *)user)->revision_root==root)
        ((AppRuntimePlatform *)user)->revision_root=NULL;
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
    if(location->database) CloseHandle(location->database);
    if(location->lease) CloseHandle(location->lease);
    if(location->anchor && location->anchor!=INVALID_HANDLE_VALUE) CloseHandle(location->anchor);
    free(location);
}

int AppRuntimePlatform_Init(AppRuntimePlatform *platform)
{
    WCHAR executable[MAX_PATH];
    WCHAR parent[MAX_PATH];
    WCHAR *separator;
    DWORD count;

    if (platform == NULL) return PRUNTIME_ARGUMENT;
    memset(platform,0,sizeof(*platform));
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

typedef struct AppRevisionHash {
    HCRYPTPROV provider;
    HCRYPTHASH hash;
} AppRevisionHash;
static void revision_hash_close(void *user,void *token)
{
    AppRevisionHash *h;
    (void)user; h=(AppRevisionHash *)token;
    if(!h) return;
    if(h->hash) CryptDestroyHash(h->hash);
    if(h->provider) CryptReleaseContext(h->provider,0);
    free(h);
}
static int revision_hash_begin(void *user,void **out)
{
    AppRevisionHash *h;
    unsigned int i;
    (void)user;
    h=(AppRevisionHash *)calloc(1,sizeof(*h));
    if(!h) return PRUNTIME_MEMORY;
    for(i=0;i<2;++i) {
        if(CryptAcquireContext(&h->provider,NULL,NULL,i ? 24 : PROV_RSA_FULL,CRYPT_VERIFYCONTEXT) &&
                CryptCreateHash(h->provider,CALG_SHA_256,0,0,&h->hash)) { *out=h; return PRUNTIME_OK; }
        if(h->provider) CryptReleaseContext(h->provider,0);
        h->provider=0;
    }
    revision_hash_close(NULL,h); return PRUNTIME_UNSUPPORTED;
}
static int revision_hash_update(void *user,void *token,const void *data,unsigned int bytes)
{
    (void)user;
    return CryptHashData(((AppRevisionHash *)token)->hash,(const BYTE *)data,bytes,0) ? PRUNTIME_OK : PRUNTIME_IO;
}
static int revision_hash_finish(void *user,void *token,unsigned char out[32])
{
    DWORD bytes;
    (void)user; bytes=32;
    return CryptGetHashParam(((AppRevisionHash *)token)->hash,HP_HASHVAL,out,&bytes,0) && bytes==32 ? PRUNTIME_OK : PRUNTIME_IO;
}
static int revision_path_lease(const WCHAR *path,const WCHAR *prefix,HANDLE *out)
{
    void *hash;
    unsigned char digest[32];
    char utf8[1024];
    WCHAR name[128];
    const WCHAR *hex;
    unsigned int i,n;
    int bytes,rc;
    DWORD error;
    HANDLE lease;
    hash=NULL; hex=L"0123456789abcdef";
    bytes=WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),NULL,NULL);
    if(bytes<2) return PRUNTIME_IO;
    rc=revision_hash_begin(NULL,&hash);
    if(rc==PRUNTIME_OK) rc=revision_hash_update(NULL,hash,utf8,(unsigned int)bytes-1);
    if(rc==PRUNTIME_OK) rc=revision_hash_finish(NULL,hash,digest);
    revision_hash_close(NULL,hash);
    if(rc!=PRUNTIME_OK) return rc;
    lstrcpyW(name,prefix); n=(unsigned int)wcslen(name);
    for(i=0;i<32;++i) { name[n+i*2]=hex[digest[i]>>4]; name[n+i*2+1]=hex[digest[i]&15]; }
    name[n+64]=0;
    SetLastError(0); lease=CreateMutexW(NULL,FALSE,name); error=GetLastError();
    if(!lease) return PRUNTIME_IO;
    if(error==ERROR_ALREADY_EXISTS) { CloseHandle(lease); return PRUNTIME_BUSY; }
    *out=lease; return PRUNTIME_OK;
}
static int revision_capture(void *user,const char *directory,unsigned int bytes,PRuntimeDirectoryRoot *out)
{
    AppRuntimePlatform *platform;
    AppRuntimeRoot *root,*check;
    PRuntimeDirectoryRoot captured,again;
    WCHAR input[MAX_PATH],canonical[MAX_PATH];
    HANDLE lease;
    unsigned int i,j,total;
    DWORD count;
    int rc;
    platform=(AppRuntimePlatform *)user; lease=NULL;
    if(platform->revision_root) return PRUNTIME_STATE;
    rc=app_runtime_wide(directory,bytes,input,MAX_PATH);
    if(rc==PRUNTIME_OK) rc=app_runtime_canonical(input,canonical,1);
    if(rc==PRUNTIME_OK) rc=revision_path_lease(canonical,L"Positron.Resource.",&lease);
    if(rc!=PRUNTIME_OK) return rc;
    memset(&captured,0,sizeof(captured)); memset(&again,0,sizeof(again));
    rc=app_runtime_root_open(user,directory,bytes,&captured);
    if(rc!=PRUNTIME_OK) { CloseHandle(lease); return rc; }
    root=(AppRuntimeRoot *)captured.root; root->lease=lease; total=0;
    for(i=0;i<root->count && rc==PRUNTIME_OK;++i) {
        AppRuntimeFile *file;
        file=&root->files[i]; count=file->info.nFileSizeLow;
        if(file->info.nFileSizeHigh || count>4U*1024U*1024U-total) { rc=PRUNTIME_LIMIT; break; }
        total+=count;
        root->snapshot[i]=(unsigned char *)malloc(count ? count : 1);
        if(!root->snapshot[i]) { rc=PRUNTIME_MEMORY; break; }
        if(!app_runtime_file_unchanged(file) || SetFilePointer(file->file,0,NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER ||
                !ReadFile(file->file,root->snapshot[i],count,&count,NULL) || count!=file->info.nFileSizeLow ||
                !app_runtime_file_unchanged(file)) rc=PRUNTIME_IO;
    }
#ifdef _DEBUG
    if(rc==PRUNTIME_OK && platform->capture_hook) platform->capture_hook(platform->capture_user);
#endif
    /* Observe a second COMPLETE enumeration while original files stay pinned.
     * This detects observed changes, not an atomic snapshot of hostile I/O. */
    if(rc==PRUNTIME_OK) rc=app_runtime_root_open(user,directory,bytes,&again);
    check=(AppRuntimeRoot *)again.root;
    if(rc==PRUNTIME_OK && check->count!=root->count) rc=PRUNTIME_IO;
    if(rc==PRUNTIME_OK) for(i=0;i<root->count;++i) {
        for(j=0;j<check->count;++j) if(!wcscmp(root->files[i].path,check->files[j].path)) break;
        if(j==check->count || !app_runtime_file_unchanged(&root->files[i]) ||
                check->files[j].info.dwOID!=root->files[i].info.dwOID ||
                check->files[j].info.nFileIndexLow!=root->files[i].info.nFileIndexLow ||
                check->files[j].info.nFileIndexHigh!=root->files[i].info.nFileIndexHigh ||
                check->files[j].info.dwVolumeSerialNumber!=root->files[i].info.dwVolumeSerialNumber ||
                check->files[j].info.nFileSizeLow!=root->files[i].info.nFileSizeLow) { rc=PRUNTIME_IO; break; }
    }
    app_runtime_root_close(user,again.root);
    if(rc!=PRUNTIME_OK) { app_runtime_root_close(user,root); return rc; }
    platform->revision_root=root; *out=captured; return PRUNTIME_OK;
}
static int revision_enumerate(void *user,void *token,unsigned int index,PRuntimeRevisionFile *out)
{
    AppRuntimeRoot *root;
    const WCHAR *relative;
    unsigned int i;
    (void)user; root=(AppRuntimeRoot *)token;
    if(index>=root->count) return PRUNTIME_NOT_FOUND;
    relative=root->files[index].path+wcslen(root->path)+1;
    if(!WideCharToMultiByte(CP_UTF8,0,relative,-1,out->path,sizeof(out->path),NULL,NULL)) return PRUNTIME_LIMIT;
    for(i=0;out->path[i];++i) if(out->path[i]=='\\') out->path[i]='/';
    out->file_bytes=root->files[index].info.nFileSizeLow;
    return PRUNTIME_OK;
}
static int revision_read_at(void *user,void *token,const char *path,unsigned int offset,
        void *buffer,unsigned int capacity,unsigned int *written)
{
    AppRuntimeRoot *root;
    PRuntimeRevisionFile file;
    unsigned int i,n;
    root=(AppRuntimeRoot *)token;
    for(i=0;i<root->count;++i) {
        if(revision_enumerate(user,token,i,&file)!=PRUNTIME_OK) return PRUNTIME_IO;
        if(!strcmp(file.path,path)) break;
    }
    if(i==root->count || offset>file.file_bytes) return PRUNTIME_IO;
    n=file.file_bytes-offset; if(n>capacity) n=capacity;
    if(n) memcpy(buffer,root->snapshot[i]+offset,n);
    *written=n; return PRUNTIME_OK;
}
void AppRuntimePlatform_RevisionProvider(AppRuntimePlatform *p,PRuntimeRevisionProvider *out)
{
    memset(out,0,sizeof(*out)); out->size=sizeof(*out); out->version=1;
    out->user_data=p; out->digest_algorithm=PRUNTIME_DIGEST_SHA256;
    out->open_root=revision_capture; out->close_root=app_runtime_root_close;
    out->enumerate_file=revision_enumerate; out->read_at=revision_read_at;
    out->hash_begin=revision_hash_begin; out->hash_update=revision_hash_update;
    out->hash_finish=revision_hash_finish; out->hash_close=revision_hash_close;
}
int AppRuntimePlatform_CopyManifest(AppRuntimePlatform *p,void *buffer,unsigned int capacity,unsigned int *bytes)
{
    AppRuntimeRoot *root;
    PRuntimeRevisionFile file;
    unsigned int i;
    root=(AppRuntimeRoot *)p->revision_root;
    if(!root) return PRUNTIME_STATE;
    for(i=0;i<root->count;++i) {
        if(revision_enumerate(p,root,i,&file)!=PRUNTIME_OK) return PRUNTIME_IO;
        if(!strcmp(file.path,"manifest.json")) {
            if(file.file_bytes>capacity) return PRUNTIME_LIMIT;
            return revision_read_at(p,root,file.path,0,buffer,capacity,bytes);
        }
    }
    return PRUNTIME_IO;
}
static int installed_location(void *user,const char *key,PRuntimeStorageLocation *out)
{
    AppRuntimePlatform *p;
    AppRuntimeLocation *location;
    WCHAR relative[MAX_PATH],path[MAX_PATH],canonical[MAX_PATH],anchor[MAX_PATH];
    int n,rc;
    unsigned int i;
    p=(AppRuntimePlatform *)user;
    if(PRuntime_ValidateResourcePath(key,(unsigned int)strlen(key))!=PRUNTIME_OK ||
            app_runtime_wide(key,(unsigned int)strlen(key),relative,MAX_PATH)!=PRUNTIME_OK) return PRUNTIME_INVALID;
    if(wcslen(p->data_root)+wcslen(relative)+2>=MAX_PATH) return PRUNTIME_LIMIT;
    lstrcpyW(path,p->data_root); lstrcatW(path,L"\\"); lstrcatW(path,relative);
    for(i=0;path[i];++i) if(path[i]=='/') path[i]='\\';
    rc=app_runtime_canonical(path,canonical,0);
    if(rc!=PRUNTIME_OK) return rc;
    location=(AppRuntimeLocation *)calloc(1,sizeof(*location));
    if(!location) return PRUNTIME_MEMORY;
    rc=revision_path_lease(canonical,L"Positron.Data.",&location->lease);
    if(rc!=PRUNTIME_OK) { free(location); return rc; }
    lstrcpyW(anchor,p->data_root); lstrcatW(anchor,L"\\.positron-runtime-data");
    location->anchor=CreateFileW(anchor,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_HIDDEN,NULL);
    location->database=CreateFileW(canonical,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(location->anchor==INVALID_HANDLE_VALUE || location->database==INVALID_HANDLE_VALUE) {
        if(location->anchor==INVALID_HANDLE_VALUE) location->anchor=NULL;
        if(location->database==INVALID_HANDLE_VALUE) location->database=NULL;
        app_runtime_location_close(user,location); return PRUNTIME_IO;
    }
    n=WideCharToMultiByte(CP_UTF8,0,canonical,-1,out->path,sizeof(out->path),NULL,NULL);
    if(n<2) { app_runtime_location_close(user,location); return PRUNTIME_LIMIT; }
    out->path_bytes=(unsigned int)n-1; out->token=location; return PRUNTIME_OK;
}
void AppRuntimePlatform_ExistingStorageProvider(AppRuntimePlatform *p,PRuntimeInstalledStorageProvider *out)
{
    memset(out,0,sizeof(*out)); out->size=sizeof(*out); out->version=1; out->user_data=p;
    out->open_existing_location=installed_location; out->close_location=app_runtime_location_close;
}
