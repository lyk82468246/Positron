#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../positron_runtime/positron_runtime.h"
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
