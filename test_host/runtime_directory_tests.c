/* Platform fixture only: known private roots, native files/locks, assertions.
 * This is not a general-purpose filesystem canonicalization adapter. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "positron_runtime.h"
#include "positron_db.h"
#include "positron_json.h"
#include "positron_browser.h"
#include "positron_script.h"
#include "positron_core.h"

static const char fixture_manifest[] = "{\"manifestVersion\":1,\"id\":\"Org.Demo_App.Notes\",\"name\":\"Notes\","
    "\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[],\"migrations\":[]}";
static const char* directory_error = "Runtime directory fixture failed";
static char directory_diagnostic[1024];
const char* test1365_runtime_directory_last_error(void) { return directory_error; }
const char* test1367_runtime_migrations_last_error(void) { return directory_error; }
const char* test1368_runtime_storage_last_error(void) { return directory_error; }
const char* test1369_runtime_sql_last_error(void) { return directory_error; }
const char* test1370_runtime_queue_last_error(void) { return directory_error; }
const char* test1371_runtime_page_last_error(void) { return directory_error; }
static char runtime_zip_report[4096];
const char* test1372_runtime_zip_report(void) { return runtime_zip_report; }
BOOL test1372_runtime_zip_probe(void)
{
    static const WCHAR* keys[] = { L".zip", L"CompressedFolder\\CLSID", L"CompressedFolder\\shell\\open\\command",
        L"zipfile\\shell\\open\\command", L"CLSID\\{E88DCCE0-B7B3-11D1-A9F0-00AA0060FA31}\\InProcServer32",
        L"CLSID\\{13709620-C279-11CE-A49E-444553540000}\\InProcServer32" };
    WIN32_FIND_DATAW data;
    HANDLE find;
    HKEY key;
    WCHAR value[256];
    char utf8[768];
    char line[900];
    DWORD size;
    DWORD type;
    DWORD error;
    unsigned int i;
    unsigned int count;
    LONG rc;
    strcpy(runtime_zip_report, "Read-only ROM ZIP inventory (not installer acceptance):");
    count = 0; SetLastError(0); find = FindFirstFileW(L"\\Windows\\*zip*", &data); error = GetLastError();
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (++count > 32) { FindClose(find); directory_error = "ROM ZIP inventory exceeded 32 entries"; return FALSE; }
            if (!WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, utf8, sizeof(utf8), NULL, NULL)) { FindClose(find); return FALSE; }
            sprintf(line, "\nfile=%s bytes=%lu attributes=%lu", utf8, data.nFileSizeLow, data.dwFileAttributes);
            if (strlen(runtime_zip_report) + strlen(line) >= sizeof(runtime_zip_report)) { FindClose(find); return FALSE; }
            strcat(runtime_zip_report, line);
        } while (FindNextFileW(find, &data));
        error = GetLastError(); FindClose(find);
        if (error != ERROR_NO_MORE_FILES) { directory_error = "ROM ZIP enumeration incomplete"; return FALSE; }
    } else if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) {
        sprintf(runtime_zip_report, "ROM ZIP enumeration unavailable native=%lu", error); return FALSE;
    }
    if (!count) strcat(runtime_zip_report, "\nWindows ZIP-named files=0");
    for (i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        key = NULL; rc = RegOpenKeyExW(HKEY_CLASSES_ROOT, keys[i], 0, KEY_READ, &key);
        sprintf(line, "\nassociation[%u] open=%ld", i, rc);
        if (strlen(runtime_zip_report) + strlen(line) >= sizeof(runtime_zip_report)) { if (key) RegCloseKey(key); return FALSE; }
        strcat(runtime_zip_report, line);
        if (rc == ERROR_SUCCESS) {
            memset(value, 0, sizeof(value)); size = sizeof(value); type = 0;
            rc = RegQueryValueExW(key, NULL, NULL, &type, (BYTE*)value, &size); RegCloseKey(key);
            if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size <= sizeof(value) &&
                    size >= sizeof(WCHAR) && value[size/sizeof(WCHAR)-1] == 0 &&
                    WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8, sizeof(utf8), NULL, NULL)) {
                sprintf(line, " value=%s", utf8);
            } else sprintf(line, " query=%ld type=%lu bytes=%lu", rc, type, size);
            if (strlen(runtime_zip_report) + strlen(line) >= sizeof(runtime_zip_report)) return FALSE;
            strcat(runtime_zip_report, line);
        }
    }
    if (strlen(runtime_zip_report) + 100 >= sizeof(runtime_zip_report)) return FALSE;
    strcat(runtime_zip_report, "\nNo extraction/launch/registry write. Shell association alone is not a bounded ZIP API.");
    return TRUE;
}
typedef struct RuntimePageFixture {
    PRuntimeDirectoryHandle directory;
    HANDLE resources;
    HANDLE doc;
    unsigned int role;
    int copied;
    int released;
} RuntimePageFixture;
static int runtime_page_resolve(void* pw, const char* base, const char* ref, char* output, int capacity)
{
    RuntimePageFixture* f;
    PRuntimeResourceReference r;
    int rc;
    f = (RuntimePageFixture*)pw; r.size = sizeof(r); r.version = 1;
    rc = PRuntime_DirectoryResolveReference(f->directory, base, base ? (unsigned int)strlen(base) : 0,
        ref, ref ? (unsigned int)strlen(ref) : 0, &r);
    if (rc || capacity <= (int)strlen(r.url)) return 1;
    strcpy(output, r.url); return 0;
}
static int runtime_page_fetch(void* pw, const char* url, char** data, int* len)
{
    RuntimePageFixture* f;
    PRuntimeResourceReference r;
    unsigned int bytes;
    char* copy;
    int index;
    int rc;
    f = (RuntimePageFixture*)pw; index = -1;
    if (PBrowser_NavigationResourceRegister(f->resources, url,
            f->role == PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET, f->role, &index)) return 1;
    copy = (char*)malloc(4097); if (!copy) return 1;
    if (!PBrowser_NavigationResourceCopyData(f->resources, index, copy, 4097, len)) {
        *data = copy; f->copied++; return 0;
    }
    if (PBrowser_NavigationResourceBeginAttempt(f->resources, index)) { free(copy); return 1; }
    r.size = sizeof(r); r.version = 1; bytes = 0;
    rc = PRuntime_DirectoryReadResource(f->directory, NULL, 0, url, (unsigned int)strlen(url), copy, 4096, &bytes, &r);
    if (!rc) rc = PBrowser_NavigationResourceSetData(f->resources, index, copy, (int)bytes);
    if (rc) {
        PBrowser_NavigationResourceFail(f->resources, index, PBROWSER_NAVIGATION_FAILURE_TRANSPORT);
        free(copy); return 1;
    }
    copy[bytes] = 0; *data = copy; *len = (int)bytes; f->copied++; return 0;
}
static void runtime_page_free(void* pw, char* data)
{ RuntimePageFixture* f; f = (RuntimePageFixture*)pw; free(data); f->released++; }
static int runtime_page_has(void* pw, const char* id)
{ return PCore_NodeExistsById(((RuntimePageFixture*)pw)->doc, id); }
static int runtime_page_text(void* pw, const char* id, char* text, int capacity, int* bytes)
{ return PCore_NodeTextContentById(((RuntimePageFixture*)pw)->doc, id, text, capacity, bytes); }
static int runtime_page_set(void* pw, const char* id, const char* text)
{ return PCore_NodeSetTextContentById(((RuntimePageFixture*)pw)->doc, id, text) == 0 ? 1 : -1; }
typedef struct DirectoryFixture {
    WCHAR root[MAX_PATH];
    WCHAR outer[MAX_PATH];
    char canonical[1024];
    PRuntimeDirectoryHandle instance;
    int opens;
    int closes;
    int reads;
    int releases;
    int allocations;
    int locks;
    int unlocks;
    int fail_open;
    int bad_root;
    int fail_read;
    int oversized;
    int fail_lock;
    int reentry;
    int reentry_ok;
    int fail_at;
} DirectoryFixture;
static int directory_fixture_init(DirectoryFixture* f, DWORD pid)
{
    WCHAR module[MAX_PATH];
    WCHAR* slash;
    DWORD count;
    memset(f, 0, sizeof(*f));
    count = GetModuleFileNameW(NULL, module, MAX_PATH);
    if (!count || count >= MAX_PATH - 50) return 0;
    slash = wcsrchr(module, L'\\'); if (!slash) return 0; *slash = 0;
    wsprintfW(f->outer, L"%s\\runtime-%lu", module, pid);
    wsprintfW(f->root, L"%s\\\x4e2d\x6587", f->outer);
    return WideCharToMultiByte(CP_UTF8, 0, f->root, -1, f->canonical, sizeof(f->canonical), NULL, NULL) > 0;
}
static int directory_fixture_write(DirectoryFixture* f, const WCHAR* name, const void* bytes, DWORD count)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD written;
    char* copy;
    BOOL ok;
    /* This ROM rejected one image-backed constant buffer in WriteFile but
     * accepted an identical private copy. Always use bounded writable storage,
     * not an error-triggered retry; retain exact byte-count assertions. */
    if (count > 32769 || (!bytes && count)) return 0;
    copy = (char*)malloc(count ? count : 1);
    if (!copy) { strcpy(directory_diagnostic, "private file write buffer allocation failed"); return 0; }
    if (count) memcpy(copy, bytes, count);
    wsprintfW(path, L"%s\\%s", f->root, name);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        sprintf(directory_diagnostic, "private file open: error=%lu root=%.800s", GetLastError(), f->canonical);
        free(copy);
        return 0;
    }
    written = 0; ok = WriteFile(file, copy, count, &written, NULL);
    if (!ok || written != count) {
        sprintf(directory_diagnostic, "private file write: ok=%d error=%lu requested=%lu written=%lu root=%.500s",
            ok, GetLastError(), count, written, f->canonical);
    }
    free(copy); CloseHandle(file); return ok && written == count;
}
static int directory_fixture_open(void* user, const char* path, unsigned int bytes, PRuntimeDirectoryRoot* root)
{
    DirectoryFixture* f;
    DWORD attributes;
    f = (DirectoryFixture*)user;
    if (!((bytes == strlen(f->canonical) && !memcmp(path, f->canonical, bytes)) ||
            (bytes == 13 && !memcmp(path, "fixture-alias", 13)))) return PRUNTIME_IO;
    attributes = GetFileAttributesW(f->root);
    if (attributes == 0xffffffffUL || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return PRUNTIME_IO;
    f->opens++; root->root = f;
    strcpy(root->canonical, f->canonical); root->canonical_bytes = (unsigned int)strlen(f->canonical);
    if (f->bad_root) { root->canonical[0] = 0; }
    return f->fail_open ? PRUNTIME_IO : PRUNTIME_OK;
}
static void directory_fixture_close(void* user, void* root)
{ DirectoryFixture* f; f = (DirectoryFixture*)user; if (root == f) f->closes++; }
static int directory_fixture_lock(void* user, const char* name, void** lease)
{
    DirectoryFixture* f;
    WCHAR wide[64];
    HANDLE mutex;
    DWORD error;
    f = (DirectoryFixture*)user;
    if (!MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64)) return PRUNTIME_IO;
    SetLastError(0); mutex = CreateMutexW(NULL, FALSE, wide); error = GetLastError();
    if (!mutex) return PRUNTIME_IO;
    if (error == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return PRUNTIME_BUSY; }
    f->locks++; *lease = mutex;
    return f->fail_lock ? PRUNTIME_IO : PRUNTIME_OK;
}
static void directory_fixture_unlock(void* user, void* lease)
{ DirectoryFixture* f; f = (DirectoryFixture*)user; f->unlocks++; CloseHandle((HANDLE)lease); }
static int directory_fixture_read(void* user, void* root, const char* key, unsigned int limit, PRuntimeBorrowedBytes* out)
{
    DirectoryFixture* f;
    PRuntimeManifestInfo info;
    PRuntimeResourceReference reference;
    PRuntimeDirectoryHandle alias;
    WCHAR relative[256];
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD bytes;
    DWORD high;
    DWORD received;
    unsigned int i;
    void* memory;
    BOOL ok;
    f = (DirectoryFixture*)user; f->reads++;
    if (root != f) return PRUNTIME_IO;
    if (f->reentry && f->instance) {
        alias = f->instance; info.size = sizeof(info); info.version = 1;
        reference.size = sizeof(reference); reference.version = 1;
        f->reentry_ok = PRuntime_DirectoryGetInfo(f->instance, &info) == PRUNTIME_STATE &&
            PRuntime_DirectoryResolveReference(f->instance, NULL, 0, NULL, 0, &reference) == PRUNTIME_STATE &&
            PRuntime_DirectoryClose(&alias) == PRUNTIME_STATE && alias == f->instance;
        {
            PRuntimeMigrationPlanOptions options;
            PRuntimeMigrationPlanHandle plan;
            memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
            options.granted_permissions = PRUNTIME_PERMISSION_SQL; plan = NULL;
            f->reentry_ok = f->reentry_ok && PRuntime_DirectoryPrepareMigrations(f->instance, &options, &plan) == PRUNTIME_STATE && !plan;
        }
    }
    /* Known fixture key -> native file. Runtime already validated it; this
     * provider additionally confines opens to the four files it created. */
    if (strcmp(key, "manifest.json") && strcmp(key, "index.html") &&
            strcmp(key, "\xe5\x9b\xbe.svg") && strcmp(key, "empty") &&
            strcmp(key, "pages/index.html") && strcmp(key, "assets/main.css") &&
            strcmp(key, "assets/theme.css") && strcmp(key, "assets/main.js") && strcmp(key, "assets/icon.svg") &&
            !(strlen(key) == 7 && key[0] == 'm' && key[1] >= '0' && key[1] <= '1' &&
            key[2] >= '0' && key[2] <= '9' && !strcmp(key + 3, ".sql"))) return PRUNTIME_IO;
    if (!MultiByteToWideChar(CP_UTF8, 0, key, -1, relative, 256)) return PRUNTIME_IO;
    for (i = 0; relative[i]; i++) if (relative[i] == '/') relative[i] = '\\';
    if (wcslen(f->root) + wcslen(relative) + 2 > MAX_PATH) return PRUNTIME_LIMIT;
    wsprintfW(path, L"%s\\%s", f->root, relative);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return PRUNTIME_IO;
    high = 0; bytes = GetFileSize(file, &high);
    if (high || bytes > limit) { CloseHandle(file); return PRUNTIME_LIMIT; }
    memory = malloc(bytes ? bytes : 1);
    if (!memory) { CloseHandle(file); return PRUNTIME_MEMORY; }
    received = 0; ok = ReadFile(file, memory, bytes, &received, NULL); CloseHandle(file);
    if (!ok || received != bytes) { free(memory); return PRUNTIME_IO; }
    out->data = memory; out->bytes = f->oversized ? limit + 1 : bytes; out->token = memory;
    f->allocations++;
    return f->fail_read || f->fail_at == f->reads ? PRUNTIME_IO : PRUNTIME_OK;
}
static void directory_fixture_release(void* user, void* root, PRuntimeBorrowedBytes* out)
{
    DirectoryFixture* f;
    f = (DirectoryFixture*)user; if (root == f) f->releases++;
    free(out->token); memset(out, 0, sizeof(*out));
}
static void directory_fixture_provider(DirectoryFixture* f, PRuntimeDirectoryProvider* p)
{
    memset(p, 0, sizeof(*p)); p->size = sizeof(*p); p->version = 1; p->user_data = f;
    p->open_root = directory_fixture_open; p->close_root = directory_fixture_close;
    p->acquire_lock = directory_fixture_lock; p->release_lock = directory_fixture_unlock;
    p->read = directory_fixture_read; p->release_bytes = directory_fixture_release;
}
static void directory_fixture_cleanup(DirectoryFixture* f, int owned_root, int owned_outer)
{
    WCHAR path[MAX_PATH];
    unsigned int i;
    /* Only known files inside a directory created by this fixture. */
    if (owned_root) {
        wsprintfW(path, L"%s\\manifest.json", f->root); DeleteFileW(path);
        wsprintfW(path, L"%s\\index.html", f->root); DeleteFileW(path);
        wsprintfW(path, L"%s\\\x56fe.svg", f->root); DeleteFileW(path);
        wsprintfW(path, L"%s\\empty", f->root); DeleteFileW(path);
        for (i = 1; i <= 16; i++) { wsprintfW(path, L"%s\\m%02u.sql", f->root, i); DeleteFileW(path); }
        RemoveDirectoryW(f->root);
    }
    if (owned_outer) RemoveDirectoryW(f->outer);
}
/* Run before normal test/GUI startup, only for our cross-process fixture. */
int test1365_runtime_directory_command(const WCHAR* command, int* result)
{
    static const WCHAR prefix[] = L"--runtime-directory-probe ";
    DirectoryFixture f;
    PRuntimeDirectoryProvider provider;
    PRuntimeDirectoryHandle instance;
    WCHAR* end;
    unsigned long pid;
    int rc;
    if (!command || wcsncmp(command, prefix, wcslen(prefix))) return 0;
    pid = wcstoul(command + wcslen(prefix), &end, 10); *result = 3;
    if (!pid || *end || !directory_fixture_init(&f, pid)) return 1;
    directory_fixture_provider(&f, &provider); instance = NULL;
    rc = PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &instance);
    if (rc == PRUNTIME_BUSY && !instance && f.opens == f.closes && f.allocations == f.releases) *result = 0;
    PRuntime_DirectoryClose(&instance); return 1;
}
static DWORD WINAPI directory_fixture_wrong_thread(void* user)
{
    DirectoryFixture* f;
    PRuntimeDirectoryHandle alias;
    PRuntimeManifestInfo info;
    f = (DirectoryFixture*)user; alias = f->instance; info.size = sizeof(info); info.version = 1;
    return PRuntime_DirectoryGetInfo(f->instance, &info) == PRUNTIME_STATE &&
        PRuntime_DirectoryClose(&alias) == PRUNTIME_STATE && alias == f->instance ? 0 : 3;
}
#define RUNTIME_VERIFY(test, why) do { if (!(test)) { directory_error = why; goto done; } } while (0)
BOOL test1365_runtime_directory_contract(void)
{
    DirectoryFixture f;
    DirectoryFixture other;
    PRuntimeDirectoryProvider provider;
    PRuntimeDirectoryProvider other_provider;
    PRuntimeDirectoryHandle second;
    PRuntimeManifestInfo info;
    PRuntimeResourceReference resource;
    PRuntimeResourceReference before;
    WCHAR module[MAX_PATH];
    WCHAR command[128];
    PROCESS_INFORMATION process;
    HANDLE thread;
    DWORD exit_code;
    unsigned int written;
    unsigned int i;
    int reads;
    char output[256];
    BOOL ok;
    int owned_root;
    int owned_outer;
    int owned_other_root;
    int owned_other_outer;
    memset(&process, 0, sizeof(process)); second = NULL; thread = NULL; ok = FALSE;
    owned_root = owned_outer = 0;
    owned_other_root = owned_other_outer = 0;
    memset(&other, 0, sizeof(other));
    if (!directory_fixture_init(&f, GetCurrentProcessId())) return FALSE;
    directory_fixture_provider(&f, &provider);
    owned_outer = CreateDirectoryW(f.outer, NULL) != 0;
    if (owned_outer) owned_root = CreateDirectoryW(f.root, NULL) != 0;
    RUNTIME_VERIFY(owned_root, "fresh private Unicode directory");
    RUNTIME_VERIFY(directory_fixture_write(&f, L"manifest.json", fixture_manifest, sizeof(fixture_manifest) - 1) &&
        directory_fixture_write(&f, L"index.html", "<p>notes</p>", 12) &&
        directory_fixture_write(&f, L"\x56fe.svg", "A\0B", 3) && directory_fixture_write(&f, L"empty", "", 0), "native fixture files");
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == PRUNTIME_OK,
        "manifest read, development identity, lease");
    info.size = sizeof(info); info.version = 1;
    RUNTIME_VERIFY(PRuntime_DirectoryGetInfo(f.instance, &info) == PRUNTIME_OK && !strcmp(info.id, "org.demo_app.notes") &&
        !info.requested_permissions, "copied root manifest, no permissions grant");
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen("fixture-alias", 13, &provider, &second) == PRUNTIME_BUSY && !second,
        "same physical identity through provider alias is busy");
    RUNTIME_VERIFY(directory_fixture_init(&other, GetCurrentProcessId() ^ 0x40000000UL), "second root identity");
    owned_other_outer = CreateDirectoryW(other.outer, NULL) != 0;
    if (owned_other_outer) owned_other_root = CreateDirectoryW(other.root, NULL) != 0;
    if (!owned_other_root) {
        DWORD error;
        error = GetLastError();
        sprintf(directory_diagnostic, "second private root: outer=%d root=%d error=%lu equal=%d first=%.400s second=%.400s", owned_other_outer,
            owned_other_root, error, !wcscmp(f.outer, other.outer), f.canonical, other.canonical);
        directory_error = directory_diagnostic; goto done;
    }
    RUNTIME_VERIFY(owned_other_root && directory_fixture_write(&other, L"manifest.json", fixture_manifest, sizeof(fixture_manifest) - 1) &&
        directory_fixture_write(&other, L"index.html", "other", 5), "same-ID distinct-root fixtures");
    directory_fixture_provider(&other, &other_provider);
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(other.canonical, (unsigned int)strlen(other.canonical), &other_provider, &other.instance) == PRUNTIME_OK,
        "same ID in different physical development root remains distinct");
    resource.size = sizeof(resource); resource.version = 1;
    RUNTIME_VERIFY(PRuntime_DirectoryReadResource(other.instance, NULL, 0, NULL, 0, output, sizeof(output), &written, &resource) == PRUNTIME_OK &&
        written == 5 && !memcmp(output, "other", 5), "second root reads only its own bytes");
    RUNTIME_VERIFY(PRuntime_DirectoryReadResource(f.instance, NULL, 0, NULL, 0, output, sizeof(output), &written, &resource) == PRUNTIME_OK &&
        written == 12 && !memcmp(output, "<p>notes</p>", 12), "first root remains isolated");
    RUNTIME_VERIFY(PRuntime_DirectoryClose(&other.instance) == PRUNTIME_OK && other.opens == other.closes &&
        other.allocations == other.releases && other.locks == other.unlocks, "second root complete release");
    GetModuleFileNameW(NULL, module, MAX_PATH); wsprintfW(command, L"--runtime-directory-probe %lu", GetCurrentProcessId());
    RUNTIME_VERIFY(CreateProcessW(module, command, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process), "cross-process lock probe start");
    RUNTIME_VERIFY(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exit_code) &&
        exit_code == 0, "cross-process duplicate rejected by Runtime");
    CloseHandle(process.hThread); CloseHandle(process.hProcess); memset(&process, 0, sizeof(process));
    thread = CreateThread(NULL, 0, directory_fixture_wrong_thread, &f, 0, NULL);
    RUNTIME_VERIFY(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &exit_code) && exit_code == 0,
        "owner-thread guard"); CloseHandle(thread); thread = NULL;
    resource.size = sizeof(resource); resource.version = 1; written = 91;
    f.reentry = 1;
    provider.read = NULL; /* Runtime must have copied the table at open. */
    RUNTIME_VERIFY(PRuntime_DirectoryReadResource(f.instance, NULL, 0, "/%e5%9b%be.svg?x=1#f", 20,
        output, sizeof(output), &written, &resource) == PRUNTIME_OK && written == 3 && !memcmp(output, "A\0B", 3) &&
        !strcmp(resource.path, "\xe5\x9b\xbe.svg") && !strcmp(resource.query, "x=1") && f.reentry_ok,
        "Unicode binary read, suffix separation, callback reentry"); f.reentry = 0;
    directory_fixture_provider(&f, &provider);
    RUNTIME_VERIFY(PRuntime_DirectoryReadResource(f.instance, NULL, 0, "empty", 5, output, sizeof(output), &written, &resource) == PRUNTIME_OK &&
        written == 0, "empty resource");
    for (i = 0; i < 4; i++) {
        memset(output, 'z', sizeof(output)); written = 91; before = resource; reads = f.reads;
        f.fail_read = i == 0; f.oversized = i == 1;
        RUNTIME_VERIFY(PRuntime_DirectoryReadResource(f.instance, NULL, 0, i == 2 ? "../escape" : "index.html", i == 2 ? 9 : 10,
            output, i == 3 ? 2 : sizeof(output), &written, &resource) == (i == 0 ? PRUNTIME_IO : i == 2 ? PRUNTIME_INVALID : PRUNTIME_LIMIT) &&
            written == 91 && !memcmp(&before, &resource, sizeof(resource)) && output[0] == 'z' && output[255] == 'z' &&
            (i != 2 || reads == f.reads), "read failure preserves all outputs and no traversal I/O");
    }
    f.fail_read = f.oversized = 0;
    RUNTIME_VERIFY(PRuntime_DirectoryClose(&f.instance) == PRUNTIME_OK && !f.instance && PRuntime_DirectoryClose(&f.instance) == PRUNTIME_OK,
        "ordered close and idempotence");
    for (i = 0; i < 4; i++) {
        f.fail_open = i == 0; f.bad_root = i == 1; f.fail_read = i == 2; f.fail_lock = i == 3;
        second = (PRuntimeDirectoryHandle)1;
        RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &second) == PRUNTIME_IO &&
            second == (PRuntimeDirectoryHandle)1 && f.opens == f.closes && f.locks == f.unlocks &&
            f.allocations == f.releases, "open failure unwinds partial roots, bytes and locks"); second = NULL;
    }
    f.fail_open = f.bad_root = f.fail_read = f.fail_lock = 0;
    RUNTIME_VERIFY(directory_fixture_write(&f, L"manifest.json", "{}", 2), "bad manifest setup");
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &second) == PRUNTIME_INVALID &&
        !second && f.opens == f.closes && f.locks == f.unlocks, "root manifest required and rejected without lock leak");
    RUNTIME_VERIFY(directory_fixture_write(&f, L"manifest.json", fixture_manifest, sizeof(fixture_manifest) - 1), "restore manifest");
    for (i = 0; i < 12; i++) {
        RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == PRUNTIME_OK &&
            PRuntime_DirectoryClose(&f.instance) == PRUNTIME_OK, "reopen and teardown");
    }
    provider.version = 9; reads = f.opens;
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &second) == PRUNTIME_ARGUMENT &&
        reads == f.opens && !second, "invalid provider before I/O");
    RUNTIME_VERIFY(f.opens == f.closes && f.locks == f.unlocks, "all roots and leases released"); ok = TRUE;
