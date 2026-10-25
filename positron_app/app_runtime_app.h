/* EXE owner-thread scheduling, not Runtime product semantics. */
#ifndef POSITRON_APP_RUNTIME_APP_H
#define POSITRON_APP_RUNTIME_APP_H
#include "app_runtime_platform.h"
typedef struct AppRuntimeApp AppRuntimeApp;
#define APP_RUNTIME_OPENING 0
#define APP_RUNTIME_OPEN 1
#define APP_RUNTIME_FAILED 2

AppRuntimeApp *AppRuntimeApp_Start(const char *directory);
int AppRuntimeApp_State(AppRuntimeApp *app);
int AppRuntimeApp_Result(AppRuntimeApp *app);
/* UI only, after OPEN; copied metadata and a separate lexical resolver. */
int AppRuntimeApp_Accept(AppRuntimeApp *app, PRuntimeManifestInfo *info,
        char *entry, unsigned int capacity);
int AppRuntimeApp_Resolve(AppRuntimeApp *app, const char *base,
        const char *reference, char *url, int capacity);
/* Navigation worker only. Serial, copied I/O request; never UI/Core/JS. */
int AppRuntimeApp_Read(AppRuntimeApp *app, const char *base,
        const char *reference, void *buffer, unsigned int capacity,
        unsigned int *bytes, PRuntimeResourceReference *resolved);
/* After all navigation workers have joined, request close then poll UI. */
void AppRuntimeApp_RequestClose(AppRuntimeApp *app);
int AppRuntimeApp_TryDestroy(AppRuntimeApp **app);
#endif
