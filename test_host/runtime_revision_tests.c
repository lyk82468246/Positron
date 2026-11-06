#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../positron_runtime/positron_runtime.h"
#include "../positron_db/positron_db.h"
#include "../positron_browser/positron_browser.h"
#include "../positron_script/positron_script.h"
#include "runtime_revision_fixture.h"

typedef struct RevisionFixture {
    char paths[8][256];
    const unsigned char* data[8];
    unsigned int actual[8], declared[8], count, reverse;
    unsigned int roots, leases, hashes, calls, reads;
    unsigned int step_reads, step_enums, step_hash_bytes, bad_budget, bad_guard;
    unsigned int partial_root, partial_registry, missing_record, no_hash;
    unsigned int fail_read, fail_update, fail_finish, partial_hash;
    unsigned int changed_byte, changed_generation, cancel;
    unsigned int huge_count;
    unsigned int bad_root;
    PRuntimeRevisionHandle job;
    PRuntimeStorageHandle storage;
    PRuntimeServiceQueueHandle guarded_queue;
    PRuntimeInstalledRecord record;
} RevisionFixture;
typedef struct RevisionHash {
    HCRYPTPROV provider;
    HCRYPTHASH hash;
} RevisionHash;
static char revision_error[200];
static void revision_guard(RevisionFixture* f)
{
    PRuntimeRevisionInfo info;
    PRuntimeRevisionHandle alias;
    ++f->calls;
    if (f->storage) {
        PRuntimeStorageInfo storage_info;
        PRuntimeStorageHandle storage_alias = f->storage;
        storage_info.size = sizeof(storage_info); storage_info.version = 1;
        if (PRuntime_StorageGetInfo(f->storage, &storage_info) != PRUNTIME_STATE ||
                PRuntime_StorageClose(&storage_alias) != PRUNTIME_STATE || storage_alias != f->storage) ++f->bad_guard;
    }
    if (f->guarded_queue) {
        PRuntimeServiceQueueHandle queue_alias = f->guarded_queue;
        if (PRuntime_ServiceQueueClose(&queue_alias) != PRUNTIME_STATE || queue_alias != f->guarded_queue) ++f->bad_guard;
    }
    if (!f->job) return;
    info.size = sizeof(info); info.version = 1; alias = f->job;
    if (PRuntime_RevisionGetInfo(f->job, &info) != PRUNTIME_STATE ||
            PRuntime_RevisionClose(&alias) != PRUNTIME_STATE || alias != f->job) ++f->bad_guard;
}
static int revision_root(void* user, const char* directory, unsigned int bytes, PRuntimeDirectoryRoot* root)
{
    RevisionFixture* f = (RevisionFixture*)user;
    revision_guard(f);
    if (f->roots) return PRUNTIME_BUSY;
    if (!bytes || bytes > 1023) return PRUNTIME_IO;
    root->root = f; memcpy(root->canonical, directory, bytes); root->canonical[bytes] = 0;
    root->canonical_bytes = bytes; ++f->roots;
    if (f->bad_root) root->canonical[0] = 'X';
    return f->partial_root ? PRUNTIME_IO : PRUNTIME_OK;
}
static void revision_root_close(void* user, void* root)
{
    RevisionFixture* f = (RevisionFixture*)user;
    revision_guard(f);
    if (root != f || !f->roots || f->hashes) ++f->bad_guard;
    else --f->roots;
}
static int revision_enum(void* user, void* root, unsigned int index, PRuntimeRevisionFile* file)
{
    RevisionFixture* f = (RevisionFixture*)user;
    unsigned int i;
    revision_guard(f);
    if (++f->step_enums > 16) ++f->bad_budget;
    if (root != f) return PRUNTIME_IO;
    if (f->huge_count) {
        if (index >= 1025) return PRUNTIME_NOT_FOUND;
        if (!index) strcpy(file->path, "manifest.json");
        else sprintf(file->path, "f%04u", index);
        file->file_bytes = index ? 0 : revision_lengths[0]; return PRUNTIME_OK;
    }
    if (index >= f->count) return PRUNTIME_NOT_FOUND;
    i = f->reverse ? f->count - 1 - index : index;
    strcpy(file->path, f->paths[i]); file->file_bytes = f->declared[i];
    return PRUNTIME_OK;
}
static int revision_read(void* user, void* root, const char* path, unsigned int offset,
        void* buffer, unsigned int capacity, unsigned int* written)
{
    RevisionFixture* f = (RevisionFixture*)user;
    unsigned int i, n, j;
    revision_guard(f); ++f->reads; f->step_reads += capacity;
    if (f->step_reads > PRUNTIME_REVISION_STEP_BYTES) ++f->bad_budget;
    if (root != f || f->fail_read) return PRUNTIME_IO;
    for (i = 0; i < f->count; ++i) if (!strcmp(path, f->paths[i])) break;
    if (i == f->count || offset > f->actual[i]) return PRUNTIME_IO;
    n = f->actual[i] - offset; if (n > capacity) n = capacity;
    if (f->data[i]) memcpy(buffer, f->data[i] + offset, n);
    else for (j = 0; j < n; ++j) ((unsigned char*)buffer)[j] = (unsigned char)((offset + j) * 17 + ((offset + j) >> 8));
    if (f->changed_byte && !strcmp(path, "data.bin") && !offset && n) ((unsigned char*)buffer)[0] ^= 1;
    *written = n; return PRUNTIME_OK;
}
static int revision_hash_begin(void* user, void** output)
{
    RevisionFixture* f = (RevisionFixture*)user;
    RevisionHash* h;
    DWORD type, n;
    unsigned char digest[32];
    revision_guard(f);
    if (f->no_hash) return PRUNTIME_UNSUPPORTED;
    h = (RevisionHash*)calloc(1, sizeof(*h)); if (!h) return PRUNTIME_MEMORY;
    /* Capability probe only: same SHA256, no weaker algorithm fallback,
     * no key container creation or registry changes. */
    for (type = 0; type < 2; ++type) {
        if (CryptAcquireContext(&h->provider, NULL, NULL, type ? 24 : PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
                CryptCreateHash(h->provider, CALG_SHA_256, 0, 0, &h->hash)) break;
        if (h->provider) CryptReleaseContext(h->provider, 0);
        h->provider = 0;
    }
    if (!h->hash) { free(h); return PRUNTIME_UNSUPPORTED; }
    n = sizeof(digest);
    if (!CryptHashData(h->hash, (const BYTE*)"abc", 3, 0) ||
            !CryptGetHashParam(h->hash, HP_HASHVAL, digest, &n, 0) || n != 32 ||
            memcmp(digest, revision_sha256_abc, 32)) {
        CryptDestroyHash(h->hash); CryptReleaseContext(h->provider, 0); free(h); return PRUNTIME_IO;
    }
    CryptDestroyHash(h->hash); h->hash = 0;
    if (!CryptCreateHash(h->provider, CALG_SHA_256, 0, 0, &h->hash)) {
        CryptReleaseContext(h->provider, 0); free(h); return PRUNTIME_UNSUPPORTED;
    }
    *output = h; ++f->hashes;
    return f->partial_hash ? PRUNTIME_IO : PRUNTIME_OK;
}
static int revision_hash_update(void* user, void* token, const void* data, unsigned int bytes)
{
    RevisionFixture* f = (RevisionFixture*)user;
    RevisionHash* h = (RevisionHash*)token;
    revision_guard(f); f->step_hash_bytes += bytes;
    if (f->step_hash_bytes > PRUNTIME_REVISION_STEP_BYTES) ++f->bad_budget;
    if (f->fail_update || !h) return PRUNTIME_IO;
    return CryptHashData(h->hash, (const BYTE*)data, bytes, 0) ? PRUNTIME_OK : PRUNTIME_IO;
}
static int revision_hash_finish(void* user, void* token, unsigned char output[32])
{
    RevisionFixture* f = (RevisionFixture*)user;
    RevisionHash* h = (RevisionHash*)token;
    DWORD bytes = 32;
    revision_guard(f);
    if (f->fail_finish || !h) return PRUNTIME_IO;
    return CryptGetHashParam(h->hash, HP_HASHVAL, output, &bytes, 0) && bytes == 32 ? PRUNTIME_OK : PRUNTIME_IO;
}
static void revision_hash_close(void* user, void* token)
{
    RevisionFixture* f = (RevisionFixture*)user;
    RevisionHash* h = (RevisionHash*)token;
    revision_guard(f);
    if (!h || !f->hashes) { ++f->bad_guard; return; }
    CryptDestroyHash(h->hash); CryptReleaseContext(h->provider, 0); free(h); --f->hashes;
}
static int revision_registry_open(void* user, const char* id, PRuntimeInstalledRecord* record, void** lease)
{
    RevisionFixture* f = (RevisionFixture*)user;
    revision_guard(f);
    if (f->missing_record) return PRUNTIME_NOT_FOUND;
    if (f->leases) return PRUNTIME_BUSY;
    *lease = f; ++f->leases; *record = f->record;
    return f->partial_registry ? PRUNTIME_IO : PRUNTIME_OK;
}
static int revision_registry_refresh(void* user, void* lease, PRuntimeInstalledRecord* record)
{
    RevisionFixture* f = (RevisionFixture*)user;
    revision_guard(f);
    if (lease != f) return PRUNTIME_IO;
    *record = f->record;
    if (f->changed_generation) ++record->generation;
    return PRUNTIME_OK;
}
static void revision_registry_close(void* user, void* lease)
{
    RevisionFixture* f = (RevisionFixture*)user;
    revision_guard(f);
    if (lease != f || !f->leases || f->roots || f->hashes) ++f->bad_guard;
    else --f->leases;
}
static int revision_cancelled(void* user)
{
    RevisionFixture* f = (RevisionFixture*)user;
    revision_guard(f); return (int)f->cancel;
}
static void revision_fixture(RevisionFixture* f, const PRuntimeApprovalSnapshot* snapshot)
{
    unsigned int i;
    memset(f, 0, sizeof(*f)); f->count = 5;
    for (i = 0; i < 5; ++i) {
        strcpy(f->paths[i], revision_paths[i]); f->data[i] = revision_sources[i];
        f->actual[i] = f->declared[i] = revision_lengths[i];
    }
    f->record.size = sizeof(f->record); f->record.version = 1;
    f->record.generation = 7; f->record.active = 1;
    strcpy(f->record.directory, "\\revision-root");
    if (snapshot) f->record.approval = *snapshot;
}
static void revision_tables(RevisionFixture* f, PRuntimeRevisionProvider* p,
        PRuntimeInstalledProvider* registry, PRuntimeRevisionOptions* options)
{
    memset(p, 0, sizeof(*p)); p->size = sizeof(*p); p->version = 1;
    p->digest_algorithm = PRUNTIME_DIGEST_SHA256; p->user_data = f;
    p->open_root = revision_root; p->close_root = revision_root_close;
    p->enumerate_file = revision_enum; p->read_at = revision_read;
    p->hash_begin = revision_hash_begin; p->hash_update = revision_hash_update;
    p->hash_finish = revision_hash_finish; p->hash_close = revision_hash_close;
    memset(registry, 0, sizeof(*registry)); registry->size = sizeof(*registry); registry->version = 1;
    registry->user_data = f; registry->open = revision_registry_open;
    registry->refresh = revision_registry_refresh; registry->close = revision_registry_close;
    options->size = sizeof(*options); options->version = 1;
    options->cancelled = revision_cancelled; options->user_data = f;
}
static int revision_run(RevisionFixture* f, PRuntimeRevisionHandle job)
{
    unsigned int i;
    int rc;
    f->job = job;
    for (i = 0; i < 5000; ++i) {
        f->step_reads = f->step_enums = f->step_hash_bytes = 0;
        rc = PRuntime_RevisionStep(job);
        if (rc != PRUNTIME_OK) return rc;
    }
    return PRUNTIME_LIMIT;
}
static void revision_dispose(RevisionFixture* f, PRuntimeRevisionHandle* job)
{
    if (*job && *job != (PRuntimeRevisionHandle)1) {
        f->job = *job; PRuntime_RevisionClose(job);
    }
    f->job = NULL;
}
typedef struct RevisionThread { PRuntimeRevisionHandle job; int rc; } RevisionThread;
static DWORD WINAPI revision_other_thread(void* user)
{
    RevisionThread* t = (RevisionThread*)user;
    t->rc = PRuntime_RevisionStep(t->job); return 0;
}
#define RCHECK(expr) do { if (!(expr)) { \
    sprintf(revision_error, "Revision contract failed at line %d (last rc=%d)", __LINE__, rc); goto failed; \
} } while (0)
BOOL test1377_runtime_revision_contract(void)
{
    RevisionFixture f;
    PRuntimeRevisionProvider p;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions options;
    PRuntimeRevisionHandle job = NULL, other = NULL;
    PRuntimeRevisionInfo info;
    PRuntimeRevisionFile file;
    PRuntimeApprovalOptions approval;
    PRuntimeApprovalSnapshot snapshot, canary;
    RevisionThread t;
    HANDLE thread;
    unsigned int i, j, written, mode;
    unsigned char bytes[32], expected[32];
    int rc = 0;
    revision_error[0] = 0;
    revision_fixture(&f, NULL); revision_tables(&f, &p, &registry, &options);
    job = (PRuntimeRevisionHandle)1; p.digest_algorithm = 2;
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &p, &options, &job) == PRUNTIME_UNSUPPORTED &&
            job == (PRuntimeRevisionHandle)1 && f.calls == 0);
    job = NULL; p.digest_algorithm = PRUNTIME_DIGEST_SHA256;
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &p, &options, &job) == PRUNTIME_OK && !f.calls);
    f.job = job;
    info.size = sizeof(info); info.version = 1;
    RCHECK(PRuntime_RevisionGetInfo(job, &info) == PRUNTIME_OK && info.state == PRUNTIME_REVISION_INITIALIZING && !info.id[0]);
    file.size = sizeof(file); file.version = 1;
    RCHECK(PRuntime_RevisionGetFile(job, 0, &file) == PRUNTIME_STATE);
    t.job = job; t.rc = 0; thread = CreateThread(NULL, 0, revision_other_thread, &t, 0, NULL);
    RCHECK(thread != NULL);
    RCHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
    RCHECK(t.rc == PRUNTIME_STATE);
    rc = revision_run(&f, job);
    RCHECK(rc == PRUNTIME_REVISION_DONE);
    RCHECK(PRuntime_RevisionGetInfo(job, &info) == PRUNTIME_OK && info.file_count == 5 &&
            info.verified_files == 5 && info.verified_bytes == info.total_bytes && !info.installed);
    RCHECK(!memcmp(info.content_id, revision_content_id, 32));
    written = 99;
    RCHECK(PRuntime_InstalledReadFile(job, 0, 0, bytes, sizeof(bytes), &written) == PRUNTIME_DENIED && written == 99);
    memset(&approval, 0, sizeof(approval)); approval.size = sizeof(approval); approval.version = 1;
    approval.revision = 1; strcpy(approval.installation_id, "00112233445566778899aabbccddeeff");
    memset(&snapshot, 0x5a, sizeof(snapshot)); snapshot.size = sizeof(snapshot); snapshot.version = 1; canary = snapshot;
    RCHECK(PRuntime_RevisionBuildApproval(job, &approval, &snapshot) == PRUNTIME_DENIED && !memcmp(&snapshot, &canary, sizeof(snapshot)));
    approval.confirmed = 1;
    approval.revision = 2;
    RCHECK(PRuntime_RevisionBuildApproval(job, &approval, &snapshot) == PRUNTIME_UNSUPPORTED &&
            !memcmp(&snapshot, &canary, sizeof(snapshot)));
    approval.revision = 1;
    RCHECK(PRuntime_RevisionBuildApproval(job, &approval, &snapshot) == PRUNTIME_OK);
    RCHECK(snapshot.approved_permissions == PRUNTIME_PERMISSION_SQL && !memcmp(snapshot.content_id, revision_content_id, 32));
    revision_dispose(&f, &job);
    RCHECK(!f.roots && !f.leases && !f.hashes && !f.bad_guard && !f.bad_budget);
    for (mode = 0; mode < 4; ++mode) {
        revision_fixture(&f, &snapshot); revision_tables(&f, &p, &registry, &options);
        f.reverse = mode & 1;
        if (mode == 2) strcpy(f.record.directory, "\\another-root");
        if (mode == 3) f.record.approval.installation_id[0] = '1';
        RCHECK(PRuntime_OpenInstalled(f.record.approval.installation_id, 32, 7, &registry, &p, &options, &job) == PRUNTIME_OK);
        rc = revision_run(&f, job); RCHECK(rc == PRUNTIME_REVISION_DONE);
        RCHECK(PRuntime_RevisionGetInfo(job, &info) == PRUNTIME_OK && info.installed &&
                !strcmp(info.installation_id, f.record.approval.installation_id) && info.registry_generation == 7 &&
                !memcmp(info.content_id, snapshot.content_id, 32));
        RCHECK(PRuntime_OpenInstalled(f.record.approval.installation_id, 32, 7, &registry, &p, &options, &other) == PRUNTIME_OK);
        f.job = other;
        RCHECK(PRuntime_RevisionStep(other) == PRUNTIME_BUSY);
        revision_dispose(&f, &other); f.job = job;
        RCHECK(f.roots == 1 && f.leases == 1);
        for (i = 0; i < 5; ++i) {
            file.size = sizeof(file); file.version = 1;
            RCHECK(PRuntime_RevisionGetFile(job, i, &file) == PRUNTIME_OK);
            written = 99; f.step_reads = 0;
            RCHECK(PRuntime_InstalledReadFile(job, i, 0, bytes, sizeof(bytes), &written) == PRUNTIME_OK);
            for (j = 0; j < f.count; ++j) if (!strcmp(file.path, f.paths[j])) break;
            RCHECK(j < f.count && written == (f.actual[j] < sizeof(bytes) ? f.actual[j] : sizeof(bytes)));
            if (f.data[j]) RCHECK(!memcmp(bytes, f.data[j], written));
            else for (j = 0; j < written; ++j) RCHECK(bytes[j] == (unsigned char)(j * 17 + (j >> 8)));
        }
        memset(bytes, 0xa5, sizeof(bytes)); memcpy(expected, bytes, sizeof(bytes)); written = 99;
        f.record.active = 0;
        RCHECK(PRuntime_InstalledReadFile(job, 0, 0, bytes, sizeof(bytes), &written) == PRUNTIME_DENIED &&
                written == 99 && !memcmp(bytes, expected, sizeof(bytes)));
        revision_dispose(&f, &job);
        RCHECK(!f.roots && !f.leases && !f.hashes && !f.bad_guard && !f.bad_budget);
    }
    /* Each failure starts with a fresh immutable fixture and native record. */
    for (mode = 0; mode < 23; ++mode) {
        revision_fixture(&f, &snapshot); revision_tables(&f, &p, &registry, &options);
        switch (mode) {
        case 0: f.changed_byte = 1; rc = PRUNTIME_INVALID; break;
        case 1: strcpy(f.paths[4], "renamed.bin"); rc = PRUNTIME_INVALID; break;
        case 2: f.count = 4; rc = PRUNTIME_INVALID; break;
        case 3: f.count = 6; strcpy(f.paths[5], "extra.bin"); rc = PRUNTIME_INVALID; break;
        case 4: f.record.active = 0; rc = PRUNTIME_DENIED; break;
        case 5: f.missing_record = 1; rc = PRUNTIME_DENIED; break;
        case 6: f.record.generation = 6; rc = PRUNTIME_INVALID; break;
        case 7: f.changed_generation = 1; rc = PRUNTIME_INVALID; break;
        case 8: f.record.approval.approved_permissions = 0; rc = PRUNTIME_DENIED; break;
        case 9: f.record.approval.runtime_api_version = 2; rc = PRUNTIME_UNSUPPORTED; break;
        case 10: strcpy(f.record.approval.installation_id, "11112233445566778899aabbccddeeff"); rc = PRUNTIME_INVALID; break;
        case 11: f.partial_root = 1; rc = PRUNTIME_IO; break;
        case 12: f.partial_registry = 1; rc = PRUNTIME_IO; break;
        case 13: f.no_hash = 1; rc = PRUNTIME_UNSUPPORTED; break;
        case 14: f.partial_hash = 1; rc = PRUNTIME_IO; break;
        case 15: f.fail_update = 1; rc = PRUNTIME_IO; break;
        case 16: f.fail_finish = 1; rc = PRUNTIME_IO; break;
        case 17: --f.declared[4]; rc = PRUNTIME_INVALID; break;
        case 18: --f.actual[4]; rc = PRUNTIME_IO; break;
        case 19: f.bad_root = 1; rc = PRUNTIME_INVALID; break;
        case 20: f.record.approval.approved_permissions |= 2; rc = PRUNTIME_UNSUPPORTED; break;
        case 21: f.record.approval.revision = 2; rc = PRUNTIME_UNSUPPORTED; break;
        default: strcpy(f.record.approval.id, "com.other.app"); rc = PRUNTIME_INVALID; break;
        }
        RCHECK(PRuntime_OpenInstalled(snapshot.installation_id, 32, 7, &registry, &p, &options, &job) == PRUNTIME_OK);
        {
            int expected_rc = rc;
            rc = revision_run(&f, job); RCHECK(rc == expected_rc);
        }
        RCHECK(PRuntime_RevisionStep(job) == PRUNTIME_STATE);
        RCHECK(PRuntime_RevisionGetInfo(job, &info) == PRUNTIME_OK && info.state == PRUNTIME_REVISION_FAILED && !info.id[0]);
        revision_dispose(&f, &job);
        RCHECK(!f.roots && !f.leases && !f.hashes && !f.bad_guard && !f.bad_budget);
    }
    for (mode = 0; mode < 7; ++mode) {
        revision_fixture(&f, NULL); revision_tables(&f, &p, &registry, &options);
        if (mode == 0) strcpy(f.paths[2], "../escape");
        if (mode == 1) strcpy(f.paths[2], "INDEX.HTML");
        if (mode == 2) f.declared[4] = PRUNTIME_RESOURCE_BYTES + 1;
        if (mode == 3) for (i = 1; i < 5; ++i) f.declared[i] = PRUNTIME_RESOURCE_BYTES;
        if (mode == 4) f.huge_count = 1;
        if (mode == 5) f.declared[0] = 0;
        if (mode == 6) strcpy(f.paths[1], "missing-entry.html");
        RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &p, &options, &job) == PRUNTIME_OK);
        rc = revision_run(&f, job); RCHECK(rc == ((mode < 2 || mode >= 5) ? PRUNTIME_INVALID : PRUNTIME_LIMIT));
        revision_dispose(&f, &job); RCHECK(!f.roots && !f.hashes && !f.bad_guard && !f.bad_budget);
    }
    revision_fixture(&f, NULL); revision_tables(&f, &p, &registry, &options);
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &p, &options, &job) == PRUNTIME_OK);
    f.job = job;
    do {
        f.step_reads = f.step_enums = f.step_hash_bytes = 0;
        rc = PRuntime_RevisionStep(job); RCHECK(rc == PRUNTIME_OK);
        RCHECK(PRuntime_RevisionGetInfo(job, &info) == PRUNTIME_OK);
    } while (!info.verified_bytes);
    f.cancel = 1; RCHECK(PRuntime_RevisionStep(job) == PRUNTIME_CANCELLED && !f.hashes);
    RCHECK(PRuntime_RevisionCancel(job) == PRUNTIME_OK);
    revision_dispose(&f, &job); RCHECK(PRuntime_RevisionClose(&job) == PRUNTIME_OK && !f.roots && !f.bad_guard && !f.bad_budget);
    return TRUE;
