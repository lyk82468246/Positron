#ifndef POSITRON_RUNTIME_H
#define POSITRON_RUNTIME_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef POSITRON_RUNTIME_EXPORTS
#define PRUNTIME_API __declspec(dllexport)
#else
#define PRUNTIME_API __declspec(dllimport)
#endif
#define PRUNTIME_VERSION 1
#define PRUNTIME_OK 0
#define PRUNTIME_ARGUMENT -1
#define PRUNTIME_INVALID -2
#define PRUNTIME_LIMIT -3
#define PRUNTIME_UNSUPPORTED -4
#define PRUNTIME_MEMORY -5
#define PRUNTIME_IO -6
#define PRUNTIME_BUSY -7
#define PRUNTIME_STATE -8
#define PRUNTIME_DENIED -9
#define PRUNTIME_SCHEMA -10
#define PRUNTIME_DATABASE -11
#define PRUNTIME_CANCELLED -12
#define PRUNTIME_NOT_FOUND -13
#define PRUNTIME_UNRESOLVED -14
#define PRUNTIME_SERVICE_MAX_PENDING 8U
#define PRUNTIME_SERVICE_MAX_QUEUES 4U
#define PRUNTIME_STORAGE_KEY_BYTES 127
#define PRUNTIME_SERVICE_JSON_BYTES 4096U
#define PRUNTIME_STORAGE_READY 1
#define PRUNTIME_STORAGE_FAILED 2
#define PRUNTIME_MIGRATION_SQL_BYTES 32768U
#define PRUNTIME_MIGRATION_TOTAL_BYTES (16U * PRUNTIME_MIGRATION_SQL_BYTES)
#define PRUNTIME_ROOT_BYTES 1023
#define PRUNTIME_RESOURCE_BYTES (8U * 1024U * 1024U)
#define PRUNTIME_MANIFEST_BYTES 8192
#define PRUNTIME_ID_BYTES 63
#define PRUNTIME_PATH_BYTES 255
#define PRUNTIME_MAX_MIGRATIONS 16
#define PRUNTIME_PERMISSION_SQL 1UL
#define PRUNTIME_URL_BYTES 1023
#define PRUNTIME_SUFFIX_BYTES 255
#define PRUNTIME_URL_QUERY 1U
#define PRUNTIME_URL_FRAGMENT 2U
typedef struct PRuntimeManifest PRuntimeManifest;
typedef PRuntimeManifest* PRuntimeManifestHandle;
typedef struct PRuntimeDirectory PRuntimeDirectory;
typedef PRuntimeDirectory* PRuntimeDirectoryHandle;
typedef struct PRuntimeMigrationPlan PRuntimeMigrationPlan;
typedef PRuntimeMigrationPlan* PRuntimeMigrationPlanHandle;
typedef struct PRuntimeStorage PRuntimeStorage;
typedef PRuntimeStorage* PRuntimeStorageHandle;
typedef struct PRuntimeServiceQueue PRuntimeServiceQueue;
typedef PRuntimeServiceQueue* PRuntimeServiceQueueHandle;

/* Read-only revision/installed verification; separate handle family, NOT a
 * DevelopmentDirectory, storage grant, installer or author-code session. */
#define PRUNTIME_REVISION_INITIALIZING 1U
#define PRUNTIME_REVISION_PENDING 2U
#define PRUNTIME_REVISION_COMPLETE 3U
#define PRUNTIME_REVISION_FAILED 4U
#define PRUNTIME_REVISION_CANCELLED 5U
#define PRUNTIME_REVISION_DONE 1
#define PRUNTIME_DIGEST_SHA256 1U
#define PRUNTIME_REVISION_MAX_FILES 1024U
#define PRUNTIME_REVISION_TOTAL_BYTES (32U * 1024U * 1024U)
#define PRUNTIME_REVISION_STEP_BYTES 65536U
typedef struct PRuntimeRevision* PRuntimeRevisionHandle;
typedef struct PRuntimeRevisionFile {
    unsigned int size, version;
    unsigned int file_bytes;
    char path[256]; /* decoded relative UTF-8 regular file; no directory entries */
    unsigned char sha256[32]; /* enumeration ignores this; GetFile copies computed digest */
} PRuntimeRevisionFile;
typedef struct PRuntimeApprovalSnapshot {
    unsigned int size, version;
    char id[64];
    char installation_id[33]; /* native nonzero 128-bit ID, exactly 32 lowercase hex */
    unsigned int revision; /* v1 first-install admission supports 1 only */
    unsigned int app_version, runtime_api_version, digest_algorithm;
    unsigned char content_id[32];
    unsigned long requested_permissions, approved_permissions;
    unsigned int approval_policy; /* 1: complete explicit native confirmation */
} PRuntimeApprovalSnapshot;
typedef struct PRuntimeInstalledRecord {
    unsigned int size, version;
    unsigned int generation; /* 1..2147483647; host COMMIT generation */
    unsigned int active; /* exactly 1, otherwise DENIED */
    char directory[1024]; /* native canonical revision root, not stable data identity */
    PRuntimeApprovalSnapshot approval;
} PRuntimeInstalledRecord;
typedef struct PRuntimeApprovalOptions {
    unsigned int size, version;
    char installation_id[33];
    unsigned int revision; /* exactly 1; no update approval in this contract */
    unsigned int confirmed; /* exactly 1; permission-free author code still needs confirmation */
} PRuntimeApprovalOptions;
typedef struct PRuntimeRevisionOptions {
    unsigned int size, version;
    int (*cancelled)(void* user); /* borrowed atomic flag; owner-thread callback */
    void* user_data;
} PRuntimeRevisionOptions;
typedef struct PRuntimeRevisionInfo {
    unsigned int size, version;
    unsigned int state;
    int error;
    unsigned int installed; /* kind, not authority; COMPLETE required */
    unsigned int file_count, total_bytes, verified_bytes, verified_files;
    char canonical_root[1024]; /* complete only, native-only */
    char id[64], entry[256];
    unsigned int app_version;
    unsigned long requested_permissions;
    char installation_id[33]; /* complete installed-kind only */
    unsigned int revision, registry_generation;
    unsigned long approved_permissions; /* copied registration, not SQL service grant */
    unsigned char content_id[32]; /* complete only */
} PRuntimeRevisionInfo;
/* Native registration reconciliation only; no persistence, storage or grant. */
#define PRUNTIME_REG_PREPARING 1U
#define PRUNTIME_REG_READY 2U
#define PRUNTIME_REG_FAILED 3U
#define PRUNTIME_REG_CANCELLED 4U
#define PRUNTIME_REG_REVOKED 5U
#define PRUNTIME_COMMIT_CONFIRMED 1U
#define PRUNTIME_COMMIT_NOT_COMMITTED 2U
#define PRUNTIME_COMMIT_UNKNOWN 3U
#define PRUNTIME_REG_MATCH_PROPOSED 1U
#define PRUNTIME_REG_MATCH_PREVIOUS 2U
#define PRUNTIME_REG_READY_WON_CANCEL 3U
typedef struct PRuntimeRegistrationRecord {
    unsigned int size, version;
    unsigned int state, generation, target_schema;
    char directory[1024];
    PRuntimeApprovalSnapshot approval;
} PRuntimeRegistrationRecord;
typedef struct PRuntimeRegistrationTransition {
    unsigned int size, version;
    unsigned int commit_outcome; /* exact native CAS result, not retry policy */
    unsigned int previous_exists; /* exactly 0 or 1 */
    unsigned int confirmed; /* 1 required for new intent / FAILED retry */
    PRuntimeRegistrationRecord previous; /* ignored if absent */
    PRuntimeRegistrationRecord proposed;
} PRuntimeRegistrationTransition;
typedef struct PRuntimeRegistrationProvider {
    unsigned int size, version;
    void* user_data;
    /* Nonblocking native app-ID then installation-ID exclusive coordination.
     * Compatible with installed launch leases; BUSY on conflict. Retain no
     * transaction or background write across return, including failure.
     * Reconciliation closes every non-NULL partial lease before returning. */
    int (*acquire)(void*, const char*, const char*, void**);
    /* Fresh committed read under lease. NOT_FOUND means genuinely absent;
     * inaccessible/unknown is IO, never NOT_FOUND. No author-supplied record. */
    int (*read)(void*, void*, PRuntimeRegistrationRecord*);
    void (*close)(void*, void*);
} PRuntimeRegistrationProvider;
typedef struct PRuntimeRegistrationDecision {
    unsigned int size, version;
    unsigned int match; /* MATCH_*; metadata observation, NEVER launch authority */
    unsigned int observed_state, observed_generation; /* zero if absent */
} PRuntimeRegistrationDecision;
/* Complete prepare-kind only. Validate the exact first-install transition
 * against retained manifest/root, acquire native coordination, then read once.
 * Supports absent->PREPARING(gen1), PREPARING->READY/FAILED/CANCELLED and
 * explicitly confirmed FAILED->PREPARING (generation+1, no overflow).
 * COMMIT_UNKNOWN may match old or proposed; a claimed confirmed/not-committed
 * outcome inconsistent with readback fails INVALID. READY at the proposed
 * generation may win an UNKNOWN cancellation. Any other record is INVALID.
 * Missing source is DENIED; unavailable read remains an error, not absence.
 * OK only publishes a copied observation, never permission to retry/write,
 * start author code, claim DB readiness or bypass OpenInstalled.
 * No CAS, SQL, files, worker, pump or allocation. Owner thread/no reentry;
 * callback targets borrowed until return. All errors preserve output and
 * leave the revision unchanged; root remains retained by its caller. */
