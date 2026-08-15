/*
 * positron_browser.h - product-level browser session primitives.
 *
 * The first public slices own document history and browser-script session
 * lifetime without depending on a window, network stack or DOM renderer. A
 * browser host composes this DLL with positron_core, positron_script and its
 * own transport/UI callbacks.
 *
 * All strings are UTF-8. History handles and returned strings are owned by
 * this DLL: callers destroy handles with PBrowser_HistoryDestroy and must not
 * free borrowed entry/state strings. Borrowed strings remain valid until the
 * next mutation of the same history handle or destruction of that handle.
 */

#ifndef POSITRON_BROWSER_H
#define POSITRON_BROWSER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_BROWSER_EXPORTS
#  define PBROWSER_API __declspec(dllexport)
#else
#  define PBROWSER_API __declspec(dllimport)
#endif

#define PBROWSER_ABI_VERSION 0x00010000UL

#define PBROWSER_HISTORY_MAX 16
#define PBROWSER_HISTORY_URL_MAX 1024
#define PBROWSER_HISTORY_STATE_MAX 1024

#define PBROWSER_HISTORY_TARGET_NEW (-1)
#define PBROWSER_HISTORY_TARGET_REPLACE_CURRENT (-2)

#define PBROWSER_HISTORY_METHOD_OTHER 0
#define PBROWSER_HISTORY_METHOD_GET 1

#define PBROWSER_SCRIPT_MAX_FUNCTIONS 16

#define PBROWSER_OK 0
#define PBROWSER_ERROR_ARGUMENT (-1)
#define PBROWSER_ERROR_LIMIT (-2)
#define PBROWSER_ERROR_ORIGIN (-3)
#define PBROWSER_ERROR_STATE (-4)
#define PBROWSER_ERROR_RANGE (-5)
#define PBROWSER_ERROR_METHOD (-6)

/* Returns the major/minor ABI version encoded as 0xMMMMmmmm. */
PBROWSER_API unsigned long PBrowser_AbiVersion(void);

/* Create/destroy an independent history session. The returned HANDLE is an
 * opaque browser-owned object and is not a Win32 kernel handle. */
PBROWSER_API HANDLE PBrowser_HistoryCreate(void);
PBROWSER_API void PBrowser_HistoryDestroy(HANDLE hHistory);
PBROWSER_API void PBrowser_HistoryReset(HANDLE hHistory);

/* Borrowed history inspection. Invalid handles or indexes return zero, -1 or
 * NULL as appropriate. An entry without state is represented by JSON "null". */
PBROWSER_API int PBrowser_HistoryCount(HANDLE hHistory);
PBROWSER_API int PBrowser_HistoryIndex(HANDLE hHistory);
PBROWSER_API const char *PBrowser_HistoryCurrentUrl(HANDLE hHistory);
PBROWSER_API const char *PBrowser_HistoryCurrentState(HANDLE hHistory);
PBROWSER_API const char *PBrowser_HistoryEntryUrl(HANDLE hHistory, int index);
PBROWSER_API const char *PBrowser_HistoryEntryState(HANDLE hHistory, int index);
PBROWSER_API unsigned long PBrowser_HistoryEntryDocumentId(HANDLE hHistory,
        int index);

/* URL policy used by history state operations. This is intentionally a
 * narrow textual same-origin check, not a complete URL Standard parser. */
PBROWSER_API int PBrowser_HistorySameOriginUrl(const char *left,
        const char *right);
PBROWSER_API int PBrowser_HistorySameBaseUrl(const char *left,
        const char *right);
PBROWSER_API int PBrowser_HistoryIsSameDocumentTarget(HANDLE hHistory,
        int target_index);

/* Commit a successful document navigation. method must be
 * PBROWSER_HISTORY_METHOD_GET; target_index is TARGET_NEW for a new entry,
 * TARGET_REPLACE_CURRENT for replace navigation, or an existing entry index
 * for traversal. Any other negative target returns PBROWSER_ERROR_RANGE. */
PBROWSER_API int PBrowser_HistoryCommitNavigation(HANDLE hHistory,
        const char *url, int method, int target_index);
PBROWSER_API int PBrowser_HistoryCommitNavigationWithState(HANDLE hHistory,
        const char *url, int method, int target_index,
        const char *state_json);
PBROWSER_API int PBrowser_HistoryReplaceCurrent(HANDLE hHistory,
        const char *url);
PBROWSER_API int PBrowser_HistoryCommitTarget(HANDLE hHistory,
        int target_index);
PBROWSER_API int PBrowser_HistoryCommitTargetDocument(HANDLE hHistory,
        int target_index);

/* Same-document history state operations. The URL must be same-origin with
 * the current entry and state_json must be a valid compact JSON value within
 * PBROWSER_HISTORY_STATE_MAX bytes. */
PBROWSER_API int PBrowser_HistoryReplaceState(HANDLE hHistory,
        const char *url, const char *state_json);
