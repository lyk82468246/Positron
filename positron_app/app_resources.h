/*
 * positron_app/app_resources.h - private resource transaction adapter.
 */

#ifndef POSITRON_APP_RESOURCES_H
#define POSITRON_APP_RESOURCES_H

#include "app_host.h"

struct AppNavigationResource {
    AppNavigationResource *next;
    int index;
    char url[APP_HOST_URL_MAX];
};

void AppResources_DestroyRequest(AppNavigationRequest *request);
int AppResources_Register(AppNavigationRequest *request, const char *url,
        int required, unsigned int role_mask, int *out_index);
int AppResources_GetInfo(AppNavigationRequest *request,
        AppNavigationResource *resource,
        PBrowserNavigationResourceInfo *out_info);
int AppResources_FindPending(AppNavigationRequest *request,
        AppNavigationResource **out_resource);
int AppResources_PendingCount(AppNavigationRequest *request);
int AppResources_Fetch(void *pw, const char *url,
        char **out_data, int *out_len);
void AppResources_Free(void *pw, char *data);
int AppResources_Resolve(void *pw, const char *base_url,
        const char *reference, char *out_url, int out_capacity);
int AppResources_ResolveTransport(AppNavigationRequest *request,
        const char *reference, char *out_host, int out_host_capacity,
        char *out_path, int out_path_capacity, int *out_port);

#endif /* POSITRON_APP_RESOURCES_H */
