/* Explicit isolated bridge/store consumer fixture.
 * Only Debug, no live page, user database, file deletion or network access. */
#include "app_settings_services.h"
#include "app_debug.h"

#ifdef _DEBUG
#include "../positron_script/positron_script.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int app_services_debug_js(HANDLE session, const char *source)
{
    const char *value;

    if (PBrowser_ScriptSessionEvaluate(session, source, -1) != PSCRIPT_OK)
        return 0;
    value = PBrowser_ScriptSessionGetResult(session);
    return value != NULL && strcmp(value, "true") == 0;
}

static int app_services_debug_next(AppSettingsStore *store,
        AppSettingsResult *result)
{
    DWORD started;
    int rc;

    started = GetTickCount();
    do {
        rc = AppSettingsStore_Poll(store, result);
        if (rc != APP_SETTINGS_PENDING) return rc;
        Sleep(5);
    } while (GetTickCount() - started < 30000UL);
    return APP_SETTINGS_PENDING;
}

static int app_services_debug_close(AppSettingsStore **store)
{
    DWORD started;
    int rc;

    if (*store == NULL) return APP_SETTINGS_OK;
    if (AppSettingsStore_RequestClose(*store) != APP_SETTINGS_OK)
        return APP_SETTINGS_PLATFORM_FAILED;
    started = GetTickCount();
    do {
        rc = AppSettingsStore_TryDestroy(store);
        if (rc != APP_SETTINGS_PENDING) return rc;
        Sleep(5);
    } while (GetTickCount() - started < 30000UL);
    return APP_SETTINGS_PENDING;
}

static int app_services_debug_pending(AppSettingsServices *services)
{
    int index;
    int count;

    count = 0;
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index)
        if (services->pending[index].used) ++count;
    return count;
}

#define APP_SERVICES_CHECK(expression) do { \
    if (!(expression)) { failed_line = __LINE__; goto done; } \
} while (0)