done:
    if (thread) { WaitForSingleObject(thread, 10000); CloseHandle(thread); }
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    if (second != (PRuntimeDirectoryHandle)1) PRuntime_DirectoryClose(&second);
    PRuntime_DirectoryClose(&f.instance);
    PRuntime_DirectoryClose(&other.instance);
    directory_fixture_cleanup(&f, owned_root, owned_outer);
    directory_fixture_cleanup(&other, owned_other_root, owned_other_outer);
    return ok;
}

static DWORD WINAPI migrations_wrong_thread(void* user)
{
    PRuntimeMigrationPlanHandle alias;
    PRuntimeMigrationPlanInfo info;
    PRuntimeMigrationSource source;
    alias = (PRuntimeMigrationPlanHandle)user;
    info.size = sizeof(info); info.version = 1; source.size = sizeof(source); source.version = 1;
    return PRuntime_MigrationPlanGetInfo(alias, &info) == PRUNTIME_STATE &&
        PRuntime_MigrationPlanGetSource(alias, 0, &source) == PRUNTIME_STATE &&
        PRuntime_MigrationPlanClose(&alias) == PRUNTIME_STATE && alias == (PRuntimeMigrationPlanHandle)user ? 0 : 3;
}
BOOL test1367_runtime_migrations_contract(void)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"org.demo.notes\",\"name\":\"Notes\","
        "\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],"
        "\"migrations\":[{\"version\":1,\"file\":\"m01.sql\"},{\"version\":2,\"file\":\"m02.sql\"}]}";
    static const char sql1[] = "CREATE TABLE notes(body TEXT);\r\n";
    static const char sql2[] = "\tINSERT INTO notes VALUES('\xe4\xb8\xad\xe6\x96\x87');\n";
    static const char* const bad[] = { "", "A\0B", "\xef\xbb\xbfSELECT 1", "\xc0\xaf", "\x01", "\xc2\x80" };
    static const unsigned int bad_bytes[] = { 0, 3, 11, 2, 1, 2 };
    DirectoryFixture f;
    PRuntimeDirectoryProvider provider;
    PRuntimeMigrationPlanOptions options;
    PRuntimeMigrationPlanHandle plan;
    PRuntimeMigrationPlanInfo info;
    PRuntimeMigrationPlanInfo before;
    PRuntimeMigrationSource source;
    PRuntimeMigrationSource prior;
    PDbHandle db;
    PDbStmtHandle stmt;
    PDbRestrictedOptions db_options;
    PDbMigrationReport report;
    WCHAR name[32];
    char catalog[2048];
    char item[96];
    char* big;
    HANDLE thread;
    DWORD code;
    unsigned int i;
    int reads;
    int version;
    int owned_outer;
    int owned_root;
    BOOL ok;
    plan = NULL; db = NULL; stmt = NULL; big = NULL; thread = NULL; ok = FALSE;
    owned_outer = owned_root = 0;
    if (!directory_fixture_init(&f, GetCurrentProcessId() ^ 0x20000000UL)) return FALSE;
    directory_fixture_provider(&f, &provider);
    owned_outer = CreateDirectoryW(f.outer, NULL) != 0;
    if (owned_outer) owned_root = CreateDirectoryW(f.root, NULL) != 0;
    RUNTIME_VERIFY(owned_root && directory_fixture_write(&f, L"manifest.json", manifest, sizeof(manifest) - 1) &&
        directory_fixture_write(&f, L"m01.sql", sql1, sizeof(sql1) - 1) &&
        directory_fixture_write(&f, L"m02.sql", sql2, sizeof(sql2) - 1), "migration native fixtures");
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0,
        "migration bound root");
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
    reads = f.reads;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_DENIED && !plan && reads == f.reads,
        "declaration alone does not authorize source reads");
    options.granted_permissions = 2;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_UNSUPPORTED && !plan && reads == f.reads,
        "unknown native grant fails before I/O");
    options.granted_permissions = PRUNTIME_PERMISSION_SQL; options.current_schema_version = 3;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_SCHEMA && !plan && reads == f.reads,
        "future schema cannot downgrade");
    options.current_schema_version = 0; options.version = 2;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_ARGUMENT && !plan && reads == f.reads,
        "versioned options before I/O"); options.version = 1;
    f.reentry = 1;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == 0 && plan && f.reentry_ok && f.reads == reads + 2,
        "all pending files, no callback reentry"); f.reentry = 0;
    info.size = sizeof(info); info.version = 1;
    source.size = sizeof(source); source.version = 1;
    RUNTIME_VERIFY(PRuntime_MigrationPlanGetInfo(plan, &info) == 0 && info.previous_version == 0 && info.target_version == 2 &&
        info.file_count == 2 && info.total_bytes == sizeof(sql1) + sizeof(sql2) - 2 && !strcmp(info.id, "org.demo.notes"), "copied plan info");
    thread = CreateThread(NULL, 0, migrations_wrong_thread, plan, 0, NULL);
    RUNTIME_VERIFY(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &code) && !code,
        "plan owner-thread guard"); CloseHandle(thread); thread = NULL;
    RUNTIME_VERIFY(PRuntime_DirectoryClose(&f.instance) == 0, "plan independent of directory lifetime");
    RUNTIME_VERIFY(PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK, "private DB integration fixture");
    memset(&db_options, 0, sizeof(db_options)); db_options.size = sizeof(db_options); db_options.version = 1; db_options.vm_budget = 1000000;
    memset(&report, 0, sizeof(report)); report.size = sizeof(report); report.version = 1;
    for (i = 0; i < 2; i++) {
        RUNTIME_VERIFY(PRuntime_MigrationPlanGetSource(plan, i, &source) == 0 && source.schema_version == i + 1 &&
            source.sql_bytes == (i ? sizeof(sql2) : sizeof(sql1)) - 1 && !memcmp(source.sql, i ? sql2 : sql1, source.sql_bytes),
            "exact immutable UTF-8 SQL order and length after root close");
        RUNTIME_VERIFY(PDb_ApplyMigrationRestricted(db, i, i + 1, source.sql, source.sql_bytes, &db_options, &report) == PDB_OK,
            "public source to public restricted DB primitive");
    }
    RUNTIME_VERIFY(PDb_GetMigrationVersion(db, &version) == 0 && version == 2 &&
        PDb_Prepare(db, "SELECT body FROM notes", &stmt) == 0 && PDb_Step(stmt) == PDB_STEP_ROW &&
        !strcmp(PDb_ColumnText(stmt, 0), "\xe4\xb8\xad\xe6\x96\x87"), "migration integration typed text and strict version");
    PDb_Finalize(stmt); stmt = NULL; PDb_Close(db); db = NULL;
    prior = source; before = info;
    RUNTIME_VERIFY(PRuntime_MigrationPlanGetSource(plan, 2, &source) == PRUNTIME_ARGUMENT && !memcmp(&source, &prior, sizeof(source)),
        "source index failure unchanged"); info.version = 2; before = info;
    RUNTIME_VERIFY(PRuntime_MigrationPlanGetInfo(plan, &info) == PRUNTIME_ARGUMENT && !memcmp(&info, &before, sizeof(info)),
        "info version failure unchanged"); info.version = 1;
    RUNTIME_VERIFY(PRuntime_MigrationPlanClose(&plan) == 0 && !plan && PRuntime_MigrationPlanClose(&plan) == 0, "plan release idempotent");
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0, "reopen for suffix");
    options.current_schema_version = 1;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == 0 &&
        PRuntime_MigrationPlanGetInfo(plan, &info) == 0 && info.file_count == 1 && info.previous_version == 1 &&
        PRuntime_MigrationPlanGetSource(plan, 0, &source) == 0 && source.schema_version == 2, "only pending catalog suffix");
    PRuntime_MigrationPlanClose(&plan); reads = f.reads; options.current_schema_version = 2;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == 0 &&
        PRuntime_MigrationPlanGetInfo(plan, &info) == 0 && !info.file_count && reads == f.reads, "up-to-date zero read plan");
    PRuntime_MigrationPlanClose(&plan); options.current_schema_version = 0;
    for (i = 0; i < 2; i++) {
        f.fail_at = i ? f.reads + 2 : 0; f.oversized = i == 0; plan = (PRuntimeMigrationPlanHandle)1;
        RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == (i ? PRUNTIME_IO : PRUNTIME_LIMIT) &&
            plan == (PRuntimeMigrationPlanHandle)1 && f.allocations == f.releases, "partial/oversized acquisition unwinds without plan"); plan = NULL;
    }
    f.fail_at = f.oversized = 0; PRuntime_DirectoryClose(&f.instance);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        RUNTIME_VERIFY(directory_fixture_write(&f, L"m02.sql", bad[i], bad_bytes[i]) &&
            PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0,
            "malformed source fixture"); plan = (PRuntimeMigrationPlanHandle)1;
        RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_INVALID &&
            plan == (PRuntimeMigrationPlanHandle)1 && f.allocations == f.releases, "bad later source exposes no partial plan"); plan = NULL;
        PRuntime_DirectoryClose(&f.instance);
    }
    big = (char*)malloc(PRUNTIME_MIGRATION_SQL_BYTES + 1); RUNTIME_VERIFY(big, "bounded fixture allocation");
    memset(big, ' ', PRUNTIME_MIGRATION_SQL_BYTES + 1);
    RUNTIME_VERIFY(directory_fixture_write(&f, L"m02.sql", big, PRUNTIME_MIGRATION_SQL_BYTES + 1) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0,
        "oversized native SQL fixture");
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_LIMIT && !plan, "real file upper limit");
    PRuntime_DirectoryClose(&f.instance);
    strcpy(catalog, "{\"manifestVersion\":1,\"id\":\"org.demo.notes\",\"name\":\"Notes\",\"version\":1,"
        "\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],\"migrations\":[");
    for (i = 1; i <= 16; i++) {
        sprintf(item, "%s{\"version\":%u,\"file\":\"m%02u.sql\"}", i == 1 ? "" : ",", i, i); strcat(catalog, item);
        wsprintfW(name, L"m%02u.sql", i);
        RUNTIME_VERIFY(directory_fixture_write(&f, name, big, PRUNTIME_MIGRATION_SQL_BYTES), "maximum file fixture");
    }
    strcat(catalog, "]}");
    RUNTIME_VERIFY(directory_fixture_write(&f, L"manifest.json", catalog, (DWORD)strlen(catalog)) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0, "maximum catalog fixture");
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == 0 &&
        PRuntime_MigrationPlanGetInfo(plan, &info) == 0 && info.file_count == 16 && info.total_bytes == PRUNTIME_MIGRATION_TOTAL_BYTES,
        "exact 16 file / 512 KiB snapshot budget");
    PRuntime_MigrationPlanClose(&plan); PRuntime_DirectoryClose(&f.instance);
    RUNTIME_VERIFY(directory_fixture_write(&f, L"manifest.json", fixture_manifest, sizeof(fixture_manifest) - 1) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0, "undeclared SQL fixture");
    reads = f.reads;
    RUNTIME_VERIFY(PRuntime_DirectoryPrepareMigrations(f.instance, &options, &plan) == PRUNTIME_DENIED && !plan && reads == f.reads,
        "native grant cannot invent missing declaration");
    RUNTIME_VERIFY(f.allocations == f.releases, "all acquired migration bytes released"); ok = TRUE;