PRUNTIME_API int PRuntime_RevisionReconcileRegistration(PRuntimeRevisionHandle revision,
        const PRuntimeRegistrationTransition* transition,
        const PRuntimeRegistrationProvider* provider,
        PRuntimeRegistrationDecision* output);
#define PRUNTIME_INTENT_NEW 1U
#define PRUNTIME_INTENT_RESUME 2U
#define PRUNTIME_INTENT_RETRY_FAILED 3U
#define PRUNTIME_INTENT_INITIALIZING 1U
#define PRUNTIME_INTENT_PENDING 2U
#define PRUNTIME_INTENT_PREPARED 3U
#define PRUNTIME_INTENT_FAILED 4U
#define PRUNTIME_INTENT_CANCELLED 5U
#define PRUNTIME_INTENT_UNRESOLVED 6U
#define PRUNTIME_INTENT_FINALIZED 7U
#define PRUNTIME_INTENT_DONE 1
typedef struct PRuntimeInstallIntent* PRuntimeInstallIntentHandle;
typedef struct PRuntimeInstallIntentOptions {
    unsigned int size, version, mode;
    PRuntimeApprovalOptions approval; /* explicit native confirmation, revision 1 */
} PRuntimeInstallIntentOptions;
typedef struct PRuntimeInstallIntentProvider {
    unsigned int size, version;
    void* user_data;
    /* Same nonblocking namespace/installation lease and fresh-read contract as
     * RegistrationProvider, held through IntentClose. */
    int (*acquire)(void*, const char*, const char*, void**);
    int (*read)(void*, void*, PRuntimeRegistrationRecord*);
    /* Atomic exact CAS; NULL expected means absent + logical ID unoccupied.
     * Always set outcome even on error: CONFIRMED, NOT_COMMITTED or UNKNOWN.
     * No active transaction/background operation survives return. Native
     * provider persists fields, not struct padding, and enforces app-ID
     * uniqueness. Runtime supplies PREPARING/CANCELLED and data-preparation
     * FAILED records; InstallCommit supplies READY or CANCELLED. */
    int (*compare_exchange)(void*, void*, const PRuntimeRegistrationRecord*,
            const PRuntimeRegistrationRecord*, unsigned int* outcome);
    void (*close)(void*, void*);
} PRuntimeInstallIntentProvider;
typedef struct PRuntimeInstallIntentInfo {
    unsigned int size, version, state;
    int error, provider_result; /* UNRESOLVED retains underlying read/CAS error */
    unsigned int commit_outcome, record_known, record_exists;
    PRuntimeRegistrationRecord record; /* native-only, zero unless known/present */
} PRuntimeInstallIntentInfo;
/* Complete prepare-kind + explicit confirmation; one child, no callbacks at
 * Begin. Copies options/provider, retains parent/root. Parent Cancel/Close,
 * approval building and standalone reconciliation return STATE until child
 * closes. GetInfo/GetFile remain available outside active callbacks.
 * Step performs at most one acquire/read/CAS callback; confirmed/unknown writes
 * are always read back, never automatically replayed. PREPARED is an inactive
 * durable intent, NOT installed identity, DB readiness or permission to run.
 * RESUME requires exact PREPARING; RETRY_FAILED exact FAILED + generation+1.
 * Unreadable post-CAS result becomes UNRESOLVED, never not-found/safe-to-retry.
 * No SQL, resources writes, author execution, worker, pump or hard I/O deadline.
 * Owner-thread, no reentry; provider code/user lives through Close. */
PRUNTIME_API int PRuntime_InstallIntentBegin(PRuntimeRevisionHandle revision,
        const PRuntimeInstallIntentOptions* options,
        const PRuntimeInstallIntentProvider* provider, PRuntimeInstallIntentHandle* output);
PRUNTIME_API int PRuntime_InstallIntentStep(PRuntimeInstallIntentHandle intent);
PRUNTIME_API int PRuntime_InstallIntentGetInfo(PRuntimeInstallIntentHandle intent, PRuntimeInstallIntentInfo* info);
/* Marks a request, no I/O. Step must resolve any in-flight write then, if an
 * exact PREPARING exists, CAS CANCELLED and read back. Cannot undo COMMIT.
 * Cancel before new/retry CAS leaves registration untouched. Close does NOT
 * imply cancel or delete; pending/unknown durable records remain for recovery. */
