/* EXE registration and scheduling only; Runtime owns queue/SQL semantics. */
#ifndef POSITRON_APP_RUNTIME_SERVICES_H
#define POSITRON_APP_RUNTIME_SERVICES_H
#include "app_runtime_app.h"
#include "positron_browser.h"
typedef struct AppRuntimeServices {
    AppRuntimePage *page;
    HANDLE session;
    unsigned long tab_id;
    unsigned long generation;
    int registered;
    PRuntimeServiceResult result; /* fixed owner-UI copy, never a stack frame */
} AppRuntimeServices;
int AppRuntimeServices_Begin(AppRuntimeServices *services, AppRuntimeApp *app,
        unsigned long tab_id, unsigned long generation);
/* After bootstrap, before author scripts: 0 pending, 1 ready, -1 failed. */
int AppRuntimeServices_Prepare(AppRuntimeServices *services, HANDLE session);
int AppRuntimeServices_Pump(AppRuntimeServices *services);
void AppRuntimeServices_Revoke(AppRuntimeServices *services);
#endif
