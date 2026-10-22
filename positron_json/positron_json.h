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

/* Stable node types for callers that need to distinguish JSON null,
 * booleans and numbers without depending on cJSON internals. */
enum {
    PJSON_TYPE_INVALID = 0,
    PJSON_TYPE_NULL = 1,
    PJSON_TYPE_FALSE = 2,
    PJSON_TYPE_TRUE = 3,
    PJSON_TYPE_NUMBER = 4,
    PJSON_TYPE_STRING = 5,
    PJSON_TYPE_ARRAY = 6,
    PJSON_TYPE_OBJECT = 7
};

/* Parse a UTF-8 JSON string. Returns NULL on parse error. */
PJSON_API HANDLE PJson_Parse(const char* json_string);

#define PJSON_PARSE_VERSION 1
#define PJSON_PARSE_OK 0
#define PJSON_PARSE_ARGUMENT -1
#define PJSON_PARSE_LIMIT -2
#define PJSON_PARSE_INVALID -3
typedef struct PJsonParseLimits {
    unsigned int size;
    unsigned int version;
    unsigned int max_bytes;  /* 1..65536 */
    unsigned int max_depth;  /* 1..16 container levels */
    unsigned int max_tokens; /* 1..1024; includes object keys/containers */
} PJsonParseLimits;
/* Strict additive admission: explicit UTF-8 length, no BOM/NUL, trailing
 * material, duplicate keys, nonfinite numbers or decoded NUL strings/keys.
 * Preflight bounds containers/tokens before cJSON allocation; cJSON still
 * owns grammar/escape parsing. INVALID includes parser allocation failure
 * (cJSON does not distinguish it here); never change global allocator hooks.
 * No callbacks or public global error slot. Outputs unchanged on failure; successful
 * root is caller-owned and freed by PJson_Free. Old Parse is unchanged. */
PJSON_API int PJson_ParseEx(const char* json, unsigned int bytes,
        const PJsonParseLimits* limits, HANDLE* outRoot);
/* Borrowed scalar string, or NULL for non-string; lifetime follows root.
 * ParseEx excludes embedded NUL; legacy Parse retains legacy C-string limits. */
PJSON_API const char* PJson_GetStringValue(HANDLE value);

/* Free a top-level handle obtained from PJson_Parse. NULL-safe. */
PJSON_API void PJson_Free(HANDLE hObj);

/* Get the string value at `key` on a JSON object.
 * Returns NULL if hObj isn't an object, key is missing, or the value
 * isn't a string. Returned pointer aliases internal storage and is
 * valid until hObj's top-level handle is freed. */
PJSON_API const char* PJson_GetString(HANDLE hObj, const char* key);

/* Get the integer value at `key`. Returns 0 if missing or not numeric. */
PJSON_API int PJson_GetInt(HANDLE hObj, const char* key);

/* Return the type of a node handle. Returns PJSON_TYPE_INVALID for NULL or
 * an invalid handle. The handle may be the top-level parse result or a
 * borrowed child returned by one of the traversal functions. */
PJSON_API int PJson_GetType(HANDLE hObj);

/* Get a boolean value at `key`. Returns 1 for true, 0 for false and -1 when
 * the key is missing or is not a JSON boolean. */
PJSON_API int PJson_GetBool(HANDLE hObj, const char* key);

/* Get a JSON number as double. Returns non-zero on success and leaves the
 * output untouched on failure. */
PJSON_API int PJson_GetDouble(HANDLE hObj, const char* key,
        double* outValue);

/* Get a nested JSON value by key. Returns NULL if missing.
 * The returned handle is owned by hObj's top-level handle. DO NOT free. */
PJSON_API HANDLE PJson_GetObject(HANDLE hObj, const char* key);

/* Enumerate direct children of a JSON object. Returned key strings and value
 * handles are borrowed from the top-level parse tree. */
PJSON_API int PJson_GetObjectSize(HANDLE hObj);
PJSON_API const char* PJson_GetObjectKey(HANDLE hObj, int index);
PJSON_API HANDLE PJson_GetObjectValue(HANDLE hObj, int index);

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

#define PJSON_ENCODE_VERSION 1
#define PJSON_ENCODE_OK 0
#define PJSON_ENCODE_ARGUMENT -1
#define PJSON_ENCODE_LIMIT -2
#define PJSON_ENCODE_INVALID -3
#define PJSON_ENCODE_MEMORY -4
#define PJSON_SCALAR_TEXT_BYTES 4096U
typedef struct PJsonScalar {
    unsigned int size;
    unsigned int version;
    int type; /* NULL, FALSE, TRUE, NUMBER or STRING from PJSON_TYPE_* */
    double number;
    const char* text;
    unsigned int text_bytes;
} PJsonScalar;
/* Bounded additive scalar encoder owned by JSON, no global allocator hooks.
 * Text explicit valid UTF-8 <=4096 bytes, no NUL; empty text may be NULL.
 * Numbers finite. cJSON owns escaping/number formatting; int64 callers use a
 * decimal STRING, never convert to double. Output always NUL terminated;
 * written excludes NUL. NULL buffer/capacity=0 is a successful size probe.
 * Capacity failure preserves buffer/written; other failures likewise.
 * Worst string size <=24578 bytes plus NUL, transient allocations bounded by
 * input/escaped size. No persistent handle or callback; old APIs unchanged. */
PJSON_API int PJson_EncodeScalar(const PJsonScalar* scalar, char* buffer,
        unsigned int capacity, unsigned int* written);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_JSON_H */
