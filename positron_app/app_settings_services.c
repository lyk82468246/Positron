#include <stdio.h>
#include <string.h>

#include "app_settings_services.h"
#include "../positron_json/positron_json.h"

static const char *const g_settings_methods[] = {
    "settings.read", "settings.write"
};

static int app_settings_services_owner(AppSettingsServices *services)
{
    return services != NULL && services->owner_thread == GetCurrentThreadId();
}

/* Quote one bounded application string, not a JSON parser. The JSON DLL
 * canonicalizes the completed object; no cJSON/internal handles are used. */
static int app_settings_services_json(const AppSettingsValues *values,
        char *output, unsigned long capacity)
{
    AppSettingsValues normalized;
    const unsigned char *cursor;
    const char *language;
    unsigned int byte;
    unsigned long length;

    if (capacity < 128 || AppSettingsValues_Normalize(values, &normalized) !=
            APP_SETTINGS_OK) return 0;
    language = normalized.language == APP_SETTINGS_LANGUAGE_CHINESE ? "zh-CN" :
            (normalized.language == APP_SETTINGS_LANGUAGE_ENGLISH ? "en-US" : "system");
    strcpy(output, "{\"startupPage\":\"");
    length = (unsigned long) strlen(output);
    cursor = (const unsigned char *) normalized.startup_url;
    while (*cursor) {
        byte = *cursor++;
        /* Reserve the entire fixed suffix before admitting any byte. A
         * write that cannot return its snapshot must fail BEFORE enqueue. */
        if (length + 6 + 128 >= capacity) return 0;
        if (byte < 0x20 || byte == '\\' || byte == '"') {
            _snprintf(output + length, capacity - length, "\\u%04x", byte);
            length += 6;
        } else output[length++] = (char) byte;
    }
    _snprintf(output + length, capacity - length,
            "\",\"language\":\"%s\",\"javascriptEnabled\":%s}", language,
            normalized.javascript_enabled ? "true" : "false");
    output[capacity - 1] = '\0';
    return 1;
}

static int app_settings_services_params(const PBrowserServiceRequest *request,
        int *operation, AppSettingsStartPage *page, AppSettingsValues *values,
        int *full_values)
{
    HANDLE root;
    const char *value;
    const char *url;
    const char *key;
    char input[APP_SETTINGS_SERVICES_PARAMS_MAX + 1];
    char *canonical;
    int index;
    int valid;

    if (request->method == NULL || request->params_json == NULL ||
            request->params_bytes == 0 ||
            request->params_bytes > APP_SETTINGS_SERVICES_PARAMS_MAX)
        return 0;
    if (memchr(request->params_json, '\0', (size_t) request->params_bytes) != NULL)
        return 0;
    memcpy(input, request->params_json, (size_t) request->params_bytes);
    input[request->params_bytes] = '\0';
    root = PJson_Parse(input);
    if (root == NULL) return 0;
    valid = 0;
    *page = APP_SETTINGS_START_NEWTAB;
    *full_values = 0;
    AppSettingsValues_Default(values);
    key = PJson_GetObjectKey(root, 0);
    if (PJson_GetType(root) == PJSON_TYPE_OBJECT) {
        if (strcmp(request->method, "settings.read") == 0 &&
                PJson_GetObjectSize(root) == 0) {
            *operation = APP_SETTINGS_LOAD;
            valid = 1;
        } else if (strcmp(request->method, "settings.write") == 0 &&
                PJson_GetObjectSize(root) == 1 &&
                key != NULL && strcmp(key, "startupPage") == 0) {
            value = PJson_GetString(root, "startupPage");
            if (value != NULL) {
                for (index = APP_SETTINGS_START_NEWTAB;
                        index <= APP_SETTINGS_START_CONTROLS; ++index) {
                    url = AppSettingsStore_StartPageUrl((AppSettingsStartPage) index);
                    if (strcmp(value, url) == 0) {
                        *operation = APP_SETTINGS_SAVE;
                        *page = (AppSettingsStartPage) index;
                        valid = 1;
                        break;
                    }
                }
            }
        } else if (strcmp(request->method, "settings.write") == 0 &&
                PJson_GetObjectSize(root) == 3) {
            value = PJson_GetString(root, "startupPage");
            url = PJson_GetString(root, "language");
            index = PJson_GetBool(root, "javascriptEnabled");
            if (value != NULL && strlen(value) < sizeof(values->startup_url) &&
                    url != NULL && index >= 0) {
                strcpy(values->startup_url, value);
                values->javascript_enabled = index;
                if (!strcmp(url, "system")) values->language = APP_SETTINGS_LANGUAGE_SYSTEM;
                else if (!strcmp(url, "en-US")) values->language = APP_SETTINGS_LANGUAGE_ENGLISH;
                else if (!strcmp(url, "zh-CN")) values->language = APP_SETTINGS_LANGUAGE_CHINESE;
                else values->language = (AppSettingsLanguage) -1;
                if (AppSettingsValues_Normalize(values, values) == APP_SETTINGS_OK) {
                    *operation = APP_SETTINGS_SAVE;
                    *full_values = 1;
                    valid = 1;
                }
            }
        }
    }
    /* Browser serializes the JS params value; its JSON must round-trip
     * exactly before using projected fields. This additionally rejects
     * strings cJSON projects through an embedded NUL, duplicate properties or
     * trailing bytes; do not implement a second JSON parser in the host. */
    if (valid) {
        canonical = PJson_Serialize(root);
        valid = canonical != NULL && strlen(canonical) == request->params_bytes &&
                memcmp(canonical, request->params_json,
                (size_t) request->params_bytes) == 0;
        PJson_FreeString(canonical);
    }
    if (valid && *full_values)
        valid = app_settings_services_json(values, input, sizeof(input));
    PJson_Free(root);
    return valid;
}