int AppSettingsServices_DebugCheck(void)
{
    WCHAR dll_path[APP_SETTINGS_PATH_MAX];
    WCHAR *separator;
    DWORD length;
    AppSettingsStore *store;
    AppSettingsServices a;
    AppSettingsServices b;
    AppSettingsResult result;
    AppSettingsResult wrong;
    HANDLE session_a;
    HANDLE session_b;
    unsigned long delivered;
    int index;
    int phase;
    int failed_line;
    int passed;
    int cleanup;
    char log[256];

    store = NULL;
    session_a = session_b = NULL;
    AppSettingsServices_Init(&a);
    AppSettingsServices_Init(&b);
    phase = 1;
    failed_line = 0;
    passed = 0;
    length = GetModuleFileNameW(NULL, dll_path, APP_SETTINGS_PATH_MAX);
    APP_SERVICES_CHECK(length != 0 && length < APP_SETTINGS_PATH_MAX);
    separator = wcsrchr(dll_path, L'\\');
    APP_SERVICES_CHECK(separator != NULL && separator - dll_path <
            APP_SETTINGS_PATH_MAX - 32);
    wcscpy(separator + 1, L"positron_db.dll");
    APP_SERVICES_CHECK(AppSettingsStore_Create(dll_path, ":memory:", &store) ==
            APP_SETTINGS_OK && app_services_debug_next(store, &result) ==
            APP_SETTINGS_OK && result.request_id == 1 &&
            result.result == APP_SETTINGS_OK &&
            result.start_page == APP_SETTINGS_START_NEWTAB);
    /* Fresh bare sessions exercise the adapter; this is not a Core page or
     * bootstrap acceptance gate. Production must register after COMPLETE. */
    session_a = PBrowser_ScriptSessionCreate(1000);
    session_b = PBrowser_ScriptSessionCreate(1000);
    APP_SERVICES_CHECK(session_a != NULL && session_b != NULL);
    APP_SERVICES_CHECK(AppSettingsServices_Register(&a, store, session_a,
            APP_SETTINGS_PAGE_UNTRUSTED, 7, 9) == APP_SETTINGS_SERVICES_INVALID &&
            !a.bound && app_services_debug_js(session_a,
            "typeof PositronServices==='undefined'"));
    APP_SERVICES_CHECK(AppSettingsServices_Register(&a, store, session_a,
            APP_SETTINGS_PAGE_EMBEDDED_SETTINGS, 7, 9) == APP_SETTINGS_SERVICES_OK &&
            AppSettingsServices_Register(&b, store, session_b,
            APP_SETTINGS_PAGE_EMBEDDED_SETTINGS, 8, 9) == APP_SETTINGS_SERVICES_OK);
    APP_SERVICES_CHECK(app_services_debug_js(session_a,
            "var calls=[];PositronServices.request('settings.read',{},"
            "function(ok,v){calls.push([ok,v]);});calls.length===0"));
    APP_SERVICES_CHECK(app_services_debug_next(store, &result) == APP_SETTINGS_OK &&
            result.operation == APP_SETTINGS_LOAD &&
            result.result == APP_SETTINGS_OK && app_services_debug_pending(&a) == 1);
    wrong = result;
    wrong.generation++;
    APP_SERVICES_CHECK(AppSettingsServices_AcceptResult(&a, &wrong) ==
            APP_SETTINGS_SERVICES_IGNORED && app_services_debug_pending(&a) == 1 &&
            AppSettingsServices_AcceptResult(&b, &result) ==
            APP_SETTINGS_SERVICES_IGNORED);
    wrong = result;
    wrong.operation = APP_SETTINGS_SAVE;
    APP_SERVICES_CHECK(AppSettingsServices_AcceptResult(&a, &wrong) ==
            APP_SETTINGS_SERVICES_INVALID && app_services_debug_pending(&a) == 1);
    APP_SERVICES_CHECK(AppSettingsServices_AcceptResult(&a, &result) ==
            APP_SETTINGS_SERVICES_OK && AppSettingsServices_AcceptResult(&a, &result) ==
            APP_SETTINGS_SERVICES_IGNORED && app_services_debug_js(session_a,
            "calls.length===0"));
    APP_SERVICES_CHECK(AppSettingsServices_Pump(&a, &delivered) ==
            APP_SETTINGS_SERVICES_OK && delivered == 1 &&
            app_services_debug_js(session_a, "calls.length===1&&calls[0][0]&&"
            "calls[0][1].startupPage==='positron://newtab'"));
    phase = 2;
    APP_SERVICES_CHECK(app_services_debug_js(session_a,
            "var bad=[['sql',{}],['settings.read',null],['settings.read',{sql:'SELECT 1'}],"
            "['settings.write',{startupPage:'positron://quit'}],"
            "['settings.write',{startupPage:'https://example.com/'}],"
            "['settings.write',{startupPage:'positron://newtab\\u0000suffix'}],"
            "['settings.write',{startupPage:'positron://welcome',extra:1}],"
            "['settings.write',{startupPage:0}]],denied=0;"
            "for(var i=0;i<bad.length;i++){try{PositronServices.request("
            "bad[i][0],bad[i][1],function(){});}catch(e){denied++;}}denied===bad.length"));
    APP_SERVICES_CHECK(app_services_debug_pending(&a) == 0 &&
            AppSettingsStore_Poll(store, &result) == APP_SETTINGS_PENDING);
    /* Unread worker completions count toward quota; independent of races. */
    APP_SERVICES_CHECK(app_services_debug_js(session_a,
            "for(var i=0;i<8;i++)PositronServices.request('settings.write',"
            "{startupPage:'positron://welcome'},function(ok,v){calls.push([ok,v]);});"
            "var full=false;try{PositronServices.request('settings.read',{},"
            "function(){});}catch(e){full=true;}full"));
    APP_SERVICES_CHECK(app_services_debug_pending(&a) == 8);
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        APP_SERVICES_CHECK(app_services_debug_next(store, &result) == APP_SETTINGS_OK &&
                result.operation == APP_SETTINGS_SAVE && result.result == APP_SETTINGS_OK &&
                result.start_page == APP_SETTINGS_START_WELCOME &&
                result.state_valid && !result.state.transaction_active &&
                AppSettingsServices_AcceptResult(&b, &result) == APP_SETTINGS_SERVICES_IGNORED &&
                AppSettingsServices_AcceptResult(&a, &result) == APP_SETTINGS_SERVICES_OK);
    }
    APP_SERVICES_CHECK(app_services_debug_js(session_a, "calls.length===1"));
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        APP_SERVICES_CHECK(AppSettingsServices_Pump(&a, &delivered) ==
                APP_SETTINGS_SERVICES_OK && delivered == 1);
    }
    APP_SERVICES_CHECK(app_services_debug_js(session_a,
            "calls.length===9&&calls.slice(1).every(function(v){return v[0]&&"
            "v[1].startupPage==='positron://welcome';})"));
    phase = 3;
    APP_SERVICES_CHECK(app_services_debug_js(session_a,
            "PositronServices.request('settings.write',{startupPage:'positron://controls'},"
            "function(){calls.push('stale');});true") &&
            AppSettingsServices_Revoke(&a) == APP_SETTINGS_SERVICES_OK);
    APP_SERVICES_CHECK(app_services_debug_next(store, &result) == APP_SETTINGS_OK &&
            result.result == APP_SETTINGS_OK && result.start_page == APP_SETTINGS_START_CONTROLS &&
            AppSettingsServices_AcceptResult(&a, &result) == APP_SETTINGS_SERVICES_IGNORED);
    delivered = 777;
    APP_SERVICES_CHECK(AppSettingsServices_Pump(&a, &delivered) ==
            APP_SETTINGS_SERVICES_IGNORED && delivered == 777 &&
            app_services_debug_js(session_a, "calls.length===9"));
    PBrowser_ScriptSessionDestroy(session_a);
    session_a = NULL;
    AppSettingsServices_Init(&a);
    session_a = PBrowser_ScriptSessionCreate(1000);
    APP_SERVICES_CHECK(session_a != NULL && AppSettingsServices_Register(&a, store,
            session_a, APP_SETTINGS_PAGE_EMBEDDED_SETTINGS, 7, 10) ==
            APP_SETTINGS_SERVICES_OK && AppSettingsServices_AcceptResult(&a, &result) ==
            APP_SETTINGS_SERVICES_IGNORED);
    APP_SERVICES_CHECK(app_services_debug_js(session_b,
            "var bc=0;PositronServices.request('settings.read',{},function(ok,v){"
            "if(ok&&v.startupPage==='positron://controls')bc++;});true") &&
            app_services_debug_next(store, &result) == APP_SETTINGS_OK &&
            AppSettingsServices_AcceptResult(&a, &result) == APP_SETTINGS_SERVICES_IGNORED &&
            AppSettingsServices_AcceptResult(&b, &result) == APP_SETTINGS_SERVICES_OK &&
            AppSettingsServices_Pump(&b, &delivered) == APP_SETTINGS_SERVICES_OK &&
            delivered == 1 && app_services_debug_js(session_b, "bc===1"));
    phase = 4;
    /* Synthetic presentation check, not DB fault-injection evidence. Root
     * SQL/path/message details must never reach the trusted page either. */
    APP_SERVICES_CHECK(app_services_debug_js(session_b,
            "var errorValue=null;PositronServices.request('settings.read',{},"
            "function(ok,v){if(!ok)errorValue=v;});true") &&
            app_services_debug_next(store, &result) == APP_SETTINGS_OK);
    result.result = APP_SETTINGS_DB_FAILED;
    result.error.category = PDB_ERROR_CATEGORY_FULL;
    APP_SERVICES_CHECK(AppSettingsServices_AcceptResult(&b, &result) ==
            APP_SETTINGS_SERVICES_OK && AppSettingsServices_Pump(&b, &delivered) ==
            APP_SETTINGS_SERVICES_OK && delivered == 1 &&
            app_services_debug_js(session_b,
            "JSON.stringify(errorValue)==='{\"error\":\"storageFull\"}'"));
    APP_SERVICES_CHECK(app_services_debug_js(session_a,
            "var thrown=0;PositronServices.request('settings.read',{},function(){"
            "thrown++;throw new Error('fixture');});true") &&
            app_services_debug_next(store, &result) == APP_SETTINGS_OK &&
            AppSettingsServices_AcceptResult(&a, &result) == APP_SETTINGS_SERVICES_OK &&
            AppSettingsServices_Pump(&a, &delivered) == APP_SETTINGS_SERVICES_BRIDGE_FAILED &&
            delivered == 1 && !a.active);
    delivered = 777;
    APP_SERVICES_CHECK(AppSettingsServices_Pump(&a, &delivered) ==
            APP_SETTINGS_SERVICES_IGNORED && delivered == 777 &&
            app_services_debug_js(session_a, "thrown===1"));
    passed = 1;