done:
    if (thread) { WaitForSingleObject(thread, 10000); CloseHandle(thread); }
    if (stmt) PDb_Finalize(stmt);
    if (db) PDb_Close(db);
    if (plan != (PRuntimeMigrationPlanHandle)1) PRuntime_MigrationPlanClose(&plan);
    PRuntime_DirectoryClose(&f.instance); free(big);
    directory_fixture_cleanup(&f, owned_root, owned_outer); return ok;
}

typedef struct StorageFixture {
    DirectoryFixture* directory;
    PRuntimeDirectoryHandle reentry_target;
    WCHAR data[MAX_PATH];
    WCHAR path[MAX_PATH];
    WCHAR override_path[MAX_PATH];
    char keys[8][128];
    unsigned int count;
    int opens;
    int closes;
    int fail;
    int reentry_ok;
    volatile LONG cancelled;
    PRuntimeStorageHandle live;
    int cancel_reentry_ok;
    HANDLE inside;
    HANDLE resume;
    int hold;
    int polls;
} StorageFixture;
static int storage_fixture_directory(const WCHAR* path)
{
    DWORD attributes;
    DWORD error;
    DWORD attribute_error;
    char text[800];
    attributes = GetFileAttributesW(path);
    if (attributes != 0xffffffffUL) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    SetLastError(0);
    if (CreateDirectoryW(path, NULL)) return 1;
    error = GetLastError();
    attributes = GetFileAttributesW(path); attribute_error = GetLastError();
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, text, sizeof(text), NULL, NULL)) strcpy(text, "<conversion failed>");
    sprintf(directory_diagnostic, "storage native mkdir failed error=%lu after=%lu attrError=%lu chars=%u path=%.800s",
        error, attributes, attribute_error, (unsigned int)wcslen(path), text);
    return 0;
}
static int storage_fixture_open(void* user, const char* key, PRuntimeStorageLocation* out)
{
    StorageFixture* f;
    PRuntimeDirectoryHandle alias;
    WCHAR part[17];
    WCHAR path[MAX_PATH];
    unsigned int i;
    int known;
    f = (StorageFixture*)user;
    if (strlen(key) != 30 || memcmp(key, "v1/dev/", 7) || strcmp(key + 23, "/app.db")) return PRUNTIME_IO;
    for (i = 0; i < 16; i++) {
        if (!strchr("0123456789abcdef", key[7 + i])) return PRUNTIME_IO;
        part[i] = key[7 + i];
    }
    part[16] = 0;
    wsprintfW(f->data, L"%s\\data", f->directory->outer); if (!storage_fixture_directory(f->data)) return PRUNTIME_IO;
    wsprintfW(path, L"%s\\v1", f->data); if (!storage_fixture_directory(path)) return PRUNTIME_IO;
    wsprintfW(path, L"%s\\v1\\dev", f->data); if (!storage_fixture_directory(path)) return PRUNTIME_IO;
    wsprintfW(path, L"%s\\v1\\dev\\%s", f->data, part); if (!storage_fixture_directory(path)) return PRUNTIME_IO;
    wsprintfW(f->path, L"%s\\app.db", path);
    known = 0;
    for (i = 0; i < f->count; i++) if (!strcmp(key, f->keys[i])) known = 1;
    if (!known) {
        if (f->count == 8) return PRUNTIME_LIMIT;
        strcpy(f->keys[f->count++], key);
    }
    out->token = f; f->opens++;
    out->path_bytes = (unsigned int)WideCharToMultiByte(CP_UTF8, 0, f->override_path[0] ? f->override_path : f->path,
        -1, out->path, sizeof(out->path), NULL, NULL);
    if (!out->path_bytes) return PRUNTIME_IO;
    out->path_bytes--;
    alias = f->reentry_target;
    f->reentry_ok = PRuntime_DirectoryClose(&alias) == PRUNTIME_STATE && alias == f->reentry_target;
    return f->fail ? PRUNTIME_IO : PRUNTIME_OK;
}
static void storage_fixture_close(void* user, void* token)
{ StorageFixture* f; f = (StorageFixture*)user; if (token == f) f->closes++; }
static int storage_fixture_cancel(void* user)
{
    StorageFixture* f;
    PRuntimeStorageHandle alias;
    PRuntimeStorageInfo info;
    f = (StorageFixture*)user;
    /* Test-only bounded rendezvous, not a product callback/scheduler policy. */
    if (f->hold && ++f->polls == 8) {
        SetEvent(f->inside);
        if (WaitForSingleObject(f->resume, 5000) != WAIT_OBJECT_0) InterlockedExchange((LONG*)&f->cancelled, 1);
    }
    if (f->live) {
        alias = f->live; info.size = sizeof(info); info.version = 1;
        f->cancel_reentry_ok = PRuntime_StorageGetInfo(f->live, &info) == PRUNTIME_STATE &&
            PRuntime_StorageClose(&alias) == PRUNTIME_STATE && alias == f->live;
    }
    return InterlockedCompareExchange((LONG*)&f->cancelled, 0, 0) != 0;
}
static void storage_fixture_cleanup(StorageFixture* f)
{
    WCHAR part[17];
    WCHAR path[MAX_PATH];
    unsigned int i;
    unsigned int j;
    for (i = 0; i < f->count; i++) {
        for (j = 0; j < 16; j++) part[j] = f->keys[i][7 + j]; part[16] = 0;
        wsprintfW(path, L"%s\\v1\\dev\\%s\\app.db-journal", f->data, part); DeleteFileW(path);
        wsprintfW(path, L"%s\\v1\\dev\\%s\\app.db", f->data, part); DeleteFileW(path);
        wsprintfW(path, L"%s\\v1\\dev\\%s", f->data, part); RemoveDirectoryW(path);
    }
    if (f->count) {
        wsprintfW(path, L"%s\\v1\\dev", f->data); RemoveDirectoryW(path);
        wsprintfW(path, L"%s\\v1", f->data); RemoveDirectoryW(path); RemoveDirectoryW(f->data);
    }
}
static DWORD WINAPI storage_wrong_thread(void* user)
{
    PRuntimeStorageHandle alias;
    PRuntimeStorageInfo info;
    PRuntimeStorageError error;
    alias = (PRuntimeStorageHandle)user; info.size = sizeof(info); info.version = 1; error.size = sizeof(error); error.version = 1;
    return PRuntime_StorageGetInfo(alias, &info) == PRUNTIME_STATE && PRuntime_StorageGetError(alias, &error) == PRUNTIME_STATE &&
        PRuntime_StorageClose(&alias) == PRUNTIME_STATE && alias == (PRuntimeStorageHandle)user ? 0 : 3;
}
BOOL test1368_runtime_storage_contract(void)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"org.demo.storage\",\"name\":\"Storage\","
        "\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],"
        "\"migrations\":[{\"version\":1,\"file\":\"m01.sql\"},{\"version\":2,\"file\":\"m02.sql\"}]}";
    static const char sql1[] = "CREATE TABLE notes(id INTEGER PRIMARY KEY,body TEXT);INSERT INTO notes VALUES(1,'first');";
    static const char sql2[] = "INSERT INTO notes VALUES(2,'second');";
    DirectoryFixture f;
    DirectoryFixture other;
    StorageFixture sf;
    PRuntimeDirectoryProvider provider;
    PRuntimeDirectoryProvider provider2;
    PRuntimeStorageProvider sp;
    PRuntimeStorageOptions options;
    PRuntimeStorageHandle storage;
    PRuntimeStorageHandle second;
    PRuntimeStorageInfo info;
    PRuntimeStorageInfo before;
    PRuntimeStorageError error;
    PRuntimeDirectoryHandle alias;
    PDbHandle db;
    PDbStmtHandle stmt;
    WCHAR first_path[MAX_PATH];
    char first_key[128];
    char path[1024];
    HANDLE thread;
    DWORD code;
    int outer;
    int root;
    int outer2;
    int root2;
    int version;
    int admission_rc;
    int reads;
    BOOL ok;
    storage = second = NULL; db = NULL; stmt = NULL; thread = NULL; ok = FALSE;
    outer = root = outer2 = root2 = 0; memset(&sf, 0, sizeof(sf)); memset(&other, 0, sizeof(other));
    if (!directory_fixture_init(&f, GetCurrentProcessId() ^ 0x10000000UL)) return FALSE;
    sf.directory = &f; directory_fixture_provider(&f, &provider);
    outer = CreateDirectoryW(f.outer, NULL) != 0; if (outer) root = CreateDirectoryW(f.root, NULL) != 0;
    RUNTIME_VERIFY(root && directory_fixture_write(&f, L"manifest.json", manifest, sizeof(manifest) - 1) &&
        directory_fixture_write(&f, L"m01.sql", sql1, sizeof(sql1) - 1) && directory_fixture_write(&f, L"m02.sql", sql2, sizeof(sql2) - 1), "storage private files");
    RUNTIME_VERIFY(PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0, "storage directory open");
    sf.reentry_target = f.instance;
    memset(&sp, 0, sizeof(sp)); sp.size = sizeof(sp); sp.version = 1; sp.user_data = &sf;
    sp.open_location = storage_fixture_open; sp.close_location = storage_fixture_close;
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1; options.vm_budget = 1000000;
    options.cancelled = storage_fixture_cancel; options.user_data = &sf;
    error.size = sizeof(error); error.version = 1;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == PRUNTIME_DENIED && !storage && !sf.opens,
        "storage explicit native grant before I/O"); options.granted_permissions = PRUNTIME_PERMISSION_SQL;
    sf.cancelled = 1;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == PRUNTIME_CANCELLED && !storage && !sf.opens,
        "storage pre-cancel before I/O"); sf.cancelled = 0; sf.fail = 1;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == PRUNTIME_IO && !storage && sf.opens == sf.closes,
        "storage partial native acquisition release"); sf.fail = 0;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == 0 && storage && sf.reentry_ok,
        "Runtime identity stamp, source admission and both migrations");
    info.size = sizeof(info); info.version = 1;
    RUNTIME_VERIFY(PRuntime_StorageGetInfo(storage, &info) == 0 && info.schema_version == 2 && !strcmp(info.id, "org.demo.storage") &&
        !strncmp(info.key, "v1/dev/", 7), "storage copied ready info");
    strcpy(first_key, info.key); wcscpy(first_path, sf.path); alias = f.instance;
    RUNTIME_VERIFY(PRuntime_DirectoryClose(&alias) == PRUNTIME_STATE && alias == f.instance &&
        PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &second, &error) == PRUNTIME_STATE && !second, "parent retention and one storage child");
    thread = CreateThread(NULL, 0, storage_wrong_thread, storage, 0, NULL);
    RUNTIME_VERIFY(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &code) && !code,
        "storage owner thread guard"); CloseHandle(thread); thread = NULL;
    before = info; info.version = 2; before = info;
    RUNTIME_VERIFY(PRuntime_StorageGetInfo(storage, &info) == PRUNTIME_ARGUMENT && !memcmp(&info, &before, sizeof(info)), "storage info canary"); info.version = 1;
    RUNTIME_VERIFY(PRuntime_StorageClose(&storage) == 0 && !storage && PRuntime_StorageClose(&storage) == 0 && sf.opens == sf.closes, "storage ordered close");
    reads = f.reads;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == 0 && f.reads == reads &&
        PRuntime_StorageGetInfo(storage, &info) == 0 && info.schema_version == 2 && !strcmp(first_key, info.key), "cold DB reopen, no repeated migration reads");
    PRuntime_StorageClose(&storage);
    RUNTIME_VERIFY(WideCharToMultiByte(CP_UTF8, 0, first_path, -1, path, sizeof(path), NULL, NULL) &&
        PDb_OpenUtf8(path, PDB_OPEN_LOCAL_FULL_SQL, &db) == 0 && PDb_GetMigrationVersion(db, &version) == 0 && version == 2 &&
        PDb_Prepare(db, "SELECT count(*) FROM notes", &stmt) == 0 && PDb_Step(stmt) == PDB_STEP_ROW && PDb_ColumnInt64(stmt, 0) == 2,
        "real file migrations persisted once"); PDb_Finalize(stmt); stmt = NULL; PDb_Close(db); db = NULL;
    RUNTIME_VERIFY(directory_fixture_init(&other, GetCurrentProcessId() ^ 0x30000000UL), "second development identity");
    directory_fixture_provider(&other, &provider2); outer2 = CreateDirectoryW(other.outer, NULL) != 0;
    if (outer2) root2 = CreateDirectoryW(other.root, NULL) != 0;
    RUNTIME_VERIFY(root2 && directory_fixture_write(&other, L"manifest.json", manifest, sizeof(manifest) - 1) &&
        directory_fixture_write(&other, L"m01.sql", sql1, sizeof(sql1) - 1) && directory_fixture_write(&other, L"m02.sql", sql2, sizeof(sql2) - 1) &&
        PRuntime_DevelopmentDirectoryOpen(other.canonical, (unsigned int)strlen(other.canonical), &provider2, &other.instance) == 0,
        "same ID second physical root");
    sf.reentry_target = other.instance;
    wcscpy(sf.override_path, first_path); reads = other.reads; second = (PRuntimeStorageHandle)1;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(other.instance, &options, &sp, &second, &error) == PRUNTIME_SCHEMA &&
        second == (PRuntimeStorageHandle)1 && error.db_result == PDB_SCHEMA_MISMATCH && reads == other.reads,
        "wrong mapped database/full identity collision refuses before SQL"); second = NULL; sf.override_path[0] = 0;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(other.instance, &options, &sp, &second, &error) == 0 &&
        PRuntime_StorageGetInfo(second, &info) == 0 && strcmp(first_key, info.key), "same ID different roots have distinct data");
    PRuntime_StorageClose(&second); PRuntime_DirectoryClose(&other.instance);
    RUNTIME_VERIFY(sf.opens == sf.closes && f.allocations == f.releases && other.allocations == other.releases, "all storage providers balanced");
    RUNTIME_VERIFY(PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db) == 0 &&
        PDb_BindLocalIdentity(db, "development:one", 15) == 0 && PDb_BindLocalIdentity(db, "development:one", 15) == 0 &&
        PDb_BindLocalIdentity(db, "installed:one", 13) == PDB_SCHEMA_MISMATCH && PDb_BindLocalIdentity(db, "development:two", 15) == PDB_SCHEMA_MISMATCH,
        "DB exact repeated binding and dev/installed isolation");
    RUNTIME_VERIFY(PDb_BindLocalIdentity(db, "A\0B", 3) == PDB_INVALID_ARGUMENT && PDb_BindLocalIdentity(db, "x", 2049) == PDB_LIMIT &&
        PDb_Begin(db) == 0 && PDb_BindLocalIdentity(db, "x", 1) == PDB_STATE && PDb_Rollback(db) == 0,
        "DB binding argument/transaction guards"); PDb_Close(db); db = NULL;
    RUNTIME_VERIFY(PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db) == 0 && PDb_Exec(db, "CREATE TABLE browser_data(x)") == 0 &&
        PDb_BindLocalIdentity(db, "development:one", 15) == PDB_SCHEMA_MISMATCH,
        "unclaimed nonempty browser/application DB cannot be adopted"); PDb_Close(db); db = NULL;
    RUNTIME_VERIFY(PDb_OpenUtf8(":memory:", PDB_OPEN_LOCAL_FULL_SQL, &db) == 0 &&
        PDb_Exec(db, "INSERT INTO __pdb_meta VALUES('local_identity',x'78')") == 0 &&
        PDb_BindLocalIdentity(db, "x", 1) == PDB_SCHEMA_MISMATCH, "corrupt BLOB identity never converted to valid TEXT");
    PDb_Close(db); db = NULL;
    PRuntime_DirectoryClose(&f.instance);
    RUNTIME_VERIFY(DeleteFileW(first_path) && directory_fixture_write(&f, L"m02.sql", "INSERT INTO notes VALUES(1,'bad');", 34) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0,
        "private fresh DB/later SQL constraint fixture"); sf.reentry_target = f.instance;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == PRUNTIME_DATABASE && !storage &&
        error.failed_schema_version == 2 && error.db_category == PDB_ERROR_CATEGORY_CONSTRAINT, "second migration fails without author-ready storage");
    RUNTIME_VERIFY(PDb_OpenUtf8(path, PDB_OPEN_LOCAL_FULL_SQL, &db) == 0 && PDb_GetMigrationVersion(db, &version) == 0 && version == 1 &&
        PDb_Prepare(db, "SELECT count(*) FROM notes", &stmt) == 0 && PDb_Step(stmt) == PDB_STEP_ROW && PDb_ColumnInt64(stmt, 0) == 1,
        "per-file atomicity preserves committed first migration and rolls back second");
    PDb_Finalize(stmt); stmt = NULL; PDb_Close(db); db = NULL; PRuntime_DirectoryClose(&f.instance);
    RUNTIME_VERIFY(directory_fixture_write(&f, L"m01.sql", "DROP TABLE notes;", 17) &&
        directory_fixture_write(&f, L"m02.sql", sql2, sizeof(sql2) - 1) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0, "resume source fixture");
    sf.reentry_target = f.instance;
    RUNTIME_VERIFY(PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error) == 0 &&
        PRuntime_StorageGetInfo(storage, &info) == 0 && info.schema_version == 2, "resume only pending migration, no first-file replay");
    PRuntime_StorageClose(&storage); PRuntime_DirectoryClose(&f.instance);
    RUNTIME_VERIFY(DeleteFileW(first_path) && directory_fixture_write(&f, L"m01.sql", sql1, sizeof(sql1) - 1) &&
        directory_fixture_write(&f, L"m02.sql", "\xc0", 1) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0,
        "invalid later file before migration writes"); sf.reentry_target = f.instance;
    admission_rc = PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error);
    sprintf(directory_diagnostic, "invalid source admission rc=%d storage=%d db=%d category=%d failedSchema=%u",
        admission_rc, storage != NULL, error.db_result, error.db_category, error.failed_schema_version);
    RUNTIME_VERIFY(admission_rc == PRUNTIME_INVALID && !storage, directory_diagnostic);
    RUNTIME_VERIFY(PDb_OpenUtf8(path, PDB_OPEN_LOCAL_FULL_SQL, &db) == 0, "invalid source DB metadata reopen");
    version = -1; admission_rc = PDb_GetMigrationVersion(db, &version);
    sprintf(directory_diagnostic, "invalid source schema stays zero rc=%d version=%d", admission_rc, version);
    RUNTIME_VERIFY(admission_rc == 0 && !version, directory_diagnostic);
    RUNTIME_VERIFY(PDb_Prepare(db, "SELECT * FROM notes", &stmt) != 0, "invalid later source must prevent first notes DDL");
    PDb_Close(db); db = NULL;
    ok = TRUE;
