/*
 * positron_app/app_host.c - private WM6 host state and lifecycle boundary.
 */

#include <stdlib.h>
#include <string.h>

#include "app_host.h"
#include "app_script.h"
#include "positron_core.h"
#include "positron_http.h"

static void app_host_copy_text(char *target, int target_capacity,
        const char *source)
{
    int length;

    if (target == NULL || target_capacity <= 0) {
        return;
    }
    target[0] = '\0';
    if (source == NULL || target_capacity == 1) {
        return;
    }
    length = (int) strlen(source);
    if (length >= target_capacity) {
        length = target_capacity - 1;
    }
    memcpy(target, source, (size_t) length);
    target[length] = '\0';
}

void AppHostContext_Init(AppHostContext *context)
{
    if (context == NULL) {
        return;
    }
    memset(context, 0, sizeof(*context));
    context->page_kind = 1;
    context->page_width = 1;
    context->page_height = 1;
    context->document_width = 1;
    context->document_height = 1;
    context->dpi = 96;
    context->focus_index = -1;
}

void AppHostContext_SetInstance(AppHostContext *context, HINSTANCE instance)
{
    if (context != NULL) {
        context->instance = instance;
    }
}

void AppHostContext_SetWindow(AppHostContext *context, HWND window)
{
    if (context != NULL) {
        context->window = window;
    }
}

void AppHostContext_SetCoreInitialized(AppHostContext *context, int initialized)
{
    if (context != NULL) {
        context->core_initialized = initialized ? 1 : 0;
    }
}

void AppHostContext_SetHttpInitialized(AppHostContext *context, int initialized)
{
    if (context != NULL) {
        context->http_initialized = initialized ? 1 : 0;
    }
}

void AppHostContext_SetHistory(AppHostContext *context, HANDLE history)
{
    if (context != NULL) {
        context->history = history;
    }
}

void AppHostContext_SetAddress(AppHostContext *context, HWND address,
        WNDPROC original_proc)
{
    if (context == NULL) {
        return;
    }
    context->address = address;
    context->address_original_proc = original_proc;
}

void AppHostContext_SetPageWindow(AppHostContext *context, HWND page_window)
{
    if (context != NULL) {
        context->page_window = page_window;
    }
}

void AppHostContext_SetMenuBar(AppHostContext *context, HWND menu_bar)
{
    if (context != NULL) {
        context->menu_bar = menu_bar;
    }
}

int AppHostContext_ReplacePage(AppHostContext *context, HANDLE document,
        HANDLE stylesheet, int page_kind, const char *url)
{
    return AppHostContext_ReplacePageWithScript(context, document,
            stylesheet, NULL, page_kind, url);
}

int AppHostContext_ReplacePageWithScript(AppHostContext *context,
        HANDLE document, HANDLE stylesheet, AppScriptContext *script,
        int page_kind, const char *url)
{
    HANDLE old_document;
    HANDLE old_stylesheet;
    AppScriptContext *old_script;

    if (context == NULL || document == NULL) {
        return 1;
    }
    old_document = context->document;
    old_stylesheet = context->stylesheet;
    old_script = context->script;
    context->document = document;
    context->stylesheet = stylesheet;
    context->script = script;
    context->page_kind = page_kind;
    context->document_width = 1;
    context->document_height = 1;
    context->scroll_x = 0;
    context->scroll_y = 0;
    context->focus_index = -1;
    context->focus_count = 0;
    context->focus_id[0] = '\0';
    app_host_copy_text(context->current_url,
            sizeof(context->current_url), url);
    if (old_stylesheet != NULL) {
        PCore_FreeStylesheet(old_stylesheet);
    }
    if (old_script != NULL) {
        AppScript_Destroy(old_script);
    }
    if (old_document != NULL) {
        PCore_FreeDocument(old_document);
    }
    return 0;
}

void AppHostContext_ReleasePage(AppHostContext *context)
{
    if (context == NULL) {
        return;
    }
    if (context->script != NULL) {
        AppScript_Destroy(context->script);
        context->script = NULL;
    }
    if (context->stylesheet != NULL) {
        PCore_FreeStylesheet(context->stylesheet);
        context->stylesheet = NULL;
    }
    if (context->document != NULL) {
        PCore_FreeDocument(context->document);
        context->document = NULL;
    }
    context->document_width = 1;
    context->document_height = 1;
    context->scroll_x = 0;
    context->scroll_y = 0;
    context->focus_index = -1;
    context->focus_count = 0;
    context->focus_id[0] = '\0';
}

void AppHostContext_Shutdown(AppHostContext *context)
{
    if (context == NULL) {
        return;
    }
    AppHostContext_ReleasePage(context);
    if (context->history != NULL) {
        PBrowser_HistoryDestroy(context->history);
        context->history = NULL;
    }
    if (context->menu_bar != NULL) {
        CommandBar_Destroy(context->menu_bar);
        context->menu_bar = NULL;
    }
    if (context->http_initialized) {
        PHttp_Cleanup();
        context->http_initialized = 0;
    }
    if (context->core_initialized) {
        PCore_Shutdown();
        context->core_initialized = 0;
    }
    context->window = NULL;
    context->address = NULL;
    context->page_window = NULL;
    context->address_original_proc = NULL;
}
