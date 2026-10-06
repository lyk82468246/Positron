/* EXE-private tab ownership. All live Core/Browser handles stay on the UI
 * thread; transport workers continue to own only their request resources. */
#ifndef POSITRON_APP_TABS_H
#define POSITRON_APP_TABS_H

#include "app_host.h"
#include "app_input.h"
#include "app_settings_store.h"

#define APP_TAB_MAX 4

typedef struct AppControlsContext AppControlsContext;

typedef struct AppTab {
    AppHostContext host;
    AppControlsContext *controls;
    int used;
    int closing;
    HWND saved_focus;
    int address_editing;
    WCHAR address_text[APP_HOST_URL_MAX];
    DWORD address_start;
    DWORD address_end;
    int overflow_pointer;
    POINT overflow_pointer_point;
    AppInputPointer page_pointer;
    HANDLE page_pointer_document;
    int page_pointer_scroll_x;
    int page_pointer_scroll_y;
    int refresh_pending;
    int refresh_form_reset;
    AppScriptContext *refresh_context;
    AppVisitSnapshot *visits;
    int visits_status;
    int visits_redraw_pending;
    int visits_reload_pending;
    unsigned long visits_generation;
    unsigned long visits_request_id;
    __int64 visits_before_id;
} AppTab;

#endif
