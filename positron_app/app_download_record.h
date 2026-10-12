/* EXE-private persisted download model. Never exposed to web scripts. */
#ifndef POSITRON_APP_DOWNLOAD_RECORD_H
#define POSITRON_APP_DOWNLOAD_RECORD_H
#define APP_DOWNLOAD_PAGE_MAX 16
#define APP_DOWNLOAD_PATH_MAX 512
#define APP_DOWNLOAD_URL_MAX 2048
enum {
    APP_DOWNLOAD_QUEUED = 0,
    APP_DOWNLOAD_RECEIVING = 1,
    APP_DOWNLOAD_PUBLISHING = 2,
    APP_DOWNLOAD_COMPLETE = 3,
    APP_DOWNLOAD_CANCELLED = 4,
    APP_DOWNLOAD_FAILED = 5,
    APP_DOWNLOAD_INTERRUPTED = 6
};
typedef struct AppDownloadRecord {
    __int64 id;
    __int64 created_utc;
    __int64 received;
    __int64 total;
    int state;
    int http_status;
    int error;
    char url[APP_DOWNLOAD_URL_MAX];
    char final_url[APP_DOWNLOAD_URL_MAX];
    char path[APP_DOWNLOAD_PATH_MAX];
    char temporary[APP_DOWNLOAD_PATH_MAX];
} AppDownloadRecord;
typedef struct AppDownloadSnapshot {
    int count;
    int has_more;
    AppDownloadRecord entries[APP_DOWNLOAD_PAGE_MAX];
} AppDownloadSnapshot;
#endif
