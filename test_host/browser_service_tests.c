/* Public Browser/Script consumer only: service queue fixture and assertions. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "positron_browser.h"
#include "positron_script.h"

static char service_error[256];
typedef struct service_fixture {
    HANDLE session;
    PBrowserServiceToken tokens[64];
    unsigned long accepted;
    unsigned long cancelled;
    unsigned long tab;
    unsigned long generation;
    int refused;
    int reentry;
    int guard_ok;
    char params[128];
} service_fixture;

static int fixture_submit(void *pw, const PBrowserServiceRequest *request)
{
    service_fixture *f;
    unsigned long count;
    f = (service_fixture *) pw;
    if (request->size != sizeof(*request) || request->version != PBROWSER_SERVICE_VERSION ||
            request->tab_id != f->tab || request->page_generation != f->generation ||
            strcmp(request->method, "echo") != 0 || f->accepted >= 64) { return 1; }
    if (f->reentry) {
        count = 987;
        f->guard_ok = PBrowser_ScriptSessionCompleteService(f->session,
                &request->token, 1, "null", 4) == PBROWSER_ERROR_STATE &&
                PBrowser_ScriptSessionPumpServices(f->session, 1, &count) == PBROWSER_ERROR_STATE &&
                count == 987 && PBrowser_ScriptSessionRevokeServices(f->session) == PBROWSER_ERROR_STATE &&
                PBrowser_ScriptSessionEvaluate(f->session, "1", -1) == PSCRIPT_ERROR_ARGUMENT;
        PBrowser_ScriptSessionDestroy(f->session); /* required no-op */
    }
    if (f->refused) { return 1; }
    f->tokens[f->accepted++] = request->token;
    if (request->params_bytes < sizeof(f->params)) {
        memcpy(f->params, request->params_json, request->params_bytes);
        f->params[request->params_bytes] = '\0';
    }
    return 0;
}
static void fixture_cancel(void *pw, const PBrowserServiceToken *token)
{
    service_fixture *f;
    f = (service_fixture *) pw;
    if (token->bridge_id != 0 && token->request_id != 0) { f->cancelled++; }
    if (f->reentry && PBrowser_ScriptSessionRevokeServices(f->session) != PBROWSER_ERROR_STATE) {
        f->guard_ok = 0;
    }
    PBrowser_ScriptSessionDestroy(f->session); /* no-op during cancel */
}
static void fixture_options(PBrowserServiceOptions *o, service_fixture *f)
{
    static const char *METHODS[] = { "echo" };
    memset(o, 0, sizeof(*o));
    o->size = sizeof(*o);
    o->version = PBROWSER_SERVICE_VERSION;
    o->tab_id = f->tab;
    o->page_generation = f->generation;
    o->method_count = 1;
    o->methods = METHODS;
    o->submit = fixture_submit;
    o->cancel = fixture_cancel;
    o->pw = f;
}
static int fixture_js(HANDLE session, const char *source)
{
    const char *result;
    if (PBrowser_ScriptSessionEvaluate(session, source, -1) != PSCRIPT_OK) { return 0; }
    result = PBrowser_ScriptSessionGetResult(session);
    return result != NULL && strcmp(result, "true") == 0;
}
typedef struct service_wrong_thread {
    HANDLE session;
    PBrowserServiceToken token;
    int result;
} service_wrong_thread;
static DWORD WINAPI fixture_wrong_thread(LPVOID pw)
{
    service_wrong_thread *data;
    data = (service_wrong_thread *) pw;
    data->result = PBrowser_ScriptSessionCompleteService(data->session,
            &data->token, 1, "null", 4);
    return 0;
}
static int fixture_native_large(void *pw, const char *args, int bytes,
        char *out, int capacity, int *length)
{
    (void) pw; (void) args; (void) bytes;
    if (capacity < 303) { return 1; }
    out[0] = '"';
    memset(out + 1, 'x', 300);
    out[301] = '"';
    out[302] = '\0';
    *length = 302;
    return 0;
}
static int fixture_finalized(void *pw, const char *args, int bytes,
        char *out, int capacity, int *length)
{
    unsigned long *count;
    count = (unsigned long *) pw;
    if (bytes != 6 || memcmp(args, "[true]", 6) != 0 || capacity < 5) { return 1; }
    (*count)++;
    memcpy(out, "null", 5); *length = 4;
    return 0;
}

