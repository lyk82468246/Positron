#include <windows.h>
#include <string.h>
#include <wchar.h>

#include "app_loading.h"
#include "app_i18n.h"
#include "app_debug.h"

#define APP_LOADING_TITLE_MAX 256

static const AppTextId g_loading_text[APP_LOADING_COUNT] = {
    APP_TEXT_LOADING_REQUEST,
    APP_TEXT_LOADING_RECEIVE,
    APP_TEXT_LOADING_PARSE,
    APP_TEXT_LOADING_SCRIPT_FETCH,
    APP_TEXT_LOADING_SCRIPT_EXECUTE,
    APP_TEXT_LOADING_STYLE,
    APP_TEXT_LOADING_IMAGES,
    APP_TEXT_LOADING_LAYOUT,
    APP_TEXT_LOADING_PAINT,
    APP_TEXT_LOADING_DNS,
    APP_TEXT_LOADING_CONNECT,
    APP_TEXT_LOADING_TLS,
    APP_TEXT_LOADING_SEND,
    APP_TEXT_LOADING_WAIT,
    APP_TEXT_LOADING_HEADERS,
    APP_TEXT_LOADING_REDIRECT
};

int AppLoading_FromHttp(const PHttpObserverEvent *event,
        AppLoadingPhase *out_phase)
{
    AppLoadingPhase phase;

    if (event == NULL || out_phase == NULL ||
            event->size < sizeof(PHttpObserverEvent) ||
            event->version != PHTTP_OBSERVER_VERSION) return 0;
    switch (event->phase) {
    case PHTTP_PHASE_RESOLVING_NAME: phase = APP_LOADING_DNS; break;
    case PHTTP_PHASE_CONNECTING: phase = APP_LOADING_CONNECT; break;
    case PHTTP_PHASE_TLS_HANDSHAKE: phase = APP_LOADING_TLS; break;
    case PHTTP_PHASE_SENDING_REQUEST: phase = APP_LOADING_SEND; break;
    case PHTTP_PHASE_WAITING_RESPONSE: phase = APP_LOADING_WAIT; break;
    case PHTTP_PHASE_RECEIVING_HEADERS: phase = APP_LOADING_HEADERS; break;
    case PHTTP_PHASE_RECEIVING_BODY: phase = APP_LOADING_RECEIVE; break;
    case PHTTP_PHASE_REDIRECTING: phase = APP_LOADING_REDIRECT; break;
    default: return 0;
    }
    if (event->flags & PHTTP_OBSERVER_FLAG_PHASE_MERGED) {
        phase = APP_LOADING_REQUEST;
    }
    *out_phase = phase;
    return 1;
}

int AppLoading_Format(AppLoadingPhase phase, WCHAR *buffer, int capacity)
{
    WCHAR text[APP_LOADING_TITLE_MAX];
    int length;

    if (buffer == NULL || capacity <= 0) return 0;
    buffer[0] = L'\0';
    if (phase < 0 || phase >= APP_LOADING_COUNT) return 0;
    length = AppI18n_LoadString(g_loading_text[(int) phase], text,
            APP_LOADING_TITLE_MAX);
    if (length <= 0 || length + 1 > capacity) return 0;
    memcpy(buffer, text, ((size_t) length + 1U) * sizeof(WCHAR));
    return length;
}

void AppLoading_Render(HWND window, AppLoadingPhase phase)
{
    WCHAR title[APP_LOADING_TITLE_MAX];
    WCHAR previous[APP_LOADING_TITLE_MAX];

    if (window == NULL || AppLoading_Format(phase, title,
            APP_LOADING_TITLE_MAX) == 0) return;
    previous[0] = L'\0';
    GetWindowTextW(window, previous, APP_LOADING_TITLE_MAX);
    if (wcscmp(title, previous) != 0) SetWindowTextW(window, title);
}

