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
#define PRUNTIME_URL_BYTES 1023
#define PRUNTIME_SUFFIX_BYTES 255
#define PRUNTIME_URL_QUERY 1U
#define PRUNTIME_URL_FRAGMENT 2U
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
#ifdef __cplusplus
}
#endif
#endif