done:
    if (thread) { WaitForSingleObject(thread, 10000); CloseHandle(thread); }
    if (stmt) PDb_Finalize(stmt); if (db) PDb_Close(db);
    if (second != (PRuntimeStorageHandle)1) PRuntime_StorageClose(&second);
    PRuntime_StorageClose(&storage); PRuntime_DirectoryClose(&f.instance); PRuntime_DirectoryClose(&other.instance);
    storage_fixture_cleanup(&sf); directory_fixture_cleanup(&other, root2, outer2); directory_fixture_cleanup(&f, root, outer); return ok;
}

static int sql_fixture_call(PRuntimeStorageHandle storage, const char* request, char* output, unsigned int* written, int expected)
{
    int rc;
    memset(output, 'z', 4097); *written = 91;
    rc = PRuntime_StorageExecuteJson(storage, request, (unsigned int)strlen(request), output, 4097, written);
    return rc == expected && (rc == PRUNTIME_OK || (*written == 91 && output[0] == 'z' && output[4096] == 'z'));
}
BOOL test1369_runtime_sql_contract(void)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"org.demo.sql\",\"name\":\"SQL\",\"version\":1,"
        "\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],"
        "\"migrations\":[{\"version\":1,\"file\":\"m01.sql\"}]}";
    static const char schema[] = "CREATE TABLE notes(id INTEGER PRIMARY KEY,body TEXT);INSERT INTO notes VALUES(1,'base');";
    static const char* const denied[] = {
        "{\"version\":1,\"method\":\"execute\",\"sql\":\"DROP TABLE notes\",\"params\":[]}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT * FROM __pdb_meta\",\"params\":[]}",
        "{\"version\":1,\"method\":\"execute\",\"sql\":\"ATTACH ':memory:' AS other\",\"params\":[]}",
        "{\"version\":1,\"method\":\"execute\",\"sql\":\"BEGIN\",\"params\":[]}",
        "{\"version\":1,\"method\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(2,'x') RETURNING id\",\"params\":[]}"
    };
    static const char* const invalid[] = {
        "{}", "{\"version\":1,\"version\":1}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT 1\",\"params\":[],\"path\":\"browser.db\"}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT ?\",\"params\":[{\"type\":\"int64\",\"value\":9007199254740993}]}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT ?\",\"params\":[{\"type\":\"int64\",\"value\":\"9223372036854775808\"}]}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT ?\",\"params\":[{\"type\":\"int64\",\"value\":\"01\"}]}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT ?\",\"params\":[{\"type\":\"int64\",\"value\":\"-0\"}]}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT ?\",\"params\":[{\"type\":\"null\",\"value\":null}]}",
        "{\"version\":1,\"method\":\"transaction\",\"statements\":[]}",
        "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT 1\",\"params\":[{\"type\":\"text\",\"value\":\"a\\u0000b\"}]}"
    };
    DirectoryFixture f;
    StorageFixture sf;
    PRuntimeDirectoryProvider provider;
    PRuntimeStorageProvider sp;
    PRuntimeStorageOptions options;
    PRuntimeStorageHandle storage;
    PRuntimeStorageError error;
    PJsonScalar scalar;
    HANDLE json;
    HANDLE rows;
    HANDLE values;
    unsigned int written;
    unsigned int i;
    char output[4097];
    char request[4097];
    char encoded[128];
    int open_result;
    int outer;
    int root;
    BOOL ok;
    storage = NULL; json = NULL; outer = root = 0; ok = FALSE; memset(&sf, 0, sizeof(sf));
    if (!directory_fixture_init(&f, GetCurrentProcessId() ^ 0x08000000UL)) return FALSE;
    sf.directory = &f; directory_fixture_provider(&f, &provider);
    outer = CreateDirectoryW(f.outer, NULL) != 0; if (outer) root = CreateDirectoryW(f.root, NULL) != 0;
    RUNTIME_VERIFY(root && directory_fixture_write(&f, L"manifest.json", manifest, sizeof(manifest) - 1) &&
        directory_fixture_write(&f, L"m01.sql", schema, sizeof(schema) - 1) &&
        PRuntime_DevelopmentDirectoryOpen(f.canonical, (unsigned int)strlen(f.canonical), &provider, &f.instance) == 0, "SQL protocol private root");
    sf.reentry_target = f.instance;
    memset(&sp, 0, sizeof(sp)); sp.size = sizeof(sp); sp.version = 1; sp.user_data = &sf; sp.open_location = storage_fixture_open; sp.close_location = storage_fixture_close;
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1; options.granted_permissions = 1;
    options.vm_budget = 1000000; options.cancelled = storage_fixture_cancel; options.user_data = &sf;
    error.size = sizeof(error); error.version = 1;
    open_result = PRuntime_DevelopmentStorageOpen(f.instance, &options, &sp, &storage, &error);
    if (open_result != PRUNTIME_IO) sprintf(directory_diagnostic, "SQL ready storage rc=%d db=%d category=%d sqlite=%d/%d schema=%lu dataAttr=%lu fileAttr=%lu message=%.250s",
        open_result, error.db_result, error.db_category, error.sqlite_code, error.sqlite_extended_code, error.failed_schema_version,
        GetFileAttributesW(sf.data), GetFileAttributesW(sf.path), error.message);
    RUNTIME_VERIFY(open_result == 0, directory_diagnostic); sf.live = storage;
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT ?,?,?,?,? FROM notes WHERE id=1\",\"params\":["
        "{\"type\":\"int64\",\"value\":\"9223372036854775807\"},{\"type\":\"int64\",\"value\":\"-9223372036854775808\"},"
        "{\"type\":\"real\",\"value\":1.25},{\"type\":\"null\"},{\"type\":\"text\",\"value\":\"\xe4\xb8\xad\\n\\\"\\\\\"}]}", output, &written, 0) &&
        sf.cancel_reentry_ok, "typed int64 limits, real/null/UTF8/control escaping and callback guard");
    json = PJson_Parse(output); rows = PJson_GetObject(json, "rows"); values = PJson_GetObject(PJson_GetArrayItem(rows, 0), "values");
    RUNTIME_VERIFY(json && PJson_GetInt(json, "rowCount") == 1 && PJson_GetArraySize(values) == 5 &&
        !strcmp(PJson_GetString(PJson_GetArrayItem(values, 0), "value"), "9223372036854775807") &&
        !strcmp(PJson_GetString(PJson_GetArrayItem(values, 1), "value"), "-9223372036854775808") &&
        !strcmp(PJson_GetString(PJson_GetArrayItem(values, 4), "value"), "\xe4\xb8\xad\n\"\\"), "complete typed JSON round trip");
    PJson_Free(json); json = NULL;
    for (i = 0; i < sizeof(denied) / sizeof(denied[0]); i++) RUNTIME_VERIFY(sql_fixture_call(storage, denied[i], output, &written, PRUNTIME_DATABASE), "SQL authority rejects without output");
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) RUNTIME_VERIFY(sql_fixture_call(storage, invalid[i], output, &written, PRUNTIME_INVALID), "strict request/typed value admission");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":2,\"method\":\"query\",\"sql\":\"SELECT 1\",\"params\":[]}", output, &written, PRUNTIME_UNSUPPORTED), "future request version");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"transaction\",\"statements\":["
        "{\"kind\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(2,'provisional')\",\"params\":[]},"
        "{\"kind\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(1,'duplicate')\",\"params\":[]}]}", output, &written, PRUNTIME_DATABASE), "batch constraint rolls back provisional writes");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"transaction\",\"statements\":["
        "{\"kind\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(2,'provisional')\",\"params\":[]},"
        "{\"kind\":\"query\",\"sql\":\"SELECT replace(hex(zeroblob(1000)),'0',char(10)) FROM notes\",\"params\":[]}]}", output, &written, PRUNTIME_DATABASE),
        "unavailable function rejects whole batch");
    /* Escaped result >4096 despite <4096 raw UTF-8 text, after provisional write. */
    strcpy(request, "{\"version\":1,\"method\":\"transaction\",\"statements\":[{\"kind\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(2,'provisional')\",\"params\":[]},"
        "{\"kind\":\"query\",\"sql\":\"WITH RECURSIVE t(n,s) AS (VALUES(1,char(1)) UNION ALL SELECT n+1,s||char(1) FROM t WHERE n<800) SELECT s FROM t WHERE n=800\",\"params\":[]}]}");
    RUNTIME_VERIFY(sql_fixture_call(storage, request, output, &written, PRUNTIME_LIMIT), "encoded JSON overflow before commit rolls back batch");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"query\",\"sql\":\"WITH RECURSIVE t(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM t WHERE n<17) SELECT n FROM t\",\"params\":[]}",
        output, &written, PRUNTIME_LIMIT), "17th row fails instead of truncating result");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"query\",\"sql\":\"WITH RECURSIVE t(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM t) SELECT sum(n) FROM t\",\"params\":[]}",
        output, &written, PRUNTIME_LIMIT), "shared VM budget stops long SQL");
    error.size = sizeof(error); error.version = 1;
    RUNTIME_VERIFY(PRuntime_StorageGetError(storage, &error) == 0 && error.result == PRUNTIME_LIMIT, "sticky protocol result error");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT count(*) FROM notes\",\"params\":[]}", output, &written, 0) &&
        strstr(output, "\"value\":\"1\"") && PRuntime_StorageGetError(storage, &error) == 0 && error.result == PRUNTIME_LIMIT, "rollback count and success preserves prior error");
    sf.cancelled = 1;
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"execute\",\"sql\":\"DELETE FROM notes\",\"params\":[]}", output, &written, PRUNTIME_CANCELLED), "request cooperative cancellation preserves data"); sf.cancelled = 0;
    strcpy(request, "{\"version\":1,\"method\":\"execute\",\"sql\":\"DELETE FROM notes\",\"params\":[]}");
    memset(output, 'z', sizeof(output)); written = 91;
    RUNTIME_VERIFY(PRuntime_StorageExecuteJson(storage, request, (unsigned int)strlen(request), output, 4096, &written) == PRUNTIME_LIMIT &&
        written == 91 && output[0] == 'z', "small output fails before any write, no size-probe execution");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT char(0) FROM notes\",\"params\":[]}", output, &written, PRUNTIME_UNSUPPORTED), "embedded NUL result cannot truncate JSON");
    RUNTIME_VERIFY(sql_fixture_call(storage, "{\"version\":1,\"method\":\"transaction\",\"statements\":["
        "{\"kind\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(2,'committed')\",\"params\":[]},"
        "{\"kind\":\"query\",\"sql\":\"SELECT body FROM notes ORDER BY id\",\"params\":[]}]}", output, &written, 0) &&
        strstr(output, "committed") && strstr(output, "\"statement\":1"), "batch success only after commit, query statement indices");
    memset(&scalar, 0, sizeof(scalar)); scalar.size = sizeof(scalar); scalar.version = 1; scalar.type = PJSON_TYPE_STRING;
    scalar.text = "a\n\"\\"; scalar.text_bytes = 4;
    RUNTIME_VERIFY(PJson_EncodeScalar(&scalar, NULL, 0, &written) == 0 && written == 9 &&
        PJson_EncodeScalar(&scalar, encoded, sizeof(encoded), &written) == 0 && !strcmp(encoded, "\"a\\n\\\"\\\\\""), "JSON public scalar encode/probe");
    memset(encoded, 'z', sizeof(encoded)); written = 91;
    RUNTIME_VERIFY(PJson_EncodeScalar(&scalar, encoded, 2, &written) == PJSON_ENCODE_LIMIT && written == 91 && encoded[0] == 'z', "JSON scalar capacity canary");
    scalar.text = "A\0B"; scalar.text_bytes = 3;
    RUNTIME_VERIFY(PJson_EncodeScalar(&scalar, encoded, sizeof(encoded), &written) == PJSON_ENCODE_INVALID && written == 91, "JSON scalar NUL fails closed");
    ok = TRUE;
