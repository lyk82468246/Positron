/*
 * positron_browser.h - product-level browser session primitives.
 *
 * This first public slice owns document history and same-origin state
 * transitions without depending on a window, network stack, DOM renderer or
 * JavaScript engine. A browser host can compose this DLL with positron_core,
 * positron_script and its own transport/UI callbacks.
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

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_BROWSER_H */
