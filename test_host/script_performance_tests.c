/* Public Script timing and trusted bytecode cache contracts; no product code. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "positron_browser.h"
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

typedef struct bc_test_native_state {
    int delta;
    int calls;
} bc_test_native_state;

static int bc_test_native_fn(void *pw, const char *args, int length,
        char *out, int capacity, int *out_len)
{
    bc_test_native_state *state;
    int written;

    (void) args;
    (void) length;
    state = (bc_test_native_state *) pw;
    if (state == NULL || out == NULL || capacity < 16 || out_len == NULL) {
        return -1;
    }
    state->calls++;
    written = _snprintf(out, (size_t) capacity - 1, "%d", state->delta);
    if (written <= 0 || written >= capacity) {
        return -1;
    }
    out[written] = '\0';
    *out_len = written;
    return 0;
}

static int bc_build_padded_script(char *buf, int cap, const char *tag6,
        int repeat_funcs)
{
    int pos;
    int i;
    int n;

    pos = _snprintf(buf, (size_t) cap - 1,
            "/*bc-head-0123456789*/(function(){var acc=0;");
    if (pos <= 0 || pos >= cap) return -1;
    for (i = 0; i < repeat_funcs; i++) {
        n = _snprintf(buf + pos, (size_t) (cap - pos - 1),
                "function fn_%d(x){return (x+%d)*3-((x+%d)&7);}", i, i, i);
        if (n <= 0 || pos + n >= cap) return -1;
        pos += n;
    }
    n = _snprintf(buf + pos, (size_t) (cap - pos - 1),
            "acc=fn_0(5);var tag='%s';return tag+':'+acc;})();/*bc-tail-9876543210*/",
            tag6);
    if (n <= 0 || pos + n >= cap) return -1;
    pos += n;
    buf[pos] = '\0';
    return pos;
}

BOOL test1388_script_bytecode_cache_contract(void (*progress)(const char *),
        char *error, int error_capacity)
{
    char *src_a;
    char *src_b;
    char *big_src;
    HANDLE ctx_a;
    HANDLE ctx_b;
    HANDLE ctx_tight;
    HANDLE sess1;
    HANDLE sess2;
    bc_test_native_state cb_a;
    bc_test_native_state cb_b;
    PScriptPerformanceInfo perf_cold;
    PScriptPerformanceInfo perf_warm;
    PBrowserScriptBootstrapOptions bopt;
    PBrowserScriptBootstrapInfo binfo1;
    PBrowserScriptBootstrapInfo binfo2;
    int len_a;
    int len_b;
    int big_len;
    int i;
    int steps;
    int rc;
    char tag[16];
    char expected[32];
    char msg[256];
    const char *phase;
    BOOL ok;

    src_a = (char *) malloc(32768);
    src_b = (char *) malloc(32768);
    big_src = (char *) malloc(32768);
    ctx_a = NULL;
    ctx_b = NULL;
    ctx_tight = NULL;
    sess1 = NULL;
    sess2 = NULL;
    phase = "alloc";
    ok = (src_a != NULL && src_b != NULL && big_src != NULL) ? TRUE : FALSE;

    /* 1. Exact source match: identical length, identical 16-byte head and tail,
     * single-token difference in the middle must never reuse cached bytecode. */
    if (ok) {
        phase = "exact-source-match";
        len_a = bc_build_padded_script(src_a, 32768, "ALPHA1", 28);
        len_b = bc_build_padded_script(src_b, 32768, "ALPHA2", 28);
        ctx_a = PScript_CreateEx(4000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ctx_b = PScript_CreateEx(4000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ok = (len_a >= 1024 && len_a == len_b &&
                memcmp(src_a, src_b, 16) == 0 &&
                memcmp(src_a + len_a - 16, src_b + len_b - 16, 16) == 0 &&
                ctx_a != NULL && ctx_b != NULL &&
                PScript_Evaluate(ctx_a, src_a, len_a) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_a), "ALPHA1:10") == 0 &&
                PScript_Evaluate(ctx_a, src_b, len_b) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_a), "ALPHA2:10") == 0 &&
                PScript_Evaluate(ctx_b, src_b, len_b) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_b), "ALPHA2:10") == 0 &&
                PScript_Evaluate(ctx_b, src_a, len_a) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_b), "ALPHA1:10") == 0);
        PScript_Destroy(ctx_a);
        PScript_Destroy(ctx_b);
        ctx_a = NULL;
        ctx_b = NULL;
    }

    /* 2. Independent session globals, closures and native callbacks on cache hit. */
    if (ok) {
        int pos;
        int n;

        phase = "isolated-globals-closures-callbacks";
        pos = _snprintf(src_a, 32767,
                "var state=(typeof state==='number'?state:seed);"
                "function bump(k){state+=k+hostFn();return state;}");
        for (i = 0; pos > 0 && i < 32; i++) {
            n = _snprintf(src_a + pos, (size_t) (32767 - pos),
                    "function pad_%d(v){return (v+%d)^(%d);}", i, i, i + 1);
            if (n <= 0 || pos + n >= 32000) { pos = -1; break; }
            pos += n;
        }
        if (pos > 0) {
            n = _snprintf(src_a + pos, (size_t) (32767 - pos),
                    "bump(5);");
            pos = (n > 0 && pos + n < 32767) ? (pos + n) : -1;
        }
        cb_a.delta = 100;
        cb_a.calls = 0;
        cb_b.delta = 200;
        cb_b.calls = 0;
        ctx_a = PScript_CreateEx(4000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ctx_b = PScript_CreateEx(4000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ok = (pos >= 1024 && ctx_a != NULL && ctx_b != NULL &&
                PScript_SetGlobalNumber(ctx_a, "seed", -1, 10.0) == PSCRIPT_OK &&
                PScript_SetGlobalNumber(ctx_b, "seed", -1, 20.0) == PSCRIPT_OK &&
                PScript_RegisterGlobalJsonFunction(ctx_a, "hostFn", -1,
                        bc_test_native_fn, &cb_a) == PSCRIPT_OK &&
                PScript_RegisterGlobalJsonFunction(ctx_b, "hostFn", -1,
                        bc_test_native_fn, &cb_b) == PSCRIPT_OK &&
                /* ctx_a compiles & caches; ctx_b loads cached bytecode. */
                PScript_Evaluate(ctx_a, src_a, pos) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_a), "115") == 0 &&
                PScript_Evaluate(ctx_b, src_a, pos) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_b), "225") == 0 &&
                cb_a.calls == 1 && cb_b.calls == 1 &&
                /* Mutating & destroying ctx_a must not affect ctx_b. */
                PScript_CallGlobalJson(ctx_a, "bump", -1, "[1]", -1) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_a), "216") == 0);
        PScript_Destroy(ctx_a);
        ctx_a = NULL;
        ok = ok && PScript_CallGlobalJson(ctx_b, "bump", -1, "[2]", -1) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_b), "427") == 0 &&
                cb_a.calls == 2 && cb_b.calls == 2;
        PScript_Destroy(ctx_b);
        ctx_b = NULL;
    }

    /* 3. Tight heap fallback (dump skipped cleanly) and OOM non-poisoning. */
    if (ok) {
        phase = "tight-heap-fallback";
        len_a = bc_build_padded_script(src_a, 32768, "TIGHT1", 28);
        /* 140 KiB leaves < length+128 KiB free so dump is skipped cleanly. */
        ctx_tight = PScript_CreateEx(4000UL, 140UL * 1024UL);
        ok = (len_a >= 1024 && ctx_tight != NULL &&
                PScript_Evaluate(ctx_tight, src_a, len_a) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_tight), "TIGHT1:10") == 0 &&
                PScript_GetPeakMemoryUsed(ctx_tight) <=
                        PScript_GetMemoryLimit(ctx_tight));
        PScript_Destroy(ctx_tight);
        ctx_tight = NULL;
    }
    if (ok) {
        phase = "oom-non-poisoning";
        /* Populate cache for a 12 KiB script, then hit OOM in a tiny heap
         * while loading/running it, then verify a normal heap still succeeds. */
        big_len = bc_build_padded_script(big_src, 32768, "OOMCHK", 220);
        ctx_a = PScript_CreateEx(4000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ok = (big_len >= 10240 && ctx_a != NULL &&
                PScript_Evaluate(ctx_a, big_src, big_len) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_a), "OOMCHK:10") == 0);
        PScript_Destroy(ctx_a);
        ctx_a = NULL;
        if (ok) {
            ctx_tight = PScript_CreateEx(4000UL, 96UL * 1024UL);
            if (ctx_tight != NULL) {
                rc = PScript_Evaluate(ctx_tight, big_src, big_len);
                ok = (rc == PSCRIPT_ERROR_MEMORY_LIMIT || rc == PSCRIPT_OK);
                PScript_Destroy(ctx_tight);
                ctx_tight = NULL;
            }
        }
        if (ok) {
            ctx_b = PScript_CreateEx(4000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
            ok = (ctx_b != NULL &&
                    PScript_Evaluate(ctx_b, big_src, big_len) == PSCRIPT_OK &&
                    strcmp(PScript_GetResult(ctx_b), "OOMCHK:10") == 0);
            PScript_Destroy(ctx_b);
            ctx_b = NULL;
        }
    }

    /* 4. Bounded LRU eviction (> 48 distinct >= 1 KiB scripts). */
    if (ok) {
        phase = "lru-eviction";
        ctx_a = PScript_CreateEx(8000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ok = (ctx_a != NULL);
        for (i = 0; ok && i < 56; i++) {
            _snprintf(tag, sizeof(tag) - 1, "EV%04d", i);
            tag[sizeof(tag) - 1] = '\0';
            _snprintf(expected, sizeof(expected) - 1, "%s:10", tag);
            expected[sizeof(expected) - 1] = '\0';
            len_a = bc_build_padded_script(src_a, 32768, tag, 30);
            ok = (len_a >= 1024 &&
                    PScript_Evaluate(ctx_a, src_a, len_a) == PSCRIPT_OK &&
                    strcmp(PScript_GetResult(ctx_a), expected) == 0);
        }
        /* Re-evaluate evicted #0 and resident #55 on a fresh context. */
        PScript_Destroy(ctx_a);
        ctx_a = PScript_CreateEx(8000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        if (ok) {
            len_a = bc_build_padded_script(src_a, 32768, "EV0000", 30);
            len_b = bc_build_padded_script(src_b, 32768, "EV0055", 30);
            ok = (ctx_a != NULL &&
                    PScript_Evaluate(ctx_a, src_a, len_a) == PSCRIPT_OK &&
                    strcmp(PScript_GetResult(ctx_a), "EV0000:10") == 0 &&
                    PScript_Evaluate(ctx_a, src_b, len_b) == PSCRIPT_OK &&
                    strcmp(PScript_GetResult(ctx_a), "EV0055:10") == 0);
        }
        PScript_Destroy(ctx_a);
        ctx_a = NULL;
    }

    /* 5. Cold vs warm telemetry on a fresh ~24 KiB script and Browser bootstrap. */
    if (ok) {
        phase = "cold-vs-warm-telemetry";
        big_len = bc_build_padded_script(big_src, 32768, "BENCH9", 450);
        ctx_a = PScript_CreateEx(8000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        ctx_b = PScript_CreateEx(8000UL, PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES);
        memset(&perf_cold, 0, sizeof(perf_cold));
        perf_cold.size = sizeof(perf_cold);
        perf_cold.version = PSCRIPT_PERFORMANCE_VERSION;
        memset(&perf_warm, 0, sizeof(perf_warm));
        perf_warm.size = sizeof(perf_warm);
        perf_warm.version = PSCRIPT_PERFORMANCE_VERSION;
        ok = (big_len >= 20000 && ctx_a != NULL && ctx_b != NULL &&
                PScript_SetPerformanceEnabled(ctx_a, 1) == PSCRIPT_OK &&
                PScript_SetPerformanceEnabled(ctx_b, 1) == PSCRIPT_OK &&
                PScript_Evaluate(ctx_a, big_src, big_len) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_a), "BENCH9:10") == 0 &&
                PScript_GetPerformanceInfo(ctx_a, &perf_cold) == PSCRIPT_OK &&
                PScript_Evaluate(ctx_b, big_src, big_len) == PSCRIPT_OK &&
                strcmp(PScript_GetResult(ctx_b), "BENCH9:10") == 0 &&
                PScript_GetPerformanceInfo(ctx_b, &perf_warm) == PSCRIPT_OK);
        if (ok && progress != NULL) {
            _snprintf(msg, sizeof(msg) - 1,
                    "script-cache bytes=%d cold(total=%lu compile=%lu exec=%lu peak=%lu) "
                    "warm(total=%lu compile=%lu exec=%lu peak=%lu)",
                    big_len, perf_cold.last_total_ms, perf_cold.last_compile_ms,
                    perf_cold.last_execute_ms, PScript_GetPeakMemoryUsed(ctx_a),
                    perf_warm.last_total_ms, perf_warm.last_compile_ms,
                    perf_warm.last_execute_ms, PScript_GetPeakMemoryUsed(ctx_b));
            msg[sizeof(msg) - 1] = '\0';
            progress(msg);
        }
        PScript_Destroy(ctx_a);
        PScript_Destroy(ctx_b);
        ctx_a = NULL;
        ctx_b = NULL;
    }

    if (ok) {
        phase = "browser-bootstrap-warm-isolation";
        memset(&bopt, 0, sizeof(bopt));
        bopt.size = sizeof(bopt);
        bopt.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
        bopt.generation = 1;
        memset(&binfo1, 0, sizeof(binfo1));
        binfo1.size = sizeof(binfo1);
        binfo1.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
        memset(&binfo2, 0, sizeof(binfo2));
        binfo2.size = sizeof(binfo2);
        binfo2.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
        sess1 = PBrowser_ScriptSessionCreate(8000UL);
        sess2 = PBrowser_ScriptSessionCreate(8000UL);
        ok = (sess1 != NULL && sess2 != NULL &&
                PBrowser_ScriptSessionSetGlobalString(sess1,
                        "__pcoreDocumentUrl", "https://example.com/one") == PSCRIPT_OK &&
                PBrowser_ScriptSessionSetGlobalString(sess2,
                        "__pcoreDocumentUrl", "https://example.com/two") == PSCRIPT_OK &&
                PBrowser_ScriptSessionBootstrapBegin(sess1, &bopt) == PSCRIPT_OK);
        for (steps = 0; ok && steps < PBROWSER_SCRIPT_BOOTSTRAP_MAX_STEPS; steps++) {
            ok = (PBrowser_ScriptSessionBootstrapStep(sess1, 1, &binfo1) == PSCRIPT_OK);
            if (ok && binfo1.state == PBROWSER_BOOTSTRAP_COMPLETE) break;
        }
        ok = ok && binfo1.state == PBROWSER_BOOTSTRAP_COMPLETE &&
                PBrowser_ScriptSessionBootstrapBegin(sess2, &bopt) == PSCRIPT_OK;
        for (steps = 0; ok && steps < PBROWSER_SCRIPT_BOOTSTRAP_MAX_STEPS; steps++) {
            ok = (PBrowser_ScriptSessionBootstrapStep(sess2, 1, &binfo2) == PSCRIPT_OK);
            if (ok && binfo2.state == PBROWSER_BOOTSTRAP_COMPLETE) break;
        }
        ok = ok && binfo2.state == PBROWSER_BOOTSTRAP_COMPLETE &&
                PBrowser_ScriptSessionEvaluate(sess1,
                        "window.tag='S1';document.URL==='https://example.com/one'", -1) == PSCRIPT_OK &&
                strcmp(PBrowser_ScriptSessionGetResult(sess1), "true") == 0 &&
                PBrowser_ScriptSessionEvaluate(sess2,
                        "typeof window.tag==='undefined'&&document.URL==='https://example.com/two'", -1) == PSCRIPT_OK &&
                strcmp(PBrowser_ScriptSessionGetResult(sess2), "true") == 0;
        if (ok && progress != NULL) {
            _snprintf(msg, sizeof(msg) - 1,
                    "browser-bootstrap pass1(active=%lu max_step=%lu stages=%lu mem=%lu) "
                    "pass2(active=%lu max_step=%lu stages=%lu mem=%lu)",
                    binfo1.active_ms, binfo1.max_step_ms, binfo1.completed_stages,
                    PBrowser_ScriptSessionMemoryUsed(sess1),
                    binfo2.active_ms, binfo2.max_step_ms, binfo2.completed_stages,
                    PBrowser_ScriptSessionMemoryUsed(sess2));
            msg[sizeof(msg) - 1] = '\0';
            progress(msg);
        }
        PBrowser_ScriptSessionDestroy(sess1);
        PBrowser_ScriptSessionDestroy(sess2);
        sess1 = NULL;
        sess2 = NULL;
    }

    if (ctx_a != NULL) PScript_Destroy(ctx_a);
    if (ctx_b != NULL) PScript_Destroy(ctx_b);
    if (ctx_tight != NULL) PScript_Destroy(ctx_tight);
    if (sess1 != NULL) PBrowser_ScriptSessionDestroy(sess1);
    if (sess2 != NULL) PBrowser_ScriptSessionDestroy(sess2);
    if (src_a != NULL) free(src_a);
    if (src_b != NULL) free(src_b);
    if (big_src != NULL) free(big_src);
    if (!ok && error != NULL && error_capacity > 0) {
        _snprintf(error, (size_t) error_capacity - 1,
                "Bytecode cache contract failed: %s", phase);
        error[error_capacity - 1] = '\0';
    }
    return ok;
}