static int app_settings_services_submit(void *pw,
        const PBrowserServiceRequest *request)
{
    AppSettingsServices *services;
    AppSettingsStartPage page;
    AppSettingsValues values;
    unsigned long request_id;
    int operation;
    int index;
    int full_values;
    int rc;

    services = (AppSettingsServices *) pw;
    if (!app_settings_services_owner(services) || !services->active ||
            request == NULL || request->size < sizeof(*request) ||
            request->version != PBROWSER_SERVICE_VERSION ||
            request->tab_id != services->tab_id ||
            request->page_generation != services->generation ||
            request->token.bridge_id == 0 || request->token.request_id == 0)
        return 1;
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        if (!services->pending[index].used) break;
    }
    if (index == APP_SETTINGS_QUEUE_MAX ||
            !app_settings_services_params(request, &operation, &page,
                    &values, &full_values)) return 1;
    rc = full_values ? AppSettingsStore_SaveValues(services->store, &values,
            services->tab_id, services->generation, &request_id) :
            AppSettingsStore_Submit(services->store, operation, page,
            services->tab_id, services->generation, &request_id);
    if (rc != APP_SETTINGS_OK) return 1;
    services->pending[index].used = 1;
    services->pending[index].store_request_id = request_id;
    services->pending[index].operation = operation;
    services->pending[index].token = request->token;
    return 0;
}

static void app_settings_services_cancel(void *pw,
        const PBrowserServiceToken *token)
{
    AppSettingsServices *services;
    int index;

    services = (AppSettingsServices *) pw;
    if (!app_settings_services_owner(services) || token == NULL) return;
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        if (services->pending[index].used &&
                services->pending[index].token.bridge_id == token->bridge_id &&
                services->pending[index].token.request_id == token->request_id) {
            memset(&services->pending[index], 0, sizeof(services->pending[index]));
            break;
        }
    }
}

void AppSettingsServices_Init(AppSettingsServices *services)
{
    if (services == NULL) return;
    memset(services, 0, sizeof(*services));
    services->owner_thread = GetCurrentThreadId();
}

int AppSettingsServices_Register(AppSettingsServices *services,
        AppSettingsStore *store, HANDLE session, AppSettingsPageIdentity identity,
        unsigned long tab_id, unsigned long generation)
{
    PBrowserServiceOptions options;
    int rc;

    if (!app_settings_services_owner(services) || services->bound ||
            store == NULL || session == NULL || tab_id == 0 || generation == 0 ||
            identity != APP_SETTINGS_PAGE_EMBEDDED_SETTINGS)
        return APP_SETTINGS_SERVICES_INVALID;
    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.version = PBROWSER_SERVICE_VERSION;
    options.tab_id = tab_id;
    options.page_generation = generation;
    options.methods = g_settings_methods;
    options.method_count = sizeof(g_settings_methods) / sizeof(g_settings_methods[0]);
    options.submit = app_settings_services_submit;
    options.cancel = app_settings_services_cancel;
    options.pw = services;
    services->store = store;
    services->session = session;
    services->tab_id = tab_id;
    services->generation = generation;
    services->bound = 1;
    services->active = 1;
    services->in_call = 1;
    rc = PBrowser_ScriptSessionRegisterServices(session, &options);
    services->in_call = 0;
    services->last_bridge_result = rc;
    if (rc != PBROWSER_OK) {
        services->active = 0;
        return APP_SETTINGS_SERVICES_BRIDGE_FAILED;
    }
    return APP_SETTINGS_SERVICES_OK;
}