failed:
    revision_dispose(&f, &other);
    revision_dispose(&f, &job); return FALSE;
}
const char* test1377_runtime_revision_error(void) { return revision_error; }

typedef struct RegistrationFixture {
    RevisionFixture* revision;
    PRuntimeRegistrationRecord record;
    int acquire_result, read_result;
    unsigned int leases, acquired, reads, closes, partial;
} RegistrationFixture;
static int registration_acquire(void* user, const char* id, const char* installation, void** lease)
{
    RegistrationFixture* f = (RegistrationFixture*)user;
    revision_guard(f->revision); ++f->acquired;
    if (strcmp(id, "com.example.installed") || strcmp(installation, "00112233445566778899aabbccddeeff"))
        return PRUNTIME_IO;
    if (f->acquire_result == PRUNTIME_OK || f->partial) { *lease = f; ++f->leases; }
    return f->acquire_result;
}
static int registration_read(void* user, void* lease, PRuntimeRegistrationRecord* output)
{
    RegistrationFixture* f = (RegistrationFixture*)user;
    revision_guard(f->revision); ++f->reads;
    if (lease != f || f->leases != 1) return PRUNTIME_IO;
    *output = f->record; return f->read_result;
}
static void registration_close(void* user, void* lease)
{
    RegistrationFixture* f = (RegistrationFixture*)user;
    revision_guard(f->revision); ++f->closes;
    if (lease != f || f->leases != 1) ++f->revision->bad_guard;
    else --f->leases;
}
BOOL test1378_runtime_registration_contract(void)
{
    RevisionFixture f;
    RegistrationFixture native;
    PRuntimeRevisionProvider p;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions options;
    PRuntimeRevisionHandle job = NULL;
    PRuntimeApprovalOptions approval;
    PRuntimeApprovalSnapshot snapshot;
    PRuntimeRegistrationProvider provider;
    PRuntimeRegistrationTransition t;
    PRuntimeRegistrationDecision decision, canary;
    PRuntimeRevisionInfo info;
    unsigned int mode, expected_match;
    int rc = 0, expected_rc;
    revision_error[0] = 0;
    revision_fixture(&f, NULL); revision_tables(&f, &p, &registry, &options);
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &p, &options, &job) == PRUNTIME_OK);
    rc = revision_run(&f, job); RCHECK(rc == PRUNTIME_REVISION_DONE);
    memset(&approval, 0, sizeof(approval)); approval.size = sizeof(approval); approval.version = 1;
    approval.revision = 1; approval.confirmed = 1;
    strcpy(approval.installation_id, "00112233445566778899aabbccddeeff");
    snapshot.size = sizeof(snapshot); snapshot.version = 1;
    RCHECK(PRuntime_RevisionBuildApproval(job, &approval, &snapshot) == PRUNTIME_OK);
    memset(&provider, 0, sizeof(provider)); provider.size = sizeof(provider); provider.version = 1;
    provider.user_data = &native; provider.acquire = registration_acquire;
    provider.read = registration_read; provider.close = registration_close;
    for (mode = 0; mode < 29; ++mode) {
        memset(&native, 0, sizeof(native)); native.revision = &f;
        memset(&t, 0, sizeof(t)); t.size = sizeof(t); t.version = 1;
        t.commit_outcome = PRUNTIME_COMMIT_UNKNOWN; t.previous_exists = 1;
        t.previous.size = sizeof(t.previous); t.previous.version = 1;
        t.previous.state = PRUNTIME_REG_PREPARING; t.previous.generation = 7;
        t.previous.approval = snapshot; strcpy(t.previous.directory, "\\revision-root");
        t.proposed = t.previous; t.proposed.state = PRUNTIME_REG_READY; t.proposed.generation = 8;
        native.record = t.proposed; expected_rc = PRUNTIME_OK; expected_match = PRUNTIME_REG_MATCH_PROPOSED;
        switch (mode) {
        case 0: break; /* committed response lost */
        case 1: t.commit_outcome = PRUNTIME_COMMIT_CONFIRMED; break;
        case 2: t.commit_outcome = PRUNTIME_COMMIT_NOT_COMMITTED; expected_rc = PRUNTIME_INVALID; break;
        case 3: native.record = t.previous; expected_match = PRUNTIME_REG_MATCH_PREVIOUS; break;
        case 4: native.record = t.previous; t.commit_outcome = PRUNTIME_COMMIT_NOT_COMMITTED;
            expected_match = PRUNTIME_REG_MATCH_PREVIOUS; break;
        case 5: native.record = t.previous; t.commit_outcome = PRUNTIME_COMMIT_CONFIRMED;
            expected_rc = PRUNTIME_INVALID; break;
        case 6: native.read_result = PRUNTIME_NOT_FOUND; expected_rc = PRUNTIME_DENIED; break;
        case 7: native.read_result = PRUNTIME_IO; expected_rc = PRUNTIME_IO; break;
        case 8: native.acquire_result = PRUNTIME_IO; native.partial = 1; expected_rc = PRUNTIME_IO; break;
        case 9: native.acquire_result = PRUNTIME_BUSY; expected_rc = PRUNTIME_BUSY; break;
        case 10: native.record.generation = 9; expected_rc = PRUNTIME_INVALID; break;
        case 11: native.record.approval.installation_id[0] = '1'; expected_rc = PRUNTIME_INVALID; break;
        case 12: t.proposed.state = PRUNTIME_REG_CANCELLED; expected_match = PRUNTIME_REG_READY_WON_CANCEL; break;
        case 13: t.proposed.state = PRUNTIME_REG_CANCELLED; native.record = t.proposed; break;
        case 14: t.previous.state = PRUNTIME_REG_FAILED; t.proposed.state = PRUNTIME_REG_PREPARING;
            t.confirmed = 1; native.record = t.proposed; break;
        case 15: t.previous.state = PRUNTIME_REG_FAILED; t.proposed.state = PRUNTIME_REG_PREPARING;
            expected_rc = PRUNTIME_DENIED; break;
        case 16: t.previous_exists = 0; t.proposed.state = PRUNTIME_REG_PREPARING;
            t.proposed.generation = 1; t.confirmed = 1; native.read_result = PRUNTIME_NOT_FOUND;
            expected_match = PRUNTIME_REG_MATCH_PREVIOUS; break;
        case 17: t.previous_exists = 0; t.proposed.state = PRUNTIME_REG_PREPARING;
            t.proposed.generation = 1; t.confirmed = 1; native.record = t.proposed; break;
        case 18: t.previous_exists = 0; t.proposed.state = PRUNTIME_REG_PREPARING;
            t.proposed.generation = 1; expected_rc = PRUNTIME_DENIED; break;
        case 19: t.previous.state = PRUNTIME_REG_READY; t.proposed.state = PRUNTIME_REG_CANCELLED;
            expected_rc = PRUNTIME_DENIED; break;
        case 20: t.previous.generation = 2147483647U; expected_rc = PRUNTIME_LIMIT; break;
        case 21: t.proposed.target_schema = 1; expected_rc = PRUNTIME_INVALID; break;
        case 22: strcpy(native.record.directory, "\\other-root"); expected_rc = PRUNTIME_INVALID; break;
        case 23: native.record.approval.approved_permissions |= 2; expected_rc = PRUNTIME_UNSUPPORTED; break;
        case 24: native.record.version = 2; expected_rc = PRUNTIME_INVALID; break;
        case 25: t.previous.state = PRUNTIME_REG_REVOKED; expected_rc = PRUNTIME_DENIED; break;
        case 26: t.previous.state = PRUNTIME_REG_CANCELLED; expected_rc = PRUNTIME_DENIED; break;
        case 27: t.proposed.state = PRUNTIME_REG_FAILED; native.record = t.proposed; break;
        default: t.commit_outcome = 0; expected_rc = PRUNTIME_ARGUMENT; break;
        }
        memset(&decision, 0xa5, sizeof(decision)); decision.size = sizeof(decision); decision.version = 1;
        canary = decision;
        rc = PRuntime_RevisionReconcileRegistration(job, &t, &provider, &decision);
        RCHECK(rc == expected_rc);
        if (rc == PRUNTIME_OK) {
            RCHECK(decision.match == expected_match);
            /* Repeat readback must neither advance generation nor replay CAS. */
            rc = PRuntime_RevisionReconcileRegistration(job, &t, &provider, &decision);
            RCHECK(rc == PRUNTIME_OK && decision.match == expected_match);
        } else RCHECK(!memcmp(&decision, &canary, sizeof(decision)));
        RCHECK(!native.leases && native.closes == native.acquired -
                ((native.acquire_result != PRUNTIME_OK && !native.partial) ? native.acquired : 0));
        RCHECK(!f.bad_guard && !f.bad_budget);
        info.size = sizeof(info); info.version = 1;
        RCHECK(PRuntime_RevisionGetInfo(job, &info) == PRUNTIME_OK && info.state == PRUNTIME_REVISION_COMPLETE &&
                !memcmp(info.content_id, snapshot.content_id, 32));
    }
    revision_dispose(&f, &job); RCHECK(!f.roots && !f.hashes && !f.bad_guard);
    return TRUE;
