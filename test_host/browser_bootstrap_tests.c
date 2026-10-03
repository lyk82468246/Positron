/* Public Browser bootstrap contract fixtures. No product implementation. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "positron_browser.h"
#include "positron_script.h"

typedef struct bootstrap_probe {
    HANDLE session;
    unsigned long reads;
    int guard_failed;
} bootstrap_probe;

static void bootstrap_info_init(PBrowserScriptBootstrapInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
}

static int bootstrap_has_element(void *pw, const char *id)
{
    bootstrap_probe *probe;
    PBrowserScriptBootstrapInfo info;

    probe = (bootstrap_probe *) pw;
    probe->reads++;
    bootstrap_info_init(&info);
    if (PBrowser_ScriptSessionBootstrapGetState(probe->session, &info) !=
            PSCRIPT_ERROR_ARGUMENT ||
            PBrowser_ScriptSessionBootstrapCancel(probe->session, 1) !=
            PSCRIPT_ERROR_ARGUMENT ||
            PBrowser_ScriptSessionRuntime(probe->session) != NULL ||
            PBrowser_ScriptSessionEvaluate(probe->session, "1", -1) !=
            PSCRIPT_ERROR_ARGUMENT) {
        probe->guard_failed = 1;
    }
    /* A destruction attempt from an active callback must not free the context. */
    PBrowser_ScriptSessionDestroy(probe->session);
    return id != NULL && strcmp(id, "body") == 0;
}

static int bootstrap_get_text(void *pw, const char *id, char *out,
        int capacity, int *length)
{
    (void) pw;
    (void) id;
    if (length == NULL || (out != NULL && capacity < 1)) {
        return -1;
    }
    *length = 0;
    if (out != NULL) {
        out[0] = '\0';
    }
    return 0;
}

static int bootstrap_pending_guard(HANDLE session)
{
    return PBrowser_ScriptSessionRuntime(session) == NULL &&
            PBrowser_ScriptSessionGetResult(session) == NULL &&
            PBrowser_ScriptSessionGetError(session) == NULL &&
            PBrowser_ScriptSessionMemoryUsed(session) == 0 &&
            PBrowser_ScriptSessionNativeFunctionCount(session) == 0 &&
            PBrowser_ScriptSessionEvaluate(session, "this.polluted=1", -1) ==
            PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionSetGlobalString(session, "polluted", "yes") ==
            PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionCallGlobalJson(session, "f", "[]") ==
            PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionUnregisterDomReadCallbacks(session) ==
            PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionSetCurrentScriptIndex(session, 0) ==
            PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionRunTaskCheckpoint(session, 0, 0, 0, 0,
                    PBROWSER_SCRIPT_PUMP_ALL) == PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionDispatchPageLifecycle(session, "complete") ==
            PSCRIPT_ERROR_ARGUMENT &&
            PBrowser_ScriptSessionEvaluateBootstrap(session) ==
            PSCRIPT_ERROR_ARGUMENT;
}

static DWORD WINAPI bootstrap_wrong_thread(void *pw)
{
    HANDLE session;
    PBrowserScriptBootstrapInfo info;

    session = (HANDLE) pw;
    bootstrap_info_init(&info);
    if (PBrowser_ScriptSessionBootstrapGetState(session, &info) !=
            PSCRIPT_ERROR_ARGUMENT ||
            PBrowser_ScriptSessionBootstrapStep(session, 1, &info) !=
            PSCRIPT_ERROR_ARGUMENT ||
            PBrowser_ScriptSessionBootstrapCancel(session, 1) !=
            PSCRIPT_ERROR_ARGUMENT) {
        return 1;
    }
    PBrowser_ScriptSessionDestroy(session);
    return 0;
}

