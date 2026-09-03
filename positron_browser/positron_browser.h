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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_BROWSER_EXPORTS
#  define PBROWSER_API __declspec(dllexport)
#else
#  define PBROWSER_API __declspec(dllimport)
#endif

#define PBROWSER_ABI_VERSION 0x00010005UL

#define PBROWSER_HISTORY_MAX 16
#define PBROWSER_HISTORY_URL_MAX 1024
#define PBROWSER_HISTORY_STATE_MAX 1024

#define PBROWSER_HISTORY_TARGET_NEW (-1)
#define PBROWSER_HISTORY_TARGET_REPLACE_CURRENT (-2)

#define PBROWSER_HISTORY_METHOD_OTHER 0
#define PBROWSER_HISTORY_METHOD_GET 1

#define PBROWSER_SCRIPT_MAX_FUNCTIONS 19
#define PBROWSER_SCRIPT_ANCHOR_TARGET_MAX 64
#define PBROWSER_SCRIPT_ANCHOR_REL_MAX 256
#define PBROWSER_SCRIPT_WINDOW_NAME_MAX 64
#define PBROWSER_SCRIPT_DIALOG_VALUE_MAX 1024
#define PBROWSER_SCRIPT_DIALOG_ID_MAX 1024
#define PBROWSER_SCRIPT_ACTIVE_ELEMENT_ID_MAX 1024
#define PBROWSER_SCRIPT_INTERACTION_STATE_MAX 16
#define PBROWSER_SCRIPT_INTERACTION_ACTIVE "active"
#define PBROWSER_SCRIPT_INTERACTION_HOVER "hover"
#define PBROWSER_SCRIPT_INTERACTION_VISITED "visited"
#define PBROWSER_SCRIPT_LINK_HREF_MAX PBROWSER_HISTORY_URL_MAX

#define PBROWSER_OK 0
#define PBROWSER_ERROR_ARGUMENT (-1)
#define PBROWSER_ERROR_LIMIT (-2)
#define PBROWSER_ERROR_ORIGIN (-3)
#define PBROWSER_ERROR_STATE (-4)
#define PBROWSER_ERROR_RANGE (-5)
#define PBROWSER_ERROR_METHOD (-6)
#define PBROWSER_ERROR_MEMORY (-7)
#define PBROWSER_ERROR_NOT_FOUND (-8)

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

/* History-entry viewport snapshots are opaque, non-negative application
 * coordinates. The browser layer stores them with the entry but never
 * clamps them against a document or touches a window; the host applies its
 * own viewport/document bounds when restoring the snapshot. Both output
 * pointers are required. */
PBROWSER_API int PBrowser_HistoryEntryScroll(HANDLE hHistory, int index,
        int *out_scroll_x, int *out_scroll_y);
PBROWSER_API int PBrowser_HistorySetEntryScroll(HANDLE hHistory, int index,
        int scroll_x, int scroll_y);

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

/* Host-independent navigation resource transaction. The handle owns a
 * bounded, deduplicated set of resource records for one candidate page. It
 * does not create threads, perform network I/O, or reference a Core
 * document; a host supplies attempts and successful bytes through these
 * calls, then asks for the commit gate before swapping the page. */
#define PBROWSER_NAVIGATION_RESOURCE_MAX 64
#define PBROWSER_NAVIGATION_RESOURCE_URL_MAX PBROWSER_HISTORY_URL_MAX
#define PBROWSER_NAVIGATION_RESOURCE_BYTES_MAX (2UL * 1024UL * 1024UL)
#define PBROWSER_NAVIGATION_RESOURCE_RETRY_LIMIT 2
#define PBROWSER_NAVIGATION_RESOURCE_SUMMARY_MAX 4
#define PBROWSER_NAVIGATION_RESOURCE_SUMMARY_CAPACITY 320

#define PBROWSER_NAVIGATION_RESOURCE_PENDING 0
#define PBROWSER_NAVIGATION_RESOURCE_READY 1
#define PBROWSER_NAVIGATION_RESOURCE_FAILED 2
#define PBROWSER_NAVIGATION_RESOURCE_CANCELLED 3

#define PBROWSER_NAVIGATION_RESOURCE_ROLE_NONE 0u
#define PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET 1u
#define PBROWSER_NAVIGATION_RESOURCE_ROLE_SCRIPT 2u
#define PBROWSER_NAVIGATION_RESOURCE_ROLE_IMAGE 4u

#define PBROWSER_NAVIGATION_RESOURCE_OPTIONAL 0
#define PBROWSER_NAVIGATION_RESOURCE_REQUIRED 1

#define PBROWSER_NAVIGATION_GATE_READY 0
#define PBROWSER_NAVIGATION_GATE_PENDING 1
#define PBROWSER_NAVIGATION_GATE_REQUIRED_FAILED 2
#define PBROWSER_NAVIGATION_GATE_CANCELLED 3

#define PBROWSER_NAVIGATION_FAILURE_NONE 0
#define PBROWSER_NAVIGATION_FAILURE_RESOLVE 1
#define PBROWSER_NAVIGATION_FAILURE_TRANSPORT 2
#define PBROWSER_NAVIGATION_FAILURE_HTTP 3
#define PBROWSER_NAVIGATION_FAILURE_BUDGET 4
#define PBROWSER_NAVIGATION_FAILURE_MEMORY 5
#define PBROWSER_NAVIGATION_FAILURE_CANCELLED 6

typedef struct PBrowserNavigationResourceInfo {
    unsigned long size;
    int state;
    int failure_class;
    int required;
    unsigned int role_mask;
    int attempted;
    int attempt_count;
    int retry_count;
    int data_bytes;
} PBrowserNavigationResourceInfo;

typedef struct PBrowserNavigationResourceStats {
    unsigned long size;
    int resource_count;
    int resources_ready;
    int resources_fetched;
    int resources_failed;
    int resources_cancelled;
    int resources_pending;
    int resource_attempts;
    int resource_retries;
    int resource_retry_exhausted;
    int resource_failures_resolve;
    int resource_failures_transport;
    int resource_failures_http;
    int resource_failures_budget;
    int resource_failures_memory;
    int resource_required_failed;
    int resource_optional_failed;
    int resource_commit_gate;
    int resource_fallback_images;
    int resource_fallback_scripts;
    int resource_fallback_other;
    int resource_fallback_observed;
    int resource_failure_summary_count;
    int resource_failure_summary_truncated;
    char resource_failure_summary[
            PBROWSER_NAVIGATION_RESOURCE_SUMMARY_CAPACITY];
    int resource_bytes;
    int budget_rejected;
} PBrowserNavigationResourceStats;

/* The returned HANDLE is an opaque Browser-owned transaction, not a Win32
 * kernel handle. All records remain index-stable until destroy. Calls are
 * synchronous and the caller must serialize access to one transaction. */
PBROWSER_API HANDLE PBrowser_NavigationResourceCreate(void);
PBROWSER_API void PBrowser_NavigationResourceDestroy(HANDLE hTransaction);
PBROWSER_API int PBrowser_NavigationResourceRegister(HANDLE hTransaction,
        const char *url, int required, unsigned int role_mask,
        int *out_index);
PBROWSER_API int PBrowser_NavigationResourceFind(HANDLE hTransaction,
        const char *url, int *out_index);
PBROWSER_API int PBrowser_NavigationResourceGet(HANDLE hTransaction,
        int index, PBrowserNavigationResourceInfo *out_info);
PBROWSER_API int PBrowser_NavigationResourceGetUrl(HANDLE hTransaction,
        int index, char *out_url, int out_capacity, int *out_len);
PBROWSER_API int PBrowser_NavigationResourceCopyData(HANDLE hTransaction,
        int index, char *out_data, int out_capacity, int *out_len);
PBROWSER_API int PBrowser_NavigationResourceSetData(HANDLE hTransaction,
        int index, const char *data, int data_len);
PBROWSER_API int PBrowser_NavigationResourceBeginAttempt(HANDLE hTransaction,
        int index);
PBROWSER_API int PBrowser_NavigationResourceShouldRetry(HANDLE hTransaction,
        int index, int transport_failure, int *out_retry);
PBROWSER_API int PBrowser_NavigationResourceFail(HANDLE hTransaction,
        int index, int failure_class);
PBROWSER_API int PBrowser_NavigationResourceCancel(HANDLE hTransaction,
        int index);
PBROWSER_API int PBrowser_NavigationResourceCancelAll(HANDLE hTransaction);
PBROWSER_API int PBrowser_NavigationResourceCommitGate(HANDLE hTransaction);
PBROWSER_API int PBrowser_NavigationResourceObserveFallbacks(
        HANDLE hTransaction);
PBROWSER_API int PBrowser_NavigationResourceGetStats(HANDLE hTransaction,
        PBrowserNavigationResourceStats *out_stats);

/* A commit snapshot composes two independently owned Browser handles without
 * merging their storage or transitioning either handle. Resource gate and
 * summary fields may be refreshed as in ResourceCommitGate; MarkCommitted
 * still rechecks the candidate before changing its terminal state. */
#define PBROWSER_NAVIGATION_COMMIT_READY 0
#define PBROWSER_NAVIGATION_COMMIT_RESOURCE_PENDING 1
#define PBROWSER_NAVIGATION_COMMIT_REQUIRED_FAILED 2
#define PBROWSER_NAVIGATION_COMMIT_RESOURCE_CANCELLED 3
#define PBROWSER_NAVIGATION_COMMIT_CANDIDATE_CANCEL_REQUESTED 4
#define PBROWSER_NAVIGATION_COMMIT_CANDIDATE_CANCELLED 5
#define PBROWSER_NAVIGATION_COMMIT_CANDIDATE_STALE 6
#define PBROWSER_NAVIGATION_COMMIT_CANDIDATE_FAILED 7
#define PBROWSER_NAVIGATION_COMMIT_CANDIDATE_COMMITTED 8

typedef struct PBrowserNavigationCommitInfo {
    unsigned long size;
    unsigned long current_generation;
    int decision;
    int candidate_result;
    int resource_gate;
    int candidate_current;
    int candidate_cancel_requested;
    int candidate_retired;
    int can_commit;
} PBrowserNavigationCommitInfo;

PBROWSER_API int PBrowser_NavigationCommitGetInfo(HANDLE hCandidate,
        HANDLE hTransaction, unsigned long current_generation,
        PBrowserNavigationCommitInfo *out_info);

/* Navigation candidate identity and lifecycle. A candidate is the Browser
 * owned admission record for one pending document. It carries only a
 * generation and lifecycle flags; it never references a window, worker,
 * network response or Core document. The host may use CanApply before every
 * UI-side continuation and must retire a superseded candidate before its
 * worker is allowed to deliver completion. Cancellation is safe to request
 * while a worker polls the handle; other state transitions and info snapshots
 * are synchronous and the caller must serialize access to one handle. */
#define PBROWSER_NAVIGATION_CANDIDATE_ACTIVE 0
#define PBROWSER_NAVIGATION_CANDIDATE_COMMITTED 1
#define PBROWSER_NAVIGATION_CANDIDATE_FAILED 2
#define PBROWSER_NAVIGATION_CANDIDATE_RETIRED 3

#define PBROWSER_NAVIGATION_CANDIDATE_RESULT_PENDING 0
#define PBROWSER_NAVIGATION_CANDIDATE_RESULT_COMMITTED 1
#define PBROWSER_NAVIGATION_CANDIDATE_RESULT_FAILED 2
#define PBROWSER_NAVIGATION_CANDIDATE_RESULT_CANCELLED 3
#define PBROWSER_NAVIGATION_CANDIDATE_RESULT_STALE 4

typedef struct PBrowserNavigationCandidateInfo {
    unsigned long size;
    unsigned long generation;
    int state;
    int cancel_requested;
    int retired;
    int current;
    int can_apply;
} PBrowserNavigationCandidateInfo;

