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
#include "positron_script.h"

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

    static const char P_BROWSER_SCRIPT_BOOTSTRAP[] =
        "(function(g){"
        "function PElement(id){this.__id=id;}"
        "PElement.prototype.click=function(){"
        "if(!__pcoreClick({id:this.__id})){throw new Error('click failed');}};"
        "Object.defineProperty(PElement.prototype,'textContent',{"
        "get:function(){return __pcoreGetText({id:this.__id});},"
        "set:function(v){if(!__pcoreSetText({id:this.__id,text:String(v)}))"
        "{throw new Error('textContent update failed');}}});"
        "PElement.prototype.getAttribute=function(name){"
        "return __pcoreGetAttribute({id:this.__id,name:String(name)});};"
        "PElement.prototype.hasAttribute=function(name){"
        "return this.getAttribute(name)!==null;};"
        "PElement.prototype.setAttribute=function(name,value){"
        "if(!__pcoreSetAttribute({id:this.__id,name:String(name),"
        "value:String(value)})){throw new Error('setAttribute failed');}};"
        "PElement.prototype.removeAttribute=function(name){"
        "if(!__pcoreRemoveAttribute({id:this.__id,name:String(name)}))"
        "{throw new Error('removeAttribute failed');}};"
        "Object.defineProperty(PElement.prototype,'value',{"
        "get:function(){return __pcoreGetValue({id:this.__id});},"
        "set:function(v){if(!__pcoreSetValue({id:this.__id,value:String(v)}))"
        "{throw new Error('value update failed');}}});"
        "Object.defineProperty(PElement.prototype,'defaultValue',{"
        "get:function(){return __pcoreFormProperty({id:this.__id,"
        "op:'getDefaultValue'});},"
        "set:function(v){if(!__pcoreFormProperty({id:this.__id,"
        "op:'setDefaultValue',value:String(v)})){throw new Error("
        "'defaultValue update failed');}}});"
        "Object.defineProperty(PElement.prototype,'checked',{"
        "get:function(){return __pcoreGetChecked({id:this.__id});},"
        "set:function(v){if(!__pcoreSetChecked({id:this.__id,"
        "checked:v?1:0})){throw new Error('checked update failed');}}});"
        "function PBooleanAttribute(o,name,v){"
        "if(v){if(!__pcoreSetAttribute({id:o.__id,name:name,value:''}))"
        "{throw new Error(name+' update failed');}}"
        "else{if(!__pcoreRemoveAttribute({id:o.__id,name:name}))"
        "{throw new Error(name+' update failed');}}}"
        "function PDefineBoolean(name,attr){"
        "Object.defineProperty(PElement.prototype,name,{"
        "get:function(){return this.hasAttribute(attr);},"
        "set:function(v){PBooleanAttribute(this,attr,v);}});}"
        "function PDefineString(name,attr){"
        "Object.defineProperty(PElement.prototype,name,{"
        "get:function(){var v=this.getAttribute(attr);"
        "return v===null?'':v;},"
        "set:function(v){if(!__pcoreSetAttribute({id:this.__id,"
        "name:attr,value:String(v)})){throw new Error(name+' update failed');}}});}"
        "PDefineBoolean('required','required');"
        "PDefineBoolean('readOnly','readonly');"
        "PDefineBoolean('multiple','multiple');"
        "PDefineBoolean('noValidate','novalidate');"
        "PDefineBoolean('formNoValidate','formnovalidate');"
        "PDefineBoolean('disabled','disabled');"
        "PDefineString('name','name');"
        "PDefineString('title','title');"
        "PDefineString('lang','lang');"
        "PDefineString('dir','dir');"
        "PDefineBoolean('hidden','hidden');"
        "PDefineString('accessKey','accesskey');"
        "PDefineString('role','role');"
        "PDefineString('ariaLabel','aria-label');"
        "PDefineString('contentEditable','contenteditable');"
        "PDefineString('draggable','draggable');"
        "PDefineString('accept','accept');"
        "PDefineString('capture','capture');"
        "PDefineString('dirname','dirname');"
        "PDefineString('list','list');"
        "PDefineString('wrap','wrap');"
        "PDefineString('htmlFor','for');"
        "PDefineString('slot','slot');"
        "PDefineString('itemId','itemid');"
        "PDefineString('itemProp','itemprop');"
        "PDefineString('itemRef','itemref');"
        "PDefineBoolean('itemScope','itemscope');"
        "PDefineString('itemType','itemtype');"
        "PDefineString('nonce','nonce');"
        "PDefineString('part','part');"
        "function PDefineInteger(name,attr){"
        "Object.defineProperty(PElement.prototype,name,{"
        "get:function(){var v=this.getAttribute(attr);var n;"
        "if(v===null||v==='')return -1;n=Number(v);"
        "return n===n&&isFinite(n)&&n===Math.floor(n)?n:-1;},"
        "set:function(v){var n=Number(v);"
        "if(n!==n||!isFinite(n)||n!==Math.floor(n))"
        "{throw new Error(name+' value');}"
        "if(!__pcoreSetAttribute({id:this.__id,name:attr,value:String(n)}))"
        "{throw new Error(name+' update failed');}}});}"
        "PDefineInteger('tabIndex','tabindex');"
        "PDefineString('action','action');"
        "PDefineString('method','method');"
        "PDefineString('enctype','enctype');"
        "PDefineString('target','target');"
        "PDefineString('autocomplete','autocomplete');"
        "PDefineString('acceptCharset','accept-charset');"
        "PDefineString('placeholder','placeholder');"
        "PDefineString('inputMode','inputmode');"
        "PDefineString('type','type');"
        "PDefineString('formAction','formaction');"
        "PDefineString('formMethod','formmethod');"
        "PDefineString('formEnctype','formenctype');"
        "PDefineString('min','min');"
        "PDefineString('max','max');"
        "PDefineString('step','step');"
        "PDefineString('pattern','pattern');"
        "function PDefineLength(name,attr){"
        "Object.defineProperty(PElement.prototype,name,{"
        "get:function(){var v=this.getAttribute(attr);"
        "if(v===null||v==='')return -1;var n=Number(v);"
        "return n===n&&n!==Infinity&&n!==-Infinity&&n>=0&&"
        "n===Math.floor(n)?n:-1;},"
        "set:function(v){var n=Number(v);"
        "if(n!==n||n===Infinity||n===-Infinity||"
        "n!==Math.floor(n)||n<0)"
        "{throw new Error(name+' value');}"
        "if(!__pcoreSetAttribute({id:this.__id,name:attr,value:String(n)}))"
        "{throw new Error(name+' update failed');}}});}"
        "PDefineLength('minLength','minlength');"
        "PDefineLength('maxLength','maxlength');"
        "function PValidation(o){return __pcoreValidation({id:o.__id});}"
        "PElement.prototype.checkValidity=function(){"
        "return !!PValidation(this).valid;};"
        "PElement.prototype.reportValidity=function(){"
        "return !!__pcoreReportValidity({id:this.__id});};"
        "Object.defineProperty(PElement.prototype,'willValidate',{"
        "get:function(){return !!PValidation(this).willValidate;}});"
        "Object.defineProperty(PElement.prototype,'validity',{"
        "get:function(){return PValidation(this);}});"
        "PElement.prototype.setCustomValidity=function(v){"
        "if(!__pcoreCustomValidity({id:this.__id,op:'set',"
        "value:String(v)})){throw new Error('custom validity update failed');}};"
        "Object.defineProperty(PElement.prototype,'validationMessage',{"
        "get:function(){return __pcoreCustomValidity({id:this.__id,"
        "op:'get'});}});"
        "Object.defineProperty(PElement.prototype,'defaultChecked',{"
        "get:function(){return __pcoreFormProperty({id:this.__id,"
        "op:'getDefaultChecked'});},"
        "set:function(v){if(!__pcoreFormProperty({id:this.__id,"
        "op:'setDefaultChecked',checked:v?1:0})){throw new Error("
        "'defaultChecked update failed');}}});"
        "Object.defineProperty(PElement.prototype,'selectedIndex',{"
        "get:function(){return __pcoreFormProperty({id:this.__id,"
        "op:'getSelectedIndex'});},"
        "set:function(v){var n=Number(v);"
        "if(n!==n||n!==Math.floor(n)){throw new Error('selectedIndex value');}"
        "if(!__pcoreFormProperty({id:this.__id,op:'setSelectedIndex',"
        "index:n}))"
        "{throw new Error('selectedIndex update failed');}}});"
        "Object.defineProperty(PElement.prototype,'id',{"
        "get:function(){var v=this.getAttribute('id');"
        "return v===null?'':v;},"
        "set:function(v){var s=String(v);"
        "if(!__pcoreSetAttribute({id:this.__id,name:'id',value:s}))"
        "{throw new Error('id update failed');}this.__id=s;}});"
        "Object.defineProperty(PElement.prototype,'className',{"
        "get:function(){var v=this.getAttribute('class');"
        "return v===null?'':v;},"
        "set:function(v){if(!__pcoreSetAttribute({id:this.__id,"
        "name:'class',value:String(v)})){throw new Error("
        "'className update failed');}}});"
        "function PClassList(owner){this.__owner=owner;}"
        "PClassList.prototype._tokens=function(){"
        "var s=this.__owner.className;var a;"
        "if(s===''){return [];}s=s.replace(/\\s+/g,' ');"
        "if(s.charAt(0)===' '){s=s.substring(1);}"
        "if(s.charAt(s.length-1)===' '){s=s.substring(0,s.length-1);}"
        "return s===''?[]:s.split(' ');};"
        "PClassList.prototype._write=function(a){"
        "var s='';var i;"
        "for(i=0;i<a.length;i++){if(a[i]===''){continue;}"
        "if(s!==''){s+=' ';}s+=a[i];}"
        "this.__owner.className=s;};"
        "PClassList.prototype.contains=function(token){"
        "var t=String(token);var a=this._tokens();var i;"
        "for(i=0;i<a.length;i++){if(a[i]===t){return true;}}"
        "return false;};"
        "PClassList.prototype.add=function(){"
        "var a=this._tokens();var t;var i;var j;var found;"
        "for(i=0;i<arguments.length;i++){t=String(arguments[i]);"
        "if(t===''){continue;}found=false;"
        "for(j=0;j<a.length;j++){if(a[j]===t){found=true;break;}}"
        "if(!found){a.push(t);}}this._write(a);};"
        "PClassList.prototype.remove=function(){"
        "var a=this._tokens();var t;var i;var j;"
        "for(i=0;i<arguments.length;i++){t=String(arguments[i]);"
        "for(j=a.length-1;j>=0;j--){if(a[j]===t){a.splice(j,1);}}}"
        "this._write(a);};"
        "PClassList.prototype.toggle=function(token,force){"
        "var t=String(token);var present=this.contains(t);"
        "if(arguments.length>1){if(force===true&&!present){this.add(t);return true;}"
        "if(force===false&&present){this.remove(t);return false;}"
        "return present;}"
        "if(present){this.remove(t);return false;}this.add(t);return true;};"
        "PClassList.prototype.toString=function(){return this.__owner.className;};"
        "Object.defineProperty(PElement.prototype,'classList',{"
        "get:function(){return new PClassList(this);}});"
        "function PTrim(s){return String(s).replace(/^\\s+|\\s+$/g,'');}"
        "function PStyle(owner){this.__owner=owner;}"
        "PStyle.prototype._parse=function(){"
        "var raw=this.__owner.getAttribute('style')||'';var parts=raw.split(';');"
        "var a=[];var i;var p;var k;var n;"
        "for(i=0;i<parts.length;i++){p=parts[i];n=p.indexOf(':');"
        "if(n<=0){continue;}k=PTrim(p.substring(0,n)).toLowerCase();"
        "if(k!==''){a.push([k,PTrim(p.substring(n+1))]);}}return a;};"
        "PStyle.prototype._write=function(a){"
        "var s='';var i;"
        "for(i=0;i<a.length;i++){if(i>0){s+='; ';}"
        "s+=a[i][0]+': '+a[i][1];}"
        "this.__owner.setAttribute('style',s);};"
        "PStyle.prototype.getPropertyValue=function(name){"
        "var n=PTrim(name).toLowerCase();var a=this._parse();var i;"
        "for(i=0;i<a.length;i++){if(a[i][0]===n){return a[i][1];}}return '';};"
        "PStyle.prototype.setProperty=function(name,value,priority){"
        "var n=PTrim(name).toLowerCase();var v=PTrim(value);var a=this._parse();"
        "var i;var found=false;(void priority);if(n===''){return;}"
        "if(v===''){this.removeProperty(n);return;}"
        "for(i=0;i<a.length;i++){if(a[i][0]===n){a[i][1]=v;found=true;break;}}"
        "if(!found){a.push([n,v]);}this._write(a);};"
        "PStyle.prototype.removeProperty=function(name){"
        "var n=PTrim(name).toLowerCase();var a=this._parse();var old='';var b=[];"
        "var i;for(i=0;i<a.length;i++){if(a[i][0]===n){old=a[i][1];}"
        "else{b.push(a[i]);}}this._write(b);return old;};"
        "Object.defineProperty(PStyle.prototype,'cssText',{"
        "get:function(){return this.__owner.getAttribute('style')||'';},"
        "set:function(v){if(!__pcoreSetAttribute({id:this.__owner.__id,"
        "name:'style',value:String(v)})){throw new Error('cssText update failed');}}});"
        "Object.defineProperty(PElement.prototype,'style',{"
        "get:function(){return new PStyle(this);}});"
        "g.__pcoreHandlers={};"
        "g.__pcoreDispatchEvent=function(info){"
        "var fn=g.__pcoreHandlers[info.listener];"
        "if(typeof fn!=='function'){return false;}"
        "var e={type:info.type,phase:info.phase,bubbles:!!info.bubbles,"
        "cancelable:!!info.cancelable,trusted:!!info.trusted,"
        "defaultPrevented:!!info.defaultPrevented,key:info.key||'',"
        "inputType:info.inputType||'',data:info.data||'',"
        "isComposing:!!info.isComposing,"
        "keyCode:info.keyCode||0,charCode:info.charCode||0,"
        "repeat:!!info.repeat,shiftKey:!!info.shiftKey,"
        "ctrlKey:!!info.ctrlKey,altKey:!!info.altKey,"
        "target:info.targetId?new PElement(info.targetId):null,"
        "currentTarget:info.currentTargetId?"
        "new PElement(info.currentTargetId):null};"
        "e.preventDefault=function(){if(e.cancelable){"
        "e.defaultPrevented=true;}};"
        "fn.call(null,e);return e.defaultPrevented;};"
        "PElement.prototype.addEventListener=function(type,fn,capture){"
        "var t=String(type);var c=!!capture;var n;"
        "if(typeof fn!=='function'){return 0;}"
        "n=__pcoreAddEvent({id:this.__id,type:t,capture:c?1:0});"
        "if(n>0){g.__pcoreHandlers[n]=fn;"
        "if(!this.__listeners){this.__listeners=[];}"
        "this.__listeners.push({id:n,type:t,fn:fn,capture:c});}"
        "return n;};"
        "PElement.prototype.removeEventListener=function(type,fn,capture){"
        "var t=String(type);var c=!!capture;var a=this.__listeners||[];"
        "var i;"
        "for(i=0;i<a.length;i++){if(a[i].type===t&&a[i].fn===fn&&"
        "a[i].capture===c){__pcoreRemoveEvent({listener:a[i].id});"
        "delete g.__pcoreHandlers[a[i].id];a.splice(i,1);return;}}};"
        "var purl=String(g.__pcoreDocumentUrl||'');"
        "var phistoryLength=Number(g.__pcoreHistoryLength||1);"
        "var phistoryStateJson=JSON.stringify(g.__pcoreHistoryState);"
        "if(typeof phistoryStateJson!=='string'){phistoryStateJson='null';}"
        "try{delete g.__pcoreDocumentUrl;}catch(purlerror){}"
        "try{delete g.__pcoreHistoryLength;}catch(phistoryerror){}"
        "try{delete g.__pcoreHistoryState;}catch(phistorystateerror){}"
        "function ppartial(v,min){var u=String(v);var l=u.toLowerCase();"
        "var q=u.indexOf('?');var h=u.indexOf('#');var a;var n;var z;var s;"
        "if(q<0||(h>=0&&h<q)){q=h;}a=l.indexOf('/.%2e');"
        "n=l.indexOf('/%2e.');if(a<0||(n>=0&&n<a)){a=n;}"
        "if(a<=min||(q>=0&&a>=q)||u.indexOf('/./')>=0||"
        "u.indexOf('/../')>=0||l.indexOf('/%2e/')>=0||"
        "l.indexOf('/%2e%2e')>=0){return null;}z=a+5;"
        "if(!((l.charAt(z)==='/'&&(q<0||z<q))||"
        "(q>=0&&z===q)||(q<0&&z===u.length))){return null;}"
        "s=l.indexOf('/.%2e',z);n=l.indexOf('/%2e.',z);"
        "if(s<0||(n>=0&&n<s)){s=n;}"
        "if(s>=0&&(q<0||s<q)){return null;}s=u.lastIndexOf('/',a-1);"
        "if(s<min||s+1>=a){return null;}return u.substring(0,s+1)+"
        "u.substring(z+(l.charAt(z)==='/'?1:0));}"
        "function pdouble(v,min,many){var u=String(v);var l=u.toLowerCase();"
        "var q=u.indexOf('?');var h=u.indexOf('#');var a;var s;var z;var n=0;"
        "if(q<0||(h>=0&&h<q)){q=h;}if(u.indexOf('/./')>=0){return null;}"
        "for(;;){a=l.indexOf('/%2e%2e');if(a<0||(q>=0&&a>=q)){break;}"
        "z=a+7;if(a<=min||!((l.charAt(z)==='/'&&(q<0||z<q))||"
        "(q>=0&&z===q)||(q<0&&z===u.length))){return null;}"
        "n++;if(!many&&n>1){return null;}s=u.lastIndexOf('/',a-1);"
        "if(s<min||s+1>=a){return null;}if(l.charAt(z)==='/'){z++;}"
        "if(q>=0){q-=z-s-1;}u=u.substring(0,s+1)+u.substring(z);"
        "l=u.toLowerCase();}return n>0?u:null;}"
        "function pfragmentReference(v){var h=purl.indexOf('#');"
        "var b=h>=0?purl.substring(0,h):purl;var s;var ps;var q;var ls;"
        "var u;var lower;var next;"
        "if(v.charAt(0)==='#'){return b+v;}if(v.indexOf(b+'#')===0)"
        "{return v;}if(h>=0&&v===b){return v;}s=v.indexOf('://');"
        "ps=s>=0?v.indexOf('/',s+3):-1;if(ps>=0){u=v;q=u.indexOf('?');"
        "ls=u.indexOf('#');if(q<0||(ls>=0&&ls<q)){q=ls;}"
        "ls=u.indexOf('/./');if(ls>=0&&(q<0||ls<q)){"
        "while(ls>=0&&(q<0||ls<q)){u=u.substring(0,ls+1)"
        "+u.substring(ls+3);if(q>=0){q-=2;}ls=u.indexOf('/./');}"
        "if(u.indexOf(b+'#')===0){return u;}if(h>=0&&u===b){return u;}}"
        "lower=u.toLowerCase();ls=lower.indexOf('/%2e/');"
        "if(v.indexOf('/./')<0&&ls>=0&&(q<0||ls<q)){"
        "next=lower.indexOf('/%2e');while(next>=0&&(q<0||next<q)&&"
        "lower.charAt(next+4)==='/'){next=lower.indexOf('/%2e',next+5);}"
        "if(next<0||(q>=0&&next>=q)){while(ls>=0&&(q<0||ls<q)){"
        "u=u.substring(0,ls+1)+u.substring(ls+5);if(q>=0){q-=4;}"
        "lower=u.toLowerCase();ls=lower.indexOf('/%2e/');}"
        "if(u.indexOf(b+'#')===0){return u;}"
        "if(h>=0&&u===b){return u;}}}"
        "next=pdouble(v,ps,true);if(next!==null){u=next;q=u.indexOf('?');"
        "ls=u.indexOf('#');if(q<0||(ls>=0&&ls<q)){q=ls;}"
        "if(u.indexOf(b+'#')===0){return u;}"
        "if(h>=0&&u===b){return u;}}lower=u.toLowerCase();"
        "ls=lower.indexOf('/%2e');if(v.indexOf('/./')<0&&ls>=0&&"
        "((q>=0&&ls+4===q)||(q<0&&ls+4===u.length))){"
        "u=u.substring(0,ls+1)+u.substring(ls+4);"
        "if(u.indexOf(b+'#')===0){return u;}"
        "if(h>=0&&u===b){return u;}}"
        "u=ppartial(v,ps);if(u!==null){"
        "if(u.indexOf(b+'#')===0){return u;}"
        "if(h>=0&&u===b){return u;}}}"
        "if(v.charAt(0)==='/'){"
        "s=b.indexOf('://');ps=s>=0?b.indexOf('/',s+3):-1;"
        "if(ps>=0){u=v;q=u.indexOf('?');ls=u.indexOf('#');"
        "if(q<0||(ls>=0&&ls<q)){q=ls;}ls=u.indexOf('/./');"
        "while(ls>=0&&(q<0||ls<q)){u=u.substring(0,ls+1)"
        "+u.substring(ls+3);if(q>=0){q-=2;}ls=u.indexOf('/./');}"
        "lower=u.toLowerCase();ls=lower.indexOf('/%2e/');"
        "if(v.indexOf('/./')<0&&ls>=0&&(q<0||ls<q)){"
        "next=lower.indexOf('/%2e');"
        "while(next>=0&&(q<0||next<q)&&lower.charAt(next+4)==='/'){"
        "next=lower.indexOf('/%2e',next+5);}"
        "if(next<0||(q>=0&&next>=q)){while(ls>=0&&(q<0||ls<q)){"
        "u=u.substring(0,ls+1)+u.substring(ls+5);if(q>=0){q-=4;}"
        "lower=u.toLowerCase();ls=lower.indexOf('/%2e/');}}}"
        "next=pdouble(v,0,true);if(next!==null){u=next;q=u.indexOf('?');"
        "ls=u.indexOf('#');if(q<0||(ls>=0&&ls<q)){q=ls;}}"
        "lower=u.toLowerCase();ls=lower.indexOf('/%2e');"
        "if(v.indexOf('/./')<0&&ls>=0&&"
        "((q>=0&&ls+4===q)||(q<0&&ls+4===u.length))){"
        "u=u.substring(0,ls+1)+u.substring(ls+4);}"
        "lower=String(v).toLowerCase();if(lower.indexOf('/.%2e')>=0||"
        "lower.indexOf('/%2e.')>=0){next=ppartial(v,0);"
        "u=next===null?v:next;}"
        "u=b.substring(0,ps)+u;if(u.indexOf(b+'#')===0)"
        "{return u;}if(h>=0&&u===b){return u;}}}if(v.charAt(0)==='?'){"
        "q=b.indexOf('?');u=(q>=0?b.substring(0,q):b)+v;"
        "if(u.indexOf(b+'#')===0){return u;}if(h>=0&&u===b){return u;}"
        "}if(v.substring(0,2)==='./'){q=b.indexOf('?');"
        "ls=b.lastIndexOf('/',q>=0?q-1:b.length-1);if(ls>=0){"
        "u=v;while(u.substring(0,2)==='./'){u=u.substring(2);}"
        "u=b.substring(0,ls+1)+u;"
        "if(u.indexOf(b+'#')===0){return u;}if(h>=0&&u===b){return u;}"
        "}}if(v.substring(0,3)==='../'){q=b.indexOf('?');"
        "s=b.indexOf('://');ps=s>=0?b.indexOf('/',s+3):-1;"
        "ls=b.lastIndexOf('/',q>=0?q-1:b.length-1);if(ps>=0&&ls>=ps){"
        "u=v;while(u.substring(0,3)==='../'){"
        "if(ls>ps){ls=b.lastIndexOf('/',ls-1);}else{ls=ps;}"
        "u=u.substring(3);}while(u.substring(0,2)==='./'){u=u.substring(2);}"
        "q=u.indexOf('?');s=u.indexOf('#');"
        "if(q<0||(s>=0&&s<q)){q=s;}s=u.indexOf('/./');"
        "while(s>=0&&(q<0||s<q)){u=u.substring(0,s+1)+u.substring(s+3);"
        "if(q>=0){q-=2;}s=u.indexOf('/./');}"
        "u=b.substring(0,ls+1)+u;"
        "if(u.indexOf(b+'#')===0){return u;}if(h>=0&&u===b){return u;}"
        "}}if(v.length>0&&v.charAt(0)!=='/'&&v.charAt(0)!=='?'&&"
        "v.charAt(0)!=='#'&&v.substring(0,2)!=='./'&&"
        "v.substring(0,3)!=='../'){q=b.indexOf('?');"
        "ls=b.lastIndexOf('/',q>=0?q-1:b.length-1);if(ls>=0){"
        "u=b.substring(0,ls+1)+v;if(u.indexOf(b+'#')===0){return u;}"
        "if(h>=0&&u===b){return u;}}}return null;}"
        "function pqueueFragment(v,replace){var u=pfragmentReference(v);"
        "if(u===null){return false;}if(u===purl){return true;}"
        "if(!__pcoreNavigation({op:replace?'fragmentReplace':'fragment',"
        "url:u})){throw new Error('location fragment navigation failed');}"
        "return true;}"
        "function pnavigate(v){var s=String(v);if(pqueueFragment(s,false))"
        "{return;}if(!__pcoreNavigation({op:'assign',url:s})){"
        "throw new Error('location navigation failed');}}"
        "function preload(){if(!__pcoreNavigation({op:'reload',url:purl}))"
        "{throw new Error('location reload failed');}}"
        "function preplace(v){var s=String(v);if(pqueueFragment(s,true))"
        "{return;}if(!__pcoreNavigation({op:'replace',url:s})){"
        "throw new Error('location replace failed');}}"
        "function phistoryOrigin(v){var s=String(v);var i=s.indexOf('://');"
        "var p;var q;var h;if(i<0){return '';}i+=3;p=s.indexOf('/',i);"
        "q=s.indexOf('?',i);h=s.indexOf('#',i);"
        "if(p<0||(q>=0&&q<p)){p=q;}if(p<0||(h>=0&&h<p)){p=h;}"
        "return s.substring(0,p<0?s.length:p);}"
        "function phistoryPath(v){var s=String(v);var i=s.indexOf('://');"
        "var start;var q;var h;var end;if(i<0){return '';}i+=3;"
        "start=s.indexOf('/',i);end=s.length;q=s.indexOf('?',i);"
        "h=s.indexOf('#',i);if(q>=0&&q<end){end=q;}"
        "if(h>=0&&h<end){end=h;}if(start<0||start>=end){return '/';}"
        "return s.substring(start,end);}"
        "function phistoryOriginEquivalent(a,b){var x=String(a).toLowerCase();"
        "var y=String(b).toLowerCase();var i=x.indexOf('://');"
        "var j=y.indexOf('://');var xa;var ya;var p;"
        "if(i<0||j!==i||x.substring(0,i)!==y.substring(0,j)){return false;}"
        "xa=x.substring(i+3);ya=y.substring(j+3);p=xa.lastIndexOf(':');"
        "if(p>=0&&p===xa.indexOf(':')&&((i===4&&xa.substring(p+1)==='80')||"
        "(i===5&&xa.substring(p+1)==='443'))){xa=xa.substring(0,p);}"
        "p=ya.lastIndexOf(':');if(p>=0&&p===ya.indexOf(':')&&"
        "((i===4&&ya.substring(p+1)==='80')||"
        "(i===5&&ya.substring(p+1)==='443'))){ya=ya.substring(0,p);}"
        "return xa===ya;}"
        "function phistoryRelativePath(p){var l=String(p).toLowerCase();"
        "var i=0;var slash;var segment;"
        "if(p===''||p==='.'||p==='..'||p.substring(0,2)==='./'||"
        "p.substring(0,3)==='../'||p.indexOf('//')>=0||"
        "p.indexOf('/./')>=0||p.indexOf('/../')>=0||"
        "(p.length>=2&&p.substring(p.length-2)==='/.')||"
        "(p.length>=3&&p.substring(p.length-3)==='/..')){return false;}"
        "if(l.indexOf('%2e')>=0){"
        "while(i<=l.length){slash=l.indexOf('/',i);"
        "if(slash<0){slash=l.length;}segment=l.substring(i,slash);"
        "if(segment==='%2e'||segment==='%2e%2e'||"
        "segment==='.%2e'||segment==='%2e.'){return false;}"
        "if(slash===l.length){break;}i=slash+1;}}return true;}"
        "function phistoryUrl(url,provided){var u=provided&&"
        "typeof url!=='undefined'?String(url):'';"
        "var h;var b;var q;var pathEnd;var slash;var origin;var explicit;"
        "if(u===''){return purl;}"
        "h=purl.indexOf('#');b=h>=0?purl.substring(0,h):purl;"
        "if(u.charAt(0)==='#'){return b+u;}"
        "if(u.charAt(0)==='?'){q=b.indexOf('?');"
        "return(q>=0?b.substring(0,q):b)+u;}"
        "if(u.substring(0,2)==='//'){throw new Error('history state URL unsupported');}"
        "if(u.charAt(0)==='/'){origin=phistoryOrigin(purl);"
        "if(origin===''){throw new Error('history state URL unsupported');}"
        "q=u.indexOf('?');h=u.indexOf('#');pathEnd=u.length;"
        "if(q>=0&&q<pathEnd){pathEnd=q;}if(h>=0&&h<pathEnd){pathEnd=h;}"
        "if(!phistoryRelativePath(u.substring(0,pathEnd))){"
        "throw new Error('history state URL unsupported');}"
        "return origin+u;}"
        "if(u.indexOf('://')>=0){origin=phistoryOrigin(purl);"
        "if(origin===''||!phistoryOriginEquivalent(origin,phistoryOrigin(u)))"
        "{throw new Error('history state URL unsupported');}"
        "if(u===b||u.indexOf(b+'#')===0){return u;}"
        "if(phistoryPath(u)!==phistoryPath(purl)&&"
        "!phistoryRelativePath(phistoryPath(u))){"
        "throw new Error('history state URL unsupported');}return u;}"
        "explicit=0;if(u.substring(0,2)==='./'){u=u.substring(2);"
        "explicit=1;}"
        "if(u===b||u.indexOf(b+'#')===0){return u;}"
        "q=u.indexOf('?');h=u.indexOf('#');pathEnd=u.length;"
        "if(q>=0&&q<pathEnd){pathEnd=q;}if(h>=0&&h<pathEnd){pathEnd=h;}"
        "if(pathEnd===0&&explicit&&(u.charAt(0)==='?'||"
        "u.charAt(0)==='#')){q=b.indexOf('?');if(q<0){q=b.length;}"
        "slash=b.lastIndexOf('/',q-1);if(slash>=0){"
        "return b.substring(0,slash+1)+u;}}"
        "if(pathEnd>0&&u.substring(0,pathEnd)!=='.'&&"
        "u.substring(0,pathEnd)!=='..'&&"
        "phistoryRelativePath(u.substring(0,pathEnd))){"
        "q=b.indexOf('?');if(q<0){q=b.length;}slash=b.lastIndexOf('/',q-1);"
        "if(slash>=0){return b.substring(0,slash+1)+u;}}"
        "throw new Error('history state URL unsupported');}"
        "function preplaceState(state,title,url){var u=phistoryUrl(url,"
        "arguments.length>2);var s;var clone;"
        "s=JSON.stringify(state);if(typeof s!=='string'||s.length>=1024){"
        "throw new Error('history state unsupported');}clone=JSON.parse(s);"
        "if(!__pcoreNavigation({op:'replaceState',state:clone,url:u})){"
        "throw new Error('history state failed');}phistoryStateJson=s;purl=u;}"
        "function ppushState(state,title,url){var u=phistoryUrl(url,"
        "arguments.length>2);var s;var clone;var n;"
        "s=JSON.stringify(state);if(typeof s!=='string'||s.length>=1024){"
        "throw new Error('history state unsupported');}clone=JSON.parse(s);"
        "n=Number(__pcoreNavigation({op:'pushState',state:clone,url:u}));"
        "if(!isFinite(n)||Math.floor(n)!==n||n<1||n>16){"
        "throw new Error('history push state failed');}"
        "phistoryStateJson=s;phistoryLength=n;purl=u;}"
        "var pwindowListeners={popstate:[],hashchange:[]};"
        "g.onpopstate=null;g.onhashchange=null;"
        "g.addEventListener=function(type,fn,capture){var t=String(type);"
        "var a;var i;if((t!=='popstate'&&t!=='hashchange')||"
        "typeof fn!=='function'){return;}a=pwindowListeners[t];"
        "for(i=0;i<a.length;i++){if(a[i]===fn){return;}}a.push(fn);};"
        "g.removeEventListener=function(type,fn,capture){var t=String(type);"
        "var a;var i;if(t!=='popstate'&&t!=='hashchange'){return;}"
        "a=pwindowListeners[t];for(i=a.length-1;i>=0;i--){"
        "if(a[i]===fn){a.splice(i,1);}}};"
        "function pdispatchWindow(type,e){var h=g['on'+type];"
        "var a=pwindowListeners[type].slice(0);var i;"
        "if(typeof h==='function'){try{h.call(g,e);}catch(handlerError){}}"
        "for(i=0;i<a.length;i++){try{a[i].call(g,e);}"
        "catch(listenerError){}}}"
        "function pdispatchPopState(s){var e={type:'popstate',"
        "state:JSON.parse(s),target:g,currentTarget:g,bubbles:false,"
        "cancelable:false,defaultPrevented:false,isTrusted:true};"
        "e.preventDefault=function(){};pdispatchWindow('popstate',e);}"
        "function pfragment(u){var h=u.indexOf('#');"
        "return h<0?'':u.substring(h);}"
        "function pdispatchHashChange(oldUrl,newUrl){var e={"
        "type:'hashchange',oldURL:oldUrl,newURL:newUrl,target:g,"
        "currentTarget:g,bubbles:false,cancelable:false,"
        "defaultPrevented:false,isTrusted:true};e.preventDefault=function(){};"
        "pdispatchWindow('hashchange',e);}"
        "Object.defineProperty(g,'__pcoreHistoryTraverse',{"
        "value:function(state,url){var s=JSON.stringify(state);var oldUrl=purl;"
        "var newUrl=String(url);if(typeof s!=='string'){s='null';}"
        "purl=newUrl;phistoryStateJson=s;pdispatchPopState(s);"
        "if(pfragment(oldUrl)!==pfragment(newUrl)){"
        "pdispatchHashChange(oldUrl,newUrl);}},"
        "writable:false,configurable:false});"
        "Object.defineProperty(g,'__pcoreHashNavigate',{"
        "value:function(url,length){var oldUrl=purl;var newUrl=String(url);"
        "purl=newUrl;phistoryStateJson='null';phistoryLength=Number(length);"
        "if(pfragment(oldUrl)!==pfragment(newUrl)){"
        "pdispatchHashChange(oldUrl,newUrl);}},"
        "writable:false,configurable:false});"
        "function purlParts(){var m=/^([A-Za-z][A-Za-z0-9+.-]*:)"
        "(?:\\/\\/([^\\/?#]*))?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(purl);"
        "var protocol='';var host='';var hostname='';var port='';"
        "var pathname='';var search='';var hash='';var origin='null';"
        "var colon;var close;if(m){protocol=m[1]||'';host=m[2]||'';"
        "pathname=m[3]||'';search=m[4]||'';hash=m[5]||'';}"
        "if(pathname===''){pathname='/';}if(host.charAt(0)==='['){"
        "close=host.indexOf(']');if(close>=0){hostname=host.substring(0,"
        "close+1);if(host.charAt(close+1)===':'){"
        "port=host.substring(close+2);}}}else{colon=host.lastIndexOf(':');"
        "if(colon>=0&&host.indexOf(':')===colon){hostname=host.substring(0,"
        "colon);port=host.substring(colon+1);}else{hostname=host;}}"
        "if((protocol==='http:'||protocol==='https:')&&host!==''){"
        "origin=protocol+'//'+host;}return {protocol:protocol,host:host,"
        "hostname:hostname,port:port,pathname:pathname,search:search,"
        "hash:hash,origin:origin};}"
        "function phashNavigate(value){var v=String(value);"
        "var h=purl.indexOf('#');var b=h>=0?purl.substring(0,h):purl;var u;"
        "if(v===''){u=b;}else{if(v.charAt(0)!=='#'){v='#'+v;}u=b+v;}"
        "if(u===purl){return;}if(!__pcoreNavigation({op:'fragment',url:u})){"
        "throw new Error('location hash navigation failed');}}"
        "var plocation={};"
        "Object.defineProperty(plocation,'href',{get:function(){"
        "return purl;},set:pnavigate});"
        "function pdefineLocationPart(name){Object.defineProperty(plocation,"
        "name,{get:function(){return purlParts()[name];}});}"
        "pdefineLocationPart('protocol');pdefineLocationPart('host');"
        "pdefineLocationPart('hostname');pdefineLocationPart('port');"
        "pdefineLocationPart('pathname');pdefineLocationPart('search');"
        "Object.defineProperty(plocation,'hash',{get:function(){"
        "return purlParts().hash;},set:phashNavigate});"
        "pdefineLocationPart('origin');"
        "plocation.assign=pnavigate;"
        "plocation.reload=preload;"
        "plocation.replace=preplace;"
        "plocation.toString=function(){return this.href;};"
        "var pdocument={getElementById:function(id){id=String(id);"
        "return __pcoreHasElement({id:id})?new PElement(id):null;}};"
        "Object.defineProperty(pdocument,'URL',{get:function(){"
        "return plocation.href;}});"
        "Object.defineProperty(pdocument,'documentURI',{get:function(){"
        "return plocation.href;}});"
        "Object.defineProperty(pdocument,'location',{get:function(){"
        "return plocation;},set:pnavigate});"
        "g.window=g;g.document=pdocument;"
        "Object.defineProperty(g,'location',{get:function(){"
        "return plocation;},set:pnavigate});"
        "var phistory={back:function(){__pcoreNavigation({op:'back'});},"
        "forward:function(){__pcoreNavigation({op:'forward'});},"
        "go:function(delta){var n=Number(arguments.length?delta:0);"
        "if(!isFinite(n)||Math.floor(n)!==n||n < -15||n > 15){return;}"
        "__pcoreNavigation({op:'go',delta:n});}};"
        "Object.defineProperty(phistory,'length',{get:function(){"
        "return phistoryLength;},enumerable:true});"
        "Object.defineProperty(phistory,'state',{get:function(){"
        "return JSON.parse(phistoryStateJson);},enumerable:true});"
        "phistory.replaceState=preplaceState;"
        "phistory.pushState=ppushState;g.history=phistory;"
        "})(this);";
