/*
 * positron_json.h - JSON parsing for the Positron framework.
 * Thin opaque-HANDLE wrapper over cJSON 1.7.18.
 *
 * Lifetime contract:
 *   PJson_Parse        -> returns a top-level HANDLE; caller MUST free it
 *                         with PJson_Free.
 *   PJson_GetObject    -> returns a HANDLE that aliases a child node of the
 *   PJson_GetArrayItem    parent. DO NOT free it; freeing the top-level
 *                         parent frees everything reachable.
 *   PJson_Serialize    -> returns a freshly malloc'd char*; caller MUST
 *                         free it with PJson_FreeString.
 *
 * All string I/O is UTF-8.
 */

#ifndef POSITRON_JSON_H
#define POSITRON_JSON_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_JSON_EXPORTS
#  define PJSON_API __declspec(dllexport)
#else
#  define PJSON_API __declspec(dllimport)
#endif

/* Parse a UTF-8 JSON string. Returns NULL on parse error. */
PJSON_API HANDLE PJson_Parse(const char* json_string);

/* Free a top-level handle obtained from PJson_Parse. NULL-safe. */
PJSON_API void PJson_Free(HANDLE hObj);

/* Get the string value at `key` on a JSON object.
 * Returns NULL if hObj isn't an object, key is missing, or the value
 * isn't a string. Returned pointer aliases internal storage and is
 * valid until hObj's top-level handle is freed. */
PJSON_API const char* PJson_GetString(HANDLE hObj, const char* key);

/* Get the integer value at `key`. Returns 0 if missing or not numeric. */
PJSON_API int PJson_GetInt(HANDLE hObj, const char* key);

/* Get a nested object or array node by key. Returns NULL if missing.
 * The returned handle is owned by hObj's top-level handle. DO NOT free. */
PJSON_API HANDLE PJson_GetObject(HANDLE hObj, const char* key);

/* Get an array element by zero-based index. NULL if out of range or
 * hObj isn't an array. Same lifetime rule as PJson_GetObject. */
PJSON_API HANDLE PJson_GetArrayItem(HANDLE hObj, int index);

/* Length of a JSON array. Returns 0 if hObj isn't an array. */
PJSON_API int PJson_GetArraySize(HANDLE hObj);

/* Serialize a JSON value to a freshly allocated UTF-8 string.
 * Returns NULL on allocation failure. Caller MUST free with
 * PJson_FreeString. */
PJSON_API char* PJson_Serialize(HANDLE hObj);

/* Free a string returned by PJson_Serialize. NULL-safe. */
PJSON_API void PJson_FreeString(char* str);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_JSON_H */