/* A result snapshot is derived from the candidate state and the caller's
 * current generation; it never changes the handle. PENDING means an active
 * candidate still owns the current generation. COMMITTED and FAILED remain
 * terminal even when the caller later reports another current generation.
 * CANCELLED describes a retired candidate whose generation is still current;
 * STALE describes any non-committed candidate whose generation is no longer
 * current. A cancellation request by itself is not completion. */
typedef struct PBrowserNavigationCandidateResult {
    unsigned long size;
    unsigned long generation;
    unsigned long current_generation;
    int result;
    int state;
    int current;
    int cancel_requested;
    int retired;
} PBrowserNavigationCandidateResult;

PBROWSER_API HANDLE PBrowser_NavigationCandidateCreate(
        unsigned long generation);
PBROWSER_API void PBrowser_NavigationCandidateDestroy(HANDLE hCandidate);
PBROWSER_API int PBrowser_NavigationCandidateRequestCancel(
        HANDLE hCandidate);
PBROWSER_API int PBrowser_NavigationCandidateRetire(HANDLE hCandidate);
PBROWSER_API int PBrowser_NavigationCandidateCanApply(HANDLE hCandidate,
        unsigned long current_generation);
PBROWSER_API int PBrowser_NavigationCandidateMarkCommitted(
        HANDLE hCandidate, unsigned long current_generation);
PBROWSER_API int PBrowser_NavigationCandidateMarkFailed(HANDLE hCandidate);
PBROWSER_API int PBrowser_NavigationCandidateGetInfo(HANDLE hCandidate,
        unsigned long current_generation,
        PBrowserNavigationCandidateInfo *out_info);
PBROWSER_API int PBrowser_NavigationCandidateGetResult(HANDLE hCandidate,
        unsigned long current_generation,
        PBrowserNavigationCandidateResult *out_result);

/* A cleanup snapshot is taken before either independent handle is destroyed.
 * It copies the candidate result and full bounded resource observation into
 * caller-owned storage; it never retains a handle or changes candidate state.
 * Resource gate/summary fields may refresh as in ResourceGetStats.
 * can_release is nonzero only after candidate work and resource work are
 * settled, and a committed candidate has a READY resource gate. */
#define PBROWSER_NAVIGATION_CLEANUP_CANDIDATE_PENDING 0
#define PBROWSER_NAVIGATION_CLEANUP_RESOURCE_PENDING 1
#define PBROWSER_NAVIGATION_CLEANUP_COMMITTED 2
#define PBROWSER_NAVIGATION_CLEANUP_FAILED 3
#define PBROWSER_NAVIGATION_CLEANUP_CANCELLED 4
#define PBROWSER_NAVIGATION_CLEANUP_STALE 5
#define PBROWSER_NAVIGATION_CLEANUP_INCONSISTENT 6

typedef struct PBrowserNavigationCleanupInfo {
    unsigned long size;
    unsigned long current_generation;
    int decision;
    int can_release;
    PBrowserNavigationCandidateResult candidate;
    PBrowserNavigationResourceStats resource;
} PBrowserNavigationCleanupInfo;

PBROWSER_API int PBrowser_NavigationCleanupGetInfo(HANDLE hCandidate,
        HANDLE hTransaction, unsigned long current_generation,
        PBrowserNavigationCleanupInfo *out_info);

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

/* The host supplies the current focused element's non-empty UTF-8 DOM id.
 * The returned pointer is borrowed for the synchronous callback only;
 * NULL/empty means that no id-addressable element is focused and makes
 * document.activeElement fall back to document.body. The host remains the
 * owner of native focus and may derive this value from positron_core's
 * PCore_InteractionFocusElementId. Registering this table installs the
 * optional activeElement projection and enables the bounded :focus and
 * :focus-within selector pseudos; a session without this bridge does not pay
 * for or promise those properties. */
typedef const char *(*PBrowserScriptGetActiveElementFn)(void *pw);
typedef struct PBrowserScriptActiveElementCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetActiveElementFn get_active_element;
} PBrowserScriptActiveElementCallbacks;

/* The host supplies one current pointer-interaction element id for the
 * requested state. `state` is one of PBROWSER_SCRIPT_INTERACTION_ACTIVE or
 * PBROWSER_SCRIPT_INTERACTION_HOVER and is borrowed for this synchronous
 * call only; the returned pointer is borrowed for the same call.
 * NULL/empty means that the state is not id-addressable. Registering this
 * table enables the bounded Browser :active and :hover selector pseudos. A
 * session without the bridge fails
 * closed for those selectors. The callback does not dispatch events or
 * mutate Core state; hosts own the pointer transaction and repaint/restyle
 * policy. */
typedef const char *(*PBrowserScriptGetInteractionElementFn)(void *pw,
        const char *state);
typedef struct PBrowserScriptInteractionCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetInteractionElementFn get_interaction_element;
} PBrowserScriptInteractionCallbacks;

/* Additive interaction adapter for the privacy-sensitive link state. The
 * Browser layer supplies the element id and raw href only for an <a>/<area>
 * with an href attribute; both strings are borrowed for this synchronous
 * call. Return >0 for a host-approved visited link, 0 for unvisited, and a
 * negative value when the host cannot answer. The host owns history storage,
 * privacy policy and URL normalization. An absent adapter, invalid input or
 * a negative callback result is fail-closed. The original interaction
 * callback remains available for ABI-compatible hosts; this Ex table reuses
 * the same native function slot rather than adding another script global. */
typedef int (*PBrowserScriptGetLinkVisitedFn)(void *pw,
        const char *element_id, const char *href);
typedef struct PBrowserScriptInteractionCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptGetInteractionElementFn get_interaction_element;
    PBrowserScriptGetLinkVisitedFn get_link_visited;
} PBrowserScriptInteractionCallbacksEx;

/* Typed host adapter for script-visible HTMLElement.focus()/blur(). The
 * browser layer owns the methods and request validation; the host resolves
 * the id, updates native/Core focus and dispatches the corresponding focus
 * family through the host's existing focus event callbacks. focused is 1 for
 * focus() and 0 for blur(). The id is borrowed only for this synchronous
 * callback. A non-negative return means that the request was processed,
 * including a no-op for a disconnected, disabled or already-unfocused target;
 * a negative return reports an adapter failure. Registering this table
 * installs the optional methods after bootstrap. */
typedef struct PBrowserScriptFocusRequestInfo {
    unsigned long size;
    const char *element_id;
    int focused;
} PBrowserScriptFocusRequestInfo;
typedef int (*PBrowserScriptRequestFocusFn)(void *pw,
        const PBrowserScriptFocusRequestInfo *info);
typedef struct PBrowserScriptFocusRequestCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptRequestFocusFn request_focus;
} PBrowserScriptFocusRequestCallbacks;

/* Extended focus request adapter. This additive form preserves the original
 * callback ABI while allowing focus(options) to coordinate page-level reveal.
 * prevent_scroll requests that the host leave the page viewport unchanged. It
 * is set for focus({preventScroll:true}) and also when Browser has identified
 * a retained overflow ancestor and will perform its bounded nested reveal
 * after the callback; hosts must honor it in both cases. The host may write
 * the actually applied CSS page scroll into out_result; coordinates are
 * non-negative and scroll_changed is nonzero only when the viewport moved.
 * The result is copied synchronously and is never retained. Complete scroll
 * trees, scroll chaining, smooth scrolling and scroll-margin remain outside
 * this boundary. */
typedef struct PBrowserScriptFocusRequestInfoEx {
    unsigned long size;
    const char *element_id;
    int focused;
    int prevent_scroll;
} PBrowserScriptFocusRequestInfoEx;
typedef struct PBrowserScriptFocusRequestResult {
    unsigned long size;
    int scroll_changed;
    int scroll_x;
    int scroll_y;
} PBrowserScriptFocusRequestResult;
typedef int (*PBrowserScriptRequestFocusFnEx)(void *pw,
        const PBrowserScriptFocusRequestInfoEx *info,
        PBrowserScriptFocusRequestResult *out_result);
typedef struct PBrowserScriptFocusRequestCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptRequestFocusFnEx request_focus;
} PBrowserScriptFocusRequestCallbacksEx;

/* Typed host adapter for the bounded, ID-addressable DOM relationship
 * boundary. Value relationships (parent/sibling/child-at/tag/form-owner)
 * use the same UTF-8 size-probe contract as PBrowserScriptGetTextFn. Count
 * and child-node-type relationships leave out_value/out_bytes unused and
 * write out_number; attribute name/value and child-node fields use the
 * bounded UTF-8 probe/truncation contract. The CHILD_NODE_* relations expose
 * a direct childNodes snapshot, including text/comment nodes; an element id
 * is returned only when that child has a non-empty id. FORM_OWNER and
 * FORM_CONTROL_* use the Core form-owner rules: a supported control with a
 * present `form` attribute resolves the first form with that id, while an
 * empty or invalid target has no owner and does not fall back to an ancestor.
 * Form-control relations enumerate supported controls in document order,
 * including explicitly associated controls outside the form. The callback
 * returns 0 when found, 2 when the relationship is absent or outside the
 * bounded wrapper tree, and a negative value on adapter failure. */
typedef int (*PBrowserScriptGetNodeRelationFn)(void *pw, const char *id,
        unsigned int relation, unsigned int index, char *out_value,
        int out_capacity, int *out_bytes, int *out_number);
typedef struct PBrowserScriptDomRelationCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetNodeRelationFn get_relation;
} PBrowserScriptDomRelationCallbacks;

/* Relationship constants mirror positron_core.h without requiring public
 * browser consumers to include the core header. The three structural tokens
 * are shared with positron_core.h so a host adapter can expose document root
 * wrappers without inventing a second DOM identity scheme. The four
 * LAYOUT_RECT_* relations are numeric document-CSS-pixel geometry components;
 * the Browser wrapper subtracts the current page scroll before exposing them
 * through Element.getBoundingClientRect() or the bounded Element.getClientRects()
 * collection. The latter is a fresh array-like snapshot with at most
 * PBROWSER_SCRIPT_LAYOUT_FRAGMENT_MAX rectangles; it does not expose the Core
 * box tree. LAYOUT_OFFSET_*, LAYOUT_CLIENT_* and LAYOUT_SCROLL_* relations are
 * integer CSS-pixel width/height snapshots for supported block, replaced,
 * table and flex boxes. They are read-only and unavailable until layout; the
 * Browser wrapper maps unavailable metrics to zero. The two scroll-offset
 * relations are live retained CSS overflow positions. The
 * FORM_CONTROL_DISABLED relation is a UTF-8 string "0"/"1" effective state
 * for input, button, select, textarea, option and optgroup elements; it lets
 * the Browser selector layer share Core's fieldset and optgroup inheritance
 * rules while remaining compatible with older hosts that do not provide it. */
#define PBROWSER_SCRIPT_DOCUMENT_ELEMENT_TOKEN "__positron_document_element__"
#define PBROWSER_SCRIPT_DOCUMENT_HEAD_TOKEN    "__positron_document_head__"
#define PBROWSER_SCRIPT_DOCUMENT_BODY_TOKEN    "__positron_document_body__"

