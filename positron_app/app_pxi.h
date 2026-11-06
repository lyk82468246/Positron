/* EXE-private physical output; ZIP and application semantics remain DLL-owned. */
#ifndef POSITRON_APP_PXI_H
#define POSITRON_APP_PXI_H
#include "app_runtime_platform.h"
typedef struct AppPxi AppPxi;
/* Owner worker only. New, isolated directory; all entries DONE before success.
 * Failure cleans only files created by this operation; never user data.
 * First consumer limits: archive16MiB, 128 entries, 4MiB total, 256 FS objects. */
int AppPxi_Extract(const char *package,int (*cancelled)(void *),void *user,AppPxi **out);
const char *AppPxi_Directory(AppPxi *pxi);
/* Call BEFORE a registration callback could write. Even an unknown result must
 * retain the resource directory for recovery. No orphan collector here. */
void AppPxi_Retain(AppPxi *pxi);
/* Release pins; pre-intent cleanup in reverse creation order, no recursion.
 * Unprovable/replaced output is preserved and reported as IO. */
int AppPxi_Close(AppPxi **pxi);
#ifdef _DEBUG
int AppPxi_DebugRun(void);
/* Explicit fixture only, one shot after new output root and before extraction. */
void AppPxi_DebugHook(void (*hook)(const char *,void *),void *user);
#endif
#endif
