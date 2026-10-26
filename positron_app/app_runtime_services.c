#include <string.h>
#include "app_runtime_services.h"
#include "resource.h"
#ifdef _DEBUG
#include <stdio.h>
#include "app_debug.h"
#endif

static const char *const app_runtime_methods[] = { "storage.sql" };

#ifdef _DEBUG
static void app_runtime_services_debug(AppRuntimeServices *services,
        const char *phase, int result)
{
    char message[768];
    const char *error;

    error = services->session == NULL ? NULL :
            PBrowser_ScriptSessionGetError(services->session);
    _snprintf(message, sizeof(message) - 1,
            "runtime-service phase=%s tab=%lu gen=%lu result=%d error=%s\r\n",
            phase, services->tab_id, services->generation, result,
            error == NULL ? "" : error);
    message[sizeof(message) - 1] = 0;
    AppDebug_Log(message);
}
#endif

static int app_runtime_submit(void *parameter, const PBrowserServiceRequest *source)
{
    AppRuntimeServices *services;
    PRuntimeServiceRequest request;

    services = (AppRuntimeServices *) parameter;
    if (source == NULL || source->size < sizeof(*source) ||
            source->version != PBROWSER_SERVICE_VERSION ||
            source->tab_id != services->tab_id ||
            source->page_generation != services->generation) return 1;
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = PRUNTIME_VERSION;
    request.tab_id = source->tab_id;
    request.page_generation = source->page_generation;
    request.bridge_id = source->token.bridge_id;
    request.request_id = source->token.request_id;
    request.method = source->method;
    request.json = source->params_json;
    request.json_bytes = source->params_bytes;
    return AppRuntimePage_Submit(services->page, &request) == PRUNTIME_OK ? 0 : 1;
}

static void app_runtime_cancel(void *parameter, const PBrowserServiceToken *token)
{
    AppRuntimeServices *services;

    services = (AppRuntimeServices *) parameter;
    if (token != NULL) AppRuntimePage_Cancel(services->page,
            token->bridge_id, token->request_id);
}

int AppRuntimeServices_Begin(AppRuntimeServices *services, AppRuntimeApp *app,
        unsigned long tab_id, unsigned long generation)
{
    memset(services, 0, sizeof(*services));
    services->tab_id = tab_id;
    services->generation = generation;
    services->page = AppRuntimeApp_BeginPage(app, tab_id, generation);
    return services->page == NULL ? 1 : 0;
}

int AppRuntimeServices_Prepare(AppRuntimeServices *services, HANDLE session)
{
    PBrowserServiceOptions options;
    HINSTANCE instance;
    HRSRC resource;
    HGLOBAL loaded;
    const char *source;
    DWORD bytes;
    int ready;

    if (services->registered) return 1;
    ready = AppRuntimePage_Ready(services->page);
    if (ready <= 0) return ready;
    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.version = PBROWSER_SERVICE_VERSION;
    options.tab_id = services->tab_id;
    options.page_generation = services->generation;
    options.methods = app_runtime_methods;
    options.method_count = 1;
    options.submit = app_runtime_submit;
    options.cancel = app_runtime_cancel;
    options.pw = services;
    services->session = session;
    if (PBrowser_ScriptSessionRegisterServices(session, &options) != PBROWSER_OK) return -1;
    services->registered = 1;
    instance = GetModuleHandle(NULL);
    resource = FindResource(instance, MAKEINTRESOURCE(IDR_APP_RUNTIME_SDK), RT_RCDATA);
    if (resource == NULL) return -1;
    bytes = SizeofResource(instance, resource);
    loaded = LoadResource(instance, resource);
    source = loaded == NULL ? NULL : (const char *) LockResource(loaded);
    if (source == NULL || !bytes || bytes > 8192 ||
            PBrowser_ScriptSessionEvaluate(session, source, (int) bytes) != 0) return -1;
    return 1;
}

int AppRuntimeServices_Pump(AppRuntimeServices *services)
{
    PRuntimeServiceResult *result;
    PBrowserServiceToken token;
    unsigned long delivered;
    int rc;

    if (!services->registered) return 0;
    /* One bounded heap-owned copy per context, no allocation in idle ticks. */
    result = &services->result;
    result->size = sizeof(*result);
    result->version = PRUNTIME_VERSION;
    rc = AppRuntimePage_Take(services->page, result);
    if (rc == PRUNTIME_OK) {
        if (result->tab_id != services->tab_id ||
                result->page_generation != services->generation) rc = PRUNTIME_STATE;
        else {
            token.bridge_id = result->bridge_id;
            token.request_id = result->request_id;
            if (PBrowser_ScriptSessionCompleteService(services->session, &token,
                    result->result == PRUNTIME_OK, result->json,
                    result->json_bytes) != PBROWSER_OK) rc = PRUNTIME_STATE;
        }
    }
    if (rc != PRUNTIME_OK && rc != PRUNTIME_NOT_FOUND) {
#ifdef _DEBUG
        app_runtime_services_debug(services, "result", rc);
#endif
        return 1;
    }
    delivered = 0;
    rc = PBrowser_ScriptSessionPumpServices(services->session, 1, &delivered);
#ifdef _DEBUG
    if (rc != PBROWSER_OK) app_runtime_services_debug(services, "callback", rc);
#endif
    return rc == PBROWSER_OK ? 0 : 1;
}

void AppRuntimeServices_Revoke(AppRuntimeServices *services)
{
    if (services->registered) {
        services->registered = 0;
        (void) PBrowser_ScriptSessionRevokeServices(services->session);
    }
    services->session = NULL;
    AppRuntimePage_Release(&services->page);
}