PRUNTIME_API int PRuntime_InstallIntentCancel(PRuntimeInstallIntentHandle intent);
PRUNTIME_API int PRuntime_InstallIntentClose(PRuntimeInstallIntentHandle* intent);
typedef struct PRuntimeServiceQueueOptions {
    unsigned int size;
    unsigned int version;
    unsigned long tab_id;
    unsigned long page_generation;
    unsigned long delivery_thread_id;
} PRuntimeServiceQueueOptions;
typedef struct PRuntimeServiceRequest {
    unsigned int size;
    unsigned int version;
    unsigned long tab_id;
    unsigned long page_generation;
    unsigned long bridge_id;
    unsigned long request_id;
    const char* method; /* exactly "storage.sql"; borrowed during Submit */
    const char* json;
    unsigned int json_bytes;
} PRuntimeServiceRequest;
typedef struct PRuntimeServiceResult {
    unsigned int size;
    unsigned int version;
    unsigned long tab_id;
    unsigned long page_generation;
    unsigned long bridge_id;
    unsigned long request_id;
    int result;
    unsigned int json_bytes;
    char json[4097]; /* copied sanitized failure or committed success */
} PRuntimeServiceResult;
typedef struct PRuntimeServiceQueueInfo {
    unsigned int size;
    unsigned int version;
    unsigned int waiting;
    unsigned int running;
    unsigned int completed;
    int revoked;
} PRuntimeServiceQueueInfo;
typedef struct PRuntimeStorageLocation {
    void* token;
    unsigned int path_bytes;
    char path[1024]; /* absolute native UTF-8 database filename */
} PRuntimeStorageLocation;
typedef struct PRuntimeStorageProvider {
    unsigned int size;
    unsigned int version;
    void* user_data;
    /* Anchor exactly to the native EXE-adjacent data directory. Runtime owns
     * relative key; provider only creates/retains native directories and proves
     * physical containment. No fallback, browser DB alias, deletion or overwrite.
     * Token retained through DB close. Release even a failed partial acquisition.
     * Never use the supplied short bucket as full identity: DB verifies a full
     * private stamp before migration. Keep volume/path stable until close. */
    int (*open_location)(void* user, const char* key, PRuntimeStorageLocation* location);
    void (*close_location)(void* user, void* token);
} PRuntimeStorageProvider;
/* READY startup only: no directory/file creation, deletion, fallback or
 * overwrite. Retain an existing contained physical path/volume and exclusive
 * native location lease until DB close. Partial tokens are always released.
 * Registry coordination is retained by the installed parent revision. */
typedef struct PRuntimeInstalledStorageProvider {
    unsigned int size, version;
    void* user_data;
    int (*open_existing_location)(void* user, const char* key, PRuntimeStorageLocation* location);
    void (*close_location)(void* user, void* token);
} PRuntimeInstalledStorageProvider;

typedef struct PRuntimeStorageOptions {
    unsigned int size;
    unsigned int version;
    unsigned long granted_permissions; /* trusted native SQL confirmation */
    unsigned int vm_budget; /* 100..1000000, multiples of 100, per file/request */
    int (*cancelled)(void* user); /* synchronous borrowed atomic flag only */
    void* user_data;
} PRuntimeStorageOptions;
typedef struct PRuntimeStorageInfo {
    unsigned int size;
    unsigned int version;
    char id[64];
    char key[128]; /* native-only mapping hint, NOT authorization or URL */
    unsigned long schema_version;
    int state;
} PRuntimeStorageInfo;
typedef struct PRuntimeStorageError {
    unsigned int size;
    unsigned int version;
    int result;
    int db_result;
    int db_category;
    int sqlite_code;
    int sqlite_extended_code;
    int cleanup_result;
    unsigned long failed_schema_version;
    char message[256]; /* copied native diagnostic, never expose blindly to JS */
} PRuntimeStorageError;
typedef struct PRuntimeInstallData* PRuntimeInstallDataHandle;
#define PRUNTIME_INSTALL_DATA_INITIALIZING 1U
#define PRUNTIME_INSTALL_DATA_PENDING 2U
#define PRUNTIME_INSTALL_DATA_PREPARED 3U
#define PRUNTIME_INSTALL_DATA_FAILED 4U
#define PRUNTIME_INSTALL_DATA_CANCELLED 5U
#define PRUNTIME_INSTALL_DATA_DONE 1
typedef struct PRuntimeInstallDataOptions {
    unsigned int size, version, vm_budget; /* 100..1000000, multiples of 100, per migration */
    int (*cancelled)(void*);
    void* user_data;
} PRuntimeInstallDataOptions;
typedef struct PRuntimeInstallDataInfo {
    unsigned int size, version, state;
    unsigned int previous_schema, current_schema, target_schema, applied_files;
    char key[128]; /* v1/installed/<native installation ID>/app.db; native only */
    PRuntimeStorageError error; /* primary failure, including committed prefix */
    int registration_result; /* independent failure/cancel recording outcome */
    unsigned int registration_outcome;
} PRuntimeInstallDataInfo;
/* PREPARED live intent + approved storage.sql only. One data child; copies
 * tables/options, no callbacks at Begin. Retains intent/root/coordination.
 * Parent intent Cancel/Close return STATE until data Close. No raw snapshot,
 * development handle, raw DB, SQL service, READY transition or author code.
 * Step refreshes exact PREPARING registration, maps full installed identity,
 * opens/binds a private DB and reads strict version. Copies ALL remaining
 * sources before any migration; <=16*32768 bytes, nonempty UTF-8/no BOM/NUL.
 * Applies one PDb restricted migration per step, each separately committed;
 * exact version resumes suffix only. Future/corrupt/mismatched DB fails closed.
 * Refresh before each migration and final completion. Owner-thread/no reentry,
 * no pump/worker; native I/O/SQL operation is not a wall-clock yield.
 * Failure/cancel attempts exact FAILED/CANCELLED registration CAS then readback;
 * primary error survives secondary error/unknown. No automatic write replay,
 * data deletion, rollback of prior commits or fallback location. Provider/CSP/
 * DB memory excluded; Runtime combined parent/intent/data reserve <=1280 KiB.
 * READY startup uses the separate InstalledStorageOpen contract. */
PRUNTIME_API int PRuntime_InstallDataBegin(PRuntimeInstallIntentHandle intent,
        const PRuntimeInstallDataOptions* options, const PRuntimeStorageProvider* provider,
        PRuntimeInstallDataHandle* output);
PRUNTIME_API int PRuntime_InstallDataStep(PRuntimeInstallDataHandle data);
PRUNTIME_API int PRuntime_InstallDataGetInfo(PRuntimeInstallDataHandle data, PRuntimeInstallDataInfo* info);
/* Request only; Step records cancellation when possible. Cannot undo a COMMIT.
 * Close does not cancel/register/delete. DB then location, parent detach;
 * safe repeated NULL slot, active/wrong-thread Close preserves the handle. */
