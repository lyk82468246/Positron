#ifndef POSITRON_APP_DOWNLOADS_H
#define POSITRON_APP_DOWNLOADS_H
#include "app_settings_store.h"
#include "../positron_http/positron_http.h"
#define APP_DOWNLOAD_TASK_MAX 2
#define APP_DOWNLOAD_BODY_LIMIT (32UL * 1024UL * 1024UL)
#define APP_DOWNLOAD_TIMEOUT_MS 120000UL
typedef struct AppDownloads AppDownloads;
AppDownloads *AppDownloads_Create(AppSettingsStore *store);
/* UI owner only. Absolute user-confirmed new path, no overwrite/URL filenames.
 * A task is not transferred until its initial journal record commits. */
int AppDownloads_Start(AppDownloads *downloads, const char *url, const WCHAR *path,
        const PHttpProbeResult *checked);
void AppDownloads_Cancel(AppDownloads *downloads, int slot);
void AppDownloads_AcceptResult(AppDownloads *downloads, const AppSettingsResult *result);
void AppDownloads_Tick(AppDownloads *downloads);
int AppDownloads_Snapshot(AppDownloads *downloads,
        AppDownloadRecord records[APP_DOWNLOAD_TASK_MAX]);
unsigned long AppDownloads_Revision(AppDownloads *downloads);
int AppDownloads_LastError(AppDownloads *downloads);
const char *AppDownloads_StateText(int state, int chinese);
void AppDownloads_RequestClose(AppDownloads *downloads);
/* Nonblocking; caller closes shared DB worker only after this succeeds. */
int AppDownloads_TryDestroy(AppDownloads **downloads);
#ifdef _DEBUG
int AppDownloads_DebugRun(int reopen);
int AppDownloads_DebugUiServer(char *url, int capacity);
void AppDownloads_DebugUiServerStop(void);
#endif
#endif
