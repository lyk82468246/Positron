/* Platform fixture only: known private roots, native files/locks, assertions.
 * This is not a general-purpose filesystem canonicalization adapter. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "positron_runtime.h"

static const char fixture_manifest[] = "{\"manifestVersion\":1,\"id\":\"Org.Demo_App.Notes\",\"name\":\"Notes\","
    "\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[],\"migrations\":[]}";
static const char* directory_error = "Runtime directory fixture failed";
const char* test1365_runtime_directory_last_error(void) { return directory_error; }
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
    BOOL ok;
    wsprintfW(path, L"%s\\%s", f->root, name);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    written = 0; ok = WriteFile(file, bytes, count, &written, NULL);
    CloseHandle(file); return ok && written == count;
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
    }
    /* Known fixture key -> native file. Runtime already validated it; this
     * provider additionally confines opens to the four files it created. */
    if (strcmp(key, "manifest.json") && strcmp(key, "index.html") &&
            strcmp(key, "\xe5\x9b\xbe.svg") && strcmp(key, "empty")) return PRUNTIME_IO;
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
    return f->fail_read ? PRUNTIME_IO : PRUNTIME_OK;
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
    /* Only the four known files inside a directory created by this fixture. */
    if (owned_root) {
        wsprintfW(path, L"%s\\manifest.json", f->root); DeleteFileW(path);
        wsprintfW(path, L"%s\\index.html", f->root); DeleteFileW(path);
        wsprintfW(path, L"%s\\\x56fe.svg", f->root); DeleteFileW(path);
        wsprintfW(path, L"%s\\empty", f->root); DeleteFileW(path);
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