PRUNTIME_API int PRuntime_InstallDataCancel(PRuntimeInstallDataHandle data);
PRUNTIME_API int PRuntime_InstallDataClose(PRuntimeInstallDataHandle* data);
/* Final first-install commit; no author execution or storage/service grant. */
typedef struct PRuntimeInstallCommit* PRuntimeInstallCommitHandle;
#define PRUNTIME_INSTALL_COMMIT_PENDING 1U
#define PRUNTIME_INSTALL_COMMIT_COMPLETE 2U
#define PRUNTIME_INSTALL_COMMIT_FAILED 3U
#define PRUNTIME_INSTALL_COMMIT_CANCELLED 4U
#define PRUNTIME_INSTALL_COMMIT_UNRESOLVED 5U
#define PRUNTIME_INSTALL_COMMIT_DONE 1
typedef struct PRuntimeInstallCommitInfo {
    unsigned int size, version, state;
    int error, provider_result;
    unsigned int commit_outcome;
    PRuntimeStorageError data_error; /* DB verification diagnostics, native only */
    PRuntimeRegistrationRecord record; /* exact committed result only on COMPLETE/CANCELLED */
} PRuntimeInstallCommitInfo;
/* Live PREPARED intent, plus its live PREPARED data child iff SQL approved.
 * No callbacks at Begin; output unchanged on failure. Retains intent, resource
 * root, coordination lease, DB/location until CommitClose. Parent Close/Cancel
 * and DataClose return STATE. Copies no authority from caller snapshots.
 * Step: exact PREPARING read, retained DB idle/exact schema check, fresh exact
 * read, at most ONE CAS, exact committed readback. No create/bind/migration,
 * repair or automatic CAS retry. Provider's immutable-root/location and
 * exclusive coordination obligations continue throughout; no cross-DB atomicity.
 * Cancellation before CAS proposes CANCELLED instead; after CAS only readback,
 * confirmed READY wins. Unknown/unreadable result is UNRESOLVED. Closing after
 * CAS without readback also retires intent as UNRESOLVED, never permits replay.
 * COMPLETE finalizes intent (FINALIZED); close child->data->intent->revision,
 * then normal OpenInstalled/InstalledStorageOpen performs startup validation.
 * Failure preserves data/PREPARING for explicit recovery; no implicit FAILED
 * write, deletion, author SQL or execution. Lease remains held on failure.
 * One owner thread, no callback reentry, worker or message pump; each Step has
 * at most one provider callback or bounded DB check, not a hard wall-time bound.
 * Combined Runtime-owned reserve <=1280 KiB; DB/provider memory excluded.
 * Info exact size/version; error leaves output intact. Close NULL idempotent. */
PRUNTIME_API int PRuntime_InstallCommitBegin(PRuntimeInstallIntentHandle intent,
        PRuntimeInstallDataHandle data, PRuntimeInstallCommitHandle* output);
PRUNTIME_API int PRuntime_InstallCommitStep(PRuntimeInstallCommitHandle commit);
PRUNTIME_API int PRuntime_InstallCommitGetInfo(PRuntimeInstallCommitHandle commit, PRuntimeInstallCommitInfo* info);
PRUNTIME_API int PRuntime_InstallCommitCancel(PRuntimeInstallCommitHandle commit);
PRUNTIME_API int PRuntime_InstallCommitClose(PRuntimeInstallCommitHandle* commit);

/* Read-only, native resource-update qualification; NOT a registry write. */
typedef struct PRuntimeResourceUpdate* PRuntimeResourceUpdateHandle;
#define PRUNTIME_UPDATE_PENDING 1U
#define PRUNTIME_UPDATE_COMPLETE 2U
#define PRUNTIME_UPDATE_FAILED 3U
#define PRUNTIME_UPDATE_CANCELLED 4U
#define PRUNTIME_UPDATE_DONE 1
typedef struct PRuntimeResourceUpdateOptions {
    unsigned int size, version, confirmed; /* v1, explicit native update confirmation */
} PRuntimeResourceUpdateOptions;
typedef struct PRuntimeResourceUpdateInfo {
    unsigned int size, version, state;
    int error;
    PRuntimeStorageError data_error;
    PRuntimeInstalledRecord current;
    PRuntimeApprovalSnapshot candidate; /* complete only, copied native metadata, not bearer authority */
    char candidate_root[1024];
    unsigned int target_schema;
} PRuntimeResourceUpdateInfo;
/* Live COMPLETE installed current + separate COMPLETE prepare-kind candidate;
 * one creating worker, SQL current requires its READY storage with no queues.
 * Same canonical ID, increasing app_version, current revision+1, unchanged
 * permissions/schema and exact migration version/path/hash history. Changes
 * to capabilities/schema/history UNSUPPORTED; identity/root/version mismatch
 * INVALID; overflow LIMIT. No callback at Prepare, failure preserves output.
 * Retains BOTH parents and borrows the current parent's existing exclusive
 * registry lease, never reacquires it. Retains SQL DB/location if supplied.
 * Parent Cancel/Close, storage Close/SQL/QueueCreate are STATE until child closes.
 * Step refreshes current, checks retained idle DB/exact schema, refreshes again.
 * No DB creation/bind/migration/writes, CAS, pending/current publication, author
 * execution or Browser grant. COMPLETE proves qualification ONLY; copied info
 * cannot publish an update. Future publication must consume the live qualifier
 * under this same retained coordination, not trust a detached snapshot.
 * One registration operation or bounded DB check/Step plus cancel predicates;
 * owner/reentry guards, no worker/pump/hard deadline. Failed/cancelled child
 * leaves old data/registration untouched, still held until Close. Info exact
 * v1, verified metadata zero until COMPLETE. Close NULL value idempotent.
 * Two revisions+storage+qualifier+128KiB reserve <=1280KiB Runtime-owned;
 * provider snapshot/CSP/SQLite allocations excluded and separately bounded. */
PRUNTIME_API int PRuntime_ResourceUpdatePrepare(PRuntimeRevisionHandle current,
        PRuntimeRevisionHandle candidate, PRuntimeStorageHandle current_storage,
        const PRuntimeResourceUpdateOptions* options, PRuntimeResourceUpdateHandle* output);
PRUNTIME_API int PRuntime_ResourceUpdateStep(PRuntimeResourceUpdateHandle update);
PRUNTIME_API int PRuntime_ResourceUpdateGetInfo(PRuntimeResourceUpdateHandle update, PRuntimeResourceUpdateInfo* info);
PRUNTIME_API int PRuntime_ResourceUpdateCancel(PRuntimeResourceUpdateHandle update);
PRUNTIME_API int PRuntime_ResourceUpdateClose(PRuntimeResourceUpdateHandle* update);