#define PBROWSER_SCRIPT_NODE_RELATION_PARENT_ELEMENT       1u
#define PBROWSER_SCRIPT_NODE_RELATION_FIRST_CHILD          2u
#define PBROWSER_SCRIPT_NODE_RELATION_LAST_CHILD           3u
#define PBROWSER_SCRIPT_NODE_RELATION_PREVIOUS_SIBLING     4u
#define PBROWSER_SCRIPT_NODE_RELATION_NEXT_SIBLING         5u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_COUNT          6u
#define PBROWSER_SCRIPT_NODE_RELATION_TAG_NAME             7u
#define PBROWSER_SCRIPT_NODE_RELATION_FORM_OWNER           8u
#define PBROWSER_SCRIPT_NODE_RELATION_FORM_CONTROL_COUNT   9u
#define PBROWSER_SCRIPT_NODE_RELATION_FORM_CONTROL_AT     10u
#define PBROWSER_SCRIPT_NODE_RELATION_ATTRIBUTE_COUNT     11u
#define PBROWSER_SCRIPT_NODE_RELATION_ATTRIBUTE_NAME_AT   12u
#define PBROWSER_SCRIPT_NODE_RELATION_ATTRIBUTE_VALUE_AT  13u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_COUNT    14u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_TYPE_AT  15u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_NAME_AT  16u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_VALUE_AT 17u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_ID_AT    18u
#define PBROWSER_SCRIPT_NODE_RELATION_CHILD_NODE_TEXT_AT  19u
#define PBROWSER_SCRIPT_NODE_RELATION_LABEL_CONTROL       20u
#define PBROWSER_SCRIPT_NODE_RELATION_CONTROL_LABEL_COUNT 21u
#define PBROWSER_SCRIPT_NODE_RELATION_CONTROL_LABEL_AT    22u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_RECT_X       23u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_RECT_Y       24u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_RECT_WIDTH   25u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_RECT_HEIGHT  26u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_FRAGMENT_COUNT 27u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_FRAGMENT_X_AT   28u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_FRAGMENT_Y_AT   29u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_FRAGMENT_WIDTH_AT 30u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_FRAGMENT_HEIGHT_AT 31u
#define PBROWSER_SCRIPT_LAYOUT_FRAGMENT_MAX                 16u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_OFFSET_WIDTH   32u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_OFFSET_HEIGHT  33u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_CLIENT_WIDTH   34u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_CLIENT_HEIGHT  35u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_SCROLL_WIDTH   36u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_SCROLL_HEIGHT  37u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_SCROLL_LEFT    38u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_SCROLL_TOP     39u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_SCROLLABLE_X   40u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_SCROLLABLE_Y   41u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_CLIENT_X       42u
#define PBROWSER_SCRIPT_NODE_RELATION_LAYOUT_CLIENT_Y       43u
#define PBROWSER_SCRIPT_NODE_RELATION_FORM_CONTROL_DISABLED 44u

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

/* Typed host adapter for the bounded single-element contenteditable
 * boundary. The browser DLL owns the JSON bridge and `isContentEditable`
 * property; the host reads effective state and performs a plain-text Core
 * mutation. Neither callback dispatches input events: a host must retain the
 * existing beforeinput cancellation -> mutation -> input ordering. The
 * getter returns >0 for an editable element, 0 for a non-editable element and
 * <0 on adapter failure. The setter returns >0 on success, 0 when the target
 * is unavailable/not editable and <0 on adapter failure. */
typedef int (*PBrowserScriptGetContentEditableFn)(void *pw, const char *id,
        int *out_editable);
typedef int (*PBrowserScriptSetContentEditableTextFn)(void *pw,
        const char *id, const char *text);
typedef struct PBrowserScriptContentEditableCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetContentEditableFn get_editable;
    PBrowserScriptSetContentEditableTextFn set_text;
} PBrowserScriptContentEditableCallbacks;

/* Optional native selection adapter for the bounded contenteditable
 * surface. Offsets use JavaScript UTF-16 code units (the same units exposed
 * by selectionStart/selectionEnd), and direction is one of the three
 * PBROWSER_SCRIPT_CONTENT_SELECTION_* constants below. The getter returns
 * >0 when the addressed editing host currently has a native selection, 0
 * when it is not materialized by the host and <0 on adapter failure. The
 * setter returns >0 when the host applied the requested range, 0 when the
 * target is unavailable and <0 on adapter failure. Browser keeps a small
 * script-side fallback when no native surface is available. */
#define PBROWSER_SCRIPT_CONTENT_SELECTION_NONE      0
#define PBROWSER_SCRIPT_CONTENT_SELECTION_FORWARD   1
#define PBROWSER_SCRIPT_CONTENT_SELECTION_BACKWARD  2
typedef int (*PBrowserScriptGetContentEditableSelectionFn)(void *pw,
        const char *id, int *out_start, int *out_end, int *out_direction);
typedef int (*PBrowserScriptSetContentEditableSelectionFn)(void *pw,
        const char *id, int start, int end, int direction);
typedef struct PBrowserScriptContentEditableSelectionCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetContentEditableSelectionFn get_selection;
    PBrowserScriptSetContentEditableSelectionFn set_selection;
} PBrowserScriptContentEditableSelectionCallbacks;

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

/* Typed host adapters for product-owned checked callbacks. The browser DLL
 * parses and encodes the JSON checked requests; the host only reads or
 * updates its core document through these callbacks. get_checked returns zero
 * on success or a negative value on error. set_checked returns >0 when the
 * target was updated, 0 when the target or operation was unavailable and <0
 * on an adapter error. */
typedef int (*PBrowserScriptGetCheckedFn)(void *pw, const char *id,
        int *out_checked);
typedef int (*PBrowserScriptSetCheckedFn)(void *pw, const char *id,
        int checked);
typedef struct PBrowserScriptDomCheckedCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetCheckedFn get_checked;
    PBrowserScriptSetCheckedFn set_checked;
} PBrowserScriptDomCheckedCallbacks;

/* Typed host adapters for the product-owned form-property callback. The
 * browser DLL parses and encodes the single __pcoreFormProperty JSON entry;
 * the host only supplies the six core document operations. String getters
 * use the same size-probe contract as PBrowserScriptGetTextFn. All getters
 * return zero on success or a negative value on error. Setters return >0 on
 * success, 0 when the target or operation is unavailable and <0 on error. */
typedef int (*PBrowserScriptGetSelectedIndexFn)(void *pw, const char *id,
        int *out_index);
typedef int (*PBrowserScriptSetSelectedIndexFn)(void *pw, const char *id,
        int index);
typedef struct PBrowserScriptFormCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetValueFn get_default_value;
    PBrowserScriptSetValueFn set_default_value;
    PBrowserScriptGetCheckedFn get_default_checked;
    PBrowserScriptSetCheckedFn set_default_checked;
    PBrowserScriptGetSelectedIndexFn get_selected_index;
    PBrowserScriptSetSelectedIndexFn set_selected_index;
} PBrowserScriptFormCallbacks;

/* Typed adapter for the detached `new FormData(form)` snapshot without an
 * explicit submitter. The browser layer owns the JavaScript FormData object
 * and JSON bridge; the host only enumerates Core's already-filtered successful
 * controls. get_count and get_entry return zero on success, a positive value
 * when the form/entry is unavailable and a negative value on adapter failure.
 * `form_id` and all buffers are borrowed for the synchronous callback. Lengths
 * exclude the trailing NUL; capacities include it. A callback must fail closed
 * when a value does not fit its supplied capacity rather than truncating it.
 *
 * The bridge is intentionally bounded by the fixed native-script result
 * buffer. It supports at most PBROWSER_SCRIPT_FORM_DATA_MAX_ENTRIES entries,
 * and each UTF-8 field is limited by the corresponding *_MAX_BYTES constant.
 * kind 1 is a string value; kind 2 is a file whose filename/type are exposed
 * and whose local picker path is never returned to script. The value buffer
 * is empty for kind 2. No validation, submit/reset event, default action,
 * navigation or live linkage to the controls occurs while taking the snapshot.
 * After a successful Browser-side construction, the script layer dispatches
 * one synchronous, non-bubbling, non-cancelable `formdata` event whose
 * FormDataEvent.formData points at the returned object; this event is not
 * generated by the native callback. */
#define PBROWSER_SCRIPT_FORM_DATA_STRING 1
#define PBROWSER_SCRIPT_FORM_DATA_FILE   2
#define PBROWSER_SCRIPT_FORM_DATA_MAX_ENTRIES 64
#define PBROWSER_SCRIPT_FORM_DATA_NAME_MAX_BYTES 64
#define PBROWSER_SCRIPT_FORM_DATA_VALUE_MAX_BYTES 128
#define PBROWSER_SCRIPT_FORM_DATA_FILENAME_MAX_BYTES 64
#define PBROWSER_SCRIPT_FORM_DATA_TYPE_MAX_BYTES 64

typedef struct PBrowserScriptFormDataEntryInfo {
    unsigned long size;
    int kind;
    char *name;
    int name_capacity;
    int name_bytes;
    char *value;
    int value_capacity;
    int value_bytes;
    char *filename;
    int filename_capacity;
    int filename_bytes;
    char *type;
    int type_capacity;
    int type_bytes;
} PBrowserScriptFormDataEntryInfo;
typedef int (*PBrowserScriptGetFormDataCountFn)(void *pw,
        const char *form_id, int *out_count);
typedef int (*PBrowserScriptGetFormDataEntryFn)(void *pw,
        const char *form_id, unsigned int entry_index,
        PBrowserScriptFormDataEntryInfo *out_info);
typedef struct PBrowserScriptFormDataCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetFormDataCountFn get_count;
    PBrowserScriptGetFormDataEntryFn get_entry;
} PBrowserScriptFormDataCallbacks;

/* Additive adapter for `new FormData(form, submitter)`. It has the same
 * ownership and bounded-field rules as PBrowserScriptFormDataCallbacks, but
 * receives the optional submitter id so Core can validate its form owner and
 * include that successful submit control. A NULL or empty submitter id means
 * no explicit submitter. The Ex table preserves the original no-submitter ABI
 * for consumers that do not need the second constructor argument. */
typedef int (*PBrowserScriptGetFormDataCountFnEx)(void *pw,
        const char *form_id, const char *submitter_id, int *out_count);
typedef int (*PBrowserScriptGetFormDataEntryFnEx)(void *pw,
        const char *form_id, const char *submitter_id,
        unsigned int entry_index, PBrowserScriptFormDataEntryInfo *out_info);
typedef struct PBrowserScriptFormDataCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptGetFormDataCountFnEx get_count;
    PBrowserScriptGetFormDataEntryFnEx get_entry;
} PBrowserScriptFormDataCallbacksEx;

/* Additive adapter for the script-facing HTMLFormElement.reset() default
 * action. The browser layer dispatches a cancelable reset event through the
 * by-id form-event adapter before calling reset_form; the host only applies
 * the Core state-only reset for the supplied UTF-8 form id and re-layouts its
 * active page afterward. reset_form returns >0 when the form state was
 * restored, 0 when the target is absent or unavailable, and <0 on adapter
 * failure. The id is borrowed for the synchronous callback. Register this
 * table after PBrowserScriptFormCallbacks; the original form callback ABI is
 * unchanged. */
typedef int (*PBrowserScriptResetFormFn)(void *pw, const char *form_id);
typedef struct PBrowserScriptFormResetCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptResetFormFn reset_form;
} PBrowserScriptFormResetCallbacks;

/* Additive adapter for script-facing HTMLFormElement.requestSubmit(). The
 * browser layer validates the request, dispatches a cancelable bubbling
 * submit event by form id, and only then invokes submit_form for the host's
 * default action. validate_submit writes 1 when the request may submit, 0
 * when constraint validation blocks it, or -1 when form/submitter resolution
 * failed; the callback itself returns <0 only for an adapter failure.
 * submit_form returns >0 when the host accepted the Core submission, 0 for a
 * safe no-op and <0 for an adapter failure. Both ids are borrowed for the
 * synchronous callback and are UTF-8; submitter_id is empty when omitted.
 * Register this table after PBrowserScriptFormCallbacks; the original form
 * and reset callback ABIs remain unchanged. */
typedef struct PBrowserScriptFormSubmitInfo {
    unsigned long size;
    const char *form_id;
    const char *submitter_id;
} PBrowserScriptFormSubmitInfo;
typedef int (*PBrowserScriptValidateFormSubmitFn)(void *pw,
        const PBrowserScriptFormSubmitInfo *info, int *out_valid);
typedef int (*PBrowserScriptSubmitFormFn)(void *pw,
        const PBrowserScriptFormSubmitInfo *info);
typedef struct PBrowserScriptFormSubmitCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptValidateFormSubmitFn validate_submit;
    PBrowserScriptSubmitFormFn submit_form;
} PBrowserScriptFormSubmitCallbacks;

