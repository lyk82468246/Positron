/* WM6 platform operations only; Runtime owns package and storage semantics. */
#ifndef POSITRON_APP_RUNTIME_PLATFORM_H
#define POSITRON_APP_RUNTIME_PLATFORM_H
#include "positron_runtime.h"

typedef struct AppRuntimePlatform {
    WCHAR data_root[MAX_PATH];
} AppRuntimePlatform;

int AppRuntimePlatform_Init(AppRuntimePlatform *platform);
void AppRuntimePlatform_DirectoryProvider(AppRuntimePlatform *platform,
        PRuntimeDirectoryProvider *provider);
void AppRuntimePlatform_StorageProvider(AppRuntimePlatform *platform,
        PRuntimeStorageProvider *provider);
#ifdef _DEBUG
int AppRuntimePlatform_DebugCheck(const WCHAR *directory);
#endif
#endif
