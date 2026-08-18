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
 * This also loads the bundled static fallback fonts from the fonts directory
 * beside positron_core.dll. Missing fonts degrade gracefully; callers can use
 * PCore_FontFallbackStatus to diagnose deployment. */
PCORE_API int PCore_Init(void);

/* Tear down the rendering core and release session font resources. Pair with
 * PCore_Init; nested pairs are reference-counted. */
PCORE_API void PCore_Shutdown(void);

/* Report whether the bundled symbols and monochrome emoji fonts were loaded
 * into the current Windows CE session. Output pointers may be NULL. */
PCORE_API void PCore_FontFallbackStatus(int *symbols_loaded,
                                        int *emoji_loaded);

/* Return non-zero when a Unicode scalar is covered by one of the currently
 * loaded bundled symbol/monochrome emoji fonts. This diagnoses packaged font
 * coverage; it does not query arbitrary system or web fonts. */
PCORE_API int PCore_BundledFontSupports(unsigned long codepoint);

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
 * engine stays transport-agnostic: legacy calls receive raw references, while
 * the base-aware styling API can hand canonical URLs to `fetch` through the
 * embedder's resolver (e.g. WinINet + positron_http). On success `fetch` returns
 * 0 and sets *out_data
 * (raw bytes owned by the embedder) + *out_len; the engine consumes or copies
 * them before calling `freefn` to release the embedder buffer. `fetch` returns
 * non-zero to skip the resource. `pw` is passed through opaquely. */
typedef int  (*PCoreFetchFn)(void *pw, const char *url,
                             char **out_data, int *out_len);
typedef void (*PCoreFreeFn)(void *pw, char *data);

/* Resolve `reference` against `base_url` into the caller-provided buffer.
 * Return 0 on success. This keeps URL policy in the embedder while allowing
 * libcss to resolve @import and url() relative to their owning stylesheet. */
typedef int (*PCoreResolveUrlFn)(void *pw, const char *base_url,
                                const char *reference,
                                char *out_url, int out_capacity);

/* As PCore_StyleDocument, but also fetches and applies external
 * <link rel="stylesheet"> sheets via the embedder's `fetch`/`freefn` (pass NULL
 * for both to skip external CSS, which makes this identical to
 * PCore_StyleDocument). The fetched sheets are author-origin, applied in
 * document order alongside the page's inline <style> blocks. */
PCORE_API int PCore_StyleDocumentEx(HANDLE hDoc, HANDLE hSheet,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw);

/* Base-aware extension of PCore_StyleDocumentEx. `document_url` should be the
 * absolute URL of the document. `resolve` is used for linked stylesheets,
 * nested @import and CSS url() values; pass NULL to retain the legacy raw-URL
 * behavior. Imported sheets use libcss's native pending/register-import API,
 * including media conditions. Failed imports are registered as empty sheets
 * so the rest of the parent stylesheet remains usable. */
PCORE_API int PCore_StyleDocumentEx2(HANDLE hDoc, HANDLE hSheet,
        const char *document_url, PCoreResolveUrlFn resolve,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw);

/* Scan the document for non-empty <img src> and computed background-image
 * resources and invoke the embedder's fetch callback for each one. Background
 * URLs are visible after PCore_StyleDocument[Ex]. Successful bodies are copied
 * into a per-document URL cache, so repeated URLs do not fetch again; the cache
 * is freed with the document. A subsequent PCore_LayoutDocument turns cached
 * WM-Imaging or libsvgtiny-decodable resources into NetSurf image carriers;
 * PaintDocument draws them through the matching public image service.
 * Cache misses and decoder failures retain the element's alt/src text fallback.
 * `out_found` receives the number of non-empty image references; `out_fetched`
 * receives the number available from cache or a successful non-empty fetch.
 * Either output pointer may be NULL. Returns 0 when the DOM was scanned. */
PCORE_API int PCore_FetchImageResources(HANDLE hDoc, PCoreFetchFn fetch,
        PCoreFreeFn freefn, void *pw, int *out_found, int *out_fetched);

/* External script resource discovery is deliberately separate from JavaScript
 * execution. Scan non-empty <script src> attributes, resolve references when
 * a document URL and resolver are supplied, and invoke the embedder fetch
 * callback for each distinct URL. Inline script text is not executed or
 * cached, and the type attribute is not interpreted by this transport layer;
 * a future script runtime owns those policy decisions. Successful bodies are
 * copied into a per-document cache and repeated references do not fetch again.
 * out_found counts eligible external references (including duplicates), while
 * out_fetched counts references available from cache or a successful non-empty
 * fetch. Either output pointer may be NULL. Returns 0 when the DOM was
 * scanned. */
PCORE_API int PCore_FetchScriptResources(HANDLE hDoc, PCoreFetchFn fetch,
        PCoreFreeFn freefn, void *pw, int *out_found, int *out_fetched);

/* Base-aware form of PCore_FetchScriptResources. document_url is the page URL
 * and resolve is called for each relative reference. Passing NULL for both
 * retains the raw-reference behavior of the legacy function. The resolver
 * and fetch callback receive the same opaque pw. No JavaScript is run by
 * either API. */
