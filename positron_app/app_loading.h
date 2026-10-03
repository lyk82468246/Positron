#ifndef POSITRON_APP_LOADING_H
#define POSITRON_APP_LOADING_H

#include <windows.h>
#include "../positron_http/positron_http.h"

/* Presentation only, not Browser candidate/resource states. REQUEST also
 * represents provider-merged transport phases without claiming DNS/TCP. */
typedef enum AppLoadingPhase {
    APP_LOADING_REQUEST = 0,
    APP_LOADING_RECEIVE,
    APP_LOADING_PARSE,
    APP_LOADING_SCRIPT_FETCH,
    APP_LOADING_SCRIPT_EXECUTE,
    APP_LOADING_STYLE,
    APP_LOADING_IMAGES,
    APP_LOADING_LAYOUT,
    APP_LOADING_PAINT,
    APP_LOADING_DNS,
    APP_LOADING_CONNECT,
    APP_LOADING_TLS,
    APP_LOADING_SEND,
    APP_LOADING_WAIT,
    APP_LOADING_HEADERS,
    APP_LOADING_REDIRECT,
    APP_LOADING_COUNT
} AppLoadingPhase;

#define APP_LOADING_INTERVAL_MS 250U

/* Phase text only; the address bar owns the loading animation. */
int AppLoading_Format(AppLoadingPhase phase, WCHAR *buffer, int capacity);
/* UI thread only. Does not invalidate the page or pump other messages. */
void AppLoading_Render(HWND window, AppLoadingPhase phase);
/* Synchronous observer mapping only. Does not retain the borrowed event.
 * Terminal/unknown/invalid events leave out_phase unchanged; transport
 * COMPLETE is not page commit or render completion. */
int AppLoading_FromHttp(const PHttpObserverEvent *event,
        AppLoadingPhase *out_phase);

#ifdef _DEBUG
int AppLoading_DebugCheck(void);
#endif

#endif