/* Optional adapter for HTMLFormElement.submit(). Unlike requestSubmit(), the
 * direct method skips validation, submit-event dispatch and submitter
 * selection. The browser still owns the script method and invokes this host
 * callback only for a resolved form id; the host applies the Core
 * no-validation default action and its navigation/dialog policy. The callback
 * returns >0 when the action was accepted, 0 for a safe no-op and <0 for an
 * adapter failure. The id is borrowed for the synchronous call and
 * submitter_id in the supplied info is always an empty UTF-8 string. Register
 * this table after PBrowserScriptFormCallbacks; the existing form-submit ABI
 * remains unchanged. */
typedef int (*PBrowserScriptFormSubmitDirectFn)(void *pw,
        const PBrowserScriptFormSubmitInfo *info);
typedef struct PBrowserScriptFormSubmitDirectCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptFormSubmitDirectFn submit_form;
} PBrowserScriptFormSubmitDirectCallbacks;

/* Constraint-validation bits returned by the product-owned validation
 * bridge. They intentionally mirror the public positron_core flags without
 * making this DLL depend on positron_core headers. */
#define PBROWSER_SCRIPT_VALIDITY_VALUE_MISSING 0x0001u
#define PBROWSER_SCRIPT_VALIDITY_TOO_LONG 0x0002u
#define PBROWSER_SCRIPT_VALIDITY_TOO_SHORT 0x0004u
#define PBROWSER_SCRIPT_VALIDITY_PATTERN_MISMATCH 0x0008u
#define PBROWSER_SCRIPT_VALIDITY_BAD_INPUT 0x0010u
#define PBROWSER_SCRIPT_VALIDITY_RANGE_UNDERFLOW 0x0020u
#define PBROWSER_SCRIPT_VALIDITY_RANGE_OVERFLOW 0x0040u
#define PBROWSER_SCRIPT_VALIDITY_STEP_MISMATCH 0x0080u
#define PBROWSER_SCRIPT_VALIDITY_TYPE_MISMATCH 0x0100u
#define PBROWSER_SCRIPT_VALIDITY_CUSTOM_ERROR 0x0200u

/* Typed host adapter for the product-owned constraint-validation query. The
 * browser DLL owns JSON encoding and the script-facing checkValidity(),
 * willValidate, validity and bounded :valid/:invalid/:in-range/:out-of-range
 * selector properties; the
 * host supplies a synchronous state lookup by UTF-8 DOM id. A host may return
 * an aggregate form result
 * for a form id; such a result uses valid for the form query and keeps
 * will_validate=0 and flags=0. Non-validation candidates should return
 * valid=1, will_validate=0 and flags=0. The callback returns zero on success
 * and a negative value on adapter failure. */
typedef struct PBrowserScriptValidationInfo {
    unsigned long size;
    int valid;
    int will_validate;
    unsigned int flags;
} PBrowserScriptValidationInfo;
typedef int (*PBrowserScriptGetValidationFn)(void *pw, const char *id,
        PBrowserScriptValidationInfo *out_info);
typedef struct PBrowserScriptValidationCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetValidationFn get_validation;
} PBrowserScriptValidationCallbacks;

/* Typed host adapter for the script-facing reportValidity() operation. The
 * browser DLL owns JSON dispatch and the boolean method result; the host
 * validates a form or control by UTF-8 DOM id and synchronously dispatches the
 * trusted invalid events that its core can address. out_valid is the result
 * before any default feedback; preventDefault() does not make an invalid
 * form/control valid. No native validation UI, focus/scroll or submission is
 * implied by this callback. */
typedef int (*PBrowserScriptReportValidityFn)(void *pw, const char *id,
        int *out_valid);
typedef struct PBrowserScriptReportValidityCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptReportValidityFn report_validity;
} PBrowserScriptReportValidityCallbacks;

/* Typed host adapter for application-owned custom validity messages and the
 * script-facing validationMessage property. The browser DLL owns JSON
 * dispatch and setCustomValidity(); the host supplies a UTF-8 getter/setter
 * by DOM id. The getter may return a deterministic built-in English fallback
 * when no custom message exists and the control is invalid, while preserving
 * the size-probe contract used by PBrowserScriptGetValueFn. The setter
 * returns >0 on success, 0 when the target is unsupported and <0 on an
 * adapter error. No localization or native validation UI is implied. */
typedef struct PBrowserScriptCustomValidityCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetValueFn get_message;
    PBrowserScriptSetValueFn set_message;
} PBrowserScriptCustomValidityCallbacks;

/* Typed host adapter for product-owned native text-input, file-input, and
 * checkbox/radio input events. The browser layer owns the input-event
 * contract and dispatch entry point; the host supplies core
 * hit-testing/propagation for the document
 * coordinates. x/y are borrowed document CSS pixels. event_type is
 * non-empty, input_type may be empty for composition events, and all strings
 * are borrowed only for the synchronous callback. A file input notification
 * uses input_type "insertFromFile", an empty data string, and
 * is_composing == 0. A checkbox/radio input notification is non-cancelable,
 * uses empty input_type/data, and is_composing == 0. The adapter returns
 * zero when core dispatch was attempted and writes 1 when the native default
 * is allowed or 0 when a cancelable listener prevented it; a negative return
 * reports an adapter failure. */
typedef struct PBrowserScriptInputEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
    const char *input_type;
    const char *data;
    int is_composing;
} PBrowserScriptInputEventInfo;
typedef int (*PBrowserScriptDispatchInputFn)(void *pw,
        const PBrowserScriptInputEventInfo *info, int *out_default_allowed);
typedef struct PBrowserScriptInputCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchInputFn dispatch_input;
} PBrowserScriptInputCallbacks;

/* Typed host adapter for product-owned native keyboard events. The browser
 * layer owns the keyboard-event contract and dispatch entry point; the host
 * supplies core hit-testing/propagation for the document coordinates. x/y
 * are borrowed document CSS pixels. event_type and key are non-empty UTF-8
 * strings borrowed only for the synchronous callback. The adapter returns
 * zero when core dispatch was attempted and writes 1 when the native default
 * is allowed or 0 when a cancelable listener prevented it; a negative return
 * reports an adapter failure. */
typedef struct PBrowserScriptKeyEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
    const char *key;
    unsigned int key_code;
    unsigned int char_code;
    int repeat;
    int shift;
    int ctrl;
    int alt;
    int is_composing;
} PBrowserScriptKeyEventInfo;
typedef int (*PBrowserScriptDispatchKeyFn)(void *pw,
        const PBrowserScriptKeyEventInfo *info, int *out_default_allowed);
typedef struct PBrowserScriptKeyCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchKeyFn dispatch_key;
} PBrowserScriptKeyCallbacks;

/* Typed host adapter for product-owned native focus-family events. The
 * browser layer owns the focus-event contract and dispatch entry point; the
 * host supplies core hit-testing/propagation for the document coordinates.
 * x/y are borrowed document CSS pixels. event_type must be one of focus,
 * blur, focusin or focusout and is borrowed only for the synchronous
 * callback. The adapter returns zero when core dispatch was attempted and a
 * negative return reports an adapter failure. */
typedef struct PBrowserScriptFocusEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptFocusEventInfo;
typedef int (*PBrowserScriptDispatchFocusFn)(void *pw,
        const PBrowserScriptFocusEventInfo *info);
typedef struct PBrowserScriptFocusCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchFocusFn dispatch_focus;
} PBrowserScriptFocusCallbacks;

/* Typed host adapter for product-owned native EDIT change events. The
 * browser layer owns the EDIT event contract and dispatch entry point; the
 * host supplies core hit-testing/propagation for the document coordinates.
 * x/y are borrowed document CSS pixels. event_type must be "change" and is
 * borrowed only for the synchronous callback. The adapter returns zero when
 * core dispatch was attempted and a negative return reports an adapter
 * failure. */
typedef struct PBrowserScriptEditEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptEditEventInfo;
typedef int (*PBrowserScriptDispatchEditFn)(void *pw,
        const PBrowserScriptEditEventInfo *info);
typedef struct PBrowserScriptEditCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchEditFn dispatch_edit;
} PBrowserScriptEditCallbacks;

/* Product-owned native EDIT input transaction. The target token is an
 * application-owned, non-zero value that remains stable while a native EDIT
 * is attached to this script session; it is not a DOM pointer or a window
 * handle. x/y are document CSS pixels supplied by the host adapter. For
 * beforeinput, input_type and data are required (they may be empty for a
 * composition update); for the native-commit operation both may be NULL to
 * consume the last accepted beforeinput, or both may be supplied as an
 * explicit fallback. Strings are borrowed for the synchronous call only. */
typedef struct PBrowserScriptNativeEditInputInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    const char *input_type;
    const char *data;
    int cancelable;
    int is_composing;
} PBrowserScriptNativeEditInputInfo;
#define PBROWSER_SCRIPT_NATIVE_EDIT_MAX_TARGETS 16
#define PBROWSER_SCRIPT_NATIVE_EDIT_MAX_TEXT_BYTES 256

/* Product-owned native EDIT composition lifecycle. The host supplies the
 * platform IME/SIP phase and borrowed UTF-8 preedit/result data; the browser
 * layer owns the bounded active-composition state and dispatches the standard
 * compositionstart -> beforeinput/compositionupdate -> compositionend
 * ordering through the registered input callback. START is cancelable and
 * writes whether the host may keep the platform composition active. UPDATE
 * and END are non-cancelable; END may pass NULL to use the last accepted
 * update. The browser layer does not own WM_IME, SIP windows, or text
 * mutation. Strings are borrowed for the synchronous call only. */
#define PBROWSER_SCRIPT_NATIVE_EDIT_COMPOSITION_START 1
#define PBROWSER_SCRIPT_NATIVE_EDIT_COMPOSITION_UPDATE 2
#define PBROWSER_SCRIPT_NATIVE_EDIT_COMPOSITION_END 3
typedef struct PBrowserScriptNativeEditCompositionInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int phase;
    const char *data;
} PBrowserScriptNativeEditCompositionInfo;

/* A complete native EDIT IME result. The browser layer treats this as the
 * final accepted composition update: it dispatches the non-cancelable
 * beforeinput(insertCompositionText) and compositionupdate pair, records
 * the bounded pending input metadata and retains the result for composition
 * end. The host must still replace native text and then call the existing
 * native EDIT input and composition-end entries. The result string is
 * borrowed for the synchronous call only. */
typedef struct PBrowserScriptNativeEditResultInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    const char *data;
} PBrowserScriptNativeEditResultInfo;

/* Additive native EDIT adapter. The browser layer owns the bounded pending
 * beforeinput state, composition phase/preedit state, native-commit to input
 * transition, dirty tracking and blur/change ordering. The host supplies only
 * the existing core event propagation callbacks. This contract intentionally
 * does not own WM EDIT/WM_IME, IME/SIP windows, focus windows or text
 * mutation. */
typedef struct PBrowserScriptNativeEditCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchInputFn dispatch_input;
    PBrowserScriptDispatchEditFn dispatch_change;
} PBrowserScriptNativeEditCallbacksEx;

/* Typed host adapter for product-owned native SELECT input/change events,
 * checkbox/radio change events and file-input input/change events. The
 * browser layer owns this selection-like event contract and dispatch entry
 * point; the host supplies core hit-testing/propagation for document
 * coordinates. x/y are borrowed document CSS pixels. event_type must be
 * "input" or "change" and is borrowed only for the synchronous callback.
 * The adapter returns zero when core dispatch was attempted and a negative
 * return reports an adapter failure. */
typedef struct PBrowserScriptSelectEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptSelectEventInfo;
typedef int (*PBrowserScriptDispatchSelectFn)(void *pw,
        const PBrowserScriptSelectEventInfo *info);
typedef struct PBrowserScriptSelectCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchSelectFn dispatch_select;
} PBrowserScriptSelectCallbacks;

/* Native checkbox/radio activation phases. The browser layer owns the
 * trusted click cancellation and the post-commit input -> change ordering;
 * the host owns hit-testing, Core's checked-state mutation and repaint. A
 * CLICK starts a bounded transaction only when the click is not prevented.
 * COMMIT is sent after Core reports the resulting state; CANCEL is idempotent
 * and is used when the host cannot commit the default action. */