typedef struct PRuntimeMigrationPlanOptions {
    unsigned int size;
    unsigned int version;
    unsigned long granted_permissions; /* explicit trusted native decision */
    unsigned long current_schema_version; /* strict DB read, NOT author input */
} PRuntimeMigrationPlanOptions;
typedef struct PRuntimeMigrationPlanInfo {
    unsigned int size;
    unsigned int version;
    char id[64];
    unsigned long previous_version;
    unsigned long target_version;
    unsigned int file_count;
    unsigned int total_bytes;
} PRuntimeMigrationPlanInfo;
typedef struct PRuntimeMigrationSource {
    unsigned int size;
    unsigned int version;
    unsigned long schema_version;
    char file[256];
    const char* sql; /* immutable borrowed plan bytes, not a NUL-terminated API */
    unsigned int sql_bytes;
} PRuntimeMigrationSource;
/* Native provider results: zero-initialized by Runtime. A returned non-NULL
 * root/lease/bytes token is released even when its callback reports failure.
 * canonical is the filesystem's stable UTF-8 directory identity, not an alias
 * or user spelling. Provider must resolve case/aliases against the real FS,
 * reject unprovable containment/links, and anchor reads to the retained root.
 * The provider is trusted native code, never author JS. */
typedef struct PRuntimeDirectoryRoot {
    void* root;
    unsigned int canonical_bytes;
    char canonical[1024];
} PRuntimeDirectoryRoot;
typedef struct PRuntimeBorrowedBytes {
    const void* data;
    unsigned int bytes;
    void* token;
} PRuntimeBorrowedBytes;
typedef struct PRuntimeDirectoryProvider {
    unsigned int size;
    unsigned int version;
    void* user_data;
    int (*open_root)(void* user, const char* directory, unsigned int bytes,
            PRuntimeDirectoryRoot* root);
    void (*close_root)(void* user, void* root);
    /* Native atomic cross-process exclusive lease on exactly this opaque
     * ASCII name; do not decorate with PID, wait, or treat existing as success.
     * An existing live name returns BUSY. Keep lease until release_lock.
     * Runtime's finite noncryptographic lock bucket can collide: collisions
     * reject concurrent admission, NEVER identify data or grant authority. */
    int (*acquire_lock)(void* user, const char* name, void** lease);
    void (*release_lock)(void* user, void* lease);
    /* Complete synchronous immutable read, <=limit; no partial success.
     * Key is decoded UTF-8 relative to retained root, never URI/device path.
     * Enforce physical containment and stable resources for this root lifetime;
     * no additional decoding, network, fallback directory or background access.
     * Return IO for unavailable/denied/missing, LIMIT for oversized input. */
    int (*read)(void* user, void* root, const char* key, unsigned int limit,
            PRuntimeBorrowedBytes* bytes);
    void (*release_bytes)(void* user, void* root, PRuntimeBorrowedBytes* bytes);
} PRuntimeDirectoryProvider;
typedef struct PRuntimeRevisionProvider {
    unsigned int size, version;
    unsigned int digest_algorithm; /* SHA256 only; unsupported rejected before callbacks */
    void* user_data;
    /* Retain a canonical source root, native coordination and a COMPLETE
     * IMMUTABLE resource view through close_root: a protected physical view
     * or a trusted native snapshot of ALL regular files and exact bytes.
     * Manifest/migrations/reads use only this same view; no snapshot-miss
     * physical read or later catalog expansion. Reject links/aliases and
     * unprovable containment/incomplete capture. Capturing a native snapshot
     * must fail on observed changes; it is NOT an atomic filesystem snapshot
     * or protection against malicious uncoordinated writers. Canonical names
     * the physical source, content_id binds the view; every installed reopen
     * captures/verifies anew. Physical additions after snapshot publication
     * cannot change this view, but may cause the next startup to be refused.
     * Return UNSUPPORTED if these guarantees cannot be supplied. Partial
     * root token is closed even on failure; no generic WM adapter implied. */
    int (*open_root)(void*, const char*, unsigned int, PRuntimeDirectoryRoot*);
    void (*close_root)(void*, void*);
    /* Flat complete enumeration, index from 0; NOT_FOUND is the sole end
     * marker. Exact file size/path; Runtime sorts/validates and computes hash. */
    int (*enumerate_file)(void*, void*, unsigned int, PRuntimeRevisionFile*);
    /* Read <=requested into borrowed buffer, written may be zero at EOF.
     * Runtime requires exact content bytes and probes EOF after declared size.
     * No URI decoding, fallback roots, network or background access. */
    int (*read_at)(void*, void*, const char*, unsigned int, void*,
            unsigned int, unsigned int*);
    /* SHA256 incremental context, one at a time. All partial non-NULL tokens
     * close even on failure. Return UNSUPPORTED for absent native algorithm;
     * never substitute a weaker hash or return fabricated success. Native
     * provider/CSP memory is host-owned, outside DLL buffer budget. */
    int (*hash_begin)(void*, void**);
    int (*hash_update)(void*, void*, const void*, unsigned int);
    int (*hash_finish)(void*, void*, unsigned char[32]);
    void (*hash_close)(void*, void*);
} PRuntimeRevisionProvider;
typedef struct PRuntimeInstalledProvider {
    unsigned int size, version;
    void* user_data;
    /* Read committed native registration and hold exclusive per-installation
     * coordination lease through close; duplicate launch returns BUSY.
     * Not an author-provided snapshot. Prevent updates/uninstall while
     * held; Runtime also refreshes before COMPLETE and each installed read.
     * NOT_FOUND is denied. Any partial lease is closed on operation Close. */
    int (*open)(void*, const char*, PRuntimeInstalledRecord*, void**);
    int (*refresh)(void*, void*, PRuntimeInstalledRecord*);
    void (*close)(void*, void*);
} PRuntimeInstalledProvider;
/* Begin copies tables/options, no callbacks; output untouched on failure.
 * Step opens/enumerates/hashes the root; <=16 enum/framing entries OR <=64 KiB
 * read per call, no whole-resource buffering. Internal job+JSON reserve <=512
 * KiB (provider/CSP allocations excluded). Single owner thread, no reentry,
 * message pump or worker. Returns OK while pending, DONE once complete,
 * negative terminal error otherwise. Completed/failed jobs never restart.
 * Callback code/user targets survive until Close; root/registry leases stay
 * held even in terminal state until Close. Only COMPLETE exposes content.
 * Paths <=255 bytes/16 levels, <=1024 files, <=8 MiB/file, <=32 MiB total,
 * manifest <=8192 bytes. Required entry/migration files must exist in catalog.
 * Root manifest raw bytes and empty files participate. No SQL or writes. */
PRUNTIME_API int PRuntime_PrepareRevision(const char* directory, unsigned int bytes,
        const PRuntimeRevisionProvider* provider, const PRuntimeRevisionOptions* options,
        PRuntimeRevisionHandle* output);
/* Requests exact native installation ID + expected committed generation.
 * Registry supplies the canonical root; no development handle/raw approval
 * overload. v1 revision=1 only; updates/storage/author execution not provided. */
