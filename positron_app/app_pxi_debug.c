#include "app_pxi.h"
#ifdef _DEBUG
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "app_runtime_install.h"
#include "app_debug.h"
typedef struct PxiTest {
    char root[1024],collision[1024];
    AppSettingsStore *store;
    LONG cancel;
    int result,phase,hook_ok,update_mode;
} PxiTest;
static int test_cancel(void *user) { return (int)InterlockedCompareExchange(&((PxiTest *)user)->cancel,0,0); }
static int test_roots(PxiTest *t)
{
    WCHAR path[MAX_PATH]; WIN32_FIND_DATAW data; HANDLE find; int n,found; DWORD error;
    if(!MultiByteToWideChar(CP_UTF8,0,t->root,-1,path,MAX_PATH) || wcslen(path)+12>=MAX_PATH) return -1;
    wcscat(path,L"\\*"); found=0;
    find=FindFirstFileW(path,&data);
    if(find==INVALID_HANDLE_VALUE) return -1;
    do { if(!_wcsicmp(data.cFileName,L"apps")) found=(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : -1; } while(FindNextFileW(find,&data));
    error=GetLastError(); FindClose(find);
    if(error!=ERROR_NO_MORE_FILES || found<0) return -1;
    if(!found) return 0;
    path[wcslen(path)-1]=0; wcscat(path,L"apps\\*"); n=0;
    find=FindFirstFileW(path,&data); if(find==INVALID_HANDLE_VALUE) return -1;
    do { if((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !wcsncmp(data.cFileName,L"pxi-",4)) ++n; } while(FindNextFileW(find,&data));
    error=GetLastError(); FindClose(find); return error==ERROR_NO_MORE_FILES ? n : -1;
}
static int test_extract(PxiTest *t,const char *name,int expected)
{
    AppPxi *p; char path[1024],line[256]; int rc,close;
    strcpy(path,t->root); strcat(path,"\\"); strcat(path,name); p=NULL;
    rc=AppPxi_Extract(path,test_cancel,t,&p);
    close=AppPxi_Close(&p);
    _snprintf(line,sizeof(line)-1,"pxi-extract case=%s rc=%d expected=%d cleanup=%d\r\n",name,rc,expected,close);
    line[sizeof(line)-1]=0; AppDebug_Log(line);
    return rc==expected && close==PRUNTIME_OK ? 0 : 1;
}
static void test_cancel_hook(const char *directory,void *user)
{
    PxiTest *t; (void)directory; t=(PxiTest *)user;
    InterlockedExchange(&t->cancel,1); t->hook_ok=1;
}
static void test_collision_hook(const char *directory,void *user)
{
    PxiTest *t; WCHAR path[MAX_PATH]; HANDLE file; DWORD written;
    t=(PxiTest *)user; strcpy(t->collision,directory); strcat(t->collision,"\\index.html");
    if(!MultiByteToWideChar(CP_UTF8,0,t->collision,-1,path,MAX_PATH)) return;
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return;
    t->hook_ok=WriteFile(file,"fixture-owned sentinel",22,&written,NULL) && written==22;
    CloseHandle(file);
}
static int test_open(PxiTest *t,const char *target,int kind,AppRuntimePlatform *p,AppRuntimeInstall **i)
{
    char path[1024]; int rc;
    rc=AppRuntimePlatform_Init(p); if(rc!=PRUNTIME_OK) return rc;
    if(kind==APP_RUNTIME_INSTALL_PXI) { strcpy(path,t->root); strcat(path,"\\"); strcat(path,target); }
    else strcpy(path,target);
    return AppRuntimeInstall_Open(p,t->store,kind,path,test_cancel,t,i);
}
static int test_note(AppRuntimeInstall *install,int insert)
{
    static const char write[]="{\"version\":1,\"method\":\"execute\","
        "\"sql\":\"INSERT INTO notes(title,body) VALUES(?,?)\",\"params\":["
        "{\"type\":\"text\",\"value\":\"\xe7\xac\x94\xe8\xae\xb0'\"},"
        "{\"type\":\"text\",\"value\":\"keep me\"}]}";
    static const char read[]="{\"version\":1,\"method\":\"query\","
        "\"sql\":\"SELECT title,body FROM notes ORDER BY id\",\"params\":[]}";
    PRuntimeStorageHandle storage;
    const char *request;
    char output[PRUNTIME_SERVICE_JSON_BYTES+1];
    unsigned int bytes;
    int rc,closed;
    storage=AppRuntimeInstall_TakeStorage(install);
    if(!storage) return PRUNTIME_STATE;
    request=insert ? write : read;
    rc=PRuntime_StorageExecuteJson(storage,request,(unsigned int)strlen(request),output,sizeof(output),&bytes);
    closed=PRuntime_StorageClose(&storage);
    if(rc!=PRUNTIME_OK) return rc;
    if(!insert && (!strstr(output,"\"value\":\"\xe7\xac\x94\xe8\xae\xb0'\"") ||
            !strstr(output,"\"value\":\"keep me\""))) return PRUNTIME_INVALID;
    return closed;
}
static int test_path_exists(const WCHAR *path)
{
    WIN32_FIND_DATAW data;
    HANDLE find;
    find=FindFirstFileW(path,&data);
    if(find==INVALID_HANDLE_VALUE) return 0;
    FindClose(find);
    return 1;
}
static int test_extract_retained(PxiTest *t,const char *name,int record_ownership,WCHAR *out_root)
{
    AppPxi *p;
    char path[1024];
    int rc;
    strcpy(path,t->root); strcat(path,"\\"); strcat(path,name);
    p=NULL;
    rc=AppPxi_Extract(path,test_cancel,t,&p);
    if(rc!=PRUNTIME_OK) { (void)AppPxi_Close(&p); return rc; }
    if(!MultiByteToWideChar(CP_UTF8,0,AppPxi_Directory(p),-1,out_root,MAX_PATH)) {
        (void)AppPxi_Close(&p); return PRUNTIME_LIMIT;
    }
    if(record_ownership) rc=AppPxi_RecordOwnership(p);
    if(rc==PRUNTIME_OK) AppPxi_Retain(p);
    if(AppPxi_Close(&p)!=PRUNTIME_OK && rc==PRUNTIME_OK) rc=PRUNTIME_IO;
    return rc;
}
static int test_write_file(const WCHAR *path,DWORD disposition,const char *body)
{
    HANDLE file;
    DWORD bytes,length;
    length=(DWORD)strlen(body);
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,disposition,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    bytes=0;
    if(!WriteFile(file,body,length,&bytes,NULL) || bytes!=length || !FlushFileBuffers(file)) {
        CloseHandle(file); return 0;
    }
    CloseHandle(file);
    return 1;
}
static int test_update(PxiTest *t,AppRuntimeRegistry *registry,PRuntimeRegistrationRecord *record)
{
    AppRuntimePlatform platform;
    AppRuntimeInstall *install;
    PRuntimeUpdateRegistration *pair;
    AppPxiReclaimSummary summary;
    WCHAR root_a[MAX_PATH],root_b[MAX_PATH],probe[MAX_PATH];
    HANDLE lease,lock_file;
    unsigned long bytes;
    int rc;
    install=NULL; lease=NULL; lock_file=INVALID_HANDLE_VALUE;
    pair=(PRuntimeUpdateRegistration *)malloc(sizeof(*pair));
    if(!pair) return 1;
    rc=1; t->phase=1;
    if(t->update_mode==6) {
        /* Cold restart after Mode 5's interrupted deletion: resume and complete it. */
        if(test_roots(t)!=5) goto done;
        if(AppPxi_ReclaimUnused(t->store,NULL,NULL,&summary)!=PRUNTIME_OK ||
                summary.scanned!=5 || summary.reclaimed!=1 ||
                summary.referenced!=1 || summary.skipped!=3) goto done;
        t->phase=2;
        if(test_roots(t)!=4) goto done;
        t->phase=3;
        if(test_open(t,"org.positron.updatenotes",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK ||
                test_note(install,0)!=PRUNTIME_OK) goto done;
        t->phase=4; rc=0; goto done;
    }
    if(t->update_mode==5) {
        if(AppRuntimeRegistry_FindApp(registry,"org.positron.updatenotes",record)!=PRUNTIME_OK ||
                record->state!=PRUNTIME_REG_READY) goto done;
        if(!MultiByteToWideChar(CP_UTF8,0,record->directory,-1,root_a,MAX_PATH)) goto done;
        /* Live READY root and sub-root overlap must be refused as BUSY. */
        if(AppPxi_ReclaimRoot(t->store,root_a,&bytes)!=PRUNTIME_BUSY) goto done;
        memset(&registry->call,0,sizeof(registry->call));
        registry->call.operation=APP_INSTALL_CHECK_REFERENCES;
        {
            char sub[1024];
            strcpy(sub,record->directory); strcat(sub,"\\schema");
            registry->call.resource_root=sub;
            if(AppSettingsStore_InstallCall(t->store,&registry->call)!=PRUNTIME_BUSY) goto done;
        }
        /* Case A: legacy root without .ownership sidecar must be preserved. */
        if(test_extract_retained(t,"old.pxi",0,root_a)!=PRUNTIME_OK ||
                AppPxi_ReclaimRoot(t->store,root_a,&bytes)!=PRUNTIME_INVALID ||
                !test_path_exists(root_a)) goto done;
        t->phase=2;
        /* Case B: extra foreign file inside root must be refused with 0 deletions. */
        if(test_extract_retained(t,"old.pxi",1,root_a)!=PRUNTIME_OK) goto done;
        wcscpy(probe,root_a); wcscat(probe,L"\\extra.txt");
        if(!test_write_file(probe,CREATE_NEW,"foreign") ||
                AppPxi_ReclaimRoot(t->store,root_a,&bytes)!=PRUNTIME_INVALID ||
                !test_path_exists(probe)) goto done;
        /* Case C: modified file inside root must be refused with 0 deletions. */
        if(test_extract_retained(t,"old.pxi",1,root_a)!=PRUNTIME_OK) goto done;
        wcscpy(probe,root_a); wcscat(probe,L"\\index.html");
        if(!test_write_file(probe,TRUNCATE_EXISTING,"tampered-html") ||
                AppPxi_ReclaimRoot(t->store,root_a,&bytes)!=PRUNTIME_IO ||
                !test_path_exists(probe)) goto done;
        t->phase=3;
        /* Case E: lock early file (index.html) to interrupt deletion mid-way,
         * verify .reclaim persists, verify tampering during interrupted state is
         * refused, then release handle so Mode 6 resumes after cold restart. */
        if(test_extract_retained(t,"old.pxi",1,root_b)!=PRUNTIME_OK) goto done;
        wcscpy(probe,root_b); wcscat(probe,L"\\index.html");
        lock_file=CreateFileW(probe,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        if(lock_file==INVALID_HANDLE_VALUE) goto done;
        if(AppPxi_ReclaimRoot(t->store,root_b,&bytes)!=PRUNTIME_IO) goto done;
        if(!test_path_exists(probe)) goto done;
        wcscpy(probe,root_b); wcscat(probe,L".reclaim");
        if(!test_path_exists(probe)) goto done;
        wcscpy(probe,root_b); wcscat(probe,L".ownership");
        if(!test_path_exists(probe)) goto done;
        wcscpy(probe,root_b); wcscat(probe,L"\\intruder.txt");
        if(!test_write_file(probe,CREATE_NEW,"intruder") ||
                AppPxi_ReclaimRoot(t->store,root_b,&bytes)!=PRUNTIME_INVALID ||
                !DeleteFileW(probe)) goto done;
        CloseHandle(lock_file); lock_file=INVALID_HANDLE_VALUE;
        if(test_roots(t)!=5) goto done;
        t->phase=4; rc=0; goto done;
    }
    if(t->update_mode==4) {
        if(AppRuntimeRegistry_FindApp(registry,"org.positron.updatenotes",record)!=PRUNTIME_OK ||
                record->approval.revision!=2 || record->state!=PRUNTIME_REG_REVOKED) goto done;
        if(test_roots(t)!=0) goto done;
        if(test_open(t,"org.positron.updatenotes",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_DENIED) goto done;
        if(test_open(t,"old.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK ||
                AppRuntimeInstall_Confirm(install)!=PRUNTIME_INVALID) goto done;
        AppRuntimeInstall_Close(&install);
        t->phase=2;
        if(test_open(t,"resources-v2.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK ||
                AppRuntimeInstall_Action(install)!=APP_RUNTIME_RESTORE_CONFIRM ||
                AppRuntimeInstall_Confirm(install)!=PRUNTIME_OK || test_note(install,0)!=PRUNTIME_OK) goto done;
        AppRuntimeInstall_Close(&install);
        t->phase=3;
        if(AppRuntimeRegistry_FindUpdate(registry,record->approval.installation_id,pair)!=PRUNTIME_OK ||
                pair->current.state!=PRUNTIME_REG_READY ||
                pair->current.generation!=record->generation+1 ||
                pair->current.approval.revision!=record->approval.revision ||
                pair->attempt_state) goto done;
        if(test_open(t,"org.positron.updatenotes",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK ||
                test_note(install,0)!=PRUNTIME_OK) goto done;
        t->phase=4; rc=0; goto done;
    }
    if(t->update_mode==3) {
        if(AppRuntimeRegistry_FindApp(registry,"org.positron.updatenotes",record)!=PRUNTIME_OK ||
                record->approval.revision!=2 || record->state!=PRUNTIME_REG_READY) goto done;
        if(test_open(t,"org.positron.updatenotes",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK ||
                AppRuntimeRegistry_Revoke(registry,record,NULL,NULL)!=PRUNTIME_BUSY) goto done;
        AppRuntimeInstall_Close(&install);
        registry->fault=4;
        if(AppRuntimeRegistry_Revoke(registry,record,NULL,NULL)!=PRUNTIME_OK) goto done;
        if(AppRuntimeRegistry_FindUpdate(registry,record->approval.installation_id,pair)!=PRUNTIME_OK ||
                pair->current.state!=PRUNTIME_REG_REVOKED ||
                pair->current.generation!=record->generation+1 ||
                pair->attempt_state!=PRUNTIME_UPDATE_PUBLISHED ||
                test_open(t,"org.positron.updatenotes",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_DENIED) goto done;
        memset(&registry->call,0,sizeof(registry->call));
        registry->call.operation=APP_INSTALL_CHECK_REFERENCES;
        registry->call.resource_root=pair->current.directory;
        if(AppSettingsStore_InstallCall(t->store,&registry->call)!=PRUNTIME_OK) goto done;
        /* Reclaim the revoked v2 root; user SQLite data must survive for Mode 4 restore. */
        if(AppPxi_ReclaimUnused(t->store,NULL,NULL,&summary)!=PRUNTIME_OK ||
                summary.scanned!=1 || summary.reclaimed!=1 ||
                summary.referenced!=0 || summary.skipped!=0 ||
                !summary.reclaimed_bytes || test_roots(t)!=0) goto done;
        if(!MultiByteToWideChar(CP_UTF8,0,pair->current.directory,-1,root_a,MAX_PATH)) goto done;
        wcscpy(probe,root_a); wcscat(probe,L".ownership");
        if(test_path_exists(probe)) goto done;
        wcscpy(probe,root_a); wcscat(probe,L".reclaim");
        if(test_path_exists(probe)) goto done;
        t->phase=4; rc=0; goto done;
    }
    if(t->update_mode==1) {
        if(test_open(t,"old.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK ||
                AppRuntimeInstall_Confirm(install)!=PRUNTIME_OK || test_note(install,1)!=PRUNTIME_OK) goto done;
        AppRuntimeInstall_Close(&install);
    }
    if(AppRuntimeRegistry_FindApp(registry,"org.positron.updatenotes",record)!=PRUNTIME_OK ||
            record->approval.revision!=1 || record->state!=PRUNTIME_REG_READY) goto done;
    t->phase=2;
    if(test_open(t,"resources-v2.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK ||
            AppRuntimeInstall_Action(install)!=APP_RUNTIME_UPDATE_CONFIRM) goto done;
    AppRuntimeInstall_DebugOptions(install,t->update_mode==1 ? 2 : 3,1000000,0);
    if(AppRuntimeInstall_Confirm(install)!=(t->update_mode==1 ? PRUNTIME_UNRESOLVED : PRUNTIME_OK)) goto done;
    AppRuntimeInstall_Close(&install);
    t->phase=3;
    if(AppRuntimeRegistry_FindUpdate(registry,record->approval.installation_id,pair)!=PRUNTIME_OK ||
            pair->attempt_state!=(t->update_mode==1 ? PRUNTIME_UPDATE_PREPARING : PRUNTIME_UPDATE_PUBLISHED) ||
            !AppInstallStore_RecordEqual(record,&pair->base) ||
            (t->update_mode==1 ? !AppInstallStore_RecordEqual(record,&pair->current) :
                !AppInstallStore_RecordEqual(&pair->candidate,&pair->current))) goto done;
    if(t->update_mode==1 && AppRuntimeRegistry_Revoke(registry,record,NULL,NULL)!=PRUNTIME_BUSY) goto done;
    memset(&registry->call,0,sizeof(registry->call));
    registry->call.operation=APP_INSTALL_CHECK_REFERENCES;
    registry->call.resource_root=pair->candidate.directory;
    if(AppSettingsStore_InstallCall(t->store,&registry->call)!=PRUNTIME_BUSY) goto done;
    if(!MultiByteToWideChar(CP_UTF8,0,pair->base.directory,-1,root_a,MAX_PATH) ||
            !MultiByteToWideChar(CP_UTF8,0,pair->candidate.directory,-1,root_b,MAX_PATH) ||
            AppPxi_CheckOwnership(root_b,&bytes)!=PRUNTIME_OK || !bytes) goto done;
    if(t->update_mode==1) {
        /* Pending PREPARING update references both base (v1) and candidate (v2). */
        if(AppPxi_ReclaimRoot(t->store,root_a,&bytes)!=PRUNTIME_BUSY ||
                AppPxi_ReclaimRoot(t->store,root_b,&bytes)!=PRUNTIME_BUSY ||
                test_roots(t)!=2) goto done;
    } else {
        /* Update PUBLISHED: v2 is BUSY, v1 respects active resource lease, then reclaims cleanly. */
        if(AppPxi_ReclaimRoot(t->store,root_b,&bytes)!=PRUNTIME_BUSY) goto done;
        if(AppRuntimePlatform_AcquireResourceLease(root_a,&lease)!=PRUNTIME_OK ||
                AppPxi_ReclaimRoot(t->store,root_a,&bytes)!=PRUNTIME_BUSY) goto done;
        CloseHandle(lease); lease=NULL;
        if(AppPxi_ReclaimUnused(t->store,NULL,NULL,&summary)!=PRUNTIME_OK ||
                summary.scanned!=2 || summary.reclaimed!=1 ||
                summary.referenced!=1 || summary.skipped!=0 ||
                !summary.reclaimed_bytes || test_roots(t)!=1) goto done;
        wcscpy(probe,root_a); wcscat(probe,L".ownership");
        if(test_path_exists(probe)) goto done;
    }
    if(test_open(t,"org.positron.updatenotes",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK ||
            AppRuntimeInstall_Action(install) || test_note(install,0)!=PRUNTIME_OK) goto done;
    t->phase=4; rc=0;
done:
    if(lock_file!=INVALID_HANDLE_VALUE) CloseHandle(lock_file);
    if(lease) CloseHandle(lease);
    AppRuntimeInstall_Close(&install);
    free(pair); return rc;
}
static DWORD WINAPI test_worker(void *user)
{
    PxiTest *t;
    AppRuntimePlatform platform;
    AppRuntimeInstall *install;
    AppRuntimeRegistry *registry;
    PRuntimeRegistrationRecord *record;
    WCHAR path[MAX_PATH],parent[MAX_PATH],*slash;
    HANDLE file; DWORD bytes; char body[24];
    int rc;
    t=(PxiTest *)user; install=NULL;
    registry=(AppRuntimeRegistry *)calloc(1,sizeof(*registry));
    record=(PRuntimeRegistrationRecord *)calloc(1,sizeof(*record));
    if(!registry || !record) goto done;
    AppRuntimeRegistry_Init(registry,t->store); t->phase=1;
    if(t->update_mode) { t->result=test_update(t,registry,record); goto done; }
    if(test_roots(t)!=0 || test_extract(t,"valid.pxi",PRUNTIME_OK) || test_roots(t)!=0) goto done;
    t->phase=2;
    if(test_extract(t,"crc.pxi",PRUNTIME_INVALID) || test_roots(t)!=0 ||
        test_extract(t,"traversal.pxi",PRUNTIME_INVALID) || test_roots(t)!=0 ||
        test_extract(t,"collision.pxi",PRUNTIME_INVALID) || test_roots(t)!=0 ||
        test_extract(t,"over-count.pxi",PRUNTIME_LIMIT) || test_roots(t)!=0 ||
        test_extract(t,"over-bytes.pxi",PRUNTIME_LIMIT) || test_roots(t)!=0 ||
        test_extract(t,"long-native.pxi",PRUNTIME_LIMIT) || test_roots(t)!=0) goto done;
    t->phase=3;
    AppPxi_DebugHook(test_cancel_hook,t);
    if(test_extract(t,"valid.pxi",PRUNTIME_CANCELLED) || !t->hook_ok || test_roots(t)!=0) goto done;
    InterlockedExchange(&t->cancel,0);
    t->phase=4; t->hook_ok=0;
    AppPxi_DebugHook(test_collision_hook,t);
    if(test_extract(t,"valid.pxi",PRUNTIME_IO) || !t->hook_ok || test_roots(t)!=1) goto done;
    if(!MultiByteToWideChar(CP_UTF8,0,t->collision,-1,path,MAX_PATH)) goto done;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) goto done;
    bytes=0; rc=ReadFile(file,body,sizeof(body),&bytes,NULL) && bytes==22 && !memcmp(body,"fixture-owned sentinel",22);
    CloseHandle(file); if(!rc) goto done;
    /* The fixture itself created this sentinel. Production cleanup refused it.
     * Delete only that precise file and the now-empty isolated fixture root. */
    wcscpy(parent,path); slash=wcsrchr(parent,L'\\'); if(!slash) goto done; *slash=0;
    if(!DeleteFileW(path) || !RemoveDirectoryW(parent) || test_roots(t)!=0) goto done;
    t->phase=5;
    if(test_extract(t,"exact-count.pxi",PRUNTIME_OK) || test_roots(t)!=0 ||
        test_extract(t,"exact-bytes.pxi",PRUNTIME_OK) || test_roots(t)!=0) goto done;
    t->phase=6;
    if(test_open(t,"missing-manifest.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)>=0) goto done;
    AppRuntimeInstall_Close(&install); if(test_roots(t)!=0) goto done;
    if(test_open(t,"valid.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK) goto done;
    AppRuntimeInstall_Close(&install);
    if(test_roots(t)!=0 || AppRuntimeRegistry_FindApp(registry,"org.positron.pxi_demo",record)!=PRUNTIME_NOT_FOUND) goto done;
    t->phase=7;
    if(test_open(t,"sql.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK ||
        AppRuntimeInstall_Confirm(install)!=PRUNTIME_OK) goto done;
    AppRuntimeInstall_Close(&install);
    if(test_roots(t)!=1 || test_open(t,"org.positron.sql_demo",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK ||
        AppRuntimeInstall_Action(install)) goto done;
    AppRuntimeInstall_Close(&install);
    if(test_open(t,"sql.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)>=0) goto done;
    AppRuntimeInstall_Close(&install); if(test_roots(t)!=1) goto done;
    /* Installed startup must not depend on the original package remaining. */
    if(!MultiByteToWideChar(CP_UTF8,0,t->root,-1,path,MAX_PATH)) goto done;
    wcscat(path,L"\\sql.pxi");
    if(!DeleteFileW(path) || test_open(t,"org.positron.sql_demo",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK) goto done;
    AppRuntimeInstall_Close(&install);
    t->phase=8;
    if(test_open(t,"unknown.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK) goto done;
    AppRuntimeInstall_DebugOptions(install,2,1000000,0);
    if(AppRuntimeInstall_Confirm(install)!=PRUNTIME_UNRESOLVED) goto done;
    AppRuntimeInstall_Close(&install);
    if(test_roots(t)!=2 || test_open(t,"org.positron.pxi_unknown",APP_RUNTIME_INSTALLED,&platform,&install)!=PRUNTIME_OK ||
        AppRuntimeInstall_Action(install)) goto done;
    AppRuntimeInstall_Close(&install);
    t->phase=9;
    if(test_open(t,"bad-migration.pxi",APP_RUNTIME_INSTALL_PXI,&platform,&install)!=PRUNTIME_OK ||
        AppRuntimeInstall_Confirm(install)>=0) goto done;
    AppRuntimeInstall_Close(&install);
    if(test_roots(t)!=3 || AppRuntimeRegistry_FindApp(registry,"org.positron.pxi_failed",record)!=PRUNTIME_OK ||
        record->state!=PRUNTIME_REG_FAILED) goto done;
    t->phase=10; t->result=0;
done:
    AppPxi_DebugHook(NULL,NULL); AppRuntimeInstall_Close(&install);
    free(record); free(registry); return 0;
}
int AppPxi_DebugRun(int update_mode)
{
    PxiTest *t;
    AppSettingsResult result;
    WCHAR exe[MAX_PATH],dll[MAX_PATH],*slash;
    HANDLE worker;
    const WCHAR *prefix;
    char database[1024],line[192];
    int rc;
    t=(PxiTest *)calloc(1,sizeof(*t)); if(!t) return 1;
    t->result=1; t->update_mode=update_mode;
    prefix=update_mode ? L"\\Temp\\Positron-device-gate\\app-pxi-update-" :
        L"\\Temp\\Positron-device-gate\\app-pxi-";
    if(!GetModuleFileNameW(NULL,exe,MAX_PATH) || wcsncmp(exe,prefix,wcslen(prefix))) { free(t); return 1; }
    slash=wcsrchr(exe,L'\\'); if(!slash) { free(t); return 1; } *slash=0;
    wcscpy(dll,exe); wcscat(dll,L"\\positron_db.dll");
    if(!WideCharToMultiByte(CP_UTF8,0,exe,-1,t->root,sizeof(t->root),NULL,NULL)) { free(t); return 1; }
    strcpy(database,t->root); strcat(database,"\\positron.db");
    if(AppSettingsStore_Create(dll,database,&t->store)!=APP_SETTINGS_OK) { free(t); return 1; }
    memset(&result,0,sizeof(result));
    do { rc=AppSettingsStore_Poll(t->store,&result); if(rc==APP_SETTINGS_PENDING) Sleep(10); } while(rc==APP_SETTINGS_PENDING);
    worker=NULL;
    if(rc==APP_SETTINGS_OK && result.result==APP_SETTINGS_OK) worker=CreateThread(NULL,0,test_worker,t,0,NULL);
    AppSettingsResult_Release(&result);
    if(worker) { WaitForSingleObject(worker,INFINITE); CloseHandle(worker); }
    AppSettingsStore_RequestClose(t->store);
    while(AppSettingsStore_TryDestroy(&t->store)==APP_SETTINGS_PENDING) Sleep(10);
    if(update_mode)
        _snprintf(line,sizeof(line)-1,"pxi-update %s mode=%d phase=%d\r\n",t->result ? "FAIL" : "PASS",update_mode,t->phase);
    else
        _snprintf(line,sizeof(line)-1,"pxi-native %s phase=%d crc=complete cleanup=owned retention=conservative\r\n",t->result ? "FAIL" : "PASS",t->phase);
    line[sizeof(line)-1]=0; AppDebug_Log(line); rc=t->result; free(t); return rc;
}
#endif