PCORE_API int PCore_FetchScriptResourcesEx(HANDLE hDoc,
        const char *document_url, PCoreResolveUrlFn resolve,
        PCoreFetchFn fetch, PCoreFreeFn freefn, void *pw,
        int *out_found, int *out_fetched);

typedef struct PCoreScriptResourceInfo {
    int available;
    int url_bytes;
    int data_bytes;
} PCoreScriptResourceInfo;

/* Read the successful script bodies retained by the document cache. The count
 * is in document discovery order and returns -1 for an invalid handle.
 * PCore_GetScriptResource returns 0 for a valid index, copies the URL into
 * url when capacity is positive, and returns out_data as a borrowed pointer
 * valid until the document is freed. url_bytes/data_bytes exclude the URL
 * terminator; failed fetches are not cached. */
PCORE_API int PCore_GetScriptResourceCount(HANDLE hDoc);
PCORE_API int PCore_GetScriptResource(HANDLE hDoc, unsigned int index,
        PCoreScriptResourceInfo *out_info, char *url, int url_capacity,
        const char **out_data);

typedef struct PCoreInlineScriptInfo {
    int source_bytes;
    int type_bytes;
} PCoreInlineScriptInfo;

/* Enumerate non-empty inline <script> bodies in document order. Elements with
 * a non-empty src attribute are excluded because their bytes belong to the
 * external resource cache above. The raw type attribute is returned for the
 * embedding runtime to interpret; core does not claim JavaScript execution.
 * Probe calls may pass NULL buffers and use the byte counts to allocate exact
 * UTF-8 storage. A valid index returns 0. */
PCORE_API int PCore_GetInlineScriptCount(HANDLE hDoc);
PCORE_API int PCore_GetInlineScript(HANDLE hDoc, unsigned int index,
        PCoreInlineScriptInfo *out_info, char *source, int source_capacity,
        char *type, int type_capacity);

typedef struct PCoreScriptInfo {
    int kind;             /* 1 = inline body, 2 = external src */
    int available;        /* external body is present in the document cache */
    int source_bytes;     /* non-zero for inline scripts */
    int url_bytes;        /* non-zero for external scripts */
    int type_bytes;
    int data_bytes;       /* cached external body bytes */
} PCoreScriptInfo;

/* Enumerate all non-empty classic script elements in DOM order. Inline bodies
 * and external references are each returned once; an element with both src
 * and text follows the HTML external-src rule and returns only the reference.
 * The resolver arguments must match PCore_FetchScriptResourcesEx if external
 * data should be found in that document cache. The returned external data is
 * borrowed until the document is freed. */
PCORE_API int PCore_GetScriptCount(HANDLE hDoc);
PCORE_API int PCore_GetScript(HANDLE hDoc, unsigned int index,
        const char *document_url, PCoreResolveUrlFn resolve, void *pw,
        PCoreScriptInfo *out_info, char *source, int source_capacity,
        char *url, int url_capacity, char *type, int type_capacity,
        const char **out_data);

/* Minimal DOM text boundary for an external script/runtime host. IDs and text
 * are UTF-8. The getter reports the full byte count even when the caller only
 * probes or supplies a smaller buffer. The setter mutates the DOM only; the
 * caller must run style/layout again before painting an already styled page. */
PCORE_API int PCore_NodeExistsById(HANDLE hDoc, const char *element_id);
PCORE_API int PCore_NodeTextContentById(HANDLE hDoc, const char *element_id,
        char *text, int text_capacity, int *out_bytes);
PCORE_API int PCore_NodeSetTextContentById(HANDLE hDoc,
        const char *element_id, const char *text);
/* Minimal UTF-8 attribute boundary for script/runtime hosts. The getter
 * returns 0 when present, 2 when the element exists but the attribute does
 * not, and 1 for invalid input/DOM failure. out_bytes excludes the NUL. */
PCORE_API int PCore_NodeAttributeById(HANDLE hDoc, const char *element_id,
        const char *name, char *value, int value_capacity, int *out_bytes);
PCORE_API int PCore_NodeSetAttributeById(HANDLE hDoc,
        const char *element_id, const char *name, const char *value);
PCORE_API int PCore_NodeRemoveAttributeById(HANDLE hDoc,
        const char *element_id, const char *name);
/* DOM-level form properties for script/runtime hosts. Value/defaultValue
 * support input, textarea and select.value; checked/defaultChecked support
 * input; selectedIndex supports select. These functions do not require a
 * styled/layout box tree, so parser-complete scripts may use them before the
 * first layout. String getters use the same probe/truncation contract as
 * PCore_NodeTextContentById. */
PCORE_API int PCore_NodeValueById(HANDLE hDoc, const char *element_id,
        char *value, int value_capacity, int *out_bytes);
PCORE_API int PCore_NodeSetValueById(HANDLE hDoc, const char *element_id,
        const char *value);
PCORE_API int PCore_NodeDefaultValueById(HANDLE hDoc,
        const char *element_id, char *value, int value_capacity,
        int *out_bytes);
