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
/* Explicit native page grant: create only after trusted storage is READY and
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
