/*
 * positron_app/app_script.h - private Browser ScriptSession adapter.
 *
 * The adapter owns only the EXE-side bridge between one Core document and
 * one Browser script session. DOM, history and lifecycle semantics remain in
 * the public Core/Browser DLLs; this layer supplies WM6 host callbacks and
 * coordinates the session lifetime with the window host.
 */

#ifndef POSITRON_APP_SCRIPT_H
#define POSITRON_APP_SCRIPT_H

#include <windows.h>

#include "positron_core.h"
#include "positron_browser.h"

#define APP_SCRIPT_URL_MAX       PBROWSER_HISTORY_URL_MAX
#define APP_SCRIPT_STATE_MAX     PBROWSER_HISTORY_STATE_MAX
#define APP_SCRIPT_TARGET_MAX    PBROWSER_SCRIPT_ANCHOR_TARGET_MAX
#define APP_SCRIPT_REL_MAX       PBROWSER_SCRIPT_ANCHOR_REL_MAX
#define APP_SCRIPT_CONTEXT_MAX   PBROWSER_SCRIPT_WINDOW_NAME_MAX

typedef struct AppScriptContext AppScriptContext;

typedef int (*AppScriptNavigateFn)(void *pw, AppScriptContext *context,
        const PBrowserScriptNavigationInfo *info, int *out_value);
typedef int (*AppScriptScrollFn)(void *pw, AppScriptContext *context,
        const PBrowserScriptScrollInfo *info, int *out_x, int *out_y);
typedef void (*AppScriptMutationFn)(void *pw, AppScriptContext *context);

typedef struct AppScriptHostCallbacks {
    unsigned long size;
    void *pw;
    AppScriptNavigateFn navigate;
    AppScriptScrollFn scroll;
    AppScriptMutationFn mutation;
} AppScriptHostCallbacks;

typedef struct AppScriptPendingNavigation {
    int valid;
    unsigned int kind;
    int delta;
    unsigned int target_kind;
    char url[APP_SCRIPT_URL_MAX];
    char state_json[APP_SCRIPT_STATE_MAX];
    char target[APP_SCRIPT_TARGET_MAX];
    char rel[APP_SCRIPT_REL_MAX];
    char context_name[APP_SCRIPT_CONTEXT_MAX];
} AppScriptPendingNavigation;

AppScriptContext *AppScript_Create(HANDLE document,
        const char *document_url, int history_length, int history_index,
        int history_can_commit, const char *history_state_json,
        int viewport_width, int viewport_height, int dpi,
        const AppScriptHostCallbacks *callbacks);
int AppScript_Execute(AppScriptContext *context, int allow_external,
        PCoreResolveUrlFn resolve, void *resolve_pw, int *out_executed,
        int *out_ignored, int *out_errors);
void AppScript_Destroy(AppScriptContext *context);

int AppScript_BeforeUnload(AppScriptContext *context, int *out_prevented);
int AppScript_PageLifecycleComplete(AppScriptContext *context);
int AppScript_PageTeardown(AppScriptContext *context);
int AppScript_RunTaskCheckpoint(AppScriptContext *context,
        unsigned long now_ms);
int AppScript_SetVisibility(AppScriptContext *context, int hidden);
int AppScript_SetFocus(AppScriptContext *context, int focused);
int AppScript_NotifyScroll(AppScriptContext *context, int scroll_x,
        int scroll_y);
int AppScript_NotifyResize(AppScriptContext *context, int viewport_width,
        int viewport_height, int dpi);
int AppScript_DispatchHashNavigation(AppScriptContext *context,
        const char *url, int history_length);

HANDLE AppScript_Document(AppScriptContext *context);
int AppScript_QueueNavigation(AppScriptContext *context,
        const PBrowserScriptNavigationInfo *info);
void AppScript_ClearPendingNavigation(AppScriptContext *context);
int AppScript_TakeNavigation(AppScriptContext *context,
        AppScriptPendingNavigation *out_navigation);
int AppScript_HasPendingNavigation(AppScriptContext *context);

#endif /* POSITRON_APP_SCRIPT_H */