PCORE_API int PCore_NodeSetDefaultValueById(HANDLE hDoc,
        const char *element_id, const char *value);
PCORE_API int PCore_NodeCheckedById(HANDLE hDoc, const char *element_id,
        int *out_checked);
PCORE_API int PCore_NodeSetCheckedById(HANDLE hDoc,
        const char *element_id, int checked);
PCORE_API int PCore_NodeDefaultCheckedById(HANDLE hDoc,
        const char *element_id, int *out_checked);
PCORE_API int PCore_NodeSetDefaultCheckedById(HANDLE hDoc,
        const char *element_id, int checked);
PCORE_API int PCore_NodeSelectedIndexById(HANDLE hDoc,
        const char *element_id, int *out_index);
PCORE_API int PCore_NodeSetSelectedIndexById(HANDLE hDoc,
        const char *element_id, int index);

/* Legacy one-shot image helpers. New consumers should use positron_image.dll's
 * retained PImage_CreateBitmapFromMemory/BitmapGetInfo/DrawBitmap API. These
 * exports remain ABI-compatible and forward to that public image service. */
PCORE_API int PCore_ImageInfoFromMemory(const char *data, int len,
        int *out_w, int *out_h);

/* If w/h are <= 0, the image's natural dimensions are used. */
PCORE_API int PCore_DrawImageFromMemory(const char *data, int len, HDC hdc,
        int x, int y, int w, int h);

/* Last WM Imaging bridge failure, for device-side diagnostics. `stage` is:
 * 0 none/success, 1 invalid argument, 2 COM init, 3 factory creation,
 * 4 image-from-buffer, 5 image-info, 6 draw. `hr` is the HRESULT. */
PCORE_API void PCore_ImageLastError(int *out_stage, unsigned long *out_hr);

/* Read back the computed 'color' (0xAARRGGBB) that PCore_StyleDocument
 * attached to the first element named `tag` (case-insensitive). Returns 0 on
 * success, non-zero if the element/style is absent. Used to verify inheritance
 * end-to-end (a child with no color of its own returns its ancestor's). */
PCORE_API int PCore_NodeComputedColor(HANDLE hDoc, const char *tag,
                                      unsigned long *out_argb);

/* --- Block layout (engine layer 3, milestone B) --------------------- */

/* Lay out the styled document through NetSurf's layout_document, given a
 * target extent of `viewport_w` x `viewport_h` device px. Must be called after
 * PCore_StyleDocument. For a high-DPI host, call PCore_SetDeviceViewport
 * before styling; the CSS viewport is then kept separately while this extent
 * remains physical. Legacy callers that do not use that entry point retain
 * the historical explicit-CSS-pixel compatibility behavior. The resulting
 * NetSurf box tree supplies geometry, margin collapse, inline wrapping, flex
 * and common table layout. Returns 0 on success. */
PCORE_API int PCore_LayoutDocument(HANDLE hDoc, int viewport_w, int viewport_h);

typedef struct PCoreLayoutStats {
    unsigned long total_ms;
    unsigned long box_construct_ms;
    unsigned long first_layout_ms;
    unsigned long settling_ms;
    unsigned long finalize_ms;
    int settling_pass;
} PCoreLayoutStats;

/* Read the most recent PCore_LayoutDocument phase timings for this document.
 * `settling_ms` includes overflow detection and the optional second layout;
 * `settling_pass` is 1 only when that second pass ran. Timings are diagnostic
 * GetTickCount deltas and do not alter layout scheduling. */
PCORE_API int PCore_GetLayoutStats(HANDLE hDoc, PCoreLayoutStats *out_stats);

typedef struct PCoreBoxStats {
    unsigned long tree_ms;
    unsigned long backgrounds_ms;
    unsigned long style_ms;
    unsigned long text_ms;
    unsigned long image_ms;
    unsigned long anonymous_ms;
    unsigned long table_normalise_ms;
    unsigned int style_calls;
    unsigned int text_calls;
    unsigned int image_calls;
    unsigned int anonymous_calls;
    unsigned int table_calls;
    unsigned int image_reuses;
    unsigned int image_markup_first;
} PCoreBoxStats;

/* Read non-overlapping box-construction probes from the most recent layout.
 * tree_ms excludes backgrounds_ms. The style/text/image/anonymous/table
 * probes are exclusive work inside tree_ms, so callers may derive tree-other
 * by subtracting them. image_reuses counts document-owned retained handles;
 * image_markup_first counts XML-like bytes dispatched to the SVG service
 * before WM Imaging. This diagnostic API does not alter box construction. */
PCORE_API int PCore_GetBoxStats(HANDLE hDoc, PCoreBoxStats *out_stats);

typedef struct PCoreImageDecodeStats {
    unsigned long svg_total_ms;
    unsigned long svg_setup_ms;
    unsigned long svg_parse_ms;
    unsigned long svg_raster_ms;
    unsigned int svg_creates;
} PCoreImageDecodeStats;

/* Aggregate retained SVG creation timings in this document's image cache.
 * Cached relayouts do not increment svg_creates. Timings come from the
 * creating positron_image.dll object and use GetTickCount resolution. */
