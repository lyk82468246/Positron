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
    volatile LONG* operation_cancel;
};
static void runtime_storage_error_init(PRuntimeStorageError* error, int result)
{
    memset(error, 0, sizeof(*error)); error->size = sizeof(*error); error->version = PRUNTIME_VERSION;
    error->result = result;
}
static int runtime_storage_db_error(PRuntimeStorageHandle storage, int result, unsigned long schema)
{
    PDbErrorInfo error;
    int rc;
    error.size = sizeof(error); error.version = PDB_ERROR_INFO_VERSION;
    rc = PRUNTIME_DATABASE;
    if (result == PDB_NOMEM) rc = PRUNTIME_MEMORY;
    else if (result == PDB_LIMIT) rc = PRUNTIME_LIMIT;
    else if (result == PDB_BUSY) rc = PRUNTIME_BUSY;
    else if (result == PDB_SCHEMA_MISMATCH) rc = PRUNTIME_SCHEMA;
    runtime_storage_error_init(&storage->error, rc);
    storage->error.db_result = result; storage->error.failed_schema_version = schema;
    if (storage->db && PDb_GetErrorInfo(storage->db, &error) == PDB_OK) {
        storage->error.db_category = error.category; storage->error.sqlite_code = error.sqlite_code;
        storage->error.sqlite_extended_code = error.sqlite_extended_code;
        storage->error.cleanup_result = error.cleanup_result;
        memcpy(storage->error.message, error.message, sizeof(storage->error.message));
        if (error.category == PDB_ERROR_CATEGORY_CANCELLED) storage->error.result = rc = PRUNTIME_CANCELLED;
    }
    return rc;
}
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
    if (storage->owner != GetCurrentThreadId() || storage->active || storage->directory->active) return PRUNTIME_STATE;
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
    PRuntimeDirectoryHandle directory;
    int rc;
    if (!storage) return PRUNTIME_ARGUMENT;
    if (!*storage) return PRUNTIME_OK;
    rc = runtime_storage_ready(*storage); if (rc != PRUNTIME_OK) return rc;
    if ((*storage)->queue_count) return PRUNTIME_STATE;
    directory = (*storage)->directory; directory->active = (*storage)->active = 1;
    runtime_storage_dispose(*storage); *storage = NULL;
    directory->storage_count--; directory->active = 0; return PRUNTIME_OK;
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
    RuntimeSqlWork* work;
    work = (RuntimeSqlWork*)user;
    return (work->storage->operation_cancel && InterlockedCompareExchange((LONG*)work->storage->operation_cancel, 0, 0)) ||
        (work->storage->options.cancelled && work->storage->options.cancelled(work->storage->options.user_data));
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
        storage->active = storage->directory->active = 1;
        memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = PDB_RESTRICTED_VERSION;
        options.vm_budget = storage->options.vm_budget; options.cancelled = runtime_sql_cancel; options.row = runtime_sql_row; options.user_data = work;
        memset(&report, 0, sizeof(report)); report.size = sizeof(report); report.version = PDB_RESTRICTED_VERSION;
        native = PDb_ExecuteRestricted(storage->db, work->statements, count, &options, &report);
        if (native != PDB_OK) {
            rc = runtime_storage_db_error(storage, native, 0);
            if (work->result != PRUNTIME_OK) storage->error.result = rc = work->result;
            state.size = sizeof(state); state.version = PDB_CONNECTION_STATE_VERSION;
            if (PDb_GetConnectionState(storage->db, &state) != PDB_OK || state.transaction_active) storage->info.state = PRUNTIME_STORAGE_FAILED;
        } else {
            /* Counts <=8/16 and signed int64 changes: this fixed report fits
             * within the 128-byte allowance reserved before DB COMMIT. */
            sprintf(header, "{\"version\":1,\"statementCount\":%d,\"rowCount\":%d,\"changes\":{\"type\":\"int64\",\"value\":\"%I64d\"},\"rows\":[",
                report.statement_count, report.row_count, report.changes);
            length = (unsigned int)strlen(header);
            memcpy(output, header, length); memcpy(output + length, work->rows, work->used); length += work->used;
            memcpy(output + length, "]}", 3); *written = length + 2;
        }
        storage->active = storage->directory->active = 0;
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
    EnterCriticalSection(&(*queue)->lock);
    if (!(*queue)->revoked || (*queue)->processing) { LeaveCriticalSection(&(*queue)->lock); return PRUNTIME_STATE; }
    LeaveCriticalSection(&(*queue)->lock); DeleteCriticalSection(&(*queue)->lock);
    for (i = 0; i < PRUNTIME_SERVICE_MAX_QUEUES; i++) if ((*queue)->storage->queues[i] == *queue) { (*queue)->storage->queues[i] = NULL; break; }
    (*queue)->storage->queue_count--; free(*queue); *queue = NULL; return PRUNTIME_OK;
}
