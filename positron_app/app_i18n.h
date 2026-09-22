#ifndef POSITRON_APP_I18N_H
#define POSITRON_APP_I18N_H

#include <windows.h>

#define APP_I18N_PAGE_WELCOME 1
#define APP_I18N_PAGE_CONTROLS 2

typedef enum AppLanguage {
    APP_LANGUAGE_EN_US = 0,
    APP_LANGUAGE_ZH_CN = 1
} AppLanguage;

typedef enum AppTextId {
    APP_TEXT_MENU_BACK = 0,
    APP_TEXT_MENU_MENU,
    APP_TEXT_MENU_FORWARD,
    APP_TEXT_MENU_HOME,
    APP_TEXT_MENU_ADDRESS,
    APP_TEXT_MENU_REFRESH,
    APP_TEXT_MENU_EXIT,
    APP_TEXT_STATUS_OFFLINE,
    APP_TEXT_STATUS_HISTORY,
    APP_TEXT_STATUS_READY_WELCOME,
    APP_TEXT_STATUS_READY_CONTROLS,
    APP_TEXT_STATUS_ADDRESS_INVALID,
    APP_TEXT_STATUS_NO_BACK,
    APP_TEXT_STATUS_NO_FORWARD,
    APP_TEXT_STATUS_LAYOUT,
    APP_TEXT_STATUS_ADDRESS_CANCELLED,
    APP_TEXT_STATUS_LOADING,
    APP_TEXT_STATUS_READY_REMOTE,
    APP_TEXT_STATUS_NETWORK,
    APP_TEXT_ERROR_CORE_INIT,
    APP_TEXT_ERROR_HISTORY_INIT,
    APP_TEXT_ERROR_CLASS_REGISTER,
    APP_TEXT_ERROR_WINDOW_CREATE,
    APP_TEXT_COUNT
} AppTextId;

/* Initialize the EXE-private locale/resource selector. Returns 0 on success. */
int AppI18n_Init(HINSTANCE instance);

AppLanguage AppI18n_CurrentLanguage(void);

/* Return the selected WM6 SHCreateMenuBar RCDATA resource ID, or 0 if absent. */
UINT AppI18n_MenuBarResource(void);

/* Load a localized UTF-16 UI string into caller-owned storage. */
int AppI18n_LoadString(AppTextId text_id, WCHAR *buffer, int capacity);

/* Load a localized UTF-8 embedded page. The caller owns the returned buffer. */
int AppI18n_LoadPage(int page_kind, char **out_bytes,
        unsigned int *out_length);
void AppI18n_FreePage(char *bytes);

#endif
