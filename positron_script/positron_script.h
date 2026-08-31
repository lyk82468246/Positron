/*
 * positron_script.h - small public JavaScript runtime boundary.
 *
 * The implementation is Duktape 2.7.0, kept behind an opaque handle so
 * callers do not depend on Duktape headers, heap objects, or CRT ownership.
 * This first ABI is deliberately a standalone execution service. It does
 * not create a browser window, fetch resources, or expose DOM objects.
 */

#ifndef POSITRON_SCRIPT_H
#define POSITRON_SCRIPT_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_SCRIPT_EXPORTS
#  define PSCRIPT_API __declspec(dllexport)
#else
#  define PSCRIPT_API __declspec(dllimport)
#endif

#define PSCRIPT_ABI_VERSION 0x00010006UL
#define PSCRIPT_DEFAULT_BUDGET_MS 1000UL
#define PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES (512UL * 1024UL)
#define PSCRIPT_MAX_SOURCE_BYTES (64UL * 1024UL)
#define PSCRIPT_MAX_MODULE_NAME_BYTES 128UL
#define PSCRIPT_MAX_GLOBAL_NAME_BYTES 128UL
#define PSCRIPT_MAX_MODULES 16UL
#define PSCRIPT_MAX_NATIVE_FUNCTIONS 26UL

#define PSCRIPT_OK 0
#define PSCRIPT_ERROR_ARGUMENT (-1)
#define PSCRIPT_ERROR_SOURCE_TOO_LARGE (-2)
#define PSCRIPT_ERROR_EVALUATION (-3)
#define PSCRIPT_ERROR_TIMEOUT (-4)
#define PSCRIPT_ERROR_FATAL (-5)
#define PSCRIPT_ERROR_MEMORY_LIMIT (-6)
#define PSCRIPT_ERROR_MODULE_NAME (-7)
#define PSCRIPT_ERROR_MODULE_LIMIT (-8)
#define PSCRIPT_ERROR_MODULE_SOURCE (-9)
#define PSCRIPT_ERROR_GLOBAL (-10)
#define PSCRIPT_ERROR_CALL (-11)
#define PSCRIPT_ERROR_RESULT_TOO_LARGE (-12)
#define PSCRIPT_ERROR_JSON (-13)
#define PSCRIPT_ERROR_NATIVE (-14)
#define PSCRIPT_ERROR_NATIVE_LIMIT (-15)

/* Synchronous host-owned module source callbacks. Return 0 when source is
 * available and fill a byte buffer + non-negative byte count. The DLL calls
 * freefn after the current module evaluation returns; a no-op freefn may be
 * used for static storage. Callbacks run on the embedding thread and must
 * not call back into this context or destroy it. */
typedef int (*PScriptModuleSourceFn)(void *pw, const char *module_name,
        char **out_source, int *out_len);
typedef void (*PScriptModuleSourceFreeFn)(void *pw, char *source);

/* Returns the major/minor ABI version encoded as 0xMMMMmmmm. */
PSCRIPT_API unsigned long PScript_AbiVersion(void);

/* Create an isolated ECMAScript context. A zero budget selects the default
 * timeout. The context remains independent from positron_core documents. A
 * context is not safe for concurrent calls; the embedding program owns its
 * calling-thread discipline. */
PSCRIPT_API HANDLE PScript_Create(unsigned long budget_ms);

/* Create an isolated context with an explicit Duktape heap limit. A zero
 * memory_limit_bytes selects PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES. The limit
 * covers Duktape allocations plus the wrapper's allocation headers. */
PSCRIPT_API HANDLE PScript_CreateEx(unsigned long budget_ms,
        unsigned long memory_limit_bytes);

/* Destroy a context and all values retained by it. NULL is accepted. */
PSCRIPT_API void PScript_Destroy(HANDLE hScript);

/* Evaluate UTF-8 source. A negative source_len means NUL-terminated input.
 * The final expression value is available through PScript_GetResult. */
PSCRIPT_API int PScript_Evaluate(HANDLE hScript, const char *source,
        int source_len);

/* Set persistent JSON-compatible values in the global object. Names and
 * strings accept a negative length for NUL-terminated input. These values
 * survive later evaluations and module calls. */