BOOL test1338_browser_bootstrap_contract(char *error, int error_capacity)
{
    static const char CHECK[] =
        "window===this&&document.URL==='https://example.com/a/index.html'"
        "&&location.pathname==='/a/index.html'&&history.length===1"
        "&&history.state===null&&typeof this.polluted==='undefined'";
    PBrowserScriptBootstrapOptions options;
    PBrowserScriptBootstrapInfo info;
    PBrowserScriptBootstrapInfo guard;
    PBrowserScriptDomReadCallbacks callbacks;
    bootstrap_probe probe;
    HANDLE session;
    HANDLE old;
    HANDLE thread;
    DWORD thread_result;
    unsigned long boundary;
    unsigned long steps;
    unsigned long completed;
    int ok;
    int result;
    int i;
    char name[32];
    char pressure[4097];
    const char *phase;

    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
    options.generation = 1;
    session = NULL;
    old = PBrowser_ScriptSessionCreate(8000UL);
    phase = "old-page";
    ok = old != NULL && PBrowser_ScriptSessionEvaluateBootstrap(old) == PSCRIPT_OK &&
            PBrowser_ScriptSessionEvaluate(old, "var keep=7;", -1) == PSCRIPT_OK;
    /* Cancel at every idle boundary, using real programs, not simulated slots. */
    for (boundary = 0; ok && boundary < PBROWSER_SCRIPT_BOOTSTRAP_MAX_STEPS; boundary++) {
        phase = "cancel-boundary";
        session = PBrowser_ScriptSessionCreate(8000UL);
        bootstrap_info_init(&info);
        ok = session != NULL && PBrowser_ScriptSessionBootstrapBegin(session,
                &options) == PSCRIPT_OK;
        for (steps = 0; ok && steps < boundary; steps++) {
            ok = PBrowser_ScriptSessionBootstrapStep(session, 1, &info) ==
                    PSCRIPT_OK;
            if (info.state == PBROWSER_BOOTSTRAP_COMPLETE) {
                break;
            }
        }
        if (ok && info.state == PBROWSER_BOOTSTRAP_COMPLETE) {
            PBrowser_ScriptSessionDestroy(session);
            session = NULL;
            break;
        }
        ok = ok && bootstrap_pending_guard(session) &&
                PBrowser_ScriptSessionBootstrapCancel(session, 2) ==
                PSCRIPT_ERROR_ARGUMENT &&
                PBrowser_ScriptSessionBootstrapCancel(session, 1) == PSCRIPT_OK &&
                PBrowser_ScriptSessionBootstrapCancel(session, 1) == PSCRIPT_OK &&
                PBrowser_ScriptSessionBootstrapGetState(session, &info) == PSCRIPT_OK &&
                info.state == PBROWSER_BOOTSTRAP_CANCELLED &&
                bootstrap_pending_guard(session) &&
                PBrowser_ScriptSessionBootstrapBegin(session, &options) ==
                PSCRIPT_ERROR_ARGUMENT;
        PBrowser_ScriptSessionDestroy(session);
        session = NULL;
    }
    if (ok) {
        phase = "guard-and-complete";
        session = PBrowser_ScriptSessionCreate(8000UL);
        memset(&probe, 0, sizeof(probe));
        probe.session = session;
        memset(&callbacks, 0, sizeof(callbacks));
        callbacks.size = sizeof(callbacks);
        callbacks.pw = &probe;
        callbacks.has_element = bootstrap_has_element;
        callbacks.get_text = bootstrap_get_text;
        bootstrap_info_init(&info);
        options.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION + 1;
        ok = session != NULL && PBrowser_ScriptSessionBootstrapBegin(session,
                &options) == PSCRIPT_ERROR_ARGUMENT;
        options.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
        options.size = sizeof(options) - 1;
        ok = ok && PBrowser_ScriptSessionBootstrapBegin(session, &options) ==
                PSCRIPT_ERROR_ARGUMENT;
        options.size = sizeof(options);
        ok = ok && PBrowser_ScriptSessionBootstrapBegin(session, NULL) ==
                PSCRIPT_ERROR_ARGUMENT && PBrowser_ScriptSessionRegisterDomReadCallbacks(
                session, &callbacks) == PSCRIPT_OK &&
                PBrowser_ScriptSessionSetGlobalString(session, "__pcoreDocumentUrl",
                "https://example.com/a/index.html") == PSCRIPT_OK &&
                PBrowser_ScriptSessionSetGlobalNumber(session, "__pcoreHistoryLength",
                1.0) == PSCRIPT_OK && PBrowser_ScriptSessionBootstrapBegin(session,
                &options) == PSCRIPT_OK &&
                PBrowser_ScriptSessionBootstrapBegin(session, &options) ==
                PSCRIPT_ERROR_ARGUMENT;
        memset(&guard, 0x5a, sizeof(guard));
        guard.size = sizeof(guard);
        guard.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION + 1;
        memcpy(&info, &guard, sizeof(info));
        ok = ok && PBrowser_ScriptSessionBootstrapStep(session, 1, &info) ==
                PSCRIPT_ERROR_ARGUMENT && memcmp(&info, &guard, sizeof(info)) == 0;
        bootstrap_info_init(&info);
        memcpy(&guard, &info, sizeof(info));
        ok = ok && PBrowser_ScriptSessionBootstrapStep(session, 2, &info) ==
                PSCRIPT_ERROR_ARGUMENT && memcmp(&info, &guard, sizeof(info)) == 0;
        if (ok) {
            thread = CreateThread(NULL, 0, bootstrap_wrong_thread, session, 0, NULL);
            if (thread == NULL) {
                ok = 0;
            } else {
                ok = WaitForSingleObject(thread, 10000UL) == WAIT_OBJECT_0 &&
                        GetExitCodeThread(thread, &thread_result) && thread_result == 0;
                CloseHandle(thread);
            }
        }
        completed = 0;
        for (steps = 0; ok && steps < PBROWSER_SCRIPT_BOOTSTRAP_MAX_STEPS; steps++) {
            ok = bootstrap_pending_guard(session) &&
                    PBrowser_ScriptSessionBootstrapStep(session, 1, &info) == PSCRIPT_OK &&
                    info.completed_stages == completed + 1 &&
                    info.max_step_ms <= info.active_ms;
            completed = info.completed_stages;
            if (info.state == PBROWSER_BOOTSTRAP_COMPLETE) {
                break;
            }
        }
        ok = ok && info.state == PBROWSER_BOOTSTRAP_COMPLETE && probe.reads > 0 &&
                !probe.guard_failed && PBrowser_ScriptSessionRuntime(session) != NULL &&
                PBrowser_ScriptSessionBootstrapStep(session, 1, &info) == PSCRIPT_OK &&
                info.completed_stages == completed &&
                PBrowser_ScriptSessionBootstrapCancel(session, 1) == PSCRIPT_ERROR_ARGUMENT &&
                PBrowser_ScriptSessionEvaluate(session, CHECK, -1) == PSCRIPT_OK &&
                strcmp(PBrowser_ScriptSessionGetResult(session), "true") == 0;
        PBrowser_ScriptSessionDestroy(session);
        session = NULL;
    }
    if (ok) {
        phase = "timeout-failure";
        session = PBrowser_ScriptSessionCreate(1UL);
        bootstrap_info_init(&info);
        ok = session != NULL && PBrowser_ScriptSessionBootstrapBegin(session,
                &options) == PSCRIPT_OK;
        result = PSCRIPT_OK;
        for (steps = 0; ok && result == PSCRIPT_OK &&
                steps < PBROWSER_SCRIPT_BOOTSTRAP_MAX_STEPS; steps++) {
            result = PBrowser_ScriptSessionBootstrapStep(session, 1, &info);
            if (info.state == PBROWSER_BOOTSTRAP_COMPLETE) {
                break;
            }
        }
        ok = ok && result == PSCRIPT_ERROR_TIMEOUT &&
                info.state == PBROWSER_BOOTSTRAP_FAILED && bootstrap_pending_guard(session) &&
                PBrowser_ScriptSessionBootstrapStep(session, 1, &info) == result;
        PBrowser_ScriptSessionDestroy(session);
        session = NULL;
    }
    if (ok) {
        phase = "heap-failure";
        session = PBrowser_ScriptSessionCreate(8000UL);
        memset(pressure, 'a', sizeof(pressure) - 1);
        pressure[sizeof(pressure) - 1] = '\0';
        result = PSCRIPT_OK;
        /* Keep global setters below exhaustion (they are not protected eval).
         * The protected product compiler, not a setup fatal, must hit the limit. */
        for (i = 0; session != NULL && i < 1024 && result == PSCRIPT_OK &&
                PBrowser_ScriptSessionMemoryUsed(session) + 256UL * 1024UL <
                PBrowser_ScriptSessionMemoryLimit(session); i++) {
            _snprintf(name, sizeof(name), "pressure%d", i);
            _snprintf(pressure, 16, "%08d", i);
            pressure[8] = 'a';
            result = PBrowser_ScriptSessionSetGlobalString(session, name, pressure);
        }
        bootstrap_info_init(&info);
        ok = session != NULL && result == PSCRIPT_OK && i > 0 && i < 1024 &&
                PBrowser_ScriptSessionBootstrapBegin(session, &options) == PSCRIPT_OK;
        result = PSCRIPT_OK;
        for (steps = 0; ok && result == PSCRIPT_OK &&
                steps < PBROWSER_SCRIPT_BOOTSTRAP_MAX_STEPS; steps++) {
            result = PBrowser_ScriptSessionBootstrapStep(session, 1, &info);
            if (info.state == PBROWSER_BOOTSTRAP_COMPLETE) {
                break;
            }
        }
        ok = ok && result == PSCRIPT_ERROR_MEMORY_LIMIT &&
                info.state == PBROWSER_BOOTSTRAP_FAILED && bootstrap_pending_guard(session) &&
                PBrowser_ScriptSessionBootstrapStep(session, 1, &info) == result;
        PBrowser_ScriptSessionDestroy(session);
        session = NULL;
    }
    phase = ok ? "old-page-retained" : phase;
    ok = ok && PBrowser_ScriptSessionEvaluate(old, "keep===7", -1) == PSCRIPT_OK &&
            strcmp(PBrowser_ScriptSessionGetResult(old), "true") == 0;
    PBrowser_ScriptSessionDestroy(session);
    PBrowser_ScriptSessionDestroy(old);
    if (!ok && error != NULL && error_capacity > 0) {
        _snprintf(error, error_capacity - 1, "Bootstrap contract failed: %s", phase);
        error[error_capacity - 1] = '\0';
    }
    return ok ? TRUE : FALSE;
}