PRUNTIME_API int PRuntime_OpenInstalled(const char* installation_id, unsigned int bytes,
        unsigned int expected_generation, const PRuntimeInstalledProvider* registry,
        const PRuntimeRevisionProvider* provider, const PRuntimeRevisionOptions* options,
        PRuntimeRevisionHandle* output);
/* Explicit revision-aware opener. Same size/version-1 provider/options and
 * owned handle family; expected_revision is 1..2147483647 and must match the
 * committed trusted record exactly. V1 OpenInstalled still only accepts 1.
 * Shared Step/read/refresh, InstalledStorageOpen and page queues use the
 * explicit handle policy; no record is rewritten to pretend revision 1.
 * No update publication or authority from caller-supplied approval. */
PRUNTIME_API int PRuntime_OpenInstalledEx(const char* installation_id, unsigned int bytes,
        unsigned int expected_generation, unsigned int expected_revision,
        const PRuntimeInstalledProvider* registry, const PRuntimeRevisionProvider* resources,
        const PRuntimeRevisionOptions* options, PRuntimeRevisionHandle* output);
PRUNTIME_API int PRuntime_RevisionStep(PRuntimeRevisionHandle revision);
PRUNTIME_API int PRuntime_RevisionGetInfo(PRuntimeRevisionHandle revision, PRuntimeRevisionInfo* info);
PRUNTIME_API int PRuntime_RevisionGetFile(PRuntimeRevisionHandle revision,
        unsigned int index, PRuntimeRevisionFile* file);
/* Complete prepare-kind only; creates copied native registration data, NOT
 * a committed grant. Explicit whole-capability confirmation, first-install
 * revision=1; all outputs unchanged on failure. No callbacks or persistence. */
PRUNTIME_API int PRuntime_RevisionBuildApproval(PRuntimeRevisionHandle revision,
        const PRuntimeApprovalOptions* options, PRuntimeApprovalSnapshot* output);
/* Complete installed-kind only; stable catalog index/offset, <=64 KiB copied
 * read through retained root after registry refresh. Temporary buffer protects
 * buffer/written on failure. No directory/SQL/service identity conversion. */
PRUNTIME_API int PRuntime_InstalledReadFile(PRuntimeRevisionHandle revision,
        unsigned int index, unsigned int offset, void* buffer,
        unsigned int capacity, unsigned int* written);
PRUNTIME_API int PRuntime_RevisionCancel(PRuntimeRevisionHandle revision);
/* Clears owned slot; repeated NULL value safe. STATE preserves active/wrong
 * thread job. Hash -> manifest -> root -> registry lease release, no callbacks
 * after Close, aliases invalid. */
PRUNTIME_API int PRuntime_RevisionClose(PRuntimeRevisionHandle* revision);
typedef struct PRuntimeManifestInfo {
    unsigned int size;
    unsigned int version;
    char id[64];
    char name[128];
    unsigned long app_version;
    unsigned long requested_permissions; /* declaration only, NEVER a grant */
    char entry[256];
    unsigned int migration_count;
} PRuntimeManifestInfo;
typedef struct PRuntimeMigrationInfo {
    unsigned int size;
    unsigned int version;
    unsigned long schema_version;
    char file[256];
} PRuntimeMigrationInfo;
typedef struct PRuntimeResourceReference {
    unsigned int size;
    unsigned int version;
    unsigned int flags; /* QUERY/FRAGMENT distinguish absent from empty */
    char url[1024]; /* canonical navigation URL; includes query/fragment */
    char path[256]; /* decoded package key, never decode again */
    char query[256]; /* escaped UTF-8, without '?' */
    char fragment[256]; /* escaped UTF-8, without '#' */
} PRuntimeResourceReference;
/* Pure explicit-length ASCII ID canonicalization; >=2 letter-led segments,
 * then letters/digits/underscore, <=63 bytes. Output unchanged on error. */
PRUNTIME_API int PRuntime_NormalizeId(const char* id, unsigned int bytes,
        char* output, unsigned int capacity);
/* Package-relative decoded UTF-8 path, NOT a URI/device filename. <=255 bytes,
 * <=16 segments; no empty/dot/dotdot segments, leading/trailing slash, colon,
 * backslash, percent, query/fragment, controls, trailing dot/space or native
 * wildcard/quote characters. No I/O; host must not percent-decode it or infer
 * physical containment from this lexical check. */
PRUNTIME_API int PRuntime_ValidateResourcePath(const char* path, unsigned int bytes);
/* Strict v1 manifest admission, immutable caller-owned handle. All eight
 * fields required, unknown/duplicate keys rejected. manifestVersion and
 * runtimeApiVersion numeric integer 1; app version integer 1..2147483647.
 * permissions array: [] or ["storage.sql"]. migrations array <=16 objects
 * {"version": positive contiguous integer starting at 1, "file": path}.
 * Migrations require storage.sql, unique ASCII-case-insensitive file paths.
 * Name 1..127 UTF-8 bytes without controls. Entry and migration files use the
 * resource-path contract. Input <=8192 bytes, depth <=8, tokens <=256 through
 * JSON DLL; no partial output, callback, filesystem/network/thread or SQL.
 * Handle proves valid METADATA ONLY, not trust, directory identity, exclusive
 * instance lock, permissions or permission to expose services. Installed/dev
 * binding, SQL services and PXI remain separate contracts. The lexical app URL
 * resolver below likewise confers no identity, trust or resource-read grant.
 * Borrowed input only during Create; snapshots copy, queries leave output
 * unchanged on error. Call on one owner thread; freed aliases are invalid. */
PRUNTIME_API int PRuntime_ManifestCreate(const char* json, unsigned int bytes,
        PRuntimeManifestHandle* output);
PRUNTIME_API int PRuntime_ManifestGetInfo(PRuntimeManifestHandle manifest,
        PRuntimeManifestInfo* info);
PRUNTIME_API int PRuntime_ManifestGetMigration(PRuntimeManifestHandle manifest,
        unsigned int index, PRuntimeMigrationInfo* info);
/* Pure lexical resolver, NOT identity, permission or physical containment.
 * Explicit-length reference/base <=1023 bytes, no embedded NUL/controls.
 * NULL base with zero bytes uses manifest entry. Non-NULL base must be an
 * absolute same-ID app URL. Empty reference is valid (NULL only with zero
 * bytes). Relative/root/query/fragment references supported; root maps entry,
 * other directory-only paths rejected. Dot segments cannot escape root.
 * Encoded separators, percent, dot bytes and native path characters fail
 * closed; UTF-8 is decoded exactly once into the <=255-byte/16-segment key.
 * External schemes return UNSUPPORTED; cross-ID/invalid authority INVALID.
 * Query/fragment each <=255 escaped bytes; never part of a file-open key.
 * Output exact size/version, unchanged on error. No I/O, grant or callback.
 * Host must bind a trusted physical instance before opening returned keys;
 * never use an app URL or this metadata handle as authorization. */
PRUNTIME_API int PRuntime_ManifestResolveReference(PRuntimeManifestHandle manifest,
        const char* base, unsigned int base_bytes,
        const char* reference, unsigned int reference_bytes,
        PRuntimeResourceReference* output);