#define PBROWSER_SCRIPT_NATIVE_TOGGLE_CLICK  1
#define PBROWSER_SCRIPT_NATIVE_TOGGLE_COMMIT 2
#define PBROWSER_SCRIPT_NATIVE_TOGGLE_CANCEL 3
#define PBROWSER_SCRIPT_NATIVE_TOGGLE_CHECKBOX 1
#define PBROWSER_SCRIPT_NATIVE_TOGGLE_RADIO    2
#define PBROWSER_SCRIPT_NATIVE_TOGGLE_MAX_TARGETS 16
typedef struct PBrowserScriptNativeToggleInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int phase;
    int kind;
    int disabled;
    int selected_before;
    int selected_after;
} PBrowserScriptNativeToggleInfo;

/* Trusted native button activation phases. The browser layer owns the
 * cancelable click and the conditional submit/reset event ordering; an
 * ordinary button emits only click and then consumes the accepted default.
 * The host owns hit-testing, Core validation, the default submission/reset
 * mutation, navigation and repaint. CLICK starts a bounded transaction only
 * when the target is enabled and click is not prevented. COMMIT is sent after
 * the host has queried Core validation; CANCEL is idempotent. A disabled
 * target never dispatches click. validation_valid is consumed on COMMIT, and
 * is ignored on CLICK/CANCEL. target_token is a stable, non-zero host token.
 */
#define PBROWSER_SCRIPT_NATIVE_BUTTON_CLICK  1
#define PBROWSER_SCRIPT_NATIVE_BUTTON_COMMIT 2
#define PBROWSER_SCRIPT_NATIVE_BUTTON_CANCEL 3
#define PBROWSER_SCRIPT_NATIVE_BUTTON_SUBMIT 1
#define PBROWSER_SCRIPT_NATIVE_BUTTON_RESET  2
#define PBROWSER_SCRIPT_NATIVE_BUTTON_BUTTON 3
#define PBROWSER_SCRIPT_NATIVE_BUTTON_MAX_TARGETS 16
typedef struct PBrowserScriptNativeButtonInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int phase;
    int kind;
    int disabled;
    int validation_valid;
} PBrowserScriptNativeButtonInfo;

/* Native file-input selection transaction phases. The browser layer owns the
 * bounded begin/commit/cancel state and the non-cancelable input -> change
 * ordering; the host still owns the system picker, file-system access and
 * Core's selected path mutation. BEGIN is sent before opening the picker,
 * COMMIT only after Core accepted a new path, and CANCEL on user cancel,
 * picker failure or a stale document. The browser reuses the registered
 * input/select callbacks, so no file path or picker handle crosses this ABI. */
#define PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_BEGIN  1
#define PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_COMMIT 2
#define PBROWSER_SCRIPT_NATIVE_FILE_SELECTION_CANCEL 3
#define PBROWSER_SCRIPT_NATIVE_FILE_MAX_TARGETS      16
typedef struct PBrowserScriptNativeFileSelectionInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int phase;
} PBrowserScriptNativeFileSelectionInfo;

/* Native file-picker request arbitration phases. The browser layer owns one
 * bounded pending/active request per script session so repeated
 * file.click() calls coalesce and stale host messages cannot reopen a picker.
 * The host still owns PostMessage, the system picker, file-system access and
 * the selected path. REQUEST is issued before posting a host message, OPEN
 * immediately before entering the modal picker, and CLOSE/CANCEL release the
 * request after the host returns or abandons it. */
#define PBROWSER_SCRIPT_NATIVE_FILE_PICKER_REQUEST 1
#define PBROWSER_SCRIPT_NATIVE_FILE_PICKER_OPEN    2
#define PBROWSER_SCRIPT_NATIVE_FILE_PICKER_CLOSE   3
#define PBROWSER_SCRIPT_NATIVE_FILE_PICKER_CANCEL  4
typedef struct PBrowserScriptNativeFilePickerInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int phase;
} PBrowserScriptNativeFilePickerInfo;

/* Product-owned native SELECT commit. The host calls this only after its
 * native WM control and core selection mutation have succeeded. The browser
 * layer emits the non-cancelable input -> change pair in that order and keeps
 * bounded target-shape state until reset or session destruction. target_token
 * is host-owned, non-zero and stable for the lifetime of one native SELECT;
 * it is not a window handle or a DOM pointer. selected_index is -1 when the
 * host has no single selected option; selected_count is always non-negative.
 * Strings are not used, so the contract is safe for bounded C89 consumers. */
typedef struct PBrowserScriptNativeSelectCommitInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int multiple;
    int selected_index;
    int selected_count;
} PBrowserScriptNativeSelectCommitInfo;
#define PBROWSER_SCRIPT_NATIVE_SELECT_MAX_TARGETS 16

typedef struct PBrowserScriptNativeSelectEventInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
    int multiple;
    int selected_index;
    int selected_count;
} PBrowserScriptNativeSelectEventInfo;
typedef int (*PBrowserScriptDispatchNativeSelectFn)(void *pw,
        const PBrowserScriptNativeSelectEventInfo *info);

/* Product-owned native SELECT focus transition. The browser layer owns the
 * focus-family pair and keeps bounded per-target state so repeated native
 * focus notifications are idempotent. focused=1 emits focus then focusin;
 * focused=0 emits blur then focusout. The existing generic focus callback
 * registered for this session receives the pair. target_token is host-owned,
 * non-zero and stable for the lifetime of one native SELECT; x/y are borrowed
 * document CSS pixels. */
typedef struct PBrowserScriptNativeSelectFocusInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int focused;
} PBrowserScriptNativeSelectFocusInfo;

/* Product-owned native SELECT keyboard event. The browser layer validates
 * the stable target token, supplies the trusted key-event shape to the
 * generic key adapter and returns whether the WM control may process its
 * native default action. event_type is only "keydown" or "keyup". Strings
 * are borrowed for the synchronous callback. */
typedef struct PBrowserScriptNativeSelectKeyInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    const char *event_type;
    const char *key;
    unsigned int key_code;
    unsigned int char_code;
    int repeat;
    int shift;
    int ctrl;
    int alt;
    int is_composing;
} PBrowserScriptNativeSelectKeyInfo;

/* Native SELECT dropdown transaction phases. The browser layer owns the
 * bounded candidate state and whether an accepted close should commit; the
 * host remains responsible for the WM dropdown and the eventual Core
 * selection mutation. */
#define PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_BEGIN       1
#define PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_CANDIDATE   2
#define PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_END_OK      3
#define PBROWSER_SCRIPT_NATIVE_SELECT_INTERACTION_END_CANCEL  4

typedef struct PBrowserScriptNativeSelectInteractionInfo {
    unsigned long size;
    unsigned long target_token;
    int x;
    int y;
    int multiple;
    int selected_index;
    int selected_count;
    int phase;
} PBrowserScriptNativeSelectInteractionInfo;

/* Additive native SELECT adapter. The browser layer owns the commit event
 * ordering, keyboard default-allowed policy and bounded target contract; the
 * host supplies only core event propagation and keeps WM SELECT, selection
 * mutation and repaint side effects. */
typedef struct PBrowserScriptNativeSelectCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchNativeSelectFn dispatch_select;
} PBrowserScriptNativeSelectCallbacksEx;

/* Typed host adapter for product-owned native click events. The browser
 * layer owns the click-event contract and dispatch entry point; the host
 * supplies core hit-testing/propagation for the document coordinates. x/y
 * are borrowed document CSS pixels. event_type must be "click" and is
 * borrowed only for the synchronous callback. The adapter returns zero when
 * core dispatch was attempted and writes 1 when the native default is
 * allowed or 0 when a cancelable listener prevented it; a negative return
 * reports an adapter failure. */
typedef struct PBrowserScriptClickEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptClickEventInfo;
typedef int (*PBrowserScriptDispatchClickFn)(void *pw,
        const PBrowserScriptClickEventInfo *info, int *out_default_allowed);
typedef struct PBrowserScriptClickCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchClickFn dispatch_click;
} PBrowserScriptClickCallbacks;

/* Product-owned trusted anchor activation. The host supplies the href from
 * its hit-tested document link and keeps the network/window side effects.
 * The browser layer dispatches the cancelable click through the registered
 * click adapter; when it is not prevented, it forwards an ASSIGN navigation
 * through the registered navigation adapter. href is UTF-8 and borrowed for
 * this synchronous call. out_navigated is 1 only when that navigation
 * adapter accepted the request, and 0 for preventDefault or rejection. */
typedef struct PBrowserScriptAnchorClickInfo {
    unsigned long size;
    int x;
    int y;
    const char *href;
} PBrowserScriptAnchorClickInfo;

/* Size-tagged anchor activation extension. target and rel are borrowed
 * UTF-8 attribute values for this synchronous call; NULL means the
 * attribute was absent. The old PBrowserScriptAnchorClickInfo entry point
 * remains available and behaves as target=""/rel="". */
typedef struct PBrowserScriptAnchorClickInfoEx {
    unsigned long size;
    int x;
    int y;
    const char *href;
    const char *target;
    const char *rel;
} PBrowserScriptAnchorClickInfoEx;

/* Typed host adapter for a script-visible HTMLElement.click() invocation.
 * The browser layer owns the DOM method and its synchronous dispatch entry;
 * the host receives the UTF-8 DOM id and reuses the typed click/input/change
 * contracts for any supported default action. Returning zero means the
 * invocation was processed, including a disabled/no-op target; a negative
 * return reports an adapter failure. The id is borrowed only for this call. */
typedef struct PBrowserScriptProgrammaticClickInfo {
    unsigned long size;
    const char *element_id;
} PBrowserScriptProgrammaticClickInfo;
typedef int (*PBrowserScriptProgrammaticClickFn)(void *pw,
        const PBrowserScriptProgrammaticClickInfo *info);
typedef struct PBrowserScriptProgrammaticClickCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptProgrammaticClickFn dispatch_click;
} PBrowserScriptProgrammaticClickCallbacks;

/* Product-owned programmatic form activation adapter.  The original
 * PBrowserScriptProgrammaticClickCallbacks contract remains available for
 * hosts that want to own the complete operation.  This additive contract
 * moves the browser-semantic ordering into positron_browser: the host only
 * resolves a supported form/disclosure target, performs the core/platform
 * default, and handles non-form targets. Text/password/textarea/select
 * targets use a bounded focus default; the host still owns the native window
 * and any select popup UI. A disclosure target represents the first direct
 * <summary> trigger of a laid-out <details>; the host mutates the Core open
 * attribute and schedules its normal style/layout pass. All strings and
 * target pointers are borrowed for the synchronous callback only. */
#define PBROWSER_SCRIPT_CLICK_TARGET_CHECKBOX 1
#define PBROWSER_SCRIPT_CLICK_TARGET_RADIO    2
#define PBROWSER_SCRIPT_CLICK_TARGET_SUBMIT  3
#define PBROWSER_SCRIPT_CLICK_TARGET_RESET   4
#define PBROWSER_SCRIPT_CLICK_TARGET_FILE    5
#define PBROWSER_SCRIPT_CLICK_TARGET_TEXT     6
#define PBROWSER_SCRIPT_CLICK_TARGET_PASSWORD 7
#define PBROWSER_SCRIPT_CLICK_TARGET_TEXTAREA 8
#define PBROWSER_SCRIPT_CLICK_TARGET_SELECT   9
#define PBROWSER_SCRIPT_CLICK_TARGET_DISCLOSURE 10

#define PBROWSER_SCRIPT_CLICK_DEFAULT_TOGGLE 1
#define PBROWSER_SCRIPT_CLICK_DEFAULT_SUBMIT 2
#define PBROWSER_SCRIPT_CLICK_DEFAULT_RESET  3
#define PBROWSER_SCRIPT_CLICK_DEFAULT_FILE   4
#define PBROWSER_SCRIPT_CLICK_DEFAULT_FOCUS  5
#define PBROWSER_SCRIPT_CLICK_DEFAULT_DISCLOSURE 6

