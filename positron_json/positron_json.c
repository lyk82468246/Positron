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
