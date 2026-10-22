#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "positron_runtime.h"
#include "positron_json.h"
static const char* runtime_error = "Runtime manifest fixture failed";
const char* test1364_runtime_last_error(void) { return runtime_error; }
static int runtime_fixture(char* output, const char* id, const char* version,
        const char* entry, const char* permissions, const char* migrations)
{
    return _snprintf(output, 4096, "{\"manifestVersion\":1,\"id\":\"%s\",\"name\":\"Notes\","
        "\"version\":%s,\"runtimeApiVersion\":1,\"entry\":\"%s\",\"permissions\":%s,\"migrations\":%s}",
        id, version, entry, permissions, migrations);
}
static int runtime_reject(const char* input, int expected)
{
    PRuntimeManifestHandle handle;
    int rc;
    handle = (PRuntimeManifestHandle)1;
    rc = PRuntime_ManifestCreate(input, (unsigned int)strlen(input), &handle);
    if (rc == PRUNTIME_OK) { PRuntime_ManifestClose(&handle); return 0; }
    return rc == expected && handle == (PRuntimeManifestHandle)1;
}
#define CHECK(test, why) do { if (!(test)) { runtime_error = why; goto done; } } while (0)
BOOL test1364_runtime_manifest_contract(void)
{
    static const char* const bad_ids[] = { "notes", ".com.demo", "com.demo.", "com..demo",
        "com.2demo", "com._demo", "com.demo-app", "com.%64emo", "com/demo", "com.demo:1",
        "com.\xe4\xb8\xad", " com.demo", "com.demo " };
    static const char* const bad_paths[] = { "/a", "a/", "a//b", "../a", "a/../b", "./a",
        "a/./b", "a\\b", "a:b", "%2e%2e/a", "a?x", "a#f", "a.", "a ", "a/*", "a/\x01" };
    static const char* const bad_json[] = { "{}tail", "{\"x\":1,\"x\":2}",
        "{\"x\":1,\"\\u0078\":2}", "\"\\u0000\"", "{\"\\u0000\":1}",
        "{\"x\":1e999}", "\"line\nfeed\"", "01", "[-01]", "\v{}", "\"\xc0\xaf\"" };
    PRuntimeManifestHandle handle;
    PRuntimeManifestInfo info;
    PRuntimeManifestInfo before;
    PRuntimeMigrationInfo migration;
    PJsonParseLimits limits;
    HANDLE root;
    char input[4096];
    char id[65];
    char canonical[64];
    char path[257];
    char* field;
    unsigned int i;
    int bytes;
    BOOL ok;
    handle = NULL; root = NULL; ok = FALSE;
    CHECK(PRuntime_NormalizeId("Com.Example.Notes", 17, canonical, sizeof(canonical)) == PRUNTIME_OK &&
            !strcmp(canonical, "com.example.notes"), "case canonicalization");
    CHECK(PRuntime_NormalizeId("org.demo_app.v2", 15, canonical, sizeof(canonical)) == PRUNTIME_OK, "underscore authority is not DNS");
    for (i = 0; i < sizeof(bad_ids) / sizeof(bad_ids[0]); i++) {
        strcpy(canonical, "unchanged");
        CHECK(PRuntime_NormalizeId(bad_ids[i], (unsigned int)strlen(bad_ids[i]), canonical, sizeof(canonical)) == PRUNTIME_INVALID &&
                !strcmp(canonical, "unchanged"), bad_ids[i]);
    }
    memset(id, 'a', 64); id[1] = '.'; id[63] = '\0';
    CHECK(PRuntime_NormalizeId(id, 63, canonical, sizeof(canonical)) == PRUNTIME_OK, "63-byte ID");
    strcpy(canonical, "canary");
    CHECK(PRuntime_NormalizeId(id, 63, canonical, 63) == PRUNTIME_LIMIT && !strcmp(canonical, "canary"), "ID capacity unchanged");
    id[63] = 'a'; id[64] = '\0';
    CHECK(PRuntime_NormalizeId(id, 64, canonical, sizeof(canonical)) == PRUNTIME_LIMIT, "64-byte ID");
    CHECK(PRuntime_ValidateResourcePath("assets/\xe4\xb8\xad.css", (unsigned int)strlen("assets/\xe4\xb8\xad.css")) == PRUNTIME_OK, "Unicode package path");
    for (i = 0; i < sizeof(bad_paths) / sizeof(bad_paths[0]); i++) {
        CHECK(PRuntime_ValidateResourcePath(bad_paths[i], (unsigned int)strlen(bad_paths[i])) == PRUNTIME_INVALID, bad_paths[i]);
    }
    memset(path, 'a', 256); path[256] = '\0';
    CHECK(PRuntime_ValidateResourcePath(path, 255) == PRUNTIME_OK &&
            PRuntime_ValidateResourcePath(path, 256) == PRUNTIME_LIMIT, "path byte budget");
    strcpy(path, "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a");
    CHECK(PRuntime_ValidateResourcePath(path, (unsigned int)strlen(path)) == PRUNTIME_OK, "16 path segments");
    strcat(path, "/a");
    CHECK(PRuntime_ValidateResourcePath(path, (unsigned int)strlen(path)) == PRUNTIME_LIMIT, "path depth budget");
    for (i = 0; i < 20; i++) {
        bytes = runtime_fixture(input, "Com.Example.Notes", "1", "index.html", "[\"storage.sql\"]",
                "[{\"version\":1,\"file\":\"schema/1.sql\"},{\"version\":2,\"file\":\"schema/2.sql\"}]");
        CHECK(PRuntime_ManifestCreate(input, bytes, &handle) == PRUNTIME_OK, "manifest admission");
        memset(input, 'x', bytes); /* DLL must own its validated snapshot */
        memset(&info, 0, sizeof(info)); info.size = sizeof(info); info.version = 1;
        CHECK(PRuntime_ManifestGetInfo(handle, &info) == PRUNTIME_OK && !strcmp(info.id, "com.example.notes") &&
                info.app_version == 1 && info.requested_permissions == PRUNTIME_PERMISSION_SQL && info.migration_count == 2,
                "immutable metadata, declarations not grants");
        migration.size = sizeof(migration); migration.version = 1;
        CHECK(PRuntime_ManifestGetMigration(handle, 1, &migration) == PRUNTIME_OK && migration.schema_version == 2 &&
                !strcmp(migration.file, "schema/2.sql"), "migration snapshot");
        info.version = 99; before = info;
        CHECK(PRuntime_ManifestGetInfo(handle, &info) == PRUNTIME_ARGUMENT && !memcmp(&before, &info, sizeof(info)), "query canary");
        CHECK(PRuntime_ManifestClose(&handle) == PRUNTIME_OK && handle == NULL &&
                PRuntime_ManifestClose(&handle) == PRUNTIME_OK, "repeated owned-slot close");
    }
    runtime_fixture(input, "com.notes", "1.5", "index.html", "[]", "[]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "fractional version");
    runtime_fixture(input, "", "1", "index.html", "[]", "[]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "empty metadata ID is content error");
    runtime_fixture(input, "com.notes", "1", "", "[]", "[]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "empty metadata path is content error");
    runtime_fixture(input, "com.notes", "2147483648", "index.html", "[]", "[]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "version overflow");
    runtime_fixture(input, "com.notes", "1", "../index.html", "[]", "[]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "entry traversal");
    runtime_fixture(input, "com.notes", "1", "index.html", "[\"network\"]", "[]");
    CHECK(runtime_reject(input, PRUNTIME_UNSUPPORTED), "unknown permission");
    runtime_fixture(input, "com.notes", "1", "index.html", "[]", "[{\"version\":1,\"file\":\"a.sql\"}]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "migrations without SQL permission");
    runtime_fixture(input, "com.notes", "1", "index.html", "[\"storage.sql\"]", "[{\"version\":2,\"file\":\"a.sql\"}]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "migration version gap");
    runtime_fixture(input, "com.notes", "1", "index.html", "[\"storage.sql\"]",
            "[{\"version\":1,\"file\":\"A.sql\"},{\"version\":2,\"file\":\"a.sql\"}]");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "case-conflicting migration file");
    CHECK(runtime_reject("{}", PRUNTIME_INVALID), "missing fields");
    runtime_fixture(input, "com.notes", "1", "index.html", "[]", "[]");
    field = strstr(input, "\"runtimeApiVersion\":1"); field[strlen("\"runtimeApiVersion\":")] = '2';
    CHECK(runtime_reject(input, PRUNTIME_UNSUPPORTED), "unknown runtime API version");
    runtime_fixture(input, "com.notes", "1", "index.html", "[]", "[]");
    field = strstr(input, "\"manifestVersion\":1"); field[strlen("\"manifestVersion\":")] = '2';
    CHECK(runtime_reject(input, PRUNTIME_UNSUPPORTED), "unknown manifest version");
    bytes = runtime_fixture(input, "com.notes", "1", "index.html", "[]", "[]");
    strcpy(input + bytes - 1, ",\"extra\":1}");
    CHECK(runtime_reject(input, PRUNTIME_INVALID), "unknown manifest field");
    limits.size = sizeof(limits); limits.version = 1; limits.max_bytes = 4096; limits.max_depth = 8; limits.max_tokens = 256;
    for (i = 0; i < sizeof(bad_json) / sizeof(bad_json[0]); i++) {
        root = (HANDLE)1;
        CHECK(PJson_ParseEx(bad_json[i], (unsigned int)strlen(bad_json[i]), &limits, &root) == PJSON_PARSE_INVALID &&
                root == (HANDLE)1, bad_json[i]);
        root = NULL;
    }
    limits.max_depth = 1;
    CHECK(PJson_ParseEx("[[]]", 4, &limits, &root) == PJSON_PARSE_LIMIT && root == NULL, "depth budget");
    limits.max_depth = 8; limits.max_tokens = 2;
    CHECK(PJson_ParseEx("[1,2]", 5, &limits, &root) == PJSON_PARSE_LIMIT, "token budget");
    limits.max_tokens = 256; limits.max_bytes = 1;
    CHECK(PJson_ParseEx("[]", 2, &limits, &root) == PJSON_PARSE_LIMIT, "byte budget");
    limits.max_bytes = 4096; limits.version = 2;
    CHECK(PJson_ParseEx("[]", 2, &limits, &root) == PJSON_PARSE_ARGUMENT && root == NULL, "JSON version canary");
    root = PJson_Parse("{\"legacy\":1} trailing");
    CHECK(root != NULL && PJson_GetInt(root, "legacy") == 1, "legacy JSON behavior unchanged");
    PJson_Free(root); root = NULL;
    ok = TRUE;
done:
    if (root != (HANDLE)1) PJson_Free(root);
    PRuntime_ManifestClose(&handle);
    return ok;
}