PBROWSER_API int PBrowser_ScriptSessionEvaluateBootstrap(HANDLE hSession)
{
    return PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP, -1);
}
typedef struct p_browser_script_dom_read_binding {
    PBrowserScriptDomReadCallbacks callbacks;
} p_browser_script_dom_read_binding;

typedef struct p_browser_script_dom_write_binding {
    PBrowserScriptDomWriteCallbacks callbacks;
} p_browser_script_dom_write_binding;

typedef struct p_browser_script_dom_value_binding {
    PBrowserScriptDomValueCallbacks callbacks;
} p_browser_script_dom_value_binding;

typedef struct p_browser_script_dom_checked_binding {
    PBrowserScriptDomCheckedCallbacks callbacks;
} p_browser_script_dom_checked_binding;

typedef struct p_browser_script_form_binding {
    PBrowserScriptFormCallbacks callbacks;
} p_browser_script_form_binding;

typedef struct p_browser_script_validation_binding {
    PBrowserScriptValidationCallbacks callbacks;
} p_browser_script_validation_binding;

typedef struct p_browser_script_report_validity_binding {
    PBrowserScriptReportValidityCallbacks callbacks;
} p_browser_script_report_validity_binding;

typedef struct p_browser_script_custom_validity_binding {
    PBrowserScriptCustomValidityCallbacks callbacks;
} p_browser_script_custom_validity_binding;

