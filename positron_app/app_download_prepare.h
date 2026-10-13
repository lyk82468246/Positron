#ifndef POSITRON_APP_DOWNLOAD_PREPARE_H
#define POSITRON_APP_DOWNLOAD_PREPARE_H
#include <windows.h>
#include "../positron_http/positron_http.h"

#define APP_PREPARE_IDLE 0
#define APP_PREPARE_CHECKING 1
#define APP_PREPARE_READY 2
#define APP_PREPARE_NAME_REQUIRED 3
#define APP_PREPARE_FAILED 4
#define APP_PREPARE_CANCELLED 5
#define APP_PREPARE_STATUS_ERROR -100
#define APP_PREPARE_SIZE_ERROR -101
#define APP_PREPARE_NAME_ERROR -102
typedef struct AppDownloadPrepare AppDownloadPrepare;
/* UI owner; at most one probe worker process-wide. No DOM/file/DB access. */
AppDownloadPrepare *AppDownloadPrepare_Create(void);
void AppDownloadPrepare_ChangeUrl(AppDownloadPrepare *prepare, const char *url);
void AppDownloadPrepare_SetName(AppDownloadPrepare *prepare, const char *name);
int AppDownloadPrepare_Begin(AppDownloadPrepare *prepare);
void AppDownloadPrepare_Cancel(AppDownloadPrepare *prepare);
/* Nonblocking join/query/close; returns nonzero when display changed. */
int AppDownloadPrepare_Tick(AppDownloadPrepare *prepare);
int AppDownloadPrepare_TryDestroy(AppDownloadPrepare **prepare);
int AppDownloadPrepare_State(const AppDownloadPrepare *prepare);
int AppDownloadPrepare_Error(const AppDownloadPrepare *prepare);
int AppDownloadPrepare_HttpStatus(const AppDownloadPrepare *prepare);
unsigned long AppDownloadPrepare_Generation(const AppDownloadPrepare *prepare);
const char *AppDownloadPrepare_Name(const AppDownloadPrepare *prepare);
const char *AppDownloadPrepare_Url(const AppDownloadPrepare *prepare);
const PHttpProbeResult *AppDownloadPrepare_Result(const AppDownloadPrepare *prepare);
int AppDownloadPrepare_CanDownload(const AppDownloadPrepare *prepare);
/* WM6 filesystem policy only, no URI/header parsing or sanitizing. */
int AppDownloadPrepare_NameValid(const char *name);
const char *AppDownloadPrepare_StateText(int state, int chinese);
const char *AppDownloadPrepare_ErrorText(int error, int chinese);
#endif
