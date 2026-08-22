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

/* The browser bootstrap owns additional bounded DOM collection layers beyond
 * the standalone script surface. Keep its heap ceiling explicit and local to
 * browser sessions; independent PScript contexts remain at their 512 KiB
 * default. */
#define P_BROWSER_SCRIPT_MEMORY_LIMIT_BYTES \
        (PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES + 96UL * 1024UL)

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

    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART1[] =
        "(function(g){"
        "function PElement(id){this.__id=id;}"
        "Object.defineProperty(PElement.prototype,'nodeType',{value:1,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(PElement.prototype,'ELEMENT_NODE',"
        "{value:1,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(PElement.prototype,'nodeValue',{value:null,writable:true,"
        "configurable:true,enumerable:true});Object.defineProperty(PElement.prototype,'ownerDocument',"
        "{get:function(){return g.document||null;},enumerable:true});"
        "Object.defineProperty(PElement.prototype,'isConnected',{get:function(){"
        "return !!__pcoreHasElement({id:this.__id});},enumerable:true});"
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
        "g.__pcoreSelections={};"
        "function pselection(owner){var id=owner.__id;var s=g.__pcoreSelections[id];"
        "if(!s){s={start:0,end:0,direction:'none'};g.__pcoreSelections[id]=s;}"
        "return s;}"
        "function pselectionLimit(owner){var v;try{v=String(owner.value||'');}"
        "catch(selectionValueError){v='';}return v.length;}"
        "Object.defineProperty(PElement.prototype,'selectionStart',{get:function(){"
        "var s=pselection(this);var n=pselectionLimit(this);return s.start>n?n:s.start;}});"
        "Object.defineProperty(PElement.prototype,'selectionEnd',{get:function(){"
        "var s=pselection(this);var n=pselectionLimit(this);return s.end>n?n:s.end;}});"
        "Object.defineProperty(PElement.prototype,'selectionDirection',{"
        "get:function(){return pselection(this).direction;},set:function(v){"
        "var d=String(v);pselection(this).direction=(d==='forward'||"
        "d==='backward'||d==='none')?d:'none';}});"
        "PElement.prototype.setSelectionRange=function(start,end,direction){"
        "var s=pselection(this);var n=pselectionLimit(this);var a=Number(start);"
        "var b=Number(end);if(a!==a){a=0;}if(b!==b){b=0;}a=Math.floor(a);"
        "b=Math.floor(b);if(a<0){a=0;}if(b<0){b=0;}if(a>n){a=n;}"
        "if(b>n){b=n;}if(b<a){a=b;}s.start=a;s.end=b;"
        "if(arguments.length>2){s.direction=String(direction)==='forward'||"
        "String(direction)==='backward'?String(direction):'none';}else{"
        "s.direction='none';}};"
        "PElement.prototype.select=function(){var s=pselection(this);"
        "s.start=0;s.end=pselectionLimit(this);s.direction='none';};"
        "function pnumberInput(owner){var t=String(owner.type).toLowerCase();"
        "if(t!=='number'&&t!=='range'){throw new Error('number input required');}"
        "return Number(owner.value);}" 
        "function pnumberStep(owner){var s=String(owner.step);var n;"
        "if(s===''||s==='any'){return 1;}n=Number(s);"
        "return n>0&&isFinite(n)?n:1;}"
        "function pnumberClamp(owner,value){var min=Number(owner.min);var max=Number(owner.max);"
        "if(isFinite(min)&&value<min){value=min;}if(isFinite(max)&&value>max){value=max;}"
        "return value;}"
        "Object.defineProperty(PElement.prototype,'valueAsNumber',{get:function(){"
        "var n=pnumberInput(this);return isFinite(n)?n:NaN;},set:function(v){"
        "var n=Number(v);if(!isFinite(n)){this.value='';}else{this.value=String(n);}}});"
        "PElement.prototype.stepUp=function(count){var n=Number(count);var v;"
        "if(arguments.length===0){n=1;}if(!isFinite(n)||n<0||n!==Math.floor(n)){"
        "throw new Error('step count');}v=pnumberInput(this);if(!isFinite(v)){"
        "v=Number(this.min);if(!isFinite(v)){v=0;}}v+=pnumberStep(this)*n;"
        "this.value=String(pnumberClamp(this,v));};"
        "PElement.prototype.stepDown=function(count){var n=Number(count);var v;"
        "if(arguments.length===0){n=1;}if(!isFinite(n)||n<0||n!==Math.floor(n)){"
        "throw new Error('step count');}v=pnumberInput(this);if(!isFinite(v)){"
        "v=Number(this.min);if(!isFinite(v)){v=0;}}v-=pnumberStep(this)*n;"
        "this.value=String(pnumberClamp(this,v));};"
        "PElement.prototype.setRangeText=function(replacement,start,end,mode){"
        "var value=String(this.value||'');var s=pselection(this);var n=value.length;"
        "var a=arguments.length>1?Number(start):s.start;"
        "var b=arguments.length>2?Number(end):s.end;var r=String(replacement);"
        "var m=arguments.length>3?String(mode):'preserve';var delta;"
        "if(a!==a){a=0;}if(b!==b){b=n;}a=Math.floor(a);b=Math.floor(b);"
        "if(a<0){a=0;}if(b<0){b=0;}if(a>n){a=n;}if(b>n){b=n;}"
        "if(b<a){b=a;}this.value=value.substring(0,a)+r+value.substring(b);"
        "delta=r.length-(b-a);if(m==='select'){s.start=a;s.end=a+r.length;}"
        "else if(m==='start'){s.start=s.end=a;}else if(m==='end'){"
        "s.start=s.end=a+r.length;}else{s.start=a;s.end=a+r.length;}"
        "s.direction='none';};"
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
        "PDefineString('exportParts','exportparts');"
        "PDefineBoolean('inert','inert');"
        "PDefineString('popover','popover');"
        "PDefineBoolean('autofocus','autofocus');"
        "PDefineBoolean('open','open');"
        "PDefineString('autocapitalize','autocapitalize');"
        "PDefineString('itemValue','itemvalue');"
        "PDefineString('is','is');"
        "PDefineString('ariaAtomic','aria-atomic');"
        "PDefineString('ariaBusy','aria-busy');"
        "PDefineString('ariaChecked','aria-checked');"
        "PDefineString('ariaCurrent','aria-current');"
        "PDefineString('ariaDescription','aria-description');"
        "PDefineString('ariaDisabled','aria-disabled');"
        "PDefineString('ariaExpanded','aria-expanded');"
        "PDefineString('ariaHasPopup','aria-haspopup');"
        "PDefineString('ariaHidden','aria-hidden');"
        "PDefineString('ariaKeyShortcuts','aria-keyshortcuts');"
        "PDefineString('ariaLabelledBy','aria-labelledby');"
        "PDefineString('ariaLevel','aria-level');"
        "PDefineString('ariaLive','aria-live');"
        "PDefineString('ariaModal','aria-modal');"
        "PDefineString('ariaPlaceholder','aria-placeholder');"
        "PDefineString('ariaPressed','aria-pressed');"
        "PDefineString('ariaSelected','aria-selected');"
        "PDefineString('ariaColCount','aria-colcount');"
        "PDefineString('ariaColIndex','aria-colindex');"
        "PDefineString('ariaColIndexText','aria-colindextext');"
        "PDefineString('ariaControls','aria-controls');"
        "PDefineString('ariaDescribedBy','aria-describedby');"
        "PDefineString('ariaDetails','aria-details');"
        "PDefineString('ariaErrorMessage','aria-errormessage');"
        "PDefineString('ariaFlowTo','aria-flowto');"
        "PDefineString('ariaInvalid','aria-invalid');"
        "PDefineString('ariaMultiLine','aria-multiline');"
        "PDefineString('ariaMultiSelectable','aria-multiselectable');"
        "PDefineString('ariaOrientation','aria-orientation');"
        "PDefineString('ariaOwns','aria-owns');"
        "PDefineString('ariaPosInSet','aria-posinset');"
        "PDefineString('ariaReadOnly','aria-readonly');"
        "PDefineString('ariaRelevant','aria-relevant');"
        "PDefineString('ariaRequired','aria-required');"
        "PDefineString('ariaRoleDescription','aria-roledescription');"
        "PDefineString('ariaRowCount','aria-rowcount');"
        "PDefineString('ariaRowIndex','aria-rowindex');"
        "PDefineString('enterKeyHint','enterkeyhint');"
        "PDefineString('virtualKeyboardPolicy','virtualkeyboardpolicy');"
        "PDefineBoolean('webkitDirectory','webkitdirectory');"
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
        "PDefineInteger('size','size');"
        "PDefineInteger('cols','cols');"
        "PDefineInteger('rows','rows');"
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
        "PClassList.prototype.replace=function(oldToken,newToken){var a=this._tokens();"
        "var old=String(oldToken);var next=String(newToken);var i;var found=false;"
        "for(i=0;i<a.length;i++){if(a[i]===old){a[i]=next;found=true;}}"
        "if(found){this._write(a);}return found;};"
        "PClassList.prototype.item=function(index){var a=this._tokens();var n=Number(index);"
        "return n===n&&n>=0&&n===Math.floor(n)&&n<a.length?a[n]:null;};"
        "PClassList.prototype.forEach=function(fn,thisArg){var a=this._tokens();var i;"
        "if(typeof fn!=='function'){return;}for(i=0;i<a.length;i++){fn.call(thisArg,a[i],i,this);}};"
        "function pclassIterator(owner){var a=owner._tokens();var i=0;var it={next:function(){"
        "return i<a.length?{value:a[i++],done:false}:{value:undefined,done:true};}};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(it,Symbol.iterator,{"
        "value:function(){return it;}});}return it;}"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(PClassList.prototype,"
        "Symbol.iterator,{value:function(){return pclassIterator(this);}});}"
        "Object.defineProperty(PClassList.prototype,'length',{get:function(){"
        "return this._tokens().length;}});"
        "Object.defineProperty(PClassList.prototype,'value',{get:function(){"
        "return this.__owner.className;},set:function(v){this.__owner.className=String(v);}});"
        "PClassList.prototype.toString=function(){return this.__owner.className;};"
        "Object.defineProperty(PElement.prototype,'classList',{"
        "get:function(){if(!this.__classList){this.__classList=new PClassList(this);}"
        "return this.__classList;}});"
        "function pDatasetAttribute(name){var s=String(name);var out='data-';"
        "var i;var c;for(i=0;i<s.length;i++){c=s.charAt(i);if(c>='A'&&c<='Z'){"
        "out+='-'+c.toLowerCase();}else{out+=c;}}return out;}"
        "function PDataSet(owner){Object.defineProperty(this,'__owner',{value:owner,writable:false,"
        "enumerable:false,configurable:false});Object.defineProperty(this,'__names',{value:[],"
        "writable:false,enumerable:false,configurable:false});}"
        "PDataSet.prototype.get=function(name){return this.__owner.getAttribute("
        "pDatasetAttribute(name));};"
        "PDataSet.prototype.set=function(name,value){this.__owner.setAttribute("
        "pDatasetAttribute(name),String(value));return this;};"
        "PDataSet.prototype.remove=function(name){this.__owner.removeAttribute("
        "pDatasetAttribute(name));};"
        "PDataSet.prototype.has=function(name){return this.__owner.hasAttribute("
        "pDatasetAttribute(name));};"
        "PDataSet.prototype.toJSON=function(){var out={};var i;for(i=0;i<this.__names.length;i++){"
        "if(this.has(this.__names[i])){out[this.__names[i]]=this.get(this.__names[i]);}}return out;};"
        "PDataSet.prototype.keys=function(){return this.__names.slice(0);};"
        "PDataSet.prototype.toString=function(){return '[object DOMStringMap]';};"
        "function pDatasetProxy(owner){var target=new PDataSet(owner);"
        "if(typeof Proxy!=='function'){return target;}return new Proxy(target,{"
        "get:function(t,p){if(typeof Symbol==='function'&&Symbol.toStringTag&&p===Symbol.toStringTag){return 'DOMStringMap';}"
        "if(typeof p==='string'&&!(p in t)){return t.get(p);}"
        "return t[p];},set:function(t,p,v){if(typeof p==='string'&&!(p in t)){"
        "if(t.__names.indexOf(p)<0){t.__names.push(p);}t.set(p,v);return true;}"
        "t[p]=v;return true;},deleteProperty:function(t,p){"
        "if(typeof p==='string'&&!(p in t)){t.remove(p);var n=t.__names.indexOf(p);"
        "if(n>=0){t.__names.splice(n,1);}return true;}return true;},"
        "has:function(t,p){return typeof p==='string'&&!(p in t)?t.has(p):p in t;},"
        "ownKeys:function(t){var a=['__owner','__names'];var i;for(i=0;i<t.__names.length;i++){"
        "if(t.has(t.__names[i])){a.push(t.__names[i]);}}return a;},"
        "getOwnPropertyDescriptor:function(t,p){if(p==='__owner'){return {value:t.__owner,writable:false,"
        "enumerable:false,configurable:false};}if(p==='__names'){return {value:t.__names,writable:false,"
        "enumerable:false,configurable:false};}if(typeof p==='string'&&t.has(p)){return {value:t.get(p),"
        "writable:true,enumerable:true,configurable:true};}return undefined;}});}"
        "Object.defineProperty(PElement.prototype,'dataset',{get:function(){"
        "if(!this.__dataset){this.__dataset=pDatasetProxy(this);}return this.__dataset;}});"
        "function pselectorMatch(element,selector){var s=PTrim(selector);"
        "var body;var eq;var name;var value;"
        "if(s==='*'){return true;}if(s.charAt(0)==='#'){"
        "return element.id===s.substring(1)&&s.length>1;}"
        "if(s.charAt(0)==='.'&&s.length>1){"
        "return element.classList.contains(s.substring(1));}"
        "if(s.charAt(0)==='['&&s.charAt(s.length-1)===']'){"
        "body=PTrim(s.substring(1,s.length-1));eq=body.indexOf('=');"
        "if(eq<0){return body!==''&&element.hasAttribute(body);}"
        "name=PTrim(body.substring(0,eq));value=PTrim(body.substring(eq+1));"
        "if((value.charAt(0)==='\"'&&value.charAt(value.length-1)==='\"')||"
        "(value.charAt(0)==='\\''&&value.charAt(value.length-1)==='\\'')){"
        "value=value.substring(1,value.length-1);}"
        "return name!==''&&element.getAttribute(name)===value;}"
        "return false;}"
        "PElement.prototype.matches=function(selector){"
        "return pselectorMatch(this,selector);};"
        "PElement.prototype.closest=function(selector){"
        "return pselectorMatch(this,selector)?this:null;};"
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
        "var v;for(i=0;i<a.length;i++){if(a[i][0]===n){v=a[i][1];"
        "if(v.toLowerCase().indexOf('!important')===v.length-10){"
        "v=PTrim(v.substring(0,v.length-10));}return v;}}return '';};"
        "PStyle.prototype.getPropertyPriority=function(name){var n=PTrim(name).toLowerCase();"
        "var a=this._parse();var i;for(i=0;i<a.length;i++){if(a[i][0]===n&&"
        "a[i][1].toLowerCase().indexOf('!important')===a[i][1].length-10){"
        "return 'important';}}return '';};"
        "PStyle.prototype.setProperty=function(name,value,priority){"
        "var n=PTrim(name).toLowerCase();var v=PTrim(value);var a=this._parse();"
        "var i;var found=false;var pr=PTrim(priority).toLowerCase();if(n===''){return;}"
        "if(pr==='important'){v+=' !important';}"
        "if(v===''){this.removeProperty(n);return;}"
        "for(i=0;i<a.length;i++){if(a[i][0]===n){a[i][1]=v;found=true;break;}}"
        "if(!found){a.push([n,v]);}this._write(a);};"
        "PStyle.prototype.removeProperty=function(name){"
        "var n=PTrim(name).toLowerCase();var a=this._parse();var old='';var b=[];"
        "var i;for(i=0;i<a.length;i++){if(a[i][0]===n){old=this.getPropertyValue(n);}"
        "else{b.push(a[i]);}}this._write(b);return old;};"
        "PStyle.prototype.item=function(index){var a=this._parse();var n=Number(index);"
        "return n===n&&n>=0&&n===Math.floor(n)&&n<a.length?a[n][0]:'';};"
        "function pstyleIterator(owner){var a=owner._parse();var i=0;var it={next:function(){"
        "return i<a.length?{value:a[i++][0],done:false}:{value:undefined,done:true};}};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(it,Symbol.iterator,{"
        "value:function(){return it;}});}return it;}"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(PStyle.prototype,"
        "Symbol.iterator,{value:function(){return pstyleIterator(this);}});}"
        "Object.defineProperty(PStyle.prototype,'length',{get:function(){"
        "return this._parse().length;}});"
        "Object.defineProperty(PStyle.prototype,'cssText',{"
        "get:function(){return this.__owner.getAttribute('style')||'';},"
        "set:function(v){if(!__pcoreSetAttribute({id:this.__owner.__id,"
        "name:'style',value:String(v)})){throw new Error('cssText update failed');}}});"
        "Object.defineProperty(PElement.prototype,'style',{"
        "get:function(){if(!this.__style){this.__style=new PStyle(this);}"
        "return this.__style;}});"
        "PElement.prototype.toggleAttribute=function(name,force){var n=String(name);"
        "var present=this.hasAttribute(n);var next=arguments.length>1?!!force:!present;"
        "if(next&&!present){this.setAttribute(n,'');}else if(!next&&present){this.removeAttribute(n);}return next;};"
        "g.__pcoreHandlers={};g.__pcoreSyntheticListeners={};"
        "g.__pcoreListenerEntries={};"
        "function pEventOptions(value){var o={capture:false,once:false,"
        "passive:false,signal:null};"
        "if(typeof value==='boolean'){o.capture=!!value;}else if(value){"
        "o.capture=!!value.capture;o.once=!!value.once;o.passive=!!value.passive;"
        "o.signal=value.signal||null;}return o;}"
        "function pRemoveListenerEntry(entry){var owner;var a;var i;var s;"
        "if(!entry||entry.__removed){return;}entry.__removed=true;owner=entry.owner;"
        "if(entry.id>0){__pcoreRemoveEvent({listener:entry.id});"
        "delete g.__pcoreHandlers[entry.id];delete g.__pcoreListenerEntries[entry.id];}"
        "if(owner){a=entry.list||owner.__listeners||[];for(i=a.length-1;i>=0;i--){"
        "if(a[i]===entry||a[i].id===entry.id){a.splice(i,1);}}"
        "s=owner.__id!==undefined?(g.__pcoreSyntheticListeners[owner.__id]||[]):"
        "(owner.__listeners||[]);for(i=s.length-1;i>=0;i--){"
        "if(s[i]===entry||s[i].id===entry.id){s.splice(i,1);}}}}"
        "function pInvokeListener(entry,event,owner){var oldPassive;var fn;"
        "if(!entry||entry.__removed){return;}fn=entry.fn;"
        "if(typeof fn!=='function'&&!(fn&&typeof fn.handleEvent==='function')){return;}"
        "if(entry.once){pRemoveListenerEntry(entry);}oldPassive=event.__passive;"
        "event.__passive=!!entry.passive;try{if(typeof fn==='function'){"
        "fn.call(owner,event);}else{fn.handleEvent.call(fn,event);}}"
        "catch(listenerError){}event.__passive=oldPassive;}"
        "function pDispatchSynthetic(owner,event){var a=(owner.__id!==undefined?"
        "(g.__pcoreSyntheticListeners[owner.__id]||[]):(owner.__listeners||[])).slice(0);"
        "var i;event.target=owner;event.currentTarget=owner;"
        "event.eventPhase=2;for(i=0;i<a.length;i++){if(a[i].type===String(event.type)){"
        "pInvokeListener(a[i],event,owner);if(event.__stopImmediate){break;}}}"
        "return !event.defaultPrevented;}"
        "g.__pcoreDispatchEvent=function(info){"
        "var fn=g.__pcoreHandlers[info.listener];var entry;"
        "if(typeof fn!=='function'&&!(fn&&typeof fn.handleEvent==='function')){return false;}"
        "entry=g.__pcoreListenerEntries[info.listener];"
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
        "e.eventPhase=Number(info.phase)||0;"
        "e.timeStamp=Number(info.timeStamp)||0;"
        "e.srcElement=e.target;"
        "e.__propagationStopped=false;e.__immediatePropagationStopped=false;"
        "e.__passive=false;"
        "e.preventDefault=function(){if(e.cancelable&&!e.__passive){"
        "e.defaultPrevented=true;}};"
        "e.stopPropagation=function(){e.__propagationStopped=true;};"
        "e.stopImmediatePropagation=function(){e.__propagationStopped=true;"
        "e.__immediatePropagationStopped=true;};"
        "Object.defineProperty(e,'cancelBubble',{get:function(){"
        "return e.__propagationStopped;},set:function(v){"
        "e.__propagationStopped=!!v;}});"
        "Object.defineProperty(e,'returnValue',{get:function(){"
        "return !e.defaultPrevented;},set:function(v){if(v===false){"
        "e.preventDefault();}}});"
        "e.composedPath=function(){var a=[];if(e.target){a.push(e.target);}"
        "if(e.currentTarget&&e.currentTarget!==e.target){a.push(e.currentTarget);}"
        "return a;};"
        "if(entry){pInvokeListener(entry,e,e.currentTarget||e.target||g);}"
        "else{fn.call(null,e);}return e.defaultPrevented;};"
        "PElement.prototype.addEventListener=function(type,fn,capture){"
        "var t=String(type);var o=pEventOptions(capture);var n;var entry;"
        "if(typeof fn!=='function'&&!(fn&&typeof fn.handleEvent==='function')){return 0;}"
        "if(o.signal&&o.signal.aborted){return 0;}"
        "n=__pcoreAddEvent({id:this.__id,type:t,capture:o.capture?1:0});"
        "if(n>0){g.__pcoreHandlers[n]=fn;entry={id:n,type:t,fn:fn,"
        "capture:o.capture,once:o.once,passive:o.passive,owner:this,__removed:false};"
        "g.__pcoreListenerEntries[n]=entry;"
        "if(!g.__pcoreSyntheticListeners[this.__id]){"
        "g.__pcoreSyntheticListeners[this.__id]=[];}"
        "g.__pcoreSyntheticListeners[this.__id].push(entry);"
        "if(!this.__listeners){this.__listeners=[];}"
        "this.__listeners.push(entry);if(o.signal&&typeof o.signal.addEventListener==='function'){"
        "o.signal.addEventListener('abort',function(){pRemoveListenerEntry(entry);},{once:true});}}"
        "return n;};"
        "PElement.prototype.removeEventListener=function(type,fn,capture){"
        "var t=String(type);var o=pEventOptions(capture);var a=this.__listeners||[];"
        "var i;"
        "for(i=0;i<a.length;i++){if(a[i].type===t&&a[i].fn===fn&&"
        "a[i].capture===o.capture){pRemoveListenerEntry(a[i]);return;}}};"
        "function PEvent(type,init){var o=init||{};this.type=String(type);"
        "this.bubbles=!!o.bubbles;this.cancelable=!!o.cancelable;"
        "this.composed=!!o.composed;this.defaultPrevented=false;this.isTrusted=false;this.target=null;"
        "this.currentTarget=null;this.eventPhase=0;this.timeStamp=Number(o.timeStamp)||0;"
        "this.__stop=false;this.__stopImmediate=false;this.__passive=false;}"
        "PEvent.prototype.preventDefault=function(){if(this.cancelable&&!this.__passive){"
        "this.defaultPrevented=true;}};PEvent.prototype.stopPropagation=function(){"
        "this.__stop=true;};PEvent.prototype.stopImmediatePropagation=function(){"
        "this.__stop=true;this.__stopImmediate=true;};"
        "PEvent.prototype.composedPath=function(){return this.target===null?[]:[this.target];};"
        "Object.defineProperty(PEvent.prototype,'cancelBubble',{get:function(){"
        "return !!this.__stop;},set:function(v){this.__stop=!!v;}});"
        "Object.defineProperty(PEvent.prototype,'returnValue',{get:function(){"
        "return !this.defaultPrevented;},set:function(v){if(v===false){this.preventDefault();}}});"
        "PEvent.prototype.initEvent=function(type,bubbles,cancelable){this.type=String(type);"
        "this.bubbles=!!bubbles;this.cancelable=!!cancelable;this.defaultPrevented=false;"
        "this.target=null;this.currentTarget=null;this.eventPhase=0;this.__stop=false;"
        "this.__stopImmediate=false;return this;};"
        "Object.defineProperty(g,'Event',{value:PEvent,writable:false,configurable:false});"
        "PElement.prototype.dispatchEvent=function(event){var e=event;"
        "if(!e||typeof e.type==='undefined'){throw new Error('event required');}"
        "return pDispatchSynthetic(this,e);};"
        "function pDefineElementHandler(name,type){Object.defineProperty(PElement.prototype,name,{"
        "get:function(){return this.__eventHandlers&&this.__eventHandlers[name]||null;},"
        "set:function(fn){var old=this.__eventHandlers&&this.__eventHandlers[name];"
        "if(old){this.removeEventListener(type,old,false);}if(!this.__eventHandlers){"
        "this.__eventHandlers={};}this.__eventHandlers[name]=typeof fn==='function'?fn:null;"
        "if(typeof fn==='function'){this.addEventListener(type,fn,false);}}});}"
        "pDefineElementHandler('onclick','click');pDefineElementHandler('oninput','input');"
        "pDefineElementHandler('onchange','change');pDefineElementHandler('onkeydown','keydown');"
        "pDefineElementHandler('onkeyup','keyup');pDefineElementHandler('onfocus','focus');"
        "pDefineElementHandler('onblur','blur');pDefineElementHandler('onsubmit','submit');"
        "pDefineElementHandler('oninvalid','invalid');pDefineElementHandler('onselect','select');"
        "pDefineElementHandler('onbeforeinput','beforeinput');"
        "function PEventTarget(){this.__listeners=[];}"
        "PEventTarget.prototype.addEventListener=function(type,fn,options){"
        "var o=pEventOptions(options);var entry;if((typeof fn!=='function'&&"
        "!(fn&&typeof fn.handleEvent==='function'))||"
        "(o.signal&&o.signal.aborted)){return;}entry={id:0,type:String(type),fn:fn,"
        "capture:o.capture,once:o.once,passive:o.passive,owner:this,__removed:false};"
        "this.__listeners.push(entry);if(o.signal&&typeof o.signal.addEventListener==='function'){"
        "o.signal.addEventListener('abort',function(){pRemoveListenerEntry(entry);},{once:true});}};"
        "PEventTarget.prototype.removeEventListener=function(type,fn,options){"
        "var o=pEventOptions(options);var i;for(i=0;i<this.__listeners.length;i++){"
        "if(this.__listeners[i].type===String(type)&&this.__listeners[i].fn===fn&&"
        "this.__listeners[i].capture===o.capture){pRemoveListenerEntry(this.__listeners[i]);return;}}};"
        "PEventTarget.prototype.dispatchEvent=function(event){if(!event||"
        "typeof event.type==='undefined'){throw new Error('event required');}"
        "return pDispatchSynthetic(this,event);};"
        "Object.defineProperty(g,'EventTarget',{value:PEventTarget,writable:false,configurable:false});"
        "function PCustomEvent(type,init){PEvent.call(this,type,init);"
        "this.detail=init&&init.detail!==undefined?init.detail:null;}"
        "PCustomEvent.prototype=Object.create(PEvent.prototype);"
        "PCustomEvent.prototype.constructor=PCustomEvent;"
        "Object.defineProperty(g,'CustomEvent',{value:PCustomEvent,writable:false,configurable:false});"
        "function pEventCtor(proto,fields){var C=function(type,init){var o=init||{};"
        "PEvent.call(this,type,o);var i;for(i=0;i<fields.length;i++){"
        "this[fields[i]]=o[fields[i]]===undefined?0:o[fields[i]];}};"
        "C.prototype=Object.create(PEvent.prototype);C.prototype.constructor=C;"
        "return C;}"
        "var PMouseEvent=pEventCtor(PEvent.prototype,['screenX','screenY','clientX',"
        "'clientY','button','buttons','detail']);"
        "var PKeyboardEvent=pEventCtor(PEvent.prototype,['key','code','location',"
        "'repeat','isComposing','ctrlKey','shiftKey','altKey','metaKey']);"
        "var PInputEvent=pEventCtor(PEvent.prototype,['inputType','data','isComposing']);"
        "var PFocusEvent=pEventCtor(PEvent.prototype,['relatedTarget']);"
        "var PSubmitEvent=pEventCtor(PEvent.prototype,['submitter']);"
        "function PMessageEvent(type,init){PEvent.call(this,type,init);var o=init||{};"
        "this.data=o.data===undefined?null:o.data;this.origin=String(o.origin||'');"
        "this.lastEventId=String(o.lastEventId||'');this.source=o.source||null;"
        "this.ports=o.ports||[];}PMessageEvent.prototype=Object.create(PEvent.prototype);"
        "PMessageEvent.prototype.constructor=PMessageEvent;"
        "Object.defineProperty(g,'MouseEvent',{value:PMouseEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'KeyboardEvent',{value:PKeyboardEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'InputEvent',{value:PInputEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'FocusEvent',{value:PFocusEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'SubmitEvent',{value:PSubmitEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'MessageEvent',{value:PMessageEvent,writable:false,configurable:false});"
        "function PAbortSignal(){PEventTarget.call(this);this.__aborted=false;"
        "this.__reason=undefined;this.__onabort=null;}"
        "PAbortSignal.prototype=Object.create(PEventTarget.prototype);"
        "PAbortSignal.prototype.constructor=PAbortSignal;"
        "Object.defineProperty(PAbortSignal.prototype,'aborted',{get:function(){return this.__aborted;}});"
        "Object.defineProperty(PAbortSignal.prototype,'reason',{get:function(){return this.__reason;}});"
        "Object.defineProperty(PAbortSignal.prototype,'onabort',{get:function(){return this.__onabort;},"
        "set:function(fn){if(this.__onabort){this.removeEventListener('abort',this.__onabort,false);}"
        "this.__onabort=typeof fn==='function'?fn:null;if(this.__onabort){"
        "this.addEventListener('abort',this.__onabort,false);}}});"
        "PAbortSignal.prototype.throwIfAborted=function(){if(this.__aborted){throw this.__reason||new Error('aborted');}};"
        "function PAbortController(){this.signal=new PAbortSignal();}"
        "PAbortController.prototype.abort=function(reason){var e;if(this.signal.__aborted){return;}"
        "this.signal.__aborted=true;this.signal.__reason=reason===undefined?new Error('aborted'):reason;"
        "e=new PEvent('abort');e.isTrusted=false;this.signal.dispatchEvent(e);};"
        "Object.defineProperty(g,'AbortSignal',{value:PAbortSignal,writable:false,configurable:false});"
        "Object.defineProperty(g,'AbortController',{value:PAbortController,writable:false,configurable:false});"
        "PAbortSignal.timeout=function(ms){var c=new PAbortController();var d=Number(ms);"
        "if(!isFinite(d)||d<0){d=0;}if(typeof g.setTimeout==='function'){g.setTimeout(function(){"
        "c.abort(new Error('timeout'));},d);}else{c.abort(new Error('timeout'));}return c.signal;};"
        "PAbortSignal.any=function(signals){var c=new PAbortController();var a=signals||[];var i;"
        "for(i=0;i<a.length;i++){if(a[i]&&a[i].aborted){c.abort(a[i].reason);return c.signal;}"
        "if(a[i]&&typeof a[i].addEventListener==='function'){a[i].addEventListener('abort',function(){"
        "if(!c.signal.aborted){c.abort(this.reason);}}, {once:true});}}return c.signal;};"
        "PAbortSignal.abort=function(reason){var c=new PAbortController();c.abort(reason);return c.signal;};"
        "g.__pcorePElement=PElement;g.__pcorePEvent=PEvent;g.__pcoreEventTarget=PEventTarget;"
        "g.__pcoreEventOptions=pEventOptions;g.__pcoreRemoveListenerEntry=pRemoveListenerEntry;"
        "g.__pcoreInvokeListener=pInvokeListener;g.__pcoreDispatchSynthetic=pDispatchSynthetic;"
        "g.__pcoreTrim=PTrim;"
        "})(this);";

    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART2[] =
        "(function(g){var PElement=g.__pcorePElement;var PEvent=g.__pcorePEvent;"
        "var pEventOptions=g.__pcoreEventOptions;var pRemoveListenerEntry=g.__pcoreRemoveListenerEntry;"
        "var pInvokeListener=g.__pcoreInvokeListener;var PTrim=g.__pcoreTrim;"
        "function PUrlSearchParams(init){var s;var a;var i;var p;"
        "this.__pairs=[];this.__onchange=null;if(typeof init==='undefined'||init===null){return;}"
        "if(init instanceof PUrlSearchParams){for(i=0;i<init.__pairs.length;i++)"
        "{this.__pairs.push([init.__pairs[i][0],init.__pairs[i][1]]);}return;}"
        "if(typeof init==='string'){s=init.charAt(0)==='?'?init.substring(1):init;"
        "if(s!==''){a=s.split('&');for(i=0;i<a.length;i++){p=a[i].indexOf('=');"
        "if(p<0){this.append(puspDecode(a[i]),'');}else{this.append("
        "puspDecode(a[i].substring(0,p)),puspDecode(a[i].substring(p+1)));}}}"
        "return;}if(init instanceof Array){for(i=0;i<init.length;i++){"
        "if(init[i] instanceof Array&&init[i].length>=2){this.append(init[i][0],init[i][1]);}}"
        "return;}if(typeof init==='object'){for(i in init){if(init.hasOwnProperty(i))"
        "{this.append(i,init[i]);}}}}"
        "function puspDecode(v){try{return decodeURIComponent(String(v).replace(/\\+/g,' '));}"
        "catch(e){return String(v);}}"
        "function puspEncode(v){return encodeURIComponent(String(v)).replace(/%20/g,'+');}"
        "PUrlSearchParams.prototype.append=function(name,value){"
        "this.__pairs.push([String(name),String(value)]);if(this.__onchange)"
        "{this.__onchange(this);}};"
        "PUrlSearchParams.prototype.set=function(name,value){var n=String(name);"
        "var i;var first=-1;var out=[];for(i=0;i<this.__pairs.length;i++){"
        "if(this.__pairs[i][0]===n){if(first<0){first=out.length;out.push([n,String(value)]);}}"
        "else{out.push(this.__pairs[i]);}}if(first<0){out.push([n,String(value)]);}"
        "this.__pairs=out;if(this.__onchange){this.__onchange(this);}};"
        "PUrlSearchParams.prototype.get=function(name){var n=String(name);var i;"
        "for(i=0;i<this.__pairs.length;i++){if(this.__pairs[i][0]===n){return this.__pairs[i][1];}}"
        "return null;};"
        "PUrlSearchParams.prototype.getAll=function(name){var n=String(name);var out=[];var i;"
        "for(i=0;i<this.__pairs.length;i++){if(this.__pairs[i][0]===n){out.push(this.__pairs[i][1]);}}"
        "return out;};"
        "PUrlSearchParams.prototype.has=function(name){return this.get(name)!==null;};"
        "PUrlSearchParams.prototype.delete=function(name,value){var n=String(name);"
        "var hasValue=arguments.length>1;var wanted=String(value);var out=[];var i;"
        "for(i=0;i<this.__pairs.length;i++){if(this.__pairs[i][0]!==n||"
        "(hasValue&&this.__pairs[i][1]!==wanted)){out.push(this.__pairs[i]);}}"
        "this.__pairs=out;if(this.__onchange){this.__onchange(this);}};"
        "PUrlSearchParams.prototype.toString=function(){var out='';var i;"
        "for(i=0;i<this.__pairs.length;i++){if(i>0){out+='&';}out+=puspEncode("
        "this.__pairs[i][0])+'='+puspEncode(this.__pairs[i][1]);}return out;};"
        "PUrlSearchParams.prototype.sort=function(){var src=this.__pairs;var out=[];var i;var j;"
        "var item;var inserted;for(i=0;i<src.length;i++){item=src[i];inserted=false;"
        "for(j=0;j<out.length;j++){if(item[0]<out[j][0]){out.splice(j,0,item);inserted=true;break;}}"
        "if(!inserted){out.push(item);}}this.__pairs=out;"
        "if(this.__onchange){this.__onchange(this);}};"
        "PUrlSearchParams.prototype.forEach=function(fn,thisArg){var i;"
        "if(typeof fn!=='function'){return;}for(i=0;i<this.__pairs.length;i++){"
        "fn.call(thisArg,this.__pairs[i][1],this.__pairs[i][0],this);}};"
        "function puspIterator(owner,kind){var i=0;var it={next:function(){"
        "var p;if(i>=owner.__pairs.length){return {value:undefined,done:true};}"
        "p=owner.__pairs[i++];return {value:kind==='keys'?p[0]:(kind==='values'?p:[p[0],p[1]]),done:false};}};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(it,Symbol.iterator,{"
        "value:function(){return it;}});}return it;}"
        "PUrlSearchParams.prototype.entries=function(){return puspIterator(this,'entries');};"
        "PUrlSearchParams.prototype.keys=function(){return puspIterator(this,'keys');};"
        "PUrlSearchParams.prototype.values=function(){return puspIterator(this,'values');};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(PUrlSearchParams.prototype,"
        "Symbol.iterator,{value:function(){return this.entries();}});}"
        "Object.defineProperty(PUrlSearchParams.prototype,'size',{get:function(){"
        "return this.__pairs.length;}});"
        "Object.defineProperty(g,'URLSearchParams',{value:PUrlSearchParams,"
        "writable:false,configurable:false});"
        "function pstorageEvent(area,key,oldValue,newValue){pdispatchWindow('storage',{"
        "type:'storage',key:key,oldValue:oldValue,newValue:newValue,url:purl,"
        "storageArea:area,target:g,currentTarget:g,bubbles:false,cancelable:false,"
        "defaultPrevented:false,isTrusted:false,preventDefault:function(){}});}"
        "function PStorage(name){Object.defineProperty(this,'__data',{value:{},writable:true,"
        "enumerable:false,configurable:true});Object.defineProperty(this,'__keys',{value:[],"
        "writable:true,enumerable:false,configurable:true});Object.defineProperty(this,'__name',"
        "{value:String(name||''),writable:false,enumerable:false,configurable:true});}"
        "PStorage.prototype.getItem=function(key){var k=String(key);"
        "return this.__data.hasOwnProperty(k)?this.__data[k]:null;};"
        "PStorage.prototype.setItem=function(key,value){var k=String(key);"
        "var old=this.__data.hasOwnProperty(k)?this.__data[k]:null;var next=String(value);"
        "if(old===next){return;}if(!this.__data.hasOwnProperty(k)){this.__keys.push(k);}"
        "this.__data[k]=next;pstorageEvent(this,k,old,next);};"
        "PStorage.prototype.removeItem=function(key){var k=String(key);var i;var old;"
        "if(!this.__data.hasOwnProperty(k)){return;}old=this.__data[k];delete this.__data[k];"
        "for(i=0;i<this.__keys.length;i++){if(this.__keys[i]===k){"
        "this.__keys.splice(i,1);break;}}pstorageEvent(this,k,old,null);};"
        "PStorage.prototype.clear=function(){var old=this.__data;var i;var k;"
        "this.__data={};this.__keys=[];for(i=0;i<Object.keys(old).length;i++){"
        "k=Object.keys(old)[i];pstorageEvent(this,k,old[k],null);}};"
        "PStorage.prototype.key=function(index){var n=Number(index);"
        "return n===n&&n>=0&&n===Math.floor(n)&&n<this.__keys.length?"
        "this.__keys[n]:null;};"
        "PStorage.prototype.toJSON=function(){var out={};var i;for(i=0;i<this.__keys.length;i++){"
        "out[this.__keys[i]]=this.__data[this.__keys[i]];}return out;};"
        "Object.defineProperty(PStorage.prototype,'length',{get:function(){"
        "return this.__keys.length;}});"
        "function pstorageProxy(target){if(typeof Proxy!=='function'){return target;}return new Proxy(target,{"
        "get:function(t,p){if(typeof Symbol==='function'&&Symbol.toStringTag&&p===Symbol.toStringTag){return 'Storage';}"
        "if(typeof p==='string'&&!(p in t)){return t.getItem(p);}return t[p];},"
        "set:function(t,p,v){if(typeof p==='string'&&!(p in t)){t.setItem(p,v);return true;}"
        "t[p]=v;return true;},deleteProperty:function(t,p){if(typeof p==='string'&&!(p in t)){"
        "t.removeItem(p);return true;}return true;},has:function(t,p){return typeof p==='string'&&"
        "!(p in t)?t.getItem(p)!==null:p in t;},ownKeys:function(t){return t.__keys.slice(0);},"
        "getOwnPropertyDescriptor:function(t,p){if(typeof p==='string'&&t.getItem(p)!==null){"
        "return {value:t.getItem(p),writable:true,enumerable:true,configurable:true};}return undefined;}});}"
        "Object.defineProperty(g,'sessionStorage',{value:pstorageProxy(new PStorage('session')),"
        "writable:false,configurable:false});"
        "Object.defineProperty(g,'localStorage',{value:pstorageProxy(new PStorage('local')),"
        "writable:false,configurable:false});"
        "function PFormData(init){this.__pairs=[];var i;var k;"
        "if(init instanceof PFormData){for(i=0;i<init.__pairs.length;i++){"
        "this.__pairs.push([init.__pairs[i][0],init.__pairs[i][1]]);}return;}"
        "if(init instanceof Array){for(i=0;i<init.length;i++){"
        "if(init[i] instanceof Array&&init[i].length>=2){"
        "this.append(init[i][0],init[i][1]);}}return;}"
        "if(init&&typeof init==='object'){for(k in init){"
        "if(init.hasOwnProperty(k)){this.append(k,init[k]);}}}}"
        "PFormData.prototype.append=function(name,value,filename){var v=value;"
        "if(value instanceof g.Blob&&filename!==undefined){v=new g.File([value.__text],"
        "String(filename),{type:value.type});}else if(!(value instanceof g.Blob)){v=String(value);}"
        "this.__pairs.push([String(name),v]);};"
        "PFormData.prototype.set=function(name,value,filename){var n=String(name);var v=value;"
        "if(value instanceof g.Blob&&filename!==undefined){v=new g.File([value.__text],"
        "String(filename),{type:value.type});}else if(!(value instanceof g.Blob)){v=String(value);}"
        "var out=[];var first=-1;var i;for(i=0;i<this.__pairs.length;i++){"
        "if(this.__pairs[i][0]===n){if(first<0){first=out.length;"
        "out.push([n,v]);}}else{out.push(this.__pairs[i]);}}"
        "if(first<0){out.push([n,v]);}this.__pairs=out;};"
        "PFormData.prototype.get=function(name){var n=String(name);var i;"
        "for(i=0;i<this.__pairs.length;i++){if(this.__pairs[i][0]===n){"
        "return this.__pairs[i][1];}}return null;};"
        "PFormData.prototype.getAll=function(name){var n=String(name);"
        "var out=[];var i;for(i=0;i<this.__pairs.length;i++){"
        "if(this.__pairs[i][0]===n){out.push(this.__pairs[i][1]);}}return out;};"
        "PFormData.prototype.has=function(name){return this.get(name)!==null;};"
        "PFormData.prototype.delete=function(name){var n=String(name);var out=[];"
        "var i;for(i=0;i<this.__pairs.length;i++){if(this.__pairs[i][0]!==n){"
        "out.push(this.__pairs[i]);}}this.__pairs=out;};"
        "PFormData.prototype.forEach=function(fn,thisArg){var i;"
        "if(typeof fn!=='function'){return;}for(i=0;i<this.__pairs.length;i++){"
        "fn.call(thisArg,this.__pairs[i][1],this.__pairs[i][0],this);}};"
        "PFormData.prototype.entries=function(){var out=[];var i;"
        "for(i=0;i<this.__pairs.length;i++){out.push([this.__pairs[i][0],"
        "this.__pairs[i][1]]);}return out;};"
        "PFormData.prototype.keys=function(){var out=[];var i;"
        "for(i=0;i<this.__pairs.length;i++){out.push(this.__pairs[i][0]);}"
        "return out;};"
        "PFormData.prototype.values=function(){var out=[];var i;"
        "for(i=0;i<this.__pairs.length;i++){out.push(this.__pairs[i][1]);}"
        "return out;};"
        "function pfdIterator(owner,kind){var i=0;var it={next:function(){"
        "var p;if(i>=owner.__pairs.length){return {value:undefined,done:true};}"
        "p=owner.__pairs[i++];return {value:kind==='keys'?p[0]:(kind==='values'?p:[p[0],p[1]]),done:false};}};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(it,Symbol.iterator,{"
        "value:function(){return it;}});}return it;}"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(PFormData.prototype,"
        "Symbol.iterator,{value:function(){return pfdIterator(this,'entries');}});}"
        "function pFormValue(value){return value instanceof g.Blob?(value.name||'blob'):String(value);}"
        "PFormData.prototype.toQueryString=function(){var out='';var i;"
        "for(i=0;i<this.__pairs.length;i++){if(i>0){out+='&';}"
        "out+=encodeURIComponent(this.__pairs[i][0]).replace(/%20/g,'+')+'='"
        "+encodeURIComponent(pFormValue(this.__pairs[i][1])).replace(/%20/g,'+');}return out;};"
        "Object.defineProperty(PFormData.prototype,'length',{get:function(){"
        "return this.__pairs.length;}});"
        "Object.defineProperty(g,'FormData',{value:PFormData,"
        "writable:false,configurable:false});"
        "var ptimerClock=0;var ptimerNext=1;var ptimers=[];"
        "function pnewTimer(fn,delay,interval,args){var d=Number(delay);var id;"
        "if(typeof fn!=='function'){return 0;}if(!isFinite(d)||d<0){d=0;}"
        "d=Math.floor(d);if(interval&&d<1){d=1;}id=ptimerNext++;if(id<=0){"
        "ptimerNext=1;id=ptimerNext++;}ptimers.push({id:id,fn:fn,due:"
        "ptimerClock+d,delay:d,interval:!!interval,args:args||[]});return id;}"
        "function pclearTimer(id){var n=Number(id);var i;for(i=ptimers.length-1;"
        "i>=0;i--){if(ptimers[i].id===n){ptimers.splice(i,1);return;}}}"
        "g.setTimeout=function(fn,delay){var a=[];var i;for(i=2;i<arguments.length;i++){a.push(arguments[i]);}"
        "return pnewTimer(fn,delay,false,a);};"
        "g.setInterval=function(fn,delay){var a=[];var i;for(i=2;i<arguments.length;i++){a.push(arguments[i]);}"
        "return pnewTimer(fn,delay,true,a);};"
        "g.setImmediate=function(fn){var a=[];var i;for(i=1;i<arguments.length;i++){a.push(arguments[i]);}"
        "return pnewTimer(fn,0,false,a);};"
        "g.clearTimeout=function(id){pclearTimer(id);};"
        "g.clearInterval=function(id){pclearTimer(id);};"
        "g.clearImmediate=function(id){pclearTimer(id);};"
        "g.__pcoreRunTimers=function(now){var n=Number(now);var ran=0;var i;"
        "var best;var bestDue;var t;if(!isFinite(n)||n<ptimerClock){n=ptimerClock;}"
        "ptimerClock=Math.floor(n);while(ran<64){best=-1;bestDue=0;"
        "for(i=0;i<ptimers.length;i++){if(ptimers[i].due<=ptimerClock&&"
        "(best<0||ptimers[i].due<bestDue)){best=i;bestDue=ptimers[i].due;}}"
        "if(best<0){break;}t=ptimers[best];if(t.interval){t.due+=t.delay;}"
        "else{ptimers.splice(best,1);}try{t.fn.apply(g,t.args||[]);}catch(timerError){}ran++;}"
        "return ran;};"
        "var pframeNext=1;var pframes=[];"
        "g.requestAnimationFrame=function(fn){var id;if(typeof fn!=='function'){return 0;}"
        "id=pframeNext++;if(id<=0){pframeNext=1;id=pframeNext++;}"
        "pframes.push({id:id,fn:fn});return id;};"
        "g.cancelAnimationFrame=function(id){var n=Number(id);var i;"
        "for(i=pframes.length-1;i>=0;i--){if(pframes[i].id===n){"
        "pframes.splice(i,1);return;}}};"
        "g.__pcoreRunAnimationFrames=function(timestamp){var t=Number(timestamp);"
        "var list=pframes.slice(0);var i;if(!isFinite(t)||t<0){t=0;}pframes=[];"
        "if(list.length>32){list.length=32;}for(i=0;i<list.length;i++){"
        "try{list[i].fn(t);}catch(frameError){}}return list.length;};"
        "var pmicrotasks=[];"
        "g.queueMicrotask=function(fn){if(typeof fn==='function'&&pmicrotasks.length<256){"
        "pmicrotasks.push(fn);}};"
        "g.__pcoreRunMicrotasks=function(){var ran=0;var fn;"
        "while(pmicrotasks.length>0&&ran<256){fn=pmicrotasks.shift();"
        "try{fn();}catch(microtaskError){}ran++;}return ran;};"
        "var pidleNext=1;var pidles=[];"
        "g.requestIdleCallback=function(fn,options){var id;var o=options||{};"
        "var timeout=Number(o.timeout);if(typeof fn!=='function'){return 0;}"
        "if(!isFinite(timeout)||timeout<0){timeout=0;}id=pidleNext++;if(id<=0){"
        "pidleNext=1;id=pidleNext++;}if(pidles.length<64){pidles.push({id:id,fn:fn,"
        "timeout:Math.floor(timeout)});}return id;};"
        "g.cancelIdleCallback=function(id){var n=Number(id);var i;"
        "for(i=pidles.length-1;i>=0;i--){if(pidles[i].id===n){pidles.splice(i,1);return;}}};"
        "g.__pcoreRunIdleCallbacks=function(deadline){var d=Number(deadline);"
        "var list=pidles.slice(0);var i;var start;if(!isFinite(d)||d<0){d=0;}"
        "pidles=[];if(list.length>16){list.length=16;}start=d;for(i=0;i<list.length;i++){"
        "try{list[i].fn({didTimeout:list[i].timeout<=d,timeRemaining:function(){"
        "return d>start?d-start:0;}});}catch(idleError){}}return list.length;};"
        "var pmessageQueue=[];var pextraMessages=[];g.__pcoreExtraMessages=pextraMessages;"
        "g.postMessage=function(message,targetOrigin){if(pmessageQueue.length<64){"
        "pmessageQueue.push({data:message,origin:String(targetOrigin||'*'),source:g});}};"
        "g.__pcoreRunMessages=function(limit){var n=Number(limit);var ran=0;var item;"
        "if(!isFinite(n)||n<1){n=64;}n=Math.floor(n);while(pmessageQueue.length>0&&ran<n){"
        "item=pmessageQueue.shift();pdispatchWindow('message',{type:'message',data:item.data,"
        "origin:item.origin,lastEventId:'',source:item.source,ports:[],target:g,currentTarget:g,"
        "bubbles:false,cancelable:false,defaultPrevented:false,isTrusted:false,"
        "preventDefault:function(){}});ran++;}"
        "while(pextraMessages.length>0&&ran<n){item=pextraMessages.shift();"
        "if(item&&item.port&&!item.port.__closed){try{item.port.dispatchEvent(new g.MessageEvent(item.error?'messageerror':'message',"
        "{data:item.data,ports:item.ports||[]}));}catch(extraMessageError){}}"
        "else if(item&&item.channel&&!item.channel.__closed){try{item.channel.dispatchEvent(new g.MessageEvent(item.error?'messageerror':'message',"
        "{data:item.data,origin:'',source:null,ports:item.ports||[]}));}catch(channelMessageError){}}ran++;}return ran;};"
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
        "var pscrollX=0;var pscrollY=0;"
        "var pwindowListeners={popstate:[],hashchange:[],load:[],scroll:[],"
        "pagehide:[],pageshow:[],DOMContentLoaded:[],readystatechange:[],message:[],storage:[]};"
        "g.onpopstate=null;g.onhashchange=null;g.onpagehide=null;g.onpageshow=null;"
        "g.onmessage=null;g.onstorage=null;"
        "g.addEventListener=function(type,fn,capture){var t=String(type);"
        "var a;var i;if((t!=='popstate'&&t!=='hashchange'&&t!=='load'&&"
        "t!=='scroll'&&t!=='pagehide'&&t!=='pageshow'&&"
        "t!=='DOMContentLoaded'&&t!=='readystatechange'&&t!=='message'&&"
        "t!=='storage')||"
        "typeof fn!=='function'){return;}a=pwindowListeners[t];var o=pEventOptions(capture);"
        "if(o.signal&&o.signal.aborted){return;}for(i=0;i<a.length;i++){"
        "if(a[i].fn===fn&&a[i].capture===o.capture){return;}}"
        "var entry={id:0,type:t,fn:fn,capture:o.capture,once:o.once,passive:o.passive,owner:g,list:a,__removed:false};"
        "a.push(entry);if(o.signal&&typeof o.signal.addEventListener==='function'){"
        "o.signal.addEventListener('abort',function(){pRemoveListenerEntry(entry);},{once:true});}};"
        "g.removeEventListener=function(type,fn,capture){var t=String(type);"
        "var a;var i;if(t!=='popstate'&&t!=='hashchange'&&t!=='load'&&"
        "t!=='scroll'&&t!=='pagehide'&&t!=='pageshow'&&"
        "t!=='DOMContentLoaded'&&t!=='readystatechange'&&t!=='message'&&"
        "t!=='storage'){return;}"
        "a=pwindowListeners[t];for(i=a.length-1;i>=0;i--){"
        "if(a[i].fn===fn&&a[i].capture===pEventOptions(capture).capture){"
        "pRemoveListenerEntry(a[i]);}}};"
        "function pdispatchWindow(type,e){var h=g['on'+type];"
        "var a=pwindowListeners[type].slice(0);var i;"
        "if(typeof h==='function'){try{h.call(g,e);}catch(handlerError){}}"
        "for(i=0;i<a.length;i++){pInvokeListener(a[i],e,g);}}"
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
        "function pdispatchScroll(){var e={type:'scroll',target:g,"
        "currentTarget:g,bubbles:false,cancelable:false,defaultPrevented:false,"
        "isTrusted:false};e.preventDefault=function(){};pdispatchWindow('scroll',e);}"
        "function psetScroll(x,y){var nx=Number(x);var ny=Number(y);"
        "if(!isFinite(nx)){nx=0;}if(!isFinite(ny)){ny=0;}if(nx<0){nx=0;}"
        "if(ny<0){ny=0;}nx=Math.floor(nx);ny=Math.floor(ny);"
        "if(nx===pscrollX&&ny===pscrollY){return;}pscrollX=nx;pscrollY=ny;"
        "pdispatchScroll();}"
        "g.scrollTo=function(x,y){if(arguments.length===1&&typeof x==='object'){"
        "psetScroll(x.left||0,x.top||0);}else{psetScroll(x,y);}};"
        "g.scroll=function(x,y){g.scrollTo(x,y);};"
        "g.scrollBy=function(x,y){var nx=Number(x);var ny=Number(y);"
        "if(!isFinite(nx)){nx=0;}if(!isFinite(ny)){ny=0;}"
        "psetScroll(pscrollX+nx,pscrollY+ny);};"
        "Object.defineProperty(g,'scrollX',{get:function(){return pscrollX;},"
        "enumerable:true});Object.defineProperty(g,'scrollY',{get:function(){"
        "return pscrollY;},enumerable:true});"
        "Object.defineProperty(g,'pageXOffset',{get:function(){return pscrollX;},"
        "enumerable:true});Object.defineProperty(g,'pageYOffset',{get:function(){"
        "return pscrollY;},enumerable:true});"
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
        "function purlResolveInput(value,base){var v=String(value);var b=String(base);"
        "var m=/^([A-Za-z][A-Za-z0-9+.-]*:\\/\\/[^\\/?#]*)/.exec(b);"
        "var q=b.indexOf('?');var h=b.indexOf('#');var end=b.length;var slash;"
        "if(v.indexOf('://')>=0){return v;}if(!m){throw new Error('URL base unsupported');}"
        "if(h>=0&&h<end){end=h;}if(q>=0&&q<end){end=q;}"
        "if(v.charAt(0)==='#'){return b.substring(0,end)+v;}"
        "if(v.charAt(0)==='?'){return b.substring(0,end)+v;}"
        "if(v.charAt(0)==='/'){return m[1]+v;}"
        "slash=b.lastIndexOf('/',end-1);if(slash<0){return m[1]+'/'+v;}"
        "return b.substring(0,slash+1)+v;}"
        "function PUrl(value,base){var s=String(value);"
        "if(arguments.length>1){s=purlResolveInput(s,base);}"
        "if(s.indexOf('://')<0){throw new Error('URL must be absolute');}"
        "this.__href=s;this.__sync();}"
        "PUrl.prototype.__sync=function(){var m=/^([A-Za-z][A-Za-z0-9+.-]*:)"
        "(?:\\/\\/([^\\/?#]*))?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(this.__href);"
        "var host;var colon;var origin;var protocol;"
        "if(!m){throw new Error('URL parse failed');}protocol=m[1]||'';"
        "host=m[2]||'';this.__parts={protocol:protocol,host:host,"
        "hostname:host,port:'',pathname:m[3]||'/',search:m[4]||'',"
        "hash:m[5]||''};if(this.__parts.pathname===''){this.__parts.pathname='/';}"
        "if(host.charAt(0)==='['){colon=host.indexOf(']');if(colon>=0&&"
        "host.charAt(colon+1)===':'){this.__parts.hostname=host.substring(0,colon+1);"
        "this.__parts.port=host.substring(colon+2);}else{this.__parts.hostname=host;}}"
        "else{colon=host.lastIndexOf(':');if(colon>=0&&host.indexOf(':')===colon){"
        "this.__parts.hostname=host.substring(0,colon);this.__parts.port=host.substring(colon+1);}}"
        "origin=(protocol==='http:'||protocol==='https:')&&host!==''?protocol+'//'+host:'null';"
        "this.__parts.origin=origin;this.__searchParams=new PUrlSearchParams("
        "this.__parts.search);var self=this;this.__searchParams.__onchange=function(p){"
        "self.__setSearch(p.toString());};};"
        "PUrl.prototype.__setSearch=function(value){var s=String(value);"
        "if(s!==''&&s.charAt(0)!=='?'){s='?'+s;}this.__parts.search=s;"
        "this.__href=this.__parts.protocol+'//'+this.__parts.host+"
        "this.__parts.pathname+this.__parts.search+this.__parts.hash;};"
        "PUrl.prototype.toString=function(){return this.__href;};"
        "Object.defineProperty(PUrl.prototype,'href',{get:function(){return this.__href;},"
        "set:function(v){this.__href=String(v);this.__sync();}});"
        "Object.defineProperty(PUrl.prototype,'searchParams',{get:function(){"
        "return this.__searchParams;}});"
        "function purlPartProperty(name){Object.defineProperty(PUrl.prototype,name,{"
        "get:function(){return this.__parts[name];},set:function(v){var s=String(v);"
        "if(name==='search'){this.__setSearch(s);return;}if(name==='hash'){"
        "if(s!==''&&s.charAt(0)!=='#'){s='#'+s;}this.__parts.hash=s;}"
        "else if(name==='pathname'){this.__parts.pathname=s.charAt(0)==='/'?s:'/'+s;}"
        "else{this.__parts[name]=s;}this.__href=this.__parts.protocol+'//'+"
        "this.__parts.host+this.__parts.pathname+this.__parts.search+this.__parts.hash;}});};"
        "purlPartProperty('protocol');purlPartProperty('host');purlPartProperty('hostname');"
        "purlPartProperty('port');purlPartProperty('pathname');purlPartProperty('search');"
        "purlPartProperty('hash');Object.defineProperty(PUrl.prototype,'origin',"
        "{get:function(){return this.__parts.origin;}});"
        "PUrl.prototype.toJSON=function(){return this.href;};"
        "PUrl.canParse=function(value,base){try{new PUrl(value,base);return true;}"
        "catch(urlCanParseError){return false;}};"
        "PUrl.parse=function(value,base){try{return new PUrl(value,base);}"
        "catch(urlParseError){return null;}};"
        "Object.defineProperty(g,'URL',{value:PUrl,"
        "writable:false,configurable:false});"
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
        "plocation.toJSON=function(){return this.href;};"
        "var phidden=false;var pdocumentListeners={readystatechange:[],"
        "DOMContentLoaded:[],load:[],visibilitychange:[]};"
        "var preadyState='loading';"
        "function pdocumentAddEventListener(type,fn){var a;var i;"
        "type=String(type);if(typeof fn!=='function'||"
        "(type!=='readystatechange'&&type!=='DOMContentLoaded'&&"
        "type!=='load'&&type!=='visibilitychange')){return;}"
        "a=pdocumentListeners[type];"
        "for(i=0;i<a.length;i++){if(a[i]===fn){return;}}a.push(fn);}"
        "function pdocumentRemoveEventListener(type,fn){var a;var i;"
        "type=String(type);a=pdocumentListeners[type];if(!a){return;}"
        "for(i=a.length-1;i>=0;i--){if(a[i]===fn){a.splice(i,1);}}}"
        "function pdocumentDispatch(type){var a=pdocumentListeners[type].slice(0);"
        "var e={type:type,target:pdocument,currentTarget:pdocument,"
        "bubbles:false,cancelable:false,defaultPrevented:false,isTrusted:true};"
        "var i;for(i=0;i<a.length;i++){try{a[i].call(pdocument,e);}"
        "catch(documentListenerError){}}}"
        "function ppageEvent(type,target){return {type:type,target:target,"
        "currentTarget:target,bubbles:false,cancelable:false,"
        "defaultPrevented:false,isTrusted:true};}"
        "function ppageLifecycle(state){state=String(state);"
        "if(state==='interactive'&&preadyState==='loading'){"
        "preadyState='interactive';pdocumentDispatch('readystatechange');return;}"
        "if(state==='domcontentloaded'&&preadyState==='loading'){"
        "preadyState='interactive';pdocumentDispatch('readystatechange');}"
        "if(state==='domcontentloaded'&&preadyState==='interactive'){"
        "pdocumentDispatch('DOMContentLoaded');return;}"
        "if(state==='complete'){if(preadyState==='loading'){"
        "preadyState='interactive';pdocumentDispatch('readystatechange');}"
        "if(preadyState==='interactive'){pdocumentDispatch('DOMContentLoaded');}"
        "if(preadyState!=='complete'){preadyState='complete';"
        "pdocumentDispatch('readystatechange');"
        "pdispatchWindow('DOMContentLoaded',ppageEvent('DOMContentLoaded',g));"
        "pdispatchWindow('load',ppageEvent('load',g));"
        "pdocumentDispatch('load');}}}"
        "function pvisibilityEvent(type){return {type:type,target:pdocument,"
        "currentTarget:pdocument,bubbles:false,cancelable:false,"
        "defaultPrevented:false,isTrusted:true};}"
        "g.__pcoreVisibilityChange=function(hidden){var next=!!hidden;"
        "if(next===phidden){return;}phidden=next;"
        "pdocumentDispatch('visibilitychange');"
        "pdispatchWindow(next?'pagehide':'pageshow',ppageEvent("
        "next?'pagehide':'pageshow',g));};"
        "var pdocumentElementToken='__positron_document_element__';"
        "var pdocumentHeadToken='__positron_document_head__';"
        "var pdocumentBodyToken='__positron_document_body__';"
        "var pdocumentStructuralCache={};"
        "function pdocumentStructural(id){if(!__pcoreHasElement({id:id})){return null;}"
        "if(!pdocumentStructuralCache[id]){pdocumentStructuralCache[id]=new PElement(id);}"
        "return pdocumentStructuralCache[id];}"
        "var pdocument={getElementById:function(id){id=String(id);"
        "return __pcoreHasElement({id:id})?new PElement(id):null;},"
        "addEventListener:pdocumentAddEventListener,"
        "removeEventListener:pdocumentRemoveEventListener};"
        "Object.defineProperty(pdocument,'nodeType',{value:9,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdocument,'nodeName',"
        "{value:'#document',writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdocument,'ELEMENT_NODE',{value:1,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdocument,'DOCUMENT_NODE',"
        "{value:9,writable:false,configurable:false,enumerable:true});"
        "pdocument.querySelector=function(selector){var s=PTrim(selector);"
        "var lower=s.toLowerCase();if(lower==='html'||s===':root'){"
        "return this.documentElement;}if(lower==='head'){return this.head;}"
        "if(lower==='body'){return this.body;}if(s.charAt(0)==='#'&&s.length>1){"
        "return this.getElementById(s.substring(1));}return null;};"
        "pdocument.querySelectorAll=function(selector){var a=[];var e="
        "this.querySelector(selector);if(e!==null){a.push(e);}return a;};"
        "function pdocumentCollection(a){if(typeof g.__pcoreDecorateCollection13==='function'){"
        "return g.__pcoreDecorateCollection13(a,'HTMLCollection',true);}return a;}"
        "pdocument.getElementsByTagName=function(name){var a=[];var e=this.documentElement;"
        "var d;var i;var s=String(name).toLowerCase();if(s===''){return pdocumentCollection(a);}"
        "if(e!==null&&(s==='*'||e.localName===s)){a.push(e);}"
        "if(e!==null&&typeof e.getElementsByTagName==='function'){d=e.getElementsByTagName(name);"
        "for(i=0;i<d.length;i++){a.push(d[i]);}}return pdocumentCollection(a);};"
        "pdocument.getElementsByClassName=function(names){var a=[];var e=this.documentElement;"
        "var d;var i;var s=String(names).replace(/^\\s+|\\s+$/g,'');var p;var q;"
        "if(e!==null&&s!==''){p=s.split(/\\s+/);q='.';for(i=0;i<p.length;i++){"
        "if(i>0){q+='.';}q+=p[i];}if(e.matches(q)){a.push(e);}}"
        "if(e!==null&&typeof e.getElementsByClassName==='function'){d=e.getElementsByClassName(names);"
        "for(i=0;i<d.length;i++){a.push(d[i]);}}return pdocumentCollection(a);};"
        "Object.defineProperty(pdocument,'documentElement',{get:function(){"
        "return pdocumentStructural(pdocumentElementToken);},enumerable:true});"
        "Object.defineProperty(pdocument,'head',{get:function(){"
        "return pdocumentStructural(pdocumentHeadToken);},enumerable:true});"
        "Object.defineProperty(pdocument,'body',{get:function(){"
        "return pdocumentStructural(pdocumentBodyToken);},enumerable:true});"
        "var pdocumentTitle='';var pdoctype={};"
        "Object.defineProperty(pdoctype,'name',{value:'html',writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'nodeType',"
        "{value:10,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'nodeName',{value:'html',writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'nodeValue',"
        "{value:null,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'textContent',{value:null,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'ownerDocument',"
        "{get:function(){return pdocument;},enumerable:true});"
        "Object.defineProperty(pdoctype,'parentNode',{get:function(){return pdocument;},"
        "enumerable:true});Object.defineProperty(pdoctype,'parentElement',{value:null,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'isConnected',{value:true,writable:false,"
        "configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'previousSibling',{value:null,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'nextSibling',"
        "{get:function(){return pdocument.documentElement;},enumerable:true});"
        "Object.defineProperty(pdoctype,'previousElementSibling',{value:null,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'nextElementSibling',"
        "{get:function(){return pdocument.documentElement;},enumerable:true});"
        "var pdoctypeChildren=Object.freeze([]);Object.defineProperty(pdoctype,'childNodes',"
        "{value:pdoctypeChildren,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'children',{value:pdoctypeChildren,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'firstChild',"
        "{value:null,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'lastChild',{value:null,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'firstElementChild',"
        "{value:null,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'lastElementChild',{value:null,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdoctype,'childElementCount',"
        "{value:0,writable:false,configurable:false,enumerable:true});"
        "pdoctype.hasChildNodes=function(){return false;};"
        "pdoctype.isSameNode=function(other){return typeof g.__pcoreNodeSame12==='function'?"
        "g.__pcoreNodeSame12(pdoctype,other):other===pdoctype;};"
        "pdoctype.isEqualNode=function(other){var ok=other===pdoctype;"
        "if(!ok&&other){ok=Number(other.nodeType)===10&&String(other.nodeName)==='html';}"
        "return ok;};pdoctype.getRootNode=function(options){return pdocument;};"
        "pdoctype.compareDocumentPosition=function(other){return typeof g.__pcoreNodePosition12==='function'?"
        "g.__pcoreNodePosition12(pdoctype,other):(other===pdoctype?0:"
        "other===pdocument?10:other===pdocument.documentElement?4:33);};"
        "pdoctype.contains=function(other){return typeof g.__pcoreNodeContains12==='function'?"
        "g.__pcoreNodeContains12(pdoctype,other):other===pdoctype;};"
        "pdoctype.toString=function(){return '[object DocumentType]';};"
        "if(g.Symbol&&g.Symbol.toStringTag){Object.defineProperty(pdoctype,g.Symbol.toStringTag,"
        "{value:'DocumentType',writable:false,configurable:false});}"
        "var pdocumentChildrenCache=null;function pdocumentChildren(){var a,e;"
        "if(pdocumentChildrenCache!==null){return pdocumentChildrenCache;}a=[];a.push(pdoctype);"
        "e=pdocument.documentElement;if(e!==null){a.push(e);}"
        "if(typeof g.__pcoreDecorateCollection13==='function'){pdocumentChildrenCache="
        "g.__pcoreDecorateCollection13(a,'NodeList',false);}else{pdocumentChildrenCache=a;}"
        "return pdocumentChildrenCache;}"
        "var pdocumentElementsCache=null;function pdocumentElements(){var a,e;"
        "if(pdocumentElementsCache!==null){return pdocumentElementsCache;}a=[];"
        "e=pdocument.documentElement;if(e!==null){a.push(e);}"
        "if(typeof g.__pcoreDecorateCollection13==='function'){pdocumentElementsCache="
        "g.__pcoreDecorateCollection13(a,'HTMLCollection',true);}else{pdocumentElementsCache=a;}"
        "return pdocumentElementsCache;}"
        "Object.defineProperty(pdocument,'childNodes',{get:function(){return pdocumentChildren();},"
        "enumerable:true});Object.defineProperty(pdocument,'children',{get:function(){return pdocumentElements();},"
        "enumerable:true});Object.defineProperty(pdocument,'firstChild',{get:function(){"
        "var a=pdocumentChildren();return a.length?a[0]:null;},enumerable:true});"
        "Object.defineProperty(pdocument,'lastChild',{get:function(){var a=pdocumentChildren();"
        "return a.length?a[a.length-1]:null;},enumerable:true});"
        "Object.defineProperty(pdocument,'firstElementChild',{get:function(){return pdocument.documentElement;},"
        "enumerable:true});Object.defineProperty(pdocument,'lastElementChild',{get:function(){"
        "return pdocument.documentElement;},enumerable:true});"
        "Object.defineProperty(pdocument,'childElementCount',{get:function(){return pdocument.documentElement===null?0:1;},"
        "enumerable:true});pdocument.hasChildNodes=function(){return pdocumentChildren().length>0;};"
        "Object.defineProperty(pdocument,'previousSibling',{value:null,writable:false,"
        "configurable:false,enumerable:true});Object.defineProperty(pdocument,'nextSibling',{value:null,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdoctype,'baseURI',{get:function(){return g.location&&g.location.href!==undefined?"
        "String(g.location.href):'';},enumerable:true});Object.defineProperty(pdoctype,'namespaceURI',"
        "{value:null,writable:false,configurable:false,enumerable:true});Object.defineProperty(pdoctype,'prefix',"
        "{value:null,writable:false,configurable:false,enumerable:true});"
        "pdoctype.isDefaultNamespace=function(v){return v===null||v===undefined;};"
        "pdoctype.lookupNamespaceURI=function(v){return v===null||v===undefined||String(v)===''?null:"
        "(String(v)==='xml'?'http://www.w3.org/XML/1998/namespace':null);};"
        "Object.freeze(pdoctype);"
        "Object.defineProperty(pdocument,'title',{get:function(){"
        "return pdocumentTitle;},set:function(v){pdocumentTitle=String(v);},"
        "enumerable:true});"
        "Object.defineProperty(pdocument,'characterSet',{value:'UTF-8',"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdocument,'charset',{get:function(){"
        "return this.characterSet;},enumerable:true});"
        "Object.defineProperty(pdocument,'contentType',{value:'text/html',"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdocument,'compatMode',{value:'CSS1Compat',"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdocument,'doctype',{value:pdoctype,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(pdocument,'readyState',{get:function(){"
        "return preadyState;},enumerable:true});"
        "Object.defineProperty(pdocument,'hidden',{get:function(){"
        "return phidden;},enumerable:true});"
        "Object.defineProperty(pdocument,'visibilityState',{get:function(){"
        "return phidden?'hidden':'visible';},enumerable:true});"
        "var pcookieData={};var pcookieKeys=[];"
        "function pcookieGet(){var s='';var i;"
        "for(i=0;i<pcookieKeys.length;i++){if(i>0){s+='; ';}"
        "s+=pcookieKeys[i]+'='+pcookieData[pcookieKeys[i]];}return s;}"
        "function pcookieSet(value){var s=String(value);var semi=s.indexOf(';');"
        "var first=semi<0?s:s.substring(0,semi);var eq=first.indexOf('=');"
        "var name;var val;var i;if(eq<=0){return;}name=first.substring(0,eq);"
        "val=first.substring(eq+1);if(name.indexOf(';')>=0||name.indexOf('=')>=0){return;}"
        "if(s.toLowerCase().indexOf('max-age=0')>=0){val='';}"
        "if(val===''){delete pcookieData[name];for(i=0;i<pcookieKeys.length;i++){"
        "if(pcookieKeys[i]===name){pcookieKeys.splice(i,1);break;}}return;}"
        "if(!pcookieData.hasOwnProperty(name)){pcookieKeys.push(name);}pcookieData[name]=val;}"
        "Object.defineProperty(pdocument,'cookie',{get:pcookieGet,set:pcookieSet,"
        "enumerable:true});"
        "Object.defineProperty(g,'__pcorePageLifecycle',{value:ppageLifecycle,"
        "writable:false,configurable:false});"
        "Object.defineProperty(pdocument,'URL',{get:function(){"
        "return plocation.href;}});"
        "Object.defineProperty(pdocument,'documentURI',{get:function(){"
        "return plocation.href;}});"
        "Object.defineProperty(pdocument,'location',{get:function(){"
        "return plocation;},set:pnavigate});"
        "Object.defineProperty(pdocument,'defaultView',{get:function(){return g;},enumerable:true});"
        "g.window=g;g.self=g;g.top=g;g.parent=g;g.frames=g;g.document=pdocument;"
        "g.closed=false;g.length=0;g.opener=null;g.open=function(url,target,features){return null;};"
        "g.close=function(){};"
        "var pwindowName='';"
        "Object.defineProperty(g,'name',{get:function(){return pwindowName;},"
        "set:function(v){pwindowName=String(v);},enumerable:true});"
        "g.navigator={userAgent:'Positron/0.1 (Windows CE)',appCodeName:'Mozilla',"
        "appName:'Netscape',appVersion:'0.1 (Windows CE)',vendor:'Positron',"
        "vendorSub:'',product:'Gecko',productSub:'20100101',platform:'WinCE',"
        "language:'en-US',languages:['en-US'],onLine:true,cookieEnabled:true,"
        "hardwareConcurrency:1,maxTouchPoints:1,doNotTrack:null,pdfViewerEnabled:false,"
        "javaEnabled:function(){return false;},sendBeacon:function(url,data){return false;}};"
        "Object.freeze(g.navigator.languages);Object.freeze(g.navigator);"
        "Object.defineProperty(g,'location',{get:function(){"
        "return plocation;},set:pnavigate});"
        "var pviewportWidth=Number(g.__pcoreViewportWidth||0);"
        "var pviewportHeight=Number(g.__pcoreViewportHeight||0);"
        "var pdevicePixelRatio=Number(g.__pcoreDevicePixelRatio||1);"
        "if(!isFinite(pviewportWidth)||pviewportWidth<0){pviewportWidth=0;}"
        "if(!isFinite(pviewportHeight)||pviewportHeight<0){pviewportHeight=0;}"
        "if(!isFinite(pdevicePixelRatio)||pdevicePixelRatio<=0){pdevicePixelRatio=1;}"
        "Object.defineProperty(g,'innerWidth',{value:pviewportWidth,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(g,'innerHeight',{value:pviewportHeight,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(g,'outerWidth',{value:pviewportWidth,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(g,'outerHeight',{value:pviewportHeight,"
        "writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(g,'devicePixelRatio',{value:pdevicePixelRatio,"
        "writable:false,configurable:false,enumerable:true});"
        "g.screen=Object.freeze({width:pviewportWidth,height:pviewportHeight,"
        "availWidth:pviewportWidth,availHeight:pviewportHeight,left:0,top:0,"
        "availLeft:0,availTop:0,colorDepth:16,pixelDepth:16,orientation:Object.freeze({"
        "type:pviewportWidth>=pviewportHeight?'landscape-primary':'portrait-primary',"
        "angle:pviewportWidth>=pviewportHeight?90:0})});"
        "function PMediaQueryList(query,matches){this.media=String(query);this.matches=!!matches;"
        "this.onchange=null;this.__listeners=[];}"
        "PMediaQueryList.prototype.addEventListener=function(type,fn,options){"
        "if(String(type)==='change'&&typeof fn==='function'){this.__listeners.push(fn);}};"
        "PMediaQueryList.prototype.removeEventListener=function(type,fn,options){var i;"
        "if(String(type)!=='change'){return;}for(i=this.__listeners.length-1;i>=0;i--){"
        "if(this.__listeners[i]===fn){this.__listeners.splice(i,1);}}};"
        "PMediaQueryList.prototype.addListener=PMediaQueryList.prototype.addEventListener;"
        "PMediaQueryList.prototype.removeListener=PMediaQueryList.prototype.removeEventListener;"
        "function pmediaMatch(query){var q=String(query).toLowerCase();var n;"
        "var m=/\\(min-width\\s*:\\s*([0-9]+)px\\)/.exec(q);if(m&&pviewportWidth<Number(m[1])){return false;}"
        "m=/\\(max-width\\s*:\\s*([0-9]+)px\\)/.exec(q);if(m&&pviewportWidth>Number(m[1])){return false;}"
        "m=/\\(min-height\\s*:\\s*([0-9]+)px\\)/.exec(q);if(m&&pviewportHeight<Number(m[1])){return false;}"
        "m=/\\(max-height\\s*:\\s*([0-9]+)px\\)/.exec(q);if(m&&pviewportHeight>Number(m[1])){return false;}"
        "m=/\\(resolution\\s*:\\s*([0-9.]+)dppx\\)/.exec(q);if(m&&pdevicePixelRatio!==Number(m[1])){return false;}"
        "return q.indexOf('screen')<0||true;}"
        "g.matchMedia=function(query){return new PMediaQueryList(query,pmediaMatch(query));};"
        "var pperfOrigin=(new Date()).getTime();var pperfEntries=[];"
        "var pperformance={now:function(){return (new Date()).getTime()-pperfOrigin;},"
        "timeOrigin:pperfOrigin,mark:function(name){var n=String(name);var t=this.now();"
        "pperfEntries.push({name:n,entryType:'mark',startTime:t,duration:0});return pperfEntries[pperfEntries.length-1];},"
        "measure:function(name,start,end){var n=String(name);var a=0;var b=this.now();var i;"
        "for(i=0;i<pperfEntries.length;i++){if(pperfEntries[i].name===String(start)){a=pperfEntries[i].startTime;}"
        "if(pperfEntries[i].name===String(end)){b=pperfEntries[i].startTime;}}"
        "var e={name:n,entryType:'measure',startTime:a,duration:b-a};pperfEntries.push(e);return e;},"
        "getEntries:function(){return pperfEntries.slice(0);},getEntriesByName:function(name,type){"
        "var out=[];var i;for(i=0;i<pperfEntries.length;i++){if(pperfEntries[i].name===String(name)&&"
        "(!type||pperfEntries[i].entryType===String(type))){out.push(pperfEntries[i]);}}return out;},"
        "getEntriesByType:function(type){var out=[];var i;for(i=0;i<pperfEntries.length;i++){"
        "if(pperfEntries[i].entryType===String(type)){out.push(pperfEntries[i]);}}return out;},"
        "clearMarks:function(name){var n=name===undefined?null:String(name);var out=[];var i;"
        "for(i=0;i<pperfEntries.length;i++){if(pperfEntries[i].entryType!=='mark'||(n!==null&&pperfEntries[i].name!==n)){out.push(pperfEntries[i]);}}pperfEntries=out;},"
        "clearMeasures:function(name){var n=name===undefined?null:String(name);var out=[];var i;"
        "for(i=0;i<pperfEntries.length;i++){if(pperfEntries[i].entryType!=='measure'||(n!==null&&pperfEntries[i].name!==n)){out.push(pperfEntries[i]);}}pperfEntries=out;}};"
        "Object.defineProperty(g,'performance',{value:pperformance,writable:false,configurable:false});"
        "var phistory={back:function(){__pcoreNavigation({op:'back'});},"
        "forward:function(){__pcoreNavigation({op:'forward'});},"
        "go:function(delta){var n=Number(arguments.length?delta:0);"
        "if(!isFinite(n)||Math.floor(n)!==n||n < -15||n > 15){return;}"
        "__pcoreNavigation({op:'go',delta:n});}};"
        "Object.defineProperty(phistory,'length',{get:function(){"
        "return phistoryLength;},enumerable:true});"
        "Object.defineProperty(phistory,'state',{get:function(){"
        "return JSON.parse(phistoryStateJson);},enumerable:true});"
        "var pscrollRestoration='auto';Object.defineProperty(phistory,'scrollRestoration',{"
        "get:function(){return pscrollRestoration;},set:function(v){var s=String(v);"
        "if(s==='auto'||s==='manual'){pscrollRestoration=s;}},enumerable:true});"
        "phistory.replaceState=preplaceState;"
        "phistory.pushState=ppushState;g.history=phistory;"
        "})(this);";

    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART3[] =
        "(function(g){"
        "var b64='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';"
        "g.btoa=function(value){var s=String(value);var out='';var i;var a;var b;var c;"
        "if(s.length>8192){throw new Error('btoa input too large');}"
        "for(i=0;i<s.length;i+=3){a=s.charCodeAt(i);if(a>255){throw new Error('btoa binary');}"
        "b=i+1<s.length?s.charCodeAt(i+1):0;c=i+2<s.length?s.charCodeAt(i+2):0;"
        "if(b>255||c>255){throw new Error('btoa binary');}out+=b64.charAt(a>>2);"
        "out+=b64.charAt(((a&3)<<4)|(b>>4));out+=i+1<s.length?b64.charAt(((b&15)<<2)|(c>>6)):'=';"
        "out+=i+2<s.length?b64.charAt(c&63):'=';}return out;};"
        "g.atob=function(value){var s=String(value).replace(/\\s/g,'');var out='';"
        "var i;var a;var b;var c;var d;if(s.length>10924||s.length%4!==0){throw new Error('atob input');}"
        "for(i=0;i<s.length;i+=4){a=b64.indexOf(s.charAt(i));b=b64.indexOf(s.charAt(i+1));"
        "c=s.charAt(i+2)==='='?-1:b64.indexOf(s.charAt(i+2));d=s.charAt(i+3)==='='?-1:b64.indexOf(s.charAt(i+3));"
        "if(a<0||b<0||c<-1||d<-1){throw new Error('atob input');}out+=String.fromCharCode((a<<2)|(b>>4));"
        "if(c>=0){out+=String.fromCharCode(((b&15)<<4)|(c>>2));}if(d>=0&&c>=0){"
        "out+=String.fromCharCode(((c&3)<<6)|d);}}return out;};"
        "function utf8(value){var s=String(value);var out=[];var i;var c;var d;"
        "if(s.length>8192){throw new Error('UTF-8 input too large');}for(i=0;i<s.length;i++){c=s.charCodeAt(i);"
        "if(c>=55296&&c<=56319&&i+1<s.length){d=s.charCodeAt(i+1);if(d>=56320&&d<=57343){"
        "c=65536+((c-55296)<<10)+(d-56320);i++;}}if(c<128){out.push(c);}else if(c<2048){"
        "out.push(192|(c>>6),128|(c&63));}else if(c<65536){out.push(224|(c>>12),"
        "128|((c>>6)&63),128|(c&63));}else{out.push(240|(c>>18),128|((c>>12)&63),"
        "128|((c>>6)&63),128|(c&63));}}return out;}"
        "function unutf8(bytes){var out='';var i=0;var c;var d;var e;var f;var cp;"
        "while(i<bytes.length){c=Number(bytes[i++])&255;if(c<128){out+=String.fromCharCode(c);}"
        "else if(c>=192&&c<224&&i<bytes.length){d=Number(bytes[i++])&63;out+=String.fromCharCode(((c&31)<<6)|d);}"
        "else if(c>=224&&c<240&&i+1<bytes.length){d=Number(bytes[i++])&63;e=Number(bytes[i++])&63;"
        "out+=String.fromCharCode(((c&15)<<12)|(d<<6)|e);}else if(c>=240&&c<248&&i+2<bytes.length){"
        "d=Number(bytes[i++])&63;e=Number(bytes[i++])&63;f=Number(bytes[i++])&63;cp=((c&7)<<18)|(d<<12)|(e<<6)|f;"
        "cp-=65536;out+=String.fromCharCode(55296+(cp>>10),56320+(cp&1023));}else{out+='\\ufffd';}}return out;}"
        "function TextEncoder(){this.encoding='utf-8';}TextEncoder.prototype.encode=function(value){"
        "var a=utf8(value);var out=new Uint8Array(a.length);var i;for(i=0;i<a.length;i++){out[i]=a[i];}return out;};"
        "TextEncoder.prototype.encodeInto=function(source,destination){var s=String(source);var a=[];"
        "var read=0;var bytes=[];var next;var limit=destination&&typeof destination.length==='number'?destination.length:0;"
        "while(read<s.length){next=utf8(s.substring(0,read+1));if(next.length>limit){break;}bytes=next;read++;}"
        "var i;for(i=0;i<bytes.length;i++){destination[i]=bytes[i];}return {read:read,written:bytes.length};};"
        "function TextDecoder(label,options){var o=options||{};this.encoding=String(label||'utf-8').toLowerCase();"
        "this.fatal=!!o.fatal;this.ignoreBOM=!!o.ignoreBOM;"
        "if(this.encoding!=='utf-8'&&this.encoding!=='utf8'){throw new Error('encoding');}}"
        "TextDecoder.prototype.decode=function(input){var a=[];var i;if(input===undefined||input===null){return '';}"
        "for(i=0;i<input.length;i++){a.push(input[i]);}return unutf8(a);};"
        "Object.defineProperty(g,'TextEncoder',{value:TextEncoder,writable:false,configurable:false});"
        "Object.defineProperty(g,'TextDecoder',{value:TextDecoder,writable:false,configurable:false});"
        "function partText(part){if(part instanceof g.Blob){return part.__text;}if(typeof part==='string'){return part;}"
        "if(part&&typeof part.length==='number'){return unutf8(part);}return String(part);}"
        "function blobType(value){var t=String(value||'').toLowerCase();return /^[a-z0-9!#$%&'*+.^_`|~-]+\\/[a-z0-9!#$%&'*+.^_`|~-]+$/.test(t)?t:'';}"
        "function Blob(parts,options){var a=parts||[];var i;var s='';for(i=0;i<a.length&&s.length<16384;i++){s+=partText(a[i]);}"
        "if(s.length>16384){s=s.substring(0,16384);}this.__text=s;this.type=blobType(options&&options.type);this.size=utf8(s).length;}"
        "Blob.prototype.slice=function(start,end,type){var a=Number(start);var b=Number(end);"
        "if(isNaN(a)){a=0;}else if(a===-Infinity){a=0;}else if(a===Infinity){a=this.__text.length;}"
        "if(end===undefined||isNaN(b)){b=end===undefined?this.__text.length:0;}else if(b===-Infinity){b=0;}"
        "else if(b===Infinity){b=this.__text.length;}a=Math.floor(a);b=Math.floor(b);if(a<0){a=Math.max(this.__text.length+a,0);}"
        "if(b<0){b=Math.max(this.__text.length+b,0);}if(a<0){a=0;}if(b<a){b=a;}return new Blob([this.__text.substring(a,b)],{type:type||''});};"
        "Blob.prototype.text=function(){return this.__text;};Blob.prototype.arrayBuffer=function(){return new TextEncoder().encode(this.__text);};"
        "function File(parts,name,options){Blob.call(this,parts,options);this.name=String(name||'');this.lastModified=Number(options&&options.lastModified);"
        "if(!isFinite(this.lastModified)){this.lastModified=0;}}File.prototype=Object.create(Blob.prototype);File.prototype.constructor=File;"
        "Object.defineProperty(g,'Blob',{value:Blob,writable:false,configurable:false});Object.defineProperty(g,'File',{value:File,writable:false,configurable:false});"
        "})(this);";

    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART4[] =
        "(function(g){"
        "function PDOMException(message,name){this.message=String(message||'');"
        "this.name=String(name||'Error');this.code=PDOMException._code(this.name);}"
        "PDOMException._code=function(name){var m={IndexSizeError:1,DOMStringSizeError:2,"
        "HierarchyRequestError:3,WrongDocumentError:4,InvalidCharacterError:5,"
        "NoModificationAllowedError:7,NotFoundError:8,NotSupportedError:9,"
        "InUseAttributeError:10,InvalidStateError:11,SyntaxError:12,"
        "InvalidModificationError:13,NamespaceError:14,TypeMismatchError:17,"
        "SecurityError:18,NetworkError:19,AbortError:20,URLMismatchError:21,"
        "QuotaExceededError:22,TimeoutError:23,InvalidNodeTypeError:24,"
        "DataCloneError:25};return m[name]||0;};"
        "PDOMException.prototype.toString=function(){return this.name+': '+this.message;};"
        "Object.defineProperty(g,'DOMException',{value:PDOMException,writable:false,configurable:false});"
        "function pheaderName(value){var s=String(value).replace(/^\\s+|\\s+$/g,'');"
        "var i;var c;if(s===''){throw new PDOMException('Invalid header name','TypeError');}"
        "for(i=0;i<s.length;i++){c=s.charCodeAt(i);if(!((c>=48&&c<=57)||(c>=65&&c<=90)||"
        "(c>=97&&c<=122)||c===33||c===35||c===36||c===37||c===38||c===39||c===42||"
        "c===43||c===45||c===46||c===94||c===95||c===96||c===124||c===126)){"
        "throw new PDOMException('Invalid header name','TypeError');}}return s.toLowerCase();}"
        "function pheaderValue(value){return String(value).replace(/^\\s+|\\s+$/g,'');}"
        "function PHeaders(init){this.__pairs=[];var i;var k;var p;"
        "if(init instanceof PHeaders){for(i=0;i<init.__pairs.length;i++){"
        "this.__pairs.push([init.__pairs[i][0],init.__pairs[i][1]]);}return;}"
        "if(init instanceof Array){for(i=0;i<init.length;i++){p=init[i];"
        "if(p instanceof Array&&p.length>=2){this.append(p[0],p[1]);}}return;}"
        "if(init&&typeof init==='object'){for(k in init){if(init.hasOwnProperty(k)){"
        "this.append(k,init[k]);}}}}"
        "function pheaderIndex(owner,name){var i;for(i=0;i<owner.__pairs.length;i++){"
        "if(owner.__pairs[i][0]===name){return i;}}return -1;}"
        "PHeaders.prototype.append=function(name,value){var n=pheaderName(name);var v=pheaderValue(value);"
        "var i=pheaderIndex(this,n);if(i<0){if(this.__pairs.length>=128){throw new PDOMException('Header limit','QuotaExceededError');}"
        "this.__pairs.push([n,v]);}else{this.__pairs[i][1]+=', '+v;}};"
        "PHeaders.prototype.set=function(name,value){var n=pheaderName(name);var v=pheaderValue(value);"
        "var i=pheaderIndex(this,n);if(i<0){if(this.__pairs.length>=128){throw new PDOMException('Header limit','QuotaExceededError');}"
        "this.__pairs.push([n,v]);}else{this.__pairs[i][1]=v;}};"
        "PHeaders.prototype.get=function(name){var i=pheaderIndex(this,pheaderName(name));"
        "return i<0?null:this.__pairs[i][1];};"
        "PHeaders.prototype.getSetCookie=function(){var v=this.get('set-cookie');"
        "return v===null?[]:v.split(/,\\s*/);};"
        "PHeaders.prototype.has=function(name){return pheaderIndex(this,pheaderName(name))>=0;};"
        "PHeaders.prototype.delete=function(name){var n=pheaderName(name);var i=pheaderIndex(this,n);"
        "if(i>=0){this.__pairs.splice(i,1);}};"
        "PHeaders.prototype.forEach=function(fn,thisArg){var i;if(typeof fn!=='function'){return;}"
        "for(i=0;i<this.__pairs.length;i++){fn.call(thisArg,this.__pairs[i][1],this.__pairs[i][0],this);}};"
        "function pheadersIterator(owner,kind){var i=0;var it={next:function(){var p;"
        "if(i>=owner.__pairs.length){return {value:undefined,done:true};}p=owner.__pairs[i++];"
        "return {value:kind==='keys'?p[0]:(kind==='values'?p:[p[0],p[1]]),done:false};}};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(it,Symbol.iterator,{value:function(){return it;}});}return it;}"
        "PHeaders.prototype.entries=function(){return pheadersIterator(this,'entries');};"
        "PHeaders.prototype.keys=function(){return pheadersIterator(this,'keys');};"
        "PHeaders.prototype.values=function(){return pheadersIterator(this,'values');};"
        "PHeaders.prototype.toJSON=function(){var o={};var i;for(i=0;i<this.__pairs.length;i++){o[this.__pairs[i][0]]=this.__pairs[i][1];}return o;};"
        "if(typeof Symbol==='function'&&Symbol.iterator){Object.defineProperty(PHeaders.prototype,Symbol.iterator,{value:function(){return this.entries();}});}"
        "Object.defineProperty(PHeaders.prototype,'size',{get:function(){return this.__pairs.length;}});"
        "Object.defineProperty(g,'Headers',{value:PHeaders,writable:false,configurable:false});"
        "function pbodyText(body){var i;var s='';if(body===null||body===undefined){return '';}"
        "if(body instanceof g.Blob){return body.__text;}if(typeof body==='string'){return body;}"
        "if(body&&typeof body.length==='number'){for(i=0;i<body.length&&s.length<16384;i++){s+=String.fromCharCode(Number(body[i])&255);}return s;}"
        "return String(body);}"
        "function PRequest(input,init){var o=init||{};var source=input instanceof PRequest?input:null;"
        "this.url=source?source.url:String(input||'');this.method=String(o.method||"
        "(source?source.method:'GET')).toUpperCase();this.headers=new PHeaders(o.headers||"
        "(source?source.headers:null));this.destination=String(o.destination||'');"
        "this.referrer=String(o.referrer||'about:client');this.referrerPolicy=String(o.referrerPolicy||'');"
        "this.mode=String(o.mode||'cors');this.credentials=String(o.credentials||'same-origin');"
        "this.cache=String(o.cache||'default');this.redirect=String(o.redirect||'follow');"
        "this.integrity=String(o.integrity||'');this.keepalive=!!o.keepalive;this.signal=o.signal||"
        "(source?source.signal:null);this.body=source?source.body:(o.body===undefined?null:o.body);"
        "this.bodyUsed=false;}"
        "PRequest.prototype.text=function(){this.bodyUsed=true;return pbodyText(this.body);};"
        "PRequest.prototype.json=function(){return JSON.parse(this.text());};"
        "PRequest.prototype.arrayBuffer=function(){return new g.TextEncoder().encode(this.text());};"
        "PRequest.prototype.clone=function(){if(this.bodyUsed){throw new PDOMException('Body used','TypeError');}"
        "return new PRequest(this);};"
        "Object.defineProperty(g,'Request',{value:PRequest,writable:false,configurable:false});"
        "function PResponse(body,init){var o=init||{};this.status=Number(o.status===undefined?200:o.status);"
        "if(!isFinite(this.status)||this.status<0){this.status=200;}this.status=Math.floor(this.status);"
        "this.statusText=String(o.statusText||'');this.headers=new PHeaders(o.headers||null);"
        "this.url=String(o.url||'');this.type=String(o.type||'default');this.redirected=!!o.redirected;"
        "this.body=body===undefined?null:body;this.bodyUsed=false;this.ok=this.status>=200&&this.status<300;}"
        "PResponse.prototype.text=function(){this.bodyUsed=true;return pbodyText(this.body);};"
        "PResponse.prototype.json=function(){return JSON.parse(this.text());};"
        "PResponse.prototype.arrayBuffer=function(){return new g.TextEncoder().encode(this.text());};"
        "PResponse.prototype.clone=function(){if(this.bodyUsed){throw new PDOMException('Body used','TypeError');}"
        "return new PResponse(this.body,{status:this.status,statusText:this.statusText,headers:this.headers,url:this.url,type:this.type,redirected:this.redirected});};"
        "PResponse.error=function(){return new PResponse(null,{status:0,type:'error'});};"
        "PResponse.json=function(value,init){var o=init||{};var h=o.headers||{};if(!h['content-type']){h['content-type']='application/json';}"
        "return new PResponse(JSON.stringify(value),{status:o.status||200,statusText:o.statusText,headers:h,url:o.url||''});};"
        "PResponse.redirect=function(url,status){var n=Number(status||302);return new PResponse(null,{status:n,url:String(url||''),redirected:true});};"
        "Object.defineProperty(g,'Response',{value:PResponse,writable:false,configurable:false});"
        "function PMessagePort(){g.__pcoreEventTarget.call(this);this.__closed=false;this.__started=false;"
        "this.__target=null;this.__onmessage=null;this.__onmessageerror=null;}PMessagePort.prototype=Object.create(g.__pcoreEventTarget.prototype);"
        "PMessagePort.prototype.constructor=PMessagePort;PMessagePort.prototype.start=function(){this.__started=true;};"
        "PMessagePort.prototype.close=function(){this.__closed=true;};"
        "PMessagePort.prototype.postMessage=function(data){if(this.__closed||!this.__target||this.__target.__closed){return;}"
        "var copy=data;try{if(typeof g.structuredClone==='function'){copy=g.structuredClone(data);}}"
        "catch(portCloneError){if(g.__pcoreExtraMessages&&g.__pcoreExtraMessages.length<64){"
        "g.__pcoreExtraMessages.push({port:this.__target,data:null,error:true,ports:[]});}return;}"
        "if(g.__pcoreExtraMessages&&g.__pcoreExtraMessages.length<64){g.__pcoreExtraMessages.push({port:this.__target,data:copy,ports:[]});}};"
        "Object.defineProperty(PMessagePort.prototype,'onmessage',{get:function(){return this.__onmessage;},set:function(fn){"
        "if(this.__onmessage){this.removeEventListener('message',this.__onmessage,false);}this.__onmessage=typeof fn==='function'?fn:null;"
        "if(this.__onmessage){this.start();this.addEventListener('message',this.__onmessage,false);}}});"
        "Object.defineProperty(PMessagePort.prototype,'onmessageerror',{get:function(){return this.__onmessageerror;},set:function(fn){"
        "if(this.__onmessageerror){this.removeEventListener('messageerror',this.__onmessageerror,false);}"
        "this.__onmessageerror=typeof fn==='function'?fn:null;if(this.__onmessageerror){"
        "this.addEventListener('messageerror',this.__onmessageerror,false);}}});"
        "function PMessageChannel(){this.port1=new PMessagePort();this.port2=new PMessagePort();"
        "this.port1.__target=this.port2;this.port2.__target=this.port1;}"
        "Object.defineProperty(g,'MessagePort',{value:PMessagePort,writable:false,configurable:false});"
        "Object.defineProperty(g,'MessageChannel',{value:PMessageChannel,writable:false,configurable:false});"
        "function pclone(value,depth){var out;var i;var k;if(depth>16){throw new PDOMException('Clone depth','DataCloneError');}"
        "if(value===null||typeof value==='string'||typeof value==='number'||typeof value==='boolean'){return value;}"
        "if(typeof value==='undefined'){return undefined;}if(typeof value==='function'||typeof value==='symbol'){"
        "throw new PDOMException('Uncloneable value','DataCloneError');}"
        "if(value instanceof g.File){return new g.File([value.__text],value.name,{type:value.type,lastModified:value.lastModified});}"
        "if(value instanceof g.Blob){return new g.Blob([value.__text],{type:value.type});}"
        "if(value instanceof Array){if(value.length>256){throw new PDOMException('Clone size','DataCloneError');}"
        "out=[];for(i=0;i<value.length;i++){out.push(pclone(value[i],depth+1));}return out;}"
        "out={};i=0;for(k in value){if(value.hasOwnProperty(k)){if(i++>=256){throw new PDOMException('Clone size','DataCloneError');}"
        "out[k]=pclone(value[k],depth+1);}}return out;}"
        "g.structuredClone=function(value){return pclone(value,0);};"
        "})(this);";

    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART5[] =
        "(function(g){"
        "var PEvent=g.__pcorePEvent;var PEventTarget=g.__pcoreEventTarget;"
        "function PStorageEvent(type,init){var o=init||{};PEvent.call(this,type,o);"
        "this.key=o.key===undefined?null:String(o.key);this.oldValue=o.oldValue===undefined?null:o.oldValue;"
        "this.newValue=o.newValue===undefined?null:o.newValue;this.url=String(o.url||'');"
        "this.storageArea=o.storageArea||null;}PStorageEvent.prototype=Object.create(PEvent.prototype);"
        "PStorageEvent.prototype.constructor=PStorageEvent;"
        "function PHashChangeEvent(type,init){var o=init||{};PEvent.call(this,type,o);"
        "this.oldURL=String(o.oldURL||'');this.newURL=String(o.newURL||'');}"
        "PHashChangeEvent.prototype=Object.create(PEvent.prototype);PHashChangeEvent.prototype.constructor=PHashChangeEvent;"
        "function PPopStateEvent(type,init){var o=init||{};PEvent.call(this,type,o);"
        "this.state=o.state===undefined?null:o.state;}PPopStateEvent.prototype=Object.create(PEvent.prototype);"
        "PPopStateEvent.prototype.constructor=PPopStateEvent;"
        "function PErrorEvent(type,init){var o=init||{};PEvent.call(this,type,o);"
        "this.message=String(o.message||'');this.filename=String(o.filename||'');"
        "this.lineno=Number(o.lineno||0);this.colno=Number(o.colno||0);this.error=o.error||null;}"
        "PErrorEvent.prototype=Object.create(PEvent.prototype);PErrorEvent.prototype.constructor=PErrorEvent;"
        "function PProgressEvent(type,init){var o=init||{};PEvent.call(this,type,o);"
        "this.lengthComputable=!!o.lengthComputable;this.loaded=Number(o.loaded||0);"
        "this.total=Number(o.total||0);}PProgressEvent.prototype=Object.create(PEvent.prototype);"
        "PProgressEvent.prototype.constructor=PProgressEvent;"
        "function PCloseEvent(type,init){var o=init||{};PEvent.call(this,type,o);"
        "this.wasClean=!!o.wasClean;this.code=Number(o.code||0);this.reason=String(o.reason||'');}"
        "PCloseEvent.prototype=Object.create(PEvent.prototype);PCloseEvent.prototype.constructor=PCloseEvent;"
        "Object.defineProperty(g,'StorageEvent',{value:PStorageEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'HashChangeEvent',{value:PHashChangeEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'PopStateEvent',{value:PPopStateEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'ErrorEvent',{value:PErrorEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'ProgressEvent',{value:PProgressEvent,writable:false,configurable:false});"
        "Object.defineProperty(g,'CloseEvent',{value:PCloseEvent,writable:false,configurable:false});"
        "function PPerformanceEntryList(entries){this.__entries=(entries||[]).slice(0);}"
        "PPerformanceEntryList.prototype.getEntries=function(){return this.__entries.slice(0);};"
        "PPerformanceEntryList.prototype.getEntriesByName=function(name,type){var out=[];var i;"
        "for(i=0;i<this.__entries.length;i++){if(this.__entries[i].name===String(name)&&"
        "(!type||this.__entries[i].entryType===String(type))){out.push(this.__entries[i]);}}return out;};"
        "PPerformanceEntryList.prototype.getEntriesByType=function(type){var out=[];var i;"
        "for(i=0;i<this.__entries.length;i++){if(this.__entries[i].entryType===String(type)){out.push(this.__entries[i]);}}return out;};"
        "function pperformanceAccept(entry,options){var i;var types=options&&options.entryTypes;"
        "if(types&&types.length){for(i=0;i<types.length;i++){if(String(types[i])===entry.entryType){return true;}}return false;}"
        "if(options&&options.type&&String(options.type)!==entry.entryType){return false;}"
        "if(options&&options.name&&String(options.name)!==entry.name){return false;}return true;}"
        "function PPerformanceObserver(callback){this.callback=typeof callback==='function'?callback:null;"
        "this.__records=[];this.__options=null;}"
        "PPerformanceObserver.prototype.observe=function(options){var o=options||{};var a=[];var all=g.performance.getEntries();"
        "var i;this.__options=o;for(i=0;i<all.length;i++){if(pperformanceAccept(all[i],o)){a.push(all[i]);}}"
        "this.__records=a;if(this.callback&&a.length){this.callback(new PPerformanceEntryList(a),this);}};"
        "PPerformanceObserver.prototype.disconnect=function(){this.__records=[];this.__options=null;};"
        "PPerformanceObserver.prototype.takeRecords=function(){var a=this.__records.slice(0);this.__records=[];return a;};"
        "Object.defineProperty(g,'PerformanceObserver',{value:PPerformanceObserver,writable:false,configurable:false});"
        "Object.defineProperty(g,'PerformanceObserverEntryList',{value:PPerformanceEntryList,writable:false,configurable:false});"
        "var pbcRegistry={};"
        "function PBroadcastChannel(name){PEventTarget.call(this);this.name=String(name);this.__closed=false;"
        "this.__onmessage=null;this.__onmessageerror=null;if(!pbcRegistry[this.name]){pbcRegistry[this.name]=[];}pbcRegistry[this.name].push(this);}"
        "PBroadcastChannel.prototype=Object.create(PEventTarget.prototype);PBroadcastChannel.prototype.constructor=PBroadcastChannel;"
        "PBroadcastChannel.prototype.postMessage=function(data){var list=pbcRegistry[this.name]||[];var copy=data;var i;"
        "if(this.__closed){return;}try{if(typeof g.structuredClone==='function'){copy=g.structuredClone(data);}}"
        "catch(broadcastCloneError){for(i=0;i<list.length;i++){if(list[i]!==this&&!list[i].__closed&&"
        "g.__pcoreExtraMessages&&g.__pcoreExtraMessages.length<64){g.__pcoreExtraMessages.push({"
        "channel:list[i],data:null,error:true,ports:[]});}}return;}"
        "for(i=0;i<list.length;i++){if(list[i]!==this&&!list[i].__closed&&g.__pcoreExtraMessages&&g.__pcoreExtraMessages.length<64){"
        "g.__pcoreExtraMessages.push({channel:list[i],data:copy,ports:[]});}}};"
        "PBroadcastChannel.prototype.close=function(){var list=pbcRegistry[this.name]||[];var i;"
        "if(this.__closed){return;}this.__closed=true;for(i=list.length-1;i>=0;i--){if(list[i]===this){list.splice(i,1);}}};"
        "Object.defineProperty(PBroadcastChannel.prototype,'onmessage',{get:function(){return this.__onmessage;},set:function(fn){"
        "if(this.__onmessage){this.removeEventListener('message',this.__onmessage,false);}this.__onmessage=typeof fn==='function'?fn:null;"
        "if(this.__onmessage){this.addEventListener('message',this.__onmessage,false);}}});"
        "Object.defineProperty(PBroadcastChannel.prototype,'onmessageerror',{get:function(){return this.__onmessageerror;},set:function(fn){"
        "if(this.__onmessageerror){this.removeEventListener('messageerror',this.__onmessageerror,false);}"
        "this.__onmessageerror=typeof fn==='function'?fn:null;if(this.__onmessageerror){"
        "this.addEventListener('messageerror',this.__onmessageerror,false);}}});"
        "Object.defineProperty(g,'BroadcastChannel',{value:PBroadcastChannel,writable:false,configurable:false});"
        "})(this);";

    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART6[] =
        "(function(g){"
        "var S=g.Symbol;var doc=g.document;"
        "function ptag(proto,name){if(proto&&S&&S.toStringTag){Object.defineProperty(proto,S.toStringTag,{"
        "value:name,writable:false,configurable:false});}}"
        "ptag(g.Blob&&g.Blob.prototype,'Blob');ptag(g.File&&g.File.prototype,'File');"
        "ptag(g.FormData&&g.FormData.prototype,'FormData');ptag(g.Headers&&g.Headers.prototype,'Headers');"
        "ptag(g.Request&&g.Request.prototype,'Request');ptag(g.Response&&g.Response.prototype,'Response');"
        "ptag(g.URLSearchParams&&g.URLSearchParams.prototype,'URLSearchParams');"
        "function pfdIterator(owner,kind){var a=owner.__pairs.slice(0);var i=0;var it={length:a.length,next:function(){"
        "var p;if(i>=a.length){return {value:undefined,done:true};}p=a[i++];"
        "return {value:kind==='keys'?p[0]:(kind==='values'?p[1]:[p[0],p[1]]),done:false};}};"
        "if(S&&S.iterator){Object.defineProperty(it,S.iterator,{value:function(){return it;}});}return it;}"
        "if(g.FormData){g.FormData.prototype.entries=function(){return pfdIterator(this,'entries');};"
        "g.FormData.prototype.keys=function(){return pfdIterator(this,'keys');};"
        "g.FormData.prototype.values=function(){return pfdIterator(this,'values');};"
        "if(S&&S.iterator&&typeof g.FormData.prototype[S.iterator]!=='function'){Object.defineProperty(g.FormData.prototype,S.iterator,{value:function(){return this.entries();}});}"
        "g.FormData.prototype.forEach=function(fn,thisArg){var a=this.__pairs.slice(0);var i;"
        "if(typeof fn!=='function'){return;}for(i=0;i<a.length;i++){fn.call(thisArg,a[i][1],a[i][0],this);}};}"
        "function pbodyText6(body){var i;var s='';if(body===null||body===undefined){return '';}"
        "if(g.Blob&&body instanceof g.Blob){return body.__text;}if(typeof body==='string'){return body;}"
        "if(body&&typeof body.length==='number'){for(i=0;i<body.length&&s.length<16384;i++){"
        "s+=String.fromCharCode(Number(body[i])&255);}return s;}return String(body);}"
        "if(g.Request){g.Request.prototype.text=function(){if(this.bodyUsed){throw new g.DOMException('Body used','TypeError');}"
        "this.bodyUsed=true;return pbodyText6(this.body);};g.Request.prototype.json=function(){return JSON.parse(this.text());};"
        "g.Request.prototype.arrayBuffer=function(){return new g.TextEncoder().encode(this.text());};}"
        "if(g.Response){g.Response.prototype.text=function(){if(this.bodyUsed){throw new g.DOMException('Body used','TypeError');}"
        "this.bodyUsed=true;return pbodyText6(this.body);};g.Response.prototype.json=function(){return JSON.parse(this.text());};"
        "g.Response.prototype.arrayBuffer=function(){return new g.TextEncoder().encode(this.text());};}"
        "var U=g.URL;var up=U&&U.prototype;var oldUrlSync=up&&up.__sync;"
        "function pdecode6(v){try{return decodeURIComponent(String(v));}catch(e){return String(v);}}"
        "function purlAuth6(obj){var m=/^[A-Za-z][A-Za-z0-9+.-]*:\\/\\/([^\\/?#]*)/.exec(obj.__href);"
        "var a=m?m[1]:'';var at=a.lastIndexOf('@');var info='';var host=a;var c;var user='';var pass='';"
        "if(at>=0){info=a.substring(0,at);host=a.substring(at+1);c=info.indexOf(':');"
        "if(c>=0){user=pdecode6(info.substring(0,c));pass=pdecode6(info.substring(c+1));}else{user=pdecode6(info);}}"
        "return {host:host,user:user,pass:pass};}"
        "function purlSync6(){var a;var host;var hostname;var port='';var colon;oldUrlSync.call(this);"
        "a=purlAuth6(this);host=a.host;hostname=host;if(host.charAt(0)==='['){colon=host.indexOf(']');"
        "if(colon>=0&&host.charAt(colon+1)===':'){hostname=host.substring(0,colon+1);port=host.substring(colon+2);}}"
        "else{colon=host.lastIndexOf(':');if(colon>=0&&host.indexOf(':')===colon){hostname=host.substring(0,colon);"
        "port=host.substring(colon+1);}}if((this.__parts.protocol==='http:'&&port==='80')||"
        "(this.__parts.protocol==='https:'&&port==='443')){port='';}this.__parts.hostname=hostname;"
        "this.__parts.port=port;this.__parts.host=hostname+(port!==''?':'+port:'');"
        "this.__parts.origin=(this.__parts.protocol==='http:'||this.__parts.protocol==='https:')&&"
        "this.__parts.host!==''?this.__parts.protocol+'//'+this.__parts.host:'null';"
        "this.__username=a.user;this.__password=a.pass;}"
        "function purlAuthority6(obj){var s='';if(obj.__username!==''||obj.__password!==''){"
        "s=encodeURIComponent(obj.__username);if(obj.__password!==''){s+=':'+encodeURIComponent(obj.__password);}s+='@';}"
        "return s+obj.__parts.host;}"
        "function purlSerialize6(obj){return obj.__parts.protocol+'//'+purlAuthority6(obj)+obj.__parts.pathname+"
        "obj.__parts.search+obj.__parts.hash;}"
        "if(up&&oldUrlSync){up.__sync=purlSync6;up.__setSearch=function(value){var s=String(value);"
        "if(s!==''&&s.charAt(0)!=='?'){s='?'+s;}this.__parts.search=s;this.__href=purlSerialize6(this);};"
        "Object.defineProperty(up,'username',{get:function(){return this.__username||'';},set:function(v){"
        "this.__username=String(v);this.__href=purlSerialize6(this);}});Object.defineProperty(up,'password',{"
        "get:function(){return this.__password||'';},set:function(v){this.__password=String(v);"
        "this.__href=purlSerialize6(this);}});}"
        "if(g.URLSearchParams){var usp=g.URLSearchParams.prototype;var oldHas=usp.has;usp.has=function(name,value){"
        "if(arguments.length<2){return oldHas.call(this,name);}var n=String(name);var wanted=String(value);var i;"
        "for(i=0;i<this.__pairs.length;i++){if(this.__pairs[i][0]===n&&this.__pairs[i][1]===wanted){return true;}}return false;};}"
        "var oldGet=doc.getElementById;var elementCache={};doc.getElementById=function(id){var s=String(id);var e=oldGet.call(this,s);"
        "if(e===null){delete elementCache[s];return null;}if(!elementCache[s]){elementCache[s]=e;}return elementCache[s];};"
        "var oldAll=doc.querySelectorAll;doc.querySelectorAll=function(selector){var a=oldAll.call(this,selector);"
        "if(!a.item){a.item=function(index){var n=Number(index);return n===n&&n>=0&&n===Math.floor(n)&&n<a.length?a[n]:null;};}"
        "if(S&&S.iterator&&!a[S.iterator]){Object.defineProperty(a,S.iterator,{value:function(){var i=0;"
        "var it={next:function(){return i<a.length?{value:a[i++],done:false}:{value:undefined,done:true};}};"
        "Object.defineProperty(it,S.iterator,{value:function(){return it;}});return it;}});}"
        "if(S&&S.toStringTag&&!a[S.toStringTag]){Object.defineProperty(a,S.toStringTag,{value:'NodeList',configurable:false});}return a;};"
        "if(g.Event){Object.defineProperty(g.Event,'NONE',{value:0,writable:false,configurable:false});"
        "Object.defineProperty(g.Event,'CAPTURING_PHASE',{value:1,writable:false,configurable:false});"
        "Object.defineProperty(g.Event,'AT_TARGET',{value:2,writable:false,configurable:false});"
        "Object.defineProperty(g.Event,'BUBBLING_PHASE',{value:3,writable:false,configurable:false});}"
        "if(g.EventTarget){var etDispatch=g.EventTarget.prototype.dispatchEvent;g.EventTarget.prototype.dispatchEvent=function(e){"
        "var ok=etDispatch.call(this,e);e.currentTarget=null;e.eventPhase=0;return ok;};}"
        "if(g.MessagePort){Object.defineProperty(g.MessagePort.prototype,'closed',{get:function(){return !!this.__closed;}});"
        "Object.defineProperty(g.MessagePort.prototype,'started',{get:function(){return !!this.__started;}});ptag(g.MessagePort.prototype,'MessagePort');}"
        "if(g.BroadcastChannel){Object.defineProperty(g.BroadcastChannel.prototype,'closed',{get:function(){return !!this.__closed;}});"
        "ptag(g.BroadcastChannel.prototype,'BroadcastChannel');}"
        "if(g.PerformanceObserverEntryList){var el=g.PerformanceObserverEntryList.prototype;"
        "Object.defineProperty(el,'length',{get:function(){return this.__entries.length;}});el.item=function(index){"
        "var n=Number(index);return n===n&&n>=0&&n===Math.floor(n)&&n<this.__entries.length?this.__entries[n]:null;};"
        "el.toJSON=function(){return this.getEntries();};if(S&&S.iterator){Object.defineProperty(el,S.iterator,{value:function(){"
        "var a=this.__entries.slice(0);var i=0;var it={next:function(){return i<a.length?{value:a[i++],done:false}:{value:undefined,done:true};}};"
        "Object.defineProperty(it,S.iterator,{value:function(){return it;}});return it;}});}}"
        "if(g.performance){g.performance.clearResourceTimings=function(){};g.performance.toJSON=function(){"
        "return {timeOrigin:this.timeOrigin};};}"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART7[] =
        "(function(g){"
        "var S=g.Symbol;var doc=g.document;"
        "function ptag7(proto,name){if(!proto||!S||!S.toStringTag){return;}"
        "try{if(!proto[S.toStringTag]){Object.defineProperty(proto,S.toStringTag,{"
        "value:name,writable:false,configurable:false});}}catch(tagError){}}"
        "function ptagInstance7(object,name){if(!object||!S||!S.toStringTag){return;}"
        "try{if(!object[S.toStringTag]){Object.defineProperty(object,S.toStringTag,{"
        "value:name,writable:false,configurable:false});}}catch(instanceTagError){}}"
        "function pheaderIterator7(owner,kind){var a=[];var i=0;var j;for(j=0;j<owner.__pairs.length;j++){"
        "a.push([owner.__pairs[j][0],owner.__pairs[j][1]]);}var it={length:a.length,next:function(){var p;if(i>=a.length){return {"
        "value:undefined,done:true};}p=a[i++];return {value:kind==='keys'?p[0]:"
        "(kind==='values'?p:[p[0],p[1]]),done:false};}};"
        "if(S&&S.iterator){Object.defineProperty(it,S.iterator,{value:function(){return it;}});}"
        "return it;}"
        "if(g.Headers){var hp=g.Headers.prototype;hp.entries=function(){return pheaderIterator7(this,'entries');};"
        "hp.keys=function(){return pheaderIterator7(this,'keys');};hp.values=function(){return pheaderIterator7(this,'values');};"
        "hp.forEach=function(fn,thisArg){var a=[];var i;var j;if(typeof fn!=='function'){return;}"
        "for(j=0;j<this.__pairs.length;j++){a.push([this.__pairs[j][0],this.__pairs[j][1]]);}"
        "for(i=0;i<a.length;i++){fn.call(thisArg,a[i][1],a[i][0],this);}};}"
        "function pcopyBody7(body){var i;var a;if(g.File&&body instanceof g.File){return new g.File([body.__text],"
        "body.name,{type:body.type,lastModified:body.lastModified});}if(g.Blob&&body instanceof g.Blob){"
        "return new g.Blob([body.__text],{type:body.type});}if(body&&typeof body.length==='number'&&"
        "typeof body!=='string'){a=[];for(i=0;i<body.length&&i<16384;i++){a.push(Number(body[i])&255);}return a;}"
        "return body;}"
        "function pheadersJSON7(headers){return headers&&typeof headers.toJSON==='function'?headers.toJSON():headers;}"
        "if(g.Request){var rp=g.Request.prototype;rp.clone=function(){if(this.bodyUsed){throw new g.DOMException('Body used','TypeError');}"
        "return new g.Request(this.url,{method:this.method,headers:pheadersJSON7(this.headers),destination:this.destination,"
        "referrer:this.referrer,referrerPolicy:this.referrerPolicy,mode:this.mode,credentials:this.credentials,cache:this.cache,"
        "redirect:this.redirect,integrity:this.integrity,keepalive:this.keepalive,signal:this.signal,body:pcopyBody7(this.body)});};"
        "rp.toJSON=function(){return {url:this.url,method:this.method,headers:pheadersJSON7(this.headers),"
        "mode:this.mode,credentials:this.credentials,cache:this.cache,redirect:this.redirect,bodyUsed:!!this.bodyUsed};};}"
        "if(g.Response){var sp=g.Response.prototype;sp.clone=function(){if(this.bodyUsed){throw new g.DOMException('Body used','TypeError');}"
        "return new g.Response(pcopyBody7(this.body),{status:this.status,statusText:this.statusText,"
        "headers:pheadersJSON7(this.headers),url:this.url,type:this.type,redirected:this.redirected});};"
        "sp.toJSON=function(){return {status:this.status,statusText:this.statusText,headers:pheadersJSON7(this.headers),"
        "url:this.url,type:this.type,redirected:!!this.redirected,ok:!!this.ok,bodyUsed:!!this.bodyUsed};};}"
        "if(g.URLSearchParams){var up=g.URLSearchParams.prototype;up.entries=function(){return pheaderIterator7(this,'entries');};"
        "up.keys=function(){return pheaderIterator7(this,'keys');};up.values=function(){return pheaderIterator7(this,'values');};"
        "up.forEach=function(fn,thisArg){var a=[];var i;var j;if(typeof fn!=='function'){return;}"
        "for(j=0;j<this.__pairs.length;j++){a.push([this.__pairs[j][0],this.__pairs[j][1]]);}"
        "for(i=0;i<a.length;i++){fn.call(thisArg,a[i][1],a[i][0],this);}};}"
        "if(g.FormData){var fp=g.FormData.prototype;var oldAppend7=fp.append;var oldSet7=fp.set;"
        "fp.append=function(name,value,filename){if(g.Blob&&value instanceof g.Blob&&!(g.File&&value instanceof g.File)&&"
        "arguments.length<3){return oldAppend7.call(this,name,value,'blob');}return oldAppend7.apply(this,arguments);};"
        "fp.set=function(name,value,filename){if(g.Blob&&value instanceof g.Blob&&!(g.File&&value instanceof g.File)&&"
        "arguments.length<3){return oldSet7.call(this,name,value,'blob');}return oldSet7.apply(this,arguments);};}"
        "try{ptag7(Object.getPrototypeOf(g.sessionStorage),'Storage');ptag7(Object.getPrototypeOf(g.localStorage),'Storage');"
        "ptagInstance7(g.sessionStorage,'Storage');ptagInstance7(g.localStorage,'Storage');}"
        "catch(storageTagError){}"
        "var sample=doc&&doc.getElementById?doc.getElementById('target'):null;var clp;var stp;var dsp;"
        "if(sample){clp=Object.getPrototypeOf(sample.classList);stp=Object.getPrototypeOf(sample.style);dsp=Object.getPrototypeOf(sample.dataset);"
        "ptag7(clp,'DOMTokenList');ptag7(stp,'CSSStyleDeclaration');ptag7(dsp,'DOMStringMap');"
        "if(clp){var oldAdd7=clp.add;var oldRemove7=clp.remove;var oldToggle7=clp.toggle;var oldReplace7=clp.replace;"
        "function validToken7(value){var s=String(value);if(s===''||/\\s/.test(s)){throw new g.DOMException('Invalid token','SyntaxError');}return s;}"
        "clp.add=function(){var i;for(i=0;i<arguments.length;i++){validToken7(arguments[i]);}return oldAdd7.apply(this,arguments);};"
        "clp.remove=function(){var i;for(i=0;i<arguments.length;i++){validToken7(arguments[i]);}return oldRemove7.apply(this,arguments);};"
        "clp.toggle=function(token,force){validToken7(token);return oldToggle7.call(this,token,force);};"
        "clp.replace=function(oldToken,newToken){validToken7(oldToken);validToken7(newToken);return oldReplace7.call(this,oldToken,newToken);};}}"
        "function pentryJSON7(entry){return {name:entry.name,entryType:entry.entryType,startTime:entry.startTime,duration:entry.duration};}"
        "function decorateEntry7(entry){if(entry&&!entry.toJSON){Object.defineProperty(entry,'toJSON',{value:function(){return pentryJSON7(this);},"
        "writable:false,configurable:false});}return entry;}"
        "if(g.performance){var oldMark7=g.performance.mark;var oldMeasure7=g.performance.measure;"
        "g.performance.mark=function(name){return decorateEntry7(oldMark7.call(this,name));};"
        "g.performance.measure=function(name,start,end){return decorateEntry7(oldMeasure7.call(this,name,start,end));};}"
        "if(g.PerformanceObserver){if(!g.PerformanceObserver.supportedEntryTypes){Object.defineProperty(g.PerformanceObserver,"
        "'supportedEntryTypes',{value:['mark','measure'],writable:false,configurable:false});}var op=g.PerformanceObserver.prototype;"
        "var oldObserve7=op.observe;op.observe=function(options){var o=options||{};if(o.entryTypes!==undefined&&o.type!==undefined){"
        "throw new g.DOMException('Conflicting observe options','TypeError');}if(o.entryTypes!==undefined&&"
        "(!o.entryTypes||!o.entryTypes.length)){throw new g.DOMException('Empty entryTypes','TypeError');}"
        "return oldObserve7.call(this,o);};}"
        "ptag7(g.AbortSignal&&g.AbortSignal.prototype,'AbortSignal');"
        "ptag7(g.AbortController&&g.AbortController.prototype,'AbortController');"
        "if(g.Blob){g.Blob.prototype.toJSON=function(){return {size:this.size,type:this.type};};}"
        "if(g.File){g.File.prototype.toJSON=function(){return {name:this.name,lastModified:this.lastModified,size:this.size,type:this.type};};}"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART8[] =
        "(function(g){"
        "if(typeof g.Promise==='function'){return;}"
        "var MAX=64;var S=g.Symbol;"
        "function pLength8(input){var n;if(!input||typeof input.length!=='number'){return -1;}"
        "n=Number(input.length);if(n!==n||n<0||n!==Math.floor(n)){return -1;}return n;}"
        "function pPending8(){var p=Object.create(PPromise8.prototype);p.__state=0;p.__value=undefined;"
        "p.__handlers=[];p.__flushing=false;return p;}"
        "function pEnqueue8(fn){if(g.queueMicrotask){g.queueMicrotask(fn);}else if(g.setImmediate){g.setImmediate(fn);}"
        "else if(g.setTimeout){g.setTimeout(fn,0);}}"
        "function pReject8(p,reason){if(p.__state!==0){return;}p.__state=2;p.__value=reason;pFlush8(p);}"
        "function pResolve8(p,value){var then;var called;"
        "if(p.__state!==0){return;}if(value===p){pReject8(p,new TypeError('Promise self resolution'));return;}"
        "if(value&&(typeof value==='object'||typeof value==='function')){try{then=value.then;}catch(resolveError){"
        "pReject8(p,resolveError);return;}if(typeof then==='function'){called=false;try{then.call(value,function(next){"
        "if(called){return;}called=true;pResolve8(p,next);},function(reason){if(called){return;}called=true;"
        "pReject8(p,reason);});}catch(thenError){if(!called){called=true;pReject8(p,thenError);}}return;}}"
        "p.__state=1;p.__value=value;pFlush8(p);}"
        "function pFlush8(p){var a;var i;if(p.__state===0||p.__flushing){return;}p.__flushing=true;"
        "a=p.__handlers;p.__handlers=[];for(i=0;i<a.length;i++){(function(handler){pEnqueue8(function(){"
        "pRun8(p,handler);});})(a[i]);}p.__flushing=false;}"
        "function pRun8(p,handler){var cb=p.__state===1?handler.onFulfilled:handler.onRejected;"
        "if(typeof cb!=='function'){if(p.__state===1){pResolve8(handler.next,p.__value);}else{pReject8(handler.next,p.__value);}return;}"
        "try{pResolve8(handler.next,cb(p.__value));}catch(handlerError){pReject8(handler.next,handlerError);}}"
        "function pAggregate8(errors){var e=new Error('All promises were rejected');e.name='AggregateError';e.errors=errors;return e;}"
        "function PPromise8(executor){var self=this;var done=false;"
        "if(!(self instanceof PPromise8)){throw new TypeError('Promise constructor requires new');}"
        "if(typeof executor!=='function'){throw new TypeError('Promise resolver');}"
        "self.__state=0;self.__value=undefined;self.__handlers=[];self.__flushing=false;"
        "function resolve(value){if(done){return;}done=true;pResolve8(self,value);}"
        "function reject(reason){if(done){return;}done=true;pReject8(self,reason);}"
        "try{executor(resolve,reject);}catch(executorError){reject(executorError);}}"
        "PPromise8.prototype.then=function(onFulfilled,onRejected){var next=pPending8();"
        "if(this.__handlers.length>=MAX){pReject8(next,new Error('Promise handler limit'));return next;}"
        "this.__handlers.push({next:next,onFulfilled:onFulfilled,onRejected:onRejected});if(this.__state!==0){pFlush8(this);}return next;};"
        "PPromise8.prototype.catch=function(onRejected){return this.then(null,onRejected);};"
        "PPromise8.prototype['finally']=function(onFinally){var C=PPromise8;return this.then(function(value){"
        "return C.resolve(typeof onFinally==='function'?onFinally():undefined).then(function(){return value;});},"
        "function(reason){return C.resolve(typeof onFinally==='function'?onFinally():undefined).then(function(){"
        "throw reason;});});};"
        "PPromise8.resolve=function(value){if(value instanceof PPromise8){return value;}return new PPromise8(function(resolve){resolve(value);});};"
        "PPromise8.reject=function(reason){return new PPromise8(function(resolve,reject){reject(reason);});};"
        "PPromise8.all=function(input){return new PPromise8(function(resolve,reject){var len=pLength8(input);"
        "var values=[];var remaining;var i;if(len<0||len>MAX){reject(new TypeError('Promise input limit'));return;}"
        "if(len===0){resolve(values);return;}remaining=len;for(i=0;i<len;i++){(function(index){PPromise8.resolve(input[index]).then(function(value){"
        "values[index]=value;remaining--;if(remaining===0){resolve(values);}},reject);})(i);}});};"
        "PPromise8.race=function(input){return new PPromise8(function(resolve,reject){var len=pLength8(input);var i;"
        "if(len<0||len>MAX){reject(new TypeError('Promise input limit'));return;}for(i=0;i<len;i++){PPromise8.resolve(input[i]).then(resolve,reject);}});};"
        "PPromise8.allSettled=function(input){return new PPromise8(function(resolve,reject){var len=pLength8(input);"
        "var values=[];var remaining;var i;if(len<0||len>MAX){reject(new TypeError('Promise input limit'));return;}"
        "if(len===0){resolve(values);return;}remaining=len;for(i=0;i<len;i++){(function(index){PPromise8.resolve(input[index]).then(function(value){"
        "values[index]={status:'fulfilled',value:value};remaining--;if(remaining===0){resolve(values);}},function(reason){"
        "values[index]={status:'rejected',reason:reason};remaining--;if(remaining===0){resolve(values);}});})(i);}});};"
        "PPromise8.any=function(input){return new PPromise8(function(resolve,reject){var len=pLength8(input);"
        "var errors=[];var remaining;var i;if(len<0||len>MAX){reject(new TypeError('Promise input limit'));return;}"
        "if(len===0){reject(pAggregate8(errors));return;}remaining=len;for(i=0;i<len;i++){(function(index){PPromise8.resolve(input[index]).then(resolve,function(reason){"
        "errors[index]=reason;remaining--;if(remaining===0){reject(pAggregate8(errors));}});})(i);}});};"
        "if(S&&S.toStringTag){Object.defineProperty(PPromise8.prototype,S.toStringTag,{value:'Promise',writable:false,configurable:false});}"
        "g.Promise=PPromise8;"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART9[] =
        "(function(g){"
        "var PElement=g.__pcorePElement;var doc=g.document;var S=g.Symbol;"
        "var cache={};"
        "function wrap9(id){var s;if(typeof id!=='string'||id===''){return null;}"
        "s=String(id);if(s==='__positron_document_element__'){return doc.documentElement;}"
        "if(s==='__positron_document_head__'){return doc.head;}"
        "if(s==='__positron_document_body__'){return doc.body;}"
        "if(!cache[s]){cache[s]=new PElement(s);}return cache[s];}"
        "function relation9(owner,kind,index){var value;"
        "if(!owner||typeof g.__pcoreGetNodeRelation!=='function'){return null;}"
        "try{value=g.__pcoreGetNodeRelation({id:owner.__id,relation:kind,index:"
        "index===undefined?0:index});}catch(relationError){return null;}return value;}"
        "function list9(a,named){var i;Object.defineProperty(a,'item',{value:function(index){"
        "var n=Number(index);return n===n&&n>=0&&n===Math.floor(n)&&n<a.length?a[n]:null;},"
        "writable:false,configurable:false,enumerable:false});if(named){Object.defineProperty(a,'namedItem',{value:function(name){var s=String(name);"
        "var j;for(j=0;j<this.length;j++){if(this[j]&&(this[j].id===s||this[j].name===s)){"
        "return this[j];}}return null;},writable:false,configurable:false,enumerable:false});}"
        "if(S&&S.iterator&&a[S.iterator]===undefined&&typeof Array.prototype[S.iterator]==='function'){Object.defineProperty(a,S.iterator,{"
        "value:Array.prototype[S.iterator],writable:false,configurable:false});}"
        "if(typeof g.__pcoreDecorateCollection13==='function'){return g.__pcoreDecorateCollection13(a,"
        "named?'HTMLCollection':'NodeList',!!named);}return a;}"
        "function children9(owner){var a;var n;var i;var id;"
        "if(owner.__children9){return owner.__children9;}a=[];n=relation9(owner,"
        "6,0);n=Number(n);if(!(n>=0&&n===Math.floor(n))){n=0;}"
        "for(i=0;i<n;i++){id=relation9(owner,2,i);if(typeof id==='string'&&id!==''){"
        "a.push(wrap9(id));}}owner.__children9=list9(a,true);return owner.__children9;}"
        "function parent9(owner){var id=relation9(owner,1,0);return typeof id==='string'?wrap9(id):null;}"
        "function sibling9(owner,kind){var id=relation9(owner,kind,0);return typeof id==='string'?wrap9(id):null;}"
        "Object.defineProperty(PElement.prototype,'parentElement',{get:function(){return parent9(this);},enumerable:true,configurable:true});"
        "Object.defineProperty(PElement.prototype,'parentNode',{get:function(){return parent9(this);},enumerable:true,configurable:true});"
        "Object.defineProperty(PElement.prototype,'firstChild',{get:function(){return sibling9(this,2);},enumerable:true,configurable:true});"
        "Object.defineProperty(PElement.prototype,'lastChild',{get:function(){var a=children9(this);return a.length?a[a.length-1]:null;},enumerable:true,configurable:true});"
        "Object.defineProperty(PElement.prototype,'previousSibling',{get:function(){return sibling9(this,4);},enumerable:true,configurable:true});"
        "Object.defineProperty(PElement.prototype,'nextSibling',{get:function(){return sibling9(this,5);},enumerable:true,configurable:true});"
        "Object.defineProperty(PElement.prototype,'children',{get:function(){return children9(this);},enumerable:true});"
        "Object.defineProperty(PElement.prototype,'childElementCount',{get:function(){return children9(this).length;},enumerable:true});"
        "function tag9(owner){var value=relation9(owner,7,0);return typeof value==='string'?value:'';}"
        "Object.defineProperty(PElement.prototype,'tagName',{get:function(){return tag9(this).toUpperCase();},enumerable:true});"
        "Object.defineProperty(PElement.prototype,'nodeName',{get:function(){return tag9(this).toUpperCase();},enumerable:true});"
        "Object.defineProperty(PElement.prototype,'localName',{get:function(){return tag9(this).toLowerCase();},enumerable:true});"
        "PElement.prototype.contains=function(other){var n=other;var i=0;"
        "if(!other||typeof other.__id!=='string'){return false;}while(n&&i<64){"
        "if(n.__id===this.__id){return true;}n=n.parentElement;i++;}return false;};"
        "function path9(owner){var a=[];var n=owner;var i=0;while(n&&i<64){a.push(n);n=n.parentElement;i++;}return a;}"
        "PElement.prototype.compareDocumentPosition=function(other){var a;var b;var i;var j;var k;var ai;var bi;var p;var kids;"
        "if(!other||typeof other.__id!=='string'){return 1|32;}if(other.__id===this.__id){return 0;}"
        "a=path9(this);b=path9(other);if(!a.length||!b.length||a[a.length-1].__id!==b[b.length-1].__id){return 1|32;}"
        "for(i=0;i<b.length;i++){if(b[i].__id===this.__id){return 4|16;}}"
        "for(i=0;i<a.length;i++){if(a[i].__id===other.__id){return 2|8;}}"
        "i=a.length-1;j=b.length-1;while(i>=0&&j>=0&&a[i].__id===b[j].__id){i--;j--;}"
        "if(i<0||j<0){return 1|32;}p=a[i+1];kids=children9(p);ai=-1;bi=-1;"
        "for(k=0;k<kids.length;k++){if(kids[k].__id===a[i].__id){ai=k;}if(kids[k].__id===b[j].__id){bi=k;}}"
        "return ai<bi?4:2;};"
        "function trim9(value){return String(value).replace(/^\\s+|\\s+$/g,'');}"
        "function match9(owner,selector){var s=trim9(selector);var pos=0;var start;var end;var token;var body;var eq;var name;var value;"
        "if(s==='*'){return true;}if(s.indexOf(' ')>=0||s.indexOf('>')>=0||s.indexOf('+')>=0||s.indexOf('~')>=0){return false;}"
        "start=0;while(pos<s.length&&s.charAt(pos)!=='#'&&s.charAt(pos)!=='.'&&s.charAt(pos)!=='['){pos++;}"
        "if(pos>0&&owner.localName!==s.substring(0,pos).toLowerCase()){return false;}"
        "while(pos<s.length){if(s.charAt(pos)==='#'){start=++pos;while(pos<s.length&&s.charAt(pos)!=='#'&&s.charAt(pos)!=='.'&&s.charAt(pos)!=='['){pos++;}"
        "if(owner.id!==s.substring(start,pos)){return false;}}else if(s.charAt(pos)==='.'){start=++pos;while(pos<s.length&&s.charAt(pos)!=='#'&&s.charAt(pos)!=='.'&&s.charAt(pos)!=='['){pos++;}"
        "token=s.substring(start,pos);if(token===''||!owner.classList.contains(token)){return false;}}else if(s.charAt(pos)==='['){"
        "end=s.indexOf(']',pos+1);if(end<0){return false;}body=trim9(s.substring(pos+1,end));eq=body.indexOf('=');"
        "if(eq<0){if(body===''||!owner.hasAttribute(body)){return false;}}else{name=trim9(body.substring(0,eq));value=trim9(body.substring(eq+1));"
        "if((value.charAt(0)==='\\\"'&&value.charAt(value.length-1)==='\\\"')||(value.charAt(0)==='\\''&&value.charAt(value.length-1)==='\\'')){value=value.substring(1,value.length-1);}"
        "if(name===''||owner.getAttribute(name)!==value){return false;}}pos=end+1;}else{return false;}}return true;}"
        "PElement.prototype.matches=function(selector){return match9(this,selector);};"
        "PElement.prototype.closest=function(selector){var n=this;var i=0;while(n&&i<64){if(match9(n,selector)){return n;}n=n.parentElement;i++;}return null;};"
        "function query9(owner,selector,all,named){var out=[];var stack=[];var kids;var i;var n;"
        "kids=children9(owner);for(i=kids.length-1;i>=0;i--){stack.push(kids[i]);}"
        "while(stack.length){n=stack.pop();if(match9(n,selector)){if(!all){return n;}out.push(n);}"
        "kids=children9(n);for(i=kids.length-1;i>=0;i--){stack.push(kids[i]);}}"
        "return all?list9(out,named===true):null;}"
        "function docQuery9(selector,all){var s=trim9(selector);var root=doc.documentElement;var out=[];"
        "var rest;var i;if(root===null){return all?list9(out,false):null;}"
        "if(s===''){return all?list9(out,false):null;}"
        "if(s.toLowerCase()===':root'){if(all){out.push(root);return list9(out,false);}return root;}"
        "if(match9(root,s)){if(!all){return root;}out.push(root);}"
        "if(!all){return query9(root,s,false,false);}"
        "rest=query9(root,s,true,false);if(rest!==null){for(i=0;i<rest.length;i++){out.push(rest[i]);}}"
        "return all?list9(out,false):null;}"
        "doc.querySelector=function(selector){return docQuery9(selector,false);};"
        "doc.querySelectorAll=function(selector){return docQuery9(selector,true);};"
        "function named9(name){var s=String(name);var root=doc.documentElement;var out=[];"
        "var stack=[];var kids;var i;var n;if(root===null){return list9(out,false);}"
        "if(root.hasAttribute('name')&&root.name===s){out.push(root);}kids=children9(root);"
        "for(i=kids.length-1;i>=0;i--){stack.push(kids[i]);}while(stack.length){n=stack.pop();"
        "if(n.hasAttribute('name')&&n.name===s){out.push(n);}kids=children9(n);"
        "for(i=kids.length-1;i>=0;i--){stack.push(kids[i]);}}return list9(out,false);}"
        "doc.getElementsByName=function(name){return named9(name);};"
        "Object.defineProperty(doc,'forms',{get:function(){return doc.getElementsByTagName('form');},enumerable:true});"
        "Object.defineProperty(doc,'images',{get:function(){return doc.getElementsByTagName('img');},enumerable:true});"
        "Object.defineProperty(doc,'scripts',{get:function(){return doc.getElementsByTagName('script');},enumerable:true});"
        "function link9(anchors){var all=doc.getElementsByTagName('*');var out=[];var i;var n;"
        "for(i=0;i<all.length;i++){n=all[i];if(anchors){if(n.localName==='a'&&n.hasAttribute('name')){out.push(n);}}"
        "else if((n.localName==='a'||n.localName==='area')&&n.hasAttribute('href')){out.push(n);}}"
        "return list9(out,true);}"
        "Object.defineProperty(doc,'links',{get:function(){return link9(false);},enumerable:true});"
        "Object.defineProperty(doc,'anchors',{get:function(){return link9(true);},enumerable:true});"
        "function namespaceMatch9(namespace,localName,node){var ns;var ln;"
        "ns=namespace===null?null:String(namespace);ln=String(localName);"
        "if(ln===''||(ln!=='*'&&node.localName!==ln)){return false;}"
        "if(ns==='*'){return true;}return node.namespaceURI===ns;}"
        "function namespaceQuery9(owner,namespace,localName,documentScope){var all;var out=[];var i;"
        "all=documentScope?doc.getElementsByTagName('*'):owner.getElementsByTagName('*');"
        "for(i=0;i<all.length;i++){if(namespaceMatch9(namespace,localName,all[i])){out.push(all[i]);}}"
        "return list9(out,true);}"
        "doc.getElementsByTagNameNS=function(namespace,localName){return namespaceQuery9(doc,namespace,localName,true);};"
        "PElement.prototype.querySelector=function(selector){return query9(this,selector,false);};"
        "PElement.prototype.querySelectorAll=function(selector){return query9(this,selector,true);};"
        "PElement.prototype.getElementsByTagName=function(name){var s=String(name);"
        "return s===''?list9([],true):query9(this,s,true,true);};"
        "PElement.prototype.getElementsByTagNameNS=function(namespace,localName){"
        "return namespaceQuery9(this,namespace,localName,false);};"
        "PElement.prototype.getElementsByClassName=function(names){var s=String(names).replace(/^\\s+|\\s+$/g,'');"
        "var a;if(s===''){return list9([],true);}a=s.split(/\\s+/);var q='.';var i;"
        "for(i=0;i<a.length;i++){if(i>0){q+='.';}q+=a[i];}return query9(this,q,true,true);};"
        "Object.defineProperty(PElement.prototype,'form',{get:function(){var t=this.localName;if(t!=='input'&&t!=='select'&&t!=='textarea'&&t!=='button'){return null;}var id=relation9(this,8,0);return typeof id==='string'?wrap9(id):null;},enumerable:true});"
        "Object.defineProperty(PElement.prototype,'elements',{get:function(){var a=[];var n;var i;var id;"
        "if(this.localName!=='form'){return list9(a,true);}n=Number(relation9(this,9,0));if(!(n>=0&&n===Math.floor(n))){n=0;}"
        "for(i=0;i<n;i++){id=relation9(this,10,i);if(typeof id==='string'&&id!==''){a.push(wrap9(id));}}return list9(a,true);},enumerable:true});"
        "var oldGet9=doc.getElementById;doc.getElementById=function(id){var e=oldGet9.call(this,id);return e?wrap9(String(id)):null;};"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART10[] =
        "(function(g){"
        "var P=g.__pcorePElement;var S=g.Symbol;var C={};"
        "var XML_NS10='http://www.w3.org/XML/1998/namespace';"
        "var XMLNS_NS10='http://www.w3.org/2000/xmlns/';"
        "function r(o,k,i){var v;if(!o||typeof g.__pcoreGetNodeRelation!=='function'){return null;}"
        "try{v=g.__pcoreGetNodeRelation({id:o.__id,relation:k,index:i===undefined?0:i});}"
        "catch(e){return null;}return v;}"
        "function ns(o){var a=[],n=Number(r(o,11,0)),i,v;"
        "if(!(n>=0&&n===Math.floor(n))){n=0;}for(i=0;i<n;i++){v=r(o,12,i);"
        "if(typeof v==='string'){a.push(v);}}return a;}"
        "function nn(o,i){var v=r(o,12,i);return typeof v==='string'?v:null;}"
        "function attrNamespace10(name){var s=String(name),p=s.indexOf(':'),pre;"
        "if(p<0){return null;}pre=s.substring(0,p);"
        "if(pre==='xml'){return XML_NS10;}if(pre==='xmlns'){return XMLNS_NS10;}return false;}"
        "function attrPrefix10(name){var s=String(name),p=s.indexOf(':'),ns;"
        "if(p<0){return null;}ns=attrNamespace10(s);return ns===false?null:s.substring(0,p);}"
        "function attrLocal10(name){var s=String(name),p=s.indexOf(':');"
        "return p<0?s:s.substring(p+1);}"
        "function attrNamespaceArg10(value){var s;"
        "if(value===null||value===undefined){return null;}s=String(value);return s===''?null:s;}"
        "function attrFind10(owner,namespace,localName){var target=attrNamespaceArg10(namespace);"
        "var local=String(localName),a=ns(owner),i,name;if(local===''){return null;}"
        "for(i=0;i<a.length;i++){name=a[i];if(attrNamespace10(name)===target&&attrLocal10(name)===local){return name;}}"
        "return null;}"
        "function at(o,n){var s=String(n),k=o.__id+'|'+s,a=C[k];if(a){return a;}"
        "a={o:o,n:s,nodeType:2,nodeName:s,name:s,specified:true,ownerElement:o};"
        "Object.defineProperty(a,'o',{value:o,writable:false,configurable:false,enumerable:false});"
        "Object.defineProperty(a,'value',{get:function(){var v=o.getAttribute(s);return v===null?'':v;},"
        "set:function(v){o.setAttribute(s,String(v));},enumerable:true});"
        "Object.defineProperty(a,'nodeValue',{get:function(){return a.value;},"
        "set:function(v){a.value=v;},enumerable:true});"
        "Object.defineProperty(a,'ownerDocument',{get:function(){return o.ownerDocument;},"
        "enumerable:true});Object.defineProperty(a,'baseURI',{get:function(){return o.baseURI;},"
        "enumerable:true});Object.defineProperty(a,'namespaceURI',{get:function(){var ns=attrNamespace10(a.name);return ns===false?null:ns;},"
        "enumerable:true});Object.defineProperty(a,'prefix',{get:function(){return attrPrefix10(a.name);},"
        "enumerable:true});Object.defineProperty(a,'localName',{get:function(){return attrLocal10(a.name);},"
        "enumerable:true});"
        "a.isDefaultNamespace=function(v){return o.isDefaultNamespace(v);};"
        "a.lookupNamespaceURI=function(v){return o.lookupNamespaceURI(v);};"
        "a.toString=function(){return a.value;};C[k]=a;return a;}"
        "function ni(o,n){var a=ns(o),s=String(n),i;for(i=0;i<a.length;i++){"
        "if(a[i]===s||String(a[i]).toLowerCase()===s.toLowerCase()){return at(o,a[i]);}}return null;}"
        "function it(o){var a=ns(o),i=0;return {next:function(){return i<a.length?"
        "{done:false,value:at(o,a[i++])}:{done:true,value:undefined};}};}"
        "function m10(o){var m={},i;Object.defineProperty(m,'length',{get:function(){return ns(o).length;},"
        "enumerable:true});m.item=function(i){var n=Number(i),s;if(n!==n||n<0||n!==Math.floor(n)){return null;}"
        "s=nn(o,n);return s===null?null:at(o,s);};m.getNamedItem=function(n){return ni(o,n);};"
        "m.getNamedItemNS=function(namespace,localName){var n=attrFind10(o,namespace,localName);"
        "return n===null?null:at(o,n);};"
        "m.setNamedItem=function(a){var old;if(!a||a.nodeType!==2||a.o!==o){return null;}"
        "old=ni(o,a.name);o.setAttribute(a.name,a.value);return old;};"
        "m.removeNamedItem=function(n){var old=ni(o,n);if(old!==null){o.removeAttribute(old.name);}return old;};"
        "m.toString=function(){return '[object NamedNodeMap]';};"
        "for(i=0;i<8;i++){(function(j){Object.defineProperty(m,String(j),{get:function(){return m.item(j);},"
        "enumerable:true});})(i);}if(S&&S.iterator){Object.defineProperty(m,S.iterator,{value:function(){return it(o);},"
        "writable:false,configurable:false});}return m;}"
        "Object.defineProperty(P.prototype,'attributes',{get:function(){return this.__a10||(this.__a10=m10(this));},"
        "enumerable:true});P.prototype.getAttributeNames=function(){return ns(this);};"
        "P.prototype.hasAttributes=function(){return this.attributes.length>0;};"
        "P.prototype.getAttributeNode=function(n){return this.attributes.getNamedItem(n);};"
        "P.prototype.getAttributeNS=function(namespace,localName){var n=attrFind10(this,namespace,localName);"
        "return n===null?null:this.getAttribute(n);};"
        "P.prototype.hasAttributeNS=function(namespace,localName){"
        "return attrFind10(this,namespace,localName)!==null;};"
        "P.prototype.getAttributeNodeNS=function(namespace,localName){var n=attrFind10(this,namespace,localName);"
        "return n===null?null:at(this,n);};"
        "P.prototype.setAttributeNode=function(a){if(!a||a.nodeType!==2||a.o!==this){return null;}"
        "return this.attributes.setNamedItem(a);};P.prototype.removeAttributeNode=function(a){var old;"
        "if(!a||a.nodeType!==2||a.o!==this){return null;}old=this.getAttributeNode(a.name);"
        "if(old===null){return null;}this.removeAttribute(a.name);return old;};"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART11[] =
        "(function(g){"
        "var P=g.__pcorePElement;var S=g.Symbol;var doc=g.document;var cache={};"
        "function r(o,k,i){var v;if(!o||typeof g.__pcoreGetNodeRelation!=='function'){return null;}"
        "try{v=g.__pcoreGetNodeRelation({id:o.__id,relation:k,index:i===undefined?0:i});}"
        "catch(e){return null;}return v;}"
        "function num(o,k,i){var v=r(o,k,i),n=Number(v);return n===n&&n>=0&&n===Math.floor(n)?n:0;}"
        "function wrap(id){if(typeof id!=='string'||id===''){return null;}return doc.getElementById(id);}"
        "function list(a){var i;Object.defineProperty(a,'item',{value:function(index){"
        "var n=Number(index);return n===n&&n>=0&&n===Math.floor(n)&&n<a.length?a[n]:null;},"
        "writable:false,configurable:false,enumerable:false});"
        "if(S&&S.iterator&&a[S.iterator]===undefined){Object.defineProperty(a,S.iterator,{"
        "value:function(){var j=0;var it={next:function(){return j<a.length?"
        "{done:false,value:a[j++]}:{done:true,value:undefined};}};if(S&&S.iterator){Object.defineProperty(it,S.iterator,{"
        "value:function(){return it;},writable:false,configurable:false});}return it;},writable:false,configurable:false});}"
        "if(typeof g.__pcoreDecorateCollection13==='function'){return g.__pcoreDecorateCollection13(a,'NodeList',false);}return a;}"
        "function nodes(o){var a,n,i,x;if(o===doc){return doc.childNodes;}"
        "if(o.__nodes11){return o.__nodes11;}a=[];"
        "if(!o||typeof o.__id!=='string'){return list(a);}n=num(o,14,0);for(i=0;i<n;i++){"
        "x=child(o,i);if(x!==null){a.push(x);}}o.__nodes11=list(a);return o.__nodes11;}"
        "function child(o,i){var t=num(o,15,i),id,k,n,v;"
        "if(t===0){return null;}id=r(o,18,i);if(t===1&&typeof id==='string'&&id!==''){return wrap(id);}"
        "k=o.__id+'|'+i;if(cache[k]){return cache[k];}n={__owner11:o,__index11:i};"
        "Object.defineProperty(n,'nodeType',{value:t,writable:false,configurable:false,enumerable:true});"
        "v=r(o,16,i);if(typeof v!=='string'){v='';}if(t===1){v=v.toUpperCase();}"
        "Object.defineProperty(n,'nodeName',{value:v,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(n,'ownerDocument',{value:doc,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(n,'baseURI',{get:function(){return g.location&&g.location.href!==undefined?"
        "String(g.location.href):'';},enumerable:true});Object.defineProperty(n,'namespaceURI',{"
        "get:function(){return n.nodeType===1?'http://www.w3.org/1999/xhtml':null;},enumerable:true});"
        "Object.defineProperty(n,'prefix',{value:null,writable:false,configurable:false,enumerable:true});"
        "n.isDefaultNamespace=function(v){var s=v===null||v===undefined?null:String(v);"
        "var ns=n.namespaceURI;return ns===null?s===null:ns===s;};"
        "n.lookupNamespaceURI=function(v){var s=v===null||v===undefined?'':String(v);"
        "if(s==='xml'){return 'http://www.w3.org/XML/1998/namespace';}"
        "return s===''&&n.nodeType===1?'http://www.w3.org/1999/xhtml':null;};"
        "Object.defineProperty(n,'id',{get:function(){var z=r(o,18,i);return typeof z==='string'?z:'';},enumerable:true});"
        "Object.defineProperty(n,'tagName',{get:function(){return n.nodeType===1?n.nodeName:'';},enumerable:true});"
        "Object.defineProperty(n,'localName',{get:function(){return n.nodeType===1?n.nodeName.toLowerCase():null;},enumerable:true});"
        "Object.defineProperty(n,'nodeValue',{get:function(){var z;if(n.nodeType===1){return null;}"
        "z=r(o,17,i);return typeof z==='string'?z:null;},enumerable:true});"
        "Object.defineProperty(n,'textContent',{get:function(){var z=r(o,19,i);return typeof z==='string'?z:'';},enumerable:true});"
        "Object.defineProperty(n,'data',{get:function(){var z=n.nodeValue;return z===null?'':z;},enumerable:true});"
        "Object.defineProperty(n,'length',{get:function(){return n.data.length;},enumerable:true});"
        "n.substringData=function(offset,count){var a=Number(offset),b=Number(count),s=n.data;"
        "if(a!==a||b!==b||a<0||b<0){return '';}a=Math.floor(a);b=Math.floor(b);"
        "return s.substring(a,a+b);};"
        "Object.defineProperty(n,'parentNode',{get:function(){return o;},enumerable:true});"
        "Object.defineProperty(n,'parentElement',{get:function(){return o;},enumerable:true});"
        "Object.defineProperty(n,'previousSibling',{get:function(){return sibling(n,-1);},enumerable:true});"
        "Object.defineProperty(n,'nextSibling',{get:function(){return sibling(n,1);},enumerable:true});"
        "Object.defineProperty(n,'firstChild',{get:function(){var a=nodes(n);return a.length?a[0]:null;},enumerable:true});"
        "Object.defineProperty(n,'lastChild',{get:function(){var a=nodes(n);return a.length?a[a.length-1]:null;},enumerable:true});"
        "Object.defineProperty(n,'previousElementSibling',{get:function(){return elementSibling(n,-1);},enumerable:true});"
        "Object.defineProperty(n,'nextElementSibling',{get:function(){return elementSibling(n,1);},enumerable:true});"
        "Object.defineProperty(n,'childNodes',{get:function(){return n.nodeType===1?nodes(n):list([]);},enumerable:true});"
        "Object.defineProperty(n,'hasChildNodes',{value:function(){return n.nodeType===1&&nodes(n).length>0;},enumerable:true});"
        "Object.defineProperty(n,'isConnected',{get:function(){return !!(o&&o.isConnected);},enumerable:true});"
        "n.isSameNode=function(other){return typeof g.__pcoreNodeSame12==='function'?"
        "g.__pcoreNodeSame12(n,other):same(n,other);};"
        "n.isEqualNode=function(other){return typeof g.__pcoreNodeEqual12==='function'?"
        "g.__pcoreNodeEqual12(n,other):same(n,other);};"
        "n.getRootNode=function(options){return typeof g.__pcoreNodeRoot12==='function'?"
        "g.__pcoreNodeRoot12(n,options):doc;};"
        "n.compareDocumentPosition=function(other){return typeof g.__pcoreNodePosition12==='function'?"
        "g.__pcoreNodePosition12(n,other):33;};"
        "cache[k]=n;return n;}"
        "function parent(o){var id;if(o&&o.__owner11){return o.__owner11;}"
        "if(!o||typeof o.__id!=='string'){return null;}"
        "if(o.__id==='__positron_document_element__'){return doc;}"
        "id=r(o,1,0);return typeof id==='string'?wrap(id):null;}"
        "function parentElement(o){var p=parent(o);return p===doc?null:p;}"
        "function same(a,b){return a===b||!!(a&&b&&a.__id&&b.__id&&a.__id===b.__id);}"
        "function sibling(o,step){var p=parent(o),a,i;if(!p){return null;}a=nodes(p);"
        "for(i=0;i<a.length;i++){if(same(a[i],o)){i+=step;return i>=0&&i<a.length?a[i]:null;}}return null;}"
        "function elementSibling(o,step){var n=sibling(o,step);while(n!==null){if(n.nodeType===1){return n;}n=sibling(n,step);}return null;}"
        "function contains(o,other){var n=other,i=0;if(!other){return false;}while(n&&i<64){if(same(n,o)){return true;}n=parent(n);i++;}return false;}"
        "Object.defineProperty(P.prototype,'childNodes',{get:function(){return nodes(this);},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'parentElement',{get:function(){return parentElement(this);},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'parentNode',{get:function(){return parent(this);},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'firstChild',{get:function(){var a=nodes(this);return a.length?a[0]:null;},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'lastChild',{get:function(){var a=nodes(this);return a.length?a[a.length-1]:null;},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'previousSibling',{get:function(){return sibling(this,-1);},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'nextSibling',{get:function(){return sibling(this,1);},enumerable:true,configurable:true});"
        "Object.defineProperty(P.prototype,'firstElementChild',{get:function(){var a=nodes(this),i;for(i=0;i<a.length;i++){if(a[i].nodeType===1){return a[i];}}return null;},enumerable:true});"
        "Object.defineProperty(P.prototype,'lastElementChild',{get:function(){var a=nodes(this),i;for(i=a.length-1;i>=0;i--){if(a[i].nodeType===1){return a[i];}}return null;},enumerable:true});"
        "Object.defineProperty(P.prototype,'previousElementSibling',{get:function(){return elementSibling(this,-1);},enumerable:true});"
        "Object.defineProperty(P.prototype,'nextElementSibling',{get:function(){return elementSibling(this,1);},enumerable:true});"
        "P.prototype.hasChildNodes=function(){return nodes(this).length>0;};"
        "P.prototype.contains=function(other){return contains(this,other);};"
        "if(!g.Node||typeof g.Node!=='object'){g.Node={};}"
        "function constant(name,value){if(g.Node[name]===undefined){Object.defineProperty(g.Node,name,{value:value,writable:false,configurable:false,enumerable:true});}}"
        "constant('ELEMENT_NODE',1);constant('ATTRIBUTE_NODE',2);constant('TEXT_NODE',3);"
        "constant('CDATA_SECTION_NODE',4);constant('PROCESSING_INSTRUCTION_NODE',7);"
        "constant('COMMENT_NODE',8);constant('DOCUMENT_NODE',9);constant('DOCUMENT_TYPE_NODE',10);"
        "constant('DOCUMENT_FRAGMENT_NODE',11);"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART12[] =
        "(function(g){"
        "var P=g.__pcorePElement;var doc=g.document;var N=g.Node;"
        "var HTML_NS='http://www.w3.org/1999/xhtml';var XML_NS='http://www.w3.org/XML/1998/namespace';"
        "function baseURI12(){return g.location&&g.location.href!==undefined?String(g.location.href):'';}"
        "function namespace12(o){return o&&Number(o.nodeType)===1?HTML_NS:null;}"
        "function defaultNamespace12(o,v){var s=v===null||v===undefined?null:String(v);"
        "var ns=namespace12(o);return ns===null?s===null:ns===s;}"
        "function lookupNamespace12(o,v){var s=v===null||v===undefined?'':String(v);"
        "if(s==='xml'){return XML_NS;}return s===''?namespace12(o):null;}"
        "function relation(o,k,i){var v;if(!o||typeof g.__pcoreGetNodeRelation!=='function'){return null;}"
        "try{v=g.__pcoreGetNodeRelation({id:o.__id,relation:k,index:i===undefined?0:i});}"
        "catch(e){return null;}return v;}"
        "function isDoctype(o){return o===doc.doctype;}"
        "function known(o){return o===doc||isDoctype(o)||!!(o&&typeof o.nodeType==='number'&&"
        "((o.nodeType===1&&typeof o.__id==='string')||o.__owner11));}"
        "function same(a,b){if(a===b){return true;}if(!known(a)||!known(b)){return false;}"
        "if(a.nodeType===1&&b.nodeType===1&&typeof a.__id==='string'&&"
        "typeof b.__id==='string'){return String(a.__id)===String(b.__id);}"
        "if(a.__owner11&&b.__owner11){return same(a.__owner11,b.__owner11)&&"
        "a.__index11===b.__index11;}return false;}"
        "function parent(o){var id;if(!known(o)||o===doc){return null;}"
        "if(isDoctype(o)){return doc;}"
        "if(o.nodeType===1&&o.__id==='__positron_document_element__'){return doc;}"
        "if(o.__owner11){return o.__owner11;}if(o.nodeType===1&&typeof o.__id==='string'){"
        "id=relation(o,1,0);if(typeof id==='string'&&id!==''){return doc.getElementById(id);}}"
        "return null;}"
        "function children(o){var a;if(!o){return [];}try{a=o.childNodes;}catch(e){a=null;}"
        "return a&&typeof a.length==='number'?a:[];}"
        "function path(o){var a=[],p=o,i=0;if(!known(o)){return a;}while(p&&i<64){"
        "a.push(p);p=parent(p);i++;}return a;}"
        "function position(a,b){var pa,pb,i,j,k,p,kids,ai,bi;"
        "if(!known(b)){return 33;}if(same(a,b)){return 0;}"
        "if(a===doc){return b.isConnected?20:33;}if(b===doc){return a.isConnected?10:33;}"
        "pa=path(a);pb=path(b);if(!pa.length||!pb.length){return 33;}"
        "for(i=0;i<pb.length;i++){if(same(pb[i],a)){return 20;}}"
        "for(i=0;i<pa.length;i++){if(same(pa[i],b)){return 10;}}"
        "i=pa.length-1;j=pb.length-1;while(i>=0&&j>=0&&same(pa[i],pb[j])){i--;j--;}"
        "if(i<0||j<0||i+1>=pa.length||j+1>=pb.length){return 33;}"
        "p=pa[i+1];kids=children(p);ai=-1;bi=-1;"
        "for(k=0;k<kids.length;k++){if(same(kids[k],pa[i])){ai=k;}"
        "if(same(kids[k],pb[j])){bi=k;}}"
        "return ai>=0&&bi>=0?(ai<bi?4:2):33;}"
        "function equal(a,b){var av,bv;if(isDoctype(a)&&b&&Number(b.nodeType)===10&&"
        "String(b.nodeName)==='html'){return true;}if(isDoctype(b)&&a&&"
        "Number(a.nodeType)===10&&String(a.nodeName)==='html'){return true;}"
        "if(!known(a)||!known(b)||a.nodeType!==b.nodeType||"
        "String(a.nodeName)!==String(b.nodeName)){return false;}if(a.nodeType===9){return a===b;}"
        "if(a.nodeType===1){return String(a.id||'')===String(b.id||'')&&"
        "String(a.textContent||'')===String(b.textContent||'');}"
        "av=a.nodeValue===null?'':String(a.nodeValue);bv=b.nodeValue===null?'':String(b.nodeValue);"
        "return av===bv;}"
        "function root(o){return known(o)?doc:null;}"
        "function contains(a,b){var p=b,i=0;if(!known(a)||!known(b)){return false;}"
        "if(a===doc){return !!b.isConnected;}while(p&&i<64){if(same(p,a)){return true;}"
        "p=parent(p);i++;}return false;}"
        "g.__pcoreNodeSame12=same;g.__pcoreNodeEqual12=equal;g.__pcoreNodeRoot12=root;"
        "g.__pcoreNodePosition12=position;g.__pcoreNodeContains12=contains;"
        "P.prototype.isSameNode=function(other){return same(this,other);};"
        "P.prototype.isEqualNode=function(other){return equal(this,other);};"
        "P.prototype.getRootNode=function(options){return root(this);};"
        "P.prototype.compareDocumentPosition=function(other){return position(this,other);};"
        "P.prototype.contains=function(other){return contains(this,other);};"
        "Object.defineProperty(P.prototype,'baseURI',{get:baseURI12,enumerable:true});"
        "Object.defineProperty(P.prototype,'namespaceURI',{get:function(){return HTML_NS;},enumerable:true});"
        "Object.defineProperty(P.prototype,'prefix',{value:null,writable:false,configurable:false,enumerable:true});"
        "P.prototype.isDefaultNamespace=function(v){return defaultNamespace12(this,v);};"
        "P.prototype.lookupNamespaceURI=function(v){return lookupNamespace12(this,v);};"
        "doc.isSameNode=function(other){return same(this,other);};"
        "doc.isEqualNode=function(other){return equal(this,other);};"
        "doc.getRootNode=function(options){return this;};"
        "doc.compareDocumentPosition=function(other){return position(this,other);};"
        "doc.contains=function(other){return contains(this,other);};"
        "if(doc.nodeValue===undefined){Object.defineProperty(doc,'nodeValue',{value:null,"
        "writable:false,configurable:false,enumerable:true});}"
        "if(doc.ownerDocument===undefined){Object.defineProperty(doc,'ownerDocument',{value:null,"
        "writable:false,configurable:false,enumerable:true});}"
        "if(doc.parentNode===undefined){Object.defineProperty(doc,'parentNode',{value:null,"
        "writable:false,configurable:false,enumerable:true});}"
        "if(doc.parentElement===undefined){Object.defineProperty(doc,'parentElement',{value:null,"
        "writable:false,configurable:false,enumerable:true});}"
        "if(doc.isConnected===undefined){Object.defineProperty(doc,'isConnected',{value:true,"
        "writable:false,configurable:false,enumerable:true});}"
        "Object.defineProperty(doc,'baseURI',{get:baseURI12,enumerable:true});"
        "Object.defineProperty(doc,'namespaceURI',{value:null,writable:false,configurable:false,enumerable:true});"
        "Object.defineProperty(doc,'prefix',{value:null,writable:false,configurable:false,enumerable:true});"
        "doc.isDefaultNamespace=function(v){return defaultNamespace12(doc,v);};"
        "doc.lookupNamespaceURI=function(v){return lookupNamespace12(doc,v);};"
        "function constant(name,value){if(N[name]===undefined){Object.defineProperty(N,name,{"
        "value:value,writable:false,configurable:false,enumerable:true});}}"
        "constant('DOCUMENT_POSITION_DISCONNECTED',1);constant('DOCUMENT_POSITION_PRECEDING',2);"
        "constant('DOCUMENT_POSITION_FOLLOWING',4);constant('DOCUMENT_POSITION_CONTAINS',8);"
        "constant('DOCUMENT_POSITION_CONTAINED_BY',16);constant('DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC',32);"
        "})(this);";
    static const char P_BROWSER_SCRIPT_BOOTSTRAP_PART13[] =
        "(function(g){"
        "var S=g.Symbol;"
        "function iterator13(a,kind){var i=0;var n=a.length;var it={};"
        "it.next=function(){var v;if(i>=n){return {done:true,value:undefined};}"
        "if(kind==='keys'){v=i;}else if(kind==='entries'){v=[i,a[i]];}else{v=a[i];}"
        "i++;return {done:false,value:v};};"
        "if(S&&S.iterator){Object.defineProperty(it,S.iterator,{value:function(){return it;},"
        "writable:false,configurable:false});}return it;}"
        "function define13(a,name,value){if(typeof a[name]!=='function'){Object.defineProperty(a,name,{"
        "value:value,writable:false,configurable:false,enumerable:false});}}"
        "function decorate13(a,tag,named){var i;var n;"
        "if(!a||typeof a.length!=='number'){return a;}"
        "if(typeof a.item!=='function'){define13(a,'item',function(index){var x=Number(index);"
        "return x===x&&x>=0&&x===Math.floor(x)&&x<this.length?this[x]:null;});}"
        "if(named&&typeof a.namedItem!=='function'){define13(a,'namedItem',function(name){"
        "var s=String(name);var j;for(j=0;j<this.length;j++){if(this[j]&&(this[j].id===s||"
        "this[j].name===s)){return this[j];}}return null;});}"
        "define13(a,'forEach',function(callback,thisArg){var j;if(typeof callback!=='function'){"
        "throw new TypeError('callback');}n=this.length;for(j=0;j<n;j++){callback.call(thisArg,this[j],j,this);}});"
        "define13(a,'keys',function(){return iterator13(this,'keys');});"
        "define13(a,'values',function(){return iterator13(this,'values');});"
        "define13(a,'entries',function(){return iterator13(this,'entries');});"
        "if(S&&S.iterator&&typeof a[S.iterator]!=='function'){Object.defineProperty(a,S.iterator,{"
        "value:function(){return iterator13(this,'values');},writable:false,configurable:false});}"
        "if(S&&S.toStringTag){try{if(a[S.toStringTag]!==tag){Object.defineProperty(a,S.toStringTag,{"
        "value:tag,writable:false,configurable:true,enumerable:false});}}catch(tagError){}}"
        "return a;}"
        "g.__pcoreDecorateCollection13=decorate13;"
        "})(this);";
PBROWSER_API int PBrowser_ScriptSessionEvaluateBootstrap(HANDLE hSession)
{
    int result;

    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART1, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART2, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART3, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART4, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART5, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART6, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART7, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART8, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART9, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART10, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART11, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    result = PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART12, -1);
    if (result != PSCRIPT_OK) {
        return result;
    }
    return PBrowser_ScriptSessionEvaluate(hSession,
            P_BROWSER_SCRIPT_BOOTSTRAP_PART13, -1);
}
typedef struct p_browser_script_dom_read_binding {
    PBrowserScriptDomReadCallbacks callbacks;
} p_browser_script_dom_read_binding;

typedef struct p_browser_script_dom_relation_binding {
    PBrowserScriptDomRelationCallbacks callbacks;
} p_browser_script_dom_relation_binding;

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
    p_browser_script_dom_relation_binding *dom_relation;
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

static int p_browser_script_dispatch_page_lifecycle(
        p_browser_script_session *session, const char *state)
{
    int rc;

    if (session == NULL || state == NULL || state[0] == '\0' ||
            (strcmp(state, "interactive") != 0 &&
            strcmp(state, "domcontentloaded") != 0 &&
            strcmp(state, "complete") != 0)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    rc = PScript_SetGlobalString(session->runtime,
            "__pcoreLifecycleState", -1, state, -1);
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    rc = PScript_Evaluate(session->runtime,
            "if(typeof __pcorePageLifecycle==='function')"
            "{__pcorePageLifecycle(__pcoreLifecycleState);};", -1);
    (void) PScript_Evaluate(session->runtime,
            "delete this.__pcoreLifecycleState;", -1);
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

static int p_browser_script_relation_is_count(unsigned int relation)
{
    return relation == PBROWSER_SCRIPT_NODE_RELATION_CHILD_COUNT ||
            relation == PBROWSER_SCRIPT_NODE_RELATION_FORM_CONTROL_COUNT ||
            relation == PBROWSER_SCRIPT_NODE_RELATION_ATTRIBUTE_COUNT ||
            relation == PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_COUNT ||
            relation == PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_TYPE_AT;
}

static int p_browser_script_dom_get_relation(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len)
{
    p_browser_script_dom_relation_binding *binding;
    HANDLE root;
    HANDLE object;
    const char *id;
    int relation_value;
    int index_value;
    int result;
    int number;
    char *value;
    int value_len;
    int allocated_len;

    binding = (p_browser_script_dom_relation_binding *) pw;
    object = NULL;
    root = p_browser_script_args_object(args_json, args_len, &object);
    id = (object != NULL) ? PJson_GetString(object, "id") : NULL;
    relation_value = (object != NULL) ?
            PJson_GetInt(object, "relation") : 0;
    index_value = (object != NULL) ? PJson_GetInt(object, "index") : 0;
    value = NULL;
    value_len = 0;
    number = 0;
    if (binding == NULL || root == NULL || id == NULL || id[0] == '\0' ||
            relation_value <= 0 || index_value < 0 ||
            binding->callbacks.get_relation == NULL) {
        PJson_Free(root);
        return 1;
    }
    if (p_browser_script_relation_is_count((unsigned int) relation_value)) {
        result = binding->callbacks.get_relation(binding->callbacks.pw, id,
                (unsigned int) relation_value, (unsigned int) index_value,
                NULL, 0, NULL, &number);
        PJson_Free(root);
        if (result < 0) {
            return 1;
        }
        if (result == 2) {
            number = 0;
        }
        if (number < 0) {
            number = 0;
        }
        return p_browser_script_write_int(number, out_json, out_capacity,
                out_len);
    }
    result = binding->callbacks.get_relation(binding->callbacks.pw, id,
            (unsigned int) relation_value, (unsigned int) index_value,
            NULL, 0, &value_len, NULL);
    if (result == 2) {
        PJson_Free(root);
        return p_browser_script_write_null(out_json, out_capacity, out_len);
    }
    if (result != 0 || value_len < 0 ||
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
    result = binding->callbacks.get_relation(binding->callbacks.pw, id,
            (unsigned int) relation_value, (unsigned int) index_value, value,
            allocated_len + 1, &value_len, NULL);
    if (result != 0 || value_len < 0 || value_len > allocated_len ||
            value_len > PBROWSER_SCRIPT_TEXT_MAX_BYTES) {
        free(value);
        PJson_Free(root);
        return 1;
    }
    value[value_len] = '\0';
    result = p_browser_script_write_string(value, out_json, out_capacity,
            out_len);
    free(value);
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
    session->dom_relation = NULL;
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
    session->runtime = PScript_CreateEx(budget_ms,
            P_BROWSER_SCRIPT_MEMORY_LIMIT_BYTES);
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
    if (session->dom_relation != NULL) {
        PScript_UnregisterGlobalJsonFunction(session->runtime,
                "__pcoreGetNodeRelation", -1);
        free(session->dom_relation);
        session->dom_relation = NULL;
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

PBROWSER_API int PBrowser_ScriptSessionDispatchPageLifecycle(
        HANDLE hSession, const char *state)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return p_browser_script_dispatch_page_lifecycle(session, state);
}

PBROWSER_API int PBrowser_ScriptSessionRunTimers(HANDLE hSession,
        unsigned long now_ms)
{
    p_browser_script_session *session;
    char args[64];
    int length;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    length = _snprintf(args, sizeof(args) - 1, "[%lu]", now_ms);
    if (length < 0 || length >= (int) sizeof(args) - 1) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    args[length] = '\0';
    return PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreRunTimers", args);
}

PBROWSER_API int PBrowser_ScriptSessionRunAnimationFrames(HANDLE hSession,
        unsigned long timestamp_ms)
{
    p_browser_script_session *session;
    char args[64];
    int length;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    length = _snprintf(args, sizeof(args) - 1, "[%lu]", timestamp_ms);
    if (length < 0 || length >= (int) sizeof(args) - 1) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    args[length] = '\0';
    return PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreRunAnimationFrames", args);
}

PBROWSER_API int PBrowser_ScriptSessionDispatchVisibility(HANDLE hSession,
        int hidden)
{
    p_browser_script_session *session;
    const char *args;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    args = hidden ? "[true]" : "[false]";
    return PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreVisibilityChange", args);
}

PBROWSER_API int PBrowser_ScriptSessionRunMicrotasks(HANDLE hSession)
{
    p_browser_script_session *session;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    return PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreRunMicrotasks", "[]");
}

PBROWSER_API int PBrowser_ScriptSessionRunIdleCallbacks(HANDLE hSession,
        unsigned long deadline_ms)
{
    p_browser_script_session *session;
    char args[64];
    int length;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    length = _snprintf(args, sizeof(args) - 1, "[%lu]", deadline_ms);
    if (length < 0 || length >= (int) sizeof(args) - 1) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    args[length] = '\0';
    return PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreRunIdleCallbacks", args);
}

PBROWSER_API int PBrowser_ScriptSessionRunMessages(HANDLE hSession,
        unsigned long limit)
{
    p_browser_script_session *session;
    char args[64];
    int length;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    length = _snprintf(args, sizeof(args) - 1, "[%lu]", limit);
    if (length < 0 || length >= (int) sizeof(args) - 1) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    args[length] = '\0';
    return PBrowser_ScriptSessionCallGlobalJson(hSession,
            "__pcoreRunMessages", args);
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

PBROWSER_API int PBrowser_ScriptSessionRegisterDomRelationCallbacks(
        HANDLE hSession, const PBrowserScriptDomRelationCallbacks *callbacks)
{
    p_browser_script_session *session;
    p_browser_script_dom_relation_binding *binding;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session) || callbacks == NULL ||
            callbacks->size < sizeof(PBrowserScriptDomRelationCallbacks) ||
            callbacks->get_relation == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_relation != NULL) {
        return PSCRIPT_ERROR_GLOBAL;
    }
    binding = (p_browser_script_dom_relation_binding *) malloc(
            sizeof(*binding));
    if (binding == NULL) {
        return PSCRIPT_ERROR_FATAL;
    }
    memcpy(&binding->callbacks, callbacks, sizeof(binding->callbacks));
    rc = PScript_RegisterGlobalJsonFunction(session->runtime,
            "__pcoreGetNodeRelation", -1, p_browser_script_dom_get_relation,
            binding);
    if (rc != PSCRIPT_OK) {
        free(binding);
        return rc;
    }
    session->dom_relation = binding;
    return PSCRIPT_OK;
}

PBROWSER_API int PBrowser_ScriptSessionUnregisterDomRelationCallbacks(
        HANDLE hSession)
{
    p_browser_script_session *session;
    int rc;

    session = p_script_session(hSession);
    if (!p_script_session_valid(session)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (session->dom_relation == NULL) {
        return PSCRIPT_OK;
    }
    rc = PScript_UnregisterGlobalJsonFunction(session->runtime,
            "__pcoreGetNodeRelation", -1);
    free(session->dom_relation);
    session->dom_relation = NULL;
    return rc;
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
