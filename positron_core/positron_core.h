/*
 * positron_core.h - the Positron rendering core.
 *
 * positron_core.dll is the product-level boundary behind which the ported
 * NetSurf engine lives. The four NetSurf static libraries (libwapcaplet /
 * libparserutils / libhubbub via positron_netsurf+positron_hubbub, libcss,
 * libdom) are linked *into* this DLL; callers see only the small PCore_* API
 * below and never include a NetSurf header. This keeps the engine's ~hundreds
 * of internal symbols hidden and gives Positron apps one stable .dll/.lib to
 * link, exactly like positron_tls / positron_json / positron_http.
 *
 * Lifetime contract:
 *   PCore_Init        -> call once before any parse; PCore_Shutdown once after.
 *   PCore_ParseHTML   -> returns a document HANDLE; caller MUST free it with
 *                        PCore_FreeDocument.
 *   PCore_ParseCSS    -> returns a stylesheet HANDLE; caller MUST free it with
 *                        PCore_FreeStylesheet.
 *
 * All string I/O is UTF-8. Opaque HANDLEs wrap the underlying dom_document /
 * css_stylesheet so callers stay decoupled from NetSurf's types.
 */

#ifndef POSITRON_CORE_H
#define POSITRON_CORE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_CORE_EXPORTS
#  define PCORE_API __declspec(dllexport)
#else
#  define PCORE_API __declspec(dllimport)
#endif

/* Initialise the rendering core. Returns 0 on success, non-zero on failure.
 * Currently a no-op (libcss/libdom/lwc self-initialise lazily) but reserved
 * for the global setup a future layout/render stage will need. */
PCORE_API int PCore_Init(void);

/* Tear down the rendering core. Pair with PCore_Init. NULL-safe / no-op now. */
PCORE_API void PCore_Shutdown(void);

/* Parse a UTF-8 HTML document into a DOM tree (hubbub -> libdom binding).
 * `len` may be 0 to mean "html is NUL-terminated, use strlen". Returns an
 * opaque document HANDLE, or NULL on failure. Caller MUST free with
 * PCore_FreeDocument. */
PCORE_API HANDLE PCore_ParseHTML(const char *html, unsigned int len);

/* Free a document HANDLE from PCore_ParseHTML. NULL-safe. */
PCORE_API void PCore_FreeDocument(HANDLE hDoc);

/* Parse a UTF-8 CSS stylesheet (libcss). `len` may be 0 to mean "css is
 * NUL-terminated, use strlen". `url` is the sheet's base URL for @import
 * resolution; pass NULL to use a placeholder (the test sheets have no
 * @import). Returns an opaque stylesheet HANDLE, or NULL on failure. Caller
 * MUST free with PCore_FreeStylesheet. */
PCORE_API HANDLE PCore_ParseCSS(const char *css, unsigned int len,
                                const char *url);

/* Free a stylesheet HANDLE from PCore_ParseCSS. NULL-safe. */
PCORE_API void PCore_FreeStylesheet(HANDLE hSheet);

/* --- Select / computed style (engine layer 2) ----------------------- */

/* Compute the CSS 'color' of the first element named `tag` (case-insensitive,
 * e.g. "p" / "body") in document `hDoc`, applying stylesheet `hSheet` as an
 * author sheet. On success returns 0 and writes the colour to *out_argb in
 * libcss order (0xAARRGGBB); returns non-zero if the element is not found or
 * selection fails. First probe of the select/computed-style layer: it drives
 * the full libcss selection machinery (a select context + a libdom-backed
 * select handler) against the real DOM, the precursor to per-element computed
 * styles for layout. */
PCORE_API int PCore_ComputeColor(HANDLE hDoc, HANDLE hSheet,
                                 const char *tag, unsigned long *out_argb);

/* --- Whole-document styling (engine layer 2, milestone A) ----------- */

/* Style the entire document: walk the DOM top-down and, for each element,
 * compute its style from the UA default sheet + author sheet `hSheet`,
 * resolving inheritance by composing with the parent's computed style. The
 * resulting css_computed_style is attached to each element node (freed when
 * the document is freed). Returns 0 on success. This is the precursor to
 * layout: it turns a parsed DOM + stylesheet into a fully-styled tree. */
PCORE_API int PCore_StyleDocument(HANDLE hDoc, HANDLE hSheet);

/* Embedder-provided resource fetch, used to pull external resources the engine
 * references (<link rel="stylesheet"> CSS, and now <img src> discovery). The
 * engine stays transport-agnostic: it hands the raw href/src from the document
 * to `fetch`, and the embedder resolves it against the current page and fetches
 * it (e.g. via positron_http). On success `fetch` returns 0 and sets *out_data
 * (raw bytes owned by the embedder) + *out_len; the engine consumes or copies
 * them before calling `freefn` to release the embedder buffer. `fetch` returns
 * non-zero to skip the resource. `pw` is passed through opaquely. */
typedef int  (*PCoreFetchFn)(void *pw, const char *url,
                             char **out_data, int *out_len);
typedef void (*PCoreFreeFn)(void *pw, char *data);

/* As PCore_StyleDocument, but also fetches and applies external
 * <link rel="stylesheet"> sheets via the embedder's `fetch`/`freefn` (pass NULL
 * for both to skip external CSS, which makes this identical to
 * PCore_StyleDocument). The fetched sheets are author-origin, applied in
 * document order alongside the page's inline <style> blocks. */
