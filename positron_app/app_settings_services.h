/* EXE business adapter for the public Browser service bridge. */
#ifndef POSITRON_APP_SETTINGS_SERVICES_H
#define POSITRON_APP_SETTINGS_SERVICES_H

#include "app_settings_store.h"
#include "../positron_browser/positron_browser.h"

/* Identity must come from the EXE's embedded-resource construction path,
 * never a URL/scheme, redirect, DOM attribute or author script. */
typedef enum AppSettingsPageIdentity {
    APP_SETTINGS_PAGE_UNTRUSTED = 0,
    APP_SETTINGS_PAGE_EMBEDDED_SETTINGS = 1
} AppSettingsPageIdentity;

enum {
    APP_SETTINGS_SERVICES_OK = 0,
    APP_SETTINGS_SERVICES_IGNORED = 1,
    APP_SETTINGS_SERVICES_INVALID = -1,
    APP_SETTINGS_SERVICES_BRIDGE_FAILED = -2
};

/* Never exceed the public bridge budget, including escaped URL/policy. */
#define APP_SETTINGS_SERVICES_PARAMS_MAX PBROWSER_SERVICE_JSON_MAX_BYTES

typedef struct AppSettingsServicePending {
    int used;
    unsigned long store_request_id;
    int operation;
    PBrowserServiceToken token;
} AppSettingsServicePending;

/* Stable, UI-owned storage, one per page. The caller must retain the entire
 * record until its borrowed session is Destroyed, even if Register fails or
 * Revoke returns STATE. Store must outlive the grant; no handles reach worker.
 * After Revoke, destroying the session, then Init permits slot reuse. */
typedef struct AppSettingsServices {
    DWORD owner_thread;
    int bound;
    int active;
    int in_call;
    int last_bridge_result;
    unsigned long tab_id;
    unsigned long generation;
    AppSettingsStore *store;
    HANDLE session;
    AppSettingsServicePending pending[APP_SETTINGS_QUEUE_MAX];
} AppSettingsServices;

void AppSettingsServices_Init(AppSettingsServices *services);
/* Register only AFTER bootstrap COMPLETE and BEFORE trusted author code.
 * Untrusted identity/invalid inputs do not mutate the record or session.
 * A DLL registration failure requires destroying the candidate session. */
int AppSettingsServices_Register(AppSettingsServices *services,
        AppSettingsStore *store, HANDLE session, AppSettingsPageIdentity identity,
        unsigned long tab_id, unsigned long generation);
/* UI dispatcher polls the shared store exactly once, then offers the copied
 * result to live page records. A nonmatching result is IGNORED, not consumed
 * from any other page. Caller also verifies the current page/service identity.
 * Complete only copies JSON; JS runs through the separate Pump boundary. */
int AppSettingsServices_AcceptResult(AppSettingsServices *services,
        const AppSettingsResult *result);
int AppSettingsServices_Pump(AppSettingsServices *services,
        unsigned long *out_delivered);
/* Deactivate BEFORE native revoke, remove token mappings even on failure.
 * Never cancel/reverse an already accepted SAVE: global settings writes drain,
 * but stale JS recipients are dropped. Revoke must precede session Destroy. */
int AppSettingsServices_Revoke(AppSettingsServices *services);

#ifdef _DEBUG
int AppSettingsServices_DebugCheck(void);
#endif
#endif
