/* EXE owner-thread scheduling, not Runtime product semantics. */
#ifndef POSITRON_APP_RUNTIME_APP_H
#define POSITRON_APP_RUNTIME_APP_H
#include "app_runtime_platform.h"
typedef struct AppRuntimeApp AppRuntimeApp;
typedef struct AppRuntimePage AppRuntimePage;
#define APP_RUNTIME_OPENING 0
#define APP_RUNTIME_OPEN 1
#define APP_RUNTIME_FAILED 2
#define APP_RUNTIME_STORAGE_OPENING 3
#define APP_RUNTIME_READY 4

AppRuntimeApp *AppRuntimeApp_Start(const char *directory);
int AppRuntimeApp_State(AppRuntimeApp *app);
int AppRuntimeApp_Result(AppRuntimeApp *app);
/* UI only, after explicit native confirmation; no DB call on this thread. */
int AppRuntimeApp_GrantSql(AppRuntimeApp *app);
int AppRuntimeApp_SqlReady(AppRuntimeApp *app);
int AppRuntimeApp_RequiresSql(AppRuntimeApp *app);
/* UI only, after OPEN; copied metadata and a separate lexical resolver. */
int AppRuntimeApp_Accept(AppRuntimeApp *app, PRuntimeManifestInfo *info,
        char *entry, unsigned int capacity);
int AppRuntimeApp_Resolve(AppRuntimeApp *app, const char *base,
        const char *reference, char *url, int capacity);
/* Navigation worker only. Serial, copied I/O request; never UI/Core/JS. */
int AppRuntimeApp_Read(AppRuntimeApp *app, const char *base,
        const char *reference, void *buffer, unsigned int capacity,
        unsigned int *bytes, PRuntimeResourceReference *resolved);
/* UI creates a page binding; worker creates/closes its public copy channel.
 * Ready: 0 pending, 1 ready, -1 failed. Release ends ALL UI access first. */
AppRuntimePage *AppRuntimeApp_BeginPage(AppRuntimeApp *app,
        unsigned long tab_id, unsigned long generation);
int AppRuntimePage_Ready(AppRuntimePage *page);
int AppRuntimePage_Submit(AppRuntimePage *page, const PRuntimeServiceRequest *request);
int AppRuntimePage_Take(AppRuntimePage *page, PRuntimeServiceResult *result);
void AppRuntimePage_Cancel(AppRuntimePage *page, unsigned long bridge, unsigned long request);
void AppRuntimePage_Release(AppRuntimePage **page);
/* After all navigation workers have joined, request close then poll UI. */
void AppRuntimeApp_RequestClose(AppRuntimeApp *app);
int AppRuntimeApp_TryDestroy(AppRuntimeApp **app);
#endif
