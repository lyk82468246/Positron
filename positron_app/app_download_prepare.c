#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "app_download_prepare.h"
#include "app_downloads.h"
#include "app_url_router.h"

struct AppDownloadPrepare {
    DWORD owner;
    unsigned long generation;
    unsigned long launched;
    HANDLE request;
    HANDLE worker;
    int state;
    int error;
    int execution;
    int usable;
    char url[1024];
    char name[PHTTP_DOWNLOAD_NAME_MAX_BYTES + 1];
    PHttpProbeResult checked;
};
/* Modified only by the common application UI owner, not the workers. */
static int app_prepare_workers;

int AppDownloadPrepare_NameValid(const char *name)
{
    WCHAR wide[256];
    char roundtrip[256];
    WCHAR root[16];
    unsigned int i;
    unsigned int length;

    if (name == NULL || name[0] == 0 || strlen(name) > 255 ||
            !MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256) ||
            !WideCharToMultiByte(CP_UTF8, 0, wide, -1, roundtrip, 256, NULL, NULL) ||
            strcmp(name, roundtrip)) return 0;
    length = (unsigned int) wcslen(wide);
    if (wide[0] == L' ' || wide[length - 1] == L' ' || wide[length - 1] == L'.') return 0;
    for (i = 0; i < length; ++i)
        if (wide[i] < 32 || (wide[i] >= 127 && wide[i] <= 159) ||
                wcschr(L"<>:\"/\\|?*", wide[i]) != NULL) return 0;
    i = 0;
    while (i < 15 && wide[i] != 0 && wide[i] != L'.') { root[i] = wide[i]; ++i; }
    while (i > 0 && root[i - 1] == L' ') --i;
    root[i] = 0;
    if (!lstrcmpiW(root, L"CON") || !lstrcmpiW(root, L"PRN") ||
            !lstrcmpiW(root, L"AUX") || !lstrcmpiW(root, L"NUL") ||
            !lstrcmpiW(root, L"CLOCK$")) return 0;
    if (i == 4 && root[3] >= L'1' && root[3] <= L'9') {
        root[3] = 0;
        if (!lstrcmpiW(root, L"COM") || !lstrcmpiW(root, L"LPT")) return 0;
    }
    return 1;
}

static DWORD WINAPI app_prepare_worker(LPVOID context)
{
    AppDownloadPrepare *prepare;
    prepare = (AppDownloadPrepare *) context;
    prepare->execution = PHttp_ProbeGet(prepare->request);
    return 0;
}

AppDownloadPrepare *AppDownloadPrepare_Create(void)
{
    AppDownloadPrepare *prepare;
    prepare = (AppDownloadPrepare *) calloc(1, sizeof(*prepare));
    if (prepare != NULL) prepare->owner = GetCurrentThreadId();
    return prepare;
}

static void app_prepare_invalidate(AppDownloadPrepare *prepare)
{
    prepare->usable = 0;
    prepare->name[0] = 0;
    if (prepare->generation != ~0UL) ++prepare->generation;
    if (prepare->request != NULL) (void) PHttp_ProbeCancel(prepare->request);
}

void AppDownloadPrepare_ChangeUrl(AppDownloadPrepare *prepare, const char *url)
{
    if (prepare == NULL || prepare->owner != GetCurrentThreadId() || url == NULL ||
            !strcmp(prepare->url, url)) return;
    app_prepare_invalidate(prepare);
    prepare->state = APP_PREPARE_IDLE;
    prepare->error = 0;
    prepare->url[0] = 0;
    if (strlen(url) < sizeof(prepare->url)) strcpy(prepare->url, url);
    else { prepare->state = APP_PREPARE_FAILED; prepare->error = PHTTP_STREAM_LIMIT; }
}

void AppDownloadPrepare_SetName(AppDownloadPrepare *prepare, const char *name)
{
    if (prepare == NULL || prepare->owner != GetCurrentThreadId() || !prepare->usable || name == NULL) return;
    prepare->name[0] = 0;
    if (strlen(name) < sizeof(prepare->name)) strcpy(prepare->name, name);
    prepare->state = AppDownloadPrepare_NameValid(prepare->name) ?
            APP_PREPARE_READY : APP_PREPARE_NAME_REQUIRED;
}

