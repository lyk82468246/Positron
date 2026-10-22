#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "positron_runtime.h"
#include "positron_json.h"

struct PRuntimeManifest {
    PRuntimeManifestInfo info;
    PRuntimeMigrationInfo migrations[PRUNTIME_MAX_MIGRATIONS];
};
BOOL WINAPI DllMain(HANDLE module, DWORD reason, LPVOID reserved)
{ (void)module; (void)reason; (void)reserved; return TRUE; }

static int runtime_letter(unsigned char c)
{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int runtime_ascii_equal(const char* a, const char* b)
{
    unsigned char x;
    unsigned char y;
    do {
        x = (unsigned char)*a++; y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    } while (x != 0);
    return 1;
}
PRUNTIME_API int PRuntime_NormalizeId(const char* id, unsigned int bytes,
        char* output, unsigned int capacity)
{
    char next[64];
    unsigned int i;
    unsigned int dots;
    int start;
    unsigned char c;
    if (id == NULL || output == NULL || bytes == 0) return PRUNTIME_ARGUMENT;
    if (bytes > PRUNTIME_ID_BYTES || capacity <= bytes) return PRUNTIME_LIMIT;
    start = 1; dots = 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)id[i];
        if (c == '.') {
            if (start) return PRUNTIME_INVALID;
            dots++; start = 1;
        } else {
            if (!runtime_letter(c) && (start || !((c >= '0' && c <= '9') || c == '_'))) return PRUNTIME_INVALID;
            start = 0;
        }
        next[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    if (start || dots == 0) return PRUNTIME_INVALID;
    next[bytes] = '\0'; memcpy(output, next, bytes + 1);
    return PRUNTIME_OK;
}
/* Validate public UTF-8 text boundaries, not JSON grammar. */
static int runtime_text(const char* text, unsigned int bytes)
{
    unsigned int i;
    unsigned int extra;
    unsigned long code;
    unsigned long minimum;
    unsigned char c;
    if (text == NULL || bytes == 0) return 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)text[i];
        if (c < 32 || c == 127) return 0;
        if (c < 128) continue;
        if (c >= 194 && c <= 223) { extra = 1; code = c & 31; minimum = 128; }
        else if (c >= 224 && c <= 239) { extra = 2; code = c & 15; minimum = 2048; }
        else if (c >= 240 && c <= 244) { extra = 3; code = c & 7; minimum = 65536; }
        else return 0;
        while (extra-- > 0) {
            if (++i >= bytes || ((unsigned char)text[i] & 192) != 128) return 0;
            code = (code << 6) | ((unsigned char)text[i] & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
                (code >= 128 && code <= 159)) return 0;
    }
    return 1;
}
PRUNTIME_API int PRuntime_ValidateResourcePath(const char* path, unsigned int bytes)
{
    unsigned int i;
    unsigned int start;
    unsigned int segments;
    unsigned int count;
    if (path == NULL || bytes == 0) return PRUNTIME_ARGUMENT;
    if (bytes > PRUNTIME_PATH_BYTES) return PRUNTIME_LIMIT;
    if (!runtime_text(path, bytes)) return PRUNTIME_INVALID;
    start = 0; segments = 0;
    for (i = 0; i <= bytes; i++) {
        if (i != bytes && strchr("\\:%?#*\"<>|", (unsigned char)path[i]) != NULL) return PRUNTIME_INVALID;
        if (i == bytes || path[i] == '/') {
            count = i - start;
            if (count == 0 || (count == 1 && path[start] == '.') ||
                    (count == 2 && path[start] == '.' && path[start + 1] == '.') ||
                    path[i - 1] == '.' || path[i - 1] == ' ') return PRUNTIME_INVALID;
            if (++segments > 16) return PRUNTIME_LIMIT;
            start = i + 1;
        }
    }
    return PRUNTIME_OK;
}
static int runtime_keys(HANDLE object, const char* const* names, int count)
{
    int i;
    int j;
    const char* key;
    if (PJson_GetType(object) != PJSON_TYPE_OBJECT || PJson_GetObjectSize(object) != count) return 0;
    for (i = 0; i < count; i++) {
        key = PJson_GetObjectKey(object, i);
        for (j = 0; j < count; j++) if (!strcmp(key, names[j])) break;
        if (j == count) return 0;
    }
    return 1;
}
static int runtime_integer(HANDLE object, const char* key, unsigned long* output)
{
    double number;
    if (!PJson_GetDouble(object, key, &number) || number < 1 || number > 2147483647.0 ||
            number != (double)(unsigned long)number) return 0;
    *output = (unsigned long)number; return 1;
}
static int runtime_path(HANDLE object, const char* key, char* output)
{
    const char* text;
    int rc;
    text = PJson_GetString(object, key);
    if (text == NULL) return PRUNTIME_INVALID;
    rc = PRuntime_ValidateResourcePath(text, (unsigned int)strlen(text));
    if (rc == PRUNTIME_ARGUMENT) rc = PRUNTIME_INVALID;
    if (rc == PRUNTIME_OK) strcpy(output, text);
    return rc;
}
PRUNTIME_API int PRuntime_ManifestCreate(const char* json, unsigned int bytes,
        PRuntimeManifestHandle* output)
{
    static const char* const fields[] = { "manifestVersion", "id", "name", "version",
        "runtimeApiVersion", "entry", "permissions", "migrations" };
    static const char* const migration_fields[] = { "version", "file" };
    PJsonParseLimits limits;
    HANDLE root;
    HANDLE permissions;
    HANDLE migrations;
    HANDLE item;
    PRuntimeManifest* manifest;
    const char* text;
    unsigned long version;
    unsigned int i;
    unsigned int j;
    int rc;
    if (output == NULL || json == NULL || bytes == 0) return PRUNTIME_ARGUMENT;
    if (bytes > PRUNTIME_MANIFEST_BYTES) return PRUNTIME_LIMIT;
    limits.size = sizeof(limits); limits.version = PJSON_PARSE_VERSION;
    limits.max_bytes = PRUNTIME_MANIFEST_BYTES; limits.max_depth = 8; limits.max_tokens = 256;
    root = NULL;
    rc = PJson_ParseEx(json, bytes, &limits, &root);
    if (rc != PJSON_PARSE_OK) return rc == PJSON_PARSE_LIMIT ? PRUNTIME_LIMIT : PRUNTIME_INVALID;
    manifest = (PRuntimeManifest*)calloc(1, sizeof(*manifest));
    if (manifest == NULL) { PJson_Free(root); return PRUNTIME_MEMORY; }
    rc = PRUNTIME_INVALID;
    manifest->info.size = sizeof(manifest->info); manifest->info.version = PRUNTIME_VERSION;
    if (!runtime_keys(root, fields, 8)) goto done;
    if (!runtime_integer(root, "manifestVersion", &version)) goto done;
    if (version != 1) { rc = PRUNTIME_UNSUPPORTED; goto done; }
    if (!runtime_integer(root, "runtimeApiVersion", &version)) goto done;
    if (version != 1) { rc = PRUNTIME_UNSUPPORTED; goto done; }
    if (!runtime_integer(root, "version", &manifest->info.app_version)) goto done;
    text = PJson_GetString(root, "id");
    if (text == NULL) goto done;
    rc = PRuntime_NormalizeId(text, (unsigned int)strlen(text), manifest->info.id, sizeof(manifest->info.id));
    if (rc == PRUNTIME_ARGUMENT) rc = PRUNTIME_INVALID;
    if (rc != PRUNTIME_OK) goto done;
    rc = PRUNTIME_INVALID;
    text = PJson_GetString(root, "name");
    if (text == NULL || !runtime_text(text, (unsigned int)strlen(text))) goto done;
    if (strlen(text) >= sizeof(manifest->info.name)) { rc = PRUNTIME_LIMIT; goto done; }
    strcpy(manifest->info.name, text);
    rc = runtime_path(root, "entry", manifest->info.entry);
    if (rc != PRUNTIME_OK) goto done;
    rc = PRUNTIME_INVALID;
    permissions = PJson_GetObject(root, "permissions");
    migrations = PJson_GetObject(root, "migrations");
    if (PJson_GetType(permissions) != PJSON_TYPE_ARRAY || PJson_GetType(migrations) != PJSON_TYPE_ARRAY) goto done;
    if (PJson_GetArraySize(permissions) > 1) goto done;
    if (PJson_GetArraySize(permissions) == 1) {
        text = PJson_GetStringValue(PJson_GetArrayItem(permissions, 0));
        if (text == NULL) goto done;
        if (strcmp(text, "storage.sql")) { rc = PRUNTIME_UNSUPPORTED; goto done; }
        manifest->info.requested_permissions = PRUNTIME_PERMISSION_SQL;
    }
    manifest->info.migration_count = (unsigned int)PJson_GetArraySize(migrations);
    if (manifest->info.migration_count > PRUNTIME_MAX_MIGRATIONS) { rc = PRUNTIME_LIMIT; goto done; }
    if (manifest->info.migration_count && !manifest->info.requested_permissions) goto done;
    for (i = 0; i < manifest->info.migration_count; i++) {
        item = PJson_GetArrayItem(migrations, (int)i);
        if (!runtime_keys(item, migration_fields, 2) || !runtime_integer(item, "version", &version) || version != i + 1) goto done;
        manifest->migrations[i].size = sizeof(PRuntimeMigrationInfo);
        manifest->migrations[i].version = PRUNTIME_VERSION; manifest->migrations[i].schema_version = version;
        rc = runtime_path(item, "file", manifest->migrations[i].file);
        if (rc != PRUNTIME_OK) goto done;
        rc = PRUNTIME_INVALID;
        for (j = 0; j < i; j++) if (runtime_ascii_equal(manifest->migrations[i].file, manifest->migrations[j].file)) goto done;
    }
    rc = PRUNTIME_OK;
done:
    PJson_Free(root);
    if (rc == PRUNTIME_OK) *output = manifest;
    else free(manifest);
    return rc;
}
PRUNTIME_API int PRuntime_ManifestGetInfo(PRuntimeManifestHandle manifest, PRuntimeManifestInfo* info)
{
    if (manifest == NULL || info == NULL || info->size != sizeof(*info) || info->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    *info = manifest->info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_ManifestGetMigration(PRuntimeManifestHandle manifest,
        unsigned int index, PRuntimeMigrationInfo* info)
{
    if (manifest == NULL || info == NULL || info->size != sizeof(*info) || info->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    if (index >= manifest->info.migration_count) return PRUNTIME_ARGUMENT;
    *info = manifest->migrations[index]; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_ManifestClose(PRuntimeManifestHandle* manifest)
{
    if (manifest == NULL) return PRUNTIME_ARGUMENT;
    free(*manifest); *manifest = NULL; return PRUNTIME_OK;
}