failed:
    revision_dispose(&f, &job); return FALSE;
}

typedef struct IntentFixture {
    RegistrationFixture registry;
    PRuntimeInstallIntentHandle job;
    unsigned int writes, calls, commit, outcome, read_failure, fail_next_read;
    int write_result;
} IntentFixture;
static void intent_guard(IntentFixture* f)
{
    PRuntimeInstallIntentInfo info;
    PRuntimeInstallIntentHandle alias;
    ++f->calls;
    if (!f->job) return;
    info.size = sizeof(info); info.version = 1; alias = f->job;
    if (PRuntime_InstallIntentGetInfo(f->job, &info) != PRUNTIME_STATE ||
            PRuntime_InstallIntentCancel(f->job) != PRUNTIME_STATE ||
            PRuntime_InstallIntentClose(&alias) != PRUNTIME_STATE || alias != f->job)
        ++f->registry.revision->bad_guard;
}
static int intent_acquire(void* user, const char* id, const char* installation, void** lease)
{
    IntentFixture* f = (IntentFixture*)user;
    intent_guard(f); return registration_acquire(&f->registry, id, installation, lease);
}
static int intent_read(void* user, void* lease, PRuntimeRegistrationRecord* record)
{
    IntentFixture* f = (IntentFixture*)user;
    intent_guard(f);
    if (f->fail_next_read) { f->fail_next_read = 0; return PRUNTIME_IO; }
    if (f->writes && f->read_failure) return PRUNTIME_IO;
    return registration_read(&f->registry, lease, record);
}
static int intent_cas(void* user, void* lease, const PRuntimeRegistrationRecord* previous,
        const PRuntimeRegistrationRecord* proposed, unsigned int* outcome)
{
    IntentFixture* f = (IntentFixture*)user;
    intent_guard(f); revision_guard(f->registry.revision); ++f->writes;
    if (lease != &f->registry || f->registry.leases != 1 ||
            (previous && (f->registry.read_result != PRUNTIME_OK || memcmp(previous, &f->registry.record, sizeof(*previous)))) ||
            (!previous && f->registry.read_result != PRUNTIME_NOT_FOUND)) {
        *outcome = PRUNTIME_COMMIT_NOT_COMMITTED; return PRUNTIME_BUSY;
    }
    if (f->commit) { f->registry.record = *proposed; f->registry.read_result = PRUNTIME_OK; }
    *outcome = f->outcome; return f->write_result;
}
static void intent_close(void* user, void* lease)
{
    IntentFixture* f = (IntentFixture*)user;
    intent_guard(f); registration_close(&f->registry, lease);
}
typedef struct IntentThread { PRuntimeInstallIntentHandle job; int rc; } IntentThread;
static DWORD WINAPI intent_other_thread(void* user)
{
    IntentThread* t = (IntentThread*)user;
    PRuntimeInstallIntentHandle alias = t->job;
    t->rc = PRuntime_InstallIntentStep(t->job) == PRUNTIME_STATE &&
        PRuntime_InstallIntentCancel(t->job) == PRUNTIME_STATE &&
        PRuntime_InstallIntentClose(&alias) == PRUNTIME_STATE && alias == t->job;
    return 0;
}
BOOL test1379_runtime_intent_contract(void)
{
    RevisionFixture f;
    IntentFixture native;
    PRuntimeRevisionProvider p;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions options;
    PRuntimeRevisionHandle job = NULL, alias;
    PRuntimeInstallIntentHandle intent = NULL, other;
    PRuntimeInstallIntentOptions open;
    PRuntimeInstallIntentProvider provider;
    PRuntimeInstallIntentInfo info;
    PRuntimeApprovalSnapshot snapshot;
    IntentThread thread_data;
    HANDLE thread;
    unsigned int mode, steps, old_calls;
    int rc = 0, expected;
    revision_error[0] = 0;
    revision_fixture(&f, NULL); revision_tables(&f, &p, &registry, &options);
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &p, &options, &job) == PRUNTIME_OK);
    rc = revision_run(&f, job); RCHECK(rc == PRUNTIME_REVISION_DONE);
    memset(&open, 0, sizeof(open)); open.size = sizeof(open); open.version = 1;
    open.mode = PRUNTIME_INTENT_NEW; open.approval.size = sizeof(open.approval); open.approval.version = 1;
    open.approval.confirmed = 1; open.approval.revision = 1;
    strcpy(open.approval.installation_id, "00112233445566778899aabbccddeeff");
    snapshot.size = sizeof(snapshot); snapshot.version = 1;
    RCHECK(PRuntime_RevisionBuildApproval(job, &open.approval, &snapshot) == PRUNTIME_OK);
    memset(&provider, 0, sizeof(provider)); provider.size = sizeof(provider); provider.version = 1;
    provider.user_data = &native; provider.acquire = intent_acquire; provider.read = intent_read;
    provider.compare_exchange = intent_cas; provider.close = intent_close;
    memset(&native, 0, sizeof(native)); native.registry.revision = &f;
    open.approval.confirmed = 0; intent = (PRuntimeInstallIntentHandle)1;
    RCHECK(PRuntime_InstallIntentBegin(job, &open, &provider, &intent) == PRUNTIME_DENIED &&
            intent == (PRuntimeInstallIntentHandle)1 && !native.calls);
    intent = NULL; open.approval.confirmed = 1;
    for (mode = 0; mode < 21; ++mode) {
        memset(&native, 0, sizeof(native)); native.registry.revision = &f;
        native.registry.read_result = PRUNTIME_NOT_FOUND;
        native.commit = 1; native.outcome = PRUNTIME_COMMIT_CONFIRMED;
        open.mode = PRUNTIME_INTENT_NEW; expected = PRUNTIME_INTENT_DONE;
        native.registry.record.size = sizeof(native.registry.record); native.registry.record.version = 1;
        native.registry.record.approval = snapshot; native.registry.record.generation = 7;
        native.registry.record.state = PRUNTIME_REG_PREPARING;
        strcpy(native.registry.record.directory, "\\revision-root");
        switch (mode) {
        case 0: break;
        case 1: native.outcome = PRUNTIME_COMMIT_UNKNOWN; native.write_result = PRUNTIME_IO; break;
        case 2: native.commit = 0; native.outcome = PRUNTIME_COMMIT_NOT_COMMITTED;
            native.write_result = PRUNTIME_IO; expected = PRUNTIME_IO; break;
        case 3: native.outcome = PRUNTIME_COMMIT_UNKNOWN; native.read_failure = 1;
            expected = PRUNTIME_UNRESOLVED; break;
        case 4: native.registry.read_result = PRUNTIME_OK; expected = PRUNTIME_DENIED; break;
        case 5: native.registry.acquire_result = PRUNTIME_BUSY; expected = PRUNTIME_BUSY; break;
        case 6: native.registry.acquire_result = PRUNTIME_IO; native.registry.partial = 1; expected = PRUNTIME_IO; break;
        case 7: native.registry.read_result = PRUNTIME_IO; expected = PRUNTIME_IO; break;
        case 8: open.mode = PRUNTIME_INTENT_RESUME; native.registry.read_result = PRUNTIME_OK; break;
        case 9: open.mode = PRUNTIME_INTENT_RETRY_FAILED; native.registry.read_result = PRUNTIME_OK;
            native.registry.record.state = PRUNTIME_REG_FAILED; break;
        case 10: open.mode = PRUNTIME_INTENT_RESUME; expected = PRUNTIME_DENIED; break;
        case 11: open.mode = PRUNTIME_INTENT_RESUME; native.registry.read_result = PRUNTIME_OK;
            native.registry.record.state = PRUNTIME_REG_READY; expected = PRUNTIME_DENIED; break;
        case 12: open.mode = PRUNTIME_INTENT_RESUME; native.registry.read_result = PRUNTIME_OK;
            native.registry.record.approval.content_id[0] ^= 1; expected = PRUNTIME_INVALID; break;
        case 13: open.mode = PRUNTIME_INTENT_RETRY_FAILED; native.registry.read_result = PRUNTIME_OK;
            native.registry.record.state = PRUNTIME_REG_FAILED; native.registry.record.generation = 2147483647U;
            expected = PRUNTIME_LIMIT; break;
        case 14: expected = PRUNTIME_CANCELLED; break; /* cancel before I/O */
        case 15: expected = PRUNTIME_CANCELLED; break; /* cancel after CAS */
        case 16: expected = PRUNTIME_CANCELLED; break; /* cancel prepared intent */
        case 17: native.outcome = PRUNTIME_COMMIT_NOT_COMMITTED; expected = PRUNTIME_INVALID; break;
        case 18: native.commit = 0; native.outcome = PRUNTIME_COMMIT_UNKNOWN;
            native.write_result = PRUNTIME_IO; expected = PRUNTIME_IO; break;
        case 19: open.mode = PRUNTIME_INTENT_RESUME; native.registry.read_result = PRUNTIME_OK;
            expected = PRUNTIME_CANCELLED; break;
        default: native.outcome = 0; break; /* malformed outcome -> UNKNOWN, exact read still authoritative */
        }
        old_calls = native.calls;
        RCHECK(PRuntime_InstallIntentBegin(job, &open, &provider, &intent) == PRUNTIME_OK && native.calls == old_calls);
        native.job = intent;
        alias = job; other = NULL;
        RCHECK(PRuntime_RevisionCancel(job) == PRUNTIME_STATE && PRuntime_RevisionClose(&alias) == PRUNTIME_STATE && alias == job);
        RCHECK(PRuntime_InstallIntentBegin(job, &open, &provider, &other) == PRUNTIME_STATE && !other);
        RCHECK(PRuntime_RevisionBuildApproval(job, NULL, NULL) == PRUNTIME_STATE &&
                PRuntime_RevisionReconcileRegistration(job, NULL, NULL, NULL) == PRUNTIME_STATE);
        if (!mode) {
            thread_data.job = intent; thread_data.rc = 0;
            thread = CreateThread(NULL, 0, intent_other_thread, &thread_data, 0, NULL);
            RCHECK(thread != NULL);
            RCHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
            RCHECK(thread_data.rc);
        }
        if (mode == 14 || mode == 19) RCHECK(PRuntime_InstallIntentCancel(intent) == PRUNTIME_OK);
        rc = PRUNTIME_OK;
        for (steps = 0; steps < 20 && rc == PRUNTIME_OK; ++steps) {
            old_calls = native.calls;
            rc = PRuntime_InstallIntentStep(intent);
            RCHECK(native.calls - old_calls <= 1);
            if (mode == 15 && native.writes == 1) RCHECK(PRuntime_InstallIntentCancel(intent) == PRUNTIME_OK);
        }
        if (mode == 16 && rc == PRUNTIME_INTENT_DONE) {
            RCHECK(PRuntime_InstallIntentCancel(intent) == PRUNTIME_OK);
            do { rc = PRuntime_InstallIntentStep(intent); } while (rc == PRUNTIME_OK);
        }
        RCHECK(rc == expected);
        info.size = sizeof(info); info.version = 1;
        RCHECK(PRuntime_InstallIntentGetInfo(intent, &info) == PRUNTIME_OK);
        if (rc == PRUNTIME_INTENT_DONE) RCHECK(info.state == PRUNTIME_INTENT_PREPARED && info.record_known &&
                info.record_exists && info.record.state == PRUNTIME_REG_PREPARING);
        if (rc == PRUNTIME_UNRESOLVED) RCHECK(info.state == PRUNTIME_INTENT_UNRESOLVED && !info.record_known && info.provider_result == PRUNTIME_IO);
        if (mode == 14) RCHECK(!native.writes && !native.calls);
        if (mode == 15 || mode == 16 || mode == 19) RCHECK(info.state == PRUNTIME_INTENT_CANCELLED &&
                info.record.state == PRUNTIME_REG_CANCELLED && native.writes == (mode == 19 ? 1U : 2U));
        RCHECK(PRuntime_InstallIntentStep(intent) == PRUNTIME_STATE);
        RCHECK(PRuntime_InstallIntentClose(&intent) == PRUNTIME_OK && !intent);
        native.job = NULL;
        RCHECK(PRuntime_InstallIntentClose(&intent) == PRUNTIME_OK && !native.registry.leases && !f.bad_guard);
        if (mode == 3) {
            /* Unknown result survives close; explicit resume reads exact intent
             * without replaying the first committed write. */
            native.read_failure = 0; open.mode = PRUNTIME_INTENT_RESUME;
            RCHECK(PRuntime_InstallIntentBegin(job, &open, &provider, &intent) == PRUNTIME_OK);
            native.job = intent;
            do { rc = PRuntime_InstallIntentStep(intent); } while (rc == PRUNTIME_OK);
            RCHECK(rc == PRUNTIME_INTENT_DONE && native.writes == 1);
            RCHECK(PRuntime_InstallIntentClose(&intent) == PRUNTIME_OK && !native.registry.leases);
            native.job = NULL;
        }
    }
    revision_dispose(&f, &job); RCHECK(!f.roots && !f.hashes && !f.bad_guard);
    return TRUE;