int AppDownloadPrepare_Begin(AppDownloadPrepare *prepare)
{
    PHttpProbeOptions options;
    AppUrlSchemeKind scheme;
    int rc;

    if (prepare == NULL || prepare->owner != GetCurrentThreadId()) return PHTTP_STREAM_ARGUMENT;
    if (prepare->worker != NULL) return PHTTP_STREAM_STATE;
    if (app_prepare_workers != 0) {
        app_prepare_invalidate(prepare);
        prepare->error = PHTTP_STREAM_STATE;
        prepare->state = APP_PREPARE_FAILED;
        return PHTTP_STREAM_STATE;
    }
    app_prepare_invalidate(prepare);
    scheme = AppUrlRouter_ClassifyScheme(prepare->url);
    memset(&prepare->checked, 0, sizeof(prepare->checked));
    rc = PHTTP_STREAM_ARGUMENT;
    if (prepare->generation == ~0UL) rc = PHTTP_STREAM_LIMIT;
    else if (scheme == APP_URL_SCHEME_HTTP || scheme == APP_URL_SCHEME_HTTPS) {
        memset(&options, 0, sizeof(options));
        options.size = sizeof(options);
        options.version = PHTTP_PROBE_VERSION;
        options.timeout_ms = 30000;
        rc = PHttp_ProbeCreateGet(prepare->url, NULL, &options, &prepare->request);
    }
    prepare->error = rc;
    prepare->state = APP_PREPARE_FAILED;
    if (rc != 0) return rc;
    prepare->launched = prepare->generation;
    prepare->worker = CreateThread(NULL, 0, app_prepare_worker, prepare, 0, NULL);
    if (prepare->worker == NULL) {
        (void) PHttp_ProbeClose(&prepare->request);
        prepare->error = PHTTP_STREAM_MEMORY;
        return prepare->error;
    }
    ++app_prepare_workers;
    prepare->state = APP_PREPARE_CHECKING;
    return 0;
}

void AppDownloadPrepare_Cancel(AppDownloadPrepare *prepare)
{
    if (prepare == NULL || prepare->owner != GetCurrentThreadId()) return;
    app_prepare_invalidate(prepare);
    prepare->state = APP_PREPARE_CANCELLED;
    prepare->error = PHTTP_STREAM_CANCELLED;
}

int AppDownloadPrepare_Tick(AppDownloadPrepare *prepare)
{
    int rc;
    int current;

    if (prepare == NULL || prepare->owner != GetCurrentThreadId() || prepare->worker == NULL ||
            WaitForSingleObject(prepare->worker, 0) != WAIT_OBJECT_0) return 0;
    CloseHandle(prepare->worker);
    prepare->worker = NULL;
    --app_prepare_workers;
    current = prepare->launched == prepare->generation;
    memset(&prepare->checked, 0, sizeof(prepare->checked));
    prepare->checked.size = sizeof(prepare->checked);
    prepare->checked.version = PHTTP_PROBE_VERSION;
    rc = PHttp_ProbeGetResult(prepare->request, &prepare->checked);
    (void) PHttp_ProbeClose(&prepare->request);
    if (!current) return 1;
    if (rc == 0) rc = prepare->execution;
    if (rc == 0) rc = prepare->checked.metadata.result;
    if (rc == 0 && prepare->checked.metadata.status_code != 200) rc = APP_PREPARE_STATUS_ERROR;
    if (rc == 0 && prepare->checked.metadata.total > APP_DOWNLOAD_BODY_LIMIT) rc = APP_PREPARE_SIZE_ERROR;
    if (rc == 0 && prepare->checked.name.status != PHTTP_NAME_OK &&
            prepare->checked.name.status != PHTTP_NAME_MISSING) rc = APP_PREPARE_NAME_ERROR;
    prepare->error = rc;
    prepare->state = APP_PREPARE_FAILED;
    if (rc == 0) {
        prepare->usable = 1;
        AppDownloadPrepare_SetName(prepare, prepare->checked.name.basename);
    }
    return 1;
}

int AppDownloadPrepare_TryDestroy(AppDownloadPrepare **pointer)
{
    AppDownloadPrepare *prepare;
    if (pointer == NULL || *pointer == NULL) return 0;
    prepare = *pointer;
    if (prepare->owner != GetCurrentThreadId()) return PHTTP_STREAM_STATE;
    AppDownloadPrepare_Cancel(prepare);
    (void) AppDownloadPrepare_Tick(prepare);
    if (prepare->worker != NULL) return PHTTP_STREAM_STATE;
    free(prepare);
    *pointer = NULL;
    return 0;
}

int AppDownloadPrepare_State(const AppDownloadPrepare *p) { return p == NULL ? APP_PREPARE_IDLE : p->state; }
int AppDownloadPrepare_Error(const AppDownloadPrepare *p) { return p == NULL ? 0 : p->error; }
int AppDownloadPrepare_HttpStatus(const AppDownloadPrepare *p) { return p == NULL ? 0 : p->checked.metadata.status_code; }
unsigned long AppDownloadPrepare_Generation(const AppDownloadPrepare *p) { return p == NULL ? 0 : p->generation; }
const char *AppDownloadPrepare_Name(const AppDownloadPrepare *p) { return p == NULL ? "" : p->name; }
const char *AppDownloadPrepare_Url(const AppDownloadPrepare *p) { return p == NULL ? "" : p->url; }
const PHttpProbeResult *AppDownloadPrepare_Result(const AppDownloadPrepare *p)
{ return p != NULL && p->usable ? &p->checked : NULL; }
int AppDownloadPrepare_CanDownload(const AppDownloadPrepare *p)
{ return p != NULL && p->usable && p->state == APP_PREPARE_READY && AppDownloadPrepare_NameValid(p->name); }