PCORE_API int PCore_GetImageDecodeStats(HANDLE hDoc,
        PCoreImageDecodeStats *out_stats);

/* Read back the laid-out content-box (CSS px) of the first element named `tag`.
 * Any of the out pointers may be NULL. Returns 0 on success. */
PCORE_API int PCore_NodeBox(HANDLE hDoc, const char *tag,
                            int *x, int *y, int *w, int *h);

/* Inspect the first laid-out form control named `tag`. kind is 1 for a
 * checkbox and 2 for a radio button. `selected` and `disabled` receive 0/1.
 * This diagnostics API verifies the DOM -> box gadget state carried into
 * NetSurf redraw without exposing the internal form_control structure. */
PCORE_API int PCore_NodeFormControlState(HANDLE hDoc, const char *tag,
                                        int *kind, int *selected,
                                        int *disabled);

/* Inspect a form control in box-tree order. Geometry is in absolute document
 * CSS px; kind is 1 checkbox, 2 radio, 3 single-line text, 4 password,
 * 5 textarea, 6 select, 7 submit, 8 reset, 9 ordinary button and 10 file. */
PCORE_API int PCore_FormControlInfo(HANDLE hDoc, unsigned int index,
                                   int *x, int *y, int *w, int *h,
                                   int *kind, int *selected, int *disabled);

/* Inspect a laid-out form control by its UTF-8 DOM id. The returned geometry
 * and kind use the same document-space coordinates and kind values as
 * PCore_FormControlInfo. Returns 0 for a matching form gadget and non-zero
 * when the id is absent, not laid out, or not a form control. */
PCORE_API int PCore_FormControlInfoById(HANDLE hDoc, const char *element_id,
                                       int *x, int *y, int *w, int *h,
                                       int *kind, int *selected,
                                       int *disabled);

/* Resolve a file gadget at a document-space point. `file_index` is the
 * file-only index accepted by PCore_FileInputInfo/SetPath. A disabled gadget
 * still consumes the point. Returns 1 for a file gadget and 0 otherwise. */
PCORE_API int PCore_FileInputAt(HANDLE hDoc, int x, int y,
                               unsigned int *file_index, int *disabled);

typedef struct PCoreFileInputInfo {
    int x;
    int y;
    int width;
    int height;
    int disabled;
    int value_bytes;
    int path_bytes;
} PCoreFileInputInfo;

/* Read one file input in file-only box-tree order. `value` is the UTF-8
 * display filename and `path` is the retained UTF-8 local path. Returns 0 on
 * success. Probe calls may pass NULL buffers; sizes exclude the terminating
 * NUL. */
PCORE_API int PCore_FileInputInfo(HANDLE hDoc, unsigned int file_index,
                                 PCoreFileInputInfo *out_info,
                                 char *value, int value_capacity,
                                 char *path, int path_capacity);

/* Store a selected file. The display value and raw local path are kept
 * separately, following NetSurf's file-gadget model. Both strings are UTF-8.
 * Returns 0 on success. */
PCORE_API int PCore_FileInputSetPath(HANDLE hDoc, unsigned int file_index,
                                    const char *value, const char *path);

typedef struct PCoreTextInputInfo {
    int x;
    int y;
    int width;
    int height;
    int password;
    int read_only;
    int disabled;
    int max_length;
    int value_bytes;
} PCoreTextInputInfo;

/* Enumerate text/password/textarea controls independently of other form
 * controls. Geometry is the border box in absolute document CSS px. `value`
 * receives UTF-8 and is always NUL-terminated when cap > 0. max_length is -1
 * when the element has no limit. This is the platform bridge used by a WM
 * embedder to place native EDIT children over NetSurf's retained box. */
PCORE_API int PCore_TextInputInfo(HANDLE hDoc, unsigned int index,
                                 PCoreTextInputInfo *out_info,
                                 char *value, int cap);

/* Query whether an enumerated text control is a multiline textarea without
 * changing the next94 PCoreTextInputInfo structure layout. */
PCORE_API int PCore_TextInputIsMultiline(HANDLE hDoc, unsigned int index,
                                        int *multiline);

/* Replace one text control's live UTF-8 value and synchronise it to libdom.
 * Textarea CRLF/CR is normalised to LF. Returns 0 on success, 1 for a
 * missing/non-text control, 2 when the control is disabled/read-only, and 3
 * for invalid UTF-8 or maxlength excess. The DOM value survives later
 * style/layout passes. */
PCORE_API int PCore_TextInputSetValue(HANDLE hDoc, unsigned int index,
                                     const char *value);

typedef struct PCoreSelectInfo {
    int x;
    int y;
    int width;
    int height;
    int disabled;
    int multiple;
    int option_count;
    int selected_count;
    int selected_index;
} PCoreSelectInfo;

/* Enumerate select controls independently of other form controls. Geometry
 * is the border box in absolute document CSS px. selected_index is -1 when
 * no option is selected or when a multiple select has more than one
 * selection. */