static const char *app_settings_services_error(const AppSettingsResult *result)
{
    if (result->result == APP_SETTINGS_BAD_DATA) return "invalidStoredSettings";
    if (result->result == APP_SETTINGS_DB_FAILED) {
        if (result->error.category == PDB_ERROR_CATEGORY_FULL) return "storageFull";
        if (result->error.category == PDB_ERROR_CATEGORY_BUSY) return "storageBusy";
    }
    return "storageUnavailable";
}

int AppSettingsServices_AcceptResult(AppSettingsServices *services,
        const AppSettingsResult *result)
{
    AppSettingsServicePending *pending;
    HANDLE root;
    char *json;
    char input[APP_SETTINGS_SERVICES_PARAMS_MAX + 1];
    int index;
    int rc;

    if (!app_settings_services_owner(services) || result == NULL || services->in_call)
        return APP_SETTINGS_SERVICES_INVALID;
    if (!services->active || result->tab_id != services->tab_id ||
            result->generation != services->generation)
        return APP_SETTINGS_SERVICES_IGNORED;
    pending = NULL;
    for (index = 0; index < APP_SETTINGS_QUEUE_MAX; ++index) {
        if (services->pending[index].used &&
                services->pending[index].store_request_id == result->request_id) {
            pending = &services->pending[index];
            break;
        }
    }
    if (pending == NULL) return APP_SETTINGS_SERVICES_IGNORED;
    if (pending->operation != result->operation)
        return APP_SETTINGS_SERVICES_INVALID;
    if (result->result == APP_SETTINGS_OK) {
        if (!app_settings_services_json(&result->values, input, sizeof(input)))
            return APP_SETTINGS_SERVICES_INVALID;
    } else {
        _snprintf(input, sizeof(input) - 1, "{\"error\":\"%s\"}",
                app_settings_services_error(result));
    }
    input[sizeof(input) - 1] = '\0';
    root = PJson_Parse(input);
    if (root == NULL) return APP_SETTINGS_SERVICES_BRIDGE_FAILED;
    json = PJson_Serialize(root);
    PJson_Free(root);
    if (json == NULL) return APP_SETTINGS_SERVICES_BRIDGE_FAILED;
    services->in_call = 1;
    rc = PBrowser_ScriptSessionCompleteService(services->session, &pending->token,
            result->result == APP_SETTINGS_OK, json, (unsigned long) strlen(json));
    PJson_FreeString(json);
    services->in_call = 0;
    services->last_bridge_result = rc;
    if (rc != PBROWSER_OK) {
        /* Caller must revoke/discard the damaged page. No automatic retry of
         * a result whose terminal JS state is unknown. */
        services->active = 0;
        return APP_SETTINGS_SERVICES_BRIDGE_FAILED;
    }
    memset(pending, 0, sizeof(*pending));
    return APP_SETTINGS_SERVICES_OK;
}

int AppSettingsServices_Pump(AppSettingsServices *services,
        unsigned long *out_delivered)
{
    int rc;

    if (!app_settings_services_owner(services) || out_delivered == NULL ||
            services->in_call) return APP_SETTINGS_SERVICES_INVALID;
    if (!services->active) return APP_SETTINGS_SERVICES_IGNORED;
    services->in_call = 1;
    rc = PBrowser_ScriptSessionPumpServices(services->session, 1, out_delivered);
    services->in_call = 0;
    services->last_bridge_result = rc;
    if (rc != PBROWSER_OK) {
        services->active = 0;
        return APP_SETTINGS_SERVICES_BRIDGE_FAILED;
    }
    return APP_SETTINGS_SERVICES_OK;
}

int AppSettingsServices_Revoke(AppSettingsServices *services)
{
    int rc;

    if (!app_settings_services_owner(services) || services->in_call)
        return APP_SETTINGS_SERVICES_INVALID;
    if (!services->bound) return APP_SETTINGS_SERVICES_IGNORED;
    services->active = 0;
    services->in_call = 1;
    rc = PBrowser_ScriptSessionRevokeServices(services->session);
    services->in_call = 0;
    services->last_bridge_result = rc;
    memset(services->pending, 0, sizeof(services->pending));
    return rc == PBROWSER_OK ? APP_SETTINGS_SERVICES_OK :
            APP_SETTINGS_SERVICES_BRIDGE_FAILED;
}