PSCRIPT_API int PScript_SetGlobalString(HANDLE hScript, const char *name,
        int name_len, const char *value, int value_len);
PSCRIPT_API int PScript_SetGlobalNumber(HANDLE hScript, const char *name,
        int name_len, double value);
PSCRIPT_API int PScript_SetGlobalBoolean(HANDLE hScript, const char *name,
        int name_len, int value);
/* Set one persistent JSON value. Objects and arrays are copied into the
 * Duktape context; the input is consumed during this call and is not
 * retained by the DLL. JSON input uses the same source-size limit as
 * PScript_Evaluate. */
PSCRIPT_API int PScript_SetGlobalJson(HANDLE hScript, const char *name,
        int name_len, const char *value_json, int value_len);

/* Return one persistent global as compact JSON through PScript_GetResult.
 * Undefined/non-JSON values and encoded results over the DLL result buffer
 * are reported instead of being silently truncated. */
PSCRIPT_API int PScript_GetGlobalJson(HANDLE hScript, const char *name,
        int name_len);

/* Call a persistent global function with a JSON array of arguments, for
 * example "[2,3]". The JSON-encoded return value is available through
 * PScript_GetResult. This is a standalone embedding hook: it does not add
 * DOM, window, network or browser event bindings. */
PSCRIPT_API int PScript_CallGlobalJson(HANDLE hScript, const char *name,
        int name_len, const char *args_json, int args_len);

/* Synchronous host callback exposed as a global JavaScript function. The
 * callback receives the compact JSON array of JS arguments and writes one
 * JSON value to out_json. It must not re-enter or destroy hScript; pw is
 * borrowed only for the duration of the callback. */
typedef int (*PScriptJsonFunctionFn)(void *pw, const char *args_json,
        int args_len, char *out_json, int out_capacity, int *out_len);
PSCRIPT_API int PScript_RegisterGlobalJsonFunction(HANDLE hScript,
        const char *name, int name_len, PScriptJsonFunctionFn fn, void *pw);
PSCRIPT_API int PScript_UnregisterGlobalJsonFunction(HANDLE hScript,
        const char *name, int name_len);
PSCRIPT_API unsigned long PScript_GetNativeFunctionCount(HANDLE hScript);

/* Evaluate one CommonJS-style module. The module source receives the usual
 * (module, exports, require) arguments. A successful name is cached for the
 * lifetime of the context, so a repeated call does not execute it again.
 * require() can return modules loaded by an earlier call, and a failed load
 * removes its incomplete entry. This is a standalone module service: it does
 * not resolve URLs, fetch files, or expose DOM/window objects. */
PSCRIPT_API int PScript_EvaluateModule(HANDLE hScript,
        const char *module_name, int module_name_len,
        const char *source, int source_len);

/* Install or clear the optional synchronous source provider. Pass both
 * callbacks as NULL to clear it. This is a host policy hook only: it does
 * not perform URL resolution, file I/O, network access, or browser binding. */
PSCRIPT_API int PScript_SetModuleSourceProvider(HANDLE hScript,
        PScriptModuleSourceFn source_fn,
        PScriptModuleSourceFreeFn free_fn, void *pw);

/* Ask the installed provider for a root module by name. Dependencies may
 * call require(), which consults the same provider on demand. A cached name
 * does not invoke the provider or execute source again. */
PSCRIPT_API int PScript_LoadModule(HANDLE hScript,
        const char *module_name, int module_name_len);

/* Drop all cached module exports. The context and its ordinary global state
 * remain usable. The operation returns PSCRIPT_OK or an error code. */
PSCRIPT_API int PScript_ClearModules(HANDLE hScript);

/* Borrowed strings valid until the next evaluation or destruction. The
 * caller must not free or modify them. */
PSCRIPT_API const char *PScript_GetResult(HANDLE hScript);
PSCRIPT_API const char *PScript_GetError(HANDLE hScript);

/* Diagnostic counters for embedders and device telemetry. */
PSCRIPT_API unsigned long PScript_GetMemoryUsed(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetPeakMemoryUsed(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetMemoryLimit(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetEvaluationCount(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetModuleCount(HANDLE hScript);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_SCRIPT_H */
