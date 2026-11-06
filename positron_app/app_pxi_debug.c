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
    int result,phase,hook_ok;
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
int AppPxi_DebugRun(void)
{
    PxiTest *t;
    AppSettingsResult result;
    WCHAR exe[MAX_PATH],dll[MAX_PATH],*slash;
    HANDLE worker;
    const WCHAR *prefix;
    char database[1024],line[192];
    int rc;
    t=(PxiTest *)calloc(1,sizeof(*t)); if(!t) return 1;
    t->result=1; prefix=L"\\Temp\\Positron-device-gate\\app-pxi-";
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
    _snprintf(line,sizeof(line)-1,"pxi-native %s phase=%d crc=complete cleanup=owned retention=conservative\r\n",t->result ? "FAIL" : "PASS",t->phase);
    line[sizeof(line)-1]=0; AppDebug_Log(line); rc=t->result; free(t); return rc;
}
#endif
