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
