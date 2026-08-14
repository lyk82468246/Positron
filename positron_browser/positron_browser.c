/*
 * positron_browser.c - host-independent browser history/session state.
 *
 * C89 only. No window, network, DOM or JavaScript dependency belongs here;
 * those concerns are composed by a browser host around this DLL.
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "positron_browser.h"
#include "positron_json.h"

typedef struct p_browser_history {
    char entries[PBROWSER_HISTORY_MAX][PBROWSER_HISTORY_URL_MAX];
    char states[PBROWSER_HISTORY_MAX][PBROWSER_HISTORY_STATE_MAX];
    unsigned long document_ids[PBROWSER_HISTORY_MAX];
    unsigned long next_document_id;
    int count;
    int index;
} p_browser_history;

static p_browser_history *p_history(HANDLE hHistory)
{
    return (p_browser_history *) hHistory;
}

static int p_history_valid(const p_browser_history *history)
{
    return history != NULL;
}

static int p_history_url_valid(const char *url)
{
    size_t length;

    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    length = strlen(url);
    return length < PBROWSER_HISTORY_URL_MAX;
}

static int p_history_state_valid(const char *state_json)
{
    HANDLE parsed;
    size_t length;

    if (state_json == NULL || state_json[0] == '\0') {
        return PBROWSER_ERROR_ARGUMENT;
    }
    length = strlen(state_json);
    if (length >= PBROWSER_HISTORY_STATE_MAX) {
        return PBROWSER_ERROR_LIMIT;
    }
    parsed = PJson_Parse(state_json);
    if (parsed == NULL) {
        return PBROWSER_ERROR_STATE;
    }
    PJson_Free(parsed);
    return PBROWSER_OK;
}

static int p_history_target_valid(int target_index)
{
    return target_index == PBROWSER_HISTORY_TARGET_NEW ||
            target_index == PBROWSER_HISTORY_TARGET_REPLACE_CURRENT ||
            target_index >= 0;
}

static unsigned long p_history_new_document_id(
        p_browser_history *history)
{
    history->next_document_id++;
    if (history->next_document_id == 0) {
        history->next_document_id++;
    }
    return history->next_document_id;
}

static const char *p_history_trim_default_port(const char *scheme,
        size_t scheme_length, const char *authority_start,
        const char *authority_end)
{
    const char *cursor;
    const char *colon;
    int colon_count;
    size_t port_length;

    colon = NULL;
    colon_count = 0;
    for (cursor = authority_start; cursor < authority_end; cursor++) {
        if (*cursor == ':') {
            colon = cursor;
            colon_count++;
        }
    }
    if (colon_count != 1 || colon == NULL ||
            (scheme_length != 4 && scheme_length != 5)) {
        return authority_end;
    }
    port_length = (size_t) (authority_end - colon - 1);
    if (scheme_length == 4 && _strnicmp(scheme, "http", 4) == 0 &&
            port_length == 2 && memcmp(colon + 1, "80", 2) == 0) {
        return colon;
    }
    if (scheme_length == 5 && _strnicmp(scheme, "https", 5) == 0 &&
            port_length == 3 && memcmp(colon + 1, "443", 3) == 0) {
        return colon;
    }
    return authority_end;
}

PBROWSER_API unsigned long PBrowser_AbiVersion(void)
{
    return PBROWSER_ABI_VERSION;
}

PBROWSER_API HANDLE PBrowser_HistoryCreate(void)
{
    p_browser_history *history;

    history = (p_browser_history *) malloc(sizeof(*history));
    if (history == NULL) {
        return NULL;
    }
    memset(history, 0, sizeof(*history));
    history->index = -1;
    return (HANDLE) history;
}

PBROWSER_API void PBrowser_HistoryDestroy(HANDLE hHistory)
{
    free(p_history(hHistory));
}

PBROWSER_API void PBrowser_HistoryReset(HANDLE hHistory)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history)) {
        return;
    }
    memset(history, 0, sizeof(*history));
    history->index = -1;
}

PBROWSER_API int PBrowser_HistoryCount(HANDLE hHistory)
{
    p_browser_history *history;

    history = p_history(hHistory);
    return p_history_valid(history) ? history->count : 0;
}

PBROWSER_API int PBrowser_HistoryIndex(HANDLE hHistory)
{
    p_browser_history *history;

    history = p_history(hHistory);
    return p_history_valid(history) ? history->index : -1;
}

PBROWSER_API const char *PBrowser_HistoryEntryUrl(HANDLE hHistory, int index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || index < 0 ||
            index >= history->count) {
        return NULL;
    }
    return history->entries[index];
}

PBROWSER_API const char *PBrowser_HistoryEntryState(HANDLE hHistory,
        int index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || index < 0 ||
            index >= history->count) {
        return NULL;
    }
    return history->states[index][0] != '\0' ?
            history->states[index] : "null";
}

PBROWSER_API unsigned long PBrowser_HistoryEntryDocumentId(HANDLE hHistory,
        int index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || index < 0 ||
            index >= history->count) {
        return 0;
    }
    return history->document_ids[index];
}

PBROWSER_API const char *PBrowser_HistoryCurrentUrl(HANDLE hHistory)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || history->index < 0 ||
            history->index >= history->count) {
        return NULL;
    }
    return history->entries[history->index];
}

PBROWSER_API const char *PBrowser_HistoryCurrentState(HANDLE hHistory)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || history->index < 0 ||
            history->index >= history->count ||
            history->states[history->index][0] == '\0') {
        return "null";
    }
    return history->states[history->index];
}

PBROWSER_API int PBrowser_HistorySameOriginUrl(const char *left,
        const char *right)
{
    const char *left_scheme;
    const char *right_scheme;
    const char *left_authority;
    const char *right_authority;
    const char *left_end;
    const char *right_end;
    size_t left_scheme_length;
    size_t right_scheme_length;
    size_t left_length;
    size_t right_length;

    if (!p_history_url_valid(left) || !p_history_url_valid(right)) {
        return 0;
    }
    left_scheme = strstr(left, "://");
    right_scheme = strstr(right, "://");
    if (left_scheme == NULL || right_scheme == NULL) {
        return 0;
    }
    left_scheme_length = (size_t) (left_scheme - left);
    right_scheme_length = (size_t) (right_scheme - right);
    if (left_scheme_length != right_scheme_length ||
            _strnicmp(left, right, left_scheme_length) != 0) {
        return 0;
    }
    left_authority = left_scheme + 3;
    right_authority = right_scheme + 3;
    left_end = left_authority;
    while (*left_end != '\0' && *left_end != '/' &&
            *left_end != '?' && *left_end != '#') {
        left_end++;
    }
    right_end = right_authority;
    while (*right_end != '\0' && *right_end != '/' &&
            *right_end != '?' && *right_end != '#') {
        right_end++;
    }
    left_end = p_history_trim_default_port(left, left_scheme_length,
            left_authority, left_end);
    right_end = p_history_trim_default_port(right, right_scheme_length,
            right_authority, right_end);
    left_length = (size_t) (left_end - left_authority);
    right_length = (size_t) (right_end - right_authority);
    return left_length == right_length &&
            _strnicmp(left_authority, right_authority, left_length) == 0;
}

PBROWSER_API int PBrowser_HistorySameBaseUrl(const char *left,
        const char *right)
{
    const char *left_hash;
    const char *right_hash;
    size_t left_length;
    size_t right_length;

    if (!p_history_url_valid(left) || !p_history_url_valid(right)) {
        return 0;
    }
    left_hash = strchr(left, '#');
    right_hash = strchr(right, '#');
    left_length = (left_hash != NULL) ?
            (size_t) (left_hash - left) : strlen(left);
    right_length = (right_hash != NULL) ?
            (size_t) (right_hash - right) : strlen(right);
    return left_length == right_length &&
            memcmp(left, right, left_length) == 0;
}

PBROWSER_API int PBrowser_HistoryIsSameDocumentTarget(HANDLE hHistory,
        int target_index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || target_index < 0 ||
            target_index >= history->count || target_index == history->index ||
            history->index < 0 || history->index >= history->count ||
            history->document_ids[target_index] == 0) {
        return 0;
    }
    return history->document_ids[target_index] ==
            history->document_ids[history->index];
}

static int p_history_copy_entry(p_browser_history *history, int index,
        const char *url, const char *state_json)
{
    size_t url_length;
    size_t state_length;

    if (!p_history_valid(history) || index < 0 ||
            index >= PBROWSER_HISTORY_MAX || !p_history_url_valid(url) ||
            p_history_state_valid(state_json) != PBROWSER_OK) {
        return PBROWSER_ERROR_ARGUMENT;
    }
    url_length = strlen(url);
    state_length = strlen(state_json);
    if (url_length >= PBROWSER_HISTORY_URL_MAX ||
            state_length >= PBROWSER_HISTORY_STATE_MAX) {
        return PBROWSER_ERROR_LIMIT;
    }
    memcpy(history->entries[index], url, url_length + 1);
    memcpy(history->states[index], state_json, state_length + 1);
    return PBROWSER_OK;
}

PBROWSER_API int PBrowser_HistoryReplaceState(HANDLE hHistory,
        const char *url, const char *state_json)
{
    p_browser_history *history;
    int state_rc;

    history = p_history(hHistory);
    if (!p_history_valid(history) ||
            PBrowser_HistoryCurrentUrl(hHistory) == NULL) {
        return PBROWSER_ERROR_ARGUMENT;
    }
    if (!p_history_url_valid(url)) {
        return PBROWSER_ERROR_LIMIT;
    }
    if (!PBrowser_HistorySameOriginUrl(
            PBrowser_HistoryCurrentUrl(hHistory), url)) {
        return PBROWSER_ERROR_ORIGIN;
    }
    state_rc = p_history_state_valid(state_json);
    if (state_rc != PBROWSER_OK) {
        return state_rc;
    }
    return p_history_copy_entry(history, history->index, url, state_json);
}

PBROWSER_API int PBrowser_HistoryPushState(HANDLE hHistory,
        const char *url, const char *state_json)
{
    p_browser_history *history;
    int state_rc;
    int i;

    history = p_history(hHistory);
    if (!p_history_valid(history) ||
            PBrowser_HistoryCurrentUrl(hHistory) == NULL) {
        return PBROWSER_ERROR_ARGUMENT;
    }
    if (!p_history_url_valid(url)) {
        return PBROWSER_ERROR_LIMIT;
    }
    if (!PBrowser_HistorySameOriginUrl(
            PBrowser_HistoryCurrentUrl(hHistory), url)) {
        return PBROWSER_ERROR_ORIGIN;
    }
    state_rc = p_history_state_valid(state_json);
    if (state_rc != PBROWSER_OK) {
        return state_rc;
    }
    if (history->index + 1 < history->count) {
        history->count = history->index + 1;
    }
    if (history->count >= PBROWSER_HISTORY_MAX) {
        for (i = 1; i < history->count; i++) {
            memcpy(history->entries[i - 1], history->entries[i],
                    PBROWSER_HISTORY_URL_MAX);
            memcpy(history->states[i - 1], history->states[i],
                    PBROWSER_HISTORY_STATE_MAX);
            history->document_ids[i - 1] = history->document_ids[i];
        }
        history->count--;
        history->index--;
    }
    if (p_history_copy_entry(history, history->count, url,
            state_json) != PBROWSER_OK) {
        return PBROWSER_ERROR_ARGUMENT;
    }
    history->document_ids[history->count] =
            history->document_ids[history->index];
    history->count++;
    history->index = history->count - 1;
    return PBROWSER_OK;
}

static int p_history_commit_new(p_browser_history *history, const char *url)
{
    int i;

    if (!p_history_valid(history) || !p_history_url_valid(url)) {
        return PBROWSER_ERROR_LIMIT;
    }
    if (PBrowser_HistoryCurrentUrl((HANDLE) history) != NULL &&
            strcmp(PBrowser_HistoryCurrentUrl((HANDLE) history), url) == 0) {
        history->document_ids[history->index] =
                p_history_new_document_id(history);
        return PBROWSER_OK;
    }
    if (history->index + 1 < history->count) {
        history->count = history->index + 1;
    }
    if (history->count >= PBROWSER_HISTORY_MAX) {
        for (i = 1; i < history->count; i++) {
            memcpy(history->entries[i - 1], history->entries[i],
                    PBROWSER_HISTORY_URL_MAX);
            memcpy(history->states[i - 1], history->states[i],
                    PBROWSER_HISTORY_STATE_MAX);
            history->document_ids[i - 1] = history->document_ids[i];
        }
        history->count--;
        history->index--;
    }
    memcpy(history->entries[history->count], url, strlen(url) + 1);
    memcpy(history->states[history->count], "null", 5);
    history->document_ids[history->count] =
            p_history_new_document_id(history);
    history->count++;
    history->index = history->count - 1;
    return PBROWSER_OK;
}

PBROWSER_API int PBrowser_HistoryCommitTarget(HANDLE hHistory,
        int target_index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || target_index < 0 ||
            target_index >= history->count) {
        return PBROWSER_ERROR_RANGE;
    }
    history->index = target_index;
    return PBROWSER_OK;
}

PBROWSER_API int PBrowser_HistoryCommitTargetDocument(HANDLE hHistory,
        int target_index)
{
    p_browser_history *history;
    int rc;

    history = p_history(hHistory);
    rc = PBrowser_HistoryCommitTarget(hHistory, target_index);
    if (rc != PBROWSER_OK) {
        return rc;
    }
    history->document_ids[target_index] =
            p_history_new_document_id(history);
    return PBROWSER_OK;
}

PBROWSER_API int PBrowser_HistoryReplaceCurrent(HANDLE hHistory,
        const char *url)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || !p_history_url_valid(url)) {
        return PBROWSER_ERROR_LIMIT;
    }
    if (PBrowser_HistoryCurrentUrl(hHistory) == NULL) {
        return p_history_commit_new(history, url);
    }
    memcpy(history->entries[history->index], url, strlen(url) + 1);
    memcpy(history->states[history->index], "null", 5);
    history->document_ids[history->index] =
            p_history_new_document_id(history);
    return PBROWSER_OK;
}

PBROWSER_API int PBrowser_HistoryCommitNavigation(HANDLE hHistory,
        const char *url, int method, int target_index)
{
    if (method != PBROWSER_HISTORY_METHOD_GET) {
        return PBROWSER_ERROR_METHOD;
    }
    if (!p_history_target_valid(target_index)) {
        return PBROWSER_ERROR_RANGE;
    }
    if (target_index == PBROWSER_HISTORY_TARGET_REPLACE_CURRENT) {
        return PBrowser_HistoryReplaceCurrent(hHistory, url);
    }
    if (target_index >= 0) {
        return PBrowser_HistoryCommitTargetDocument(hHistory, target_index);
    }
    return p_history_commit_new(p_history(hHistory), url);
}

PBROWSER_API int PBrowser_HistoryCommitNavigationWithState(HANDLE hHistory,
        const char *url, int method, int target_index,
        const char *state_json)
{
    int rc;
    int state_rc;

    state_rc = PBROWSER_OK;
    if (state_json != NULL) {
        state_rc = p_history_state_valid(state_json);
        if (state_rc != PBROWSER_OK) {
            return state_rc;
        }
    }
    rc = PBrowser_HistoryCommitNavigation(hHistory, url, method,
            target_index);
    if (rc != PBROWSER_OK || state_json == NULL) {
        return rc;
    }
    return PBrowser_HistoryReplaceState(hHistory,
            PBrowser_HistoryCurrentUrl(hHistory), state_json);
}

PBROWSER_API const char *PBrowser_HistoryBackTarget(HANDLE hHistory,
        int *out_index)
{
    p_browser_history *history;
    int target;

    history = p_history(hHistory);
    if (out_index == NULL || !p_history_valid(history) || history->index <= 0 ||
            history->index >= history->count) {
        return NULL;
    }
    target = history->index - 1;
    *out_index = target;
    return history->entries[target];
}

PBROWSER_API const char *PBrowser_HistoryForwardTarget(HANDLE hHistory,
        int *out_index)
{
    p_browser_history *history;
    int target;

    history = p_history(hHistory);
    if (out_index == NULL || !p_history_valid(history) || history->index < 0 ||
            history->index + 1 >= history->count) {
        return NULL;
    }
    target = history->index + 1;
    *out_index = target;
    return history->entries[target];
}

PBROWSER_API const char *PBrowser_HistoryGoTarget(HANDLE hHistory, int delta,
        int *out_index)
{
    p_browser_history *history;
    int target;

    history = p_history(hHistory);
    if (out_index == NULL || !p_history_valid(history) || history->index < 0 ||
            history->index >= history->count ||
            delta < -(PBROWSER_HISTORY_MAX - 1) ||
            delta > PBROWSER_HISTORY_MAX - 1) {
        return NULL;
    }
    target = history->index + delta;
    if (target < 0 || target >= history->count) {
        return NULL;
    }
    *out_index = target;
    return history->entries[target];
}

PBROWSER_API int PBrowser_HistoryNavigationLength(HANDLE hHistory,
        const char *url, int method, int target_index)
{
    p_browser_history *history;
    int count;

    history = p_history(hHistory);
    if (!p_history_valid(history)) {
        return 1;
    }
    count = history->count;
    if (method != PBROWSER_HISTORY_METHOD_GET) {
        return (count > 0) ? count : 1;
    }
    if (target_index == PBROWSER_HISTORY_TARGET_REPLACE_CURRENT ||
            target_index >= 0) {
        return (count > 0) ? count : 1;
    }
    if (!p_history_url_valid(url)) {
        return (count > 0) ? count : 1;
    }
    if (PBrowser_HistoryCurrentUrl(hHistory) != NULL &&
            strcmp(PBrowser_HistoryCurrentUrl(hHistory), url) == 0) {
        return (count > 0) ? count : 1;
    }
    if (history->index + 1 < count) {
        count = history->index + 1;
    }
    if (count < PBROWSER_HISTORY_MAX) {
        count++;
    }
    return (count > 0) ? count : 1;
}

PBROWSER_API int PBrowser_HistoryNavigationIndex(HANDLE hHistory,
        const char *url, int method, int target_index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history)) {
        return 0;
    }
    if (method != PBROWSER_HISTORY_METHOD_GET) {
        return (history->index >= 0) ? history->index : 0;
    }
    if (target_index == PBROWSER_HISTORY_TARGET_REPLACE_CURRENT) {
        return (history->index >= 0) ? history->index : 0;
    }
    if (target_index >= 0) {
        return target_index;
    }
    if (PBrowser_HistoryCurrentUrl(hHistory) != NULL && url != NULL &&
            strcmp(PBrowser_HistoryCurrentUrl(hHistory), url) == 0) {
        return history->index;
    }
    return PBrowser_HistoryNavigationLength(hHistory, url, method,
            target_index) - 1;
}

PBROWSER_API const char *PBrowser_HistoryNavigationState(HANDLE hHistory,
        const char *url, int method, int target_index)
{
    p_browser_history *history;

    history = p_history(hHistory);
    if (!p_history_valid(history) || method != PBROWSER_HISTORY_METHOD_GET) {
        return "null";
    }
    if (target_index == PBROWSER_HISTORY_TARGET_REPLACE_CURRENT) {
        return "null";
    }
    if (target_index >= 0) {
        return PBrowser_HistoryEntryState(hHistory, target_index);
    }
    if (PBrowser_HistoryCurrentUrl(hHistory) != NULL && url != NULL &&
            strcmp(PBrowser_HistoryCurrentUrl(hHistory), url) == 0) {
        return PBrowser_HistoryCurrentState(hHistory);
    }
    return "null";
}