typedef struct p_browser_script_input_binding {
    PBrowserScriptInputCallbacks callbacks;
} p_browser_script_input_binding;

typedef struct p_browser_script_key_binding {
    PBrowserScriptKeyCallbacks callbacks;
} p_browser_script_key_binding;

typedef struct p_browser_script_focus_binding {
    PBrowserScriptFocusCallbacks callbacks;
} p_browser_script_focus_binding;

typedef struct p_browser_script_edit_binding {
    PBrowserScriptEditCallbacks callbacks;
} p_browser_script_edit_binding;

typedef struct p_browser_script_select_binding {
    PBrowserScriptSelectCallbacks callbacks;
} p_browser_script_select_binding;

typedef struct p_browser_script_click_binding {
    PBrowserScriptClickCallbacks callbacks;
} p_browser_script_click_binding;

typedef struct p_browser_script_programmatic_click_binding {
    HANDLE session;
    PBrowserScriptProgrammaticClickCallbacks callbacks;
} p_browser_script_programmatic_click_binding;

typedef struct p_browser_script_form_event_binding {
    PBrowserScriptFormEventCallbacks callbacks;
} p_browser_script_form_event_binding;

typedef struct p_browser_script_invalid_binding {
    PBrowserScriptInvalidCallbacks callbacks;
} p_browser_script_invalid_binding;