#ifdef _DEBUG
int AppLoading_DebugCheck(void)
{
    WCHAR first[APP_LOADING_TITLE_MAX];
    WCHAR actual[APP_LOADING_TITLE_MAX];
    int phase;
    int length;
    int result;
    PHttpObserverEvent event;
    AppLoadingPhase mapped;
    static const AppLoadingPhase expected_http[] = {
        APP_LOADING_DNS, APP_LOADING_CONNECT, APP_LOADING_TLS,
        APP_LOADING_SEND, APP_LOADING_WAIT, APP_LOADING_HEADERS,
        APP_LOADING_RECEIVE, APP_LOADING_REDIRECT
    };

    result = 1;
    memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    event.version = PHTTP_OBSERVER_VERSION;
    for (phase = PHTTP_PHASE_RESOLVING_NAME;
            phase <= PHTTP_PHASE_REDIRECTING; ++phase) {
        event.phase = phase;
        event.flags = 0;
        if (!AppLoading_FromHttp(&event, &mapped) ||
                mapped != expected_http[phase - PHTTP_PHASE_RESOLVING_NAME]) goto done;
        event.flags = PHTTP_OBSERVER_FLAG_PHASE_MERGED;
        if (!AppLoading_FromHttp(&event, &mapped) ||
                mapped != APP_LOADING_REQUEST) goto done;
    }
    mapped = APP_LOADING_STYLE;
    event.phase = PHTTP_PHASE_COMPLETE;
    if (AppLoading_FromHttp(&event, &mapped) || mapped != APP_LOADING_STYLE) goto done;
    event.phase = PHTTP_PHASE_FAILED;
    if (AppLoading_FromHttp(&event, &mapped) || mapped != APP_LOADING_STYLE) goto done;
    event.phase = PHTTP_PHASE_NONE;
    if (AppLoading_FromHttp(&event, &mapped) || mapped != APP_LOADING_STYLE) goto done;
    event.phase = 999;
    if (AppLoading_FromHttp(&event, &mapped) || mapped != APP_LOADING_STYLE) goto done;
    event.phase = PHTTP_PHASE_CONNECTING;
    event.size = sizeof(event) - 1U;
    if (AppLoading_FromHttp(&event, &mapped) || mapped != APP_LOADING_STYLE) goto done;
    event.size = sizeof(event);
    event.version = PHTTP_OBSERVER_VERSION + 1U;
    if (AppLoading_FromHttp(&event, &mapped) || mapped != APP_LOADING_STYLE ||
            AppLoading_FromHttp(NULL, &mapped) ||
            AppLoading_FromHttp(&event, NULL)) goto done;
    for (phase = 0; phase < APP_LOADING_COUNT; ++phase) {
        length = AppLoading_Format((AppLoadingPhase) phase, first,
                APP_LOADING_TITLE_MAX);
        if (length < 1 || wcschr(L"|/-\\", first[0]) != NULL ||
                AppI18n_LoadString(g_loading_text[phase], actual,
                APP_LOADING_TITLE_MAX) != length || wcscmp(first, actual)) goto done;
        /* Exact capacity succeeds; one WCHAR less returns an empty result. */
        if (AppLoading_Format((AppLoadingPhase) phase, actual,
                length + 1) != length ||
                AppLoading_Format((AppLoadingPhase) phase, actual,
                length) != 0 || actual[0] != L'\0') goto done;
    }
    if (AppLoading_Format(APP_LOADING_COUNT, actual,
            APP_LOADING_TITLE_MAX) != 0 || actual[0] != L'\0' ||
            AppLoading_Format((AppLoadingPhase) -1, actual,
            APP_LOADING_TITLE_MAX) != 0 ||
            AppLoading_Format(APP_LOADING_REQUEST, NULL, 0) != 0) goto done;
    result = 0;
done:
    AppDebug_Log(result == 0 ? "positron loading-title selftest OK\r\n" :
            "positron loading-title selftest FAILED\r\n");
    return result;
}
#endif
