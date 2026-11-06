#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "positron_runtime.h"
#include "positron_json.h"
#include "positron_db.h"

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
static int runtime_text_mode(const char* text, unsigned int bytes, int sql)
{
    unsigned int i;
    unsigned int extra;
    unsigned long code;
    unsigned long minimum;
    unsigned char c;
    if (text == NULL || bytes == 0) return 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)text[i];
        if (c < 32 || c == 127) {
            if (sql && (c == 9 || c == 10 || c == 13)) continue;
            return 0;
        }
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
static int runtime_text(const char* text, unsigned int bytes)
{ return runtime_text_mode(text, bytes, 0); }
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

typedef struct RuntimeUrlParts {
    const char* path;
    unsigned int path_bytes;
    const char* query;
    unsigned int query_bytes;
    const char* fragment;
    unsigned int fragment_bytes;
    unsigned int flags;
    int absolute;
} RuntimeUrlParts;
static int runtime_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static int runtime_unreserved(unsigned char c)
{ return runtime_letter(c) || (c >= '0' && c <= '9') || strchr("-._~", c) != NULL; }
static int runtime_url_parts(const char* text, unsigned int bytes, const char* id,
        int require_absolute, RuntimeUrlParts* parts)
{
    unsigned int i;
    unsigned int start;
    unsigned int end;
    char authority[64];
    int rc;
    memset(parts, 0, sizeof(*parts));
    if (bytes > PRUNTIME_URL_BYTES) return PRUNTIME_LIMIT;
    if (bytes && !runtime_text(text, bytes)) return PRUNTIME_INVALID;
    /* URI whitespace/backslashes are never implicit native path aliases. */
    for (i = 0; i < bytes; i++) if (text[i] == '\\' || text[i] == ' ') return PRUNTIME_INVALID;
    start = 0;
    for (i = 0; i < bytes && text[i] != '/' && text[i] != '?' && text[i] != '#'; i++) {
        if (text[i] == ':') {
            if (i != 3 || (text[0] != 'a' && text[0] != 'A') ||
                    (text[1] != 'p' && text[1] != 'P') || (text[2] != 'p' && text[2] != 'P'))
                return PRUNTIME_UNSUPPORTED;
            if (bytes < 6 || text[4] != '/' || text[5] != '/') return PRUNTIME_INVALID;
            start = 6; parts->absolute = 1; break;
        }
    }
    if (!parts->absolute && bytes >= 2 && text[0] == '/' && text[1] == '/') {
        start = 2; parts->absolute = 1;
    }
    if (require_absolute && (!parts->absolute || start != 6)) return PRUNTIME_INVALID;
    if (parts->absolute) {
        end = start;
        while (end < bytes && text[end] != '/' && text[end] != '?' && text[end] != '#') end++;
        rc = PRuntime_NormalizeId(text + start, end - start, authority, sizeof(authority));
        if (rc != PRUNTIME_OK) return rc == PRUNTIME_LIMIT ? rc : PRUNTIME_INVALID;
        if (strcmp(authority, id)) return PRUNTIME_INVALID;
        start = end;
    }
    end = start;
    while (end < bytes && text[end] != '?' && text[end] != '#') end++;
    parts->path = text + start; parts->path_bytes = end - start;
    if (end < bytes && text[end] == '?') {
        start = ++end;
        while (end < bytes && text[end] != '#') end++;
        parts->query = text + start; parts->query_bytes = end - start;
        parts->flags |= PRUNTIME_URL_QUERY;
    }
    if (end < bytes) {
        parts->fragment = text + end + 1; parts->fragment_bytes = bytes - end - 1;
        parts->flags |= PRUNTIME_URL_FRAGMENT;
    }
    return PRUNTIME_OK;
}
/* Decode once, rejecting encoded path syntax rather than accepting aliases. */
static int runtime_decode(const char* source, unsigned int bytes, char* output,
        unsigned int capacity, int path)
{
    unsigned int i;
    unsigned int used;
    int high;
    int low;
    unsigned char c;
    used = 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)source[i];
        if (c == '%') {
            if (i + 2 >= bytes || (high = runtime_hex(source[i + 1])) < 0 ||
                    (low = runtime_hex(source[i + 2])) < 0) return PRUNTIME_INVALID;
            c = (unsigned char)((high << 4) | low); i += 2;
            if (path && strchr("/.\\:%?#*\"<>|", c) != NULL) return PRUNTIME_INVALID;
        }
        if (c == 0 || c < 32 || c == 127 || c == '\\') return PRUNTIME_INVALID;
        if (used + 1 >= capacity) return PRUNTIME_LIMIT;
        output[used++] = (char)c;
    }
    output[used] = '\0';
    if (used && !runtime_text(output, used)) return PRUNTIME_INVALID;
    return PRUNTIME_OK;
}
static int runtime_escape(const char* source, char* output, unsigned int capacity, int path)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned int used;
    unsigned char c;
    used = 0;
    while ((c = (unsigned char)*source++) != 0) {
        if (runtime_unreserved(c) || (path && c == '/')) {
            if (used + 1 >= capacity) return PRUNTIME_LIMIT;
            output[used++] = (char)c;
        } else {
            if (used + 3 >= capacity) return PRUNTIME_LIMIT;
            output[used++] = '%'; output[used++] = digits[c >> 4]; output[used++] = digits[c & 15];
        }
    }
    output[used] = '\0'; return PRUNTIME_OK;
}
/* Preserve URI reserved delimiters and escaped octets: %26 is not '&'. */
static int runtime_suffix(const char* source, unsigned int bytes, char* output)
{
    static const char digits[] = "0123456789ABCDEF";
    char decoded[1024];
    unsigned int i;
    unsigned int used;
    unsigned char c;
    int value;
    int rc;
    rc = runtime_decode(source, bytes, decoded, sizeof(decoded), 0);
    if (rc != PRUNTIME_OK) return rc;
    used = 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)source[i];
        if (c == '%') {
            if (used + 3 > PRUNTIME_SUFFIX_BYTES) return PRUNTIME_LIMIT;
            value = (runtime_hex(source[i + 1]) << 4) | runtime_hex(source[i + 2]);
            output[used++] = '%'; output[used++] = digits[value >> 4]; output[used++] = digits[value & 15]; i += 2;
        } else if (runtime_unreserved(c) || strchr("!$&'()*+,;=:@/?", c) != NULL) {
            if (used == PRUNTIME_SUFFIX_BYTES) return PRUNTIME_LIMIT;
            output[used++] = (char)c;
        } else {
            if (used + 3 > PRUNTIME_SUFFIX_BYTES) return PRUNTIME_LIMIT;
            output[used++] = '%'; output[used++] = digits[c >> 4]; output[used++] = digits[c & 15];
        }
    }
    output[used] = '\0'; return PRUNTIME_OK;
}
static int runtime_resolve_path(const char* base, const RuntimeUrlParts* parts,
        const char* entry, char* output)
{
    char decoded[1024];
    char combined[1280];
    unsigned int used;
    unsigned int start;
    unsigned int end;
    unsigned int count;
    const char* slash;
    int rc;
    rc = runtime_decode(parts->path, parts->path_bytes, decoded, sizeof(decoded), 1);
    if (rc != PRUNTIME_OK) return rc;
    if (parts->absolute || decoded[0] == '/') {
        if (decoded[0] == '/' && decoded[1] == '/') return PRUNTIME_INVALID;
        strcpy(combined, decoded[0] == '/' ? decoded + 1 : decoded);
        if (!combined[0]) strcpy(combined, entry);
    } else if (!decoded[0]) strcpy(combined, base);
    else {
        slash = strrchr(base, '/'); used = slash ? (unsigned int)(slash - base + 1) : 0;
        memcpy(combined, base, used); strcpy(combined + used, decoded);
    }
    used = 0; start = 0;
    for (end = 0; ; end++) {
        if (combined[end] != '/' && combined[end] != 0) continue;
        count = end - start;
        if (count == 0) return PRUNTIME_INVALID;
        if (count == 1 && combined[start] == '.') {
            if (!combined[end]) return PRUNTIME_INVALID;
        } else if (count == 2 && combined[start] == '.' && combined[start + 1] == '.') {
            if (!used || !combined[end]) return PRUNTIME_INVALID;
            while (used && output[used - 1] != '/') used--;
            if (used) used--;
        } else {
            if (used + (used ? 1 : 0) + count > PRUNTIME_PATH_BYTES) return PRUNTIME_LIMIT;
            if (used) output[used++] = '/';
            memcpy(output + used, combined + start, count); used += count;
        }
        if (!combined[end]) break;
        start = end + 1;
    }
    output[used] = '\0';
    return PRuntime_ValidateResourcePath(output, used);
}
PRUNTIME_API int PRuntime_ManifestResolveReference(PRuntimeManifestHandle manifest,
        const char* base, unsigned int base_bytes, const char* reference,
        unsigned int reference_bytes, PRuntimeResourceReference* output)
{
    RuntimeUrlParts prior;
    RuntimeUrlParts next;
    PRuntimeResourceReference result;
    char base_path[256];
    char escaped[768];
    char suffix[256];
    unsigned int needed;
    int rc;
    if (!manifest || !output || output->size != sizeof(*output) || output->version != PRUNTIME_VERSION ||
            (!base && base_bytes) || (!reference && reference_bytes)) return PRUNTIME_ARGUMENT;
    memset(&prior, 0, sizeof(prior)); strcpy(base_path, manifest->info.entry);
    if (base) {
        rc = runtime_url_parts(base, base_bytes, manifest->info.id, 1, &prior);
        if (rc != PRUNTIME_OK) return rc;
        rc = runtime_resolve_path(manifest->info.entry, &prior, manifest->info.entry, base_path);
        if (rc != PRUNTIME_OK) return rc;
        rc = runtime_suffix(prior.query, prior.query_bytes, suffix);
        if (rc != PRUNTIME_OK) return rc;
        rc = runtime_suffix(prior.fragment, prior.fragment_bytes, suffix);
        if (rc != PRUNTIME_OK) return rc;
    }
    rc = runtime_url_parts(reference ? reference : "", reference_bytes, manifest->info.id, 0, &next);
    if (rc != PRUNTIME_OK) return rc;
    memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PRUNTIME_VERSION;
    rc = runtime_resolve_path(base_path, &next, manifest->info.entry, result.path);
    if (rc != PRUNTIME_OK) return rc;
    if (!next.absolute && !next.path_bytes && !(next.flags & PRUNTIME_URL_QUERY) && (prior.flags & PRUNTIME_URL_QUERY)) {
        next.query = prior.query; next.query_bytes = prior.query_bytes; next.flags |= PRUNTIME_URL_QUERY;
    }
    result.flags = next.flags;
    rc = runtime_suffix(next.query, next.query_bytes, result.query);
    if (rc != PRUNTIME_OK) return rc;
    rc = runtime_suffix(next.fragment, next.fragment_bytes, result.fragment);
    if (rc != PRUNTIME_OK) return rc;
    rc = runtime_escape(result.path, escaped, sizeof(escaped), 1);
    if (rc != PRUNTIME_OK) return rc;
    needed = 6 + (unsigned int)strlen(manifest->info.id) + 1 + (unsigned int)strlen(escaped) +
        (result.flags & PRUNTIME_URL_QUERY ? 1 + (unsigned int)strlen(result.query) : 0) +
        (result.flags & PRUNTIME_URL_FRAGMENT ? 1 + (unsigned int)strlen(result.fragment) : 0);
    if (needed > PRUNTIME_URL_BYTES) return PRUNTIME_LIMIT;
    strcpy(result.url, "app://"); strcat(result.url, manifest->info.id); strcat(result.url, "/"); strcat(result.url, escaped);
    if (result.flags & PRUNTIME_URL_QUERY) { strcat(result.url, "?"); strcat(result.url, result.query); }
    if (result.flags & PRUNTIME_URL_FRAGMENT) { strcat(result.url, "#"); strcat(result.url, result.fragment); }
    *output = result; return PRUNTIME_OK;
}

struct PRuntimeDirectory {
    PRuntimeDirectoryProvider provider;
    PRuntimeDirectoryRoot root;
    PRuntimeManifestHandle manifest;
    void* lease;
    DWORD owner;
    int active;
    unsigned int storage_count;
};
static int runtime_provider_result(int rc, int lock)
{
    if (rc == PRUNTIME_OK || rc == PRUNTIME_IO || rc == PRUNTIME_LIMIT ||
            rc == PRUNTIME_UNSUPPORTED || rc == PRUNTIME_MEMORY || (lock && rc == PRUNTIME_BUSY)) return rc;
    return PRUNTIME_IO;
}
static void runtime_directory_dispose(PRuntimeDirectoryHandle directory)
{
    PRuntime_ManifestClose(&directory->manifest);
    if (directory->lease) directory->provider.release_lock(directory->provider.user_data, directory->lease);
    if (directory->root.root) directory->provider.close_root(directory->provider.user_data, directory->root.root);
    free(directory);
}
/* Lock buckets are deliberately not persistent data identities. A collision
 * only excludes a simultaneous launch; it never combines data/permissions.
 * Hash framed, NUL-free canonical root and normalized ID, not caller aliases. */