/* Clears owned slot; repeated NULL value is safe; NULL slot is ARGUMENT. */
PRUNTIME_API int PRuntime_ManifestClose(PRuntimeManifestHandle* manifest);
/* Explicit trusted development launch ONLY, not installed identity or grant.
 * Copies provider table, opens canonical root, reads/admit root manifest.json,
 * takes per-(development, canonical root, normalized ID) exclusive lease.
 * Output unchanged on failure; all acquired bytes/lease/root unwound. No SQL,
 * writes, worker, window pump, storage key or Browser service authorization.
 * Paths/canonical identity <=1023 valid UTF-8 bytes without controls/NUL.
 * Host must prove physical canonicalization/containment; absent callbacks fail
 * before I/O. Callback code/user live until Close; original table may be
 * discarded after Open. One creating thread, synchronous
 * non-reentrant callbacks; operations on a live instance return STATE from its
 * callback or another thread. Open creates a new owner-bound instance; it must
 * not be recursively invoked by its provider. No API is a cross-thread cancel;
 * host schedules around calls.
 * Provider failure codes allowed: IO, LIMIT, UNSUPPORTED, MEMORY; lock also BUSY.
 * Other statuses/malformed provider results become IO. Release callbacks must
 * complete, cannot fail/reenter, and may not throw across the C ABI. */
PRUNTIME_API int PRuntime_DevelopmentDirectoryOpen(const char* directory,
        unsigned int directory_bytes, const PRuntimeDirectoryProvider* provider,
        PRuntimeDirectoryHandle* output);
PRUNTIME_API int PRuntime_DirectoryGetInfo(PRuntimeDirectoryHandle directory,
        PRuntimeManifestInfo* info);
PRUNTIME_API int PRuntime_DirectoryResolveReference(PRuntimeDirectoryHandle directory,
        const char* base, unsigned int base_bytes, const char* reference,
        unsigned int reference_bytes, PRuntimeResourceReference* output);
/* Read through resolver and retained root. Capacity 1..8 MiB is also the read
 * limit (not a size probe); consumers retain any stricter Core/Browser budget.
 * buffer, written and exact size/version reference output must be non-NULL and
 * non-overlapping, disjoint from provider bytes. Copies binary bytes, no NUL
 * append. All three unchanged on failure, empty resources allowed. Returned
 * borrowed bytes released before return; no allocation survives the call. */
PRUNTIME_API int PRuntime_DirectoryReadResource(PRuntimeDirectoryHandle directory,
        const char* base, unsigned int base_bytes, const char* reference,
        unsigned int reference_bytes, void* buffer, unsigned int capacity,
        unsigned int* written, PRuntimeResourceReference* resolved);
/* Releases manifest then lock then root, clears slot. NULL value idempotent;
 * STATE leaves slot and live instance intact. Freed aliases remain invalid. */
PRUNTIME_API int PRuntime_DirectoryClose(PRuntimeDirectoryHandle* directory);
/* Prepare all pending manifest migrations from the retained directory root.
 * Requires declaration AND explicit native SQL grant, no unknown grant bits.
 * Never derive that decision/version from JS, URL or a manifest declaration.
 * Current strict DB version must be <= catalog length; no downgrade/skip.
 * Reads only catalog entries current+1..target, in order, <=32 KiB each and
 * <=512 KiB total. Nonempty UTF-8 without BOM/NUL/control bytes except TAB/CR/LF.
 * SQL syntax/authorizer/budget admission remains DB's responsibility; this
 * snapshot is NOT an executable capability, data identity or DB binding.
 * All files copied before publishing the handle. Failure unwinds bytes/plan
 * and preserves output; callbacks run with directory reentry/Close blocked.
 * No SQL/writes/worker/pump. Provider immutable-root contract still applies.
 * Caller must later compare expected version under DB's migration write lock.
 * No-pending is a valid zero-file plan, with no resource reads.
 * Plan independent of directory lifetime, one creating thread; copied requests
 * only may cross workers, never either live handle. No callback after return. */
PRUNTIME_API int PRuntime_DirectoryPrepareMigrations(PRuntimeDirectoryHandle directory,
        const PRuntimeMigrationPlanOptions* options, PRuntimeMigrationPlanHandle* output);
/* Exact size/version, outputs unchanged on error. Source borrowed until plan
 * Close; do not mutate/free or use after Close. Index is pending-file index,
 * not schema version. Close clears owned slot; repeated NULL value is safe. */
PRUNTIME_API int PRuntime_MigrationPlanGetInfo(PRuntimeMigrationPlanHandle plan,
        PRuntimeMigrationPlanInfo* info);
PRUNTIME_API int PRuntime_MigrationPlanGetSource(PRuntimeMigrationPlanHandle plan,
        unsigned int index, PRuntimeMigrationSource* source);
PRUNTIME_API int PRuntime_MigrationPlanClose(PRuntimeMigrationPlanHandle* plan);
/* Development storage only; installed identity is NOT inferred from same ID.
 * Native decision + manifest SQL declaration required before platform I/O.
 * Native provider anchored to EXE/data opens Runtime's v1/dev bucket. Full
 * framed canonical-root/normalized-ID private DB stamp rejects collisions or
 * wrong files; unclaimed nonempty DB is never adopted. Identity binding may
 * create mode/stamp metadata but never deletes data after failure.
 * Strict version -> all pending sources admitted -> restricted DB migrations
 * in catalog order. Each file is atomic, NOT the whole catalog: on failure
 * stop before author code; retry may resume committed versions, no downgrade.
 * One owner worker for directory/storage/DB; live handles never cross threads.
 * Directory retained by child: DirectoryClose returns STATE until child closes.
 * Callbacks guarded against reentry/Close, no message pump or internal worker.
 * All copied callback code/user targets live through StorageClose. Cancel only
 * reads host atomic state, cannot undo COMMIT or interrupt blocking native I/O.
 * Output slot unchanged on failure. Optional exact v1 error copied on success/
 * failure after valid arguments; invalid error header changes no outputs. */
PRUNTIME_API int PRuntime_DevelopmentStorageOpen(PRuntimeDirectoryHandle directory,
        const PRuntimeStorageOptions* options, const PRuntimeStorageProvider* provider,
        PRuntimeStorageHandle* output, PRuntimeStorageError* error);
PRUNTIME_API int PRuntime_StorageGetInfo(PRuntimeStorageHandle storage, PRuntimeStorageInfo* info);
/* Sticky copied last failure, success does not erase it. */
PRUNTIME_API int PRuntime_StorageGetError(PRuntimeStorageHandle storage, PRuntimeStorageError* error);
/* Native synchronous READY storage. COMPLETE installed-kind revision and
 * approved SQL required; copied v1 options/provider. No CREATE, Bind, migration
 * or READY write. Exact installed identity/schema checked on the retained DB
 * connection. Refresh registration before/after opening and before each SQL
 * request; changed/revoked registration retires storage and parent, publishes
 * no SQL/output. Provider honors the parent's exclusive installation lease:
 * refresh is not an atomic transaction with another registry database.
 * Success retains parent/root/registry/location until StorageClose. Parent
 * Cancel/Close is STATE while attached. Single owner worker, callback reentry
 * rejected; no pump/background thread. Existing StorageInfo/Error/Close and
 * restricted ExecuteJson/ServiceQueue contracts apply, with unchanged budgets.
 * Failure preserves handle
 * output, copies optional StorageError, closes partial DB/location; never repair,
 * retry, claim or grant Browser services. Native diagnostics are not author text. */