PCORE_API int PCore_SelectInfo(HANDLE hDoc, unsigned int index,
                              PCoreSelectInfo *out_info);

/* Inspect one option. Label and value are UTF-8 and NUL-terminated whenever
 * the corresponding capacity is greater than zero. */
PCORE_API int PCore_SelectOptionInfo(HANDLE hDoc, unsigned int select_index,
                                    unsigned int option_index,
                                    char *label, int label_cap,
                                    char *value, int value_cap,
                                    int *selected, int *disabled,
                                    int *label_bytes, int *value_bytes);

/* Set one option's selected state and synchronise it to libdom. A single
 * select automatically clears its other options. Returns 0 on success,
 * 1 for a missing select/option, and 2 for a disabled select/option. */
PCORE_API int PCore_SelectSetOptionSelected(HANDLE hDoc,
                                            unsigned int select_index,
                                            unsigned int option_index,
                                            int selected);

/* Inspect one table cell's used border after collapsed-border conflict
 * resolution. Cells and sides are in document order; side uses CSS order
 * 0=top, 1=right, 2=bottom, 3=left. `style` receives the libcss border-style
 * value, `argb` is 0xAARRGGBB and `width` is CSS px. This is a read-only
 * diagnostics API and does not alter layout or redraw. */
PCORE_API int PCore_TableCellBorder(HANDLE hDoc, unsigned int cell_index,
                                   int side, int *style,
                                   unsigned long *argb, int *width);

typedef struct PCoreTableCellGeometry {
    int cell_x;
    int cell_y;
    int cell_width;
    int cell_height;
    int first_text_x;
    int first_text_y;
    int first_text_width;
    int first_text_height;
} PCoreTableCellGeometry;

/* Inspect table-cell and first visible text geometry in absolute page CSS px.
 * This additive diagnostics API is used to verify vertical alignment without
 * exposing NetSurf's internal box tree. */
PCORE_API int PCore_TableCellGeometry(HANDLE hDoc, unsigned int cell_index,
                                     PCoreTableCellGeometry *out_geometry);

/* Read the table-cell vertical alignment carried by the final box style.
 * kind is 1 top, 2 middle, 3 bottom, and 0 for baseline/other. */
PCORE_API int PCore_TableCellVerticalAlign(HANDLE hDoc,
                                          unsigned int cell_index,
                                          int *kind);

typedef struct PCoreTableRowGeometry {
    int row_x;
    int row_y;
    int row_width;
    int row_height;
} PCoreTableRowGeometry;

/* Inspect a table row in document order after layout. This diagnostics API
 * verifies table-height distribution without exposing internal box pointers. */
PCORE_API int PCore_TableRowGeometry(HANDLE hDoc, unsigned int row_index,
                                    PCoreTableRowGeometry *out_geometry);

/* Read a final row box's specified height. kind is 0 auto, 1 percentage,
 * and 2 another specified length; percentage value 25 means 25%. */
PCORE_API int PCore_TableRowSpecifiedHeight(HANDLE hDoc,
                                           unsigned int row_index,
                                           int *kind, int *value);

/* Read back a laid-out list marker by document order. `text` receives the
 * marker's UTF-8 bytes and is always NUL-terminated when cap > 0. The
 * geometry is in absolute page CSS px. Returns non-zero when the marker does
 * not exist or layout has not run. This is a diagnostics/inspection API. */
PCORE_API int PCore_ListMarker(HANDLE hDoc, unsigned int index,
                              char *text, int cap,
                              int *x, int *y, int *w, int *h);

typedef struct PCoreListItemGeometry {
    int item_x;
    int item_y;
    int item_width;
    int item_height;
    int marker_x;
    int marker_y;
    int marker_width;
    int marker_height;
    int first_text_x;
    int first_text_y;
    int wrapped_text_x;
    int wrapped_text_y;
} PCoreListItemGeometry;

/* Inspect list-item flow geometry. Wrapped text coordinates are -1 when the
 * item has no second text line. This additive API is intended for layout
 * diagnostics and does not expose internal NetSurf boxes. */
PCORE_API int PCore_ListItemGeometry(HANDLE hDoc, unsigned int index,
                                    PCoreListItemGeometry *out_geometry);

/* --- Painting (engine layer 4, milestone C) ------------------------- */

/* Paint the laid-out document into a GDI device context through NetSurf's
 * html_redraw and the Positron GDI plotter. Must be called after
 * PCore_StyleDocument + PCore_LayoutDocument. scroll_x/scroll_y are device
 * pixel document offsets and shift the page beneath the viewport. The
 * application owns the window and message loop and calls this from WM_PAINT. */
PCORE_API void PCore_PaintDocument(HANDLE hDoc, HDC hdc,
                                   int scroll_x, int scroll_y);

/* Total laid-out document height in device px (the value is from the most
 * recent PCore_LayoutDocument). Lets the application size a scrollbar. */
PCORE_API int PCore_DocumentHeight(HANDLE hDoc);