typedef struct p_browser_script_navigation_binding {
    PBrowserScriptNavigationCallbacks callbacks;
} p_browser_script_navigation_binding;

typedef struct p_browser_script_dom_attribute_binding {
    PBrowserScriptDomAttributeCallbacks callbacks;
} p_browser_script_dom_attribute_binding;

typedef struct p_browser_script_event_binding {
    PBrowserScriptEventCallbacks callbacks;
} p_browser_script_event_binding;

typedef struct p_browser_script_session {
    HANDLE runtime;
    p_browser_script_dom_read_binding *dom_read;
    p_browser_script_dom_write_binding *dom_write;
    p_browser_script_dom_value_binding *dom_value;
    p_browser_script_dom_checked_binding *dom_checked;
    p_browser_script_form_binding *form;
    p_browser_script_validation_binding *validation;
    p_browser_script_report_validity_binding *report_validity;
    p_browser_script_custom_validity_binding *custom_validity;
    p_browser_script_input_binding *input;
    p_browser_script_key_binding *key;
    p_browser_script_focus_binding *focus;
    p_browser_script_edit_binding *edit;
    p_browser_script_select_binding *select;
    p_browser_script_click_binding *click;
    p_browser_script_programmatic_click_binding *programmatic_click;
    p_browser_script_form_event_binding *form_event;
    p_browser_script_invalid_binding *invalid;
    p_browser_script_navigation_binding *navigation;
    p_browser_script_dom_attribute_binding *dom_attribute;
    p_browser_script_event_binding *event;
} p_browser_script_session;

static p_browser_script_session *p_script_session(HANDLE hSession)
{
    return (p_browser_script_session *) hSession;
}

static int p_script_session_valid(
        const p_browser_script_session *session)
{
    return session != NULL && session->runtime != NULL;
}

static void p_browser_script_clear_dispatch_globals(
        p_browser_script_session *session, int traversal)
{
    if (session == NULL || session->runtime == NULL) {
        return;
    }
    if (traversal) {
        (void) PScript_Evaluate(session->runtime,
                "delete this.__pcoreHistoryTraversalState;"
                "delete this.__pcoreHistoryTraversalUrl;", -1);
    } else {
        (void) PScript_Evaluate(session->runtime,
                "delete this.__pcoreHashNavigationUrl;"
                "delete this.__pcoreHashNavigationLength;", -1);
    }
}

static int p_browser_script_dispatch_history_traversal(
        p_browser_script_session *session, const char *state_json,
        const char *url)
{
    int rc;

    if (session == NULL || state_json == NULL || url == NULL ||
            state_json[0] == '\0' || url[0] == '\0' ||
            strlen(state_json) >= PBROWSER_HISTORY_STATE_MAX ||
            strlen(url) >= PBROWSER_HISTORY_URL_MAX) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = PScript_SetGlobalJson(session->runtime,
            "__pcoreHistoryTraversalState", -1, state_json, -1);
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    rc = PScript_SetGlobalString(session->runtime,
            "__pcoreHistoryTraversalUrl", -1, url, -1);
    if (rc != PSCRIPT_OK) {
        p_browser_script_clear_dispatch_globals(session, 1);
        return rc;
    }
    rc = PScript_Evaluate(session->runtime,
            "__pcoreHistoryTraverse(__pcoreHistoryTraversalState,"
            "__pcoreHistoryTraversalUrl);", -1);
    p_browser_script_clear_dispatch_globals(session, 1);
    return rc;
}

static int p_browser_script_dispatch_hash_navigation(
        p_browser_script_session *session, const char *url,
        int history_length)
{
    int rc;

    if (session == NULL || url == NULL || url[0] == '\0' ||
            strlen(url) >= PBROWSER_HISTORY_URL_MAX ||
            history_length < 1 || history_length > PBROWSER_HISTORY_MAX) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = PScript_SetGlobalString(session->runtime,
            "__pcoreHashNavigationUrl", -1, url, -1);
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    rc = PScript_SetGlobalNumber(session->runtime,
            "__pcoreHashNavigationLength", -1,
            (double) history_length);
    if (rc != PSCRIPT_OK) {
        p_browser_script_clear_dispatch_globals(session, 0);
        return rc;
    }
    rc = PScript_Evaluate(session->runtime,
            "__pcoreHashNavigate(__pcoreHashNavigationUrl,"
            "__pcoreHashNavigationLength);", -1);
    p_browser_script_clear_dispatch_globals(session, 0);
    return rc;
}

#define PBROWSER_SCRIPT_TEXT_MAX_BYTES 65535

static HANDLE p_browser_script_args_object(const char *args_json,
        int args_len, HANDLE *out_object)
{
    HANDLE root;
    HANDLE object;
    char *copy;

    if (out_object == NULL) {
        return NULL;
    }
    *out_object = NULL;
    if (args_json == NULL || args_len < 0) {
        return NULL;
    }
    copy = (char *) malloc((size_t) args_len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, args_json, (size_t) args_len);
    copy[args_len] = '\0';
    root = PJson_Parse(copy);
    free(copy);
    if (root == NULL || PJson_GetArraySize(root) != 1) {
        PJson_Free(root);
        return NULL;
    }
    object = PJson_GetArrayItem(root, 0);
    if (object == NULL) {
        PJson_Free(root);
        return NULL;
    }
    *out_object = object;
    return root;
}

static int p_browser_script_write_bool(int value, char *out_json,
        int out_capacity, int *out_len)
{
    const char *word;
    int length;

    word = value ? "true" : "false";
    length = value ? 4 : 5;
    if (out_json == NULL || out_len == NULL || out_capacity <= length) {
        return 1;
    }
    memcpy(out_json, word, (size_t) length + 1);
    *out_len = length;
    return 0;
}

static int p_browser_script_write_int(int value, char *out_json,
        int out_capacity, int *out_len)
{
    char number[24];
    int length;

    if (out_json == NULL || out_len == NULL || out_capacity <= 0) {
        return 1;
    }
    length = _snprintf(number, sizeof(number) - 1, "%d", value);
    if (length < 0 || length >= (int) sizeof(number) - 1 ||
            length >= out_capacity) {
        return 1;
    }
    number[length] = '\0';
    memcpy(out_json, number, (size_t) length + 1);
    *out_len = length;
    return 0;
}

static int p_browser_script_write_null(char *out_json, int out_capacity,
        int *out_len)
{
    if (out_json == NULL || out_len == NULL || out_capacity < 5) {
        return 1;
    }
    memcpy(out_json, "null", 5);
    *out_len = 4;
    return 0;
}

static int p_browser_script_write_validation(
        const PBrowserScriptValidationInfo *info, char *out_json,
        int out_capacity, int *out_len)
{
    unsigned int flags;
    int length;

    if (info == NULL || out_json == NULL || out_len == NULL ||
            out_capacity <= 0) {
        return 1;
    }
    flags = info->flags;
    length = _snprintf(out_json, out_capacity - 1,
            "{\"valid\":%s,\"willValidate\":%s,"
            "\"valueMissing\":%s,\"tooLong\":%s,"
            "\"tooShort\":%s,\"patternMismatch\":%s,"
            "\"badInput\":%s,\"rangeUnderflow\":%s,"
            "\"rangeOverflow\":%s,\"stepMismatch\":%s,"
            "\"typeMismatch\":%s,\"customError\":%s}",
            info->valid ? "true" : "false",
            info->will_validate ? "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_VALUE_MISSING) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_TOO_LONG) ? "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_TOO_SHORT) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_PATTERN_MISMATCH) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_BAD_INPUT) ? "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_RANGE_UNDERFLOW) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_RANGE_OVERFLOW) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_STEP_MISMATCH) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_TYPE_MISMATCH) ?
                    "true" : "false",
            (flags & PBROWSER_SCRIPT_VALIDITY_CUSTOM_ERROR) ?
                    "true" : "false");
    if (length < 0 || length >= out_capacity - 1) {
        return 1;
    }
    out_json[length] = '\0';
    *out_len = length;
    return 0;
}