typedef struct PBrowserScriptProgrammaticClickTargetInfo {
    unsigned long size;
    int found;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int disabled;
} PBrowserScriptProgrammaticClickTargetInfo;
typedef int (*PBrowserScriptGetProgrammaticClickTargetFn)(void *pw,
        const char *element_id,
        PBrowserScriptProgrammaticClickTargetInfo *out_info);
typedef int (*PBrowserScriptValidateProgrammaticClickFn)(void *pw,
        const PBrowserScriptProgrammaticClickInfo *info,
        const PBrowserScriptProgrammaticClickTargetInfo *target,
        int *out_valid);
typedef struct PBrowserScriptProgrammaticClickDefaultInfo {
    unsigned long size;
    const char *element_id;
    int action;
    int x;
    int y;
    int width;
    int height;
    int kind;
    int validation_valid;
} PBrowserScriptProgrammaticClickDefaultInfo;
typedef int (*PBrowserScriptProgrammaticClickDefaultFn)(void *pw,
        const PBrowserScriptProgrammaticClickDefaultInfo *info);
typedef struct PBrowserScriptProgrammaticClickCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptGetProgrammaticClickTargetFn get_target;
    PBrowserScriptValidateProgrammaticClickFn validate_submit;
    PBrowserScriptProgrammaticClickDefaultFn perform_default;
    PBrowserScriptProgrammaticClickFn dispatch_generic;
} PBrowserScriptProgrammaticClickCallbacksEx;

/* Additive adapter for programmatic anchor activation. The existing Ex
 * programmatic-click contract continues to own form controls; this separate
 * size-tagged callback lets a host resolve an <a href> by DOM id without
 * changing that ABI. The browser layer copies href and, when the extended
 * fields are available, target/rel into the supplied UTF-8 buffers only for
 * the synchronous callback, then reuses the trusted anchor
 * click/navigation transaction. A zero return with found=0 lets the existing
 * generic click path handle non-anchor elements. */
typedef struct PBrowserScriptProgrammaticAnchorTargetInfo {
    unsigned long size;
    int found;
    int x;
    int y;
    int width;
    int height;
    char *href;
    int href_capacity;
    char *target;
    int target_capacity;
    char *rel;
    int rel_capacity;
} PBrowserScriptProgrammaticAnchorTargetInfo;
typedef int (*PBrowserScriptGetProgrammaticAnchorTargetFn)(void *pw,
        const char *element_id,
        PBrowserScriptProgrammaticAnchorTargetInfo *out_info);
typedef struct PBrowserScriptProgrammaticAnchorCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptGetProgrammaticAnchorTargetFn get_target;
} PBrowserScriptProgrammaticAnchorCallbacks;

/* Typed host adapter for product-owned native form submit/reset events. The
 * browser layer owns the form-event contract and dispatch entry point; the
 * host supplies core hit-testing/propagation for the document coordinates.
 * x/y are borrowed document CSS pixels. event_type must be "submit" or
 * "reset" and is borrowed only for the synchronous callback. The adapter
 * returns zero when core dispatch was attempted and writes 1 when the native
 * default is allowed or 0 when a cancelable listener prevented it; a negative
 * return reports an adapter failure. */
typedef struct PBrowserScriptFormEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptFormEventInfo;
typedef int (*PBrowserScriptDispatchFormEventFn)(void *pw,
        const PBrowserScriptFormEventInfo *info, int *out_default_allowed);
typedef struct PBrowserScriptFormEventCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchFormEventFn dispatch_form_event;
} PBrowserScriptFormEventCallbacks;

/* Additive form-event adapter addressed by DOM id. This is used by
 * script-facing form methods whose target is not represented by a pointer
 * coordinate. event_type is "submit" or "reset" and all strings are borrowed
 * only for the synchronous callback. The callback returns zero when Core
 * dispatch was attempted and writes 1 when the default is allowed or 0 when
 * a cancelable listener prevented it; a negative return reports an adapter
 * failure. */
typedef struct PBrowserScriptFormEventInfoEx {
    unsigned long size;
    const char *element_id;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptFormEventInfoEx;
typedef int (*PBrowserScriptDispatchFormEventFnEx)(void *pw,
        const PBrowserScriptFormEventInfoEx *info,
        int *out_default_allowed);
typedef struct PBrowserScriptFormEventCallbacksEx {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchFormEventFnEx dispatch_form_event;
} PBrowserScriptFormEventCallbacksEx;

/* Typed host adapter for product-owned native constraint-validation events.
 * The browser layer owns the invalid-event contract and dispatch entry point;
 * the host supplies core hit-testing/propagation for the invalid control's
 * document coordinates. x/y are borrowed document CSS pixels. event_type must
 * be "invalid" and is borrowed only for the synchronous callback. The
 * adapter returns zero when core dispatch was attempted and writes 1 when the
 * host's invalid feedback is allowed or 0 when a cancelable listener prevented
 * it; a negative return reports an adapter failure. */
typedef struct PBrowserScriptInvalidEventInfo {
    unsigned long size;
    int x;
    int y;
    const char *event_type;
    int bubbles;
    int cancelable;
} PBrowserScriptInvalidEventInfo;
typedef int (*PBrowserScriptDispatchInvalidEventFn)(void *pw,
        const PBrowserScriptInvalidEventInfo *info,
        int *out_default_allowed);
typedef struct PBrowserScriptInvalidCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptDispatchInvalidEventFn dispatch_invalid;
} PBrowserScriptInvalidCallbacks;

/* Navigation operations understood by the browser bootstrap. The browser
 * DLL owns their JSON parsing and result encoding; a host adapter supplies
 * the navigation/history side effects. */
#define PBROWSER_SCRIPT_NAVIGATION_REPLACE_STATE   1u
#define PBROWSER_SCRIPT_NAVIGATION_PUSH_STATE      2u
#define PBROWSER_SCRIPT_NAVIGATION_BACK             3u
#define PBROWSER_SCRIPT_NAVIGATION_FORWARD          4u
#define PBROWSER_SCRIPT_NAVIGATION_GO               5u
#define PBROWSER_SCRIPT_NAVIGATION_ASSIGN           6u
#define PBROWSER_SCRIPT_NAVIGATION_RELOAD           7u
#define PBROWSER_SCRIPT_NAVIGATION_REPLACE          8u
#define PBROWSER_SCRIPT_NAVIGATION_FRAGMENT         9u
#define PBROWSER_SCRIPT_NAVIGATION_FRAGMENT_REPLACE 10u
#define PBROWSER_SCRIPT_NAVIGATION_OPEN              11u

/* Bounded browsing-context classification for anchor navigation. The
 * browser DLL classifies the raw target attribute but does not create or own
 * windows. DEFAULT means an absent/empty target; SELF, PARENT and TOP are
 * current-context policies that a single-window host can safely map to its
 * active document. BLANK and NAMED require a host window manager to create or
 * select another context; a host without that capability must fail closed. */
#define PBROWSER_SCRIPT_NAVIGATION_TARGET_DEFAULT 0u
#define PBROWSER_SCRIPT_NAVIGATION_TARGET_SELF    1u
#define PBROWSER_SCRIPT_NAVIGATION_TARGET_PARENT  2u
#define PBROWSER_SCRIPT_NAVIGATION_TARGET_TOP     3u
#define PBROWSER_SCRIPT_NAVIGATION_TARGET_BLANK   4u
#define PBROWSER_SCRIPT_NAVIGATION_TARGET_NAMED   5u

/* Typed navigation request passed to the host adapter. url and state_json
 * are borrowed for the duration of the callback; either may be NULL when
 * the operation does not use it. state_json is compact UTF-8 JSON. target
 * and rel are borrowed anchor metadata and are NULL for non-anchor
 * operations, except OPEN where target carries window.open's second
 * argument. target_kind is one of the bounded target-policy constants
 * above; it is DEFAULT when an operation has no target and for an
 * absent/empty target. The raw target remains available for a host window
 * manager. OPEN is a request to reuse a current browsing context; a host
 * without that target policy must return zero, so the product bootstrap
 * returns null and never silently replaces the current document. The optional features
 * argument to window.open is intentionally ignored in this bounded subset.
 * For OPEN, context_name is the current bounded window.name snapshot. It is
 * borrowed for the callback and is present only for OPEN; a single-window
 * host may accept a NAMED target only when target and context_name match
 * exactly. This lets an existing named current context be reused without
 * pretending that a new window manager exists. For a successful PUSH_STATE
 * callback, out_value must receive the exposed history length; other
 * operations ignore it. */
typedef struct PBrowserScriptNavigationInfo {
    unsigned long size;
    unsigned int kind;
    const char *url;
    const char *state_json;
    int delta;
    /* Optional anchor metadata. Non-anchor navigation passes NULL. */
    const char *target;
    const char *rel;
    unsigned int target_kind;
    const char *context_name;
} PBrowserScriptNavigationInfo;
typedef int (*PBrowserScriptNavigateFn)(void *pw,
        const PBrowserScriptNavigationInfo *info, int *out_value);
typedef struct PBrowserScriptNavigationCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptNavigateFn navigate;
} PBrowserScriptNavigationCallbacks;

/* Typed host adapter for the product page viewport. The Browser bootstrap
 * normalizes window.scrollTo/scrollBy coordinates, then gives the active
 * host a chance to clamp and apply them. The request is in non-negative CSS
 * page pixels; the host writes the actually applied non-negative coordinates
 * to out_x/out_y. A zero return means the request was handled, while a
 * negative return rejects it as an adapter failure. The callback is never
 * asked to create a window or own the Browser session. */
typedef struct PBrowserScriptScrollInfo {
    unsigned long size;
    int scroll_x;
    int scroll_y;
    /* Borrowed element id for an Element.scrollTo/scrollBy request. NULL
     * denotes the page viewport and preserves the original callback ABI. */
    const char *element_id;
} PBrowserScriptScrollInfo;
typedef int (*PBrowserScriptScrollFn)(void *pw,
        const PBrowserScriptScrollInfo *info, int *out_x, int *out_y);
typedef struct PBrowserScriptScrollCallbacks {
    unsigned long size;
    void *pw;
    PBrowserScriptScrollFn scroll;
} PBrowserScriptScrollCallbacks;

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

/* Browser script session. The session owns one browser-sized PScript context
 * (the browser bootstrap uses a bounded 730 KiB heap ceiling) and all
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
/* Apply a host-committed same-document traversal to the product bootstrap.
 * The caller owns history commit/rollback and supplies borrowed UTF-8 JSON
 * state plus the resulting URL; this API only updates location/history state
 * in the script context and dispatches popstate/hashchange in that context. */
PBROWSER_API int PBrowser_ScriptSessionDispatchHistoryTraversal(
        HANDLE hSession, const char *state_json, const char *url);
/* Apply a host-committed same-document fragment navigation to the product
 * bootstrap. The caller owns the history entry and side effects; this API
 * updates location/history length and dispatches hashchange. */
PBROWSER_API int PBrowser_ScriptSessionDispatchHashNavigation(
        HANDLE hSession, const char *url, int history_length);
/* Advance the product-owned initial page lifecycle. The host calls this
 * after the document's classic scripts have run. `state` accepts
 * "interactive", "domcontentloaded", or "complete"; repeated or regressive
 * states are ignored. The bootstrap updates document.readyState and
 * dispatches readystatechange/DOMContentLoaded/load, then one window
 * pageshow event, in that order. The initial pageshow is not repeated when
 * "complete" is dispatched again. */
PBROWSER_API int PBrowser_ScriptSessionDispatchPageLifecycle(
        HANDLE hSession, const char *state);
/* Dispatch the current document's cancelable `beforeunload` event before a
 * host replaces or closes it. `out_prevented` is set to one when a handler
 * called preventDefault(), assigned a non-empty returnValue, or returned a
 * non-empty string from window.onbeforeunload. The output is initialized to
 * one and remains fail-closed when the script call cannot be completed. The
 * event is not a prompt UI and does not perform navigation or teardown; the
 * host owns that policy and must call this while the session is alive. */