/* Set the rendering viewport: CSS-px width/height (used for vw/vh units and
 * the initial containing block) and the device DPI. Call before styling /
 * layout; defaults are 800x600 @ 96 dpi. The app should pass the real screen
 * size + DPI so styling and layout adapt to the device. A zero/negative
 * argument leaves that field unchanged. */
PCORE_API void PCore_SetViewport(int css_width, int css_height, int dpi);

/* Set a device-backed viewport. `device_width`/`device_height` are physical
 * pixels supplied by the host window and `dpi` is the device DPI. The core
 * derives the CSS viewport (device * 96 / dpi) for media queries and vw/vh,
 * then the next PCore_LayoutDocument uses the original device extent for
 * NetSurf layout and GDI painting. This is the correct entry point for a
 * resizable WM window; PCore_SetViewport remains the explicit CSS-pixel API
 * for engine tests and callers that already own the conversion. */
PCORE_API void PCore_SetDeviceViewport(int device_width, int device_height,
                                       int dpi);

/* --- Links / navigation (engine layer 4, inline milestone) ---------- */

/* Hit-test a document-space point (device px, i.e. client coordinate + the
 * current scroll offset) against the laid-out inline link fragments. If a link
 * covers the point, writes its (UTF-8, NUL-terminated, possibly truncated to
 * `cap`) href into `out_href` and returns 1; otherwise returns 0 and leaves
 * `out_href` untouched. Must be called after PCore_LayoutDocument. Lets the
 * application turn a tap into a navigation. */
PCORE_API int PCore_LinkAt(HANDLE hDoc, int x, int y,
                           char *out_href, int cap);

/* Activate a checkbox/radio at a document-space point. The control consumes
 * the click even when disabled or already-selected. Changed controls are
 * synchronised to libdom so later re-layouts retain their state. A union
 * dirty rectangle is returned in document CSS px; its width/height are zero
 * when no pixels changed. Returns 1 when a form control consumed the point. */
PCORE_API int PCore_FormActivateAt(HANDLE hDoc, int x, int y,
                                  int *dirty_x, int *dirty_y,
                                  int *dirty_w, int *dirty_h);

/* Dynamic CSS selector state supplied by an embedder. Focus, active and hover
 * nodes are document-owned references consumed by the normal libcss selector
 * callbacks during the next PCore_StyleDocument[Ex] pass. SetAt resolves the
 * nearest enabled form control, link or label for focus/active, and the
 * nearest element for hover, at document CSS coordinates. Clear removes the
 * requested state. Return 1 when state changed, 0 when it was already equal/
 * no interactive target existed, and -1 on invalid input. */
#define PCORE_INTERACTION_FOCUS  0x01
#define PCORE_INTERACTION_ACTIVE 0x02
#define PCORE_INTERACTION_HOVER  0x04
PCORE_API int PCore_InteractionSetAt(HANDLE hDoc, int x, int y,
                                    unsigned int state_flags);
PCORE_API int PCore_InteractionClear(HANDLE hDoc,
                                    unsigned int state_flags);

/* DOM event bridge. Listener callbacks run synchronously on the dispatching
 * thread and return a bitwise combination of PCORE_EVENT_ACTION_* values.
 * Element ids and event types are UTF-8. The opaque listener handle remains
 * owned by the document until explicitly removed or the document is freed.
 * Phase values intentionally match DOM CAPTURING/AT_TARGET/BUBBLING. */
#define PCORE_EVENT_PHASE_CAPTURING 1u
#define PCORE_EVENT_PHASE_TARGET    2u
#define PCORE_EVENT_PHASE_BUBBLING  3u
#define PCORE_EVENT_ACTION_NONE              0x00u
#define PCORE_EVENT_ACTION_PREVENT_DEFAULT   0x01u
#define PCORE_EVENT_ACTION_STOP_PROPAGATION  0x02u
#define PCORE_EVENT_ACTION_STOP_IMMEDIATE    0x04u
typedef struct PCoreKeyEventData {
    const char *key;
    unsigned int key_code;
    unsigned int char_code;
    int repeat;
    int shift;
    int ctrl;
    int alt;
} PCoreKeyEventData;
typedef struct PCoreKeyEventDataEx {
    unsigned int struct_size;
    const char *key;
    unsigned int key_code;
    unsigned int char_code;
    int repeat;
    int shift;
    int ctrl;
    int alt;
    int is_composing;
} PCoreKeyEventDataEx;
typedef struct PCoreInputEventData {
    const char *input_type;
    const char *data;
} PCoreInputEventData;
/* Size-tagged extension for callers that need composition state without
 * changing the ABI of PCoreInputEventData. Set struct_size to sizeof(*data). */
typedef struct PCoreInputEventDataEx {
    unsigned int struct_size;
    const char *input_type;
    const char *data;
    int is_composing;
} PCoreInputEventDataEx;
typedef struct PCoreEventInfo {
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
    /* UTF-8 IDs are borrowed for the synchronous callback only. They are
     * NULL when the DOM event target has no element id. */
    const char *target_id;
    const char *current_target_id;
} PCoreEventInfo;
typedef unsigned int (*PCoreEventListenerFn)(void *pw,
                                             const PCoreEventInfo *event_info);