PCORE_API int PCore_StyleDocumentEx(HANDLE hDoc, HANDLE hSheet,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw);

/* Scan the document for non-empty <img src> resources and invoke the embedder's
 * fetch callback for each one. Successful bodies are copied into a per-document
 * URL cache, so repeated scans do not fetch the same raw src again; the cache is
 * freed with the document. Bytes are not decoded or painted yet.
 * `out_found` receives the number of non-empty src attributes; `out_fetched`
 * receives the number available from cache or a successful non-empty fetch.
 * Either output pointer may be NULL. Returns 0 when the DOM was scanned. */
PCORE_API int PCore_FetchImageResources(HANDLE hDoc, PCoreFetchFn fetch,
        PCoreFreeFn freefn, void *pw, int *out_found, int *out_fetched);

/* Read back the computed 'color' (0xAARRGGBB) that PCore_StyleDocument
 * attached to the first element named `tag` (case-insensitive). Returns 0 on
 * success, non-zero if the element/style is absent. Used to verify inheritance
 * end-to-end (a child with no color of its own returns its ancestor's). */
PCORE_API int PCore_NodeComputedColor(HANDLE hDoc, const char *tag,
                                      unsigned long *out_argb);

/* --- Block layout (engine layer 3, milestone B) --------------------- */

/* Lay out the styled document through NetSurf's layout_document, given a
 * viewport of `viewport_w` x `viewport_h` CSS px. Must be called after
 * PCore_StyleDocument. The resulting NetSurf box tree supplies geometry,
 * margin collapse, inline wrapping, flex and common table layout. Returns 0
 * on success. */
PCORE_API int PCore_LayoutDocument(HANDLE hDoc, int viewport_w, int viewport_h);

/* Read back the laid-out content-box (CSS px) of the first element named `tag`.
 * Any of the out pointers may be NULL. Returns 0 on success. */
PCORE_API int PCore_NodeBox(HANDLE hDoc, const char *tag,
                            int *x, int *y, int *w, int *h);

/* --- Painting (engine layer 4, milestone C) ------------------------- */

/* Paint the laid-out document into a GDI device context through NetSurf's
 * html_redraw and the Positron GDI plotter. Must be called after
 * PCore_StyleDocument + PCore_LayoutDocument. scroll_x/scroll_y shift the page
 * beneath the viewport. The application owns the window and message loop and
 * calls this from its WM_PAINT handler. */
PCORE_API void PCore_PaintDocument(HANDLE hDoc, HDC hdc,
                                   int scroll_x, int scroll_y);

/* Total laid-out document height in CSS px (the value is from the most recent
 * PCore_LayoutDocument). Lets the application size a scrollbar. */
PCORE_API int PCore_DocumentHeight(HANDLE hDoc);

/* Set the rendering viewport: CSS-px width/height (used for vw/vh units and
 * the initial containing block) and the device DPI. Call before styling /
 * layout; defaults are 800x600 @ 96 dpi. The app should pass the real screen
 * size + DPI so styling and layout adapt to the device. A zero/negative
 * argument leaves that field unchanged. */
PCORE_API void PCore_SetViewport(int css_width, int css_height, int dpi);

/* --- Links / navigation (engine layer 4, inline milestone) ---------- */

/* Hit-test a document-space point (CSS px, i.e. client coordinate + the
 * current scroll offset) against the laid-out inline link fragments. If a link
 * covers the point, writes its (UTF-8, NUL-terminated, possibly truncated to
 * `cap`) href into `out_href` and returns 1; otherwise returns 0 and leaves
 * `out_href` untouched. Must be called after PCore_LayoutDocument. Lets the
 * application turn a tap into a navigation. */
PCORE_API int PCore_LinkAt(HANDLE hDoc, int x, int y,
                           char *out_href, int cap);

/* --- NetSurf layout/redraw port (milestone H) ----------------------- */

/* M1 self-test for the GDI-backed NetSurf plotter table: draws a bordered box,
 * a line and a line of text into `hdc` by driving the plotter directly (no
 * layout engine involved). Lets the plotter + colour/pen/font handling be
 * device-verified before redraw.c is ported in. */
PCORE_API void PCore_PlotTest(HDC hdc);

/* M5e self-test: render a built-in page with NetSurf's real layout + redraw
 * into `hdc` over a cw x ch client area - the first page drawn end-to-end by
 * the ported NetSurf engine (rebuilt each call; drive it from WM_PAINT). */
PCORE_API void PCore_NsRenderTest(HDC hdc, int cw, int ch);

/* M3 self-test: build a NetSurf box tree from a small styled document and
 * report box counts by type into `out` (ASCII). Verifies the slim DOM->box
 * builder + talloc shim before layout.c is ported. */
PCORE_API void PCore_BoxTreeTest(char *out, int cap);

/* M4 self-test: build a box tree and run NetSurf's real layout_document on it,
 * reporting root + first-text-box geometry. Verifies the ported layout.c. */
PCORE_API void PCore_LayoutBoxTest(char *out, int cap);

/* M2 self-test for the GDI font-measurement table: measures a known string and
 * computes a word-wrap split point, writing a numeric summary into `out`
 * (ASCII, NUL-terminated, <= cap). Lets the measure/split path be sanity-
 * checked offline before layout.c (which depends on it) is ported. */
PCORE_API void PCore_FontTest(char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_CORE_H */