static int p_browser_script_json_escape(const char *value, char *out,
        int capacity)
{
    static const char HEX[] = "0123456789abcdef";
    const unsigned char *p;
    unsigned char c;
    unsigned int codepoint;
    unsigned int high;
    unsigned int low;
    int used;

    if (out == NULL || capacity <= 0) {
        return -1;
    }
    if (value == NULL) {
        value = "";
    }
    used = 0;
    for (p = (const unsigned char *) value; *p != '\0'; p++) {
        c = *p;
        if (c == '"' || c == '\\') {
            if (used + 2 >= capacity) {
                return -1;
            }
            out[used++] = '\\';
            out[used++] = (char) c;
        } else if (c == '\b' || c == '\f' || c == '\n' ||
                c == '\r' || c == '\t') {
            if (used + 2 >= capacity) {
                return -1;
            }
            out[used++] = '\\';
            if (c == '\b') {
                out[used++] = 'b';
            } else if (c == '\f') {
                out[used++] = 'f';
            } else if (c == '\n') {
                out[used++] = 'n';
            } else if (c == '\r') {
                out[used++] = 'r';
            } else {
                out[used++] = 't';
            }
        } else if (c < 0x20) {
            if (used + 6 >= capacity) {
                return -1;
            }
            out[used++] = '\\';
            out[used++] = 'u';
            out[used++] = '0';
            out[used++] = '0';
            out[used++] = HEX[c >> 4];
            out[used++] = HEX[c & 0x0f];
        } else if (c >= 0xf0 && c <= 0xf4 && p[1] != '\0' &&
                p[2] != '\0' && p[3] != '\0' &&
                (p[1] & 0xc0) == 0x80 &&
                (p[2] & 0xc0) == 0x80 &&
                (p[3] & 0xc0) == 0x80 &&
                !(c == 0xf0 && p[1] < 0x90) &&
                !(c == 0xf4 && p[1] > 0x8f)) {
            if (used + 12 >= capacity) {
                return -1;
            }
            codepoint = ((unsigned int) (c & 0x07) << 18) |
                    ((unsigned int) (p[1] & 0x3f) << 12) |
                    ((unsigned int) (p[2] & 0x3f) << 6) |
                    (unsigned int) (p[3] & 0x3f);
            codepoint -= 0x10000U;
            high = 0xd800U + (codepoint >> 10);
            low = 0xdc00U + (codepoint & 0x3ffU);
            out[used++] = '\\';
            out[used++] = 'u';
            out[used++] = HEX[(high >> 12) & 0x0f];
            out[used++] = HEX[(high >> 8) & 0x0f];
            out[used++] = HEX[(high >> 4) & 0x0f];
            out[used++] = HEX[high & 0x0f];
            out[used++] = '\\';
            out[used++] = 'u';
            out[used++] = HEX[(low >> 12) & 0x0f];
            out[used++] = HEX[(low >> 8) & 0x0f];
            out[used++] = HEX[(low >> 4) & 0x0f];
            out[used++] = HEX[low & 0x0f];
            p += 3;
        } else {
            if (used + 1 >= capacity) {
                return -1;
            }
            out[used++] = (char) c;
        }
    }
    out[used] = '\0';
    return used;
}