PCORE_API HANDLE PCore_EventListenerAdd(HANDLE hDoc,
                                        const char *element_id,
                                        const char *event_type,
                                        int capture,
                                        PCoreEventListenerFn callback,
                                        void *pw);
PCORE_API int PCore_EventListenerRemove(HANDLE hDoc, HANDLE hListener);

/* Dispatch a trusted generic event either to an element id or to the nearest
 * laid-out element at document coordinates. Return 1 when dispatched, 0 when
 * no target exists and -1 on invalid input/DOM failure. default_allowed is
 * initialised to 1 and becomes 0 only when a cancelable event was canceled. */
PCORE_API int PCore_EventDispatchToId(HANDLE hDoc, const char *element_id,
                                     const char *event_type, int bubbles,
                                     int cancelable, int *default_allowed);
PCORE_API int PCore_EventDispatchAt(HANDLE hDoc, int x, int y,
                                   const char *event_type, int bubbles,
                                   int cancelable, int *default_allowed);
/* Dispatch a trusted keyboard event with host-provided key metadata. The
 * metadata is valid only during synchronous listener callbacks. */
PCORE_API int PCore_EventDispatchKeyToId(HANDLE hDoc,
                                        const char *element_id,
                                        const char *event_type, int bubbles,
                                        int cancelable,
                                        const PCoreKeyEventData *key_data,
                                        int *default_allowed);
PCORE_API int PCore_EventDispatchKeyAt(HANDLE hDoc, int x, int y,
                                      const char *event_type, int bubbles,
                                      int cancelable,
                                      const PCoreKeyEventData *key_data,
                                      int *default_allowed);
PCORE_API int PCore_EventDispatchKeyExToId(HANDLE hDoc,
                                          const char *element_id,
                                          const char *event_type,
                                          int bubbles, int cancelable,
                                          const PCoreKeyEventDataEx *data,
                                          int *default_allowed);
PCORE_API int PCore_EventDispatchKeyExAt(HANDLE hDoc, int x, int y,
                                        const char *event_type,
                                        int bubbles, int cancelable,
                                        const PCoreKeyEventDataEx *data,
                                        int *default_allowed);
/* Dispatch a trusted beforeinput-style event with host-provided input
 * metadata. The metadata is valid only during synchronous listener callbacks. */
PCORE_API int PCore_EventDispatchInputToId(HANDLE hDoc,
                                          const char *element_id,
                                          const char *event_type, int bubbles,
                                          int cancelable,
                                          const PCoreInputEventData *input_data,
                                          int *default_allowed);
PCORE_API int PCore_EventDispatchInputAt(HANDLE hDoc, int x, int y,
                                        const char *event_type, int bubbles,
                                        int cancelable,
                                        const PCoreInputEventData *input_data,
                                        int *default_allowed);
/* Extended input dispatch. Old callers keep using the ABI-stable functions
 * above and observe is_composing == 0 in listener callbacks. */
PCORE_API int PCore_EventDispatchInputExToId(HANDLE hDoc,
                                            const char *element_id,
                                            const char *event_type,
                                            int bubbles, int cancelable,
                                            const PCoreInputEventDataEx *data,
                                            int *default_allowed);
PCORE_API int PCore_EventDispatchInputExAt(HANDLE hDoc, int x, int y,
                                          const char *event_type,
                                          int bubbles, int cancelable,
                                          const PCoreInputEventDataEx *data,
                                          int *default_allowed);

/* Resolve a click on an explicit for=id or wrapping <label> to its visible
 * form gadget. The target point and kind use the same document coordinates
 * and kind values as PCore_FormControlInfo. Returns 1 for a resolved label.
 * The embedder performs the target's normal activation/focus action. */
PCORE_API int PCore_LabelTargetAt(HANDLE hDoc, int x, int y,
                                 int *target_x, int *target_y,
                                 int *target_kind);

typedef struct PCoreFormSubmissionInfo {
    int method;
    int action_bytes;
    int body_bytes;
} PCoreFormSubmissionInfo;

/* Constraint-validation result for one form submission target. The first
 * invalid control uses PCore_FormControlInfo kind values and absolute
 * document CSS-px geometry so a platform host can reveal and focus it. */
#define PCORE_VALIDITY_VALUE_MISSING 0x0001u
#define PCORE_VALIDITY_TOO_LONG     0x0002u
#define PCORE_VALIDITY_TOO_SHORT    0x0004u
#define PCORE_VALIDITY_PATTERN_MISMATCH 0x0008u
typedef struct PCoreFormValidationInfo {
    int valid;
    int invalid_count;
    int first_control_kind;
    int first_x;
    int first_y;
    int first_width;
    int first_height;
    unsigned int first_flags;
} PCoreFormValidationInfo;

/* Validate the form targeted by an explicit submit control or by Enter in an
 * enumerated single-line text/password control. The current implementation
 * covers required, pattern and length constraints for text/password/textarea/file,
 * checkbox, radio groups and select controls, including disabled/read-only
 * and no-validate bypasses. Unsupported or malformed constraint attributes
 * are ignored conservatively; pattern support is the documented ASCII subset
 * in pcore_pattern.c.
 * Returns 1 when a target form was checked and 0 when no qualifying target
 * exists or the DOM could not be inspected. */
