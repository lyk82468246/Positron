/*
 * positron_app/app_resources.c - private Browser/Core resource adapter.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_resources.h"
#include "positron_http.h"

static void app_resources_copy_text(char *target, int capacity,
        const char *source)
{
    int length;

    if (target == NULL || capacity <= 0) {
        return;
    }
    target[0] = '\0';
    if (source == NULL || capacity <= 1) {
        return;
    }
    length = (int) strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(target, source, (size_t) length);
    target[length] = '\0';
}

static AppNavigationResource *app_resources_find(
        AppNavigationRequest *request, const char *url)
{
    AppNavigationResource *resource;

    if (request == NULL || url == NULL) {
        return NULL;
    }
    for (resource = request->resources; resource != NULL;
            resource = resource->next) {
        if (strcmp(resource->url, url) == 0) {
            return resource;
        }
    }
    return NULL;
}

static AppNavigationResource *app_resources_find_index(
        AppNavigationRequest *request, int index)
{
    AppNavigationResource *resource;

    if (request == NULL || index < 0) {
        return NULL;
    }
    for (resource = request->resources; resource != NULL;
            resource = resource->next) {
        if (resource->index == index) {
            return resource;
        }
    }
    return NULL;
}

void AppResources_DestroyRequest(AppNavigationRequest *request)
{
    AppNavigationResource *resource;
    AppNavigationResource *next;

    if (request == NULL) {
        return;
    }
    resource = request->resources;
    while (resource != NULL) {
        next = resource->next;
        free(resource);
        resource = next;
    }
    request->resources = NULL;
    request->resource_count = 0;
}

int AppResources_Register(AppNavigationRequest *request, const char *url,
        int required, unsigned int role_mask, int *out_index)
{
    AppNavigationResource *resource;
    size_t length;
    int index;
    int rc;

    if (request == NULL || request->resource_transaction == NULL ||
            url == NULL || url[0] == '\0' || out_index == NULL) {
        if (request != NULL && required) {
            request->resource_registration_failed = 1;
        }
        return 1;
    }
    length = strlen(url);
    if (length == 0 || length >= APP_HOST_URL_MAX) {
        if (required) {
            request->resource_registration_failed = 1;
        }
        return 1;
    }
    resource = app_resources_find(request, url);
    if (resource != NULL) {
        rc = PBrowser_NavigationResourceRegister(
                request->resource_transaction, url, required, role_mask,
                &index);
        if (rc != PBROWSER_OK || index != resource->index) {
            if (required) {
                request->resource_registration_failed = 1;
            }
            return 1;
        }
        *out_index = resource->index;
        return 0;
    }
    if (request->resource_count >= PBROWSER_NAVIGATION_RESOURCE_MAX) {
        if (required) {
            request->resource_registration_failed = 1;
        }
        return 1;
    }
    rc = PBrowser_NavigationResourceRegister(
            request->resource_transaction, url, required, role_mask,
            &index);
    if (rc != PBROWSER_OK) {
        if (required) {
            request->resource_registration_failed = 1;
        }
        return 1;
    }
    resource = (AppNavigationResource *) malloc(sizeof(*resource));
    if (resource == NULL) {
        if (required) {
            request->resource_registration_failed = 1;
        }
        (void) PBrowser_NavigationResourceFail(
                request->resource_transaction, index,
                PBROWSER_NAVIGATION_FAILURE_MEMORY);
        return 1;
    }
    memset(resource, 0, sizeof(*resource));
    resource->index = index;
    app_resources_copy_text(resource->url, sizeof(resource->url), url);
    resource->next = request->resources;
    request->resources = resource;
    request->resource_count++;
    *out_index = index;
    return 0;
}

int AppResources_GetInfo(AppNavigationRequest *request,
        AppNavigationResource *resource,
        PBrowserNavigationResourceInfo *out_info)
{
    if (request == NULL || resource == NULL || out_info == NULL ||
            request->resource_transaction == NULL) {
        return 1;
    }
    memset(out_info, 0, sizeof(*out_info));
    out_info->size = sizeof(*out_info);
    return PBrowser_NavigationResourceGet(request->resource_transaction,
            resource->index, out_info) == PBROWSER_OK ? 0 : 1;
}

int AppResources_FindPending(AppNavigationRequest *request,
        AppNavigationResource **out_resource)
{
    AppNavigationResource *resource;
    PBrowserNavigationResourceInfo info;

    if (out_resource == NULL) {
        return 1;
    }
    *out_resource = NULL;
    if (request == NULL) {
        return 1;
    }
    for (resource = request->resources; resource != NULL;
            resource = resource->next) {
        if (AppResources_GetInfo(request, resource, &info) == 0 &&
                info.state == PBROWSER_NAVIGATION_RESOURCE_PENDING &&
                !info.attempted) {
            *out_resource = resource;
            return 0;
        }
    }
    return 1;
}

int AppResources_PendingCount(AppNavigationRequest *request)
{
    PBrowserNavigationResourceStats stats;

    if (request == NULL || request->resource_transaction == NULL) {
        return 0;
    }
    memset(&stats, 0, sizeof(stats));
    stats.size = sizeof(stats);
    if (PBrowser_NavigationResourceGetStats(
            request->resource_transaction, &stats) != PBROWSER_OK) {
        return 0;
    }
    return stats.resources_pending;
}

int AppResources_Fetch(void *pw, const char *url,
        char **out_data, int *out_len)
{
    AppNavigationRequest *request;
    AppNavigationResource *resource;
    PBrowserNavigationResourceInfo info;
    char *data;
    int index;
    int copied;

    request = (AppNavigationRequest *) pw;
    if (out_data == NULL || out_len == NULL) {
        return 1;
    }
    *out_data = NULL;
    *out_len = 0;
    if (request == NULL || url == NULL || url[0] == '\0' ||
            AppResources_Register(request, url, request->resource_policy,
            request->resource_role_mask, &index) != 0) {
        return 1;
    }
    resource = app_resources_find_index(request, index);
    if (resource == NULL || AppResources_GetInfo(request, resource, &info) != 0 ||
            info.state != PBROWSER_NAVIGATION_RESOURCE_READY ||
            info.data_bytes <= 0) {
        return 1;
    }
    data = (char *) malloc((size_t) info.data_bytes);
    if (data == NULL) {
        return 1;
    }
    copied = 0;
    if (PBrowser_NavigationResourceCopyData(
            request->resource_transaction, resource->index, data,
            info.data_bytes, &copied) != PBROWSER_OK ||
            copied != info.data_bytes) {
        free(data);
        return 1;
    }
    *out_data = data;
    *out_len = copied;
    return 0;
}

void AppResources_Free(void *pw, char *data)
{
    (void) pw;
    free(data);
}

int AppResources_Resolve(void *pw, const char *base_url,
        const char *reference, char *out_url, int out_capacity)
{
    (void) pw;
    if (reference == NULL || out_url == NULL || out_capacity <= 1) {
        return 1;
    }
    return PHttp_ResolveReferenceUrl(base_url, reference, out_url,
            out_capacity);
}

int AppResources_ResolveTransport(AppNavigationRequest *request,
        const char *reference, char *out_host, int out_host_capacity,
        char *out_path, int out_path_capacity, int *out_port)
{
    char base_host[APP_HOST_NAV_HOST_MAX];
    char base_path[APP_HOST_NAV_PATH_MAX];
    int base_port;

    if (request == NULL || reference == NULL || out_host == NULL ||
            out_path == NULL || out_port == NULL) {
        return 1;
    }
    base_host[0] = '\0';
    base_path[0] = '\0';
    base_port = 443;
    if (PHttp_ResolveReference(NULL, 443, NULL, request->url,
            base_host, sizeof(base_host), base_path, sizeof(base_path),
            &base_port) != 0) {
        return 1;
    }
    return PHttp_ResolveReference(base_host, base_port, base_path,
            reference, out_host, out_host_capacity, out_path,
            out_path_capacity, out_port);
}