done:
    /* Records stay alive during revoke and engine/finalizer destruction.
     * Worker holds only store/POD, never these stack records or sessions. */
    cleanup = APP_SETTINGS_OK;
    if (a.bound && AppSettingsServices_Revoke(&a) != APP_SETTINGS_SERVICES_OK)
        cleanup = APP_SETTINGS_PLATFORM_FAILED;
    if (b.bound && AppSettingsServices_Revoke(&b) != APP_SETTINGS_SERVICES_OK)
        cleanup = APP_SETTINGS_PLATFORM_FAILED;
    if (session_a != NULL) PBrowser_ScriptSessionDestroy(session_a);
    if (session_b != NULL) PBrowser_ScriptSessionDestroy(session_b);
    if (app_services_debug_close(&store) != APP_SETTINGS_OK)
        cleanup = APP_SETTINGS_PLATFORM_FAILED;
    if (cleanup != APP_SETTINGS_OK) passed = 0;
    _snprintf(log, sizeof(log) - 1,
            "positron settings-services selftest %s phase=%d line=%d cleanup=%d "
            "production_enabled=0\r\n", passed ? "OK" : "FAILED", phase,
            failed_line, cleanup);
    log[sizeof(log) - 1] = '\0';
    AppDebug_Log(log);
    return passed ? 0 : 1;
}
#undef APP_SERVICES_CHECK
#endif
