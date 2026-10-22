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
#define PRUNTIME_MANIFEST_BYTES 8192
#define PRUNTIME_ID_BYTES 63
#define PRUNTIME_PATH_BYTES 255
#define PRUNTIME_MAX_MIGRATIONS 16
#define PRUNTIME_PERMISSION_SQL 1UL
typedef struct PRuntimeManifest PRuntimeManifest;
typedef PRuntimeManifest* PRuntimeManifestHandle;
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
 * binding, app URL resolution, SQL services and PXI remain separate contracts.
 * Borrowed input only during Create; snapshots copy, queries leave output
 * unchanged on error. Call on one owner thread; freed aliases are invalid. */
PRUNTIME_API int PRuntime_ManifestCreate(const char* json, unsigned int bytes,
        PRuntimeManifestHandle* output);
PRUNTIME_API int PRuntime_ManifestGetInfo(PRuntimeManifestHandle manifest,
        PRuntimeManifestInfo* info);
PRUNTIME_API int PRuntime_ManifestGetMigration(PRuntimeManifestHandle manifest,
        unsigned int index, PRuntimeMigrationInfo* info);
/* Clears owned slot; repeated NULL value is safe; NULL slot is ARGUMENT. */
PRUNTIME_API int PRuntime_ManifestClose(PRuntimeManifestHandle* manifest);
#ifdef __cplusplus
}
#endif
#endif