PBROWSER_API int PBrowser_ScriptSessionDispatchBeforeUnload(
        HANDLE hSession, int *out_prevented);
/* A task checkpoint lets the host drive the bounded browser-script queues
 * through one product-owned ordering. Timers, animation frames, messages and
 * idle callbacks are selected by phase_mask; after every selected phase the
 * Browser drains the bounded microtask queue. A zero mask only drains
 * microtasks. `now_ms`, `frame_timestamp_ms` and `idle_deadline_ms` are
 * host-owned monotonic millisecond values. A zero message_limit uses the
 * message queue's default bound. The call is synchronous and never creates a
 * thread or takes ownership of the host clock. */
#define PBROWSER_SCRIPT_PUMP_TIMERS             0x0001UL
#define PBROWSER_SCRIPT_PUMP_ANIMATION_FRAMES   0x0002UL
#define PBROWSER_SCRIPT_PUMP_MESSAGES           0x0004UL
#define PBROWSER_SCRIPT_PUMP_IDLE_CALLBACKS     0x0008UL
#define PBROWSER_SCRIPT_PUMP_ALL                (PBROWSER_SCRIPT_PUMP_TIMERS | \
                                                  PBROWSER_SCRIPT_PUMP_ANIMATION_FRAMES | \
                                                  PBROWSER_SCRIPT_PUMP_MESSAGES | \
                                                  PBROWSER_SCRIPT_PUMP_IDLE_CALLBACKS)
PBROWSER_API int PBrowser_ScriptSessionRunTaskCheckpoint(HANDLE hSession,
        unsigned long now_ms, unsigned long frame_timestamp_ms,
        unsigned long idle_deadline_ms, unsigned long message_limit,
        unsigned long phase_mask);
/* Complete the product-owned lifecycle for a document that is being
 * replaced after beforeunload has been allowed. The first call dispatches
 * document visibilitychange (when the page was visible), window pagehide,
 * and window unload in that order, then
 * clears page-owned timers, animation frames, microtasks, idle callbacks and
 * queued messages. Repeated calls are idempotent. The host must call this
 * while the old document and session are still alive, before destroying the
 * session; failed candidate navigations must not call it. */
PBROWSER_API int PBrowser_ScriptSessionDispatchPageTeardown(HANDLE hSession);
/* Run due script-owned timers at a host monotonic millisecond timestamp.
 * The host owns the clock and may call this from its message loop; the
 * bootstrap runs bounded setTimeout/setInterval callbacks synchronously. */
PBROWSER_API int PBrowser_ScriptSessionRunTimers(HANDLE hSession,
        unsigned long now_ms);
/* Run and consume the current requestAnimationFrame batch with a host-owned
 * timestamp in milliseconds. The callback receives that timestamp. */
PBROWSER_API int PBrowser_ScriptSessionRunAnimationFrames(HANDLE hSession,
        unsigned long timestamp_ms);
/* Update the product-owned visibility state and dispatch visibilitychange
 * followed by pagehide when entering hidden or pageshow when returning to
 * visible. Duplicate visibility values are silent; pageshow events expose
 * persisted == false because this layer has no bfcache. `hidden` is
 * normalized to 0 or 1. */
PBROWSER_API int PBrowser_ScriptSessionDispatchVisibility(HANDLE hSession,
        int hidden);
/* Update the product-owned top-level window focus state and dispatch a
 * non-bubbling window focus or blur event when the normalized value changes.
 * Duplicate values are silent; `focused` is normalized to 0 or 1. The host
 * must call this for every WM activation transition and immediately report
 * an inactive state when a session is created in an inactive window. The
 * script-facing document.hasFocus() reflects the current state. */
PBROWSER_API int PBrowser_ScriptSessionDispatchWindowFocus(HANDLE hSession,
        int focused);
/* Pump bounded product-owned microtasks, idle callbacks, and queued
 * same-window postMessage deliveries. The host owns scheduling and may call
 * these from its message loop; each function returns the script result code. */