failed:
    if (intent) PRuntime_InstallIntentClose(&intent);
    revision_dispose(&f, &job); return FALSE;
}

typedef struct InstallDataFixture {
    IntentFixture* registry;
    PRuntimeInstallDataHandle data;
    WCHAR path[MAX_PATH];
    char utf8[1024];
    unsigned int opens, closes, leases, bad_guard, partial;
    int location_error;
} InstallDataFixture;
static void install_data_guard(InstallDataFixture* f)
{
    PRuntimeInstallDataInfo info;
    PRuntimeInstallDataHandle alias;
    intent_guard(f->registry); revision_guard(f->registry->registry.revision);
    if (!f->data) return;
    info.size = sizeof(info); info.version = 1; alias = f->data;
    if (PRuntime_InstallDataGetInfo(f->data, &info) != PRUNTIME_STATE ||
            PRuntime_InstallDataCancel(f->data) != PRUNTIME_STATE ||
            PRuntime_InstallDataClose(&alias) != PRUNTIME_STATE || alias != f->data) ++f->bad_guard;
}
static int install_data_location(void* user, const char* key, PRuntimeStorageLocation* output)
{
    InstallDataFixture* f = (InstallDataFixture*)user;
    install_data_guard(f); ++f->opens;
    if (strcmp(key, "v1/installed/00112233445566778899aabbccddeeff/app.db")) return PRUNTIME_IO;
    if (!f->location_error || f->partial) {
        output->token = f; ++f->leases; strcpy(output->path, f->utf8);
        output->path_bytes = (unsigned int)strlen(f->utf8);
    }
    return f->location_error;
}
static void install_data_location_close(void* user, void* token)
{
    InstallDataFixture* f = (InstallDataFixture*)user;
    install_data_guard(f); ++f->closes;
    if (token != f || f->leases != 1) ++f->bad_guard;
    else --f->leases;
}
static int install_data_db_inspect(const char* path, unsigned int expected_rows, int expected_schema)
{
    PDbHandle db = NULL;
    PDbStmtHandle stmt = NULL;
    int count = 0, version = -1, ok = 0;
    if (PDb_OpenUtf8(path, PDB_OPEN_LOCAL_FULL_SQL, &db) != PDB_OK) return 0;
    if (PDb_GetMigrationVersion(db, &version) != PDB_OK || version != expected_schema) goto done;
    if (PDb_Prepare(db, "SELECT count(*) FROM sqlite_master WHERE name='journal'", &stmt) != PDB_OK ||
            PDb_Step(stmt) != PDB_STEP_ROW) goto done;
    count = (int)PDb_ColumnInt64(stmt, 0); PDb_Finalize(stmt); stmt = NULL;
    if (count) {
        if (PDb_Prepare(db, "SELECT count(*) FROM journal", &stmt) != PDB_OK || PDb_Step(stmt) != PDB_STEP_ROW) goto done;
        count = (int)PDb_ColumnInt64(stmt, 0);
    }
    ok = count == (int)expected_rows;
done:
    if (stmt) PDb_Finalize(stmt);
    PDb_Close(db); return ok;
}
typedef struct DataThread { PRuntimeInstallDataHandle data; int ok; } DataThread;
static DWORD WINAPI data_other_thread(void* user)
{
    DataThread* t = (DataThread*)user;
    PRuntimeInstallDataHandle alias = t->data;
    t->ok = PRuntime_InstallDataStep(t->data) == PRUNTIME_STATE &&
        PRuntime_InstallDataCancel(t->data) == PRUNTIME_STATE &&
        PRuntime_InstallDataClose(&alias) == PRUNTIME_STATE && alias == t->data;
    return 0;
}
BOOL test1380_runtime_install_data_contract(void)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"com.example.installed\",\"name\":\"Data\",\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],\"migrations\":[{\"version\":1,\"file\":\"one.sql\"},{\"version\":2,\"file\":\"two.sql\"}]}";
    static const char no_sql[] = "{\"manifestVersion\":1,\"id\":\"com.example.installed\",\"name\":\"Data\",\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[],\"migrations\":[]}";
    static const char sql1[] = "CREATE TABLE journal(n INTEGER);INSERT INTO journal VALUES(1);";
    static const char sql2[] = "INSERT INTO journal VALUES(2);";
    static const char sql_bad[] = "INSERT INTO missing_table VALUES(2);";
    RevisionFixture f;
    IntentFixture native;
    InstallDataFixture platform;
    PRuntimeRevisionProvider resource;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions revision_options;
    PRuntimeRevisionHandle job = NULL;
    PRuntimeInstallIntentHandle intent = NULL, intent_alias;
    PRuntimeInstallIntentProvider intent_provider;
    PRuntimeInstallIntentOptions intent_options;
    PRuntimeStorageProvider location;
    PRuntimeInstallDataOptions data_options;
    PRuntimeInstallDataInfo info;
    PRuntimeInstallDataHandle data = NULL, other;
    PRuntimeInstallIntentInfo intent_info;
    WCHAR module[MAX_PATH], root[MAX_PATH], *slash;
    char identity[160];
    PDbHandle fixture_db = NULL;
    DataThread thread_data;
    HANDLE thread;
    unsigned int mode, steps, injected, expected_rows;
    int rc = 0, expected, expected_schema, owned_root = 0;
    memset(&platform, 0, sizeof(platform));
    revision_error[0] = 0;
    RCHECK(GetModuleFileNameW(NULL, module, MAX_PATH) && wcslen(module) < MAX_PATH - 55);
    slash = wcsrchr(module, L'\\'); RCHECK(slash != NULL); *slash = 0;
    wsprintfW(root, L"%s\\installed-data-%lu", module, GetCurrentProcessId());
    owned_root = CreateDirectoryW(root, NULL) != 0; RCHECK(owned_root);
    memset(&intent_provider, 0, sizeof(intent_provider)); intent_provider.size = sizeof(intent_provider); intent_provider.version = 1;
    intent_provider.user_data = &native; intent_provider.acquire = intent_acquire; intent_provider.read = intent_read;
    intent_provider.compare_exchange = intent_cas; intent_provider.close = intent_close;
    memset(&intent_options, 0, sizeof(intent_options)); intent_options.size = sizeof(intent_options); intent_options.version = 1;
    intent_options.approval.size = sizeof(intent_options.approval); intent_options.approval.version = 1;
    intent_options.approval.confirmed = 1; intent_options.approval.revision = 1;
    strcpy(intent_options.approval.installation_id, "00112233445566778899aabbccddeeff");
    memset(&location, 0, sizeof(location)); location.size = sizeof(location); location.version = 1;
    location.user_data = &platform; location.open_location = install_data_location; location.close_location = install_data_location_close;
    memset(&data_options, 0, sizeof(data_options)); data_options.size = sizeof(data_options); data_options.version = 1;
    data_options.vm_budget = 1000000;
    strcpy(identity, "Positron.storage.v1:installed:com.example.installed:00112233445566778899aabbccddeeff");
    for (mode = 0; mode < 17; ++mode) {
        revision_fixture(&f, NULL); revision_tables(&f, &resource, &registry, &revision_options);
        memset(&native, 0, sizeof(native)); native.registry.revision = &f;
        native.registry.read_result = PRUNTIME_NOT_FOUND; native.commit = 1; native.outcome = PRUNTIME_COMMIT_CONFIRMED;
        memset(&platform, 0, sizeof(platform)); platform.registry = &native;
        wsprintfW(platform.path, L"%s\\case-%u.db", root, mode);
        RCHECK(WideCharToMultiByte(CP_UTF8, 0, platform.path, -1, platform.utf8, sizeof(platform.utf8), NULL, NULL));
        expected = PRUNTIME_INSTALL_DATA_DONE; expected_rows = 2; expected_schema = 2; injected = 0;
        if (mode == 1 || mode == 15) {
            f.data[0] = (const unsigned char*)(mode == 1 ? (const char*)revision_sources[0] : no_sql);
            f.actual[0] = f.declared[0] = (unsigned int)strlen((const char*)f.data[0]);
            expected_rows = 0; expected_schema = 0;
        } else {
            f.data[0] = (const unsigned char*)manifest; f.actual[0] = f.declared[0] = sizeof(manifest)-1;
            f.count = 7; strcpy(f.paths[5], "one.sql"); strcpy(f.paths[6], "two.sql");
            f.data[5] = (const unsigned char*)sql1; f.actual[5] = f.declared[5] = sizeof(sql1)-1;
            f.data[6] = (const unsigned char*)sql2; f.actual[6] = f.declared[6] = sizeof(sql2)-1;
        }
        if (mode == 2) { f.data[6] = (const unsigned char*)"\xff"; f.actual[6] = f.declared[6] = 1;
            expected = PRUNTIME_INVALID; expected_rows = 0; expected_schema = 0; }
        if (mode == 3 || mode == 13 || mode == 16) { f.data[6] = (const unsigned char*)sql_bad; f.actual[6] = f.declared[6] = sizeof(sql_bad)-1;
            expected = PRUNTIME_DATABASE; expected_rows = 1; expected_schema = 1; }
        if (mode == 5 || mode == 6 || mode == 7) {
            RCHECK(PDb_OpenUtf8(platform.utf8, PDB_OPEN_LOCAL_FULL_SQL, &fixture_db) == PDB_OK);
            if (mode == 5) RCHECK(PDb_BindLocalIdentity(fixture_db, "Positron.storage.v1:development:other", 35) == PDB_OK);
            if (mode == 6) RCHECK(PDb_Exec(fixture_db, "CREATE TABLE unclaimed(n INTEGER)") == PDB_OK);
            if (mode == 7) {
                RCHECK(PDb_BindLocalIdentity(fixture_db, identity, (int)strlen(identity)) == PDB_OK);
                RCHECK(PDb_ApplyMigration(fixture_db, 3, sql1) == PDB_OK);
            }
            PDb_Close(fixture_db); fixture_db = NULL;
            expected = PRUNTIME_SCHEMA; expected_rows = mode == 7 ? 1 : 0; expected_schema = mode == 7 ? 3 : 0;
        }
        if (mode == 8 || mode == 9) { platform.location_error = PRUNTIME_IO; platform.partial = mode == 9; expected = PRUNTIME_IO; }
        if (mode == 10 || mode == 11) { expected = PRUNTIME_CANCELLED; expected_rows = mode == 11 ? 1 : 0; expected_schema = mode == 11 ? 1 : 0; }
        if (mode == 12) { expected = PRUNTIME_IO; expected_rows = 0; expected_schema = 0; }
        if (mode == 14) expected = PRUNTIME_INVALID;
        RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &resource, &revision_options, &job) == PRUNTIME_OK);
        rc = revision_run(&f, job); RCHECK(rc == PRUNTIME_REVISION_DONE);
        intent_options.mode = PRUNTIME_INTENT_NEW;
        RCHECK(PRuntime_InstallIntentBegin(job, &intent_options, &intent_provider, &intent) == PRUNTIME_OK);
        native.job = intent;
        do { rc = PRuntime_InstallIntentStep(intent); } while (rc == PRUNTIME_OK);
        RCHECK(rc == PRUNTIME_INTENT_DONE);
        if (mode == 15) {
            RCHECK(PRuntime_InstallDataBegin(intent, &data_options, &location, &data) == PRUNTIME_DENIED && !data && !platform.opens);
        } else {
            if (mode == 14) ++native.registry.record.generation;
            RCHECK(PRuntime_InstallDataBegin(intent, &data_options, &location, &data) == PRUNTIME_OK);
            platform.data = data; intent_alias = intent; other = NULL;
            RCHECK(PRuntime_InstallIntentClose(&intent_alias) == PRUNTIME_STATE && intent_alias == intent &&
                    PRuntime_InstallIntentCancel(intent) == PRUNTIME_STATE);
            RCHECK(PRuntime_InstallDataBegin(intent, &data_options, &location, &other) == PRUNTIME_STATE && !other);
            if (!mode) {
                thread_data.data = data; thread_data.ok = 0;
                thread = CreateThread(NULL, 0, data_other_thread, &thread_data, 0, NULL);
                RCHECK(thread != NULL && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
                CloseHandle(thread); RCHECK(thread_data.ok);
            }
            if (mode == 10) RCHECK(PRuntime_InstallDataCancel(data) == PRUNTIME_OK);
            if (mode == 12) f.fail_read = 1;
            rc = PRUNTIME_OK;
            for (steps = 0; steps < 80 && rc == PRUNTIME_OK; ++steps) {
                rc = PRuntime_InstallDataStep(data);
                info.size = sizeof(info); info.version = 1;
                RCHECK(PRuntime_InstallDataGetInfo(data, &info) == PRUNTIME_OK);
                if (!injected && info.current_schema == 1 && (mode == 4 || mode == 11)) {
                    if (mode == 4) native.fail_next_read = 1;
                    else RCHECK(PRuntime_InstallDataCancel(data) == PRUNTIME_OK);
                    injected = 1;
                }
                if (mode == 13 && info.error.result != PRUNTIME_OK) native.read_failure = 1;
                if (mode == 16 && info.error.result != PRUNTIME_OK) {
                    native.commit = 0; native.outcome = PRUNTIME_COMMIT_NOT_COMMITTED; native.write_result = PRUNTIME_BUSY;
                }
            }
            RCHECK(rc == (mode == 4 ? PRUNTIME_IO : expected));
            if (mode == 13) RCHECK(info.error.result == PRUNTIME_DATABASE && info.error.failed_schema_version == 2 &&
                    info.registration_result == PRUNTIME_UNRESOLVED);
            if (mode == 16) RCHECK(info.error.result == PRUNTIME_DATABASE && info.error.failed_schema_version == 2 &&
                    info.registration_result == PRUNTIME_BUSY && native.registry.record.state == PRUNTIME_REG_PREPARING);
            RCHECK(PRuntime_InstallDataClose(&data) == PRUNTIME_OK && !data);
            platform.data = NULL;
            RCHECK(!platform.leases && !platform.bad_guard && !f.bad_guard && !f.hashes &&
                    platform.closes == (platform.opens && (!platform.location_error || platform.partial) ? 1U : 0U));
            RCHECK(PRuntime_InstallDataClose(&data) == PRUNTIME_OK);
            if (mode == 4) {
                RCHECK(native.registry.record.state == PRUNTIME_REG_FAILED);
                RCHECK(PRuntime_InstallIntentClose(&intent) == PRUNTIME_OK); native.job = NULL;
                intent_options.mode = PRUNTIME_INTENT_RETRY_FAILED;
                RCHECK(PRuntime_InstallIntentBegin(job, &intent_options, &intent_provider, &intent) == PRUNTIME_OK); native.job = intent;
                do { rc = PRuntime_InstallIntentStep(intent); } while (rc == PRUNTIME_OK);
                RCHECK(rc == PRUNTIME_INTENT_DONE);
                RCHECK(PRuntime_InstallDataBegin(intent, &data_options, &location, &data) == PRUNTIME_OK); platform.data = data;
                do { rc = PRuntime_InstallDataStep(data); } while (rc == PRUNTIME_OK);
                info.size = sizeof(info); info.version = 1;
                RCHECK(rc == PRUNTIME_INSTALL_DATA_DONE && PRuntime_InstallDataGetInfo(data, &info) == PRUNTIME_OK &&
                        info.previous_schema == 1 && info.current_schema == 2 && info.applied_files == 1);
                RCHECK(PRuntime_InstallDataClose(&data) == PRUNTIME_OK); platform.data = NULL;
            }
            if (mode != 8 && mode != 9 && mode != 10 && mode != 14)
                RCHECK(install_data_db_inspect(platform.utf8, expected_rows, expected_schema));
            else RCHECK(GetFileAttributesW(platform.path) == 0xffffffffUL);
            intent_info.size = sizeof(intent_info); intent_info.version = 1;
            RCHECK(PRuntime_InstallIntentGetInfo(intent, &intent_info) == PRUNTIME_OK &&
                    native.registry.record.state != PRUNTIME_REG_READY);
        }
        RCHECK(PRuntime_InstallIntentClose(&intent) == PRUNTIME_OK); native.job = NULL;
        revision_dispose(&f, &job); RCHECK(!f.roots && !native.registry.leases);
        if (GetFileAttributesW(platform.path) != 0xffffffffUL) RCHECK(DeleteFileW(platform.path));
    }
    RCHECK(RemoveDirectoryW(root)); return TRUE;
