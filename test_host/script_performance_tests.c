/* Public Script timing contract only; no product implementation. */
#include <windows.h>
#include <string.h>
#include "positron_script.h"

static int timing_native(void *pw, const char *args, int length,
        char *out, int capacity, int *out_len)
{
    PScriptPerformanceInfo rejected;
    HANDLE script;

    script = *(HANDLE *) pw;
    memset(&rejected, 0, sizeof(rejected));
    rejected.size = sizeof(rejected);
    rejected.version = PSCRIPT_PERFORMANCE_VERSION;
    if (PScript_SetPerformanceEnabled(script, 0) != PSCRIPT_ERROR_ARGUMENT ||
            PScript_GetPerformanceInfo(script, &rejected) !=
            PSCRIPT_ERROR_ARGUMENT || args == NULL || length < 0 ||
            capacity < 5 || out_len == NULL) {
        return -1;
    }
    memcpy(out, "true", 5);
    *out_len = 4;
    return 0;
}

BOOL test1336_script_performance_contract(void)
{
    PScriptPerformanceInfo info;
    PScriptPerformanceInfo guard;
    HANDLE script;
    int i;
    int result;
    BOOL ok;

    ok = TRUE;
    script = NULL;
    for (i = 0; i < 3 && ok; i++) {
        script = PScript_CreateEx(2000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        info.version = PSCRIPT_PERFORMANCE_VERSION;
        memset(&guard, 0x5a, sizeof(guard));
        guard.size = sizeof(guard);
        guard.version = PSCRIPT_PERFORMANCE_VERSION + 1;
        memcpy(&info, &guard, sizeof(info));
        if (script == NULL || PScript_SetPerformanceEnabled(script, 2) !=
                PSCRIPT_ERROR_ARGUMENT ||
                PScript_GetPerformanceInfo(script, &info) !=
                PSCRIPT_ERROR_ARGUMENT || memcmp(&info, &guard,
                sizeof(info)) != 0 ||
                PScript_RegisterGlobalJsonFunction(script, "probe", -1,
                timing_native, &script) != PSCRIPT_OK ||
                PScript_SetPerformanceEnabled(script, 1) != PSCRIPT_OK ||
                PScript_Evaluate(script, "var g=this;function f(){return 7;}"
                "probe();this===g && (function(){'use strict';return this;})()"
                "===undefined;", -1) != PSCRIPT_OK ||
                strcmp(PScript_GetResult(script), "true") != 0) {
            ok = FALSE;
        }
        info.size = sizeof(info);
        info.version = PSCRIPT_PERFORMANCE_VERSION;
        if (ok && (PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                !info.enabled || info.evaluations != 1 ||
                info.last_result != PSCRIPT_OK || info.last_native_calls != 1 ||
                info.native_calls != 1 || info.last_compile_ms >
                info.last_total_ms || info.last_execute_ms > info.last_total_ms ||
                info.last_callback_ms > info.last_native_ms ||
                strcmp(info.max_callback_name, "probe") != 0)) {
            ok = FALSE;
        }
        if (ok && (PScript_Evaluate(script, "var = ;", -1) !=
                PSCRIPT_ERROR_EVALUATION ||
                PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                info.last_result != PSCRIPT_ERROR_EVALUATION ||
                info.last_execute_ms != 0 || info.last_native_calls != 0 ||
                PScript_CallGlobalJson(script, "f", -1, "[]", -1) !=
                PSCRIPT_OK || strcmp(PScript_GetResult(script), "7") != 0 ||
                PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                info.calls != 1 || info.last_call_result != PSCRIPT_OK)) {
            ok = FALSE;
        }
        if (ok) {
            result = PScript_Evaluate(script, "while(true){}", -1);
            if (result != PSCRIPT_ERROR_TIMEOUT ||
                    PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                    info.last_result != PSCRIPT_ERROR_TIMEOUT ||
                    info.max_sync_ms < info.last_total_ms ||
                    PScript_Evaluate(script, "f();", -1) != PSCRIPT_OK ||
                    strcmp(PScript_GetResult(script), "7") != 0) {
                ok = FALSE;
            }
        }
        if (ok) {
            result = PScript_Evaluate(script,
                    "var pressure=[];for(var i=0;i<50000;i++){"
                    "pressure.push('aaaaaaaaaaaaaaaaaaaaaaaaaaaa'+i);}", -1);
            if (result != PSCRIPT_ERROR_MEMORY_LIMIT ||
                    PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                    info.last_result != result ||
                    PScript_Evaluate(script, "pressure=null;f();", -1) !=
                    PSCRIPT_OK || PScript_CollectGarbage(script) != PSCRIPT_OK ||
                    PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                    info.gc_calls != 1 || info.total_gc_ms != info.last_gc_ms ||
                    PScript_GetPeakMemoryUsed(script) >
                    PScript_GetMemoryLimit(script)) {
                ok = FALSE;
            }
        }
        if (ok && (PScript_SetPerformanceEnabled(script, 0) != PSCRIPT_OK ||
                PScript_Evaluate(script, "this===g && f()===7;", -1) !=
                PSCRIPT_OK || strcmp(PScript_GetResult(script), "true") != 0 ||
                PScript_GetPerformanceInfo(script, &info) != PSCRIPT_OK ||
                info.enabled || info.evaluations || info.native_calls ||
                info.gc_calls)) {
            ok = FALSE;
        }
        PScript_Destroy(script);
        script = NULL;
    }
    PScript_Destroy(NULL);
    return ok;
}