PBROWSER_API int PBrowser_ScriptSessionRunMicrotasks(HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRunIdleCallbacks(HANDLE hSession,
        unsigned long deadline_ms);
PBROWSER_API int PBrowser_ScriptSessionRunMessages(HANDLE hSession,
        unsigned long limit);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomReadCallbacks(
        HANDLE hSession, const PBrowserScriptDomReadCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomReadCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterActiveElementCallbacks(
        HANDLE hSession,
        const PBrowserScriptActiveElementCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterActiveElementCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterInteractionElementCallbacks(
        HANDLE hSession,
        const PBrowserScriptInteractionCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionRegisterInteractionElementCallbacksEx(
        HANDLE hSession,
        const PBrowserScriptInteractionCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterInteractionElementCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterFocusRequestCallbacks(
        HANDLE hSession,
        const PBrowserScriptFocusRequestCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionRegisterFocusRequestCallbacksEx(
        HANDLE hSession,
        const PBrowserScriptFocusRequestCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFocusRequestCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFocusRequestCallbacksEx(
        HANDLE hSession);
/* Dispatch one script-visible HTMLElement.focus()/blur() request through the
 * registered host adapter. */
PBROWSER_API int PBrowser_ScriptSessionDispatchFocusRequest(HANDLE hSession,
        const PBrowserScriptFocusRequestInfo *info);
/* Extended dispatch carries the page-reveal suppression requested by the
 * script path (including focus({preventScroll:true})) and an optional
 * caller-owned result containing the host's applied CSS page scroll. */
PBROWSER_API int PBrowser_ScriptSessionDispatchFocusRequestEx(HANDLE hSession,
        const PBrowserScriptFocusRequestInfoEx *info,
        PBrowserScriptFocusRequestResult *out_result);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomRelationCallbacks(
        HANDLE hSession, const PBrowserScriptDomRelationCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomRelationCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomWriteCallbacks(
        HANDLE hSession, const PBrowserScriptDomWriteCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomWriteCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterContentEditableCallbacks(
        HANDLE hSession,
        const PBrowserScriptContentEditableCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterContentEditableCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterContentEditableSelectionCallbacks(
        HANDLE hSession,
        const PBrowserScriptContentEditableSelectionCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterContentEditableSelectionCallbacks(
        HANDLE hSession);
/* Notify the Browser that a host-native contenteditable range changed. The
 * Browser updates its bounded script-side selection state and dispatches a
 * non-bubbling, non-cancelable `selectionchange` event only when the range
 * actually changed. `trusted` is normalized to zero or one. out_changed may
 * be NULL; when supplied it is set to one only when an event was emitted. */
PBROWSER_API int PBrowser_ScriptSessionNotifyContentEditableSelection(
        HANDLE hSession, const char *element_id, int start, int end,
        int direction, int trusted, int *out_changed);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomValueCallbacks(
        HANDLE hSession, const PBrowserScriptDomValueCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomValueCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterDomCheckedCallbacks(
        HANDLE hSession, const PBrowserScriptDomCheckedCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterDomCheckedCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormCallbacks(
        HANDLE hSession, const PBrowserScriptFormCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormDataCallbacks(
        HANDLE hSession, const PBrowserScriptFormDataCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormDataCallbacksEx(
        HANDLE hSession, const PBrowserScriptFormDataCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormDataCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormResetCallbacks(
        HANDLE hSession, const PBrowserScriptFormResetCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormResetCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormSubmitCallbacks(
        HANDLE hSession, const PBrowserScriptFormSubmitCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormSubmitCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormSubmitDirectCallbacks(
        HANDLE hSession,
        const PBrowserScriptFormSubmitDirectCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormSubmitDirectCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterValidationCallbacks(
        HANDLE hSession, const PBrowserScriptValidationCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterValidationCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterReportValidityCallbacks(
        HANDLE hSession,
        const PBrowserScriptReportValidityCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterReportValidityCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterCustomValidityCallbacks(
        HANDLE hSession,
        const PBrowserScriptCustomValidityCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterCustomValidityCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterInputCallbacks(
        HANDLE hSession, const PBrowserScriptInputCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterInputCallbacks(
        HANDLE hSession);
/* Dispatch one native text/file/checkbox-radio input event through the host's
 * core adapter. On success out_default_allowed is 1 or 0 as described above. */
PBROWSER_API int PBrowser_ScriptSessionDispatchInputEvent(HANDLE hSession,
        const PBrowserScriptInputEventInfo *info, int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionRegisterKeyCallbacks(
        HANDLE hSession, const PBrowserScriptKeyCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterKeyCallbacks(
        HANDLE hSession);
/* Dispatch one native keyboard event through the host's core adapter. On
 * success out_default_allowed is 1 or 0 as described above. */
PBROWSER_API int PBrowser_ScriptSessionDispatchKeyEvent(HANDLE hSession,
        const PBrowserScriptKeyEventInfo *info, int *out_default_allowed);
/* Ask the active script-owned modal dialog to run requestClose(). The host
 * supplies the platform gesture (for example Escape); this API only applies
 * the browser-side cancel/close lifecycle. out_handled is set when an active
 * modal existed, including when its cancel event prevented closing, and
 * out_closed is set only when the close event was committed. return_value is
 * UTF-8, borrowed for the call, and may be NULL for the empty value. */
PBROWSER_API int PBrowser_ScriptSessionRequestDialogClose(HANDLE hSession,
        const char *return_value, int *out_handled, int *out_closed);
/* Directly close one open script-owned dialog addressed by UTF-8 DOM id.
 * This follows dialog.close(value): it updates returnValue and dispatches
 * close without a preceding cancel event. It is the Browser-side default
 * action for a Core-resolved method="dialog" form after validation and the
 * cancelable submit event have both succeeded. out_closed is 1 only when the
 * id resolved to an open dialog and close committed. Strings are borrowed for
 * the call, may not exceed the bounded dialog constants, and return_value may
 * be NULL for the empty value. */
PBROWSER_API int PBrowser_ScriptSessionCloseDialogById(HANDLE hSession,
        const char *dialog_id, const char *return_value, int *out_closed);
/* Read the id of the current script-owned modal dialog. The returned id is
 * UTF-8 and excludes the terminating NUL from out_bytes. An empty string and
 * zero bytes mean that no modal is active. The caller may probe with
 * out_id=NULL and out_capacity=0; otherwise the capacity must include the
 * complete string and its NUL. A too-small output buffer returns
 * PSCRIPT_ERROR_ARGUMENT and still reports the required byte count. */
PBROWSER_API int PBrowser_ScriptSessionGetActiveDialogId(HANDLE hSession,
        char *out_id, int out_capacity, int *out_bytes);
PBROWSER_API int PBrowser_ScriptSessionRegisterFocusCallbacks(
        HANDLE hSession, const PBrowserScriptFocusCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFocusCallbacks(
        HANDLE hSession);
/* Dispatch one native focus-family event through the host's core adapter. */
PBROWSER_API int PBrowser_ScriptSessionDispatchFocusEvent(HANDLE hSession,
        const PBrowserScriptFocusEventInfo *info);
PBROWSER_API int PBrowser_ScriptSessionRegisterEditCallbacks(
        HANDLE hSession, const PBrowserScriptEditCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterEditCallbacks(
        HANDLE hSession);
/* Dispatch one native EDIT change event through the host's core adapter. */
PBROWSER_API int PBrowser_ScriptSessionDispatchEditEvent(HANDLE hSession,
        const PBrowserScriptEditEventInfo *info);
PBROWSER_API int PBrowser_ScriptSessionRegisterNativeEditCallbacksEx(
        HANDLE hSession,
        const PBrowserScriptNativeEditCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterNativeEditCallbacksEx(
        HANDLE hSession);
/* Dispatch a product-owned native EDIT beforeinput. On success
 * out_default_allowed is 1 when the host may let the native control mutate,
 * or 0 when a cancelable listener prevented that default. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeEditBeforeInput(
        HANDLE hSession, const PBrowserScriptNativeEditInputInfo *info,
        int *out_default_allowed);
/* Dispatch one native EDIT composition phase. START is cancelable and
 * returns whether the host may keep the platform composition active; UPDATE
 * and END are non-cancelable and preserve the browser-owned preedit state. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeEditComposition(
        HANDLE hSession,
        const PBrowserScriptNativeEditCompositionInfo *info,
        int *out_default_allowed);
/* Dispatch one complete native EDIT IME result through the browser-owned
 * composition/update and pending-input transaction. The host owns only the
 * platform text replacement and subsequently commits native input. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeEditResult(
        HANDLE hSession, const PBrowserScriptNativeEditResultInfo *info);
/* Notify the product layer that the native EDIT value was committed. This
 * dispatches input using the accepted beforeinput metadata, or the explicit
 * input_type/data fallback supplied in info, and marks the target dirty for a
 * later blur/change. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeEditInput(
        HANDLE hSession, const PBrowserScriptNativeEditInputInfo *info);
/* Notify the product layer that the native EDIT lost focus. A change event is
 * dispatched only when this target has committed a value since its last
 * blur/change. The target's pending metadata is then cleared. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeEditBlur(
        HANDLE hSession, const PBrowserScriptNativeEditInputInfo *info);
/* Drop all native EDIT pending/dirty state, normally before the host destroys
 * and rebuilds its native controls for a new document/layout. */
PBROWSER_API int PBrowser_ScriptSessionResetNativeEditState(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterSelectCallbacks(
        HANDLE hSession, const PBrowserScriptSelectCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterSelectCallbacks(
        HANDLE hSession);
/* Dispatch one native SELECT input/change event through the host's core
 * adapter. */
PBROWSER_API int PBrowser_ScriptSessionDispatchSelectEvent(HANDLE hSession,
        const PBrowserScriptSelectEventInfo *info);
/* Arbitrate one trusted checkbox/radio activation. CLICK dispatches a
 * cancelable click and sets out_default_allowed to 1 only when the host may
 * mutate Core. COMMIT dispatches non-cancelable input then change only when
 * selected_after differs from the CLICK snapshot. CANCEL clears the bounded
 * transaction without dispatching events. The host owns Core mutation and
 * native control state; target_token is stable and non-zero for one target. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeToggle(
        HANDLE hSession, const PBrowserScriptNativeToggleInfo *info,
        int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionResetNativeToggleState(
        HANDLE hSession);
/* Dispatch one phase of a trusted native button activation. CLICK emits
 * click and opens bounded state; COMMIT emits submit or reset for those kinds,
 * while an ordinary button only consumes the accepted default; CANCEL clears
 * state. A form-event callback is required for submit/reset and not required
 * for an ordinary button. The host may perform Core/WM default action only
 * after an accepted COMMIT. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeButton(
        HANDLE hSession, const PBrowserScriptNativeButtonInfo *info,
        int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionResetNativeButtonState(
        HANDLE hSession);
/* Notify the product layer about one native file-input picker transaction.
 * BEGIN opens bounded state without dispatching events. COMMIT dispatches a
 * non-cancelable input(insertFromFile) followed by change and closes the
 * transaction; CANCEL closes it without changing the already committed Core
 * value. The host owns the picker and file mutation. A CANCEL for an absent
 * transaction is idempotent. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeFileSelection(
        HANDLE hSession,
        const PBrowserScriptNativeFileSelectionInfo *info);
/* Drop all bounded native file-input transaction state before native controls
 * or the document are destroyed/rebuilt. */
PBROWSER_API int PBrowser_ScriptSessionResetNativeFileState(
        HANDLE hSession);
/* Arbitrate one host-owned programmatic file-picker request. out_accepted is
 * set to 1 when the phase changed product state and 0 for an idempotent
 * coalesced/absent request. REQUEST coalesces while another token is pending
 * or active; CLOSE/CANCEL release the matching request without owning any
 * picker or path. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeFilePicker(
        HANDLE hSession, const PBrowserScriptNativeFilePickerInfo *info,
        int *out_accepted);
/* Drop any pending/active file-picker request before a document/session is
 * destroyed or replaced. */
PBROWSER_API int PBrowser_ScriptSessionResetNativeFilePickerState(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx(
        HANDLE hSession,
        const PBrowserScriptNativeSelectCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterNativeSelectCallbacksEx(
        HANDLE hSession);
/* Notify the product layer that a native SELECT value/selection was
 * committed. The browser dispatches input then change; the host remains the
 * owner of the WM control and core selection mutation. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeSelectCommit(
        HANDLE hSession,
        const PBrowserScriptNativeSelectCommitInfo *info);
/* Notify the product layer that a native SELECT gained or lost focus. The
 * browser dispatches the corresponding focus-family pair through the
 * session's generic focus adapter and suppresses duplicate transitions. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeSelectFocus(
        HANDLE hSession,
        const PBrowserScriptNativeSelectFocusInfo *info);
/* Dispatch one native SELECT key event through the session's generic key
 * adapter. On success out_default_allowed is 1 when the host may let the WM
 * control process its native default, or 0 when a cancelable keydown was
 * prevented. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeSelectKey(
        HANDLE hSession,
        const PBrowserScriptNativeSelectKeyInfo *info,
        int *out_default_allowed);
/* Notify the product layer about a single-select dropdown transaction. On
 * END_OK, out_should_commit is 1 only when a candidate was observed since
 * BEGIN; on all other phases it is zero. END_CANCEL clears the candidate
 * without dispatching input/change. */
PBROWSER_API int PBrowser_ScriptSessionDispatchNativeSelectInteraction(
        HANDLE hSession,
        const PBrowserScriptNativeSelectInteractionInfo *info,
        int *out_should_commit);
/* Drop all bounded native SELECT target state before native controls are
 * destroyed or rebuilt for a new document/layout. */
PBROWSER_API int PBrowser_ScriptSessionResetNativeSelectState(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterClickCallbacks(
        HANDLE hSession, const PBrowserScriptClickCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterClickCallbacks(
        HANDLE hSession);
/* Dispatch one native click event through the host's core adapter. On success
 * out_default_allowed is 1 or 0 as described above. */
PBROWSER_API int PBrowser_ScriptSessionDispatchClickEvent(HANDLE hSession,
        const PBrowserScriptClickEventInfo *info, int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionDispatchAnchorClick(
        HANDLE hSession, const PBrowserScriptAnchorClickInfo *info,
        int *out_navigated);
PBROWSER_API int PBrowser_ScriptSessionDispatchAnchorClickEx(
        HANDLE hSession, const PBrowserScriptAnchorClickInfoEx *info,
        int *out_navigated);
PBROWSER_API int PBrowser_ScriptSessionRegisterProgrammaticClickCallbacks(
        HANDLE hSession,
        const PBrowserScriptProgrammaticClickCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterProgrammaticClickCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx(
        HANDLE hSession,
        const PBrowserScriptProgrammaticClickCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterProgrammaticClickCallbacksEx(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterProgrammaticAnchorCallbacks(
        HANDLE hSession,
        const PBrowserScriptProgrammaticAnchorCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterProgrammaticAnchorCallbacks(
        HANDLE hSession);
/* Dispatch one script-visible HTMLElement.click() invocation through the
 * host's typed activation adapter.  When the additive Ex callbacks are
 * registered, positron_browser owns disabled-target suppression, typed click
 * dispatch, submit/reset event ordering and submit validation before calling
 * the host's default-action callback. */
PBROWSER_API int PBrowser_ScriptSessionDispatchProgrammaticClick(
        HANDLE hSession, const PBrowserScriptProgrammaticClickInfo *info);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormEventCallbacks(
        HANDLE hSession, const PBrowserScriptFormEventCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormEventCallbacks(
        HANDLE hSession);
/* Dispatch one native form submit/reset event through the host's core
 * adapter. On success out_default_allowed is 1 or 0 as described above. */
PBROWSER_API int PBrowser_ScriptSessionDispatchFormEvent(HANDLE hSession,
        const PBrowserScriptFormEventInfo *info, int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionRegisterFormEventCallbacksEx(
        HANDLE hSession,
        const PBrowserScriptFormEventCallbacksEx *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterFormEventCallbacksEx(
        HANDLE hSession);
/* Dispatch one script-visible form event addressed by DOM id. This additive
 * entry point requires the Ex callback table and preserves the old
 * coordinate-based form-event ABI. */
PBROWSER_API int PBrowser_ScriptSessionDispatchFormEventById(HANDLE hSession,
        const PBrowserScriptFormEventInfoEx *info,
        int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionRegisterInvalidCallbacks(
        HANDLE hSession, const PBrowserScriptInvalidCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterInvalidCallbacks(
        HANDLE hSession);
/* Dispatch one native constraint-validation event through the host's core
 * adapter. On success out_default_allowed is 1 or 0 as described above. */
PBROWSER_API int PBrowser_ScriptSessionDispatchInvalidEvent(HANDLE hSession,
        const PBrowserScriptInvalidEventInfo *info, int *out_default_allowed);
PBROWSER_API int PBrowser_ScriptSessionRegisterNavigationCallbacks(
        HANDLE hSession,
        const PBrowserScriptNavigationCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterNavigationCallbacks(
        HANDLE hSession);
PBROWSER_API int PBrowser_ScriptSessionRegisterScrollCallbacks(
        HANDLE hSession, const PBrowserScriptScrollCallbacks *callbacks);
PBROWSER_API int PBrowser_ScriptSessionUnregisterScrollCallbacks(
        HANDLE hSession);
/* Synchronize a physical host viewport change into the Browser bootstrap.
 * Coordinates are non-negative CSS page pixels. The Browser dispatches one
 * visualViewport `scroll` followed by one window `scroll` event only when the
 * effective pair changes; it does not call the host scroll adapter again, so
 * hosts can use this after scrollbar, touch, keyboard or resize handling
 * without recursion. */
PBROWSER_API int PBrowser_ScriptSessionNotifyScroll(HANDLE hSession,
        int scroll_x, int scroll_y);
/* Synchronize a host-driven retained element overflow scroll into the Browser
 * bootstrap. Coordinates are non-negative CSS pixels. The Browser updates
 * the addressed element and dispatches one non-bubbling `scroll` event only
 * when the effective pair changes; it never calls the host scroll callback
 * again. Unknown ids are ignored. */
PBROWSER_API int PBrowser_ScriptSessionNotifyElementScroll(HANDLE hSession,
        const char *element_id, int scroll_x, int scroll_y);
/* Read the browser history scroll-restoration policy. The output is
 * PBROWSER_SCROLL_RESTORATION_AUTO (the default) or
 * PBROWSER_SCROLL_RESTORATION_MANUAL. A host should skip its automatic
 * history viewport restore only for the MANUAL value; fragment scrolling and
 * explicit application scrolling remain host policy. The output is written
 * on success and the session/bootstrap state is otherwise left untouched. */
#define PBROWSER_SCROLL_RESTORATION_AUTO   0
#define PBROWSER_SCROLL_RESTORATION_MANUAL 1
PBROWSER_API int PBrowser_ScriptSessionGetScrollRestoration(HANDLE hSession,
        int *out_mode);
/* Synchronize a host viewport change into the Browser bootstrap. Width and
 * height are CSS pixels and device_pixel_ratio must be positive. The Browser
 * updates inner/outer dimensions, devicePixelRatio and screen metadata, then
 * synchronously refreshes at most 64 `matchMedia()` lists and dispatches
 * their `change` events before a direction flip dispatches the stable
 * `screen.orientation` object's trusted `change` event, followed by the
 * visualViewport and window `resize` events. The visualViewport snapshot
 * follows this layout viewport: scale is 1, offsets are 0, and pageLeft/pageTop
 * track the page scroll. It does not perform Core style/layout work or run
 * queued timers/animation frames. Duplicate snapshots are accepted without
 * dispatching another event; same-direction resizes do not dispatch an
 * orientation event. `screen.orientation` tracks at most 16 listeners and
 * supports the `onchange` property. */
PBROWSER_API int PBrowser_ScriptSessionNotifyResize(HANDLE hSession,
        double viewport_width, double viewport_height,
        double device_pixel_ratio);
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