done:
    PJson_Free(json); sf.live = NULL; PRuntime_StorageClose(&storage); PRuntime_DirectoryClose(&f.instance);
    storage_fixture_cleanup(&sf); directory_fixture_cleanup(&f, root, outer); return ok;
}

typedef struct QueueFixture {
    DirectoryFixture directory;
    StorageFixture native;
    PRuntimeServiceQueueHandle queue;
    PRuntimeServiceQueueOptions queue_options;
    PRuntimeStorageHandle storage;
    HANDLE ready;
    HANDLE work;
    HANDLE done;
    HANDLE thread;
    HANDLE session;
    volatile LONG action;
    int result;
    unsigned int processed;
    PBrowserServiceToken last_token;
    int accepted;
    int refused;
    int cancelled;
} QueueFixture;
static DWORD WINAPI queue_fixture_worker(void* user)
{
    QueueFixture* f;
    PRuntimeDirectoryProvider directory_provider;
    PRuntimeStorageProvider provider;
    PRuntimeStorageOptions options;
    PRuntimeStorageError error;
    PRuntimeStorageHandle alias;
    PRuntimeServiceQueueHandle extra[4];
    PRuntimeServiceQueueOptions extra_options;
    char buffer[4097];
    unsigned int written;
    LONG action;
    unsigned int i;
    f = (QueueFixture*)user;
    directory_fixture_provider(&f->directory, &directory_provider);
    f->result = PRuntime_DevelopmentDirectoryOpen(f->directory.canonical, (unsigned int)strlen(f->directory.canonical), &directory_provider, &f->directory.instance);
    if (f->result == 0) {
        f->native.reentry_target = f->directory.instance;
        memset(&provider, 0, sizeof(provider)); provider.size = sizeof(provider); provider.version = 1;
        provider.user_data = &f->native; provider.open_location = storage_fixture_open; provider.close_location = storage_fixture_close;
        memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
        options.granted_permissions = 1; options.vm_budget = 1000000;
        options.cancelled = storage_fixture_cancel; options.user_data = &f->native;
        error.size = sizeof(error); error.version = 1;
        f->result = PRuntime_DevelopmentStorageOpen(f->directory.instance, &options, &provider, &f->storage, &error);
        if (f->result) sprintf(directory_diagnostic, "queue storage rc=%d db=%d category=%d sqlite=%d/%d schema=%lu message=%.250s",
            f->result, error.db_result, error.db_category, error.sqlite_code, error.sqlite_extended_code, error.failed_schema_version, error.message);
    }
    if (f->result == 0) f->result = PRuntime_ServiceQueueCreate(f->storage, &f->queue_options, &f->queue);
    f->native.live = f->storage; SetEvent(f->ready);
    if (f->result != 0) goto done;
    for (;;) {
        if (WaitForSingleObject(f->work, 30000) != WAIT_OBJECT_0) { f->result = PRUNTIME_STATE; break; }
        action = InterlockedExchange((LONG*)&f->action, 0);
        if (action == 3) break;
        if (action == 1) f->result = PRuntime_ServiceQueueProcessOne(f->queue, &f->processed);
        else if (action == 2) {
            f->result = PRuntime_ServiceQueueClose(&f->queue);
            if (f->result == 0) f->result = PRuntime_ServiceQueueCreate(f->storage, &f->queue_options, &f->queue);
        } else if (action == 4) {
            alias = f->storage;
            f->result = PRuntime_StorageClose(&alias) == PRUNTIME_STATE && alias == f->storage &&
                PRuntime_StorageExecuteJson(f->storage, "{}", 2, buffer, sizeof(buffer), &written) == PRUNTIME_STATE ? 0 : PRUNTIME_STATE;
        } else if (action == 5) {
            memset(extra, 0, sizeof(extra)); extra_options = f->queue_options; f->result = 0;
            if (PRuntime_ServiceQueueCreate(f->storage, &extra_options, &extra[3]) != PRUNTIME_STATE || extra[3]) f->result = PRUNTIME_STATE;
            for (i = 0; i < 3 && !f->result; i++) {
                extra_options.page_generation++;
                f->result = PRuntime_ServiceQueueCreate(f->storage, &extra_options, &extra[i]);
            }
            extra_options.page_generation++;
            if (!f->result && PRuntime_ServiceQueueCreate(f->storage, &extra_options, &extra[3]) != PRUNTIME_LIMIT) f->result = PRUNTIME_STATE;
            for (i = 0; i < 4; i++) if (extra[i]) { PRuntime_ServiceQueueRevoke(extra[i]); PRuntime_ServiceQueueClose(&extra[i]); }
        } else f->result = PRUNTIME_ARGUMENT;
        SetEvent(f->done);
    }
done:
    f->native.live = NULL;
    if (f->queue) { PRuntime_ServiceQueueRevoke(f->queue); PRuntime_ServiceQueueClose(&f->queue); }
    PRuntime_StorageClose(&f->storage); PRuntime_DirectoryClose(&f->directory.instance); SetEvent(f->done); return 0;
}
static int queue_fixture_action(QueueFixture* f, LONG action)
{
    ResetEvent(f->done); InterlockedExchange((LONG*)&f->action, action); SetEvent(f->work);
    return WaitForSingleObject(f->done, 10000) == WAIT_OBJECT_0;
}
static int queue_fixture_submit(void* user, const PBrowserServiceRequest* input)
{
    QueueFixture* f;
    PRuntimeServiceRequest request;
    int rc;
    f = (QueueFixture*)user;
    memset(&request, 0, sizeof(request)); request.size = sizeof(request); request.version = 1;
    request.tab_id = input->tab_id; request.page_generation = input->page_generation;
    request.bridge_id = input->token.bridge_id; request.request_id = input->token.request_id;
    request.method = input->method; request.json = input->params_json; request.json_bytes = input->params_bytes;
    rc = PRuntime_ServiceQueueSubmit(f->queue, &request);
    if (rc == 0) { f->accepted++; f->last_token = input->token; } else f->refused++;
    return rc;
}
static void queue_fixture_cancel(void* user, const PBrowserServiceToken* token)
{
    QueueFixture* f;
    f = (QueueFixture*)user; f->cancelled++;
    PRuntime_ServiceQueueCancel(f->queue, token->bridge_id, token->request_id);
}
static int queue_fixture_js(QueueFixture* f, const char* source)
{
    const char* result;
    if (PBrowser_ScriptSessionEvaluate(f->session, source, -1) != PSCRIPT_OK) return 0;
    result = PBrowser_ScriptSessionGetResult(f->session); return result && !strcmp(result, "true");
}
static void queue_fixture_request(QueueFixture* f, PRuntimeServiceRequest* request, const char* json, unsigned long id)
{
    memset(request, 0, sizeof(*request)); request->size = sizeof(*request); request->version = 1;
    request->tab_id = f->queue_options.tab_id; request->page_generation = f->queue_options.page_generation;
    request->bridge_id = 71; request->request_id = id; request->method = "storage.sql";
    request->json = json; request->json_bytes = (unsigned int)strlen(json);
}
BOOL test1370_runtime_queue_contract(void)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"org.demo.queue\",\"name\":\"Queue\",\"version\":1,"
        "\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],"
        "\"migrations\":[{\"version\":1,\"file\":\"m01.sql\"}]}";
    static const char schema[] = "CREATE TABLE notes(id INTEGER PRIMARY KEY,body TEXT);INSERT INTO notes VALUES(1,'base');";
    static const char query[] = "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT count(*) FROM notes\",\"params\":[]}";
    static const char* methods[] = { "storage.sql" };
    QueueFixture* f;
    PRuntimeServiceRequest request;
    PRuntimeServiceResult* result;
    PRuntimeServiceResult* before;
    PRuntimeServiceQueueInfo info;
    PRuntimeServiceQueueHandle alias;
    PBrowserServiceOptions options;
    unsigned long delivered;
    unsigned int i;
    unsigned int processed;
    DWORD outer_before;
    DWORD root_before;
    DWORD create_error;
    int root;
    int outer;
    BOOL ok;
    f = (QueueFixture*)calloc(1, sizeof(*f)); result = (PRuntimeServiceResult*)malloc(sizeof(*result)); before = (PRuntimeServiceResult*)malloc(sizeof(*before));
    if (!f || !result || !before) { free(f); free(result); free(before); return FALSE; }
    outer = root = 0; ok = FALSE;
    RUNTIME_VERIFY(directory_fixture_init(&f->directory, GetCurrentProcessId() ^ 0x04000000UL), "queue private root identity");
    f->native.directory = &f->directory;
    outer_before = GetFileAttributesW(f->directory.outer); root_before = GetFileAttributesW(f->directory.root);
    SetLastError(0); outer = CreateDirectoryW(f->directory.outer, NULL) != 0; create_error = GetLastError();
    if (outer) { SetLastError(0); root = CreateDirectoryW(f->directory.root, NULL) != 0; create_error = GetLastError(); }
    if (!root) {
        sprintf(directory_diagnostic, "queue root: outer=%d root=%d error=%lu before=%lu/%lu after=%lu/%lu path=%.700s",
            outer, root, create_error, outer_before, root_before, GetFileAttributesW(f->directory.outer), GetFileAttributesW(f->directory.root), f->directory.canonical);
        directory_error = directory_diagnostic; goto done;
    }
    if (!directory_fixture_write(&f->directory, L"manifest.json", manifest, sizeof(manifest) - 1) ||
            !directory_fixture_write(&f->directory, L"m01.sql", schema, sizeof(schema) - 1)) {
        directory_error = directory_diagnostic; goto done;
    }
    f->ready = CreateEvent(NULL, TRUE, FALSE, NULL); f->done = CreateEvent(NULL, TRUE, FALSE, NULL); f->work = CreateEvent(NULL, FALSE, FALSE, NULL);
    f->native.inside = CreateEvent(NULL, TRUE, FALSE, NULL); f->native.resume = CreateEvent(NULL, TRUE, FALSE, NULL);
    RUNTIME_VERIFY(f->ready && f->done && f->work && f->native.inside && f->native.resume, "queue native worker events");
    f->queue_options.size = sizeof(f->queue_options); f->queue_options.version = 1; f->queue_options.tab_id = 5;
    f->queue_options.page_generation = 17; f->queue_options.delivery_thread_id = GetCurrentThreadId();
    f->thread = CreateThread(NULL, 0, queue_fixture_worker, f, 0, NULL);
    RUNTIME_VERIFY(f->thread && WaitForSingleObject(f->ready, 10000) == WAIT_OBJECT_0, "queue worker startup rendezvous");
    if (f->result || !f->queue) { directory_error = directory_diagnostic; goto done; }
    processed = 91; alias = f->queue;
    RUNTIME_VERIFY(PRuntime_ServiceQueueProcessOne(f->queue, &processed) == PRUNTIME_STATE && processed == 91 &&
        PRuntime_ServiceQueueClose(&alias) == PRUNTIME_STATE && alias == f->queue && queue_fixture_action(f, 4) && f->result == 0,
        "UI cannot process/close worker queue; storage retained, direct SQL bypass refused");
    RUNTIME_VERIFY(queue_fixture_action(f, 5) && f->result == 0, "four independent old/candidate queues, fifth fails closed");
    queue_fixture_request(f, &request, query, 1); request.page_generation++;
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == PRUNTIME_STATE, "stale page cannot enter queue"); request.page_generation--;
    request.method = "storage.file";
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == PRUNTIME_DENIED, "service allowlist before copy");
    f->session = PBrowser_ScriptSessionCreate(1000); RUNTIME_VERIFY(f->session, "UI Browser service consumer");
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
    options.tab_id = 5; options.page_generation = 17; options.method_count = 1; options.methods = methods;
    options.submit = queue_fixture_submit; options.cancel = queue_fixture_cancel; options.pw = f;
    RUNTIME_VERIFY(PBrowser_ScriptSessionRegisterServices(f->session, &options) == PBROWSER_OK && queue_fixture_js(f,
        "var replies=[],p={version:1,method:'execute',sql:\"INSERT INTO notes VALUES(2,'copied')\",params:[]};"
        "PositronServices.request('storage.sql',p,function(ok,v){replies.push([ok,v]);});p.sql='DROP TABLE notes';replies.length===0"),
        "Browser submit copies only, no synchronous result or source alias");
    RUNTIME_VERIFY(queue_fixture_action(f, 1) && f->result == 0 && f->processed == 1, "worker processes accepted SQL once");
    memset(result, 'z', sizeof(*result)); result->size = sizeof(*result); result->version = 1;
    RUNTIME_VERIFY(PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && result->result == 0 && result->tab_id == 5 && result->page_generation == 17 &&
        result->bridge_id == f->last_token.bridge_id && result->request_id == f->last_token.request_id &&
        PBrowser_ScriptSessionCompleteService(f->session, &f->last_token, 1, result->json, result->json_bytes) == PBROWSER_OK &&
        queue_fixture_js(f, "replies.length===0"), "copied committed result, Complete does not call JS");
    RUNTIME_VERIFY(PBrowser_ScriptSessionPumpServices(f->session, 1, &delivered) == PBROWSER_OK && delivered == 1 &&
        queue_fixture_js(f, "replies.length===1&&replies[0][0]&&replies[0][1].changes.value==='1'"), "UI owner pump delivers typed result once");
    *before = *result;
    RUNTIME_VERIFY(PRuntime_ServiceQueueTakeResult(f->queue, result) == PRUNTIME_NOT_FOUND && !memcmp(result, before, sizeof(*result)), "result consume-once canary");
    request.method = "storage.sql"; request.bridge_id = f->last_token.bridge_id; request.request_id = f->last_token.request_id;
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == PRUNTIME_STATE, "consumed token replay refused");
    RUNTIME_VERIFY(queue_fixture_js(f, "var refused=false;for(var i=0;i<8;i++)PositronServices.request('storage.sql',"
        "{version:1,method:'query',sql:'SELECT id FROM notes ORDER BY id',params:[]},function(ok,v){replies.push([ok,v]);});"
        "try{PositronServices.request('storage.sql',{version:1,method:'query',sql:'SELECT id FROM notes',params:[]},function(){});}catch(e){refused=true;}refused"),
        "Runtime eight pending limit below Browser sixteen");
    info.size = sizeof(info); info.version = 1;
    RUNTIME_VERIFY(PRuntime_ServiceQueueGetInfo(f->queue, &info) == 0 && info.waiting == 8 && !info.running && !info.completed, "fixed queue accounting");
    for (i = 0; i < 8; i++) RUNTIME_VERIFY(queue_fixture_action(f, 1) && f->result == 0, "FIFO worker requests");
    RUNTIME_VERIFY(PRuntime_ServiceQueueGetInfo(f->queue, &info) == 0 && info.completed == 8 && queue_fixture_js(f, "replies.length===1"), "unclaimed results still use pending budget");
    for (i = 0; i < 8; i++) {
        PBrowserServiceToken token;
        RUNTIME_VERIFY(PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && result->result == 0, "result copy");
        token.bridge_id = result->bridge_id; token.request_id = result->request_id;
        RUNTIME_VERIFY(PBrowser_ScriptSessionCompleteService(f->session, &token, 1, result->json, result->json_bytes) == PBROWSER_OK, "marshal only on UI");
    }
    RUNTIME_VERIFY(PBrowser_ScriptSessionPumpServices(f->session, 8, &delivered) == PBROWSER_OK && delivered == 8 &&
        queue_fixture_js(f, "replies.length===9&&replies[8][1].rowCount===2"), "all eight callbacks in owner pump");
    RUNTIME_VERIFY(queue_fixture_js(f, "PositronServices.request('storage.sql',{version:1,method:'execute',sql:'DELETE FROM notes',params:[]},function(){replies.push('late');});true") &&
        PBrowser_ScriptSessionRevokeServices(f->session) == PBROWSER_OK && f->cancelled == 1 &&
        PRuntime_ServiceQueueRevoke(f->queue) == 0 && PRuntime_ServiceQueueRevoke(f->queue) == 0 &&
        queue_fixture_action(f, 1) && f->result == PRUNTIME_STATE, "Browser revoke cancels queued mutation without executing");
    PBrowser_ScriptSessionDestroy(f->session); f->session = NULL;
    f->queue_options.page_generation = 18; RUNTIME_VERIFY(queue_fixture_action(f, 2) && f->result == 0, "new page gets fresh queue, old grant never reenabled");
    queue_fixture_request(f, &request, "{\"version\":1,\"method\":\"transaction\",\"statements\":["
        "{\"kind\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(7,'provisional')\",\"params\":[]},"
        "{\"kind\":\"query\",\"sql\":\"WITH RECURSIVE t(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM t) SELECT sum(n) FROM t\",\"params\":[]}]}", 1);
    f->native.hold = 1; f->native.polls = 0;
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0, "in-flight race request");
    ResetEvent(f->done); InterlockedExchange((LONG*)&f->action, 1); SetEvent(f->work);
    RUNTIME_VERIFY(WaitForSingleObject(f->native.inside, 5000) == WAIT_OBJECT_0 &&
        PRuntime_ServiceQueueGetInfo(f->queue, &info) == 0 && info.running == 1 && PRuntime_ServiceQueueRevoke(f->queue) == 0,
        "UI revoke while native DB call is in flight");
    SetEvent(f->native.resume);
    RUNTIME_VERIFY(WaitForSingleObject(f->done, 10000) == WAIT_OBJECT_0 && f->result == 0 &&
        PRuntime_ServiceQueueGetInfo(f->queue, &info) == 0 && info.revoked && !info.running && !info.completed,
        "cancelled call returns before queue teardown, result dropped"); f->native.hold = 0;
    *before = *result;
    RUNTIME_VERIFY(PRuntime_ServiceQueueTakeResult(f->queue, result) == PRUNTIME_STATE && !memcmp(result, before, sizeof(*result)), "revoked page cannot take late result");
    f->queue_options.page_generation = 19; RUNTIME_VERIFY(queue_fixture_action(f, 2) && f->result == 0, "post-cancel ready storage retained");
    queue_fixture_request(f, &request, query, 1);
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0 && queue_fixture_action(f, 1) && f->result == 0 &&
        PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && result->result == 0 && strstr(result->json, "\"value\":\"2\""),
        "queued delete not run and in-flight provisional insert rolled back");
    queue_fixture_request(f, &request, "{\"version\":1,\"method\":\"execute\",\"sql\":\"INSERT INTO notes VALUES(3,'committed')\",\"params\":[]}", 2);
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0 && queue_fixture_action(f, 1) && f->result == 0 &&
        PRuntime_ServiceQueueRevoke(f->queue) == 0, "revoke after commit drops result, never reexecutes write");
    f->queue_options.page_generation = 20; RUNTIME_VERIFY(queue_fixture_action(f, 2) && f->result == 0, "post-commit generation");
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == PRUNTIME_STATE, "old committed request generation cannot replay");
    queue_fixture_request(f, &request, query, 1);
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0 && queue_fixture_action(f, 1) && f->result == 0 &&
        PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && strstr(result->json, "\"value\":\"3\""), "committed write preserved exactly once");
    queue_fixture_request(f, &request, "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT * FROM __pdb_meta\",\"params\":[]}", 2);
    RUNTIME_VERIFY(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0 && queue_fixture_action(f, 1) && f->result == 0 &&
        PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && result->result == PRUNTIME_DATABASE && strstr(result->json, "\"error\"") &&
        !strstr(result->json, "__pdb") && !strstr(result->json, "Storage"), "sanitized failure has no SQL or physical path");
    RUNTIME_VERIFY(PRuntime_ServiceQueueRevoke(f->queue) == 0 && queue_fixture_action(f, 3) &&
        WaitForSingleObject(f->thread, 10000) == WAIT_OBJECT_0 && !f->queue && !f->storage && !f->directory.instance &&
        f->native.opens == f->native.closes && f->directory.allocations == f->directory.releases, "worker shutdown releases queue then DB then root");
    ok = TRUE;
