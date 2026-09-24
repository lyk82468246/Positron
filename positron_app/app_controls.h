/*
 * positron_app/app_controls.h - private native page-control adapter.
 *
 * The Core document remains the owner of form values, options and geometry.
 * This module projects text-like controls into WM6 EDIT children, SELECT
 * controls into WM6 COMBOBOX/LISTBOX children, and checkbox/radio controls
 * into WM6 BUTTON children. Core-painted buttons use the same document/event
 * hit path; Browser owns click/reset event transactions, while Core owns the
 * reset state change.
 */

#ifndef POSITRON_APP_CONTROLS_H
#define POSITRON_APP_CONTROLS_H

#include <windows.h>

#include "positron_core.h"
#include "app_script.h"

typedef struct AppControlsContext AppControlsContext;
typedef void (*AppControlsChangedFn)(void *pw);

AppControlsContext *AppControls_Create(HWND parent, HINSTANCE instance,
        void *pw, AppControlsChangedFn changed);
void AppControls_Destroy(AppControlsContext *context);
void AppControls_ClearPage(AppControlsContext *context);
int AppControls_Rebuild(AppControlsContext *context, HANDLE document,
        AppScriptContext *script, int scroll_x, int scroll_y);
int AppControls_Reconcile(AppControlsContext *context, HANDLE document,
        AppScriptContext *script, int scroll_x, int scroll_y);
void AppControls_PrepareReconcile(AppControlsContext *context);
void AppControls_Reposition(AppControlsContext *context, HANDLE document,
        int scroll_x, int scroll_y);
int AppControls_HandleCommand(AppControlsContext *context, WPARAM wparam,
        LPARAM lparam);
int AppControls_GetContentEditableSelection(AppControlsContext *context,
        AppScriptContext *script, const char *element_id, int *out_start,
        int *out_end, int *out_direction);
int AppControls_SetContentEditableSelection(AppControlsContext *context,
        AppScriptContext *script, const char *element_id, int start, int end,
        int direction);
int AppControls_HandleButtonPointer(AppControlsContext *context,
        int document_x, int document_y);
int AppControls_HandleButtonKey(AppControlsContext *context, UINT message,
        WPARAM key, LPARAM flags);
void AppControls_ClearButtonFocus(AppControlsContext *context);

#endif /* POSITRON_APP_CONTROLS_H */
