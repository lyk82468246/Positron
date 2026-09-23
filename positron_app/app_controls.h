/*
 * positron_app/app_controls.h - private native page-control adapter.
 *
 * The Core document remains the owner of form values, options and geometry.
 * This module projects text-like controls into WM6 EDIT children and SELECT
 * controls into WM6 COMBOBOX/LISTBOX children; Browser owns the bounded
 * native event transactions.
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
void AppControls_Reposition(AppControlsContext *context, HANDLE document,
        int scroll_x, int scroll_y);
int AppControls_HandleCommand(AppControlsContext *context, WPARAM wparam,
        LPARAM lparam);

#endif /* POSITRON_APP_CONTROLS_H */