failed:
    if (fixture_db) PDb_Close(fixture_db);
    if (data) PRuntime_InstallDataClose(&data);
    platform.data = NULL;
    if (intent) PRuntime_InstallIntentClose(&intent);
    revision_dispose(&f, &job);
    /* Failed fixture files intentionally remain in the isolated deployment. */
    (void)owned_root; return FALSE;
}

static unsigned char* existing_file_bytes(const WCHAR* path, DWORD* size)
{
    HANDLE file;
    unsigned char* bytes;
    DWORD got;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(file, NULL);
    if (*size > 1024U * 1024U) { CloseHandle(file); return NULL; }
    bytes = (unsigned char*)malloc(*size ? *size : 1);
    if (!bytes || !ReadFile(file, bytes, *size, &got, NULL) || got != *size) {
        free(bytes); bytes = NULL;
    }
    CloseHandle(file); return bytes;
}
static BOOL existing_db_cases(int writable)
{
    static const char identity[] = "existing.local.identity";
    WCHAR module[MAX_PATH], root[MAX_PATH], path[MAX_PATH], journal[MAX_PATH], *slash;
    char utf8[1024];
    PDbHandle db = NULL;
    PDbExistingLocalOptions options;
    PDbErrorInfo error, canary;
    unsigned char *before = NULL, *after = NULL;
    DWORD before_size, after_size, written;
    HANDLE file;
    unsigned int mode, repeat;
    int rc = 0, expected, owned_root = 0;
    revision_error[0] = 0;
    RCHECK(GetModuleFileNameW(NULL, module, MAX_PATH) && wcslen(module) < MAX_PATH - 55);
    slash = wcsrchr(module, L'\\'); RCHECK(slash != NULL); *slash = 0;
    wsprintfW(root, L"%s\\existing-db-%lu", module, GetCurrentProcessId());
    owned_root = CreateDirectoryW(root, NULL) != 0; RCHECK(owned_root);
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
    options.identity = identity; options.identity_bytes = sizeof(identity)-1;
    for (mode = 0; mode < 17; ++mode) {
        wsprintfW(path, L"%s\\case-%u.db", root, mode); wsprintfW(journal, L"%s-journal", path);
        RCHECK(WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL));
        options.expected_schema = mode == 3 ? 1 : 0;
        expected = mode == 0 || mode == 14 ? PDB_ERROR :
                (mode == 2 || mode == 3 || mode == 16 ? PDB_OK : PDB_SCHEMA_MISMATCH);
        if (writable && mode == 16) expected = PDB_ERROR;
        if (mode == 1 || mode == 14) {
            file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
            RCHECK(file != INVALID_HANDLE_VALUE);
            if (mode == 14) RCHECK(WriteFile(file, "not SQLite", 10, &written, NULL) && written == 10);
            CloseHandle(file);
        } else if (mode) {
            RCHECK(PDb_OpenUtf8(utf8, PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK);
            if (mode != 5 && mode != 6) RCHECK(PDb_BindLocalIdentity(db,
                    mode == 4 ? "wrong" : identity, mode == 4 ? 5 : sizeof(identity)-1) == PDB_OK);
            if (mode == 3 || mode == 7 || mode == 8) RCHECK(PDb_ApplyMigration(db, 1, "CREATE TABLE sample(n INTEGER)") == PDB_OK);
            if (mode == 6) RCHECK(PDb_Exec(db, "CREATE TABLE unclaimed(n INTEGER)") == PDB_OK);
            if (mode == 8) RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value='bad' WHERE key='schema_version'") == PDB_OK);
            if (mode == 9) RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value=X'6578697374696e67' WHERE key='local_identity'") == PDB_OK);
            if (mode == 10) RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value=CAST(X'6578697374696e672e6c6f63616c2e6964656e746974790058' AS TEXT) WHERE key='local_identity'") == PDB_OK);
            if (mode == 11) RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value=X'31' WHERE key='mode'") == PDB_OK);
            if (mode == 12) RCHECK(PDb_Exec(db, "DROP TABLE __pdb_meta;CREATE VIEW __pdb_meta AS SELECT 'mode' AS key,'1' AS value") == PDB_OK);
            if (mode == 13) RCHECK(PDb_Exec(db, "DROP TABLE __pdb_meta;CREATE TABLE __pdb_meta(key TEXT,value TEXT)") == PDB_OK);
            if (mode == 15) RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value='01' WHERE key='mode'") == PDB_OK);
            PDb_Close(db); db = NULL;
        }
        if (mode) { before = existing_file_bytes(path, &before_size); RCHECK(before != NULL); }
        if (mode == 16) RCHECK(SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY));
        for (repeat = 0; repeat < 2; ++repeat) {
            error.size = sizeof(error); error.version = PDB_ERROR_INFO_VERSION;
            rc = writable ? PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &db, &error) :
                    PDb_VerifyExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &error);
            if (writable) {
                RCHECK((rc == PDB_OK) == (db != NULL));
                if (db) { PDb_Close(db); db = NULL; }
            }
            RCHECK(rc == expected && error.result == expected);
        }
        RCHECK(GetFileAttributesW(journal) == 0xffffffffUL);
        if (mode) {
            after = existing_file_bytes(path, &after_size);
            RCHECK(after != NULL && before_size == after_size && !memcmp(before, after, before_size));
            free(before); before = NULL; free(after); after = NULL;
            if (mode == 16) RCHECK(SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL));
            RCHECK(DeleteFileW(path));
        } else RCHECK(GetFileAttributesW(path) == 0xffffffffUL);
    }
    memset(&error, 0xa5, sizeof(error)); error.size = sizeof(error); error.version = 99; canary = error;
    RCHECK(PDb_VerifyExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &error) == PDB_INVALID_ARGUMENT &&
            !memcmp(&error, &canary, sizeof(error)));
    RCHECK(PDb_VerifyExistingLocalUtf8(":memory:", 8, &options, NULL) == PDB_INVALID_ARGUMENT);
    RCHECK(PDb_VerifyExistingLocalUtf8(utf8, 1024, &options, NULL) == PDB_INVALID_ARGUMENT);
    options.version = 2;
    RCHECK(PDb_VerifyExistingLocalUtf8(utf8, (int)strlen(utf8), &options, NULL) == PDB_INVALID_ARGUMENT);
    RCHECK(RemoveDirectoryW(root)); return TRUE;
