/*
 * positron_json.c - Implementation of the Positron JSON DLL.
 *
 * Wraps cJSON behind opaque HANDLEs so callers never need to include
 * cJSON.h. All logic is delegated; this file is bookkeeping plus
 * lifetime contracts.
 *
 * C89 only.
 */

#include <windows.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "positron_json.h"

/* ------------------------------------------------------------------ */
/* DllMain                                                            */
/* ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)hModule;
    (void)lpReserved;
    (void)reason;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

PJSON_API HANDLE PJson_Parse(const char* json_string)
{
    cJSON* root;

    if (json_string == NULL) {
        return NULL;
    }
    root = cJSON_Parse(json_string);
    return (HANDLE)root;
}

static int json_utf8(const char* text, unsigned int bytes)
{
    unsigned int i;
    unsigned int n;
    unsigned long code;
    unsigned long minimum;
    unsigned char c;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)text[i];
        if (c == 0) return 0;
        if (c < 128) continue;
        if (c >= 194 && c <= 223) { n = 1; code = c & 31; minimum = 128; }
        else if (c >= 224 && c <= 239) { n = 2; code = c & 15; minimum = 2048; }
        else if (c >= 240 && c <= 244) { n = 3; code = c & 7; minimum = 65536; }
        else return 0;
        while (n-- > 0) {
            if (++i >= bytes || ((unsigned char)text[i] & 192) != 128) return 0;
            code = (code << 6) | ((unsigned char)text[i] & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}
static int json_tree_valid(const cJSON* item)
{
    const cJSON* child;
    const cJSON* other;
    if (cJSON_IsNumber(item) && (!(item->valuedouble <= DBL_MAX) ||
            !(item->valuedouble >= -DBL_MAX))) return 0;
    for (child = item->child; child != NULL; child = child->next) {
        if (cJSON_IsObject(item)) {
            for (other = item->child; other != child; other = other->next) {
                if (strcmp(child->string, other->string) == 0) return 0;
            }
        }
        if (!json_tree_valid(child)) return 0;
    }
    return 1;
}
PJSON_API int PJson_ParseEx(const char* json, unsigned int bytes,
        const PJsonParseLimits* limits, HANDLE* outRoot)
{
    unsigned int i;
    unsigned int depth;
    unsigned int tokens;
    int string;
    int atom;
    unsigned char c;
    char* copy;
    cJSON* root;
    if (json == NULL || outRoot == NULL || limits == NULL || bytes == 0 ||
            limits->size != sizeof(*limits) || limits->version != PJSON_PARSE_VERSION ||
            limits->max_bytes < 1 || limits->max_bytes > 65536 || limits->max_depth < 1 ||
            limits->max_depth > 16 || limits->max_tokens < 1 || limits->max_tokens > 1024) return PJSON_PARSE_ARGUMENT;
    if (bytes > limits->max_bytes) return PJSON_PARSE_LIMIT;
    if (!json_utf8(json, bytes) || (bytes >= 3 && !memcmp(json, "\xef\xbb\xbf", 3))) return PJSON_PARSE_INVALID;
    depth = tokens = 0; string = atom = 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)json[i];
        if (c < 32 && (string || (c != '\t' && c != '\r' && c != '\n'))) return PJSON_PARSE_INVALID;
        if (string) {
            if (c == '\\') {
                if (++i >= bytes) return PJSON_PARSE_INVALID;
                if (json[i] == 'u' && i + 4 < bytes && !memcmp(json + i + 1, "0000", 4)) return PJSON_PARSE_INVALID;
            } else if (c == '"') string = 0;
            continue;
        }
        if (c == '0' && i + 1 < bytes && json[i + 1] >= '0' && json[i + 1] <= '9' &&
                (i == 0 || json[i - 1] == '-' || strchr("[{: ,\t\r\n", json[i - 1]) != NULL)) return PJSON_PARSE_INVALID;
        if (c == '"') { string = 1; atom = 0; tokens++; }
        else if (c == '{' || c == '[') { depth++; tokens++; atom = 0; }
        else if (c == '}' || c == ']') { if (depth == 0) return PJSON_PARSE_INVALID; depth--; atom = 0; }
        else if (c == ',' || c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') atom = 0;
        else if (!atom) { atom = 1; tokens++; }
        if (depth > limits->max_depth || tokens > limits->max_tokens) return PJSON_PARSE_LIMIT;
    }
    if (depth != 0 || string) return PJSON_PARSE_INVALID;
    copy = (char*)malloc(bytes + 1);
    if (copy == NULL) return PJSON_PARSE_INVALID;
    memcpy(copy, json, bytes); copy[bytes] = '\0';
    root = cJSON_ParseWithLengthOpts(copy, bytes + 1, NULL, 1);
    free(copy);
    if (root == NULL) return PJSON_PARSE_INVALID;
    if (!json_tree_valid(root)) { cJSON_Delete(root); return PJSON_PARSE_INVALID; }
    *outRoot = (HANDLE)root;
    return PJSON_PARSE_OK;
}
PJSON_API const char* PJson_GetStringValue(HANDLE value)
{
    cJSON* item;
    item = (cJSON*)value;
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

PJSON_API void PJson_Free(HANDLE hObj)
{
    if (hObj == NULL) {
        return;
    }
    cJSON_Delete((cJSON*)hObj);
}

PJSON_API const char* PJson_GetString(HANDLE hObj, const char* key)
{
    cJSON* item;

    if (hObj == NULL || key == NULL) {
        return NULL;
    }
    item = cJSON_GetObjectItemCaseSensitive((cJSON*)hObj, key);
    if (item == NULL || !cJSON_IsString(item)) {
        return NULL;
    }
    return item->valuestring;
}

PJSON_API int PJson_GetInt(HANDLE hObj, const char* key)
{
    cJSON* item;

    if (hObj == NULL || key == NULL) {
        return 0;
    }
    item = cJSON_GetObjectItemCaseSensitive((cJSON*)hObj, key);
    if (item == NULL || !cJSON_IsNumber(item)) {
        return 0;
    }
    return item->valueint;
}

PJSON_API int PJson_GetType(HANDLE hObj)
{
    cJSON* item;

    if (hObj == NULL) {
        return PJSON_TYPE_INVALID;
    }
    item = (cJSON*)hObj;
    if (cJSON_IsNull(item)) {
        return PJSON_TYPE_NULL;
    }
    if (cJSON_IsFalse(item)) {
        return PJSON_TYPE_FALSE;
    }
    if (cJSON_IsTrue(item)) {
        return PJSON_TYPE_TRUE;
    }
    if (cJSON_IsNumber(item)) {
        return PJSON_TYPE_NUMBER;
    }
    if (cJSON_IsString(item)) {
        return PJSON_TYPE_STRING;
    }
    if (cJSON_IsArray(item)) {
        return PJSON_TYPE_ARRAY;
    }
    if (cJSON_IsObject(item)) {
        return PJSON_TYPE_OBJECT;
    }
    return PJSON_TYPE_INVALID;
}

PJSON_API int PJson_GetBool(HANDLE hObj, const char* key)
{
    cJSON* item;

    if (hObj == NULL || key == NULL) {
        return -1;
    }
    item = cJSON_GetObjectItemCaseSensitive((cJSON*)hObj, key);
    if (item == NULL) {
        return -1;
    }
    if (cJSON_IsTrue(item)) {
        return 1;
    }
    if (cJSON_IsFalse(item)) {
        return 0;
    }
    return -1;
}

PJSON_API int PJson_GetDouble(HANDLE hObj, const char* key,
        double* outValue)
{
    cJSON* item;

    if (hObj == NULL || key == NULL || outValue == NULL) {
        return 0;
    }
    item = cJSON_GetObjectItemCaseSensitive((cJSON*)hObj, key);
    if (item == NULL || !cJSON_IsNumber(item)) {
        return 0;
    }
    *outValue = item->valuedouble;
    return 1;
}

PJSON_API HANDLE PJson_GetObject(HANDLE hObj, const char* key)
{
    cJSON* item;

    if (hObj == NULL || key == NULL) {
        return NULL;
    }
    item = cJSON_GetObjectItemCaseSensitive((cJSON*)hObj, key);
    return (HANDLE)item;
}

PJSON_API int PJson_GetObjectSize(HANDLE hObj)
{
    cJSON* item;
    int count;

    if (hObj == NULL || !cJSON_IsObject((cJSON*)hObj)) {
        return 0;
    }
    count = 0;
    item = ((cJSON*)hObj)->child;
    while (item != NULL) {
        count += 1;
        item = item->next;
    }
    return count;
}

PJSON_API const char* PJson_GetObjectKey(HANDLE hObj, int index)
{
    cJSON* item;

    if (hObj == NULL || index < 0 || !cJSON_IsObject((cJSON*)hObj)) {
        return NULL;
    }
    item = ((cJSON*)hObj)->child;
    while (item != NULL && index > 0) {
        item = item->next;
        index -= 1;
    }
    if (item == NULL) {
        return NULL;
    }
    return item->string;
}

PJSON_API HANDLE PJson_GetObjectValue(HANDLE hObj, int index)
{
    cJSON* item;

    if (hObj == NULL || index < 0 || !cJSON_IsObject((cJSON*)hObj)) {
        return NULL;
    }
    item = ((cJSON*)hObj)->child;
    while (item != NULL && index > 0) {
        item = item->next;
        index -= 1;
    }
    return (HANDLE)item;
}

PJSON_API HANDLE PJson_GetArrayItem(HANDLE hObj, int index)
{
    cJSON* item;

    if (hObj == NULL || index < 0) {
        return NULL;
    }
    if (!cJSON_IsArray((cJSON*)hObj)) {
        return NULL;
    }
    item = cJSON_GetArrayItem((cJSON*)hObj, index);
    return (HANDLE)item;
}

PJSON_API int PJson_GetArraySize(HANDLE hObj)
{
    if (hObj == NULL) {
        return 0;
    }
    if (!cJSON_IsArray((cJSON*)hObj)) {
        return 0;
    }
    return cJSON_GetArraySize((cJSON*)hObj);
}

PJSON_API char* PJson_Serialize(HANDLE hObj)
{
    if (hObj == NULL) {
        return NULL;
    }
    return cJSON_PrintUnformatted((cJSON*)hObj);
}

PJSON_API void PJson_FreeString(char* str)
{
    if (str == NULL) {
        return;
    }
    cJSON_free(str);
}
