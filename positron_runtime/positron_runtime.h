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
#ifdef __cplusplus
}
#endif
#endif