static int p_browser_script_event_type_safe(const char *event_type)
{
    const char *p;
    char c;

    if (event_type == NULL || event_type[0] == '\0') {
        return 0;
    }
    for (p = event_type; *p != '\0'; p++) {
        c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == ':' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

static int p_browser_script_focus_event_type_safe(const char *event_type)
{
    if (!p_browser_script_event_type_safe(event_type)) {
        return 0;
    }
    return strcmp(event_type, "focus") == 0 ||
            strcmp(event_type, "blur") == 0 ||
            strcmp(event_type, "focusin") == 0 ||
            strcmp(event_type, "focusout") == 0;
}

static int p_browser_script_select_event_type_safe(const char *event_type)
{
    return event_type != NULL && (strcmp(event_type, "input") == 0 ||
            strcmp(event_type, "change") == 0);
}

static int p_browser_script_edit_event_type_safe(const char *event_type)
{
    return event_type != NULL && strcmp(event_type, "change") == 0;
}

static int p_browser_script_click_event_type_safe(const char *event_type)
{
    return event_type != NULL && strcmp(event_type, "click") == 0;
}

static int p_browser_script_form_event_type_safe(const char *event_type)
{
    return event_type != NULL &&
            (strcmp(event_type, "submit") == 0 ||
            strcmp(event_type, "reset") == 0);
}

static int p_browser_script_invalid_event_type_safe(const char *event_type)
{
    return event_type != NULL && strcmp(event_type, "invalid") == 0;
}

static int p_browser_script_write_string(const char *value,
        char *out_json, int out_capacity, int *out_len)
{
    int escaped;

    if (value == NULL || out_json == NULL || out_len == NULL ||
            out_capacity < 3) {
        return 1;
    }
    out_json[0] = '"';
    escaped = p_browser_script_json_escape(value, out_json + 1,
            out_capacity - 2);
    if (escaped < 0 || escaped + 2 >= out_capacity) {
        return 1;
    }
    out_json[escaped + 1] = '"';
    out_json[escaped + 2] = '\0';
    *out_len = escaped + 2;
    return 0;
}

static int p_browser_script_dom_has_element(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_read_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    int exists;

    binding = (p_browser_script_dom_read_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    if (binding == NULL || root == NULL || id == NULL ||
            binding->callbacks.has_element == NULL) {
        PJson_Free(root);
        return 1;
    }
    exists = binding->callbacks.has_element(binding->callbacks.pw, id);
    PJson_Free(root);
    if (exists < 0) {
        return 1;
    }
    return p_browser_script_write_bool(exists > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_dom_get_text(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_read_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    char *text;
    int allocated_len;
    int text_len;
    int result;

    binding = (p_browser_script_dom_read_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    text = NULL;
    text_len = 0;
    if (binding == NULL || root == NULL || id == NULL ||
            binding->callbacks.get_text == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (binding->callbacks.get_text(binding->callbacks.pw, id, NULL, 0,
            &text_len) != 0 || text_len < 0 ||
            text_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        PJson_Free(root);
        return 1;
    }
    allocated_len = text_len;
    text = (char *) malloc((size_t) allocated_len + 1);
    if (text == NULL || binding->callbacks.get_text(binding->callbacks.pw,
            id, text, allocated_len + 1, &text_len) != 0 || text_len < 0 ||
            text_len > allocated_len ||
            text_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        free(text);
        PJson_Free(root);
        return 1;
    }
    text[text_len] = '\0';
    result = p_browser_script_write_string(text, out_json,
            out_capacity, out_len);
    free(text);
    PJson_Free(root);
    return result;
}

static int p_browser_script_dom_set_text(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_write_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    const char *text;
    int changed;

    binding = (p_browser_script_dom_write_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    text = (object != NULL) ? PJson_GetString(object, "text") : NULL;
    if (binding == NULL || root == NULL || id == NULL || text == NULL ||
            binding->callbacks.set_text == NULL) {
        PJson_Free(root);
        return 1;
    }
    changed = binding->callbacks.set_text(binding->callbacks.pw, id, text);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_dom_get_value(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_value_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    char *value;
    int allocated_len;
    int value_len;
    int result;

    binding = (p_browser_script_dom_value_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    value = NULL;
    value_len = 0;
    if (binding == NULL || root == NULL || id == NULL ||
            binding->callbacks.get_value == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (binding->callbacks.get_value(binding->callbacks.pw, id, NULL, 0,
            &value_len) != 0 || value_len < 0 ||
            value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        PJson_Free(root);
        return 1;
    }
    allocated_len = value_len;
    value = (char *) malloc((size_t) allocated_len + 1);
    if (value == NULL || binding->callbacks.get_value(binding->callbacks.pw,
            id, value, allocated_len + 1, &value_len) != 0 ||
            value_len < 0 || value_len > allocated_len ||
            value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        free(value);
        PJson_Free(root);
        return 1;
    }
    value[value_len] = '\0';
    result = p_browser_script_write_string(value, out_json,
            out_capacity, out_len);
    free(value);
    PJson_Free(root);
    return result;
}

static int p_browser_script_dom_set_value(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_value_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    const char *value;
    int changed;

    binding = (p_browser_script_dom_value_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    value = (object != NULL) ? PJson_GetString(object, "value") : NULL;
    if (binding == NULL || root == NULL || id == NULL || value == NULL ||
            binding->callbacks.set_value == NULL) {
        PJson_Free(root);
        return 1;
    }
    changed = binding->callbacks.set_value(binding->callbacks.pw, id,
            value);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_dom_get_checked(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_checked_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    int checked;
    int result;

    binding = (p_browser_script_dom_checked_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    checked = 0;
    result = binding != NULL && root != NULL && id != NULL &&
            binding->callbacks.get_checked != NULL &&
            binding->callbacks.get_checked(binding->callbacks.pw, id,
            &checked) == 0;
    PJson_Free(root);
    if (!result) {
        return 1;
    }
    return p_browser_script_write_bool(checked != 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_dom_set_checked(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_checked_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    int checked;
    int changed;

    binding = (p_browser_script_dom_checked_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    checked = (object != NULL) ? PJson_GetInt(object, "checked") : 0;
    if (binding == NULL || root == NULL || id == NULL ||
            binding->callbacks.set_checked == NULL) {
        PJson_Free(root);
        return 1;
    }
    changed = binding->callbacks.set_checked(binding->callbacks.pw, id,
            checked ? 1 : 0);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_form_get_string(
        PBrowserScriptGetValueFn callback, void *callback_pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    HANDLE root;
    HANDLE object;
    const char *id;
    char *value;
    int allocated_len;
    int value_len;
    int result;

    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    value = NULL;
    value_len = 0;
    if (root == NULL || id == NULL || callback == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (callback(callback_pw, id, NULL, 0, &value_len) != 0 ||
            value_len < 0 || value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        PJson_Free(root);
        return 1;
    }
    allocated_len = value_len;
    value = (char *) malloc((size_t) allocated_len + 1);
    if (value == NULL || callback(callback_pw, id, value,
            allocated_len + 1, &value_len) != 0 || value_len < 0 ||
            value_len > allocated_len ||
            value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        free(value);
        PJson_Free(root);
        return 1;
    }
    value[value_len] = '\0';
    result = p_browser_script_write_string(value, out_json,
            out_capacity, out_len);
    free(value);
    PJson_Free(root);
    return result;
}

static int p_browser_script_form_set_string(
        PBrowserScriptSetValueFn callback, void *callback_pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    HANDLE root;
    HANDLE object;
    const char *id;
    const char *value;
    int changed;

    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    value = (object != NULL) ? PJson_GetString(object, "value") : NULL;
    if (root == NULL || id == NULL || value == NULL || callback == NULL) {
        PJson_Free(root);
        return 1;
    }
    changed = callback(callback_pw, id, value);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_form_get_bool(
        PBrowserScriptGetCheckedFn callback, void *callback_pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    HANDLE root;
    HANDLE object;
    const char *id;
    int checked;
    int result;

    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    checked = 0;
    result = root != NULL && id != NULL && callback != NULL &&
            callback(callback_pw, id, &checked) == 0;
    PJson_Free(root);
    if (!result) {
        return 1;
    }
    return p_browser_script_write_bool(checked != 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_form_set_bool(
        PBrowserScriptSetCheckedFn callback, void *callback_pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    HANDLE root;
    HANDLE object;
    const char *id;
    int checked;
    int changed;

    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    checked = (object != NULL) ? PJson_GetInt(object, "checked") : 0;
    if (root == NULL || id == NULL || callback == NULL) {
        PJson_Free(root);
        return 1;
    }
    changed = callback(callback_pw, id, checked ? 1 : 0);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_form_get_int(
        PBrowserScriptGetSelectedIndexFn callback, void *callback_pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    HANDLE root;
    HANDLE object;
    const char *id;
    int index;
    int result;

    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    index = -1;
    result = root != NULL && id != NULL && callback != NULL &&
            callback(callback_pw, id, &index) == 0;
    PJson_Free(root);
    if (!result) {
        return 1;
    }
    return p_browser_script_write_int(index, out_json, out_capacity,
            out_len);
}

static int p_browser_script_form_set_int(
        PBrowserScriptSetSelectedIndexFn callback, void *callback_pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    HANDLE root;
    HANDLE object;
    const char *id;
    int index;
    int changed;

    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    index = (object != NULL) ? PJson_GetInt(object, "index") : -2;
    if (root == NULL || id == NULL || callback == NULL) {
        PJson_Free(root);
        return 1;
    }
    changed = callback(callback_pw, id, index);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_custom_validity(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_custom_validity_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *op;
    int operation;

    binding = (p_browser_script_custom_validity_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    op = (object != NULL) ? PJson_GetString(object, "op") : NULL;
    if (binding == NULL || root == NULL || op == NULL) {
        PJson_Free(root);
        return 1;
    }
    operation = strcmp(op, "get") == 0 ? 1 :
            (strcmp(op, "set") == 0 ? 2 : 0);
    PJson_Free(root);
    if (operation == 1) {
        return p_browser_script_form_get_string(
                binding->callbacks.get_message, binding->callbacks.pw,
                args_json, args_len, out_json, out_capacity, out_len);
    }
    if (operation == 2) {
        return p_browser_script_form_set_string(
                binding->callbacks.set_message, binding->callbacks.pw,
                args_json, args_len, out_json, out_capacity, out_len);
    }
    return 1;
}

static int p_browser_script_form_property(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_form_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *op;

    binding = (p_browser_script_form_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    op = (object != NULL) ? PJson_GetString(object, "op") : NULL;
    if (binding == NULL || root == NULL || op == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (strcmp(op, "getDefaultValue") == 0) {
        PJson_Free(root);
        return p_browser_script_form_get_string(
                binding->callbacks.get_default_value, binding->callbacks.pw,
                args_json, args_len, out_json, out_capacity, out_len);
    }
    if (strcmp(op, "setDefaultValue") == 0) {
        PJson_Free(root);
        return p_browser_script_form_set_string(
                binding->callbacks.set_default_value, binding->callbacks.pw,
                args_json, args_len, out_json, out_capacity, out_len);
    }
    if (strcmp(op, "getDefaultChecked") == 0) {
        PJson_Free(root);
        return p_browser_script_form_get_bool(
                binding->callbacks.get_default_checked,
                binding->callbacks.pw, args_json, args_len, out_json,
                out_capacity, out_len);
    }
    if (strcmp(op, "setDefaultChecked") == 0) {
        PJson_Free(root);
        return p_browser_script_form_set_bool(
                binding->callbacks.set_default_checked,
                binding->callbacks.pw, args_json, args_len, out_json,
                out_capacity, out_len);
    }
    if (strcmp(op, "getSelectedIndex") == 0) {
        PJson_Free(root);
        return p_browser_script_form_get_int(
                binding->callbacks.get_selected_index, binding->callbacks.pw,
                args_json, args_len, out_json, out_capacity, out_len);
    }
    if (strcmp(op, "setSelectedIndex") == 0) {
        PJson_Free(root);
        return p_browser_script_form_set_int(
                binding->callbacks.set_selected_index, binding->callbacks.pw,
                args_json, args_len, out_json, out_capacity, out_len);
    }
    PJson_Free(root);
    return 1;
}

static int p_browser_script_validation(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_validation_binding *binding;
    PBrowserScriptValidationInfo info;
    HANDLE root;
    HANDLE object;
    const char *id;
    int result;

    binding = (p_browser_script_validation_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    if (binding == NULL || root == NULL || id == NULL ||
            binding->callbacks.get_validation == NULL) {
        PJson_Free(root);
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    result = binding->callbacks.get_validation(
            binding->callbacks.pw, id, &info);
    PJson_Free(root);
    if (result < 0) {
        return 1;
    }
    return p_browser_script_write_validation(&info, out_json,
            out_capacity, out_len);
}

static int p_browser_script_report_validity(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_report_validity_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    int valid;
    int result;

    binding = (p_browser_script_report_validity_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    if (binding == NULL || root == NULL || id == NULL || id[0] == '\0' ||
            binding->callbacks.report_validity == NULL) {
        PJson_Free(root);
        return 1;
    }
    valid = 0;
    result = binding->callbacks.report_validity(
            binding->callbacks.pw, id, &valid);
    PJson_Free(root);
    if (result < 0) {
        return 1;
    }
    return p_browser_script_write_bool(valid != 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_navigation(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_navigation_binding *binding;
    PBrowserScriptNavigationInfo info;
    HANDLE root;
    HANDLE object;
    HANDLE state;
    const char *op;
    const char *url;
    char *state_json;
    int result;
    int out_value;

    binding = (p_browser_script_navigation_binding *) pw;
    object = NULL;
    state_json = NULL;
    out_value = 0;
    root = p_browser_script_args_object(args_json, args_len, &object);
    op = (object != NULL) ? PJson_GetString(object, "op") : NULL;
    if (binding == NULL || root == NULL || op == NULL ||
            binding->callbacks.navigate == NULL) {
        PJson_Free(root);
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.delta = 0;
    url = NULL;
    if (strcmp(op, "replaceState") == 0 ||
            strcmp(op, "pushState") == 0) {
        state = PJson_GetObject(object, "state");
        url = PJson_GetString(object, "url");
        state_json = PJson_Serialize(state);
        if (url == NULL || url[0] == '\0' ||
                strlen(url) >= PBROWSER_HISTORY_URL_MAX ||
                state_json == NULL || state_json[0] == '\0' ||
                strlen(state_json) >= PBROWSER_HISTORY_STATE_MAX) {
            PJson_FreeString(state_json);
            PJson_Free(root);
            if (strcmp(op, "pushState") == 0) {
                return p_browser_script_write_int(0, out_json,
                        out_capacity, out_len);
            }
            return p_browser_script_write_bool(0, out_json,
                    out_capacity, out_len);
        }
        info.kind = (strcmp(op, "pushState") == 0) ?
                PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE :
                PBROWSER_SCRIPT_NAVIGATION_REPLACE_STATE;
        info.url = url;
        info.state_json = state_json;
        result = binding->callbacks.navigate(binding->callbacks.pw,
                &info, &out_value);
        PJson_FreeString(state_json);
        PJson_Free(root);
        if (result < 0) {
            return 1;
        }
        if (info.kind == PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE) {
            return p_browser_script_write_int(result > 0 ? out_value : 0,
                    out_json, out_capacity, out_len);
        }
        return p_browser_script_write_bool(result > 0, out_json,
                out_capacity, out_len);
    }
    if (strcmp(op, "back") == 0) {
        info.kind = PBROWSER_SCRIPT_NAVIGATION_BACK;
    } else if (strcmp(op, "forward") == 0) {
        info.kind = PBROWSER_SCRIPT_NAVIGATION_FORWARD;
    } else if (strcmp(op, "go") == 0) {
        info.kind = PBROWSER_SCRIPT_NAVIGATION_GO;
        info.delta = PJson_GetInt(object, "delta");
        if (info.delta < -(PBROWSER_HISTORY_MAX - 1) ||
                info.delta > PBROWSER_HISTORY_MAX - 1) {
            PJson_Free(root);
            return p_browser_script_write_bool(0, out_json,
                    out_capacity, out_len);
        }
    } else if (strcmp(op, "assign") == 0 ||
            strcmp(op, "reload") == 0 ||
            strcmp(op, "replace") == 0 ||
            strcmp(op, "fragment") == 0 ||
            strcmp(op, "fragmentReplace") == 0) {
        url = PJson_GetString(object, "url");
        if (url == NULL || url[0] == '\0' ||
                strlen(url) >= PBROWSER_HISTORY_URL_MAX) {
            PJson_Free(root);
            return p_browser_script_write_bool(0, out_json,
                    out_capacity, out_len);
        }
        if (strcmp(op, "assign") == 0) {
            info.kind = PBROWSER_SCRIPT_NAVIGATION_ASSIGN;
        } else if (strcmp(op, "reload") == 0) {
            info.kind = PBROWSER_SCRIPT_NAVIGATION_RELOAD;
        } else if (strcmp(op, "replace") == 0) {
            info.kind = PBROWSER_SCRIPT_NAVIGATION_REPLACE;
        } else if (strcmp(op, "fragment") == 0) {
            info.kind = PBROWSER_SCRIPT_NAVIGATION_FRAGMENT;
        } else {
            info.kind = PBROWSER_SCRIPT_NAVIGATION_FRAGMENT_REPLACE;
        }
        info.url = url;
    } else {
        PJson_Free(root);
        return 1;
    }
    result = binding->callbacks.navigate(binding->callbacks.pw,
            &info, &out_value);
    PJson_Free(root);
    if (result < 0) {
        return 1;
    }
    return p_browser_script_write_bool(result > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_dom_get_attribute(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_attribute_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    const char *name;
    char *value;
    int allocated_len;
    int value_len;
    int status;
    int result;

    binding = (p_browser_script_dom_attribute_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    name = (object != NULL) ? PJson_GetString(object, "name") : NULL;
    value = NULL;
    value_len = 0;
    if (binding == NULL || root == NULL || id == NULL || name == NULL ||
            binding->callbacks.get_attribute == NULL) {
        PJson_Free(root);
        return 1;
    }
    status = binding->callbacks.get_attribute(binding->callbacks.pw, id,
            name, NULL, 0, &value_len);
    if (status == 1) {
        PJson_Free(root);
        return p_browser_script_write_null(out_json, out_capacity,
                out_len);
    }
    if (status != 0 || value_len < 0 ||
            value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        PJson_Free(root);
        return 1;
    }
    allocated_len = value_len;
    value = (char *) malloc((size_t) allocated_len + 1);
    if (value == NULL) {
        PJson_Free(root);
        return 1;
    }
    status = binding->callbacks.get_attribute(binding->callbacks.pw, id,
            name, value, allocated_len + 1, &value_len);
    if (status != 0 || value_len < 0 || value_len > allocated_len ||
            value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        free(value);
        PJson_Free(root);
        return 1;
    }
    value[value_len] = '\0';
    result = p_browser_script_write_string(value, out_json,
            out_capacity, out_len);
    free(value);
    PJson_Free(root);
    return result;
}

static int p_browser_script_dom_set_attribute(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_attribute_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    const char *name;
    const char *value;
    int changed;

    binding = (p_browser_script_dom_attribute_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    name = (object != NULL) ? PJson_GetString(object, "name") : NULL;
    value = (object != NULL) ? PJson_GetString(object, "value") : NULL;
    if (binding == NULL || binding->callbacks.set_attribute == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (root == NULL || id == NULL || name == NULL || value == NULL) {
        PJson_Free(root);
        return p_browser_script_write_bool(0, out_json, out_capacity,
                out_len);
    }
    changed = binding->callbacks.set_attribute(binding->callbacks.pw, id,
            name, value);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_dom_remove_attribute(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_attribute_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    const char *name;
    int changed;

    binding = (p_browser_script_dom_attribute_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    name = (object != NULL) ? PJson_GetString(object, "name") : NULL;
    if (binding == NULL || binding->callbacks.remove_attribute == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (root == NULL || id == NULL || name == NULL) {
        PJson_Free(root);
        return p_browser_script_write_bool(0, out_json, out_capacity,
                out_len);
    }
    changed = binding->callbacks.remove_attribute(binding->callbacks.pw,
            id, name);
    PJson_Free(root);
    if (changed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(changed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_event_add_listener(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_event_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *element_id;
    const char *event_type;
    int capture;
    unsigned int listener;

    binding = (p_browser_script_event_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    element_id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    event_type = (object != NULL) ? PJson_GetString(object, "type") : NULL;
    capture = (object != NULL) ? PJson_GetInt(object, "capture") : 0;
    if (binding == NULL || root == NULL || element_id == NULL ||
            element_id[0] == '\0' || !p_browser_script_event_type_safe(
            event_type) || binding->callbacks.add_listener == NULL) {
        PJson_Free(root);
        return p_browser_script_write_int(0, out_json, out_capacity,
                out_len);
    }
    listener = binding->callbacks.add_listener(binding->callbacks.pw,
            element_id, event_type, capture ? 1 : 0);
    PJson_Free(root);
    if (listener == 0 || listener > 0x7fffffffU) {
        return p_browser_script_write_int(0, out_json, out_capacity,
                out_len);
    }
    return p_browser_script_write_int((int) listener, out_json,
            out_capacity, out_len);
}

static int p_browser_script_event_remove_listener(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_event_binding *binding;
    HANDLE root;
    HANDLE object;
    int listener;
    int removed;

    binding = (p_browser_script_event_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    listener = (object != NULL) ? PJson_GetInt(object, "listener") : 0;
    if (binding == NULL || root == NULL || listener <= 0 ||
            binding->callbacks.remove_listener == NULL) {
        PJson_Free(root);
        return p_browser_script_write_bool(0, out_json, out_capacity,
                out_len);
    }
    removed = binding->callbacks.remove_listener(binding->callbacks.pw,
            (unsigned int) listener);
    PJson_Free(root);
    if (removed < 0) {
        return 1;
    }
    return p_browser_script_write_bool(removed > 0, out_json,
            out_capacity, out_len);
}

static int p_browser_script_programmatic_click(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_programmatic_click_binding *binding;
    HANDLE root;
    HANDLE object;
    PBrowserScriptProgrammaticClickInfo info;
    const char *id;
    int rc;

    binding = (p_browser_script_programmatic_click_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    if (binding == NULL || binding->session == NULL || root == NULL ||
            id == NULL || id[0] == '\0') {
        PJson_Free(root);
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.element_id = id;
    rc = PBrowser_ScriptSessionDispatchProgrammaticClick(
            binding->session, &info);
    PJson_Free(root);
    return p_browser_script_write_bool(rc == PSCRIPT_OK, out_json,
            out_capacity, out_len);
}

PBROWSER_API HANDLE PBrowser_ScriptSessionCreate(unsigned long budget_ms)
{
    p_browser_script_session *session;

    session = (p_browser_script_session *) malloc(sizeof(*session));
    if (session == NULL) {
        return NULL;
    }
    session->dom_read = NULL;
    session->dom_write = NULL;
    session->dom_value = NULL;
    session->dom_checked = NULL;
    session->form = NULL;
    session->validation = NULL;
    session->report_validity = NULL;
    session->custom_validity = NULL;
    session->input = NULL;
    session->key = NULL;
    session->focus = NULL;
    session->edit = NULL;
    session->select = NULL;
    session->click = NULL;
    session->programmatic_click = NULL;
    session->form_event = NULL;
    session->invalid = NULL;
    session->navigation = NULL;
    session->dom_attribute = NULL;
    session->event = NULL;
    session->runtime = PScript_Create(budget_ms);
    if (session->runtime == NULL) {
        free(session);
        return NULL;
    }
    return (HANDLE) session;
}

PBROWSER_API void PBrowser_ScriptSessionDestroy(HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (session == NULL) {
        return;
    }
    if (session->dom_read != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreHasElement", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetText", -1);
        free(session->dom_read);
        session->dom_read = NULL;
    }
    if (session->dom_write != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreSetText", -1);
        free(session->dom_write);
        session->dom_write = NULL;
    }
    if (session->dom_value != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetValue", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreSetValue", -1);
        free(session->dom_value);
        session->dom_value = NULL;
    }
    if (session->dom_checked != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetChecked", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreSetChecked", -1);
        free(session->dom_checked);
        session->dom_checked = NULL;
    }
    if (session->form != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreFormProperty", -1);
        free(session->form);
        session->form = NULL;
    }
    if (session->validation != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreValidation", -1);
        free(session->validation);
        session->validation = NULL;
    }
    if (session->report_validity != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreReportValidity", -1);
        free(session->report_validity);
        session->report_validity = NULL;
    }
    if (session->custom_validity != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreCustomValidity", -1);
        free(session->custom_validity);
        session->custom_validity = NULL;
    }
    if (session->input != NULL) {
        free(session->input);
        session->input = NULL;
    }
    if (session->key != NULL) {
        free(session->key);
        session->key = NULL;
    }
    if (session->focus != NULL) {
        free(session->focus);
        session->focus = NULL;
    }
    if (session->edit != NULL) {
        free(session->edit);
        session->edit = NULL;
    }
    if (session->select != NULL) {
        free(session->select);
        session->select = NULL;
    }
    if (session->click != NULL) {
        free(session->click);
        session->click = NULL;
    }
    if (session->programmatic_click != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreClick", -1);
        free(session->programmatic_click);
        session->programmatic_click = NULL;
    }
    if (session->form_event != NULL) {
        free(session->form_event);
        session->form_event = NULL;
    }
    if (session->invalid != NULL) {
        free(session->invalid);
        session->invalid = NULL;
    }
    if (session->navigation != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreNavigation", -1);
        free(session->navigation);
        session->navigation = NULL;
    }
    if (session->dom_attribute != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetAttribute", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreSetAttribute", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreRemoveAttribute", -1);
        free(session->dom_attribute);
        session->dom_attribute = NULL;
    }
    if (session->event != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreAddEvent", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreRemoveEvent", -1);
        free(session->event);
        session->event = NULL;
    }
    PScript_Destroy(session->runtime);
    session->runtime = NULL;
    free(session);
}

PBROWSER_API int PBrowser_ScriptSessionRegisterJsonFunction(
        HANDLE hSession, const char *name,
        PBrowserScriptJsonFunctionFn fn, void *pw)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || name == NULL ||
            name[0] == '\0' || fn == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_RegisterGlobalJsonFunction(session->runtime, name, -1,
            fn, pw);
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterJsonFunction(
        HANDLE hSession, const char *name)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || name == NULL ||
            name[0] == '\0') {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_UnregisterGlobalJsonFunction(session->runtime, name, -1);
}

PBROWSER_API int PBrowser_ScriptSessionEvaluate(HANDLE hSession,
        const char *source, int source_len)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || source == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_Evaluate(session->runtime, source, source_len);
}

PBROWSER_API int PBrowser_ScriptSessionDispatchHistoryTraversal(
        HANDLE hSession, const char *state_json, const char *url)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return p_browser_script_dispatch_history_traversal(session, state_json,
            url);
}

PBROWSER_API int PBrowser_ScriptSessionDispatchHashNavigation(
        HANDLE hSession, const char *url, int history_length)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return p_browser_script_dispatch_hash_navigation(session, url,
            history_length);
}

PBROWSER_API int PBrowser_ScriptSessionRegisterDomReadCallbacks(
        HANDLE hSession, const PBrowserScriptDomReadCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_dom_read_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptDomReadCallbacks) ||
            callbacks->has_element == NULL || callbacks->get_text == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_read != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_dom_read_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreHasElement", -1, p_browser_script_dom_has_element,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreGetText", -1, p_browser_script_dom_get_text, binding);
    if (rc != PSCRIPT_OK) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreHasElement", -1);
        free(binding);
        return rc;
    }
    session->dom_read = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterDomReadCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;
    int second_rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_read == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreHasElement", -1);
    second_rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreGetText", -1);
    free(session->dom_read);
    session->dom_read = NULL;
    return (rc != PSCRIPT_OK) ? rc : second_rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterDomWriteCallbacks(
        HANDLE hSession, const PBrowserScriptDomWriteCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_dom_write_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptDomWriteCallbacks) ||
            callbacks->set_text == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_write != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_dom_write_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreSetText", -1, p_browser_script_dom_set_text, binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->dom_write = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterDomWriteCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_write == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreSetText", -1);
    free(session->dom_write);
    session->dom_write = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterDomValueCallbacks(
        HANDLE hSession, const PBrowserScriptDomValueCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_dom_value_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptDomValueCallbacks) ||
            callbacks->get_value == NULL || callbacks->set_value == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_value != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_dom_value_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreGetValue", -1, p_browser_script_dom_get_value,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreSetValue", -1, p_browser_script_dom_set_value,
            binding);
    if (rc != PSCRIPT_OK) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetValue", -1);
        free(binding);
        return rc;
    }
    session->dom_value = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterDomValueCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;
    int second_rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_value == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreGetValue", -1);
    second_rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreSetValue", -1);
    free(session->dom_value);
    session->dom_value = NULL;
    return (rc != PSCRIPT_OK) ? rc : second_rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterDomCheckedCallbacks(
        HANDLE hSession, const PBrowserScriptDomCheckedCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_dom_checked_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptDomCheckedCallbacks) ||
            callbacks->get_checked == NULL ||
            callbacks->set_checked == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_checked != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_dom_checked_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreGetChecked", -1, p_browser_script_dom_get_checked,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreSetChecked", -1, p_browser_script_dom_set_checked,
            binding);
    if (rc != PSCRIPT_OK) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetChecked", -1);
        free(binding);
        return rc;
    }
    session->dom_checked = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterDomCheckedCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;
    int second_rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_checked == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreGetChecked", -1);
    second_rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreSetChecked", -1);
    free(session->dom_checked);
    session->dom_checked = NULL;
    return (rc != PSCRIPT_OK) ? rc : second_rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterFormCallbacks(
        HANDLE hSession, const PBrowserScriptFormCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_form_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptFormCallbacks) ||
            callbacks->get_default_value == NULL ||
            callbacks->set_default_value == NULL ||
            callbacks->get_default_checked == NULL ||
            callbacks->set_default_checked == NULL ||
            callbacks->get_selected_index == NULL ||
            callbacks->set_selected_index == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->form != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_form_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreFormProperty", -1, p_browser_script_form_property,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->form = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterFormCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->form == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreFormProperty", -1);
    free(session->form);
    session->form = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterValidationCallbacks(
        HANDLE hSession, const PBrowserScriptValidationCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_validation_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptValidationCallbacks) ||
            callbacks->get_validation == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->validation != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_validation_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreValidation", -1, p_browser_script_validation, binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->validation = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterValidationCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->validation == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreValidation", -1);
    free(session->validation);
    session->validation = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterReportValidityCallbacks(
        HANDLE hSession,
        const PBrowserScriptReportValidityCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_report_validity_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptReportValidityCallbacks) ||
            callbacks->report_validity == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->report_validity != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_report_validity_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreReportValidity", -1, p_browser_script_report_validity,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->report_validity = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterReportValidityCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->report_validity == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreReportValidity", -1);
    free(session->report_validity);
    session->report_validity = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterCustomValidityCallbacks(
        HANDLE hSession,
        const PBrowserScriptCustomValidityCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_custom_validity_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptCustomValidityCallbacks) ||
            callbacks->get_message == NULL ||
            callbacks->set_message == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->custom_validity != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_custom_validity_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreCustomValidity", -1, p_browser_script_custom_validity,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->custom_validity = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterCustomValidityCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->custom_validity == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreCustomValidity", -1);
    free(session->custom_validity);
    session->custom_validity = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterInputCallbacks(
        HANDLE hSession, const PBrowserScriptInputCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_input_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptInputCallbacks) ||
            callbacks->dispatch_input == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->input != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_input_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->input = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterInputCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->input == NULL) {
        return PSCRIPT_OK;
    }
    free(session->input);
    session->input = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchInputEvent(HANDLE hSession,
        const PBrowserScriptInputEventInfo *info, int *out_default_allowed)
{
    p_browser_script_session *session;
    int default_allowed;
    int rc;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->input == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptInputEventInfo) ||
            info->event_type == NULL || info->event_type[0] == '\0' ||
            !p_browser_script_event_type_safe(info->event_type) ||
            info->input_type == NULL || info->data == NULL ||
            out_default_allowed == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    default_allowed = 1;
    rc = session->input->callbacks.dispatch_input(
            session->input->callbacks.pw, info, &default_allowed);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    *out_default_allowed = default_allowed ? 1 : 0;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterKeyCallbacks(
        HANDLE hSession, const PBrowserScriptKeyCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_key_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptKeyCallbacks) ||
            callbacks->dispatch_key == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->key != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_key_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->key = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterKeyCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->key == NULL) {
        return PSCRIPT_OK;
    }
    free(session->key);
    session->key = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchKeyEvent(HANDLE hSession,
        const PBrowserScriptKeyEventInfo *info, int *out_default_allowed)
{
    p_browser_script_session *session;
    int default_allowed;
    int rc;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->key == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptKeyEventInfo) ||
            info->event_type == NULL || info->event_type[0] == '\0' ||
            !p_browser_script_event_type_safe(info->event_type) ||
            info->key == NULL || info->key[0] == '\0' ||
            out_default_allowed == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    default_allowed = 1;
    rc = session->key->callbacks.dispatch_key(
            session->key->callbacks.pw, info, &default_allowed);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    *out_default_allowed = default_allowed ? 1 : 0;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterFocusCallbacks(
        HANDLE hSession, const PBrowserScriptFocusCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_focus_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptFocusCallbacks) ||
            callbacks->dispatch_focus == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->focus != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_focus_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->focus = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterFocusCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->focus == NULL) {
        return PSCRIPT_OK;
    }
    free(session->focus);
    session->focus = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchFocusEvent(HANDLE hSession,
        const PBrowserScriptFocusEventInfo *info)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->focus == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptFocusEventInfo) ||
            !p_browser_script_focus_event_type_safe(info->event_type)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = session->focus->callbacks.dispatch_focus(
            session->focus->callbacks.pw, info);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterEditCallbacks(
        HANDLE hSession, const PBrowserScriptEditCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_edit_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptEditCallbacks) ||
            callbacks->dispatch_edit == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->edit != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_edit_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->edit = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterEditCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->edit == NULL) {
        return PSCRIPT_OK;
    }
    free(session->edit);
    session->edit = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchEditEvent(HANDLE hSession,
        const PBrowserScriptEditEventInfo *info)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->edit == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptEditEventInfo) ||
            !p_browser_script_edit_event_type_safe(info->event_type)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = session->edit->callbacks.dispatch_edit(
            session->edit->callbacks.pw, info);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterSelectCallbacks(
        HANDLE hSession, const PBrowserScriptSelectCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_select_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptSelectCallbacks) ||
            callbacks->dispatch_select == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->select != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_select_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->select = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterSelectCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->select == NULL) {
        return PSCRIPT_OK;
    }
    free(session->select);
    session->select = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchSelectEvent(HANDLE hSession,
        const PBrowserScriptSelectEventInfo *info)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->select == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptSelectEventInfo) ||
            !p_browser_script_select_event_type_safe(info->event_type)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = session->select->callbacks.dispatch_select(
            session->select->callbacks.pw, info);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterClickCallbacks(
        HANDLE hSession, const PBrowserScriptClickCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_click_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptClickCallbacks) ||
            callbacks->dispatch_click == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->click != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_click_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->click = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterClickCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->click == NULL) {
        return PSCRIPT_OK;
    }
    free(session->click);
    session->click = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchClickEvent(HANDLE hSession,
        const PBrowserScriptClickEventInfo *info, int *out_default_allowed)
{
    p_browser_script_session *session;
    int default_allowed;
    int rc;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->click == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptClickEventInfo) ||
            !p_browser_script_click_event_type_safe(info->event_type) ||
            out_default_allowed == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    default_allowed = 1;
    rc = session->click->callbacks.dispatch_click(
            session->click->callbacks.pw, info, &default_allowed);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    *out_default_allowed = default_allowed ? 1 : 0;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterProgrammaticClickCallbacks(
        HANDLE hSession,
        const PBrowserScriptProgrammaticClickCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_programmatic_click_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptProgrammaticClickCallbacks) ||
            callbacks->dispatch_click == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->programmatic_click != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_programmatic_click_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    binding->session = hSession;
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime, "__pcoreClick",
            -1, p_browser_script_programmatic_click, binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->programmatic_click = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterProgrammaticClickCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->programmatic_click == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreClick", -1);
    free(session->programmatic_click);
    session->programmatic_click = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchProgrammaticClick(
        HANDLE hSession, const PBrowserScriptProgrammaticClickInfo *info)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) ||
            session->programmatic_click == NULL || info == NULL ||
            info->size < sizeof(PBrowserScriptProgrammaticClickInfo) ||
            info->element_id == NULL || info->element_id[0] == '\0') {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = session->programmatic_click->callbacks.dispatch_click(
            session->programmatic_click->callbacks.pw, info);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterFormEventCallbacks(
        HANDLE hSession, const PBrowserScriptFormEventCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_form_event_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptFormEventCallbacks) ||
            callbacks->dispatch_form_event == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->form_event != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_form_event_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->form_event = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterFormEventCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->form_event == NULL) {
        return PSCRIPT_OK;
    }
    free(session->form_event);
    session->form_event = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchFormEvent(HANDLE hSession,
        const PBrowserScriptFormEventInfo *info, int *out_default_allowed)
{
    p_browser_script_session *session;
    int default_allowed;
    int rc;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->form_event == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptFormEventInfo) ||
            !p_browser_script_form_event_type_safe(info->event_type) ||
            out_default_allowed == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    default_allowed = 1;
    rc = session->form_event->callbacks.dispatch_form_event(
            session->form_event->callbacks.pw, info, &default_allowed);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    *out_default_allowed = default_allowed ? 1 : 0;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterInvalidCallbacks(
        HANDLE hSession, const PBrowserScriptInvalidCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_invalid_binding *binding;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptInvalidCallbacks) ||
            callbacks->dispatch_invalid == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->invalid != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_invalid_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    session->invalid = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterInvalidCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->invalid == NULL) {
        return PSCRIPT_OK;
    }
    free(session->invalid);
    session->invalid = NULL;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionDispatchInvalidEvent(HANDLE hSession,
        const PBrowserScriptInvalidEventInfo *info, int *out_default_allowed)
{
    p_browser_script_session *session;
    int default_allowed;
    int rc;

    if (out_default_allowed != NULL) {
        *out_default_allowed = 1;
    }
    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || session->invalid == NULL ||
            info == NULL ||
            info->size < sizeof(PBrowserScriptInvalidEventInfo) ||
            !p_browser_script_invalid_event_type_safe(info->event_type) ||
            out_default_allowed == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    default_allowed = 1;
    rc = session->invalid->callbacks.dispatch_invalid(
            session->invalid->callbacks.pw, info, &default_allowed);
    if (rc < 0) {
        return PSCRIPT_ERROR_NATIVE;
    }
    *out_default_allowed = default_allowed ? 1 : 0;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterNavigationCallbacks(
        HANDLE hSession,
        const PBrowserScriptNavigationCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_navigation_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptNavigationCallbacks) ||
            callbacks->navigate == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->navigation != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_navigation_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreNavigation", -1, p_browser_script_navigation,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->navigation = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterNavigationCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->navigation == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreNavigation", -1);
    free(session->navigation);
    session->navigation = NULL;
    return rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterDomAttributeCallbacks(
        HANDLE hSession,
        const PBrowserScriptDomAttributeCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_dom_attribute_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptDomAttributeCallbacks) ||
            callbacks->get_attribute == NULL ||
            callbacks->set_attribute == NULL ||
            callbacks->remove_attribute == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_attribute != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_dom_attribute_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreGetAttribute", -1, p_browser_script_dom_get_attribute,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreSetAttribute", -1, p_browser_script_dom_set_attribute,
            binding);
    if (rc != PSCRIPT_OK) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetAttribute", -1);
        free(binding);
        return rc;
    }
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreRemoveAttribute", -1,
            p_browser_script_dom_remove_attribute, binding);
    if (rc != PSCRIPT_OK) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetAttribute", -1);
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreSetAttribute", -1);
        free(binding);
        return rc;
    }
    session->dom_attribute = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterDomAttributeCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;
    int second_rc;
    int third_rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_attribute == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreGetAttribute", -1);
    second_rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreSetAttribute", -1);
    third_rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreRemoveAttribute", -1);
    free(session->dom_attribute);
    session->dom_attribute = NULL;
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    return (second_rc != PSCRIPT_OK) ? second_rc : third_rc;
}

PBROWSER_API int PBrowser_ScriptSessionRegisterEventCallbacks(
        HANDLE hSession, const PBrowserScriptEventCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_event_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptEventCallbacks) ||
            callbacks->add_listener == NULL ||
            callbacks->remove_listener == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->event != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_event_binding *) malloc(sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreAddEvent", -1, p_browser_script_event_add_listener,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreRemoveEvent", -1,
            p_browser_script_event_remove_listener, binding);
    if (rc != PSCRIPT_OK) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreAddEvent", -1);
        free(binding);
        return rc;
    }
    session->event = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterEventCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;
    int second_rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->event == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreAddEvent", -1);
    second_rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreRemoveEvent", -1);
    free(session->event);
    session->event = NULL;
    return (rc != PSCRIPT_OK) ? rc : second_rc;
}

PBROWSER_API unsigned int PBrowser_ScriptSessionDispatchEvent(
        HANDLE hSession, unsigned int listener, const char *event_type,
        const PBrowserScriptEventInfo *event_info)
{
    p_browser_script_session *session;
    char args[2048];
    char key_json[256];
    char input_type_json[128];
    char data_json[256];
    char target_id_json[256];
    char current_target_id_json[256];
    const char *result;
    int length;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || listener == 0 ||
            !p_browser_script_event_type_safe(event_type) ||
            event_info == NULL ||
            event_info->size < sizeof(PBrowserScriptEventInfo)) {
        return PBROWSER_SCRIPT_EVENT_ACTION_NONE;
    }
    if (p_browser_script_json_escape(event_info->key, key_json,
            sizeof(key_json)) < 0 ||
            p_browser_script_json_escape(event_info->input_type,
            input_type_json, sizeof(input_type_json)) < 0 ||
            p_browser_script_json_escape(event_info->data, data_json,
            sizeof(data_json)) < 0 ||
            p_browser_script_json_escape(event_info->target_id,
            target_id_json, sizeof(target_id_json)) < 0 ||
            p_browser_script_json_escape(event_info->current_target_id,
            current_target_id_json, sizeof(current_target_id_json)) < 0) {
        return PBROWSER_SCRIPT_EVENT_ACTION_NONE;
    }
    length = _snprintf(args, sizeof(args) - 1,
            "[{\"listener\":%u,\"type\":\"%s\","
            "\"phase\":%u,\"bubbles\":%s,\"cancelable\":%s,"
            "\"trusted\":%s,\"defaultPrevented\":%s,"
            "\"inputType\":\"%s\",\"data\":\"%s\","
            "\"isComposing\":%s,\"key\":\"%s\",\"keyCode\":%u,"
            "\"charCode\":%u,\"repeat\":%s,\"shiftKey\":%s,"
            "\"ctrlKey\":%s,\"altKey\":%s,"
            "\"targetId\":\"%s\",\"currentTargetId\":\"%s\"}]",
            listener, event_type, event_info->phase,
            event_info->bubbles ? "true" : "false",
            event_info->cancelable ? "true" : "false",
            event_info->trusted ? "true" : "false",
            event_info->default_prevented ? "true" : "false",
            input_type_json, data_json,
            event_info->is_composing ? "true" : "false", key_json,
            event_info->key_code, event_info->char_code,
            event_info->repeat ? "true" : "false",
            event_info->shift ? "true" : "false",
            event_info->ctrl ? "true" : "false",
            event_info->alt ? "true" : "false",
            target_id_json, current_target_id_json);
    if (length < 0 || length >= (int) sizeof(args) - 1 ||
            PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreDispatchEvent", args) != PSCRIPT_OK) {
        return PBROWSER_SCRIPT_EVENT_ACTION_NONE;
    }
    result = PBrowser_ScriptSessionGetResult(hSession);
    if (result != NULL && strcmp(result, "true") == 0) {
        return PBROWSER_SCRIPT_EVENT_ACTION_PREVENT_DEFAULT;
    }
    return PBROWSER_SCRIPT_EVENT_ACTION_NONE;
}

PBROWSER_API int PBrowser_ScriptSessionSetGlobalString(HANDLE hSession,
        const char *name, const char *value)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || name == NULL ||
            value == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_SetGlobalString(session->runtime, name, -1, value, -1);
}

PBROWSER_API int PBrowser_ScriptSessionSetGlobalNumber(HANDLE hSession,
        const char *name, double value)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || name == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_SetGlobalNumber(session->runtime, name, -1, value);
}

PBROWSER_API int PBrowser_ScriptSessionSetGlobalJson(HANDLE hSession,
        const char *name, const char *value_json)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || name == NULL ||
            value_json == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_SetGlobalJson(session->runtime, name, -1,
            value_json, -1);
}

PBROWSER_API int PBrowser_ScriptSessionCallGlobalJson(HANDLE hSession,
        const char *name, const char *args_json)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || name == NULL ||
            args_json == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PScript_CallGlobalJson(session->runtime, name, -1,
            args_json, -1);
}

PBROWSER_API const char *PBrowser_ScriptSessionGetResult(HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return NULL;
    }
    return PScript_GetResult(session->runtime);
}

PBROWSER_API const char *PBrowser_ScriptSessionGetError(HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return NULL;
    }
    return PScript_GetError(session->runtime);
}

PBROWSER_API unsigned long PBrowser_ScriptSessionNativeFunctionCount(
        HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return 0;
    }
    return PScript_GetNativeFunctionCount(session->runtime);
}

PBROWSER_API HANDLE PBrowser_ScriptSessionRuntime(HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    return p_script_session_valid(session) ? session->runtime : NULL;
}
