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

#define PSCRIPT_ABI_VERSION 0x00010001UL
#define PSCRIPT_DEFAULT_BUDGET_MS 1000UL
#define PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES (512UL * 1024UL)
#define PSCRIPT_MAX_SOURCE_BYTES (64UL * 1024UL)

#define PSCRIPT_OK 0
#define PSCRIPT_ERROR_ARGUMENT (-1)
#define PSCRIPT_ERROR_SOURCE_TOO_LARGE (-2)
#define PSCRIPT_ERROR_EVALUATION (-3)
#define PSCRIPT_ERROR_TIMEOUT (-4)
#define PSCRIPT_ERROR_FATAL (-5)
#define PSCRIPT_ERROR_MEMORY_LIMIT (-6)

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

/* Borrowed strings valid until the next evaluation or destruction. The
 * caller must not free or modify them. */
PSCRIPT_API const char *PScript_GetResult(HANDLE hScript);
PSCRIPT_API const char *PScript_GetError(HANDLE hScript);

/* Diagnostic counters for embedders and device telemetry. */
PSCRIPT_API unsigned long PScript_GetMemoryUsed(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetPeakMemoryUsed(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetMemoryLimit(HANDLE hScript);
PSCRIPT_API unsigned long PScript_GetEvaluationCount(HANDLE hScript);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_SCRIPT_H */