done:
    if (f->native.resume) SetEvent(f->native.resume);
    if (f->session) { PBrowser_ScriptSessionRevokeServices(f->session); PBrowser_ScriptSessionDestroy(f->session); }
    if (f->thread) {
        if (WaitForSingleObject(f->thread, 0) != WAIT_OBJECT_0) {
            queue_fixture_action(f, 3);
            if (WaitForSingleObject(f->thread, 10000) != WAIT_OBJECT_0) {
                directory_error = "queue worker did not exit; native state preserved until test process ends";
                free(before); free(result); return FALSE;
            }
        }
        CloseHandle(f->thread);
    }
    if (f->ready) CloseHandle(f->ready); if (f->done) CloseHandle(f->done); if (f->work) CloseHandle(f->work);
    if (f->native.inside) CloseHandle(f->native.inside); if (f->native.resume) CloseHandle(f->native.resume);
    storage_fixture_cleanup(&f->native); directory_fixture_cleanup(&f->directory, root, outer);
    free(before); free(result); free(f); return ok;
}

BOOL test1371_runtime_page_contract(void)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"org.demo_app.notes\",\"name\":\"Notes\",\"version\":1,"
        "\"runtimeApiVersion\":1,\"entry\":\"pages/index.html\",\"permissions\":[],\"migrations\":[]}";
    static const char html[] = "<!doctype html><html><head><link rel=stylesheet href='../assets/main.css?q=sheet#f'>"
        "<script src='../assets/main.js?x=1#author'></script></head><body><img src='../assets/icon.svg' width=20 height=20>"
        "<div id=tile></div><p id=result>initial</p></body></html>";
    static const char css[] = "@import url('theme.css?theme=1');body{margin:4px}p{color:#008000}";
    static const char theme[] = "#tile{display:block;width:20px;height:20px;background-image:url('icon.svg?background=1');background-size:20px 20px}";
    static const char script[] = "document.getElementById('result').textContent='app-loaded';";
    static const char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'><rect width='20' height='20' fill='#336699'/></svg>";
    static const WCHAR* files[] = { L"pages\\index.html", L"assets\\main.css", L"assets\\theme.css", L"assets\\main.js", L"assets\\icon.svg" };
    DirectoryFixture* native;
    RuntimePageFixture f;
    PRuntimeDirectoryProvider provider;
    PRuntimeResourceReference entry;
    PCoreScriptResourceInfo script_info;
    PCoreImageDecodeStats stats;
    PCoreImageDecodeStats again;
    PBrowserScriptDomReadCallbacks read;
    PBrowserScriptDomWriteCallbacks write;
    PBrowserScriptBootstrapOptions options;
    PBrowserScriptBootstrapInfo state;
    PBrowserNavigationCommitInfo commit;
    HANDLE session;
    HANDLE history;
    HANDLE failed;
    HANDLE candidate;
    HDC screen;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old;
    BITMAPINFO bi;
    void* pixels;
    const char* source;
    char bytes[4097];
    char url[1024];
    WCHAR path[MAX_PATH];
    unsigned int count;
    unsigned int i;
    unsigned long color;
    int found;
    int fetched;
    int x;
    int y;
    int w;
    int h;
    int n;
    int reads;
    int root;
    int outer;
    int core;
    int index;
    BOOL ok;
    native = (DirectoryFixture*)calloc(1, sizeof(*native));
    if (!native) return FALSE;
    memset(&f, 0, sizeof(f)); session = NULL; history = NULL; failed = NULL; candidate = NULL;
    screen = NULL; dc = NULL; bitmap = NULL; old = NULL; root = 0; outer = 0; core = 0; ok = FALSE;
