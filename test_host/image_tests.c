/*
 * test_host/image_tests.c - offline image-resource callback contracts.
 *
 * This fixture is a consumer-side regression for the versioned Core image
 * fetch entry point.  It deliberately separates a resource that is pending
 * from one that is terminally unavailable, then proves that a later scan can
 * decode the pending SVG without retrying the terminal failure.
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "positron_core.h"

typedef struct image_pending_retry_fixture {
    int calls;
    int pending_calls;
    int terminal_calls;
    int frees;
} image_pending_retry_fixture;

static const char g_pending_retry_svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" "
        "height=\"60\"><rect width=\"120\" height=\"60\" "
        "fill=\"#2040a0\"/></svg>";

static int image_pending_retry_fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    image_pending_retry_fixture *fixture;
    char *copy;
    int len;

    fixture = (image_pending_retry_fixture *) pw;
    if (fixture == NULL || url == NULL || out_data == NULL ||
            out_len == NULL) {
        return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
    }
    *out_data = NULL;
    *out_len = 0;
    fixture->calls++;
    if (strcmp(url, "/img/pending.svg") == 0) {
        fixture->pending_calls++;
        if (fixture->pending_calls == 1) {
            return PCORE_IMAGE_FETCH_PENDING;
        }
        len = (int) sizeof(g_pending_retry_svg) - 1;
        copy = (char *) malloc((size_t) len);
        if (copy == NULL) {
            return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
        }
        memcpy(copy, g_pending_retry_svg, (size_t) len);
        *out_data = copy;
        *out_len = len;
        return PCORE_IMAGE_FETCH_READY;
    }
    if (strcmp(url, "/img/terminal.svg") == 0) {
        fixture->terminal_calls++;
        return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
    }
    return PCORE_IMAGE_FETCH_TERMINAL_FAIL;
}

static void image_pending_retry_free(void *pw, char *data)
{
    image_pending_retry_fixture *fixture;

    fixture = (image_pending_retry_fixture *) pw;
    if (fixture != NULL) {
        fixture->frees++;
    }
    free(data);
}

/* TEST 1313 - asynchronous image pending/retry and terminal failure. */
BOOL test1313_core_image_pending_retry_contract(void)
{
    static const char HTML[] =
            "<!doctype html><html><body>"
            "<img id='pending' src='/img/pending.svg' alt='pending'>"
            "<img id='terminal' src='/img/terminal.svg' alt='terminal'>"
            "</body></html>";
    static const char CSS[] =
            "html,body{margin:0;padding:0;}"
            "img{display:block;width:120px;height:60px;}";
    HANDLE document;
    HANDLE sheet;
    image_pending_retry_fixture fixture;
    PCoreImageDecodeStats image_stats;
    int found;
    int fetched;
    int number;
    int x;
    int y;
    int width;
    int height;
    BOOL ok;

    document = NULL;
    sheet = NULL;
    memset(&fixture, 0, sizeof(fixture));
    memset(&image_stats, 0, sizeof(image_stats));
    found = 0;
    fetched = 0;
    number = 0;
    x = 0;
    y = 0;
    width = 0;
    height = 0;
    ok = FALSE;

    document = PCore_ParseHTML(HTML, sizeof(HTML) - 1);
    if (document == NULL) {
        goto cleanup;
    }
    if (PCore_FetchImageResourcesEx(document, image_pending_retry_fetch,
            image_pending_retry_free, &fixture, &found, &fetched) != 0 ||
            found != 2 || fetched != 0 || fixture.calls != 2 ||
            fixture.pending_calls != 1 || fixture.terminal_calls != 1 ||
            fixture.frees != 0) {
        goto cleanup;
    }
    if (PCore_NodeRelationById(document, "pending",
            PCORE_NODE_RELATION_IMAGE_COMPLETE, 0, NULL, 0, NULL,
            &number) != 0 || number != 0 ||
            PCore_NodeRelationById(document, "terminal",
            PCORE_NODE_RELATION_IMAGE_COMPLETE, 0, NULL, 0, NULL,
            &number) != 0 || number != 1) {
        goto cleanup;
    }

    /* The worker has completed between scans; the pending URL must be
     * requested again while the terminal URL remains cached as failed. */
    if (PCore_FetchImageResourcesEx(document, image_pending_retry_fetch,
            image_pending_retry_free, &fixture, &found, &fetched) != 0 ||
            found != 2 || fetched != 1 || fixture.calls != 3 ||
            fixture.pending_calls != 2 || fixture.terminal_calls != 1 ||
            fixture.frees != 1) {
        goto cleanup;
    }
    sheet = PCore_ParseCSS(CSS, sizeof(CSS) - 1,
            "https://positron.local/image-pending.css");
    if (sheet == NULL || PCore_StyleDocument(document, sheet) != 0) {
        goto cleanup;
    }
    PCore_SetViewport(240, 160, 96);
    if (PCore_LayoutDocument(document, 240, 160) != 0 ||
            PCore_NodeBox(document, "img", &x, &y, &width, &height) != 0 ||
            width != 120 || height != 60 ||
            PCore_NodeRelationById(document, "pending",
            PCORE_NODE_RELATION_IMAGE_NATURAL_WIDTH, 0, NULL, 0, NULL,
            &number) != 0 || number != 120 ||
            PCore_NodeRelationById(document, "pending",
            PCORE_NODE_RELATION_IMAGE_COMPLETE, 0, NULL, 0, NULL,
            &number) != 0 || number != 1 ||
            PCore_GetImageDecodeStats(document, &image_stats) != 0 ||
            image_stats.svg_creates == 0) {
        goto cleanup;
    }
    if (PCore_FetchImageResourcesEx(document, image_pending_retry_fetch,
            image_pending_retry_free, &fixture, &found, &fetched) != 0 ||
            found != 2 || fetched != 1 || fixture.calls != 3 ||
            fixture.pending_calls != 2 || fixture.terminal_calls != 1 ||
            fixture.frees != 1) {
        goto cleanup;
    }
    ok = TRUE;

cleanup:
    if (sheet != NULL) {
        PCore_FreeStylesheet(sheet);
    }
    if (document != NULL) {
        PCore_FreeDocument(document);
    }
    return ok;
}