PRUNTIME_API int PRuntime_InstalledStorageOpen(PRuntimeRevisionHandle revision,
        const PRuntimeStorageOptions* options, const PRuntimeInstalledStorageProvider* provider,
        PRuntimeStorageHandle* output, PRuntimeStorageError* error);

/* Synchronous worker primitive, NOT a Browser service registration or UI call.
 * v1 strict JSON <=4096 bytes/depth8/tokens768. query/execute: exactly
 * {version:1,method:"query"|"execute",sql:UTF8,params:[typed...]}; transaction:
 * {version:1,method:"transaction",statements:[{kind:"query"|"execute",sql,params}]}.
 * Params exactly {type:"null"}, or {type:"text"|"real"|"int64",value:...}.
 * int64 value is canonical decimal STRING; real finite JSON number; text string.
 * No BLOB/NUL, caller path, DDL, private table or caller transaction control.
 * DB owns SQL authorizer/8x2048 SQL/16 bind/column/total rows/1m shared work.
 * Typed row JSON encoded privately BEFORE COMMIT with 128-byte report reserve;
 * overflow/invalid text/alloc/SQL/cancel failure rolls back and preserves output.
 * Capacity >=4097 required before executing; no side-effecting size probe.
 * Success copies <=4096 JSON bytes + NUL; written excludes NUL. Buffers/count
 * disjoint from request/storage and each other. No allocation after COMMIT.
 * Returns {version,statementCount,rowCount,changes:{type:"int64",value:STRING},
 * rows:[{statement:zeroBasedIndex,values:[typed...]}]}. No column-name guessing.
 * Closed/active/wrong thread STATE. Native callbacks cannot reenter/close.
 * A DB failure leaving a transaction active latches storage FAILED until close;
 * no automatic retry/delete. Host queues copied inputs/results; no callbacks
 * publish rows, and no late UI/session/generation result is replayed as SQL. */
PRUNTIME_API int PRuntime_StorageExecuteJson(PRuntimeStorageHandle storage,
        const char* request, unsigned int request_bytes, char* output,
        unsigned int capacity, unsigned int* written);
/* DB closed before releasing native location; parent remains live. */
PRUNTIME_API int PRuntime_StorageClose(PRuntimeStorageHandle* storage);
/* Explicit native page grant: installed QueueCreate refreshes READY before
 * publishing; ProcessOne rechecks through StorageExecuteJson. Worker-observed
 * installed retirement revokes all sibling queues, drops waiting/results and
 * cancels running SQL. UI sees GetInfo.revoked, must RevokeServices on its
 * Browser owner before teardown; Runtime never calls a UI/session handle.
 * Page/request Cancel/Revoke alone does NOT retire the installed instance.
 * Refresh is not cross-DB atomicity; native coordination remains essential.
 * Create only after trusted storage is READY and
 * on its worker. <=4 independent queues per storage (old/candidate sessions).
 * Duplicate (tab,generation) is STATE, not a second grant. Nonzero tags/exact UI
 * delivery thread; no URL-based grant. Retains storage until queue Close.
 * Queue is a documented cross-thread COPY channel, unlike directory/storage
 * and all live Core/Browser/JS handles. No DLL thread, pump, callback or I/O
 * in Submit/Take/Revoke. Native critical section protects <=8 copied slots,
 * each <=4096 request + <=4096 result bytes, fixed ~70 KiB per queue/~280 KiB
 * total. Worker serializes all queue processing; host owns wake/order policy.
 * Browser owns registration/pump/JS. Host installs only storage.sql, copies
 * Browser request fields into Submit, wakes worker, and completes/pumps on UI.
 * First accepted bridge_id binds this queue; accepted request_id strictly
 * increases, so completion/cancel/revoke can never replay an accepted token. */
PRUNTIME_API int PRuntime_ServiceQueueCreate(PRuntimeStorageHandle storage,
        const PRuntimeServiceQueueOptions* options, PRuntimeServiceQueueHandle* output);
/* UI delivery thread only; validates tags/method/budget and copies, no parse or
 * SQL. <=8 waiting+running+unclaimed results. Failure changes no queue/output.
 * ProcessOne worker-only, one FIFO SQL request, no UI/session pointer. It
 * returns OK even for a SQL failure stored as a result, NOT_FOUND if no job;
 * processed unchanged on API failure. A result is published only after return
 * from DB. Revoke racing COMMIT may discard a committed result, NEVER undo or
 * retry it. Only cooperative DB cancellation, no VFS/host preemption.
 * Direct StorageExecuteJson is STATE while a queue owns that storage. */
PRUNTIME_API int PRuntime_ServiceQueueSubmit(PRuntimeServiceQueueHandle queue, const PRuntimeServiceRequest* request);
PRUNTIME_API int PRuntime_ServiceQueueProcessOne(PRuntimeServiceQueueHandle queue, unsigned int* processed);
/* UI only, consumes once and copies complete result. Native result code and
 * sanitized JSON error contain no SQL/path/message. Empty => NOT_FOUND, outputs
 * unchanged. Check tab/generation before touching Browser; stale result only
 * dropped, never reexecuted. QueueResult is not a live Browser token/handle. */
PRUNTIME_API int PRuntime_ServiceQueueTakeResult(PRuntimeServiceQueueHandle queue, PRuntimeServiceResult* result);
/* UI-only per Browser waiting-token cancel; running requests marked cancel,
 * others removed. Unknown token NOT_FOUND. Revoke callable on worker or UI,
 * permanent/idempotent: drops waiting/results, cancels running, no JS callback.
 * Browser Revoke/PageTeardown plus QueueRevoke precede session destruction.
 * Close worker-only, requires revoked/idle; stop all UI access before Close,
 * then join worker before releasing parent/DLL. No TerminateThread. */
PRUNTIME_API int PRuntime_ServiceQueueCancel(PRuntimeServiceQueueHandle queue, unsigned long bridge_id, unsigned long request_id);
PRUNTIME_API int PRuntime_ServiceQueueRevoke(PRuntimeServiceQueueHandle queue);
PRUNTIME_API int PRuntime_ServiceQueueGetInfo(PRuntimeServiceQueueHandle queue, PRuntimeServiceQueueInfo* info);
PRUNTIME_API int PRuntime_ServiceQueueClose(PRuntimeServiceQueueHandle* queue);
#ifdef __cplusplus
}
#endif
#endif
