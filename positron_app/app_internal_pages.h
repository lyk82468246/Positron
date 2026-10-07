/* EXE-private internal pages and address-bar command policy. */
#ifndef POSITRON_APP_INTERNAL_PAGES_H
#define POSITRON_APP_INTERNAL_PAGES_H
#include <windows.h>
#include "app_i18n.h"
#include "app_settings_store.h"

#define APP_INTERNAL_HTML_MAX 131072U
#define APP_INTERNAL_FOCUS_MAX 32
#define APP_INTERNAL_URL_MAX 1024
#define APP_URL_NEWTAB "positron://newtab"

typedef enum AppNavigationSource {
    APP_NAV_SOURCE_DOCUMENT = 0,
    APP_NAV_SOURCE_ADDRESS,
    APP_NAV_SOURCE_MENU,
    APP_NAV_SOURCE_HISTORY,
    APP_NAV_SOURCE_SCRIPT,
    APP_NAV_SOURCE_STARTUP,
    APP_NAV_SOURCE_FORM,
    APP_NAV_SOURCE_REDIRECT
} AppNavigationSource;

typedef enum AppInternalRouteKind {
    APP_INTERNAL_PAGE = 1,
    APP_INTERNAL_ALIAS,
    APP_INTERNAL_COMMAND
} AppInternalRouteKind;

typedef struct AppInternalRoute {
    AppInternalRouteKind kind;
    int page_kind;
    char url[APP_INTERNAL_URL_MAX];
} AppInternalRoute;

typedef struct AppInternalPageData {
    HANDLE history;
    int dpi;
    int viewport_width;
    int viewport_height;
    /* Borrowed pure-data snapshot, valid only during Build. visited_utc is
     * UTC Unix seconds, supplied by the UI owner. */
    const AppVisitSnapshot *visits;
    /* 0: legacy session fixture; 1: loading; 2: loaded; 3: unavailable. */
    int visits_status;
} AppInternalPageData;

/* 0: recognized/allowed. 1: not a valid or permitted internal address.
 * Output is cleared on failure. Commands require ADDRESS; aliases resolve
 * before navigation and never create a separate document/history entry. */
int AppInternalPages_Resolve(const char *url, AppNavigationSource source,
        AppInternalRoute *out_route);
/* Caller owns UTF-8 bytes; release through AppI18n_FreePage after parsing. */
int AppInternalPages_Build(int page_kind, const AppInternalPageData *data,
        char **out_bytes, unsigned int *out_length);
int AppInternalPages_FocusIds(HANDLE document,
        char ids[APP_INTERNAL_FOCUS_MAX][128]);
#ifdef _DEBUG
int AppInternalPages_DebugCheck(const char *css);
#endif
#endif
