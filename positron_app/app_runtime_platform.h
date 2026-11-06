/* WM6 platform operations only; Runtime owns package and storage semantics. */
#ifndef POSITRON_APP_RUNTIME_PLATFORM_H
#define POSITRON_APP_RUNTIME_PLATFORM_H
#include "positron_runtime.h"

typedef struct AppRuntimePlatform {
    WCHAR data_root[MAX_PATH];
    void *revision_root; /* worker-owned immutable native view */
#ifdef _DEBUG
    void (*capture_hook)(void *);
    void *capture_user;
#endif
} AppRuntimePlatform;

int AppRuntimePlatform_Init(AppRuntimePlatform *platform);
void AppRuntimePlatform_DirectoryProvider(AppRuntimePlatform *platform,
        PRuntimeDirectoryProvider *provider);
void AppRuntimePlatform_StorageProvider(AppRuntimePlatform *platform,
        PRuntimeStorageProvider *provider);
void AppRuntimePlatform_RevisionProvider(AppRuntimePlatform *platform,
        PRuntimeRevisionProvider *provider);
void AppRuntimePlatform_ExistingStorageProvider(AppRuntimePlatform *platform,
        PRuntimeInstalledStorageProvider *provider);
int AppRuntimePlatform_CopyManifest(AppRuntimePlatform *platform, void *buffer,
        unsigned int capacity, unsigned int *bytes);
#ifdef _DEBUG
int AppRuntimePlatform_DebugCheck(const WCHAR *directory);
#endif
#endif