#define PAGE_CHECK(test, message) do { if (!(test)) { directory_error = message; goto page_done; } } while (0)
    PAGE_CHECK(directory_fixture_init(native, GetCurrentProcessId() + 137100), "app resource private root identity");
    outer = CreateDirectoryW(native->outer, NULL); PAGE_CHECK(outer, "app resource private outer create");
    root = CreateDirectoryW(native->root, NULL); PAGE_CHECK(root, "app resource private root create");
    wsprintfW(path, L"%s\\pages", native->root); PAGE_CHECK(CreateDirectoryW(path, NULL), "app pages directory");
    wsprintfW(path, L"%s\\assets", native->root); PAGE_CHECK(CreateDirectoryW(path, NULL), "app assets directory");
    PAGE_CHECK(directory_fixture_write(native, L"manifest.json", manifest, sizeof(manifest)-1) &&
        directory_fixture_write(native, files[0], html, sizeof(html)-1) &&
        directory_fixture_write(native, files[1], css, sizeof(css)-1) &&
        directory_fixture_write(native, files[2], theme, sizeof(theme)-1) &&
        directory_fixture_write(native, files[3], script, sizeof(script)-1) &&
        directory_fixture_write(native, files[4], svg, sizeof(svg)-1), "app native resource files");
    directory_fixture_provider(native, &provider);
    PAGE_CHECK(!PRuntime_DevelopmentDirectoryOpen(native->canonical, (unsigned int)strlen(native->canonical), &provider, &f.directory), "app bound directory");
    entry.size = sizeof(entry); entry.version = 1;
    PAGE_CHECK(!PRuntime_DirectoryReadResource(f.directory, NULL, 0, "?start=1#top", 12, bytes, 4096, &count, &entry), "app entry resource read");
    PAGE_CHECK(count == sizeof(html)-1 && !memcmp(bytes, html, count) && !strcmp(entry.path, "pages/index.html"), "app entry suffix not native path");
    core = PCore_Init() == 0; PAGE_CHECK(core, "app Core init");
    f.doc = PCore_ParseHTML(bytes, count); f.resources = PBrowser_NavigationResourceCreate(); history = PBrowser_HistoryCreate();
    PAGE_CHECK(f.doc && f.resources && history, "app public Core Browser handles");
    PCore_SetDeviceViewport(200, 160, 96);
    f.role = PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET;
    PAGE_CHECK(!PCore_StyleDocumentEx2(f.doc, NULL, entry.url, runtime_page_resolve, runtime_page_fetch, runtime_page_free, &f), "app linked and imported CSS");
    PAGE_CHECK(!PCore_NodeComputedColor(f.doc, "p", &color) && color == 0xff008000UL, "app external computed style");
    f.role = PBROWSER_NAVIGATION_RESOURCE_ROLE_SCRIPT;
    PAGE_CHECK(!PCore_FetchScriptResourcesEx(f.doc, entry.url, runtime_page_resolve, runtime_page_fetch, runtime_page_free, &f, &found, &fetched) && found == 1 && fetched == 1, "app script discovery");
    PAGE_CHECK(!PCore_GetScriptResource(f.doc, 0, &script_info, url, sizeof(url), &source) && script_info.data_bytes == sizeof(script)-1 && !memcmp(source, script, sizeof(script)-1), "app retained exact script");
    session = PBrowser_ScriptSessionCreateEx(5000, PBROWSER_SCRIPT_APPLICATION_MEMORY_LIMIT_BYTES);
    PAGE_CHECK(session, "app script session");
    memset(&read, 0, sizeof(read)); read.size = sizeof(read); read.pw = &f; read.has_element = runtime_page_has; read.get_text = runtime_page_text;
    memset(&write, 0, sizeof(write)); write.size = sizeof(write); write.pw = &f; write.set_text = runtime_page_set;
    PAGE_CHECK(!PBrowser_ScriptSessionRegisterDomReadCallbacks(session, &read) && !PBrowser_ScriptSessionRegisterDomWriteCallbacks(session, &write) &&
        !PBrowser_ScriptSessionSetGlobalString(session, "__pcoreDocumentUrl", entry.url), "app DOM platform adapters");
    options.size = sizeof(options); options.version = 1; options.generation = 1;
    PAGE_CHECK(!PBrowser_ScriptSessionBootstrapBegin(session, &options), "app bootstrap begin");
    for (i = 0; i < 40; i++) {
        memset(&state, 0, sizeof(state)); state.size = sizeof(state); state.version = 1;
        PAGE_CHECK(!PBrowser_ScriptSessionBootstrapStep(session, 1, &state), "app bootstrap step");
        if (state.state == PBROWSER_BOOTSTRAP_COMPLETE) break;
    }
    PAGE_CHECK(state.state == PBROWSER_BOOTSTRAP_COMPLETE, "app bootstrap complete");
    PAGE_CHECK(!PBrowser_ScriptSessionEvaluate(session, source, script_info.data_bytes) &&
        !PCore_NodeTextContentById(f.doc, "result", bytes, sizeof(bytes), &n) && !strcmp(bytes, "app-loaded"), "app author script changes Core DOM");
    PAGE_CHECK(!PBrowser_ScriptSessionEvaluate(session,
        "typeof PositronServices==='undefined'&&location.protocol==='app:'&&location.hostname==='org.demo_app.notes'", -1) &&
        PBrowser_ScriptSessionGetResult(session) && !strcmp(PBrowser_ScriptSessionGetResult(session), "true"),
        "app location identity visible, URL alone grants no services");
    f.role = PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET;
    PAGE_CHECK(!PCore_StyleDocumentEx2(f.doc, NULL, entry.url, runtime_page_resolve, runtime_page_fetch, runtime_page_free, &f), "app mutation restyle");
    f.role = PBROWSER_NAVIGATION_RESOURCE_ROLE_IMAGE;
    PAGE_CHECK(!PCore_FetchImageResources(f.doc, runtime_page_fetch, runtime_page_free, &f, &found, &fetched) && found >= 2 && found == fetched, "app image and CSS background resources");
    PAGE_CHECK(!PCore_LayoutDocument(f.doc, 200, 160) && !PCore_NodeBox(f.doc, "img", &x, &y, &w, &h) && w == 20 && h == 20, "app SVG layout");
    screen = GetDC(NULL); dc = CreateCompatibleDC(screen); memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = 200; bi.bmiHeader.biHeight = -160;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, NULL, 0);
    PAGE_CHECK(dc && bitmap, "app native paint surface"); old = SelectObject(dc, bitmap); PatBlt(dc, 0, 0, 200, 160, WHITENESS);
    PCore_PaintDocument(f.doc, dc, 0, 0);
    PAGE_CHECK(GetPixel(dc, x + 10, y + 10) == RGB(51,102,153), "app SVG actual painted center");
    PAGE_CHECK(!PCore_GetImageDecodeStats(f.doc, &stats) && stats.svg_creates >= 1, "app image parse diagnostic");
    PAGE_CHECK(!PCore_LayoutDocument(f.doc, 200, 160), "app cached relayout"); PCore_PaintDocument(f.doc, dc, 0, 0);
    PAGE_CHECK(!PCore_GetImageDecodeStats(f.doc, &again) && again.svg_creates == stats.svg_creates, "app repeated paint uses retained SVG");
    PAGE_CHECK(PBrowser_NavigationResourceCommitGate(f.resources) == PBROWSER_NAVIGATION_GATE_READY &&
        !PBrowser_HistoryCommitNavigation(history, entry.url, PBROWSER_HISTORY_METHOD_GET, PBROWSER_HISTORY_TARGET_NEW) &&
        !strcmp(PBrowser_HistoryCurrentUrl(history), entry.url), "app resource gate and history preserve app suffix");
    reads = native->reads;
    PAGE_CHECK(PRuntime_DirectoryReadResource(f.directory, NULL, 0, "app://org.other.notes/index.html", 32, bytes, 4096, &count, &entry) != 0 &&
        PRuntime_DirectoryReadResource(f.directory, NULL, 0, "%2e%2e/secret", 13, bytes, 4096, &count, &entry) != 0 &&
        native->reads == reads, "app authority/traversal rejected before native read");
    failed = PBrowser_NavigationResourceCreate(); candidate = PBrowser_NavigationCandidateCreate(2);
    PAGE_CHECK(failed && candidate && !PBrowser_NavigationResourceRegister(failed, "app://org.demo_app.notes/assets/missing.css", 1,
        PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET, &index) && !PBrowser_NavigationResourceBeginAttempt(failed, index) &&
        !PBrowser_NavigationResourceFail(failed, index, PBROWSER_NAVIGATION_FAILURE_TRANSPORT) &&
        PBrowser_NavigationResourceCommitGate(failed) != PBROWSER_NAVIGATION_GATE_READY && PBrowser_HistoryCount(history) == 1 &&
        !PCore_NodeTextContentById(f.doc, "result", bytes, sizeof(bytes), &n) && !strcmp(bytes, "app-loaded"), "app failed required candidate keeps old page/history");
    memset(&commit, 0, sizeof(commit)); commit.size = sizeof(commit);
    PAGE_CHECK(!PBrowser_NavigationCommitGetInfo(candidate, failed, 2, &commit) &&
        !commit.can_commit && commit.decision == PBROWSER_NAVIGATION_COMMIT_REQUIRED_FAILED, "app Browser rejects failed candidate commit");
    PAGE_CHECK(f.copied == f.released, "app Core callback ownership balanced");
    ok = TRUE;
page_done:
    if (old) SelectObject(dc, old); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); if (screen) ReleaseDC(NULL, screen);
    if (session) PBrowser_ScriptSessionDestroy(session); if (candidate) PBrowser_NavigationCandidateDestroy(candidate);
    if (failed) PBrowser_NavigationResourceDestroy(failed); if (history) PBrowser_HistoryDestroy(history);
    if (f.resources) PBrowser_NavigationResourceDestroy(f.resources); if (f.doc) PCore_FreeDocument(f.doc);
    if (core) {
        screen = GetDC(NULL); n = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
        if (screen) ReleaseDC(NULL, screen);
        PCore_SetDeviceViewport(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), n > 0 ? n : 96);
        PCore_Shutdown();
    }
    PRuntime_DirectoryClose(&f.directory);
    if (root) {
        for (i = 0; i < 5; i++) { wsprintfW(path, L"%s\\%s", native->root, files[i]); DeleteFileW(path); }
        wsprintfW(path, L"%s\\pages", native->root); RemoveDirectoryW(path);
        wsprintfW(path, L"%s\\assets", native->root); RemoveDirectoryW(path);
    }
    directory_fixture_cleanup(native, root, outer); free(native); return ok;
#undef PAGE_CHECK
}
