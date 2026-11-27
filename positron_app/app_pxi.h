/* EXE-private physical output; ZIP and application semantics remain DLL-owned. */
#ifndef POSITRON_APP_PXI_H
#define POSITRON_APP_PXI_H
#include "app_runtime_platform.h"
#include "app_settings_store.h"
typedef struct AppPxi AppPxi;
typedef struct AppPxiReclaimSummary {
    unsigned int scanned;
    unsigned int reclaimed;
    unsigned int referenced;
    unsigned int skipped;
    unsigned long reclaimed_bytes;
    int first_error;
} AppPxiReclaimSummary;
/* Owner worker only. New, isolated directory; all entries DONE before success.
 * Failure cleans only files created by this operation; never user data.
 * First consumer limits: archive16MiB, 128 entries, 4MiB total, 256 FS objects. */
int AppPxi_Extract(const char *package,int (*cancelled)(void *),void *user,AppPxi **out);
const char *AppPxi_Directory(AppPxi *pxi);
/* Call BEFORE a registration callback could write. Even an unknown result must
 * retain the resource directory for recovery. No orphan collector here. */
void AppPxi_Retain(AppPxi *pxi);
int AppPxi_RecordOwnership(AppPxi *pxi);
/* Read-only physical preflight. A successful result does not authorize deletion. */
int AppPxi_CheckOwnership(const WCHAR *root,unsigned long *bytes);
/* Acquire exclusive deletion leases, verify DB references and ownership ledger
 * (or resume an interrupted reclaim marker), then delete listed objects in
 * reverse creation order. Never touches user data or unverified objects. */
int AppPxi_ReclaimRoot(AppSettingsStore *store,const WCHAR *root,unsigned long *reclaimed_bytes);
/* Bounded sweep of unreferenced <exe>\apps\pxi-* directories on a worker thread. */
int AppPxi_ReclaimUnused(AppSettingsStore *store,int (*cancelled)(void *),void *user,
        AppPxiReclaimSummary *summary);
/* Release pins; pre-intent cleanup in reverse creation order, no recursion.
 * Unprovable/replaced output is preserved and reported as IO. */
int AppPxi_Close(AppPxi **pxi);
#ifdef _DEBUG
int AppPxi_DebugRun(int update_mode);
/* Explicit fixture only, one shot after new output root and before extraction. */
void AppPxi_DebugHook(void (*hook)(const char *,void *),void *user);
#endif
#endif