PBROWSER_API int PBrowser_HistoryPushState(HANDLE hHistory,
        const char *url, const char *state_json);

/* Traversal targets are borrowed strings. The output index is required. */
PBROWSER_API const char *PBrowser_HistoryBackTarget(HANDLE hHistory,
        int *out_index);
PBROWSER_API const char *PBrowser_HistoryForwardTarget(HANDLE hHistory,
        int *out_index);
PBROWSER_API const char *PBrowser_HistoryGoTarget(HANDLE hHistory, int delta,
        int *out_index);

/* Projected values used before a pending navigation is committed. */
PBROWSER_API int PBrowser_HistoryNavigationLength(HANDLE hHistory,
        const char *url, int method, int target_index);
PBROWSER_API int PBrowser_HistoryNavigationIndex(HANDLE hHistory,
        const char *url, int method, int target_index);
PBROWSER_API const char *PBrowser_HistoryNavigationState(HANDLE hHistory,
        const char *url, int method, int target_index);

/* Synchronous JSON callback shape shared with positron_script. The callback
 * runs on the caller's thread and must not re-enter or destroy its session.
 * args_json and the output buffer are borrowed for the duration of the call;
 * pw is the host-owned value supplied at registration time. */
typedef int (*PBrowserScriptJsonFunctionFn)(void *pw,
        const char *args_json, int args_len, char *out_json,
        int out_capacity, int *out_len);

/* Typed host adapters for the first product-owned DOM read callbacks. The
 * browser DLL parses the JSON argument array and encodes the JSON result;
 * the host only reads its core document through these UTF-8 callbacks.
 * has_element returns >0 for an existing id, 0 for absent and <0 on error.
 * get_text reports bytes excluding the trailing NUL through out_len and
 * returns zero on success. A call with out_text == NULL and out_capacity ==
 * 0 is a size probe; it must only set out_len. A normal call writes at most
 * out_capacity bytes including space for the trailing NUL. */
typedef int (*PBrowserScriptHasElementFn)(void *pw, const char *id);
typedef int (*PBrowserScriptGetTextFn)(void *pw, const char *id,
        char *out_text, int out_capacity, int *out_len);
typedef struct PBrowserScriptDomReadCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptHasElementFn has_element;
    PBrowserScriptGetTextFn get_text;
} PBrowserScriptDomReadCallbacks;

/* Typed host adapters for the first product-owned DOM write callback. The
 * browser DLL parses the JSON argument object and encodes the JSON result;
 * the host only mutates its core document through this UTF-8 callback.
 * set_text returns >0 when the target was updated, 0 when the target or
 * operation was unavailable and <0 for an adapter error. */
typedef int (*PBrowserScriptSetTextFn)(void *pw, const char *id,
        const char *text);
typedef struct PBrowserScriptDomWriteCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptSetTextFn set_text;
} PBrowserScriptDomWriteCallbacks;

/* Typed host adapters for product-owned form value callbacks. The browser
 * DLL parses and encodes the JSON value requests; the host only reads or
 * updates its core document through these UTF-8 callbacks. get_value follows
 * the same size-probe contract as PBrowserScriptGetTextFn and returns zero on
 * success or a negative value on error. set_value returns >0 when the target
 * was updated, 0 when the target or operation was unavailable and <0 on an
 * adapter error. */
typedef int (*PBrowserScriptGetValueFn)(void *pw, const char *id,
        char *out_value, int out_capacity, int *out_len);
typedef int (*PBrowserScriptSetValueFn)(void *pw, const char *id,
        const char *value);
typedef struct PBrowserScriptDomValueCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetValueFn get_value;
    PBrowserScriptSetValueFn set_value;
} PBrowserScriptDomValueCallbacks;

/* Typed host adapters for product-owned DOM attribute callbacks. The
 * browser DLL parses and encodes JSON. get_attribute returns 0 when an
 * attribute is present, 1 when it is absent and <0 on error; its out_len
 * excludes the trailing NUL and follows the same probe contract as
 * PBrowserScriptGetTextFn. set_attribute/remove_attribute return >0 on
 * success, 0 when the target or operation is unavailable and <0 on error. */
typedef int (*PBrowserScriptGetAttributeFn)(void *pw, const char *id,
        const char *name, char *out_value, int out_capacity, int *out_len);
typedef int (*PBrowserScriptSetAttributeFn)(void *pw, const char *id,
        const char *name, const char *value);
typedef int (*PBrowserScriptRemoveAttributeFn)(void *pw, const char *id,
        const char *name);
typedef struct PBrowserScriptDomAttributeCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetAttributeFn get_attribute;
    PBrowserScriptSetAttributeFn set_attribute;
    PBrowserScriptRemoveAttributeFn remove_attribute;
} PBrowserScriptDomAttributeCallbacks;