static void runtime_directory_lock_name(const char* root, const char* id, char* name)
{
    static const char digits[] = "0123456789abcdef";
    unsigned long a;
    unsigned long b;
    unsigned long value;
    unsigned char c;
    unsigned int i;
    int part;
    const char* text;
    a = 2166136261UL; b = 2246822519UL;
    for (part = 0; part < 2; part++) {
        text = part ? id : root;
        do {
            c = (unsigned char)*text++;
            a = (a ^ c) * 16777619UL;
            b = (b ^ c) * 3266489917UL;
        } while (c);
    }
    strcpy(name, "Positron.Dev.v1.");
    for (part = 0; part < 2; part++) {
        value = part ? b : a;
        for (i = 0; i < 8; i++) name[16 + part * 8 + i] = digits[(value >> (28 - 4 * i)) & 15];
    }
    name[32] = 0;
}
static int runtime_directory_ready(PRuntimeDirectoryHandle directory)
{
    if (!directory) return PRUNTIME_ARGUMENT;
    if (directory->owner != GetCurrentThreadId() || directory->active) return PRUNTIME_STATE;
    return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_DevelopmentDirectoryOpen(const char* path, unsigned int bytes,
        const PRuntimeDirectoryProvider* provider, PRuntimeDirectoryHandle* output)
{
    PRuntimeDirectoryHandle directory;
    PRuntimeBorrowedBytes borrowed;
    char lock_name[33];
    int rc;
    if (!path || !bytes || !output || !provider || provider->size != sizeof(*provider) ||
            provider->version != PRUNTIME_VERSION || !provider->open_root || !provider->close_root ||
            !provider->acquire_lock || !provider->release_lock || !provider->read || !provider->release_bytes)
        return PRUNTIME_ARGUMENT;
    if (bytes > PRUNTIME_ROOT_BYTES) return PRUNTIME_LIMIT;
    if (!runtime_text(path, bytes)) return PRUNTIME_INVALID;
    directory = (PRuntimeDirectoryHandle)calloc(1, sizeof(*directory));
    if (!directory) return PRUNTIME_MEMORY;
    directory->provider = *provider; provider = &directory->provider;
    directory->owner = GetCurrentThreadId(); directory->active = 1;
    rc = runtime_provider_result(provider->open_root(provider->user_data, path, bytes, &directory->root), 0);
    if (rc != PRUNTIME_OK) goto failed;
    if (!directory->root.root || !directory->root.canonical_bytes || directory->root.canonical_bytes > PRUNTIME_ROOT_BYTES ||
            !runtime_text(directory->root.canonical, directory->root.canonical_bytes) ||
            directory->root.canonical[directory->root.canonical_bytes] != 0) { rc = PRUNTIME_IO; goto failed; }
    memset(&borrowed, 0, sizeof(borrowed));
    rc = runtime_provider_result(provider->read(provider->user_data, directory->root.root,
            "manifest.json", PRUNTIME_MANIFEST_BYTES, &borrowed), 0);
    if (rc == PRUNTIME_OK) {
        if (borrowed.bytes > PRUNTIME_MANIFEST_BYTES) rc = PRUNTIME_LIMIT;
        else if (!borrowed.data || !borrowed.bytes || !borrowed.token) rc = PRUNTIME_IO;
        else rc = PRuntime_ManifestCreate((const char*)borrowed.data, borrowed.bytes, &directory->manifest);
    }
    if (borrowed.token) provider->release_bytes(provider->user_data, directory->root.root, &borrowed);
    if (rc != PRUNTIME_OK) goto failed;
    runtime_directory_lock_name(directory->root.canonical, directory->manifest->info.id, lock_name);
    rc = runtime_provider_result(provider->acquire_lock(provider->user_data, lock_name, &directory->lease), 1);
    if (rc != PRUNTIME_OK) goto failed;
    if (!directory->lease) { rc = PRUNTIME_IO; goto failed; }
    directory->active = 0; *output = directory; return PRUNTIME_OK;
failed:
    runtime_directory_dispose(directory); return rc;
}
PRUNTIME_API int PRuntime_DirectoryGetInfo(PRuntimeDirectoryHandle directory, PRuntimeManifestInfo* info)
{
    int rc;
    rc = runtime_directory_ready(directory);
    if (rc != PRUNTIME_OK) return rc;
    return PRuntime_ManifestGetInfo(directory->manifest, info);
}
PRUNTIME_API int PRuntime_DirectoryResolveReference(PRuntimeDirectoryHandle directory,
        const char* base, unsigned int base_bytes, const char* reference,
        unsigned int reference_bytes, PRuntimeResourceReference* output)
{
    int rc;
    rc = runtime_directory_ready(directory);
    if (rc != PRUNTIME_OK) return rc;
    return PRuntime_ManifestResolveReference(directory->manifest, base, base_bytes, reference, reference_bytes, output);
}
PRUNTIME_API int PRuntime_DirectoryReadResource(PRuntimeDirectoryHandle directory,
        const char* base, unsigned int base_bytes, const char* reference,
        unsigned int reference_bytes, void* buffer, unsigned int capacity,
        unsigned int* written, PRuntimeResourceReference* resolved)
{
    PRuntimeResourceReference next;
    PRuntimeBorrowedBytes borrowed;
    unsigned int count;
    int rc;
    rc = runtime_directory_ready(directory);
    if (rc != PRUNTIME_OK) return rc;
    if (!buffer || !written || !resolved || resolved->size != sizeof(*resolved) ||
            resolved->version != PRUNTIME_VERSION || !capacity) return PRUNTIME_ARGUMENT;
    if (capacity > PRUNTIME_RESOURCE_BYTES) return PRUNTIME_LIMIT;
    next.size = sizeof(next); next.version = PRUNTIME_VERSION;
    rc = PRuntime_ManifestResolveReference(directory->manifest, base, base_bytes, reference, reference_bytes, &next);
    if (rc != PRUNTIME_OK) return rc;
    directory->active = 1; memset(&borrowed, 0, sizeof(borrowed));
    rc = runtime_provider_result(directory->provider.read(directory->provider.user_data, directory->root.root,
            next.path, capacity, &borrowed), 0);
    count = borrowed.bytes;
    if (rc == PRUNTIME_OK) {
        if (count > capacity) rc = PRUNTIME_LIMIT;
        else if (!borrowed.token || (count && !borrowed.data)) rc = PRUNTIME_IO;
        else if (count) memcpy(buffer, borrowed.data, count);
    }
    if (borrowed.token) directory->provider.release_bytes(directory->provider.user_data, directory->root.root, &borrowed);
    directory->active = 0;
    if (rc == PRUNTIME_OK) { *written = count; *resolved = next; }
    return rc;
}
PRUNTIME_API int PRuntime_DirectoryClose(PRuntimeDirectoryHandle* directory)
{
    int rc;
    if (!directory) return PRUNTIME_ARGUMENT;
    if (!*directory) return PRUNTIME_OK;
    rc = runtime_directory_ready(*directory);
    if (rc != PRUNTIME_OK) return rc;
    if ((*directory)->storage_count) return PRUNTIME_STATE;
    (*directory)->active = 1; runtime_directory_dispose(*directory); *directory = NULL;
    return PRUNTIME_OK;
}

struct PRuntimeMigrationPlan {
    PRuntimeMigrationPlanInfo info;
    PRuntimeMigrationSource sources[PRUNTIME_MAX_MIGRATIONS];
    DWORD owner;
};
static void runtime_plan_dispose(PRuntimeMigrationPlanHandle plan)
{
    unsigned int i;
    for (i = 0; i < PRUNTIME_MAX_MIGRATIONS; i++) free((void*)plan->sources[i].sql);
    free(plan);
}
PRUNTIME_API int PRuntime_DirectoryPrepareMigrations(PRuntimeDirectoryHandle directory,
        const PRuntimeMigrationPlanOptions* options, PRuntimeMigrationPlanHandle* output)
{
    PRuntimeMigrationPlanHandle plan;
    PRuntimeMigrationSource* source;
    PRuntimeMigrationInfo* migration;
    PRuntimeBorrowedBytes borrowed;
    unsigned int i;
    int rc;
    rc = runtime_directory_ready(directory);
    if (rc != PRUNTIME_OK) return rc;
    if (!options || !output || options->size != sizeof(*options) || options->version != PRUNTIME_VERSION)
        return PRUNTIME_ARGUMENT;
    if (options->granted_permissions & ~PRUNTIME_PERMISSION_SQL) return PRUNTIME_UNSUPPORTED;
    if (!(options->granted_permissions & directory->manifest->info.requested_permissions & PRUNTIME_PERMISSION_SQL))
        return PRUNTIME_DENIED;
    if (options->current_schema_version > directory->manifest->info.migration_count) return PRUNTIME_SCHEMA;
    plan = (PRuntimeMigrationPlanHandle)calloc(1, sizeof(*plan));
    if (!plan) return PRUNTIME_MEMORY;
    plan->owner = GetCurrentThreadId(); plan->info.size = sizeof(plan->info); plan->info.version = PRUNTIME_VERSION;
    strcpy(plan->info.id, directory->manifest->info.id);
    plan->info.previous_version = options->current_schema_version;
    plan->info.target_version = directory->manifest->info.migration_count;
    directory->active = 1;
    for (i = (unsigned int)options->current_schema_version; i < directory->manifest->info.migration_count; i++) {
        migration = &directory->manifest->migrations[i];
        memset(&borrowed, 0, sizeof(borrowed));
        rc = runtime_provider_result(directory->provider.read(directory->provider.user_data, directory->root.root,
                migration->file, PRUNTIME_MIGRATION_SQL_BYTES, &borrowed), 0);
        if (rc == PRUNTIME_OK) {
            if (borrowed.bytes > PRUNTIME_MIGRATION_SQL_BYTES ||
                    borrowed.bytes > PRUNTIME_MIGRATION_TOTAL_BYTES - plan->info.total_bytes) rc = PRUNTIME_LIMIT;
            else if (!borrowed.token || (borrowed.bytes && !borrowed.data)) rc = PRUNTIME_IO;
            else if (!runtime_text_mode((const char*)borrowed.data, borrowed.bytes, 1) ||
                    (borrowed.bytes >= 3 && !memcmp(borrowed.data, "\xef\xbb\xbf", 3))) rc = PRUNTIME_INVALID;
            else {
                source = &plan->sources[plan->info.file_count];
                source->sql = (const char*)malloc(borrowed.bytes);
                if (!source->sql) rc = PRUNTIME_MEMORY;
                else {
                    memcpy((void*)source->sql, borrowed.data, borrowed.bytes);
                    source->size = sizeof(*source); source->version = PRUNTIME_VERSION;
                    source->schema_version = migration->schema_version; strcpy(source->file, migration->file);
                    source->sql_bytes = borrowed.bytes;
                    plan->info.file_count++; plan->info.total_bytes += borrowed.bytes;
                }
            }
        }
        if (borrowed.token) directory->provider.release_bytes(directory->provider.user_data, directory->root.root, &borrowed);
        if (rc != PRUNTIME_OK) break;
    }
    directory->active = 0;
    if (rc != PRUNTIME_OK) { runtime_plan_dispose(plan); return rc; }
    *output = plan; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_MigrationPlanGetInfo(PRuntimeMigrationPlanHandle plan, PRuntimeMigrationPlanInfo* info)
{
    if (!plan || !info || info->size != sizeof(*info) || info->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    if (plan->owner != GetCurrentThreadId()) return PRUNTIME_STATE;
    *info = plan->info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_MigrationPlanGetSource(PRuntimeMigrationPlanHandle plan,
        unsigned int index, PRuntimeMigrationSource* source)
{
    if (!plan || !source || source->size != sizeof(*source) || source->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    if (plan->owner != GetCurrentThreadId()) return PRUNTIME_STATE;
    if (index >= plan->info.file_count) return PRUNTIME_ARGUMENT;
    *source = plan->sources[index]; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_MigrationPlanClose(PRuntimeMigrationPlanHandle* plan)
{
    if (!plan) return PRUNTIME_ARGUMENT;
    if (!*plan) return PRUNTIME_OK;
    if ((*plan)->owner != GetCurrentThreadId()) return PRUNTIME_STATE;
    runtime_plan_dispose(*plan); *plan = NULL; return PRUNTIME_OK;
}

struct PRuntimeStorage {
    PRuntimeDirectoryHandle directory;
    PRuntimeRevisionHandle revision;
    PRuntimeStorageProvider provider;
    PRuntimeStorageOptions options;
    PRuntimeStorageLocation location;
    PRuntimeStorageInfo info;
    PRuntimeStorageError error;
    PDbHandle db;
    DWORD owner;
    int active;
    unsigned int queue_count;
    PRuntimeServiceQueueHandle queues[PRUNTIME_SERVICE_MAX_QUEUES];
    int queue_processing;
    int instance_cancelled;
    volatile LONG* operation_cancel;
};
static int runtime_storage_parent_busy(PRuntimeStorageHandle storage);
static void runtime_storage_parent_active(PRuntimeStorageHandle storage, int active);
static void runtime_storage_parent_detach(PRuntimeStorageHandle storage);
static int runtime_storage_refresh(PRuntimeStorageHandle storage);
static int runtime_storage_parent_cancelled(PRuntimeStorageHandle storage);
static void runtime_storage_retire(PRuntimeStorageHandle storage, int result);
static void runtime_storage_error_init(PRuntimeStorageError* error, int result)
{
    memset(error, 0, sizeof(*error)); error->size = sizeof(*error); error->version = PRUNTIME_VERSION;
    error->result = result;
}
static int runtime_db_error(PDbHandle db, PRuntimeStorageError* target, int result, unsigned long schema)
{
    PDbErrorInfo error;
    int rc;
    error.size = sizeof(error); error.version = PDB_ERROR_INFO_VERSION;
    rc = PRUNTIME_DATABASE;
    if (result == PDB_NOMEM) rc = PRUNTIME_MEMORY;
    else if (result == PDB_LIMIT) rc = PRUNTIME_LIMIT;
    else if (result == PDB_BUSY) rc = PRUNTIME_BUSY;
    else if (result == PDB_SCHEMA_MISMATCH) rc = PRUNTIME_SCHEMA;
    runtime_storage_error_init(target, rc);
    target->db_result = result; target->failed_schema_version = schema;
    if (db && PDb_GetErrorInfo(db, &error) == PDB_OK) {
        target->db_category = error.category; target->sqlite_code = error.sqlite_code;
        target->sqlite_extended_code = error.sqlite_extended_code;
        target->cleanup_result = error.cleanup_result;
        memcpy(target->message, error.message, sizeof(target->message));
        if (error.category == PDB_ERROR_CATEGORY_CANCELLED) target->result = rc = PRUNTIME_CANCELLED;
    }
    return rc;
}
static int runtime_storage_db_error(PRuntimeStorageHandle storage, int result, unsigned long schema)
{ return runtime_db_error(storage->db, &storage->error, result, schema); }
static int runtime_storage_cancel(PRuntimeStorageHandle storage)
{
    if (storage->options.cancelled && storage->options.cancelled(storage->options.user_data)) {
        runtime_storage_error_init(&storage->error, PRUNTIME_CANCELLED); return PRUNTIME_CANCELLED;
    }
    return PRUNTIME_OK;
}
static void runtime_storage_dispose(PRuntimeStorageHandle storage)
{
    if (storage->db) PDb_Close(storage->db);
    if (storage->location.token) storage->provider.close_location(storage->provider.user_data, storage->location.token);
    free(storage);
}
/* A short finite bucket selects a location only. The framed full identity is
 * persisted and compared under a DB write lock; collision MUST fail closed.
 * Deliberately separate from the launch-lease name/hash and installed namespace. */
static void runtime_storage_identity(PRuntimeDirectoryHandle directory, char* identity, char* key)
{
    static const char digits[] = "0123456789abcdef";
    unsigned long a;
    unsigned long b;
    unsigned long value;
    unsigned int i;
    int part;
    const unsigned char* p;
    strcpy(identity, "Positron.storage.v1:development:"); strcat(identity, directory->manifest->info.id);
    strcat(identity, ":"); strcat(identity, directory->root.canonical);
    a = 5381UL; b = 2166136261UL;
    for (p = (const unsigned char*)identity; *p; p++) { a = a * 33UL ^ *p; b = (b ^ *p) * 16777619UL; }
    strcpy(key, "v1/dev/");
    for (part = 0; part < 2; part++) {
        value = part ? b : a;
        for (i = 0; i < 8; i++) key[7 + part * 8 + i] = digits[(value >> (28 - 4 * i)) & 15];
    }
    strcpy(key + 23, "/app.db");
}
static int runtime_storage_ready(PRuntimeStorageHandle storage)
{
    if (!storage) return PRUNTIME_ARGUMENT;
    if (storage->owner != GetCurrentThreadId() || storage->active || runtime_storage_parent_busy(storage)) return PRUNTIME_STATE;
    return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_DevelopmentStorageOpen(PRuntimeDirectoryHandle directory,
        const PRuntimeStorageOptions* options, const PRuntimeStorageProvider* provider,
        PRuntimeStorageHandle* output, PRuntimeStorageError* error)
{
    PRuntimeStorageHandle storage;
    PRuntimeMigrationPlanHandle plan;
    PRuntimeMigrationPlanOptions plan_options;
    PDbRestrictedOptions db_options;
    PDbMigrationReport report;
    PDbErrorInfo open_error;
    char identity[1200];
    unsigned int i;
    int version;
    int db_rc;
    int rc;
    rc = runtime_directory_ready(directory);
    if (rc != PRUNTIME_OK) return rc;
    if (!options || !provider || !output || options->size != sizeof(*options) || options->version != PRUNTIME_VERSION ||
            options->vm_budget < 100 || options->vm_budget > 1000000 || options->vm_budget % 100 ||
            provider->size != sizeof(*provider) || provider->version != PRUNTIME_VERSION ||
            !provider->open_location || !provider->close_location ||
            (error && (error->size != sizeof(*error) || error->version != PRUNTIME_VERSION))) return PRUNTIME_ARGUMENT;
    if (directory->storage_count) return PRUNTIME_STATE;
    if (error) runtime_storage_error_init(error, PRUNTIME_OK);
    if (options->granted_permissions & ~PRUNTIME_PERMISSION_SQL) rc = PRUNTIME_UNSUPPORTED;
    else if (!(options->granted_permissions & directory->manifest->info.requested_permissions & PRUNTIME_PERMISSION_SQL)) rc = PRUNTIME_DENIED;
    if (rc != PRUNTIME_OK) { if (error) error->result = rc; return rc; }
    storage = (PRuntimeStorageHandle)calloc(1, sizeof(*storage)); if (!storage) { if (error) error->result = PRUNTIME_MEMORY; return PRUNTIME_MEMORY; }
    storage->directory = directory; storage->provider = *provider; storage->options = *options;
    storage->owner = GetCurrentThreadId(); storage->active = directory->active = 1;
    storage->info.size = sizeof(storage->info); storage->info.version = PRUNTIME_VERSION;
    strcpy(storage->info.id, directory->manifest->info.id); runtime_storage_error_init(&storage->error, PRUNTIME_OK);
    runtime_storage_identity(directory, identity, storage->info.key); plan = NULL;
    rc = runtime_storage_cancel(storage);
    if (rc == PRUNTIME_OK) rc = runtime_provider_result(storage->provider.open_location(storage->provider.user_data,
            storage->info.key, &storage->location), 0);
    if (rc == PRUNTIME_OK && (!storage->location.token || !storage->location.path_bytes ||
            storage->location.path_bytes > PRUNTIME_ROOT_BYTES || storage->location.path[0] != '\\' ||
            !runtime_text(storage->location.path, storage->location.path_bytes) || storage->location.path[storage->location.path_bytes])) rc = PRUNTIME_IO;
    if (rc == PRUNTIME_OK) rc = runtime_storage_cancel(storage);
    if (rc == PRUNTIME_OK) {
        open_error.size = sizeof(open_error); open_error.version = PDB_ERROR_INFO_VERSION;
        db_rc = PDb_OpenUtf8Ex(storage->location.path, PDB_OPEN_LOCAL_FULL_SQL, &storage->db, &open_error);
        if (db_rc != PDB_OK) {
            rc = runtime_storage_db_error(storage, db_rc, 0);
            storage->error.db_category = open_error.category; storage->error.sqlite_code = open_error.sqlite_code;
            storage->error.sqlite_extended_code = open_error.sqlite_extended_code; storage->error.cleanup_result = open_error.cleanup_result;
            memcpy(storage->error.message, open_error.message, sizeof(storage->error.message));
        }
    }
    if (rc == PRUNTIME_OK) {
        db_rc = PDb_BindLocalIdentity(storage->db, identity, (int)strlen(identity));
        if (db_rc != PDB_OK) rc = runtime_storage_db_error(storage, db_rc, 0);
    }
    if (rc == PRUNTIME_OK) {
        db_rc = PDb_GetMigrationVersion(storage->db, &version);
        if (db_rc != PDB_OK) rc = runtime_storage_db_error(storage, db_rc, 0);
    }
    if (rc == PRUNTIME_OK) {
        plan_options.size = sizeof(plan_options); plan_options.version = PRUNTIME_VERSION;
        plan_options.granted_permissions = storage->options.granted_permissions;
        plan_options.current_schema_version = version;
        directory->active = 0;
        rc = PRuntime_DirectoryPrepareMigrations(directory, &plan_options, &plan);
        directory->active = 1;
    }
    memset(&db_options, 0, sizeof(db_options)); db_options.size = sizeof(db_options); db_options.version = PDB_RESTRICTED_VERSION;
    db_options.vm_budget = storage->options.vm_budget; db_options.cancelled = storage->options.cancelled; db_options.user_data = storage->options.user_data;
    memset(&report, 0, sizeof(report)); report.size = sizeof(report); report.version = PDB_MIGRATION_VERSION;
    for (i = 0; rc == PRUNTIME_OK && i < plan->info.file_count; i++) {
        db_rc = PDb_ApplyMigrationRestricted(storage->db, (int)plan->sources[i].schema_version - 1,
                (int)plan->sources[i].schema_version, plan->sources[i].sql, plan->sources[i].sql_bytes, &db_options, &report);
        if (db_rc != PDB_OK) rc = runtime_storage_db_error(storage, db_rc, plan->sources[i].schema_version);
    }
    if (rc == PRUNTIME_OK) rc = runtime_storage_cancel(storage);
    if (rc == PRUNTIME_OK) {
        storage->info.schema_version = plan->info.target_version;
        storage->info.state = PRUNTIME_STORAGE_READY;
        directory->storage_count++; storage->active = 0; *output = storage;
    } else if (storage->error.result == PRUNTIME_OK) runtime_storage_error_init(&storage->error, rc);
    PRuntime_MigrationPlanClose(&plan);
    if (error) *error = storage->error;
    if (rc != PRUNTIME_OK) runtime_storage_dispose(storage);
    directory->active = 0; return rc;
}
PRUNTIME_API int PRuntime_StorageGetInfo(PRuntimeStorageHandle storage, PRuntimeStorageInfo* info)
{
    int rc;
    rc = runtime_storage_ready(storage); if (rc != PRUNTIME_OK) return rc;
    if (!info || info->size != sizeof(*info) || info->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    *info = storage->info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_StorageGetError(PRuntimeStorageHandle storage, PRuntimeStorageError* error)
{
    int rc;
    rc = runtime_storage_ready(storage); if (rc != PRUNTIME_OK) return rc;
    if (!error || error->size != sizeof(*error) || error->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    *error = storage->error; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_StorageClose(PRuntimeStorageHandle* storage)
{
    PRuntimeStorageHandle current;
    int rc;
    if (!storage) return PRUNTIME_ARGUMENT;
    if (!*storage) return PRUNTIME_OK;
    current = *storage;
    rc = runtime_storage_ready(current); if (rc != PRUNTIME_OK) return rc;
    if (current->queue_count) return PRUNTIME_STATE;
    runtime_storage_parent_active(current, 1); current->active = 1;
    if (current->db) { PDb_Close(current->db); current->db = NULL; }
    if (current->location.token) {
        current->provider.close_location(current->provider.user_data, current->location.token);
        current->location.token = NULL;
    }
    runtime_storage_parent_detach(current);
    free(current); *storage = NULL; return PRUNTIME_OK;
}

typedef struct RuntimeSqlWork {
    PRuntimeStorageHandle storage;
    PDbRestrictedStatement statements[8];
    PDbRestrictedValue params[8][16];
    char rows[4097];
    char scalar[4097];
    unsigned int used;
    unsigned int row_count;
    int result;
} RuntimeSqlWork;
static int runtime_sql_i64(const char* text, __int64* output)
{
    const unsigned char* p;
    unsigned __int64 value;
    unsigned __int64 limit;
    unsigned int digits;
    int negative;
    if (!text || !*text) return 0;
    p = (const unsigned char*)text; negative = *p == '-'; if (negative) p++;
    if (!*p || (*p == '0' && (p[1] || negative))) return 0;
    value = 0; digits = 0;
    limit = ((unsigned __int64)1 << 63) - (negative ? 0 : 1);
    while (*p) {
        if (*p < '0' || *p > '9' || ++digits > 19 || value > (limit - (*p - '0')) / 10) return 0;
        value = value * 10 + *p++ - '0';
    }
    *output = negative ? -(__int64)(value - 1) - 1 : (__int64)value; return 1;
}
static int runtime_sql_statement(HANDLE item, const char* method, PDbRestrictedStatement* statement,
        PDbRestrictedValue* params)
{
    static const char* const statement_keys[] = { "kind", "sql", "params" };
    static const char* const null_keys[] = { "type" };
    static const char* const value_keys[] = { "type", "value" };
    const char* kind;
    const char* type;
    HANDLE array;
    HANDLE value;
    int i;
    if (!method && !runtime_keys(item, statement_keys, 3)) return PRUNTIME_INVALID;
    kind = method ? method : PJson_GetString(item, "kind");
    if (!kind) return PRUNTIME_INVALID;
    if (!strcmp(kind, "query")) statement->kind = PDB_RESTRICTED_QUERY;
    else if (!strcmp(kind, "execute")) statement->kind = PDB_RESTRICTED_EXECUTE;
    else return PRUNTIME_UNSUPPORTED;
    statement->sql = PJson_GetString(item, "sql"); if (!statement->sql || !*statement->sql) return PRUNTIME_INVALID;
    statement->sql_bytes = (int)strlen(statement->sql); if (statement->sql_bytes > 2048) return PRUNTIME_LIMIT;
    array = PJson_GetObject(item, "params"); if (PJson_GetType(array) != PJSON_TYPE_ARRAY) return PRUNTIME_INVALID;
    statement->param_count = PJson_GetArraySize(array); if (statement->param_count > 16) return PRUNTIME_LIMIT;
    statement->params = params;
    for (i = 0; i < statement->param_count; i++) {
        value = PJson_GetArrayItem(array, i); type = PJson_GetString(value, "type"); if (!type) return PRUNTIME_INVALID;
        if (!strcmp(type, "null")) {
            if (!runtime_keys(value, null_keys, 1)) return PRUNTIME_INVALID;
            params[i].type = PDB_VALUE_NULL;
        } else {
            if (!runtime_keys(value, value_keys, 2)) return PRUNTIME_INVALID;
            if (!strcmp(type, "int64")) {
                params[i].type = PDB_VALUE_INTEGER;
                if (!runtime_sql_i64(PJson_GetString(value, "value"), &params[i].integer)) return PRUNTIME_INVALID;
            } else if (!strcmp(type, "real")) {
                params[i].type = PDB_VALUE_REAL;
                if (!PJson_GetDouble(value, "value", &params[i].real)) return PRUNTIME_INVALID;
            } else if (!strcmp(type, "text")) {
                params[i].type = PDB_VALUE_TEXT; params[i].text = PJson_GetString(value, "value");
                if (!params[i].text) return PRUNTIME_INVALID;
                params[i].text_bytes = (int)strlen(params[i].text);
            } else return PRUNTIME_UNSUPPORTED;
        }
    }
    return PRUNTIME_OK;
}
static void runtime_sql_put(RuntimeSqlWork* work, const char* text, unsigned int bytes)
{
    if (work->result != PRUNTIME_OK) return;
    if (bytes > PRUNTIME_SERVICE_JSON_BYTES - 128 - work->used) { work->result = PRUNTIME_LIMIT; return; }
    memcpy(work->rows + work->used, text, bytes); work->used += bytes; work->rows[work->used] = 0;
}
static void runtime_sql_literal(RuntimeSqlWork* work, const char* text)
{ runtime_sql_put(work, text, (unsigned int)strlen(text)); }
static void runtime_sql_encode(RuntimeSqlWork* work, PJsonScalar* scalar)
{
    unsigned int bytes;
    int rc;
    if (work->result != PRUNTIME_OK) return;
    rc = PJson_EncodeScalar(scalar, work->scalar, sizeof(work->scalar), &bytes);
    if (rc == PJSON_ENCODE_OK) runtime_sql_put(work, work->scalar, bytes);
    else if (rc == PJSON_ENCODE_LIMIT) work->result = PRUNTIME_LIMIT;
    else if (rc == PJSON_ENCODE_MEMORY) work->result = PRUNTIME_MEMORY;
    else work->result = PRUNTIME_UNSUPPORTED;
}
static int runtime_sql_row(void* user, int index, const PDbRestrictedValue* values, int columns)
{
    RuntimeSqlWork* work;
    PJsonScalar scalar;
    char decimal[32];
    int i;
    work = (RuntimeSqlWork*)user;
    if (work->row_count++) runtime_sql_literal(work, ",");
    sprintf(decimal, "%d", index); runtime_sql_literal(work, "{\"statement\":"); runtime_sql_literal(work, decimal);
    runtime_sql_literal(work, ",\"values\":[");
    memset(&scalar, 0, sizeof(scalar)); scalar.size = sizeof(scalar); scalar.version = PJSON_ENCODE_VERSION;
    for (i = 0; i < columns; i++) {
        if (i) runtime_sql_literal(work, ",");
        if (values[i].type == PDB_VALUE_NULL) runtime_sql_literal(work, "{\"type\":\"null\"}");
        else {
            if (values[i].type == PDB_VALUE_INTEGER) {
                runtime_sql_literal(work, "{\"type\":\"int64\",\"value\":");
                sprintf(decimal, "%I64d", values[i].integer);
                scalar.type = PJSON_TYPE_STRING; scalar.text = decimal; scalar.text_bytes = (unsigned int)strlen(decimal);
            } else if (values[i].type == PDB_VALUE_REAL) {
                runtime_sql_literal(work, "{\"type\":\"real\",\"value\":"); scalar.type = PJSON_TYPE_NUMBER; scalar.number = values[i].real;
            } else {
                runtime_sql_literal(work, "{\"type\":\"text\",\"value\":"); scalar.type = PJSON_TYPE_STRING;
                scalar.text = values[i].text; scalar.text_bytes = values[i].text_bytes;
            }
            runtime_sql_encode(work, &scalar); runtime_sql_literal(work, "}");
        }
    }
    runtime_sql_literal(work, "]}"); return work->result == PRUNTIME_OK ? PDB_OK : work->result == PRUNTIME_LIMIT ? PDB_LIMIT : PDB_STATE;
}
static int runtime_sql_cancel(void* user)
{
    RuntimeSqlWork* work = (RuntimeSqlWork*)user;
    PRuntimeStorageHandle storage = work->storage;
    if (runtime_storage_parent_cancelled(storage)) {
        storage->instance_cancelled = 1; return 1;
    }
    if (storage->operation_cancel && InterlockedCompareExchange((LONG*)storage->operation_cancel, 0, 0)) return 1;
    if (storage->options.cancelled && storage->options.cancelled(storage->options.user_data)) {
        storage->instance_cancelled = 1; return 1;
    }
    return 0;
}
PRUNTIME_API int PRuntime_StorageExecuteJson(PRuntimeStorageHandle storage,
        const char* request, unsigned int bytes, char* output, unsigned int capacity, unsigned int* written)
{
    static const char* const single_keys[] = { "version", "method", "sql", "params" };
    static const char* const batch_keys[] = { "version", "method", "statements" };
    RuntimeSqlWork* work;
    PJsonParseLimits limits;
    PDbRestrictedOptions options;
    PDbRestrictedReport report;
    PDbConnectionState state;
    HANDLE root;
    HANDLE statements;
    const char* method;
    char header[128];
    unsigned long version;
    unsigned int length;
    int count;
    int i;
    int native;
    int rc;
    rc = runtime_storage_ready(storage); if (rc != PRUNTIME_OK) return rc;
    if (storage->queue_count && !storage->queue_processing) return PRUNTIME_STATE;
    if (storage->info.state != PRUNTIME_STORAGE_READY) return PRUNTIME_STATE;
    if (!request || !bytes || !output || !written) { runtime_storage_error_init(&storage->error, PRUNTIME_ARGUMENT); return PRUNTIME_ARGUMENT; }
    if (bytes > PRUNTIME_SERVICE_JSON_BYTES || capacity < PRUNTIME_SERVICE_JSON_BYTES + 1) {
        runtime_storage_error_init(&storage->error, PRUNTIME_LIMIT); return PRUNTIME_LIMIT;
    }
    work = (RuntimeSqlWork*)calloc(1, sizeof(*work));
    if (!work) { runtime_storage_error_init(&storage->error, PRUNTIME_MEMORY); return PRUNTIME_MEMORY; }
    work->storage = storage; root = NULL;
    limits.size = sizeof(limits); limits.version = PJSON_PARSE_VERSION; limits.max_bytes = 4096; limits.max_depth = 8; limits.max_tokens = 768;
    rc = PJson_ParseEx(request, bytes, &limits, &root);
    rc = rc == PJSON_PARSE_OK ? PRUNTIME_OK : rc == PJSON_PARSE_LIMIT ? PRUNTIME_LIMIT : PRUNTIME_INVALID;
    count = 0;
    if (rc == PRUNTIME_OK && !runtime_integer(root, "version", &version)) rc = PRUNTIME_INVALID;
    if (rc == PRUNTIME_OK && version != 1) rc = PRUNTIME_UNSUPPORTED;
    method = rc == PRUNTIME_OK ? PJson_GetString(root, "method") : NULL;
    if (rc == PRUNTIME_OK && !method) rc = PRUNTIME_INVALID;
    if (rc == PRUNTIME_OK && !strcmp(method, "transaction")) {
        statements = PJson_GetObject(root, "statements");
        if (!runtime_keys(root, batch_keys, 3) || PJson_GetType(statements) != PJSON_TYPE_ARRAY) rc = PRUNTIME_INVALID;
        else {
            count = PJson_GetArraySize(statements);
            if (!count) rc = PRUNTIME_INVALID;
            else if (count > 8) rc = PRUNTIME_LIMIT;
            for (i = 0; rc == PRUNTIME_OK && i < count; i++) rc = runtime_sql_statement(PJson_GetArrayItem(statements, i), NULL, &work->statements[i], work->params[i]);
        }
    } else if (rc == PRUNTIME_OK) {
        count = 1;
        if (!runtime_keys(root, single_keys, 4)) rc = PRUNTIME_INVALID;
        else rc = runtime_sql_statement(root, method, work->statements, work->params[0]);
    }
    if (rc == PRUNTIME_OK) {
        storage->active = 1; runtime_storage_parent_active(storage, 1);
        storage->instance_cancelled = 0;
        rc = runtime_storage_refresh(storage);
        memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = PDB_RESTRICTED_VERSION;
        options.vm_budget = storage->options.vm_budget; options.cancelled = runtime_sql_cancel; options.row = runtime_sql_row; options.user_data = work;
        memset(&report, 0, sizeof(report)); report.size = sizeof(report); report.version = PDB_RESTRICTED_VERSION;
        native = rc == PRUNTIME_OK ? PDb_ExecuteRestricted(storage->db, work->statements, count, &options, &report) : PDB_STATE;
        if (rc != PRUNTIME_OK) { /* Registration failure occurs before any SQL. */ }
        else if (native != PDB_OK) {
            rc = runtime_storage_db_error(storage, native, 0);
            if (work->result != PRUNTIME_OK) storage->error.result = rc = work->result;
            state.size = sizeof(state); state.version = PDB_CONNECTION_STATE_VERSION;
            if (PDb_GetConnectionState(storage->db, &state) != PDB_OK || state.transaction_active) runtime_storage_retire(storage, rc);
        } else {
            /* Counts <=8/16 and signed int64 changes: this fixed report fits
             * within the 128-byte allowance reserved before DB COMMIT. */
            sprintf(header, "{\"version\":1,\"statementCount\":%d,\"rowCount\":%d,\"changes\":{\"type\":\"int64\",\"value\":\"%I64d\"},\"rows\":[",
                report.statement_count, report.row_count, report.changes);
            length = (unsigned int)strlen(header);
            memcpy(output, header, length); memcpy(output + length, work->rows, work->used); length += work->used;
            memcpy(output + length, "]}", 3); *written = length + 2;
        }
        if (storage->revision && rc == PRUNTIME_CANCELLED && storage->instance_cancelled) runtime_storage_retire(storage, rc);
        runtime_storage_parent_active(storage, 0); storage->active = 0;
    } else runtime_storage_error_init(&storage->error, rc);
    PJson_Free(root); free(work); return rc;
}

typedef struct RuntimeServiceJob {
    int state; /* 0 free, 1 waiting, 2 running, 3 result */
    volatile LONG cancelled;
    unsigned int request_bytes;
    char request[4097];
    PRuntimeServiceResult result;
} RuntimeServiceJob;
struct PRuntimeServiceQueue {
    PRuntimeStorageHandle storage;
    PRuntimeServiceQueueOptions options;
    CRITICAL_SECTION lock;
    RuntimeServiceJob jobs[8];
    unsigned long bridge_id;
    unsigned long last_request_id;
    DWORD owner;
    int revoked;
    int processing;
};
static int runtime_queue_thread(PRuntimeServiceQueueHandle queue, int delivery)
{
    if (!queue) return PRUNTIME_ARGUMENT;
    return GetCurrentThreadId() == (delivery ? queue->options.delivery_thread_id : queue->owner) ? PRUNTIME_OK : PRUNTIME_STATE;
}
static int runtime_queue_either_thread(PRuntimeServiceQueueHandle queue)
{
    if (!queue) return PRUNTIME_ARGUMENT;
    return GetCurrentThreadId() == queue->owner || GetCurrentThreadId() == queue->options.delivery_thread_id ? PRUNTIME_OK : PRUNTIME_STATE;
}
PRUNTIME_API int PRuntime_ServiceQueueCreate(PRuntimeStorageHandle storage,
        const PRuntimeServiceQueueOptions* options, PRuntimeServiceQueueHandle* output)
{
    PRuntimeServiceQueueHandle queue;
    unsigned int i;
    int rc;
    rc = runtime_storage_ready(storage); if (rc != PRUNTIME_OK) return rc;
    if (!options || !output || options->size != sizeof(*options) || options->version != PRUNTIME_VERSION ||
            !options->tab_id || !options->page_generation || !options->delivery_thread_id) return PRUNTIME_ARGUMENT;
    if (storage->info.state != PRUNTIME_STORAGE_READY) return PRUNTIME_STATE;
    for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; i++) if (storage->queues[i] &&
            storage->queues[i]->options.tab_id == options->tab_id &&
            storage->queues[i]->options.page_generation == options->page_generation) return PRUNTIME_STATE;
    if (storage->queue_count >= PRUNTIME_SERVICE_MAX_QUEUES) return PRUNTIME_LIMIT;
    if (storage->revision) {
        storage->active = 1; runtime_storage_parent_active(storage, 1);
        rc = runtime_storage_refresh(storage);
        if (rc == PRUNTIME_OK) {
            rc = runtime_storage_cancel(storage);
            if (rc != PRUNTIME_OK) runtime_storage_retire(storage, rc);
        }
        runtime_storage_parent_active(storage, 0); storage->active = 0;
        if (rc != PRUNTIME_OK) return rc;
    }
    queue = (PRuntimeServiceQueueHandle)calloc(1, sizeof(*queue)); if (!queue) return PRUNTIME_MEMORY;
    queue->storage = storage; queue->options = *options; queue->owner = GetCurrentThreadId();
    InitializeCriticalSection(&queue->lock); storage->queue_count++;
    for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; i++) if (!storage->queues[i]) { storage->queues[i] = queue; break; }
    *output = queue; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_ServiceQueueSubmit(PRuntimeServiceQueueHandle queue, const PRuntimeServiceRequest* request)
{
    RuntimeServiceJob* job;
    unsigned int i;
    int rc;
    rc = runtime_queue_thread(queue, 1); if (rc != PRUNTIME_OK) return rc;
    if (!request || request->size != sizeof(*request) || request->version != PRUNTIME_VERSION ||
            !request->bridge_id || !request->request_id || !request->method || !request->json || !request->json_bytes) return PRUNTIME_ARGUMENT;
    if (request->json_bytes > PRUNTIME_SERVICE_JSON_BYTES) return PRUNTIME_LIMIT;
    if (strcmp(request->method, "storage.sql")) return PRUNTIME_DENIED;
    if (request->tab_id != queue->options.tab_id || request->page_generation != queue->options.page_generation) return PRUNTIME_STATE;
    if (memchr(request->json, 0, request->json_bytes)) return PRUNTIME_INVALID;
    EnterCriticalSection(&queue->lock); job = NULL;
    if (queue->revoked || (queue->bridge_id && queue->bridge_id != request->bridge_id) || request->request_id <= queue->last_request_id) rc = PRUNTIME_STATE;
    else {
        for (i = 0; i < 8; i++) if (!queue->jobs[i].state) { job = &queue->jobs[i]; break; }
        if (!job) rc = PRUNTIME_LIMIT;
        else {
            memset(job, 0, sizeof(*job)); job->state = 1;
            job->request_bytes = request->json_bytes; memcpy(job->request, request->json, request->json_bytes);
            job->result.size = sizeof(job->result); job->result.version = PRUNTIME_VERSION;
            job->result.tab_id = request->tab_id; job->result.page_generation = request->page_generation;
            job->result.bridge_id = request->bridge_id; job->result.request_id = request->request_id;
            queue->bridge_id = request->bridge_id; queue->last_request_id = request->request_id;
        }
    }
    LeaveCriticalSection(&queue->lock); return rc;
}
PRUNTIME_API int PRuntime_ServiceQueueProcessOne(PRuntimeServiceQueueHandle queue, unsigned int* processed)
{
    RuntimeServiceJob* job;
    PRuntimeStorageError error;
    unsigned int i;
    int rc;
    rc = runtime_queue_thread(queue, 0); if (rc != PRUNTIME_OK) return rc;
    if (!processed) return PRUNTIME_ARGUMENT;
    if (runtime_storage_ready(queue->storage) != PRUNTIME_OK) return PRUNTIME_STATE;
    EnterCriticalSection(&queue->lock); job = NULL;
    if (queue->revoked || queue->processing) rc = PRUNTIME_STATE;
    else {
        for (i = 0; i < 8; i++) if (queue->jobs[i].state == 1 && (!job || queue->jobs[i].result.request_id < job->result.request_id)) job = &queue->jobs[i];
        if (!job) rc = PRUNTIME_NOT_FOUND;
        else { queue->processing = 1; job->state = 2; }
    }
    LeaveCriticalSection(&queue->lock); if (rc != PRUNTIME_OK) return rc;
    queue->storage->operation_cancel = &job->cancelled; queue->storage->queue_processing = 1;
    job->result.result = PRuntime_StorageExecuteJson(queue->storage, job->request, job->request_bytes,
        job->result.json, sizeof(job->result.json), &job->result.json_bytes);
    queue->storage->queue_processing = 0; queue->storage->operation_cancel = NULL;
    if (job->result.result != PRUNTIME_OK) {
        error.size = sizeof(error); error.version = PRUNTIME_VERSION;
        if (PRuntime_StorageGetError(queue->storage, &error) != PRUNTIME_OK) memset(&error, 0, sizeof(error));
        sprintf(job->result.json, "{\"version\":1,\"error\":{\"code\":%d,\"dbResult\":%d,\"category\":%d}}",
            job->result.result, error.db_result, error.db_category);
        job->result.json_bytes = (unsigned int)strlen(job->result.json);
    }
    EnterCriticalSection(&queue->lock);
    job->state = queue->revoked || InterlockedCompareExchange((LONG*)&job->cancelled, 0, 0) ? 0 : 3;
    queue->processing = 0; LeaveCriticalSection(&queue->lock); *processed = 1; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_ServiceQueueTakeResult(PRuntimeServiceQueueHandle queue, PRuntimeServiceResult* result)
{
    RuntimeServiceJob* job;
    unsigned int i;
    int rc;
    rc = runtime_queue_thread(queue, 1); if (rc != PRUNTIME_OK) return rc;
    if (!result || result->size != sizeof(*result) || result->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    EnterCriticalSection(&queue->lock); job = NULL;
    if (queue->revoked) rc = PRUNTIME_STATE;
    else {
        for (i = 0; i < 8; i++) if (queue->jobs[i].state == 3 && (!job || queue->jobs[i].result.request_id < job->result.request_id)) job = &queue->jobs[i];
        if (!job) rc = PRUNTIME_NOT_FOUND;
        else { *result = job->result; job->state = 0; }
    }
    LeaveCriticalSection(&queue->lock); return rc;
}
PRUNTIME_API int PRuntime_ServiceQueueCancel(PRuntimeServiceQueueHandle queue, unsigned long bridge, unsigned long request)
{
    unsigned int i;
    int rc;
    rc = runtime_queue_thread(queue, 1); if (rc != PRUNTIME_OK) return rc;
    if (!bridge || !request) return PRUNTIME_ARGUMENT;
    rc = PRUNTIME_NOT_FOUND; EnterCriticalSection(&queue->lock);
    for (i = 0; i < 8; i++) if (queue->jobs[i].state && queue->jobs[i].result.bridge_id == bridge && queue->jobs[i].result.request_id == request) {
        if (queue->jobs[i].state == 2) InterlockedExchange((LONG*)&queue->jobs[i].cancelled, 1);
        else queue->jobs[i].state = 0;
        rc = PRUNTIME_OK; break;
    }
    LeaveCriticalSection(&queue->lock); return rc;
}
PRUNTIME_API int PRuntime_ServiceQueueRevoke(PRuntimeServiceQueueHandle queue)
{
    unsigned int i;
    int rc;
    rc = runtime_queue_either_thread(queue); if (rc != PRUNTIME_OK) return rc;
    EnterCriticalSection(&queue->lock); queue->revoked = 1;
    for (i = 0; i < 8; i++) {
        if (queue->jobs[i].state == 2) InterlockedExchange((LONG*)&queue->jobs[i].cancelled, 1);
        else queue->jobs[i].state = 0;
    }
    LeaveCriticalSection(&queue->lock); return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_ServiceQueueGetInfo(PRuntimeServiceQueueHandle queue, PRuntimeServiceQueueInfo* info)
{
    PRuntimeServiceQueueInfo next;
    unsigned int i;
    int rc;
    rc = runtime_queue_either_thread(queue); if (rc != PRUNTIME_OK) return rc;
    if (!info || info->size != sizeof(*info) || info->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    memset(&next, 0, sizeof(next)); next.size = sizeof(next); next.version = PRUNTIME_VERSION;
    EnterCriticalSection(&queue->lock); next.revoked = queue->revoked;
    for (i = 0; i < 8; i++) {
        if (queue->jobs[i].state == 1) next.waiting++;
        else if (queue->jobs[i].state == 2) next.running++;
        else if (queue->jobs[i].state == 3) next.completed++;
    }
    LeaveCriticalSection(&queue->lock); *info = next; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_ServiceQueueClose(PRuntimeServiceQueueHandle* queue)
{
    unsigned int i;
    int rc;
    if (!queue) return PRUNTIME_ARGUMENT;
    if (!*queue) return PRUNTIME_OK;
    rc = runtime_queue_thread(*queue, 0); if (rc != PRUNTIME_OK) return rc;
    if (runtime_storage_ready((*queue)->storage) != PRUNTIME_OK) return PRUNTIME_STATE;
    EnterCriticalSection(&(*queue)->lock);
    if (!(*queue)->revoked || (*queue)->processing) { LeaveCriticalSection(&(*queue)->lock); return PRUNTIME_STATE; }
    LeaveCriticalSection(&(*queue)->lock); DeleteCriticalSection(&(*queue)->lock);
    for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; i++) if ((*queue)->storage->queues[i] == *queue) { (*queue)->storage->queues[i] = NULL; break; }
    (*queue)->storage->queue_count--; free(*queue); *queue = NULL; return PRUNTIME_OK;
}

/* Installed revision verification is implemented below; no filesystem/SQL policy here. */
#define RREV_MAGIC 0x52525631UL
struct PRuntimeRevision {
    DWORD magic, owner;
    int active;
    unsigned int intent_children, storage_children;
    unsigned int state, installed, phase, index, offset, frame_index;
    unsigned int count, total, verified_bytes, verified_files;
    unsigned int expected_generation;
    int error;
    char directory[1024], installation_id[33];
    PRuntimeRevisionProvider provider;
    PRuntimeInstalledProvider registry;
    PRuntimeRevisionOptions options;
    PRuntimeDirectoryRoot root;
    PRuntimeInstalledRecord record;
    void* registration_lease;
    void* digest;
    PRuntimeManifestHandle manifest;
    unsigned char content_id[32];
    PRuntimeRevisionFile files[PRUNTIME_REVISION_MAX_FILES];
    unsigned char scratch[PRUNTIME_REVISION_STEP_BYTES];
    char manifest_bytes[PRUNTIME_MANIFEST_BYTES];
};
/* Reserve a further 64 KiB for strict manifest/JSON parsing and call stack.
 * Native provider/CSP buffers are outside Runtime's allocation ownership. */
typedef char runtime_revision_budget_check[(sizeof(struct PRuntimeRevision) +
        65536U <= 512U * 1024U) ? 1 : -1];
typedef char runtime_installed_storage_budget_check[(sizeof(struct PRuntimeRevision) +
        sizeof(struct PRuntimeStorage) + sizeof(RuntimeSqlWork) +
        PRUNTIME_SERVICE_MAX_QUEUES * sizeof(struct PRuntimeServiceQueue) + 65536U <= 1280U * 1024U) ? 1 : -1];
static int rv_ready(PRuntimeRevisionHandle r)
{
    if (!r || r->magic != RREV_MAGIC) return PRUNTIME_ARGUMENT;
    return r->active || r->owner != GetCurrentThreadId() ? PRUNTIME_STATE : PRUNTIME_OK;
}
static unsigned int rv_length(const char* text, unsigned int capacity)
{
    unsigned int n = 0;
    while (n < capacity && text[n]) ++n;
    return n;
}
static int rv_id(const char* id, unsigned int bytes)
{
    unsigned int i, nonzero = 0;
    if (!id || bytes != 32) return 0;
    for (i = 0; i < bytes; ++i) {
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return 0;
        if (id[i] != '0') nonzero = 1;
    }
    return (int)nonzero;
}
static int rv_provider_rc(int rc)
{
    if (rc == PRUNTIME_OK || rc == PRUNTIME_IO || rc == PRUNTIME_LIMIT ||
            rc == PRUNTIME_MEMORY || rc == PRUNTIME_UNSUPPORTED || rc == PRUNTIME_BUSY) return rc;
    return PRUNTIME_IO;
}
static void rv_hash_close(PRuntimeRevisionHandle r)
{
    if (r->digest) r->provider.hash_close(r->provider.user_data, r->digest);
    r->digest = NULL;
}
static int rv_fail(PRuntimeRevisionHandle r, int rc)
{
    rv_hash_close(r);
    r->error = rc;
    r->state = rc == PRUNTIME_CANCELLED ? PRUNTIME_REVISION_CANCELLED : PRUNTIME_REVISION_FAILED;
    return rc;
}
static int rv_snapshot_valid(const PRuntimeApprovalSnapshot* s)
{
    char canonical[64];
    unsigned int n;
    if (s->size != sizeof(*s) || s->version != PRUNTIME_VERSION) return PRUNTIME_INVALID;
    n = rv_length(s->id, sizeof(s->id));
    if (PRuntime_NormalizeId(s->id, n, canonical, sizeof(canonical)) != PRUNTIME_OK ||
            strcmp(canonical, s->id) || rv_length(s->installation_id, 33) != 32 ||
            !rv_id(s->installation_id, 32) || !s->app_version || s->app_version > 2147483647U)
        return PRUNTIME_INVALID;
    if (s->revision != 1 || s->runtime_api_version != 1 || s->digest_algorithm != PRUNTIME_DIGEST_SHA256 ||
            s->approval_policy != 1 ||
            ((s->requested_permissions | s->approved_permissions) & ~PRUNTIME_PERMISSION_SQL)) return PRUNTIME_UNSUPPORTED;
    if (s->approved_permissions != s->requested_permissions) return PRUNTIME_DENIED;
    return PRUNTIME_OK;
}
static int rv_record_valid(PRuntimeRevisionHandle r, const PRuntimeInstalledRecord* record)
{
    unsigned int n;
    int rc;
    if (record->size != sizeof(*record) || record->version != PRUNTIME_VERSION) return PRUNTIME_INVALID;
    if (record->active != 1) return PRUNTIME_DENIED;
    if (!record->generation || record->generation != r->expected_generation) return PRUNTIME_INVALID;
    rc = rv_snapshot_valid(&record->approval);
    if (rc != PRUNTIME_OK) return rc;
    if (strcmp(record->approval.installation_id, r->installation_id)) return PRUNTIME_INVALID;
    n = rv_length(record->directory, sizeof(record->directory));
    if (!n || n > PRUNTIME_ROOT_BYTES || !runtime_text(record->directory, n)) return PRUNTIME_INVALID;
    return PRUNTIME_OK;
}
static int rv_same_approval(const PRuntimeApprovalSnapshot* a, const PRuntimeApprovalSnapshot* b)
{
    return !strcmp(a->id, b->id) && !strcmp(a->installation_id, b->installation_id) &&
            a->revision == b->revision && a->app_version == b->app_version &&
            a->runtime_api_version == b->runtime_api_version && a->digest_algorithm == b->digest_algorithm &&
            a->approval_policy == b->approval_policy &&
            a->requested_permissions == b->requested_permissions && a->approved_permissions == b->approved_permissions &&
            !memcmp(a->content_id, b->content_id, 32);
}
static int rv_refresh(PRuntimeRevisionHandle r)
{
    PRuntimeInstalledRecord current;
    int rc;
    memset(&current, 0, sizeof(current)); current.size = sizeof(current); current.version = PRUNTIME_VERSION;
    rc = r->registry.refresh(r->registry.user_data, r->registration_lease, &current);
    if (rc == PRUNTIME_NOT_FOUND || rc == PRUNTIME_DENIED) return PRUNTIME_DENIED;
    rc = rv_provider_rc(rc);
    if (rc != PRUNTIME_OK) return rc;
    rc = rv_record_valid(r, &current);
    if (rc != PRUNTIME_OK) return rc;
    if (strcmp(current.directory, r->record.directory) ||
            !rv_same_approval(&current.approval, &r->record.approval)) return PRUNTIME_INVALID;
    return PRUNTIME_OK;
}
static int runtime_storage_parent_busy(PRuntimeStorageHandle storage)
{
    return storage->revision ? storage->revision->active : storage->directory->active;
}
static void runtime_storage_parent_active(PRuntimeStorageHandle storage, int active)
{
    if (storage->revision) storage->revision->active = active;
    else storage->directory->active = active;
}
static void runtime_storage_parent_detach(PRuntimeStorageHandle storage)
{
    if (storage->revision) { --storage->revision->storage_children; storage->revision->active = 0; }
    else { --storage->directory->storage_count; storage->directory->active = 0; }
}
static int runtime_storage_parent_cancelled(PRuntimeStorageHandle storage)
{
    PRuntimeRevisionHandle r = storage->revision;
    return r && r->options.cancelled && r->options.cancelled(r->options.user_data);
}
static void runtime_storage_retire(PRuntimeStorageHandle storage, int result)
{
    unsigned int i;
    if (storage->revision) for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; ++i)
        if (storage->queues[i]) PRuntime_ServiceQueueRevoke(storage->queues[i]);
    storage->info.state = PRUNTIME_STORAGE_FAILED;
    if (storage->revision) rv_fail(storage->revision, result);
}
static int runtime_storage_refresh(PRuntimeStorageHandle storage)
{
    PRuntimeRevisionHandle r = storage->revision;
    int rc;
    if (!r) return PRUNTIME_OK;
    rc = r->state == PRUNTIME_REVISION_COMPLETE ? PRUNTIME_OK : PRUNTIME_STATE;
    if (rc == PRUNTIME_OK) rc = runtime_storage_parent_cancelled(storage) ? PRUNTIME_CANCELLED : rv_refresh(r);
    if (rc != PRUNTIME_OK) {
        runtime_storage_error_init(&storage->error, rc); runtime_storage_retire(storage, rc);
    }
    return rc;
}
PRUNTIME_API int PRuntime_InstalledStorageOpen(PRuntimeRevisionHandle r,
        const PRuntimeStorageOptions* options, const PRuntimeInstalledStorageProvider* provider,
        PRuntimeStorageHandle* output, PRuntimeStorageError* error)
{
    PRuntimeStorageHandle storage;
    PDbExistingLocalOptions db_options;
    PDbErrorInfo db_error;
    char identity[160];
    int rc = rv_ready(r), native;
    if (rc != PRUNTIME_OK) return rc;
    if (!options || !provider || !output || options->size != sizeof(*options) || options->version != 1 ||
            options->vm_budget < 100 || options->vm_budget > 1000000 || options->vm_budget % 100 ||
            provider->size != sizeof(*provider) || provider->version != 1 ||
            !provider->open_existing_location || !provider->close_location ||
            (error && (error->size != sizeof(*error) || error->version != 1))) return PRUNTIME_ARGUMENT;
    if (!r->installed) rc = PRUNTIME_DENIED;
    else if (r->state != PRUNTIME_REVISION_COMPLETE || r->storage_children || r->intent_children) rc = PRUNTIME_STATE;
    else if (options->granted_permissions & ~PRUNTIME_PERMISSION_SQL) rc = PRUNTIME_UNSUPPORTED;
    else if (!(options->granted_permissions & r->record.approval.approved_permissions & PRUNTIME_PERMISSION_SQL)) rc = PRUNTIME_DENIED;
    if (rc != PRUNTIME_OK) { if (error) runtime_storage_error_init(error, rc); return rc; }
    storage = (PRuntimeStorageHandle)calloc(1, sizeof(*storage));
    if (!storage) { if (error) runtime_storage_error_init(error, PRUNTIME_MEMORY); return PRUNTIME_MEMORY; }
    storage->revision = r; storage->owner = GetCurrentThreadId(); storage->options = *options;
    storage->provider.user_data = provider->user_data;
    storage->provider.open_location = provider->open_existing_location;
    storage->provider.close_location = provider->close_location;
    storage->info.size = sizeof(storage->info); storage->info.version = 1;
    strcpy(storage->info.id, r->manifest->info.id);
    sprintf(storage->info.key, "v1/installed/%s/app.db", r->installation_id);
    sprintf(identity, "Positron.storage.v1:installed:%s:%s", r->manifest->info.id, r->installation_id);
    runtime_storage_error_init(&storage->error, PRUNTIME_OK);
    r->active = storage->active = 1;
    rc = runtime_storage_refresh(storage);
    if (rc == PRUNTIME_OK) rc = runtime_storage_cancel(storage);
    if (rc == PRUNTIME_OK) rc = rv_provider_rc(provider->open_existing_location(provider->user_data,
            storage->info.key, &storage->location));
    if (rc == PRUNTIME_OK && (!storage->location.token || !storage->location.path_bytes ||
            storage->location.path_bytes > PRUNTIME_ROOT_BYTES || storage->location.path[0] != '\\' ||
            !runtime_text(storage->location.path, storage->location.path_bytes) ||
            storage->location.path[storage->location.path_bytes])) rc = PRUNTIME_IO;
    if (rc == PRUNTIME_OK) rc = runtime_storage_cancel(storage);
    if (rc == PRUNTIME_OK) rc = runtime_storage_refresh(storage);
    if (rc == PRUNTIME_OK) {
        db_options.size = sizeof(db_options); db_options.version = 1;
        db_options.identity = identity; db_options.identity_bytes = (int)strlen(identity);
        db_options.expected_schema = (int)r->manifest->info.migration_count;
        db_error.size = sizeof(db_error); db_error.version = PDB_ERROR_INFO_VERSION;
        native = PDb_OpenExistingLocalUtf8(storage->location.path, storage->location.path_bytes,
                &db_options, &storage->db, &db_error);
        if (native != PDB_OK) {
            rc = runtime_storage_db_error(storage, native, db_options.expected_schema);
            storage->error.db_category = db_error.category; storage->error.sqlite_code = db_error.sqlite_code;
            storage->error.sqlite_extended_code = db_error.sqlite_extended_code;
            storage->error.cleanup_result = db_error.cleanup_result;
            memcpy(storage->error.message, db_error.message, sizeof(storage->error.message));
        }
    }
    if (rc == PRUNTIME_OK) rc = runtime_storage_refresh(storage);
    if (rc == PRUNTIME_OK) rc = runtime_storage_cancel(storage);
    if (rc == PRUNTIME_OK) {
        storage->info.schema_version = r->manifest->info.migration_count;
        storage->info.state = PRUNTIME_STORAGE_READY; ++r->storage_children;
        storage->active = r->active = 0; *output = storage;
    } else {
        if (storage->error.result == PRUNTIME_OK) runtime_storage_error_init(&storage->error, rc);
        if (error) *error = storage->error;
        runtime_storage_dispose(storage); r->active = 0;
        return rc;
    }
    if (error) *error = storage->error;
    return PRUNTIME_OK;
}
static int rv_provider_valid(const PRuntimeRevisionProvider* p)
{
    if (!p || p->size != sizeof(*p) || p->version != PRUNTIME_VERSION ||
            !p->open_root || !p->close_root || !p->enumerate_file || !p->read_at ||
            !p->hash_begin || !p->hash_update || !p->hash_finish || !p->hash_close) return PRUNTIME_ARGUMENT;
    return p->digest_algorithm == PRUNTIME_DIGEST_SHA256 ? PRUNTIME_OK : PRUNTIME_UNSUPPORTED;
}
static int rv_begin(const PRuntimeRevisionProvider* provider,
        const PRuntimeRevisionOptions* options, PRuntimeRevisionHandle* output)
{
    PRuntimeRevisionHandle r;
    int rc = rv_provider_valid(provider);
    if (rc != PRUNTIME_OK) return rc;
    if (!output || (options && (options->size != sizeof(*options) ||
            options->version != PRUNTIME_VERSION))) return PRUNTIME_ARGUMENT;
    r = (PRuntimeRevisionHandle)calloc(1, sizeof(*r));
    if (!r) return PRUNTIME_MEMORY;
    r->magic = RREV_MAGIC; r->owner = GetCurrentThreadId(); r->provider = *provider;
    if (options) r->options = *options;
    r->state = PRUNTIME_REVISION_INITIALIZING;
    *output = r; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_PrepareRevision(const char* directory, unsigned int bytes,
        const PRuntimeRevisionProvider* provider, const PRuntimeRevisionOptions* options,
        PRuntimeRevisionHandle* output)
{
    PRuntimeRevisionHandle r;
    int rc;
    if (!directory || !bytes || !output) return PRUNTIME_ARGUMENT;
    if (bytes > PRUNTIME_ROOT_BYTES) return PRUNTIME_LIMIT;
    if (!runtime_text(directory, bytes)) return PRUNTIME_INVALID;
    rc = rv_begin(provider, options, &r);
    if (rc != PRUNTIME_OK) return rc;
    memcpy(r->directory, directory, bytes); *output = r; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_OpenInstalled(const char* id, unsigned int bytes, unsigned int generation,
        const PRuntimeInstalledProvider* registry, const PRuntimeRevisionProvider* provider,
        const PRuntimeRevisionOptions* options, PRuntimeRevisionHandle* output)
{
    PRuntimeRevisionHandle r;
    int rc;
    if (!output || !rv_id(id, bytes) || !generation || generation > 2147483647U ||
            !registry || registry->size != sizeof(*registry) || registry->version != PRUNTIME_VERSION ||
            !registry->open || !registry->refresh || !registry->close) return PRUNTIME_ARGUMENT;
    rc = rv_begin(provider, options, &r);
    if (rc != PRUNTIME_OK) return rc;
    memcpy(r->installation_id, id, 32); r->expected_generation = generation;
    r->registry = *registry; r->installed = 1;
    *output = r; return PRUNTIME_OK;
}
static int rv_conflict(const char* a, const char* b)
{
    WCHAR wa[256], wb[256];
    int an = MultiByteToWideChar(CP_UTF8, 0, a, -1, wa, 256) - 1;
    int bn = MultiByteToWideChar(CP_UTF8, 0, b, -1, wb, 256) - 1;
    int n = an < bn ? an : bn;
    int comparison;
    if (an < 1 || bn < 1) return 1;
    comparison = CompareStringW(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, wa, n, wb, n);
    if (!comparison) return 1;
    return comparison == CSTR_EQUAL && (an == bn || (n == an && wb[n] == '/') || (n == bn && wa[n] == '/'));
}
static int __cdecl rv_file_compare(const void* a, const void* b)
{ return strcmp(((const PRuntimeRevisionFile*)a)->path, ((const PRuntimeRevisionFile*)b)->path); }
static void rv_u32(unsigned char* p, unsigned int n)
{ p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8); p[2] = (unsigned char)(n >> 16); p[3] = (unsigned char)(n >> 24); }
static int rv_hash_begin(PRuntimeRevisionHandle r)
{ return rv_provider_rc(r->provider.hash_begin(r->provider.user_data, &r->digest)); }
static int rv_hash_update(PRuntimeRevisionHandle r, const void* bytes, unsigned int n)
{ return rv_provider_rc(r->provider.hash_update(r->provider.user_data, r->digest, bytes, n)); }
static int rv_has_file(PRuntimeRevisionHandle r, const char* path)
{
    unsigned int i;
    for (i = 0; i < r->count; ++i) if (!strcmp(r->files[i].path, path)) return 1;
    return 0;
}
static int rv_step(PRuntimeRevisionHandle r)
{
    static const unsigned char domain[] = "Positron.ResourceSet.v1";
    PRuntimeRevisionFile file;
    PRuntimeRevisionFile* current;
    unsigned int i, n, got;
    unsigned char frame[4 + 255 + 4 + 32];
    int rc;
    if (r->options.cancelled && r->options.cancelled(r->options.user_data)) return PRUNTIME_CANCELLED;
    if (!r->phase) {
        if (r->installed) {
            r->record.size = sizeof(r->record); r->record.version = PRUNTIME_VERSION;
            rc = r->registry.open(r->registry.user_data, r->installation_id,
                    &r->record, &r->registration_lease);
            if (rc == PRUNTIME_NOT_FOUND || rc == PRUNTIME_DENIED) return PRUNTIME_DENIED;
            rc = rv_provider_rc(rc);
            if (rc != PRUNTIME_OK) return rc;
            if (!r->registration_lease) return PRUNTIME_IO;
            rc = rv_record_valid(r, &r->record);
            if (rc != PRUNTIME_OK) return rc;
            strcpy(r->directory, r->record.directory);
        }
        rc = rv_provider_rc(r->provider.open_root(r->provider.user_data,
                r->directory, (unsigned int)strlen(r->directory), &r->root));
        if (rc != PRUNTIME_OK) return rc;
        if (!r->root.root || !r->root.canonical_bytes || r->root.canonical_bytes > PRUNTIME_ROOT_BYTES ||
                !runtime_text(r->root.canonical, r->root.canonical_bytes) ||
                r->root.canonical[r->root.canonical_bytes]) return PRUNTIME_IO;
        if (r->installed && strcmp(r->root.canonical, r->record.directory)) return PRUNTIME_INVALID;
        r->phase = 1; r->state = PRUNTIME_REVISION_PENDING; return PRUNTIME_OK;
    }
    if (r->phase == 1) {
        for (i = 0; i < 16; ++i) {
            memset(&file, 0, sizeof(file)); file.size = sizeof(file); file.version = PRUNTIME_VERSION;
            rc = r->provider.enumerate_file(r->provider.user_data, r->root.root, r->count, &file);
            if (rc == PRUNTIME_NOT_FOUND) {
                if (!rv_has_file(r, "manifest.json")) return PRUNTIME_INVALID;
                qsort(r->files, r->count, sizeof(file), rv_file_compare);
                r->phase = 2; return PRUNTIME_OK;
            }
            rc = rv_provider_rc(rc); if (rc != PRUNTIME_OK) return rc;
            if (r->count == PRUNTIME_REVISION_MAX_FILES) return PRUNTIME_LIMIT;
            if (file.size != sizeof(file) || file.version != PRUNTIME_VERSION) return PRUNTIME_IO;
            n = rv_length(file.path, sizeof(file.path));
            rc = PRuntime_ValidateResourcePath(file.path, n); if (rc != PRUNTIME_OK) return rc;
            if (file.file_bytes > PRUNTIME_RESOURCE_BYTES ||
                    file.file_bytes > PRUNTIME_REVISION_TOTAL_BYTES - r->total ||
                    (!strcmp(file.path, "manifest.json") && file.file_bytes > PRUNTIME_MANIFEST_BYTES)) return PRUNTIME_LIMIT;
            if (!strcmp(file.path, "manifest.json") && !file.file_bytes) return PRUNTIME_INVALID;
            {
                unsigned int j;
                for (j = 0; j < r->count; ++j) if (rv_conflict(file.path, r->files[j].path)) return PRUNTIME_INVALID;
            }
            memset(file.sha256, 0, 32); r->files[r->count++] = file; r->total += file.file_bytes;
        }
        return PRUNTIME_OK;
    }
    if (r->phase == 2) {
        current = &r->files[r->index];
        if (!r->digest) {
            rc = rv_hash_begin(r); if (rc != PRUNTIME_OK) return rc;
            if (!r->digest) return PRUNTIME_IO;
        }
        n = current->file_bytes - r->offset;
        if (n > PRUNTIME_REVISION_STEP_BYTES) n = PRUNTIME_REVISION_STEP_BYTES;
        got = 0;
        rc = rv_provider_rc(r->provider.read_at(r->provider.user_data, r->root.root,
                current->path, r->offset, r->scratch, n ? n : 1, &got));
        if (rc != PRUNTIME_OK) return rc;
        if (got != n) return n ? PRUNTIME_IO : PRUNTIME_INVALID;
        if (n) {
            rc = rv_hash_update(r, r->scratch, n); if (rc != PRUNTIME_OK) return rc;
            if (!strcmp(current->path, "manifest.json")) memcpy(r->manifest_bytes + r->offset, r->scratch, n);
            r->offset += n; r->verified_bytes += n; return PRUNTIME_OK;
        }
        rc = rv_provider_rc(r->provider.hash_finish(r->provider.user_data, r->digest, current->sha256));
        rv_hash_close(r); if (rc != PRUNTIME_OK) return rc;
        if (!strcmp(current->path, "manifest.json")) {
            rc = PRuntime_ManifestCreate(r->manifest_bytes, current->file_bytes, &r->manifest);
            if (rc != PRUNTIME_OK) return rc;
        }
        ++r->verified_files; ++r->index; r->offset = 0;
        if (r->index == r->count) r->phase = 3;
        return PRUNTIME_OK;
    }
    if (r->phase == 3) {
        if (!r->manifest || !rv_has_file(r, r->manifest->info.entry)) return PRUNTIME_INVALID;
        for (i = 0; i < r->manifest->info.migration_count; ++i)
            if (!rv_has_file(r, r->manifest->migrations[i].file)) return PRUNTIME_INVALID;
        rc = rv_hash_begin(r); if (rc != PRUNTIME_OK) return rc;
        if (!r->digest) return PRUNTIME_IO;
        rc = rv_hash_update(r, domain, sizeof(domain)); if (rc != PRUNTIME_OK) return rc;
        rv_u32(frame, r->count); rc = rv_hash_update(r, frame, 4); if (rc != PRUNTIME_OK) return rc;
        r->phase = 4; return PRUNTIME_OK;
    }
    for (i = 0; i < 16 && r->frame_index < r->count; ++i) {
        current = &r->files[r->frame_index++]; n = (unsigned int)strlen(current->path);
        rv_u32(frame, n); memcpy(frame + 4, current->path, n);
        rv_u32(frame + 4 + n, current->file_bytes); memcpy(frame + 8 + n, current->sha256, 32);
        rc = rv_hash_update(r, frame, n + 40); if (rc != PRUNTIME_OK) return rc;
    }
    if (r->frame_index != r->count) return PRUNTIME_OK;
    rc = rv_provider_rc(r->provider.hash_finish(r->provider.user_data, r->digest, r->content_id));
    rv_hash_close(r); if (rc != PRUNTIME_OK) return rc;
    if (r->installed) {
        rc = rv_refresh(r); if (rc != PRUNTIME_OK) return rc;
        if (strcmp(r->manifest->info.id, r->record.approval.id) ||
                r->manifest->info.app_version != r->record.approval.app_version ||
                r->manifest->info.requested_permissions != r->record.approval.requested_permissions ||
                memcmp(r->content_id, r->record.approval.content_id, 32)) return PRUNTIME_INVALID;
    }
    r->state = PRUNTIME_REVISION_COMPLETE; return PRUNTIME_REVISION_DONE;
}
PRUNTIME_API int PRuntime_RevisionStep(PRuntimeRevisionHandle r)
{
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (r->state != PRUNTIME_REVISION_INITIALIZING && r->state != PRUNTIME_REVISION_PENDING) return PRUNTIME_STATE;
    r->active = 1; rc = rv_step(r);
    if (rc < 0) rv_fail(r, rc);
    r->active = 0; return rc;
}
PRUNTIME_API int PRuntime_RevisionGetInfo(PRuntimeRevisionHandle r, PRuntimeRevisionInfo* output)
{
    PRuntimeRevisionInfo info;
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (!output || output->size != sizeof(*output) || output->version != PRUNTIME_VERSION) return PRUNTIME_ARGUMENT;
    memset(&info, 0, sizeof(info)); info.size = sizeof(info); info.version = PRUNTIME_VERSION;
    info.state = r->state; info.error = r->error; info.installed = r->installed;
    info.file_count = r->count; info.total_bytes = r->total;
    info.verified_bytes = r->verified_bytes; info.verified_files = r->verified_files;
    if (r->state == PRUNTIME_REVISION_COMPLETE) {
        strcpy(info.canonical_root, r->root.canonical); strcpy(info.id, r->manifest->info.id);
        strcpy(info.entry, r->manifest->info.entry); info.app_version = r->manifest->info.app_version;
        info.requested_permissions = r->manifest->info.requested_permissions;
        memcpy(info.content_id, r->content_id, 32);
        if (r->installed) {
            strcpy(info.installation_id, r->record.approval.installation_id);
            info.revision = r->record.approval.revision; info.registry_generation = r->record.generation;
            info.approved_permissions = r->record.approval.approved_permissions;
        }
    }
    *output = info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_RevisionGetFile(PRuntimeRevisionHandle r, unsigned int index, PRuntimeRevisionFile* file)
{
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (r->state != PRUNTIME_REVISION_COMPLETE) return PRUNTIME_STATE;
    if (!file || file->size != sizeof(*file) || file->version != PRUNTIME_VERSION || index >= r->count) return PRUNTIME_ARGUMENT;
    *file = r->files[index]; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_RevisionBuildApproval(PRuntimeRevisionHandle r,
        const PRuntimeApprovalOptions* options, PRuntimeApprovalSnapshot* output)
{
    PRuntimeApprovalSnapshot snapshot;
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (r->state != PRUNTIME_REVISION_COMPLETE || r->installed || r->intent_children) return PRUNTIME_STATE;
    if (!options || !output || options->size != sizeof(*options) || options->version != PRUNTIME_VERSION ||
            output->size != sizeof(*output) || output->version != PRUNTIME_VERSION ||
            rv_length(options->installation_id, 33) != 32 || !rv_id(options->installation_id, 32)) return PRUNTIME_ARGUMENT;
    if (options->revision != 1) return PRUNTIME_UNSUPPORTED;
    if (options->confirmed != 1) return PRUNTIME_DENIED;
    memset(&snapshot, 0, sizeof(snapshot)); snapshot.size = sizeof(snapshot); snapshot.version = PRUNTIME_VERSION;
    strcpy(snapshot.id, r->manifest->info.id); strcpy(snapshot.installation_id, options->installation_id);
    snapshot.revision = 1; snapshot.app_version = r->manifest->info.app_version;
    snapshot.runtime_api_version = 1; snapshot.digest_algorithm = PRUNTIME_DIGEST_SHA256;
    memcpy(snapshot.content_id, r->content_id, 32); snapshot.approval_policy = 1;
    snapshot.requested_permissions = snapshot.approved_permissions = r->manifest->info.requested_permissions;
    *output = snapshot; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstalledReadFile(PRuntimeRevisionHandle r, unsigned int index,
        unsigned int offset, void* buffer, unsigned int capacity, unsigned int* written)
{
    unsigned int n, got = 0;
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (!r->installed) return PRUNTIME_DENIED;
    if (r->state != PRUNTIME_REVISION_COMPLETE) return PRUNTIME_STATE;
    if (!buffer || !written || !capacity || capacity > PRUNTIME_REVISION_STEP_BYTES || index >= r->count ||
            offset > r->files[index].file_bytes) return PRUNTIME_ARGUMENT;
    r->active = 1;
    rc = r->options.cancelled && r->options.cancelled(r->options.user_data) ? PRUNTIME_CANCELLED : rv_refresh(r);
    n = r->files[index].file_bytes - offset; if (n > capacity) n = capacity;
    if (rc == PRUNTIME_OK && n) {
        rc = rv_provider_rc(r->provider.read_at(r->provider.user_data, r->root.root,
                r->files[index].path, offset, r->scratch, n, &got));
        if (rc == PRUNTIME_OK && got != n) rc = PRUNTIME_IO;
    }
    if (rc == PRUNTIME_OK) { if (n) memcpy(buffer, r->scratch, n); *written = n; }
    else rv_fail(r, rc);
    r->active = 0; return rc;
}
PRUNTIME_API int PRuntime_RevisionCancel(PRuntimeRevisionHandle r)
{
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (r->intent_children || r->storage_children) return PRUNTIME_STATE;
    if (r->state == PRUNTIME_REVISION_CANCELLED) return PRUNTIME_OK;
    if (r->state == PRUNTIME_REVISION_FAILED) return PRUNTIME_STATE;
    r->active = 1; rv_fail(r, PRUNTIME_CANCELLED); r->active = 0; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_RevisionClose(PRuntimeRevisionHandle* slot)
{
    PRuntimeRevisionHandle r;
    int rc;
    if (!slot) return PRUNTIME_ARGUMENT;
    r = *slot; if (!r) return PRUNTIME_OK;
    rc = rv_ready(r); if (rc != PRUNTIME_OK) return rc;
    if (r->intent_children || r->storage_children) return PRUNTIME_STATE;
    r->active = 1; rv_hash_close(r); PRuntime_ManifestClose(&r->manifest);
    if (r->root.root) r->provider.close_root(r->provider.user_data, r->root.root);
    if (r->registration_lease) r->registry.close(r->registry.user_data, r->registration_lease);
    r->magic = 0; free(r); *slot = NULL; return PRUNTIME_OK;
}

static int rv_registration_valid(PRuntimeRevisionHandle r, const PRuntimeRegistrationRecord* record)
{
    unsigned int n;
    int rc;
    if (record->size != sizeof(*record) || record->version != PRUNTIME_VERSION ||
            record->state < PRUNTIME_REG_PREPARING || record->state > PRUNTIME_REG_REVOKED ||
            !record->generation || record->generation > 2147483647U) return PRUNTIME_INVALID;
    rc = rv_snapshot_valid(&record->approval); if (rc != PRUNTIME_OK) return rc;
    n = rv_length(record->directory, sizeof(record->directory));
    if (!n || n > PRUNTIME_ROOT_BYTES || !runtime_text(record->directory, n) ||
            strcmp(record->directory, r->root.canonical) ||
            record->target_schema != r->manifest->info.migration_count ||
            strcmp(record->approval.id, r->manifest->info.id) ||
            record->approval.app_version != r->manifest->info.app_version ||
            record->approval.requested_permissions != r->manifest->info.requested_permissions ||
            memcmp(record->approval.content_id, r->content_id, 32)) return PRUNTIME_INVALID;
    return PRUNTIME_OK;
}
static int rv_same_registration(const PRuntimeRegistrationRecord* a,
        const PRuntimeRegistrationRecord* b)
{
    return a->state == b->state && a->generation == b->generation &&
            a->target_schema == b->target_schema && !strcmp(a->directory, b->directory) &&
            rv_same_approval(&a->approval, &b->approval);
}
static int rv_registration_decide(const PRuntimeRegistrationTransition* t,
        const PRuntimeRegistrationRecord* current, int found, PRuntimeRegistrationDecision* decision)
{
    PRuntimeRegistrationRecord ready;
    if (found && rv_same_registration(current, &t->proposed) &&
            t->commit_outcome != PRUNTIME_COMMIT_NOT_COMMITTED) decision->match = PRUNTIME_REG_MATCH_PROPOSED;
    else if (((!found && !t->previous_exists) || (found && t->previous_exists &&
            rv_same_registration(current, &t->previous))) &&
            t->commit_outcome != PRUNTIME_COMMIT_CONFIRMED) decision->match = PRUNTIME_REG_MATCH_PREVIOUS;
    else if (found && t->previous_exists && t->previous.state == PRUNTIME_REG_PREPARING &&
            t->proposed.state == PRUNTIME_REG_CANCELLED && t->commit_outcome == PRUNTIME_COMMIT_UNKNOWN) {
        ready = t->proposed; ready.state = PRUNTIME_REG_READY;
        if (rv_same_registration(current, &ready)) decision->match = PRUNTIME_REG_READY_WON_CANCEL;
        else return PRUNTIME_INVALID;
    } else return !found ? PRUNTIME_DENIED : PRUNTIME_INVALID;
    if (found) { decision->observed_state = current->state; decision->observed_generation = current->generation; }
    return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_RevisionReconcileRegistration(PRuntimeRevisionHandle r,
        const PRuntimeRegistrationTransition* t, const PRuntimeRegistrationProvider* p,
        PRuntimeRegistrationDecision* output)
{
    PRuntimeRegistrationTransition transition;
    PRuntimeRegistrationProvider provider;
    PRuntimeRegistrationRecord current;
    PRuntimeRegistrationDecision decision;
    void* lease = NULL;
    int rc = rv_ready(r);
    int found;
    if (rc != PRUNTIME_OK) return rc;
    if (r->state != PRUNTIME_REVISION_COMPLETE || r->installed || r->intent_children) return PRUNTIME_STATE;
    if (!t || !p || !output || t->size != sizeof(*t) || t->version != PRUNTIME_VERSION ||
            p->size != sizeof(*p) || p->version != PRUNTIME_VERSION ||
            !p->acquire || !p->read || !p->close || output->size != sizeof(*output) ||
            output->version != PRUNTIME_VERSION || t->previous_exists > 1 || t->confirmed > 1 ||
            t->commit_outcome < PRUNTIME_COMMIT_CONFIRMED ||
            t->commit_outcome > PRUNTIME_COMMIT_UNKNOWN) return PRUNTIME_ARGUMENT;
    transition = *t; provider = *p; t = &transition; p = &provider;
    rc = rv_registration_valid(r, &t->proposed); if (rc != PRUNTIME_OK) return rc;
    if (!t->previous_exists) {
        if (t->proposed.state != PRUNTIME_REG_PREPARING || t->proposed.generation != 1) return PRUNTIME_INVALID;
        if (!t->confirmed) return PRUNTIME_DENIED;
    } else {
        rc = rv_registration_valid(r, &t->previous); if (rc != PRUNTIME_OK) return rc;
        if (t->previous.generation == 2147483647U) return PRUNTIME_LIMIT;
        if (t->proposed.generation != t->previous.generation + 1 ||
                !rv_same_approval(&t->proposed.approval, &t->previous.approval)) return PRUNTIME_INVALID;
        if (t->previous.state == PRUNTIME_REG_FAILED && t->proposed.state == PRUNTIME_REG_PREPARING) {
            if (!t->confirmed) return PRUNTIME_DENIED;
        } else if (t->previous.state != PRUNTIME_REG_PREPARING ||
                (t->proposed.state != PRUNTIME_REG_READY && t->proposed.state != PRUNTIME_REG_FAILED &&
                 t->proposed.state != PRUNTIME_REG_CANCELLED)) return PRUNTIME_DENIED;
    }
    memset(&decision, 0, sizeof(decision)); decision.size = sizeof(decision); decision.version = PRUNTIME_VERSION;
    memset(&current, 0, sizeof(current)); current.size = sizeof(current); current.version = PRUNTIME_VERSION;
    r->active = 1;
    rc = rv_provider_rc(p->acquire(p->user_data, t->proposed.approval.id,
            t->proposed.approval.installation_id, &lease));
    if (rc == PRUNTIME_OK && !lease) rc = PRUNTIME_IO;
    if (rc == PRUNTIME_OK) {
        rc = p->read(p->user_data, lease, &current); found = rc != PRUNTIME_NOT_FOUND;
        if (!found) rc = PRUNTIME_OK;
        else rc = rv_provider_rc(rc);
        if (rc == PRUNTIME_OK && found) rc = rv_registration_valid(r, &current);
        if (rc == PRUNTIME_OK) rc = rv_registration_decide(t, &current, found, &decision);
    }
    if (lease) p->close(p->user_data, lease);
    r->active = 0;
    if (rc == PRUNTIME_OK) *output = decision;
    return rc;
}

#define RINT_MAGIC 0x52494e31UL
struct PRuntimeInstallIntent {
    DWORD magic;
    PRuntimeRevisionHandle parent;
    PRuntimeInstallIntentProvider provider;
    PRuntimeInstallIntentOptions options;
    PRuntimeRegistrationTransition transition;
    PRuntimeInstallIntentInfo info;
    void* lease;
    unsigned int phase, cancelled;
    int active, cas_result;
    unsigned int data_children, commit_children;
};
typedef char runtime_intent_budget_check[(sizeof(struct PRuntimeInstallIntent) <= 8192U) ? 1 : -1];
static int ri_ready(PRuntimeInstallIntentHandle job)
{
    if (!job || job->magic != RINT_MAGIC) return PRUNTIME_ARGUMENT;
    if (job->active) return PRUNTIME_STATE;
    return rv_ready(job->parent);
}
static int ri_fail(PRuntimeInstallIntentHandle job, int rc, int unresolved)
{
    job->info.error = unresolved ? PRUNTIME_UNRESOLVED : rc;
    job->info.state = unresolved ? PRUNTIME_INTENT_UNRESOLVED : PRUNTIME_INTENT_FAILED;
    return job->info.error;
}
static void ri_observed(PRuntimeInstallIntentHandle job, const PRuntimeRegistrationRecord* record, int found)
{
    job->info.record_known = 1; job->info.record_exists = found ? 1 : 0;
    memset(&job->info.record, 0, sizeof(job->info.record));
    if (found) job->info.record = *record;
}
PRUNTIME_API int PRuntime_InstallIntentBegin(PRuntimeRevisionHandle r,
        const PRuntimeInstallIntentOptions* options, const PRuntimeInstallIntentProvider* provider,
        PRuntimeInstallIntentHandle* output)
{
    PRuntimeInstallIntentHandle job;
    PRuntimeApprovalSnapshot approval;
    int rc = rv_ready(r);
    if (rc != PRUNTIME_OK) return rc;
    if (r->state != PRUNTIME_REVISION_COMPLETE || r->installed || r->intent_children) return PRUNTIME_STATE;
    if (!options || !provider || !output || options->size != sizeof(*options) || options->version != 1 ||
            options->mode < PRUNTIME_INTENT_NEW || options->mode > PRUNTIME_INTENT_RETRY_FAILED ||
            provider->size != sizeof(*provider) || provider->version != 1 || !provider->acquire ||
            !provider->read || !provider->compare_exchange || !provider->close) return PRUNTIME_ARGUMENT;
    approval.size = sizeof(approval); approval.version = 1;
    rc = PRuntime_RevisionBuildApproval(r, &options->approval, &approval);
    if (rc != PRUNTIME_OK) return rc;
    job = (PRuntimeInstallIntentHandle)calloc(1, sizeof(*job)); if (!job) return PRUNTIME_MEMORY;
    job->magic = RINT_MAGIC; job->parent = r; job->provider = *provider; job->options = *options;
    job->info.size = sizeof(job->info); job->info.version = 1; job->info.state = PRUNTIME_INTENT_INITIALIZING;
    job->transition.size = sizeof(job->transition); job->transition.version = 1; job->transition.confirmed = 1;
    job->transition.proposed.size = sizeof(PRuntimeRegistrationRecord); job->transition.proposed.version = 1;
    job->transition.proposed.state = PRUNTIME_REG_PREPARING; job->transition.proposed.generation = 1;
    job->transition.proposed.target_schema = r->manifest->info.migration_count;
    job->transition.proposed.approval = approval;
    strcpy(job->transition.proposed.directory, r->root.canonical);
    ++r->intent_children; *output = job; return PRUNTIME_OK;
}
static int ri_cancel_transition(PRuntimeInstallIntentHandle job)
{
    if (job->info.record.generation == 2147483647U) return ri_fail(job, PRUNTIME_LIMIT, 0);
    job->transition.previous_exists = 1; job->transition.previous = job->info.record;
    job->transition.proposed = job->info.record;
    job->transition.proposed.state = PRUNTIME_REG_CANCELLED; ++job->transition.proposed.generation;
    job->phase = 4; job->info.state = PRUNTIME_INTENT_PENDING; return PRUNTIME_OK;
}
static int ri_step(PRuntimeInstallIntentHandle job)
{
    PRuntimeRegistrationRecord current;
    PRuntimeRegistrationDecision decision;
    unsigned int outcome;
    int found, rc;
    if (job->cancelled && job->phase < 3 && job->options.mode != PRUNTIME_INTENT_RESUME) {
        job->info.state = PRUNTIME_INTENT_CANCELLED; job->info.error = PRUNTIME_CANCELLED; return PRUNTIME_CANCELLED;
    }
    if (!job->phase) {
        rc = rv_provider_rc(job->provider.acquire(job->provider.user_data,
                job->transition.proposed.approval.id, job->transition.proposed.approval.installation_id, &job->lease));
        job->info.provider_result = rc;
        if (rc != PRUNTIME_OK || !job->lease) return ri_fail(job, rc != PRUNTIME_OK ? rc : PRUNTIME_IO, 0);
        job->phase = 1; job->info.state = PRUNTIME_INTENT_PENDING; return PRUNTIME_OK;
    }
    if (job->phase == 2 || job->phase == 4) {
        outcome = PRUNTIME_COMMIT_UNKNOWN;
        rc = rv_provider_rc(job->provider.compare_exchange(job->provider.user_data, job->lease,
                job->transition.previous_exists ? &job->transition.previous : NULL,
                &job->transition.proposed, &outcome));
        if (outcome < PRUNTIME_COMMIT_CONFIRMED || outcome > PRUNTIME_COMMIT_UNKNOWN) {
            outcome = PRUNTIME_COMMIT_UNKNOWN; rc = PRUNTIME_IO;
        }
        job->cas_result = rc; job->info.provider_result = rc;
        job->transition.commit_outcome = job->info.commit_outcome = outcome;
        job->info.record_known = job->info.record_exists = 0; memset(&job->info.record, 0, sizeof(job->info.record));
        ++job->phase; return PRUNTIME_OK;
    }
    memset(&current, 0, sizeof(current)); current.size = sizeof(current); current.version = 1;
    rc = job->provider.read(job->provider.user_data, job->lease, &current); found = rc != PRUNTIME_NOT_FOUND;
    if (!found) rc = PRUNTIME_OK; else rc = rv_provider_rc(rc);
    if (rc != PRUNTIME_OK) {
        job->info.provider_result = rc; return ri_fail(job, rc, job->phase != 1);
    }
    if (found) {
        rc = rv_registration_valid(job->parent, &current);
        if (rc == PRUNTIME_OK && !rv_same_approval(&current.approval, &job->transition.proposed.approval)) rc = PRUNTIME_INVALID;
        if (rc != PRUNTIME_OK) return ri_fail(job, rc, 0);
    }
    ri_observed(job, &current, found);
    if (job->phase == 1) {
        if (job->options.mode == PRUNTIME_INTENT_NEW) {
            if (found) return ri_fail(job, PRUNTIME_DENIED, 0);
        } else {
            if (!found) return ri_fail(job, PRUNTIME_DENIED, 0);
            if (job->options.mode == PRUNTIME_INTENT_RESUME) {
                if (current.state != PRUNTIME_REG_PREPARING) return ri_fail(job, PRUNTIME_DENIED, 0);
                if (job->cancelled) return ri_cancel_transition(job);
                job->info.state = PRUNTIME_INTENT_PREPARED; return PRUNTIME_INTENT_DONE;
            }
            if (current.state != PRUNTIME_REG_FAILED) return ri_fail(job, PRUNTIME_DENIED, 0);
            if (current.generation == 2147483647U) return ri_fail(job, PRUNTIME_LIMIT, 0);
            job->transition.previous_exists = 1; job->transition.previous = current;
            job->transition.proposed.generation = current.generation + 1;
        }
        job->phase = 2; return PRUNTIME_OK;
    }
    memset(&decision, 0, sizeof(decision));
    rc = rv_registration_decide(&job->transition, &current, found, &decision);
    if (rc != PRUNTIME_OK) return ri_fail(job, rc, 0);
    if (decision.match == PRUNTIME_REG_READY_WON_CANCEL) return ri_fail(job, PRUNTIME_STATE, 0);
    if (decision.match != PRUNTIME_REG_MATCH_PROPOSED)
        return ri_fail(job, job->cas_result != PRUNTIME_OK ? job->cas_result : PRUNTIME_IO, 0);
    if (job->phase == 5) {
        job->info.state = PRUNTIME_INTENT_CANCELLED; job->info.error = PRUNTIME_CANCELLED; return PRUNTIME_CANCELLED;
    }
    if (job->cancelled) return ri_cancel_transition(job);
    job->info.state = PRUNTIME_INTENT_PREPARED; return PRUNTIME_INTENT_DONE;
}
PRUNTIME_API int PRuntime_InstallIntentStep(PRuntimeInstallIntentHandle job)
{
    int rc = ri_ready(job);
    if (rc != PRUNTIME_OK) return rc;
    if (job->info.state != PRUNTIME_INTENT_INITIALIZING && job->info.state != PRUNTIME_INTENT_PENDING) return PRUNTIME_STATE;
    job->active = job->parent->active = 1; rc = ri_step(job);
    job->parent->active = job->active = 0; return rc;
}
PRUNTIME_API int PRuntime_InstallIntentGetInfo(PRuntimeInstallIntentHandle job, PRuntimeInstallIntentInfo* info)
{
    int rc = ri_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (!info || info->size != sizeof(*info) || info->version != 1) return PRUNTIME_ARGUMENT;
    *info = job->info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallIntentCancel(PRuntimeInstallIntentHandle job)
{
    int rc = ri_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (job->data_children || job->commit_children) return PRUNTIME_STATE;
    if (job->info.state == PRUNTIME_INTENT_CANCELLED) return PRUNTIME_OK;
    if (job->info.state == PRUNTIME_INTENT_FAILED || job->info.state == PRUNTIME_INTENT_UNRESOLVED ||
            job->info.state == PRUNTIME_INTENT_FINALIZED) return PRUNTIME_STATE;
    job->cancelled = 1;
    return job->info.state == PRUNTIME_INTENT_PREPARED ? ri_cancel_transition(job) : PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallIntentClose(PRuntimeInstallIntentHandle* slot)
{
    PRuntimeInstallIntentHandle job;
    int rc;
    if (!slot) return PRUNTIME_ARGUMENT;
    job = *slot; if (!job) return PRUNTIME_OK;
    rc = ri_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (job->data_children || job->commit_children) return PRUNTIME_STATE;
    job->active = job->parent->active = 1;
    if (job->lease) job->provider.close(job->provider.user_data, job->lease);
    --job->parent->intent_children; job->parent->active = 0; job->magic = 0;
    free(job); *slot = NULL; return PRUNTIME_OK;
}

#define RIDATA_MAGIC 0x52494431UL
struct PRuntimeInstallData {
    DWORD magic;
    PRuntimeInstallIntentHandle intent;
    PRuntimeStorageProvider provider;
    PRuntimeStorageLocation location;
    PRuntimeInstallDataOptions options;
    PRuntimeInstallDataInfo info;
    PRuntimeMigrationPlanHandle plan;
    PDbHandle db;
    unsigned int phase, source_index, apply_index, cancelled, commit_children;
    int active;
};
typedef char runtime_install_data_budget_check[(sizeof(struct PRuntimeRevision) +
    sizeof(struct PRuntimeInstallIntent) + sizeof(struct PRuntimeInstallData) +
    sizeof(struct PRuntimeMigrationPlan) + PRUNTIME_MIGRATION_TOTAL_BYTES + 131072U <= 1280U * 1024U) ? 1 : -1];
static int rd_ready(PRuntimeInstallDataHandle data)
{
    if (!data || data->magic != RIDATA_MAGIC) return PRUNTIME_ARGUMENT;
    return data->active ? PRUNTIME_STATE : ri_ready(data->intent);
}
static int rd_refresh(PRuntimeInstallDataHandle data)
{
    PRuntimeInstallIntentHandle intent = data->intent;
    PRuntimeRegistrationRecord current;
    int rc;
    memset(&current, 0, sizeof(current)); current.size = sizeof(current); current.version = 1;
    rc = intent->provider.read(intent->provider.user_data, intent->lease, &current);
    if (rc == PRUNTIME_NOT_FOUND) return PRUNTIME_DENIED;
    rc = rv_provider_rc(rc); if (rc != PRUNTIME_OK) return rc;
    rc = rv_registration_valid(intent->parent, &current);
    if (rc != PRUNTIME_OK) return rc;
    return rv_same_registration(&current, &intent->info.record) ? PRUNTIME_OK : PRUNTIME_INVALID;
}
PRUNTIME_API int PRuntime_InstallDataBegin(PRuntimeInstallIntentHandle intent,
        const PRuntimeInstallDataOptions* options, const PRuntimeStorageProvider* provider,
        PRuntimeInstallDataHandle* output)
{
    PRuntimeInstallDataHandle data;
    int rc = ri_ready(intent);
    if (rc != PRUNTIME_OK) return rc;
    if (intent->info.state != PRUNTIME_INTENT_PREPARED || intent->data_children || intent->commit_children) return PRUNTIME_STATE;
    if (!options || !provider || !output || options->size != sizeof(*options) || options->version != 1 ||
            options->vm_budget < 100 || options->vm_budget > 1000000 || options->vm_budget % 100 ||
            provider->size != sizeof(*provider) || provider->version != 1 ||
            !provider->open_location || !provider->close_location) return PRUNTIME_ARGUMENT;
    if (!(intent->info.record.approval.approved_permissions & PRUNTIME_PERMISSION_SQL)) return PRUNTIME_DENIED;
    if (intent->info.record.generation == 2147483647U) return PRUNTIME_LIMIT;
    data = (PRuntimeInstallDataHandle)calloc(1, sizeof(*data)); if (!data) return PRUNTIME_MEMORY;
    data->magic = RIDATA_MAGIC; data->intent = intent; data->provider = *provider; data->options = *options;
    data->info.size = sizeof(data->info); data->info.version = 1; data->info.state = PRUNTIME_INSTALL_DATA_INITIALIZING;
    data->info.target_schema = intent->info.record.target_schema;
    runtime_storage_error_init(&data->info.error, PRUNTIME_OK);
    strcpy(data->info.key, "v1/installed/"); strcat(data->info.key, intent->info.record.approval.installation_id);
    strcat(data->info.key, "/app.db");
    ++intent->data_children; *output = data; return PRUNTIME_OK;
}
static int rd_db(PRuntimeInstallDataHandle data, int rc, unsigned int schema)
{ return rc == PDB_OK ? PRUNTIME_OK : runtime_db_error(data->db, &data->info.error, rc, schema); }
static int rd_abort(PRuntimeInstallDataHandle data, int rc)
{
    PRuntimeInstallIntentHandle intent = data->intent;
    if (data->info.error.result == PRUNTIME_OK) runtime_storage_error_init(&data->info.error, rc);
    intent->transition.previous_exists = 1; intent->transition.previous = intent->info.record;
    intent->transition.proposed = intent->info.record;
    intent->transition.proposed.state = rc == PRUNTIME_CANCELLED ? PRUNTIME_REG_CANCELLED : PRUNTIME_REG_FAILED;
    ++intent->transition.proposed.generation;
    data->phase = 90; return PRUNTIME_OK;
}
static int rd_record_failure(PRuntimeInstallDataHandle data)
{
    PRuntimeInstallIntentHandle intent = data->intent;
    PRuntimeRegistrationRecord current;
    PRuntimeRegistrationDecision decision;
    unsigned int outcome = PRUNTIME_COMMIT_UNKNOWN;
    int found, rc;
    if (data->phase == 90) {
        rc = rv_provider_rc(intent->provider.compare_exchange(intent->provider.user_data, intent->lease,
                &intent->transition.previous, &intent->transition.proposed, &outcome));
        if (outcome < PRUNTIME_COMMIT_CONFIRMED || outcome > PRUNTIME_COMMIT_UNKNOWN) {
            outcome = PRUNTIME_COMMIT_UNKNOWN; rc = PRUNTIME_IO;
        }
        intent->transition.commit_outcome = intent->info.commit_outcome = data->info.registration_outcome = outcome;
        data->info.registration_result = intent->info.provider_result = rc;
        intent->info.record_known = intent->info.record_exists = 0;
        memset(&intent->info.record, 0, sizeof(intent->info.record));
        ++data->phase; return PRUNTIME_OK;
    }
    memset(&current, 0, sizeof(current)); current.size = sizeof(current); current.version = 1;
    rc = intent->provider.read(intent->provider.user_data, intent->lease, &current); found = rc != PRUNTIME_NOT_FOUND;
    if (!found) rc = PRUNTIME_OK; else rc = rv_provider_rc(rc);
    if (rc != PRUNTIME_OK) {
        data->info.registration_result = PRUNTIME_UNRESOLVED;
        intent->info.provider_result = rc; intent->info.error = PRUNTIME_UNRESOLVED;
        intent->info.state = PRUNTIME_INTENT_UNRESOLVED;
    } else {
        if (found) rc = rv_registration_valid(intent->parent, &current);
        if (rc == PRUNTIME_OK) {
            ri_observed(intent, &current, found); memset(&decision, 0, sizeof(decision));
            rc = rv_registration_decide(&intent->transition, &current, found, &decision);
            if (rc == PRUNTIME_OK && decision.match != PRUNTIME_REG_MATCH_PROPOSED)
                rc = decision.match == PRUNTIME_REG_READY_WON_CANCEL ? PRUNTIME_STATE :
                    (data->info.registration_result != PRUNTIME_OK ? data->info.registration_result : PRUNTIME_IO);
        }
        data->info.registration_result = rc;
        intent->info.state = data->info.error.result == PRUNTIME_CANCELLED && rc == PRUNTIME_OK ?
                PRUNTIME_INTENT_CANCELLED : PRUNTIME_INTENT_FAILED;
        intent->info.error = data->info.error.result;
    }
    data->info.state = data->info.error.result == PRUNTIME_CANCELLED ? PRUNTIME_INSTALL_DATA_CANCELLED : PRUNTIME_INSTALL_DATA_FAILED;
    return data->info.error.result;
}
static int rd_step(PRuntimeInstallDataHandle data)
{
    PRuntimeRevisionHandle r = data->intent->parent;
    PRuntimeMigrationSource* source;
    PRuntimeMigrationInfo* migration;
    PRuntimeRevisionFile* file = NULL;
    PDbRestrictedOptions db_options;
    PDbMigrationReport report;
    PDbErrorInfo open_error;
    char identity[160];
    unsigned int i, got;
    int version, rc = PRUNTIME_OK;
    if (data->phase >= 90) return rd_record_failure(data);
    if (data->cancelled || (data->options.cancelled && data->options.cancelled(data->options.user_data)))
        return rd_abort(data, PRUNTIME_CANCELLED);
    switch (data->phase) {
    case 0: rc = rd_refresh(data); break;
    case 1:
        rc = rv_provider_rc(data->provider.open_location(data->provider.user_data, data->info.key, &data->location));
        if (rc == PRUNTIME_OK && (!data->location.token || !data->location.path_bytes ||
                data->location.path_bytes > PRUNTIME_ROOT_BYTES || data->location.path[0] != '\\' ||
                !runtime_text(data->location.path, data->location.path_bytes) || data->location.path[data->location.path_bytes])) rc = PRUNTIME_IO;
        break;
    case 2:
        open_error.size = sizeof(open_error); open_error.version = PDB_ERROR_INFO_VERSION;
        rc = rd_db(data, PDb_OpenUtf8Ex(data->location.path, PDB_OPEN_LOCAL_FULL_SQL, &data->db, &open_error), 0);
        if (rc != PRUNTIME_OK) {
            data->info.error.db_category = open_error.category; data->info.error.sqlite_code = open_error.sqlite_code;
            data->info.error.sqlite_extended_code = open_error.sqlite_extended_code;
            data->info.error.cleanup_result = open_error.cleanup_result;
            memcpy(data->info.error.message, open_error.message, sizeof(open_error.message));
        }
        break;
    case 3:
        strcpy(identity, "Positron.storage.v1:installed:"); strcat(identity, data->intent->info.record.approval.id);
        strcat(identity, ":"); strcat(identity, data->intent->info.record.approval.installation_id);
        rc = rd_db(data, PDb_BindLocalIdentity(data->db, identity, (int)strlen(identity)), 0); break;
    case 4:
        rc = rd_db(data, PDb_GetMigrationVersion(data->db, &version), 0);
        if (rc == PRUNTIME_OK) {
            if ((unsigned int)version > data->info.target_schema) rc = PRUNTIME_SCHEMA;
            else {
                data->info.previous_schema = data->info.current_schema = (unsigned int)version;
                data->source_index = (unsigned int)version;
                data->plan = (PRuntimeMigrationPlanHandle)calloc(1, sizeof(*data->plan));
                if (!data->plan) rc = PRUNTIME_MEMORY;
            }
        }
        break;
    case 5:
        if (data->source_index == data->info.target_schema) { data->phase = 6; return PRUNTIME_OK; }
        migration = &r->manifest->migrations[data->source_index];
        for (i = 0; i < r->count; ++i) if (!strcmp(r->files[i].path, migration->file)) { file = &r->files[i]; break; }
        if (!file || !file->file_bytes) return rd_abort(data, PRUNTIME_INVALID);
        if (file->file_bytes > PRUNTIME_MIGRATION_SQL_BYTES) return rd_abort(data, PRUNTIME_LIMIT);
        source = &data->plan->sources[data->plan->info.file_count];
        source->sql = (const char*)malloc(file->file_bytes); if (!source->sql) return rd_abort(data, PRUNTIME_MEMORY);
        source->sql_bytes = file->file_bytes; ++data->plan->info.file_count;
        got = 0; rc = rv_provider_rc(r->provider.read_at(r->provider.user_data, r->root.root,
                file->path, 0, (void*)source->sql, file->file_bytes, &got));
        if (rc == PRUNTIME_OK && got != file->file_bytes) rc = PRUNTIME_IO;
        if (rc == PRUNTIME_OK && (!runtime_text_mode(source->sql, got, 1) ||
                (got >= 3 && !memcmp(source->sql, "\xef\xbb\xbf", 3)))) rc = PRUNTIME_INVALID;
        if (rc == PRUNTIME_OK) { source->schema_version = migration->schema_version; ++data->source_index; }
        return rc == PRUNTIME_OK ? PRUNTIME_OK : rd_abort(data, rc);
    case 6: rc = rd_refresh(data); break;
    case 7:
        if (data->apply_index == data->plan->info.file_count) { data->phase = 8; return PRUNTIME_OK; }
        source = &data->plan->sources[data->apply_index];
        memset(&db_options, 0, sizeof(db_options)); db_options.size = sizeof(db_options); db_options.version = PDB_RESTRICTED_VERSION;
        db_options.vm_budget = data->options.vm_budget; db_options.cancelled = data->options.cancelled;
        db_options.user_data = data->options.user_data;
        memset(&report, 0, sizeof(report)); report.size = sizeof(report); report.version = PDB_MIGRATION_VERSION;
        rc = rd_db(data, PDb_ApplyMigrationRestricted(data->db, (int)source->schema_version - 1,
                (int)source->schema_version, source->sql, source->sql_bytes, &db_options, &report), source->schema_version);
        if (rc == PRUNTIME_OK) {
            ++data->apply_index; ++data->info.applied_files; data->info.current_schema = source->schema_version;
            data->phase = 6; return PRUNTIME_OK;
        }
        break;
    case 8:
        rc = rd_db(data, PDb_GetMigrationVersion(data->db, &version), 0);
        if (rc == PRUNTIME_OK && (unsigned int)version != data->info.target_schema) rc = PRUNTIME_SCHEMA;
        break;
    default:
        rc = rd_refresh(data);
        if (rc == PRUNTIME_OK) { data->info.state = PRUNTIME_INSTALL_DATA_PREPARED; return PRUNTIME_INSTALL_DATA_DONE; }
        break;
    }
    if (rc != PRUNTIME_OK) return rd_abort(data, rc);
    ++data->phase; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallDataStep(PRuntimeInstallDataHandle data)
{
    int rc = rd_ready(data); if (rc != PRUNTIME_OK) return rc;
    if (data->info.state != PRUNTIME_INSTALL_DATA_INITIALIZING && data->info.state != PRUNTIME_INSTALL_DATA_PENDING) return PRUNTIME_STATE;
    data->active = data->intent->active = data->intent->parent->active = 1;
    data->info.state = PRUNTIME_INSTALL_DATA_PENDING; rc = rd_step(data);
    data->active = data->intent->active = data->intent->parent->active = 0; return rc;
}
PRUNTIME_API int PRuntime_InstallDataGetInfo(PRuntimeInstallDataHandle data, PRuntimeInstallDataInfo* info)
{
    int rc = rd_ready(data); if (rc != PRUNTIME_OK) return rc;
    if (!info || info->size != sizeof(*info) || info->version != 1) return PRUNTIME_ARGUMENT;
    *info = data->info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallDataCancel(PRuntimeInstallDataHandle data)
{
    int rc = rd_ready(data); if (rc != PRUNTIME_OK) return rc;
    if (data->info.state != PRUNTIME_INSTALL_DATA_INITIALIZING && data->info.state != PRUNTIME_INSTALL_DATA_PENDING) return PRUNTIME_STATE;
    data->cancelled = 1; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallDataClose(PRuntimeInstallDataHandle* slot)
{
    PRuntimeInstallDataHandle data;
    int rc;
    if (!slot) return PRUNTIME_ARGUMENT;
    data = *slot; if (!data) return PRUNTIME_OK;
    rc = rd_ready(data); if (rc != PRUNTIME_OK) return rc;
    if (data->commit_children) return PRUNTIME_STATE;
    data->active = data->intent->active = data->intent->parent->active = 1;
    if (data->db) PDb_Close(data->db);
    if (data->location.token) data->provider.close_location(data->provider.user_data, data->location.token);
    if (data->plan) runtime_plan_dispose(data->plan);
    --data->intent->data_children; data->intent->active = data->intent->parent->active = 0;
    data->magic = 0; free(data); *slot = NULL; return PRUNTIME_OK;
}


#define RICOMMIT_MAGIC 0x52494331UL
struct PRuntimeInstallCommit {
    DWORD magic;
    PRuntimeInstallIntentHandle intent;
    PRuntimeInstallDataHandle data;
    PRuntimeRegistrationTransition transition;
    PRuntimeInstallCommitInfo info;
    unsigned int phase, cancelled;
    int active, cas_result;
};
typedef char runtime_install_commit_budget_check[(sizeof(struct PRuntimeRevision) +
    sizeof(struct PRuntimeInstallIntent) + sizeof(struct PRuntimeInstallData) +
    sizeof(struct PRuntimeInstallCommit) + sizeof(struct PRuntimeMigrationPlan) +
    PRUNTIME_MIGRATION_TOTAL_BYTES + 131072U <= 1280U * 1024U) ? 1 : -1];
static int rc_ready(PRuntimeInstallCommitHandle job)
{
    if (!job || job->magic != RICOMMIT_MAGIC) return PRUNTIME_ARGUMENT;
    return job->active ? PRUNTIME_STATE : ri_ready(job->intent);
}
static int rc_fail(PRuntimeInstallCommitHandle job, int error, int unresolved)
{
    job->info.error = unresolved ? PRUNTIME_UNRESOLVED : error;
    job->info.state = unresolved ? PRUNTIME_INSTALL_COMMIT_UNRESOLVED : PRUNTIME_INSTALL_COMMIT_FAILED;
    ri_fail(job->intent, error, unresolved);
    return job->info.error;
}
PRUNTIME_API int PRuntime_InstallCommitBegin(PRuntimeInstallIntentHandle intent,
        PRuntimeInstallDataHandle data, PRuntimeInstallCommitHandle* output)
{
    PRuntimeInstallCommitHandle job;
    int rc = ri_ready(intent);
    if (rc != PRUNTIME_OK) return rc;
    if (!output) return PRUNTIME_ARGUMENT;
    if (intent->info.state != PRUNTIME_INTENT_PREPARED || intent->commit_children ||
            !intent->info.record_known || !intent->info.record_exists ||
            intent->info.record.state != PRUNTIME_REG_PREPARING) return PRUNTIME_STATE;
    if (intent->info.record.approval.approved_permissions & PRUNTIME_PERMISSION_SQL) {
        rc = rd_ready(data); if (rc != PRUNTIME_OK) return rc;
        if (data->intent != intent || data->info.state != PRUNTIME_INSTALL_DATA_PREPARED ||
                !data->db || !data->location.token || data->commit_children ||
                data->info.current_schema != intent->info.record.target_schema) return PRUNTIME_STATE;
    } else if (data || intent->data_children) return PRUNTIME_ARGUMENT;
    if (intent->info.record.generation == 2147483647U) return PRUNTIME_LIMIT;
    job = (PRuntimeInstallCommitHandle)calloc(1, sizeof(*job)); if (!job) return PRUNTIME_MEMORY;
    job->magic = RICOMMIT_MAGIC; job->intent = intent; job->data = data;
    job->transition.size = sizeof(job->transition); job->transition.version = 1;
    job->transition.previous_exists = 1; job->transition.previous = intent->info.record;
    job->transition.proposed = intent->info.record; job->transition.proposed.state = PRUNTIME_REG_READY;
    ++job->transition.proposed.generation;
    job->info.size = sizeof(job->info); job->info.version = 1;
    job->info.state = PRUNTIME_INSTALL_COMMIT_PENDING;
    runtime_storage_error_init(&job->info.data_error, PRUNTIME_OK);
    ++intent->commit_children; if (data) ++data->commit_children;
    *output = job; return PRUNTIME_OK;
}
static int rc_step(PRuntimeInstallCommitHandle job)
{
    PRuntimeInstallIntentHandle intent = job->intent;
    PRuntimeRegistrationRecord current;
    PRuntimeRegistrationDecision decision;
    PDbConnectionState db_state;
    int version, found, rc;
    unsigned int outcome;
    if (job->phase == 1 && job->data) {
        memset(&db_state, 0, sizeof(db_state)); db_state.size = sizeof(db_state); db_state.version = PDB_CONNECTION_STATE_VERSION;
        rc = PDb_GetConnectionState(job->data->db, &db_state);
        if (rc != PDB_OK || db_state.transaction_active || db_state.statement_count)
            return rc_fail(job, runtime_db_error(job->data->db, &job->info.data_error, rc != PDB_OK ? rc : PDB_STATE, 0), 0);
        rc = PDb_GetMigrationVersion(job->data->db, &version);
        if (rc != PDB_OK) return rc_fail(job, runtime_db_error(job->data->db, &job->info.data_error, rc, 0), 0);
        if ((unsigned int)version != job->transition.previous.target_schema) return rc_fail(job, PRUNTIME_SCHEMA, 0);
        ++job->phase; return PRUNTIME_OK;
    }
    if (job->phase == 1) { ++job->phase; return PRUNTIME_OK; }
    if (job->phase == 3) {
        if ((intent->parent->options.cancelled && intent->parent->options.cancelled(intent->parent->options.user_data)) ||
                (job->data && job->data->options.cancelled && job->data->options.cancelled(job->data->options.user_data)))
            job->cancelled = 1;
        if (job->cancelled) job->transition.proposed.state = PRUNTIME_REG_CANCELLED;
        outcome = PRUNTIME_COMMIT_UNKNOWN;
        rc = rv_provider_rc(intent->provider.compare_exchange(intent->provider.user_data, intent->lease,
                &job->transition.previous, &job->transition.proposed, &outcome));
        if (outcome < PRUNTIME_COMMIT_CONFIRMED || outcome > PRUNTIME_COMMIT_UNKNOWN) {
            outcome = PRUNTIME_COMMIT_UNKNOWN; rc = PRUNTIME_IO;
        }
        job->cas_result = job->info.provider_result = intent->info.provider_result = rc;
        job->transition.commit_outcome = job->info.commit_outcome = intent->info.commit_outcome = outcome;
        intent->info.record_known = intent->info.record_exists = 0;
        memset(&intent->info.record, 0, sizeof(intent->info.record));
        ++job->phase; return PRUNTIME_OK;
    }
    memset(&current, 0, sizeof(current)); current.size = sizeof(current); current.version = 1;
    rc = intent->provider.read(intent->provider.user_data, intent->lease, &current);
    found = rc != PRUNTIME_NOT_FOUND;
    if (!found) rc = PRUNTIME_OK; else rc = rv_provider_rc(rc);
    job->info.provider_result = rc;
    if (rc != PRUNTIME_OK) return rc_fail(job, rc, job->phase == 4);
    if (found) rc = rv_registration_valid(intent->parent, &current);
    if (rc != PRUNTIME_OK) return rc_fail(job, rc, 0);
    ri_observed(intent, &current, found);
    if (job->phase < 3) {
        if (!found || !rv_same_registration(&current, &job->transition.previous))
            return rc_fail(job, PRUNTIME_INVALID, 0);
        ++job->phase; return PRUNTIME_OK;
    }
    memset(&decision, 0, sizeof(decision));
    rc = rv_registration_decide(&job->transition, &current, found, &decision);
    if (rc != PRUNTIME_OK) return rc_fail(job, rc, 0);
    if (decision.match == PRUNTIME_REG_MATCH_PREVIOUS)
        return rc_fail(job, job->cas_result != PRUNTIME_OK ? job->cas_result : PRUNTIME_IO, 0);
    job->info.record = current;
    if (current.state == PRUNTIME_REG_READY) {
        job->info.state = PRUNTIME_INSTALL_COMMIT_COMPLETE;
        intent->info.state = PRUNTIME_INTENT_FINALIZED;
        return PRUNTIME_INSTALL_COMMIT_DONE;
    }
    job->info.state = PRUNTIME_INSTALL_COMMIT_CANCELLED; job->info.error = PRUNTIME_CANCELLED;
    intent->info.state = PRUNTIME_INTENT_CANCELLED; intent->info.error = PRUNTIME_CANCELLED;
    return PRUNTIME_CANCELLED;
}
PRUNTIME_API int PRuntime_InstallCommitStep(PRuntimeInstallCommitHandle job)
{
    int rc = rc_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (job->info.state != PRUNTIME_INSTALL_COMMIT_PENDING) return PRUNTIME_STATE;
    job->active = job->intent->active = job->intent->parent->active = 1;
    if (job->data) job->data->active = 1;
    rc = rc_step(job);
    if (job->data) job->data->active = 0;
    job->intent->parent->active = job->intent->active = job->active = 0;
    return rc;
}
PRUNTIME_API int PRuntime_InstallCommitGetInfo(PRuntimeInstallCommitHandle job, PRuntimeInstallCommitInfo* info)
{
    int rc = rc_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (!info || info->size != sizeof(*info) || info->version != 1) return PRUNTIME_ARGUMENT;
    *info = job->info; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallCommitCancel(PRuntimeInstallCommitHandle job)
{
    int rc = rc_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (job->info.state != PRUNTIME_INSTALL_COMMIT_PENDING) return PRUNTIME_STATE;
    job->cancelled = 1; return PRUNTIME_OK;
}
PRUNTIME_API int PRuntime_InstallCommitClose(PRuntimeInstallCommitHandle* slot)
{
    PRuntimeInstallCommitHandle job;
    int rc;
    if (!slot) return PRUNTIME_ARGUMENT;
    job = *slot; if (!job) return PRUNTIME_OK;
    rc = rc_ready(job); if (rc != PRUNTIME_OK) return rc;
    if (job->phase == 4 && job->info.state == PRUNTIME_INSTALL_COMMIT_PENDING)
        ri_fail(job->intent, PRUNTIME_UNRESOLVED, 1);
    if (job->data) --job->data->commit_children;
    --job->intent->commit_children;
    job->magic = 0; free(job); *slot = NULL; return PRUNTIME_OK;
}