PCORE_API int PCore_FormValidationAt(HANDLE hDoc, int x, int y,
                                    PCoreFormValidationInfo *out_info);
PCORE_API int PCore_FormValidationForTextInput(HANDLE hDoc,
                                    unsigned int text_index,
                                    PCoreFormValidationInfo *out_info);

/* Build the application/x-www-form-urlencoded successful-control set for a
 * submit button at a document-space point. method is 1 for GET, 2 for an
 * urlencoded POST and 3 for unsupported multipart POST. action/body receive
 * UTF-8 and are NUL-terminated when their capacities are positive.
 *
 * Return values:
 *   0: no button at the point
 *   1: submission is complete
 *   2: a disabled/reset/ordinary button consumed the point
 *   3: multipart/file submission is not implemented
 *   4: DOM/allocation/output-buffer failure
 *   5: constraint validation blocked submission
 *
 * The successful-control policy follows NetSurf form.c: disabled or unnamed
 * controls and unchecked checkbox/radio inputs are skipped, selected options
 * produce one pair each, and only the activated submit button is included. */
PCORE_API int PCore_FormSubmissionAt(HANDLE hDoc, int x, int y,
                                    PCoreFormSubmissionInfo *out_info,
                                    char *action, int action_capacity,
                                    char *body, int body_capacity);

/* Build a submission for the form owning one enumerated single-line text or
 * password control, as when that native EDIT receives Enter. NetSurf's policy
 * is followed: the first enabled submit control is successful when no button
 * was explicitly clicked. A textarea never triggers implicit submission.
 * Return values and output-buffer rules match PCore_FormSubmissionAt. */
PCORE_API int PCore_FormSubmissionForTextInput(HANDLE hDoc,
                                    unsigned int text_index,
                                    PCoreFormSubmissionInfo *out_info,
                                    char *action, int action_capacity,
                                    char *body, int body_capacity);

typedef struct PCoreMultipartSubmissionInfo {
    int action_bytes;
    unsigned int part_count;
} PCoreMultipartSubmissionInfo;

typedef struct PCoreMultipartPartInfo {
    int kind;
    int name_bytes;
    int value_bytes;
    int path_bytes;
} PCoreMultipartPartInfo;

/* Capture the successful-control set of a multipart/form-data POST without
 * exposing NetSurf's fetch_multipart_data ABI. kind is 1 for a normal value
 * and 2 for a file; file `value` is the submitted filename while `path` is
 * the local file to read. The snapshot owns copied strings and remains valid
 * until PCore_FreeMultipartSubmission.
 *
 * The At variant accepts an enabled submit button. The ForTextInput variant
 * performs NetSurf's first-submit implicit selection for a single-line native
 * EDIT. NULL means no qualifying multipart form or an allocation/DOM error. */
PCORE_API HANDLE PCore_MultipartSubmissionAt(HANDLE hDoc, int x, int y);
PCORE_API HANDLE PCore_MultipartSubmissionForTextInput(HANDLE hDoc,
                                                       unsigned int text_index);
PCORE_API int PCore_MultipartSubmissionInfo(HANDLE hSubmission,
                                    PCoreMultipartSubmissionInfo *out_info,
                                    char *action, int action_capacity);
PCORE_API int PCore_MultipartPartInfo(HANDLE hSubmission,
                                    unsigned int part_index,
                                    PCoreMultipartPartInfo *out_info,
                                    char *name, int name_capacity,
                                    char *value, int value_capacity,
                                    char *path, int path_capacity);
PCORE_API void PCore_FreeMultipartSubmission(HANDLE hSubmission);

/* Perform the default action for a reset button at a document-space point.
 * Initial values/checks/selections saved by libdom are restored in the DOM.
 * The embedder must re-layout afterward so NetSurf gadgets and native controls
 * are rebuilt from that state. Returns 0 when no reset button was hit, 1 on
 * reset, 2 when a disabled/unowned reset consumed the point, and 3 on a DOM
 * failure. */
PCORE_API int PCore_FormResetAt(HANDLE hDoc, int x, int y);

/* Forward pointer input to a nested CSS overflow scrollbar. Coordinates use
 * the same document-space convention as PCore_LinkAt. DOWN performs arrow or
 * page steps, MOVE drags a scrollbar thumb, and UP ends the drag. Returns 1
 * when an overflow scrollbar consumed the event. */
#define PCORE_POINTER_DOWN 1
#define PCORE_POINTER_MOVE 2
#define PCORE_POINTER_UP   3
PCORE_API int PCore_OverflowPointer(HANDLE hDoc, int action, int x, int y);

/* Return the document-space viewport that must be repainted after the most
 * recent overflow-scrollbar pointer event. This includes the scrolled content
 * and its retained scrollbar, but excludes unrelated page content. */
PCORE_API int PCore_OverflowDirtyRect(HANDLE hDoc,
        int *x, int *y, int *w, int *h);

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