failed:
    if (db) PDb_Close(db);
    free(before); free(after); (void)owned_root; return FALSE;
}

BOOL test1381_db_existing_verification(void)
{
    return existing_db_cases(0);
}
BOOL test1382_db_existing_writable(void)
{
    WCHAR module[MAX_PATH], path[MAX_PATH], *slash;
    char utf8[1024];
    PDbHandle db = NULL, admitted = NULL;
    PDbStmtHandle query = NULL;
    PDbExistingLocalOptions options;
    PDbConnectionState state;
    PDbErrorInfo error, canary;
    PDbRestrictedOptions restricted;
    PDbRestrictedReport report;
    PDbRestrictedStatement statement;
    int rc = 0;
    RCHECK(existing_db_cases(1));
    RCHECK(GetModuleFileNameW(NULL, module, MAX_PATH) && wcslen(module) < MAX_PATH - 50);
    slash = wcsrchr(module, L'\\'); RCHECK(slash != NULL); *slash = 0;
    wsprintfW(path, L"%s\\writable-existing-%lu.db", module, GetCurrentProcessId());
    RCHECK(GetFileAttributesW(path) == 0xffffffffUL);
    RCHECK(WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL));
    options.size = sizeof(options); options.version = 1;
    options.identity = "writable.identity"; options.identity_bytes = 17; options.expected_schema = 1;
    RCHECK(PDb_OpenUtf8(utf8, PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK);
    RCHECK(PDb_BindLocalIdentity(db, options.identity, options.identity_bytes) == PDB_OK);
    RCHECK(PDb_ApplyMigration(db, 1, "CREATE TABLE sample(n INTEGER)") == PDB_OK);
    RCHECK(PDb_Exec(db, "BEGIN IMMEDIATE") == PDB_OK);
    error.size = sizeof(error); error.version = PDB_ERROR_INFO_VERSION;
    rc = PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &admitted, &error);
    RCHECK(rc == PDB_BUSY && !admitted && error.result == PDB_BUSY);
    RCHECK(PDb_Rollback(db) == PDB_OK);
    /* A change committed after an earlier probe must be checked on the actual
     * connection, never inherited from that probe's result. */
    RCHECK(PDb_VerifyExistingLocalUtf8(utf8, (int)strlen(utf8), &options, NULL) == PDB_OK);
    RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value='other' WHERE key='local_identity'") == PDB_OK);
    RCHECK(PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &admitted, NULL) == PDB_SCHEMA_MISMATCH && !admitted);
    RCHECK(PDb_Exec(db, "UPDATE __pdb_meta SET value='writable.identity' WHERE key='local_identity'") == PDB_OK);
    PDb_Close(db); db = NULL;
    RCHECK(PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &admitted, &error) == PDB_OK && admitted);
    state.size = sizeof(state); state.version = PDB_CONNECTION_STATE_VERSION;
    RCHECK(PDb_GetConnectionState(admitted, &state) == PDB_OK && !state.transaction_active && !state.statement_count);
    memset(&restricted, 0, sizeof(restricted)); restricted.size = sizeof(restricted); restricted.version = PDB_RESTRICTED_VERSION;
    restricted.vm_budget = PDB_RESTRICTED_VM_MAX;
    memset(&report, 0, sizeof(report)); report.size = sizeof(report); report.version = PDB_RESTRICTED_VERSION;
    memset(&statement, 0, sizeof(statement)); statement.sql = "INSERT INTO sample VALUES(42)";
    statement.sql_bytes = (int)strlen(statement.sql); statement.kind = PDB_RESTRICTED_EXECUTE;
    rc = PDb_ExecuteRestricted(admitted, &statement, 1, &restricted, &report);
    if (rc != PDB_OK || report.changes != 1) {
        error.size = sizeof(error); error.version = PDB_ERROR_INFO_VERSION;
        PDb_GetErrorInfo(admitted, &error);
        sprintf(revision_error, "Existing write rc=%d changes=%d sqlite=%d category=%d: %.100s",
                rc, (int)report.changes, error.sqlite_extended_code, error.category, error.message);
        goto failed;
    }
    PDb_Close(admitted); admitted = NULL;
    RCHECK(PDb_VerifyExistingLocalUtf8(utf8, (int)strlen(utf8), &options, NULL) == PDB_OK);
    RCHECK(PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &admitted, NULL) == PDB_OK);
    RCHECK(PDb_Prepare(admitted, "SELECT n FROM sample", &query) == PDB_OK);
    RCHECK(PDb_Step(query) == PDB_STEP_ROW && PDb_ColumnInt64(query, 0) == 42);
    RCHECK(PDb_Step(query) == PDB_STEP_DONE);
    PDb_Finalize(query); query = NULL;
    PDb_Close(admitted); admitted = NULL;
    memset(&error, 0xa5, sizeof(error)); error.size = sizeof(error); error.version = 99; canary = error;
    RCHECK(PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, &admitted, &error) == PDB_INVALID_ARGUMENT &&
            !admitted && !memcmp(&error, &canary, sizeof(error)));
    RCHECK(PDb_OpenExistingLocalUtf8(utf8, (int)strlen(utf8), &options, NULL, NULL) == PDB_INVALID_ARGUMENT);
    RCHECK(DeleteFileW(path)); return TRUE;
failed:
    if (query) PDb_Finalize(query);
    if (db) PDb_Close(db);
    if (admitted) PDb_Close(admitted);
    return FALSE;
}

typedef struct ReadyStorageFixture {
    RevisionFixture* revision;
    WCHAR path[MAX_PATH];
    char utf8[1024];
    unsigned int opens, closes, leases, partial, changed;
} ReadyStorageFixture;
static int ready_storage_location(void* user, const char* key, PRuntimeStorageLocation* output)
{
    ReadyStorageFixture* f = (ReadyStorageFixture*)user;
    revision_guard(f->revision); ++f->opens;
    if (strcmp(key, "v1/installed/00112233445566778899aabbccddeeff/app.db")) return PRUNTIME_IO;
    output->token = f; ++f->leases; strcpy(output->path, f->utf8);
    output->path_bytes = (unsigned int)strlen(f->utf8);
    if (f->changed) ++f->revision->record.generation;
    return f->partial ? PRUNTIME_IO : PRUNTIME_OK;
}
static void ready_storage_close(void* user, void* token)
{
    ReadyStorageFixture* f = (ReadyStorageFixture*)user;
    revision_guard(f->revision); ++f->closes;
    if (token != f || f->leases != 1) ++f->revision->bad_guard;
    else --f->leases;
}
typedef struct ReadyStorageThread { PRuntimeStorageHandle storage; int ok; } ReadyStorageThread;
static DWORD WINAPI ready_storage_thread(void* user)
{
    ReadyStorageThread* t = (ReadyStorageThread*)user;
    PRuntimeStorageHandle alias = t->storage;
    PRuntimeStorageInfo info;
    info.size = sizeof(info); info.version = 1;
    t->ok = PRuntime_StorageClose(&alias) == PRUNTIME_STATE && alias == t->storage &&
            PRuntime_StorageGetInfo(t->storage, &info) == PRUNTIME_STATE;
    return 0;
}
static void ready_storage_migrated_fixture(RevisionFixture* f)
{
    static const char manifest[] = "{\"manifestVersion\":1,\"id\":\"com.example.installed\",\"name\":\"Installed fixture\",\"version\":1,\"runtimeApiVersion\":1,\"entry\":\"index.html\",\"permissions\":[\"storage.sql\"],\"migrations\":[{\"version\":1,\"file\":\"one.sql\"}]}";
    static const char sql[] = "CREATE TABLE sample(n INTEGER)";
    f->count = 6; f->data[0] = (const unsigned char*)manifest;
    f->actual[0] = f->declared[0] = sizeof(manifest)-1;
    strcpy(f->paths[5], "one.sql"); f->data[5] = (const unsigned char*)sql;
    f->actual[5] = f->declared[5] = sizeof(sql)-1;
}
BOOL test1383_runtime_ready_storage(void)
{
    static const char identity[] = "Positron.storage.v1:installed:com.example.installed:00112233445566778899aabbccddeeff";
    static const char insert[] = "{\"version\":1,\"method\":\"execute\",\"sql\":\"INSERT INTO sample VALUES(42)\",\"params\":[]}";
    static const char select[] = "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT n FROM sample\",\"params\":[]}";
    RevisionFixture f;
    ReadyStorageFixture platform;
    PRuntimeRevisionProvider resources;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions revision_options;
    PRuntimeRevisionHandle job = NULL, alias;
    PRuntimeApprovalOptions approval;
    PRuntimeApprovalSnapshot snapshot, migrated_snapshot;
    PRuntimeInstalledStorageProvider provider;
    PRuntimeStorageOptions options;
    PRuntimeStorageHandle storage = NULL, other;
    PRuntimeStorageInfo info;
    PRuntimeStorageError error;
    PRuntimeServiceQueueOptions queue_options;
    PRuntimeServiceQueueHandle queue = NULL;
    PDbHandle db = NULL;
    ReadyStorageThread thread_data;
    HANDLE thread;
    WCHAR module[MAX_PATH], root[MAX_PATH], *slash;
    char* output = NULL;
    unsigned char *before = NULL, *after = NULL;
    DWORD before_size, after_size;
    unsigned int mode, written;
    int rc = 0, expected;
    revision_error[0] = 0;
    memset(&f, 0, sizeof(f)); memset(&platform, 0, sizeof(platform));
    RCHECK(GetModuleFileNameW(NULL, module, MAX_PATH) && wcslen(module) < MAX_PATH-55);
    slash = wcsrchr(module, L'\\'); RCHECK(slash != NULL); *slash = 0;
    wsprintfW(root, L"%s\\ready-storage-%lu", module, GetCurrentProcessId());
    RCHECK(CreateDirectoryW(root, NULL));
    output = (char*)malloc(4097); RCHECK(output != NULL);
    revision_fixture(&f, NULL); revision_tables(&f, &resources, &registry, &revision_options);
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &resources, &revision_options, &job) == PRUNTIME_OK);
    RCHECK(revision_run(&f, job) == PRUNTIME_REVISION_DONE);
    memset(&approval, 0, sizeof(approval)); approval.size = sizeof(approval); approval.version = 1;
    approval.revision = approval.confirmed = 1; strcpy(approval.installation_id, "00112233445566778899aabbccddeeff");
    snapshot.size = sizeof(snapshot); snapshot.version = 1;
    RCHECK(PRuntime_RevisionBuildApproval(job, &approval, &snapshot) == PRUNTIME_OK);
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
    options.granted_permissions = PRUNTIME_PERMISSION_SQL; options.vm_budget = 1000000;
    memset(&provider, 0, sizeof(provider)); provider.size = sizeof(provider); provider.version = 1;
    provider.user_data = &platform; provider.open_existing_location = ready_storage_location; provider.close_location = ready_storage_close;
    error.size = sizeof(error); error.version = 1;
    other = (PRuntimeStorageHandle)1;
    RCHECK(PRuntime_InstalledStorageOpen(job, &options, &provider, &other, &error) == PRUNTIME_DENIED &&
            other == (PRuntimeStorageHandle)1 && !platform.opens);
    revision_dispose(&f, &job);
    revision_fixture(&f, NULL); ready_storage_migrated_fixture(&f);
    revision_tables(&f, &resources, &registry, &revision_options);
    RCHECK(PRuntime_PrepareRevision("\\revision-root", 14, &resources, &revision_options, &job) == PRUNTIME_OK);
    RCHECK(revision_run(&f, job) == PRUNTIME_REVISION_DONE);
    migrated_snapshot.size = sizeof(migrated_snapshot); migrated_snapshot.version = 1;
    RCHECK(PRuntime_RevisionBuildApproval(job, &approval, &migrated_snapshot) == PRUNTIME_OK);
    revision_dispose(&f, &job);
    for (mode = 0; mode < 14; ++mode) {
        revision_fixture(&f, mode == 13 ? &migrated_snapshot : &snapshot);
        if (mode == 13) ready_storage_migrated_fixture(&f);
        revision_tables(&f, &resources, &registry, &revision_options);
        memset(&platform, 0, sizeof(platform)); platform.revision = &f;
        wsprintfW(platform.path, L"%s\\case-%u.db", root, mode);
        RCHECK(WideCharToMultiByte(CP_UTF8, 0, platform.path, -1, platform.utf8, sizeof(platform.utf8), NULL, NULL));
        if (mode != 1) {
            RCHECK(PDb_OpenUtf8(platform.utf8, PDB_OPEN_LOCAL_FULL_SQL, &db) == PDB_OK);
            if (mode != 3) RCHECK(PDb_BindLocalIdentity(db, mode == 2 ? "wrong" : identity,
                    mode == 2 ? 5 : sizeof(identity)-1) == PDB_OK);
            if (mode == 13) RCHECK(PDb_ApplyMigration(db, 1, "CREATE TABLE sample(n INTEGER)") == PDB_OK);
            else RCHECK(PDb_Exec(db, "CREATE TABLE sample(n INTEGER)") == PDB_OK);
            if (mode == 4) RCHECK(PDb_ApplyMigration(db, 1, "INSERT INTO sample VALUES(99)") == PDB_OK);
            PDb_Close(db); db = NULL;
            before = existing_file_bytes(platform.path, &before_size); RCHECK(before != NULL);
        }
        if (mode == 5) RCHECK(SetFileAttributesW(platform.path, FILE_ATTRIBUTE_READONLY));
        RCHECK(PRuntime_OpenInstalled(snapshot.installation_id, 32, 7, &registry, &resources, &revision_options, &job) == PRUNTIME_OK);
        RCHECK(revision_run(&f, job) == PRUNTIME_REVISION_DONE);
        if (mode == 6) platform.partial = 1;
        if (mode == 7) platform.changed = 1;
        if (mode == 8) f.record.active = 0;
        if (mode == 10) f.cancel = 1;
        expected = mode == 1 || mode == 5 ? PRUNTIME_DATABASE :
                mode == 2 || mode == 3 || mode == 4 ? PRUNTIME_SCHEMA :
                mode == 6 ? PRUNTIME_IO : mode == 7 ? PRUNTIME_INVALID :
                mode == 8 ? PRUNTIME_DENIED : mode == 10 ? PRUNTIME_CANCELLED : PRUNTIME_OK;
        other = (PRuntimeStorageHandle)1;
        if (mode == 0) {
            options.version = 2;
            RCHECK(PRuntime_InstalledStorageOpen(job, &options, &provider, &other, &error) == PRUNTIME_ARGUMENT &&
                    other == (PRuntimeStorageHandle)1 && !platform.opens);
            options.version = 1; options.granted_permissions = 0;
            RCHECK(PRuntime_InstalledStorageOpen(job, &options, &provider, &other, &error) == PRUNTIME_DENIED && !platform.opens);
            options.granted_permissions = PRUNTIME_PERMISSION_SQL;
        }
        rc = PRuntime_InstalledStorageOpen(job, &options, &provider, &other, &error);
        if (rc != expected) {
            sprintf(revision_error, "READY mode=%u rc=%d expected=%d DB=%d sqlite=%d %.70s", mode, rc, expected,
                    error.db_result, error.sqlite_extended_code, error.message); goto failed;
        }
        if (rc == PRUNTIME_OK) {
            storage = other; f.storage = storage;
            RCHECK(PRuntime_InstalledStorageOpen(job, &options, &provider, &other, NULL) == PRUNTIME_STATE && other == storage);
            alias = job; RCHECK(PRuntime_RevisionClose(&alias) == PRUNTIME_STATE && alias == job && PRuntime_RevisionCancel(job) == PRUNTIME_STATE);
            thread_data.storage = storage; thread_data.ok = 0;
            thread = CreateThread(NULL, 0, ready_storage_thread, &thread_data, 0, NULL); RCHECK(thread != NULL);
            RCHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread); RCHECK(thread_data.ok);
            memset(&queue_options, 0, sizeof(queue_options)); queue_options.size = sizeof(queue_options); queue_options.version = 1;
            queue_options.tab_id = queue_options.page_generation = 1; queue_options.delivery_thread_id = GetCurrentThreadId();
            RCHECK(PRuntime_ServiceQueueCreate(storage, &queue_options, &queue) == PRUNTIME_OK && queue);
            RCHECK(PRuntime_ServiceQueueRevoke(queue) == PRUNTIME_OK && PRuntime_ServiceQueueClose(&queue) == PRUNTIME_OK && !queue);
            if (mode == 9) f.record.active = 0;
            if (mode == 11) ++f.record.generation;
            if (mode == 12) f.cancel = 1;
            memset(output, 0x5a, 4097); written = 123;
            rc = PRuntime_StorageExecuteJson(storage, insert, sizeof(insert)-1, output, 4097, &written);
            RCHECK(rc == (mode == 9 ? PRUNTIME_DENIED : mode == 11 ? PRUNTIME_INVALID : mode == 12 ? PRUNTIME_CANCELLED : PRUNTIME_OK));
            if (rc != PRUNTIME_OK) RCHECK(output[0] == 0x5a && written == 123);
            else {
                RCHECK(strstr(output, "\"value\":\"1\"") != NULL);
                RCHECK(PRuntime_StorageExecuteJson(storage, select, sizeof(select)-1, output, 4097, &written) == PRUNTIME_OK &&
                        strstr(output, "\"value\":\"42\"") != NULL);
            }
            info.size = sizeof(info); info.version = 1;
            RCHECK(PRuntime_StorageGetInfo(storage, &info) == PRUNTIME_OK && info.schema_version == (mode == 13 ? 1U : 0U) &&
                    info.state == (mode == 9 || mode == 11 || mode == 12 ? PRUNTIME_STORAGE_FAILED : PRUNTIME_STORAGE_READY));
            RCHECK(PRuntime_StorageClose(&storage) == PRUNTIME_OK && !storage); f.storage = NULL;
            RCHECK(PRuntime_StorageClose(&storage) == PRUNTIME_OK);
        } else RCHECK(other == (PRuntimeStorageHandle)1 && error.result == rc);
        RCHECK(!platform.leases && !f.bad_guard && platform.opens == platform.closes);
        if (mode != 1 && mode != 0 && mode != 13) {
            after = existing_file_bytes(platform.path, &after_size);
            RCHECK(after && before_size == after_size && !memcmp(before, after, before_size));
            free(after); after = NULL;
        }
        free(before); before = NULL;
        revision_dispose(&f, &job); RCHECK(!f.roots && !f.leases && !f.bad_guard);
        if (mode == 5) RCHECK(SetFileAttributesW(platform.path, FILE_ATTRIBUTE_NORMAL));
        if (mode == 1) RCHECK(GetFileAttributesW(platform.path) == 0xffffffffUL);
        else RCHECK(DeleteFileW(platform.path));
    }
    RCHECK(RemoveDirectoryW(root)); free(output); return TRUE;