#define SERVICE_CHECK(expr, message) do { if (!(expr)) { \
    _snprintf(service_error, sizeof(service_error) - 1, "%s (line %d)", message, __LINE__); \
    goto done; } } while (0)

BOOL test1341_browser_service_contract(void)
{
    static const char *BAD_JSON[] = {
        "true false", "01", "[1,]", "{\"a\":}", "\"\\q\"", "\"\300\257\"", "null\0junk"
    };
    service_fixture a;
    service_fixture b;
    service_fixture c;
    PBrowserServiceOptions options;
    PBrowserScriptBootstrapOptions bootstrap;
    PBrowserServiceToken stale;
    PScriptJsonFunctionOptions native_options;
    service_wrong_thread wrong;
    HANDLE thread;
    HANDLE runtime;
    char *large;
    char nested[80];
    char native_name[32];
    unsigned long delivered;
    unsigned long before;
    unsigned long finalized;
    unsigned long i;
    int ok;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    thread = runtime = NULL;
    large = NULL;
    ok = 0;
    finalized = 0;
    service_error[0] = '\0';
    a.tab = 1; a.generation = 71; a.reentry = 1;
    b.tab = 2; b.generation = 71;
    a.session = PBrowser_ScriptSessionCreate(1000);
    b.session = PBrowser_ScriptSessionCreate(1000);
    SERVICE_CHECK(a.session != NULL && b.session != NULL, "session create");
    SERVICE_CHECK(fixture_js(a.session, "typeof PositronServices==='undefined'"), "default closed");
    delivered = 987;
    SERVICE_CHECK(PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_ERROR_STATE &&
            delivered == 987, "unregistered pump output unchanged");
    fixture_options(&options, &a);
    options.version++;
    SERVICE_CHECK(PBrowser_ScriptSessionRegisterServices(a.session, &options) == PBROWSER_ERROR_ARGUMENT &&
            fixture_js(a.session, "typeof PositronServices==='undefined'"), "invalid register no installation");
    options.version = PBROWSER_SERVICE_VERSION;
    SERVICE_CHECK(PBrowser_ScriptSessionRegisterServices(a.session, &options) == PBROWSER_OK, "register A");
    SERVICE_CHECK(PBrowser_ScriptSessionRegisterServices(a.session, &options) == PBROWSER_ERROR_STATE, "duplicate registration");
    fixture_options(&options, &b);
    SERVICE_CHECK(PBrowser_ScriptSessionRegisterServices(b.session, &options) == PBROWSER_OK, "register B");
    SERVICE_CHECK(fixture_js(a.session,
            "var calls=[];PositronServices.request('echo',{n:1},function(ok,v){calls.push([ok,v]);});calls.length===0"),
            "async request has no synchronous delivery");
    SERVICE_CHECK(a.accepted == 1 && a.guard_ok && strcmp(a.params, "{\"n\":1}") == 0, "submit copied parameters and reentry guard");
    wrong.session = a.session; wrong.token = a.tokens[0]; wrong.result = 999;
    thread = CreateThread(NULL, 0, fixture_wrong_thread, &wrong, 0, NULL);
    SERVICE_CHECK(thread != NULL && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0 &&
            wrong.result == PBROWSER_ERROR_STATE, "wrong-thread completion rejects");
    CloseHandle(thread); thread = NULL;
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(b.session, &a.tokens[0], 1, "null", 4) == PBROWSER_ERROR_NOT_FOUND,
            "cross-tab token rejects");
    for (i = 0; i < sizeof(BAD_JSON) / sizeof(BAD_JSON[0]); i++) {
        SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[0], 1, BAD_JSON[i],
                i == 6 ? 9 : (unsigned long) strlen(BAD_JSON[i])) == PBROWSER_ERROR_ARGUMENT, "invalid JSON rejects");
    }
    memset(nested, '[', 18); nested[18] = '0'; memset(nested + 19, ']', 18); nested[37] = '\0';
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[0], 1, nested, 37) == PBROWSER_ERROR_ARGUMENT,
            "JSON depth budget");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[0], 1, "{\"answer\":42}", 13) == PBROWSER_OK &&
            PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[0], 1, "null", 4) == PBROWSER_ERROR_NOT_FOUND &&
            fixture_js(a.session, "calls.length===0"), "settle copies but defers and rejects duplicate");
    /* JS cannot invoke delivery/clear outside the public owner pump. */
    SERVICE_CHECK(fixture_js(a.session, "__positronServicePump('clear');calls.length===0"), "private delivery gate");
    SERVICE_CHECK(PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_OK && delivered == 1 &&
            fixture_js(a.session, "calls.length===1&&calls[0][0]===true&&calls[0][1].answer===42"), "success delivery");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[0], 1, "null", 4) == PBROWSER_ERROR_NOT_FOUND,
            "delivered token stale");
    SERVICE_CHECK(fixture_js(a.session, "var denied=false;try{PositronServices.request('sql',{},function(){});}catch(e){denied=true;}denied"),
            "method allowlist");
    a.refused = 1; before = a.accepted;
    SERVICE_CHECK(fixture_js(a.session, "var refused=false;try{PositronServices.request('echo',{},function(){});}catch(e){refused=true;}refused") &&
            a.accepted == before, "queue refusal no pending");
    a.refused = 0;
    SERVICE_CHECK(fixture_js(a.session, "var limited=false;try{PositronServices.request('echo',Array(4097).join('x'),function(){});}catch(e){limited=true;}limited") &&
            a.accepted == before, "parameter byte budget");
    SERVICE_CHECK(fixture_js(a.session, "PositronServices.request('echo',null,function(ok,v){calls.push([ok,v]);});true"), "failure request");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[a.accepted - 1], 0,
            "{\"code\":\"denied\"}", 17) == PBROWSER_OK && PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_OK &&
            fixture_js(a.session, "calls[1][0]===false&&calls[1][1].code==='denied'"), "failure JSON callback");
    large = (char *) malloc(4098);
    SERVICE_CHECK(large != NULL, "large fixture allocate");
    large[0] = '"'; memset(large + 1, 'x', 4094); large[4095] = '"'; large[4096] = '\0';
    SERVICE_CHECK(fixture_js(a.session, "var length=0;PositronServices.request('echo',null,function(ok,v){length=v.length;});true"), "large result request");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[a.accepted - 1], 1, large, 4097) == PBROWSER_ERROR_LIMIT &&
            PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[a.accepted - 1], 1, large, 4096) == PBROWSER_OK,
            "result ceiling and full payload");
    memset(large, 'z', 4096); /* result must be independently copied */
    SERVICE_CHECK(PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_OK &&
            fixture_js(a.session, "length===4094"), "large delivery through Script Ex");
    before = a.accepted;
    SERVICE_CHECK(fixture_js(a.session, "for(var i=0;i<16;i++)PositronServices.request('echo',i,function(ok,v){calls.push(v);});"
            "var full=false;try{PositronServices.request('echo',99,function(){});}catch(e){full=true;}full"), "pending bound");
    SERVICE_CHECK(a.accepted == before + 16, "pending allocation bounded");
    for (i = 0; i < 16; i++) {
        SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[before + 15 - i], 1,
                i == 0 ? "15" : "0", i == 0 ? 2 : 1) == PBROWSER_OK, "queue completion");
    }
    delivered = 987;
    SERVICE_CHECK(PBrowser_ScriptSessionPumpServices(a.session, 0, &delivered) == PBROWSER_ERROR_ARGUMENT && delivered == 987,
            "invalid pump does not consume");
    SERVICE_CHECK(PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_OK && delivered == 1 &&
            fixture_js(a.session, "calls[2]===15"), "completion ordering and one-delivery limit");
    SERVICE_CHECK(PBrowser_ScriptSessionPumpServices(a.session, 16, &delivered) == PBROWSER_OK && delivered == 15,
            "remaining deliveries and capacity recovery");
    SERVICE_CHECK(fixture_js(a.session, "var thrown=0;PositronServices.request('echo',null,function(){thrown++;throw new Error('fixture');});true"),
            "throwing callback request");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(a.session, &a.tokens[a.accepted - 1], 1, "null", 4) == PBROWSER_OK &&
            PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_ERROR_STATE && delivered == 1 &&
            PBrowser_ScriptSessionPumpServices(a.session, 1, &delivered) == PBROWSER_OK && delivered == 0 &&
            fixture_js(a.session, "thrown===1"), "callback exception exactly one attempt");
    SERVICE_CHECK(fixture_js(a.session, "PositronServices.request('echo',null,function(){calls.push('wrong');});true"), "revoke waiting request");
    stale = a.tokens[a.accepted - 1];
    SERVICE_CHECK(PBrowser_ScriptSessionRevokeServices(a.session) == PBROWSER_OK && a.cancelled == 1 && a.guard_ok &&
            PBrowser_ScriptSessionRevokeServices(a.session) == PBROWSER_OK && a.cancelled == 1 &&
            PBrowser_ScriptSessionCompleteService(a.session, &stale, 1, "null", 4) == PBROWSER_ERROR_STATE,
            "revoke cancels exactly once and rejects late results");
    SERVICE_CHECK(fixture_js(b.session, "var bc=0;PositronServices.request('echo',null,function(){bc++;});true") &&
            PBrowser_ScriptSessionCompleteService(b.session, &b.tokens[0], 1, "null", 4) == PBROWSER_OK &&
            PBrowser_ScriptSessionPumpServices(b.session, 1, &delivered) == PBROWSER_OK && fixture_js(b.session, "bc===1"),
            "tab B survives A revoke");
    PBrowser_ScriptSessionDestroy(a.session); a.session = NULL;
    a.session = PBrowser_ScriptSessionCreate(1000);
    fixture_options(&options, &a);
    SERVICE_CHECK(a.session != NULL && PBrowser_ScriptSessionRegisterServices(a.session, &options) == PBROWSER_OK &&
            PBrowser_ScriptSessionCompleteService(a.session, &stale, 1, "null", 4) == PBROWSER_ERROR_NOT_FOUND,
            "destroyed token cannot alias new session");
    SERVICE_CHECK(fixture_js(a.session, "var closed=false;function __pcorePageTeardown(){try{PositronServices.request('echo',null,function(){});}catch(e){closed=true;}return 0;}"
            "PositronServices.request('echo',null,function(){});true") &&
            PBrowser_ScriptSessionDispatchPageTeardown(a.session) == PSCRIPT_OK &&
            a.cancelled == 2 && fixture_js(a.session, "closed"), "teardown revokes before lifecycle code");
    SERVICE_CHECK(PBrowser_ScriptSessionRegisterServices(a.session, &options) == PBROWSER_ERROR_STATE,
            "teardown cannot reauthorize");
    SERVICE_CHECK(fixture_js(b.session, "PositronServices.request('echo',null,function(){bc++;});"
            "PositronServices.request('echo',null,function(){bc++;});true"), "destroy requests");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(b.session, &b.tokens[1], 1, "null", 4) == PBROWSER_OK,
            "queued result before destroy");
    PBrowser_ScriptSessionDestroy(b.session); b.session = NULL;
    SERVICE_CHECK(b.cancelled == 1, "destroy cancels waiting, drops settled without JS");
    c.tab = 3; c.generation = 71;
    c.session = PBrowser_ScriptSessionCreate(1000);
    memset(&bootstrap, 0, sizeof(bootstrap));
    bootstrap.size = sizeof(bootstrap);
    bootstrap.version = PBROWSER_SCRIPT_BOOTSTRAP_VERSION;
    bootstrap.generation = c.generation;
    fixture_options(&options, &c);
    SERVICE_CHECK(c.session != NULL &&
            PBrowser_ScriptSessionBootstrapBegin(c.session, &bootstrap) == PSCRIPT_OK &&
            PBrowser_ScriptSessionRegisterServices(c.session, &options) == PBROWSER_ERROR_STATE &&
            PBrowser_ScriptSessionBootstrapCancel(c.session, c.generation) == PSCRIPT_OK,
            "private initialization cannot install services");
    PBrowser_ScriptSessionDestroy(c.session); c.session = NULL;
    c.session = PBrowser_ScriptSessionCreate(1000);
    SERVICE_CHECK(c.session != NULL, "native quota session");
    for (i = 0; i < PSCRIPT_MAX_NATIVE_FUNCTIONS - 1; i++) {
        _snprintf(native_name, sizeof(native_name), "fixture%lu", i);
        SERVICE_CHECK(PBrowser_ScriptSessionRegisterJsonFunction(c.session, native_name,
                fixture_native_large, NULL) == PSCRIPT_OK, "fill native quota");
    }
    SERVICE_CHECK(PBrowser_ScriptSessionRegisterServices(c.session, &options) == PBROWSER_ERROR_STATE &&
            fixture_js(c.session, "typeof PositronServices==='undefined'") &&
            PBrowser_ScriptSessionRegisterServices(c.session, &options) == PBROWSER_ERROR_STATE,
            "partial native registration remains closed and cannot retry");
    PBrowser_ScriptSessionDestroy(c.session); c.session = NULL;
    c.session = PBrowser_ScriptSessionCreate(1000);
    SERVICE_CHECK(c.session != NULL &&
            PBrowser_ScriptSessionRegisterServices(c.session, &options) == PBROWSER_OK &&
            fixture_js(c.session, "PositronServices.request('echo',0,function(){while(true){}});"
                    "PositronServices.request('echo',1,function(){});true"), "timeout callback requests");
    SERVICE_CHECK(PBrowser_ScriptSessionCompleteService(c.session, &c.tokens[0], 1, "null", 4) == PBROWSER_OK &&
            PBrowser_ScriptSessionPumpServices(c.session, 1, &delivered) == PBROWSER_ERROR_STATE && delivered == 1,
            "timeout consumes one result without continuation");
    PBrowser_ScriptSessionDestroy(c.session); c.session = NULL;
    SERVICE_CHECK(c.cancelled == 1, "timeout teardown cancels remaining request");
    for (i = 0; i < 4; i++) {
        c.accepted = c.cancelled = 0;
        c.session = PBrowser_ScriptSessionCreate(1000);
        SERVICE_CHECK(c.session != NULL &&
                PBrowser_ScriptSessionRegisterServices(c.session, &options) == PBROWSER_OK &&
                PBrowser_ScriptSessionRegisterJsonFunction(c.session, "finalized",
                        fixture_finalized, &finalized) == PSCRIPT_OK &&
                fixture_js(c.session, "var finalizerObject={};Duktape.fin(finalizerObject,function(){"
                        "var denied=false;try{PositronServices.request('echo',null,function(){});}"
                        "catch(e){denied=true;}finalized(denied);});"
                        "PositronServices.request('echo',null,function(){});true"),
                "repeat create request");
        SERVICE_CHECK(finalized == i, "finalizer retained until destruction");
        SERVICE_CHECK(c.tokens[0].bridge_id != stale.bridge_id, "repeat token identity");
        stale = c.tokens[0];
        PBrowser_ScriptSessionDestroy(c.session); c.session = NULL;
        SERVICE_CHECK(c.cancelled == 1 && c.accepted == 1 && finalized == i + 1,
                "repeat teardown and engine finalizer cannot resubmit");
    }
    runtime = PScript_Create(1000);
    SERVICE_CHECK(runtime != NULL && PScript_RegisterGlobalJsonFunction(runtime, "large", -1,
            fixture_native_large, NULL) == PSCRIPT_OK && PScript_Evaluate(runtime, "large()", -1) != PSCRIPT_OK,
            "old native result capacity unchanged");
    native_options.size = sizeof(native_options); native_options.version = PSCRIPT_JSON_FUNCTION_VERSION;
    native_options.result_capacity = 512;
    SERVICE_CHECK(PScript_RegisterGlobalJsonFunctionEx(runtime, "large", -1, fixture_native_large, NULL,
            &native_options) == PSCRIPT_OK && PScript_Evaluate(runtime, "large().length===300", -1) == PSCRIPT_OK &&
            strcmp(PScript_GetResult(runtime), "true") == 0, "Script additive capacity");
    native_options.version++;
    SERVICE_CHECK(PScript_RegisterGlobalJsonFunctionEx(runtime, "large", -1, fixture_native_large, NULL,
            &native_options) == PSCRIPT_ERROR_ARGUMENT && PScript_Evaluate(runtime, "large().length===300", -1) == PSCRIPT_OK &&
            strcmp(PScript_GetResult(runtime), "true") == 0, "invalid Ex preserves existing registration");
    ok = 1;
done:
    if (thread != NULL) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    if (runtime != NULL) { PScript_Destroy(runtime); }
    free(large);
    if (a.session != NULL) { PBrowser_ScriptSessionDestroy(a.session); }
    if (b.session != NULL) { PBrowser_ScriptSessionDestroy(b.session); }
    if (c.session != NULL) { PBrowser_ScriptSessionDestroy(c.session); }
    service_error[sizeof(service_error) - 1] = '\0';
    return ok;
}
const char *test1341_browser_service_last_error(void) { return service_error; }
