/*
 * positron_app/app_url_router.h - private scheme classification and network
 * reference routing for the WM6 application shell.
 *
 * This is not a public URL ABI.  The application owns scheme dispatch:
 * positron:// is local to the EXE, http(s) references are delegated to
 * positron_http.dll, and unsupported explicit schemes fail closed until an
 * application-owned handler is added.
 */

#ifndef POSITRON_APP_URL_ROUTER_H
#define POSITRON_APP_URL_ROUTER_H

typedef enum AppUrlSchemeKind {
    APP_URL_SCHEME_RELATIVE = 0,
    APP_URL_SCHEME_POSITRON,
    APP_URL_SCHEME_HTTP,
    APP_URL_SCHEME_HTTPS,
    APP_URL_SCHEME_OTHER
} AppUrlSchemeKind;

AppUrlSchemeKind AppUrlRouter_ClassifyScheme(const char *url);
int AppUrlRouter_ResolveNetworkReference(const char *base_url,
        const char *reference, char *out_url, int out_capacity);

#endif /* POSITRON_APP_URL_ROUTER_H */
