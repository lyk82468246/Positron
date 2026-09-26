/*
 * positron_app/app_url_router.c - private scheme classification and network
 * reference routing for the WM6 application shell.
 */

#include <string.h>

#include "app_url_router.h"
#include "positron_http.h"

static int app_url_ascii_prefix_equal(const char *value, const char *prefix)
{
    unsigned char value_char;
    unsigned char prefix_char;

    if (value == NULL || prefix == NULL) {
        return 0;
    }
    while (*prefix != '\0') {
        value_char = (unsigned char) *value++;
        if (value_char == '\0') {
            return 0;
        }
        prefix_char = (unsigned char) *prefix++;
        if (value_char >= 'A' && value_char <= 'Z') {
            value_char = (unsigned char) (value_char + ('a' - 'A'));
        }
        if (prefix_char >= 'A' && prefix_char <= 'Z') {
            prefix_char = (unsigned char) (prefix_char + ('a' - 'A'));
        }
        if (value_char != prefix_char) {
            return 0;
        }
    }
    return 1;
}

static int app_url_scheme_character(int character, int first)
{
    if ((character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z')) {
        return 1;
    }
    if (!first && ((character >= '0' && character <= '9') ||
            character == '+' || character == '-' || character == '.')) {
        return 1;
    }
    return 0;
}

AppUrlSchemeKind AppUrlRouter_ClassifyScheme(const char *url)
{
    const char *cursor;

    if (url == NULL) {
        return APP_URL_SCHEME_RELATIVE;
    }
    while (*url == ' ' || *url == '\t' || *url == '\r' ||
            *url == '\n' || *url == '\f' || *url == '\v') {
        url++;
    }
    if (!app_url_scheme_character((unsigned char) url[0], 1)) {
        return APP_URL_SCHEME_RELATIVE;
    }
    cursor = url + 1;
    while (app_url_scheme_character((unsigned char) *cursor, 0)) {
        cursor++;
    }
    if (*cursor != ':') {
        return APP_URL_SCHEME_RELATIVE;
    }
    if (app_url_ascii_prefix_equal(url, "positron:")) {
        return APP_URL_SCHEME_POSITRON;
    }
    if (app_url_ascii_prefix_equal(url, "http:")) {
        return APP_URL_SCHEME_HTTP;
    }
    if (app_url_ascii_prefix_equal(url, "https:")) {
        return APP_URL_SCHEME_HTTPS;
    }
    return APP_URL_SCHEME_OTHER;
}

int AppUrlRouter_ResolveNetworkReference(const char *base_url,
        const char *reference, char *out_url, int out_capacity)
{
    AppUrlSchemeKind base_scheme;
    AppUrlSchemeKind reference_scheme;

    if (reference == NULL || out_url == NULL || out_capacity <= 1) {
        return 1;
    }
    reference_scheme = AppUrlRouter_ClassifyScheme(reference);
    if (reference_scheme == APP_URL_SCHEME_POSITRON ||
            reference_scheme == APP_URL_SCHEME_OTHER) {
        return 1;
    }
    if (reference_scheme == APP_URL_SCHEME_HTTP ||
            reference_scheme == APP_URL_SCHEME_HTTPS) {
        /* An absolute network URL does not inherit a local positron:// base. */
        return PHttp_ResolveReferenceUrl(NULL, reference, out_url,
                out_capacity);
    }
    base_scheme = AppUrlRouter_ClassifyScheme(base_url);
    if (base_scheme == APP_URL_SCHEME_POSITRON ||
            base_scheme == APP_URL_SCHEME_OTHER) {
        return 1;
    }
    return PHttp_ResolveReferenceUrl(base_url, reference, out_url,
            out_capacity);
}