const char *AppDownloadPrepare_StateText(int state, int chinese)
{
    static const char *en[] = { "Resolve the link before downloading.", "Resolving link...",
        "Resolved. Confirm the file name and save location.", "Enter a valid file name to continue.",
        "Link could not be prepared.", "Link preparation cancelled." };
    static const char *zh[] = { "\350\257\267\345\205\210\350\247\243\346\236\220\351\223\276\346\216\245\357\274\214\345\206\215\344\270\213\350\275\275\343\200\202", "\346\255\243\345\234\250\350\247\243\346\236\220\351\223\276\346\216\245\342\200\246\342\200\246",
        "\350\247\243\346\236\220\346\210\220\345\212\237\357\274\214\350\257\267\347\241\256\350\256\244\346\226\207\344\273\266\345\220\215\345\222\214\344\277\235\345\255\230\344\275\215\347\275\256\343\200\202", "\350\257\267\350\276\223\345\205\245\346\234\211\346\225\210\346\226\207\344\273\266\345\220\215\345\220\216\345\206\215\344\270\213\350\275\275\343\200\202",
        "\351\223\276\346\216\245\350\247\243\346\236\220\345\244\261\350\264\245\343\200\202", "\345\267\262\345\217\226\346\266\210\351\223\276\346\216\245\350\247\243\346\236\220\343\200\202" };
    if (state < 0 || state > 5) state = APP_PREPARE_FAILED;
    return chinese ? zh[state] : en[state];
}

const char *AppDownloadPrepare_ErrorText(int error, int chinese)
{
    switch (error) {
    case PHTTP_STREAM_ARGUMENT: return chinese ? "\351\223\276\346\216\245\346\240\274\345\274\217\346\227\240\346\225\210" : "Invalid link";
    case PHTTP_STREAM_STATE: return chinese ? "\345\217\246\344\270\200\351\223\276\346\216\245\344\273\215\345\234\250\350\247\243\346\236\220\357\274\214\350\257\267\347\255\211\345\276\205\346\210\226\345\217\226\346\266\210" : "A link is still being prepared; wait or cancel";
    case PHTTP_PROBE_HTTP_STATUS: return chinese ? "\346\234\215\345\212\241\345\231\250\346\213\222\347\273\235\350\257\267\346\261\202" : "Server rejected the request";
    case PHTTP_STREAM_TRANSPORT: return chinese ? "\347\275\221\347\273\234\346\210\226\350\257\201\344\271\246\351\252\214\350\257\201\345\244\261\350\264\245" : "Network or certificate verification failed";
    case PHTTP_STREAM_TIMEOUT: return chinese ? "\350\247\243\346\236\220\350\266\205\346\227\266" : "Preparation timed out";
    case PHTTP_STREAM_CANCELLED: return chinese ? "\350\247\243\346\236\220\345\267\262\345\217\226\346\266\210" : "Preparation cancelled";
    case PHTTP_STREAM_PROTOCOL: return chinese ? "\345\223\215\345\272\224\345\215\217\350\256\256\346\227\240\346\225\210" : "Invalid response protocol";
    case PHTTP_STREAM_UNSUPPORTED: return chinese ? "\345\223\215\345\272\224\347\274\226\347\240\201\344\270\215\345\217\227\346\224\257\346\214\201" : "Unsupported response encoding";
    case APP_PREPARE_SIZE_ERROR: return chinese ? "\346\226\207\344\273\266\350\266\205\350\277\207\344\270\213\350\275\275\344\270\212\351\231\220" : "File exceeds the download limit";
    case APP_PREPARE_STATUS_ERROR: return chinese ? "\345\223\215\345\272\224\344\270\215\346\230\257\345\217\257\344\270\213\350\275\275\347\232\204\345\256\214\346\225\264\346\226\207\344\273\266" : "Response is not a complete downloadable object";
    case APP_PREPARE_NAME_ERROR: return chinese ? "\346\234\215\345\212\241\345\231\250\346\226\207\344\273\266\345\220\215\346\227\240\346\225\210\346\210\226\344\270\215\345\217\227\346\224\257\346\214\201" : "Invalid or unsupported server file name";
    default: return chinese ? "\350\247\243\346\236\220\350\265\204\346\272\220\346\210\226\347\263\273\347\273\237\351\224\231\350\257\257" : "Preparation resource or system error";
    }
}
