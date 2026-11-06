#ifndef POSITRON_APP_RUNTIME_INSTALL_H
#define POSITRON_APP_RUNTIME_INSTALL_H
#include "app_runtime_platform.h"
#include "app_runtime_registry.h"
#define APP_RUNTIME_DEVELOPMENT 0
#define APP_RUNTIME_INSTALL_DIRECTORY 1
#define APP_RUNTIME_INSTALLED 2
#define APP_RUNTIME_INSTALL_PXI 3
typedef struct AppRuntimeInstall AppRuntimeInstall;
/* Worker-only scheduler; never transfers a live Runtime/DB handle to UI. */
int AppRuntimeInstall_Open(AppRuntimePlatform *platform,AppSettingsStore *store,
        int kind,const char *target,int (*cancelled)(void *),void *user,
        AppRuntimeInstall **output);
int AppRuntimeInstall_Action(AppRuntimeInstall *install); /* 0 ready; otherwise INTENT_* */
int AppRuntimeInstall_CopyMetadata(AppRuntimeInstall *install,PRuntimeManifestInfo *info,
        char *manifest,unsigned int capacity,unsigned int *bytes);
int AppRuntimeInstall_Confirm(AppRuntimeInstall *install);
PRuntimeStorageHandle AppRuntimeInstall_TakeStorage(AppRuntimeInstall *install);
int AppRuntimeInstall_Read(AppRuntimeInstall *install,const char *base,const char *reference,
        void *buffer,unsigned int capacity,unsigned int *bytes,PRuntimeResourceReference *resolved);
void AppRuntimeInstall_Close(AppRuntimeInstall **install);
#ifdef _DEBUG
void AppRuntimeInstall_DebugOptions(AppRuntimeInstall *install,int fault,unsigned int budget,int cancel_after_first);
int AppRuntimeInstall_DebugRun(void);
#endif
#endif
