#ifndef POSITRON_APP_ADDRESS_BAR_H
#define POSITRON_APP_ADDRESS_BAR_H

#include <windows.h>

typedef struct AppAddressBar AppAddressBar;

/* EXE-private presentation. The native EDIT always contains a URL, never
 * a page title. All calls and the animation timer belong to the UI thread. */
AppAddressBar *AppAddressBar_Create(HINSTANCE instance, HWND parent, int id);
HWND AppAddressBar_Edit(AppAddressBar *bar);
void AppAddressBar_Move(AppAddressBar *bar, int width, int height, int dpi);
void AppAddressBar_SetUrl(AppAddressBar *bar, const WCHAR *url);
void AppAddressBar_SetTitle(AppAddressBar *bar, const WCHAR *title);
void AppAddressBar_SetLoading(AppAddressBar *bar, int loading);
void AppAddressBar_BeginEdit(AppAddressBar *bar);
void AppAddressBar_EndEdit(AppAddressBar *bar);
void AppAddressBar_Destroy(AppAddressBar *bar);

#ifdef _DEBUG
int AppAddressBar_DebugCheck(HINSTANCE instance);
#endif

#endif
