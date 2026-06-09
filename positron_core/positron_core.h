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

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_CORE_H */
