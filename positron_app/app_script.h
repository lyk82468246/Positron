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
typedef void (*AppScriptFormResetAppliedFn)(void *pw,
        AppScriptContext *context);
typedef int (*AppScriptGetContentEditableSelectionFn)(void *pw,
        AppScriptContext *context, const char *id, int *out_start,
        int *out_end, int *out_direction);
typedef int (*AppScriptSetContentEditableSelectionFn)(void *pw,
        AppScriptContext *context, const char *id, int start, int end,
        int direction);
typedef int (*AppScriptValidateFormSubmitFn)(void *pw,
        AppScriptContext *context, HANDLE document,
        const PBrowserScriptFormSubmitInfo *info, int *out_valid);
typedef int (*AppScriptFormSubmitFn)(void *pw, AppScriptContext *context,
        HANDLE document, const char *document_url,
        const PBrowserScriptFormSubmitInfo *info, char *out_target_url,
        int target_url_capacity);

typedef struct AppScriptHostCallbacks {
    unsigned long size;
    void *pw;
    AppScriptNavigateFn navigate;
    AppScriptScrollFn scroll;
    AppScriptMutationFn mutation;
    AppScriptFormResetAppliedFn form_reset_applied;
    AppScriptGetContentEditableSelectionFn get_contenteditable_selection;
    AppScriptSetContentEditableSelectionFn set_contenteditable_selection;
    AppScriptValidateFormSubmitFn validate_form_submit;
    AppScriptFormSubmitFn submit_form;
    /* Browser direct-submit callback; Core supplies the no-validation data. */
    AppScriptFormSubmitFn submit_form_direct;
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

/* Native controls use the Browser-owned keyboard/focus event adapters while
 * Core remains the owner of hit-testing and interaction state. */
int AppScript_DispatchKeyEvent(AppScriptContext *context, int x, int y,
        const char *event_type, const char *key, unsigned int key_code,
        unsigned int char_code, int repeat, int shift, int ctrl, int alt,
        int is_composing, int *out_default_allowed);
int AppScript_DispatchFocusEvent(AppScriptContext *context, int x, int y,
        const char *event_type, int bubbles, int cancelable);

/* Native EDIT transactions remain host-owned at the WM6 boundary while the
 * Browser session owns beforeinput/input/change ordering and dirty state. */
int AppScript_DispatchNativeEditBeforeInput(AppScriptContext *context,
        unsigned long target_token, const char *target_id, int x, int y,
        const char *input_type, const char *data, int cancelable,
        int is_composing, int *out_default_allowed);
int AppScript_DispatchNativeEditInput(AppScriptContext *context,
        unsigned long target_token, const char *target_id, int x, int y,
        const char *input_type, const char *data);
int AppScript_DispatchNativeEditBlur(AppScriptContext *context,
        unsigned long target_token, int x, int y);
int AppScript_NotifyNativeContentEditableSelection(AppScriptContext *context,
        const char *element_id, int start, int end, int direction,
        int trusted);
void AppScript_ResetNativeEditState(AppScriptContext *context);

/* Native SELECT keeps the WM6 control and Core selection in the host while
 * Browser owns the input/change ordering and single-select dropdown
 * candidate transaction. */
int AppScript_DispatchNativeSelectCommit(AppScriptContext *context,
        unsigned long target_token, int x, int y, int multiple,
        int selected_index, int selected_count);
int AppScript_DispatchNativeSelectInteraction(AppScriptContext *context,
        unsigned long target_token, int x, int y, int multiple,
        int selected_index, int selected_count, int phase,
        int *out_should_commit);
int AppScript_DispatchNativeSelectFocus(AppScriptContext *context,
        unsigned long target_token, int x, int y, int focused);
int AppScript_DispatchNativeSelectKey(AppScriptContext *context,
        unsigned long target_token, int x, int y, const char *event_type,
        const char *key, unsigned int key_code, unsigned int char_code,
        int repeat, int shift, int ctrl, int alt, int is_composing,
        int *out_default_allowed);
void AppScript_ResetNativeSelectState(AppScriptContext *context);

/* Native checkbox/radio activation keeps Core's checked state in the host
 * while Browser owns trusted click cancellation and input/change ordering. */
int AppScript_DispatchNativeToggle(AppScriptContext *context,
        unsigned long target_token, int x, int y, int phase, int kind,
        int disabled, int selected_before, int selected_after,
        int *out_default_allowed);
void AppScript_ResetNativeToggleState(AppScriptContext *context);

/* Native button activation uses Browser's bounded click/default transaction;
 * the host supplies the hit-tested Core point. After validation and an
 * accepted submit event, the app's form callback may schedule the supported
 * URL-encoded GET default; reset applies Core state only after acceptance. */
int AppScript_DispatchNativeButton(AppScriptContext *context,
        unsigned long target_token, int x, int y, int phase, int kind,
        int disabled, int validation_valid, int *out_default_allowed);
void AppScript_ResetNativeButtonState(AppScriptContext *context);

HANDLE AppScript_Document(AppScriptContext *context);
int AppScript_QueueNavigation(AppScriptContext *context,
        const PBrowserScriptNavigationInfo *info);
void AppScript_ClearPendingNavigation(AppScriptContext *context);
int AppScript_TakeNavigation(AppScriptContext *context,
        AppScriptPendingNavigation *out_navigation);
int AppScript_HasPendingNavigation(AppScriptContext *context);

#endif /* POSITRON_APP_SCRIPT_H */
