/*
 * positron_app/app_host.h - private WM6 host state and lifecycle boundary.
 *
 * This header is application-private.  It owns no reusable browser semantic;
 * Core and Browser remain the owners of document, history and navigation
 * behavior.  The host context only groups platform handles and the lifetime
 * state needed to connect those public APIs to one WM6 window.
 */

#ifndef POSITRON_APP_HOST_H
#define POSITRON_APP_HOST_H

#include <windows.h>
#include <aygshell.h>

#include "positron_browser.h"

#define APP_HOST_URL_MAX       1024
#define APP_HOST_FOCUS_ID_MAX  128
#define APP_HOST_FOCUS_MAX     24
#define APP_HOST_CONTENT_TYPE_MAX 256
#define APP_HOST_TITLE_MAX     256

typedef struct AppNavigationRequest AppNavigationRequest;
typedef struct AppNavigationResource AppNavigationResource;
typedef struct AppScriptContext AppScriptContext;

struct AppNavigationRequest {
    HWND hwnd;
    HANDLE candidate;
    HANDLE worker_thread;
    HANDLE resource_transaction;
    AppNavigationRequest *retired_next;
    unsigned long generation;
    int history_mode;
    int history_target;
    int resource_index;
    AppNavigationResource *resources;
    int resource_count;
    int resource_policy;
    unsigned int resource_role_mask;
    HANDLE document_candidate;
    HANDLE stylesheet_candidate;
    AppScriptContext *script_candidate;
    int worker_stage;
    int worker_resume_stage;
    int commit_stage;
    int image_scan_found;
    int image_scan_fetched;
    int resource_registration_failed;
    int worker_succeeded;
    int worker_failure_class;
    int worker_status_code;
    int method;
    char *body;
    int body_bytes;
    char content_type[APP_HOST_CONTENT_TYPE_MAX];
    char url[APP_HOST_URL_MAX];
    int ui_snapshot_valid;
    char committed_url[APP_HOST_URL_MAX];
    WCHAR committed_caption[APP_HOST_TITLE_MAX];
};

typedef struct AppHostContext {
    HWND window;
    HWND address;
    HWND page_window;
    HWND menu_bar;
    WNDPROC address_original_proc;
    SHACTIVATEINFO shell_activate;
    HINSTANCE instance;

    HANDLE document;
    HANDLE stylesheet;
    AppScriptContext *script;
    HANDLE history;
    /* Browser identity bound to the visible GET document; zero for POST. */
    unsigned long history_document_id;
    int core_initialized;
    int http_initialized;

    AppNavigationRequest *navigation_request;
    AppNavigationRequest *retired_navigation;
    LONG navigation_generation;
    int navigation_closing;

    int page_kind;
    int page_width;
    int page_height;
    int document_width;
    int document_height;
    int scroll_x;
    int scroll_y;
    int dpi;
    int focus_index;
    int focus_count;
    const char *focus_ids[APP_HOST_FOCUS_MAX];
    char internal_focus_ids[APP_HOST_FOCUS_MAX][APP_HOST_FOCUS_ID_MAX];
    char focus_id[APP_HOST_FOCUS_ID_MAX];
    char current_url[APP_HOST_URL_MAX];
} AppHostContext;

void AppHostContext_Init(AppHostContext *context);
void AppHostContext_SetInstance(AppHostContext *context, HINSTANCE instance);
void AppHostContext_SetWindow(AppHostContext *context, HWND window);
void AppHostContext_SetCoreInitialized(AppHostContext *context, int initialized);
void AppHostContext_SetHttpInitialized(AppHostContext *context, int initialized);
void AppHostContext_SetHistory(AppHostContext *context, HANDLE history);
void AppHostContext_SetAddress(AppHostContext *context, HWND address,
        WNDPROC original_proc);
void AppHostContext_SetPageWindow(AppHostContext *context, HWND page_window);
void AppHostContext_SetMenuBar(AppHostContext *context, HWND menu_bar);
int AppHostContext_ReplacePage(AppHostContext *context, HANDLE document,
        HANDLE stylesheet, int page_kind, const char *url);
int AppHostContext_ReplacePageWithScript(AppHostContext *context,
        HANDLE document, HANDLE stylesheet, AppScriptContext *script,
        int page_kind, const char *url);
void AppHostContext_ReleasePage(AppHostContext *context);
void AppHostContext_Shutdown(AppHostContext *context);

#endif /* POSITRON_APP_HOST_H */