failed:
    if (db) PDb_Close(db);
    if (storage) PRuntime_StorageClose(&storage);
    f.storage = NULL;
    revision_dispose(&f, &job); free(before); free(after); free(output); return FALSE;
}

typedef struct InstalledQueueFixture {
    RevisionFixture revision;
    ReadyStorageFixture location;
    PRuntimeRevisionHandle parent;
    PRuntimeStorageHandle storage;
    PRuntimeServiceQueueHandle queue, side;
    PRuntimeServiceQueueOptions options;
    HANDLE ready, work, done, inside, resume, thread, session;
    volatile LONG action, pause, instance_cancel;
    int result, stored_rows;
    unsigned int processed, accepted, cancelled;
    PBrowserServiceToken token;
} InstalledQueueFixture;
static int installed_queue_cancelled(void* user)
{
    InstalledQueueFixture* f = (InstalledQueueFixture*)user;
    if (InterlockedExchange((LONG*)&f->pause, 0)) {
        SetEvent(f->inside);
        if (WaitForSingleObject(f->resume, 10000) != WAIT_OBJECT_0) return 1;
    }
    return InterlockedCompareExchange((LONG*)&f->instance_cancel, 0, 0) != 0;
}
static DWORD WINAPI installed_queue_worker(void* user)
{
    static const char identity[] = "Positron.storage.v1:installed:com.example.installed:00112233445566778899aabbccddeeff";
    InstalledQueueFixture* f = (InstalledQueueFixture*)user;
    PRuntimeRevisionProvider resources;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions revision_options;
    PRuntimeApprovalOptions approval;
    PRuntimeApprovalSnapshot snapshot;
    PRuntimeInstalledStorageProvider provider;
    PRuntimeStorageOptions options;
    PRuntimeStorageError error;
    PRuntimeStorageInfo info;
    PRuntimeStorageHandle storage_alias;
    PRuntimeRevisionHandle parent_alias;
    PRuntimeServiceQueueHandle extra[3];
    PRuntimeServiceQueueOptions queue_options;
    PDbExistingLocalOptions db_options;
    PDbHandle db = NULL;
    PDbStmtHandle stmt = NULL;
    unsigned int i;
    LONG action;
    int rc = 0;
#define IQWORK(expr) do { if (!(expr)) { f->result = rc ? rc : PRUNTIME_STATE; goto finish; } } while (0)
    memset(extra, 0, sizeof(extra));
    revision_fixture(&f->revision, NULL); revision_tables(&f->revision, &resources, &registry, &revision_options);
    rc = PRuntime_PrepareRevision("\\revision-root", 14, &resources, &revision_options, &f->parent); IQWORK(rc == 0);
    rc = revision_run(&f->revision, f->parent); IQWORK(rc == PRUNTIME_REVISION_DONE); rc = 0;
    memset(&approval, 0, sizeof(approval)); approval.size = sizeof(approval); approval.version = 1;
    approval.revision = approval.confirmed = 1; strcpy(approval.installation_id, "00112233445566778899aabbccddeeff");
    snapshot.size = sizeof(snapshot); snapshot.version = 1;
    rc = PRuntime_RevisionBuildApproval(f->parent, &approval, &snapshot); IQWORK(rc == 0);
    revision_dispose(&f->revision, &f->parent);
    rc = PDb_OpenUtf8(f->location.utf8, PDB_OPEN_LOCAL_FULL_SQL, &db); IQWORK(rc == 0);
    rc = PDb_BindLocalIdentity(db, identity, sizeof(identity)-1); IQWORK(rc == 0);
    rc = PDb_Exec(db, "CREATE TABLE sample(n INTEGER)"); IQWORK(rc == 0);
    PDb_Close(db); db = NULL;
    revision_fixture(&f->revision, &snapshot); revision_tables(&f->revision, &resources, &registry, &revision_options);
    f->location.revision = &f->revision;
    rc = PRuntime_OpenInstalled(snapshot.installation_id, 32, 7, &registry, &resources, &revision_options, &f->parent); IQWORK(rc == 0);
    rc = revision_run(&f->revision, f->parent); IQWORK(rc == PRUNTIME_REVISION_DONE); rc = 0;
    memset(&provider, 0, sizeof(provider)); provider.size = sizeof(provider); provider.version = 1;
    provider.user_data = &f->location; provider.open_existing_location = ready_storage_location; provider.close_location = ready_storage_close;
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
    options.granted_permissions = 1; options.vm_budget = 1000000; options.cancelled = installed_queue_cancelled; options.user_data = f;
    error.size = sizeof(error); error.version = 1;
    rc = PRuntime_InstalledStorageOpen(f->parent, &options, &provider, &f->storage, &error); IQWORK(rc == 0);
    f->revision.storage = f->storage;
    rc = PRuntime_ServiceQueueCreate(f->storage, &f->options, &f->queue); IQWORK(rc == 0);
    f->revision.guarded_queue = f->queue;
    queue_options = f->options; ++queue_options.page_generation;
    rc = PRuntime_ServiceQueueCreate(f->storage, &queue_options, &f->side); IQWORK(rc == 0);
    memset(extra, 0, sizeof(extra));
    IQWORK(PRuntime_ServiceQueueCreate(f->storage, &f->options, &extra[2]) == PRUNTIME_STATE && !extra[2]);
    for (i = 0; i < 2; ++i) { ++queue_options.page_generation;
        rc = PRuntime_ServiceQueueCreate(f->storage, &queue_options, &extra[i]); IQWORK(rc == 0); }
    ++queue_options.page_generation;
    IQWORK(PRuntime_ServiceQueueCreate(f->storage, &queue_options, &extra[2]) == PRUNTIME_LIMIT && !extra[2]);
    for (i = 0; i < 2; ++i) { PRuntime_ServiceQueueRevoke(extra[i]); IQWORK(PRuntime_ServiceQueueClose(&extra[i]) == 0); }
    storage_alias = f->storage; parent_alias = f->parent;
    IQWORK(PRuntime_StorageClose(&storage_alias) == PRUNTIME_STATE && storage_alias == f->storage &&
            PRuntime_RevisionClose(&parent_alias) == PRUNTIME_STATE && parent_alias == f->parent);
    SetEvent(f->ready);
    for (;;) {
        IQWORK(WaitForSingleObject(f->work, 30000) == WAIT_OBJECT_0);
        action = InterlockedExchange((LONG*)&f->action, 0); if (action == 9) break;
        if (action == 2) f->revision.record.active = 0;
        if (action == 4) InterlockedExchange((LONG*)&f->instance_cancel, 1);
        if (action == 1 || action == 2 || action == 4 || action == 5)
            f->result = PRuntime_ServiceQueueProcessOne(action == 5 ? f->side : f->queue, &f->processed);
        else if (action == 3 || action == 7) {
            if (action == 3) ++f->revision.record.generation;
            else InterlockedExchange((LONG*)&f->instance_cancel, 1);
            extra[0] = NULL;
            f->result = PRuntime_ServiceQueueCreate(f->storage, &queue_options, &extra[0]);
            if (extra[0]) { PRuntime_ServiceQueueRevoke(extra[0]); PRuntime_ServiceQueueClose(&extra[0]); f->result = PRUNTIME_STATE; }
        } else if (action == 6) {
            info.size = sizeof(info); info.version = 1;
            f->result = PRuntime_StorageGetInfo(f->storage, &info);
            if (!f->result && info.state != PRUNTIME_STORAGE_READY) f->result = PRUNTIME_STATE;
        } else f->result = PRUNTIME_ARGUMENT;
        SetEvent(f->done);
    }
finish:
    f->revision.guarded_queue = NULL;
    SetEvent(f->ready);
    for (i = 0; i < 3; ++i) if (extra[i]) { PRuntime_ServiceQueueRevoke(extra[i]); PRuntime_ServiceQueueClose(&extra[i]); }
    if (f->queue) { PRuntime_ServiceQueueRevoke(f->queue); PRuntime_ServiceQueueClose(&f->queue); }
    if (f->side) { PRuntime_ServiceQueueRevoke(f->side); PRuntime_ServiceQueueClose(&f->side); }
    PRuntime_StorageClose(&f->storage); f->revision.storage = NULL;
    if (db) { PDb_Close(db); db = NULL; }
    db_options.size = sizeof(db_options); db_options.version = 1; db_options.identity = identity;
    db_options.identity_bytes = sizeof(identity)-1; db_options.expected_schema = 0;
    if (PDb_OpenExistingLocalUtf8(f->location.utf8, (int)strlen(f->location.utf8), &db_options, &db, NULL) == 0 &&
            PDb_Prepare(db, "SELECT count(*) FROM sample", &stmt) == 0 && PDb_Step(stmt) == PDB_STEP_ROW)
        f->stored_rows = (int)PDb_ColumnInt64(stmt, 0);
    if (stmt) PDb_Finalize(stmt); if (db) PDb_Close(db);
    revision_dispose(&f->revision, &f->parent); SetEvent(f->done); return 0;
#undef IQWORK
}
static int installed_queue_action(InstalledQueueFixture* f, LONG action)
{
    ResetEvent(f->done); InterlockedExchange((LONG*)&f->action, action); SetEvent(f->work);
    return WaitForSingleObject(f->done, 30000) == WAIT_OBJECT_0;
}
static int installed_queue_submit(void* user, const PBrowserServiceRequest* input)
{
    InstalledQueueFixture* f = (InstalledQueueFixture*)user;
    PRuntimeServiceRequest request;
    int rc;
    memset(&request, 0, sizeof(request)); request.size = sizeof(request); request.version = 1;
    request.tab_id = input->tab_id; request.page_generation = input->page_generation;
    request.bridge_id = input->token.bridge_id; request.request_id = input->token.request_id;
    request.method = input->method; request.json = input->params_json; request.json_bytes = input->params_bytes;
    rc = PRuntime_ServiceQueueSubmit(f->queue, &request);
    if (!rc) { ++f->accepted; f->token = input->token; } return rc;
}
static void installed_queue_cancel(void* user, const PBrowserServiceToken* token)
{
    InstalledQueueFixture* f = (InstalledQueueFixture*)user; ++f->cancelled;
    PRuntime_ServiceQueueCancel(f->queue, token->bridge_id, token->request_id);
}
static int installed_queue_js(InstalledQueueFixture* f, const char* source)
{
    const char* result;
    if (PBrowser_ScriptSessionEvaluate(f->session, source, -1) != PSCRIPT_OK) return 0;
    result = PBrowser_ScriptSessionGetResult(f->session); return result && !strcmp(result, "true");
}
BOOL test1384_runtime_installed_queue(void)
{
    static const char* methods[] = { "storage.sql" };
    static const char query[] = "{\"version\":1,\"method\":\"query\",\"sql\":\"SELECT count(*) FROM sample\",\"params\":[]}";
    static const char insert[] = "{\"version\":1,\"method\":\"execute\",\"sql\":\"INSERT INTO sample VALUES(43)\",\"params\":[]}";
    InstalledQueueFixture* f = NULL;
    PRuntimeServiceRequest request;
    PRuntimeServiceResult *result = NULL, *canary = NULL;
    PRuntimeServiceQueueInfo info;
    PBrowserServiceOptions options;
    WCHAR module[MAX_PATH], *slash;
    unsigned int mode, delivered;
    unsigned long pumped;
    int rc = 0;
    result = (PRuntimeServiceResult*)malloc(sizeof(*result)); canary = (PRuntimeServiceResult*)malloc(sizeof(*canary));
    RCHECK(result && canary);
    RCHECK(GetModuleFileNameW(NULL, module, MAX_PATH) && wcslen(module) < MAX_PATH-70);
    slash = wcsrchr(module, L'\\'); RCHECK(slash); *slash = 0;
    for (mode = 0; mode < 6; ++mode) {
        f = (InstalledQueueFixture*)calloc(1, sizeof(*f)); RCHECK(f); f->stored_rows = -1;
        wsprintfW(f->location.path, L"%s\\installed-queue-%lu-%u.db", module, GetCurrentProcessId(), mode);
        RCHECK(GetFileAttributesW(f->location.path) == 0xffffffffUL);
        RCHECK(WideCharToMultiByte(CP_UTF8, 0, f->location.path, -1, f->location.utf8, sizeof(f->location.utf8), NULL, NULL));
        f->options.size = sizeof(f->options); f->options.version = 1; f->options.tab_id = 91;
        f->options.page_generation = 3; f->options.delivery_thread_id = GetCurrentThreadId();
        f->ready = CreateEvent(NULL, TRUE, FALSE, NULL); f->done = CreateEvent(NULL, TRUE, FALSE, NULL);
        f->work = CreateEvent(NULL, FALSE, FALSE, NULL); f->inside = CreateEvent(NULL, TRUE, FALSE, NULL); f->resume = CreateEvent(NULL, TRUE, FALSE, NULL);
        RCHECK(f->ready && f->done && f->work && f->inside && f->resume);
        f->thread = CreateThread(NULL, 0, installed_queue_worker, f, 0, NULL);
        RCHECK(f->thread && WaitForSingleObject(f->ready, 30000) == WAIT_OBJECT_0 && !f->result && f->queue && f->side);
        delivered = 55;
        RCHECK(PRuntime_ServiceQueueProcessOne(f->queue, &delivered) == PRUNTIME_STATE && delivered == 55);
        f->session = PBrowser_ScriptSessionCreate(1000); RCHECK(f->session);
        memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = 1;
        options.tab_id = 91; options.page_generation = 3; options.methods = methods; options.method_count = 1;
        options.submit = installed_queue_submit; options.cancel = installed_queue_cancel; options.pw = f;
        RCHECK(PBrowser_ScriptSessionRegisterServices(f->session, &options) == PBROWSER_OK);
        RCHECK(installed_queue_js(f, "var replies=[],p={version:1,method:'execute',sql:'INSERT INTO sample VALUES(42)',params:[]};PositronServices.request('storage.sql',p,function(ok,v){replies.push([ok,v]);});p.sql='DROP TABLE sample';replies.length===0"));
        RCHECK(installed_queue_action(f, 1) && !f->result && f->processed == 1);
        result->size = sizeof(*result); result->version = 1;
        RCHECK(PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && !result->result &&
                result->bridge_id == f->token.bridge_id && result->request_id == f->token.request_id);
        RCHECK(PBrowser_ScriptSessionCompleteService(f->session, &f->token, 1, result->json, result->json_bytes) == PBROWSER_OK);
        RCHECK(installed_queue_js(f, "replies.length===0") && PBrowser_ScriptSessionPumpServices(f->session, 1, &pumped) == PBROWSER_OK && pumped == 1 &&
                installed_queue_js(f, "replies.length===1&&replies[0][0]&&replies[0][1].changes.value==='1'"));
        *canary = *result;
        RCHECK(PRuntime_ServiceQueueTakeResult(f->queue, result) == PRUNTIME_NOT_FOUND && !memcmp(result, canary, sizeof(*result)));
        memset(&request, 0, sizeof(request)); request.size = sizeof(request); request.version = 1;
        request.tab_id = 91; request.page_generation = 3; request.bridge_id = f->token.bridge_id;
        request.request_id = f->token.request_id; request.method = "storage.sql"; request.json = insert; request.json_bytes = sizeof(insert)-1;
        RCHECK(PRuntime_ServiceQueueSubmit(f->queue, &request) == PRUNTIME_STATE);
        if (mode == 0) {
            ++request.request_id;
            RCHECK(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0);
            InterlockedExchange((LONG*)&f->pause, 1); ResetEvent(f->done); InterlockedExchange((LONG*)&f->action, 1); SetEvent(f->work);
            RCHECK(WaitForSingleObject(f->inside, 10000) == WAIT_OBJECT_0 && PRuntime_ServiceQueueCancel(f->queue, request.bridge_id, request.request_id) == 0);
            SetEvent(f->resume); RCHECK(WaitForSingleObject(f->done, 30000) == WAIT_OBJECT_0 && !f->result);
            RCHECK(installed_queue_action(f, 6) && !f->result);
            ++request.request_id; request.json = query; request.json_bytes = sizeof(query)-1;
            RCHECK(PRuntime_ServiceQueueSubmit(f->queue, &request) == 0 && installed_queue_action(f, 1) && !f->result);
            RCHECK(PRuntime_ServiceQueueTakeResult(f->queue, result) == 0 && !result->result && strstr(result->json, "\"value\":\"1\""));
        } else {
            RCHECK(installed_queue_js(f, "PositronServices.request('storage.sql',{version:1,method:'execute',sql:'INSERT INTO sample VALUES(43)',params:[]},function(){replies.push('late');});true"));
            request.page_generation = 4; request.bridge_id = 71; request.request_id = 1;
            RCHECK(PRuntime_ServiceQueueSubmit(f->side, &request) == 0);
            if (mode == 2 || mode == 4) RCHECK(installed_queue_action(f, 1) && !f->result);
            if (mode == 2) RCHECK(installed_queue_action(f, 3) && f->result == PRUNTIME_INVALID);
            if (mode == 1) RCHECK(installed_queue_action(f, 2) && !f->result);
            if (mode == 3) RCHECK(installed_queue_action(f, 4) && !f->result);
            if (mode == 5) RCHECK(installed_queue_action(f, 7) && f->result == PRUNTIME_CANCELLED);
            if (mode == 4) {
                RCHECK(PBrowser_ScriptSessionRevokeServices(f->session) == PBROWSER_OK && PRuntime_ServiceQueueRevoke(f->queue) == 0);
                RCHECK(installed_queue_action(f, 6) && !f->result);
                RCHECK(installed_queue_action(f, 5) && !f->result);
                RCHECK(PRuntime_ServiceQueueTakeResult(f->side, result) == 0 && !result->result);
            } else {
                info.size = sizeof(info); info.version = 1;
                RCHECK(PRuntime_ServiceQueueGetInfo(f->queue, &info) == 0 && info.revoked && !info.waiting && !info.running && !info.completed);
                RCHECK(PRuntime_ServiceQueueGetInfo(f->side, &info) == 0 && info.revoked && !info.waiting && !info.completed);
                *canary = *result;
                RCHECK(PRuntime_ServiceQueueTakeResult(f->queue, result) == PRUNTIME_STATE && !memcmp(result, canary, sizeof(*result)));
                RCHECK(PBrowser_ScriptSessionRevokeServices(f->session) == PBROWSER_OK && f->cancelled == 1);
            }
            RCHECK(PBrowser_ScriptSessionCompleteService(f->session, &f->token, 1, "{}", 2) != PBROWSER_OK && installed_queue_js(f, "replies.length===1"));
        }
        PBrowser_ScriptSessionRevokeServices(f->session); PBrowser_ScriptSessionDestroy(f->session); f->session = NULL;
        RCHECK(installed_queue_action(f, 9) && WaitForSingleObject(f->thread, 30000) == WAIT_OBJECT_0);
        RCHECK(f->stored_rows == (mode == 2 || mode == 4 ? (mode == 4 ? 3 : 2) : 1) && !f->storage && !f->parent && !f->queue && !f->side &&
                !f->revision.bad_guard && !f->location.leases && f->location.opens == f->location.closes);
        RCHECK(DeleteFileW(f->location.path));
        CloseHandle(f->thread); CloseHandle(f->ready); CloseHandle(f->done); CloseHandle(f->work); CloseHandle(f->inside); CloseHandle(f->resume);
        free(f); f = NULL;
    }
    free(result); free(canary); return TRUE;
failed:
    if (f) {
        if (f->resume) SetEvent(f->resume);
        if (f->session) { PBrowser_ScriptSessionRevokeServices(f->session); PBrowser_ScriptSessionDestroy(f->session); }
        if (f->thread && WaitForSingleObject(f->thread, 0) != WAIT_OBJECT_0) {
            if (f->queue) PRuntime_ServiceQueueRevoke(f->queue);
            InterlockedExchange((LONG*)&f->instance_cancel, 1); installed_queue_action(f, 9);
            /* Preserve native state on timeout; never free a running worker. */
            if (WaitForSingleObject(f->thread, 30000) != WAIT_OBJECT_0) { free(result); free(canary); return FALSE; }
        }
        if (f->thread) CloseHandle(f->thread); if (f->ready) CloseHandle(f->ready); if (f->done) CloseHandle(f->done);
        if (f->work) CloseHandle(f->work); if (f->inside) CloseHandle(f->inside); if (f->resume) CloseHandle(f->resume);
        free(f);
    }
    free(result); free(canary); return FALSE;
}