/* Typed host adapters for product-owned DOM event registration. The browser
 * DLL parses the JSON add/remove requests and returns the host's opaque
 * listener token to JavaScript. add_listener returns zero when registration
 * is unavailable; remove_listener returns >0 when a token was removed, 0
 * when it was not found and <0 for an adapter error. The token is only
 * meaningful to the host adapter and is never interpreted by this DLL. */
typedef unsigned int (*PBrowserScriptAddEventListenerFn)(void *pw,
        const char *element_id, const char *event_type, int capture);
typedef int (*PBrowserScriptRemoveEventListenerFn)(void *pw,
        unsigned int listener);
typedef struct PBrowserScriptEventCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptAddEventListenerFn add_listener;
    PBrowserScriptRemoveEventListenerFn remove_listener;
} PBrowserScriptEventCallbacks;

/* Synchronous event data passed to PBrowser_ScriptSessionDispatchEvent.
 * Strings are UTF-8 and borrowed for the duration of the call. The size tag
 * permits compatible extensions without exposing positron_core types. */
typedef struct PBrowserScriptEventInfo {
    unsigned long size;
    unsigned int phase;
    int bubbles;
    int cancelable;
    int trusted;
    int default_prevented;
    const char *key;
    unsigned int key_code;
    unsigned int char_code;
    int repeat;
    int shift;
    int ctrl;
    int alt;
    const char *input_type;
    const char *data;
    int is_composing;
    const char *target_id;
    const char *current_target_id;
} PBrowserScriptEventInfo;

#define PBROWSER_SCRIPT_EVENT_ACTION_NONE            0x00u
#define PBROWSER_SCRIPT_EVENT_ACTION_PREVENT_DEFAULT 0x01u

/* Browser script session. The session owns one PScript context and all
 * registered native functions. It does not own a core document or any host
 * callback pw value. Return codes from Evaluate/Call/Set/Register are the
 * stable positron_script result codes; zero is success. */
PBROWSER_API HANDLE PBrowser_ScriptSessionCreate(unsigned long budget_ms);
PBROWSER_API void PBrowser_ScriptSessionDestroy(HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterJsonFunction(HANDLE hSession,
        const char *name, PBrowserScriptJsonFunctionFn fn, void *pw);
PBROWSER_API int PBrowser_ScriptSessionUnregisterJsonFunction(HANDLE hSession,
        const char *name);
PBROWSER_API int PBrowser_ScriptSessionEvaluate(HANDLE hSession,
        const char *source, int source_len);
/* Evaluate the product-owned browser bootstrap after the host has installed
 * the __pcore* globals and JSON callbacks it needs. The bootstrap only
 * creates the browser-facing window/document/history/location/event objects;
 * it does not own the core document, native controls or host callback pw. */
PBROWSER_API int PBrowser_ScriptSessionEvaluateBootstrap(HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomReadCallbacks(
        HANDLE hSession, const PBrowserScriptDomReadCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomReadCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomWriteCallbacks(
        HANDLE hSession, const PBrowserScriptDomWriteCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomWriteCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomValueCallbacks(
        HANDLE hSession, const PBrowserScriptDomValueCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomValueCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomAttributeCallbacks(
        HANDLE hSession,
        const PBrowserScriptDomAttributeCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomAttributeCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterEventCallbacks(HANDLE hSession,
        const PBrowserScriptEventCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterEventCallbacks(
        HANDLE hSession);
/* Dispatch one synchronous native event to the listener registered through
 * the product bootstrap. The return value is a PBROWSER_SCRIPT_EVENT_ACTION_*
 * bitmask; failures conservatively return ACTION_NONE. */
PBROWSER_API unsigned int PBrowser_ScriptSessionDispatchEvent(
        HANDLE hSession, unsigned int listener, const char *event_type,
        const PBrowserScriptEventInfo *event_info);
PBROWSER_API int PBrowser_ScriptSessionSetGlobalString(HANDLE hSession,
        const char *name, const char *value);
PBROWSER_API int PBrowser_ScriptSessionSetGlobalNumber(HANDLE hSession,
        const char *name, double value);
PBROWSER_API int PBrowser_ScriptSessionSetGlobalJson(HANDLE hSession,
        const char *name, const char *value_json);
PBROWSER_API int PBrowser_ScriptSessionCallGlobalJson(HANDLE hSession,
        const char *name, const char *args_json);
PBROWSER_API const char *PBrowser_ScriptSessionGetResult(HANDLE hSession);
PBROWSER_API const char *PBrowser_ScriptSessionGetError(HANDLE hSession);
PBROWSER_API unsigned long PBrowser_ScriptSessionNativeFunctionCount(
        HANDLE hSession);

/* Borrowed compatibility handle for consumers that still use read-only
 * positron_script diagnostics during this migration. The returned HANDLE is
 * owned by the browser session; callers must never pass it to PScript_Destroy
 * or retain it after the session is destroyed. */
PBROWSER_API HANDLE PBrowser_ScriptSessionRuntime(HANDLE hSession);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_BROWSER_H */
