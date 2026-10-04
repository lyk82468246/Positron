/* Internal Browser-owned application service state; not a public ABI. */
#ifndef P_BROWSER_SERVICES_H
#define P_BROWSER_SERVICES_H
typedef struct p_browser_services p_browser_services;
int p_services_busy(const p_browser_services *services);
int p_services_register(HANDLE runtime, const PBrowserServiceOptions *options,
        p_browser_services **out_services);
int p_services_revoke(p_browser_services *services);
void p_services_destroy(p_browser_services *services);
int p_services_complete(p_browser_services *services,
        const PBrowserServiceToken *token, int succeeded,
        const char *json, unsigned long bytes);
int p_services_pump(p_browser_services *services, unsigned long limit,
        unsigned long *out_delivered);
#endif
