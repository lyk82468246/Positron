#include "app_runtime_install.h"
#ifdef _DEBUG
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "app_debug.h"

typedef struct InstallTest {
    AppSettingsStore *store;
    char root[1024];
    char capture_file[1024];
    int result;
    int phase;
    int changed;
} InstallTest;
static int test_write(const char *path,const char *text)
{
    WCHAR wide[MAX_PATH]; HANDLE file; DWORD wrote;
    if(!MultiByteToWideChar(CP_UTF8,0,path,-1,wide,MAX_PATH)) return 1;
    file=CreateFileW(wide,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return 1;
    wrote=0;
    if(!WriteFile(file,text,(DWORD)strlen(text),&wrote,NULL) || wrote!=strlen(text)) { CloseHandle(file); return 1; }
    CloseHandle(file); return 0;
}
static void capture_change(void *user)
{
    InstallTest *t;
    t=(InstallTest *)user; t->changed=test_write(t->capture_file,"added during capture")==0;
}
static int test_open(InstallTest *t,AppRuntimePlatform *p,AppRuntimeInstall **i,
        int kind,const char *name)
{
    char target[1024];
    if(AppRuntimePlatform_Init(p)!=PRUNTIME_OK) return PRUNTIME_IO;
    if(kind==APP_RUNTIME_INSTALL_DIRECTORY) {
        if(strlen(t->root)+strlen(name)+2>=sizeof(target)) return PRUNTIME_LIMIT;
        strcpy(target,t->root); strcat(target,"\\"); strcat(target,name);
    } else strcpy(target,name);
    return AppRuntimeInstall_Open(p,t->store,kind,target,NULL,NULL,i);
}
static int test_sql(PRuntimeStorageHandle storage,const char *sql,const char *expected)
{
    char request[512],output[4097],needle[96];
    unsigned int bytes;
    int rc;
    _snprintf(request,sizeof(request)-1,"{\"version\":1,\"method\":\"query\",\"sql\":\"%s\",\"params\":[]}",sql);
    request[sizeof(request)-1]=0; bytes=0;
    rc=PRuntime_StorageExecuteJson(storage,request,(unsigned int)strlen(request),output,sizeof(output),&bytes);
    _snprintf(needle,sizeof(needle)-1,"\"value\":\"%s\"",expected); needle[sizeof(needle)-1]=0;
    return rc==PRUNTIME_OK && bytes && strstr(output,needle) ? 0 : 1;
}
static int test_install(InstallTest *t,const char *directory,const char *id,int fault,
        unsigned int budget,int expected)
{
    AppRuntimePlatform p; AppRuntimeInstall *i;
    int rc;
    i=NULL;
    rc=test_open(t,&p,&i,APP_RUNTIME_INSTALL_DIRECTORY,directory);
    if(rc==PRUNTIME_OK) {
        AppRuntimeInstall_DebugOptions(i,fault,budget,0);
        rc=AppRuntimeInstall_Confirm(i);
    }
    AppRuntimeInstall_Close(&i);
    if(rc!=expected) return 1;
    if(expected==PRUNTIME_OK || expected==PRUNTIME_UNRESOLVED) {
        rc=test_open(t,&p,&i,APP_RUNTIME_INSTALLED,id);
        if(rc!=PRUNTIME_OK || AppRuntimeInstall_Action(i)!=0) { AppRuntimeInstall_Close(&i); return 1; }
        AppRuntimeInstall_Close(&i);
    }
    return 0;
}
static int test_resize(const char *path,DWORD bytes,int create)
{
    WCHAR wide[MAX_PATH]; HANDLE file; int result;
    if(!MultiByteToWideChar(CP_UTF8,0,path,-1,wide,MAX_PATH)) return 1;
    file=CreateFileW(wide,GENERIC_WRITE,0,NULL,create ? CREATE_NEW : OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return 1;
    result=SetFilePointer(file,bytes,NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER || !SetEndOfFile(file);
    CloseHandle(file); return result;
}
static DWORD test_file_size(const char *path)
{
    WCHAR wide[MAX_PATH]; WIN32_FIND_DATAW data; HANDLE find;
    if(!MultiByteToWideChar(CP_UTF8,0,path,-1,wide,MAX_PATH)) return 0xffffffffUL;
    find=FindFirstFileW(wide,&data); if(find==INVALID_HANDLE_VALUE) return 0xffffffffUL;
    FindClose(find); return data.nFileSizeLow;
}
static DWORD WINAPI test_install_worker(void *parameter)
{
    InstallTest *t;
    AppRuntimePlatform p,second;
    AppRuntimeInstall *i,*duplicate;
    AppRuntimeRegistry *registry;
    AppInstallCall *call;
    PRuntimeStorageHandle storage;
    PRuntimeRevisionProvider provider;
    PRuntimeRevisionHandle revision;
    PRuntimeResourceReference ref;
    PRuntimeStorageInfo storage_info;
    char path[1024],body[128],missing[1024],source[1024],line[2300];
    WCHAR wide[MAX_PATH],source_wide[MAX_PATH];
    unsigned int bytes;
    DWORD padding;
    PRuntimeRevisionInfo revision_info;
    int rc;
    t=(InstallTest *)parameter; i=NULL; duplicate=NULL; storage=NULL; revision=NULL;
    registry=(AppRuntimeRegistry *)calloc(1,sizeof(*registry));
    call=(AppInstallCall *)calloc(1,sizeof(*call));
    if(!registry || !call) goto done;
    AppRuntimeRegistry_Init(registry,t->store);
    t->phase=1;
    if(test_install(t,"no-sql","org.positron.nosql",0,1000000,PRUNTIME_OK)) goto done;
    if(test_install(t,"schema-zero","org.positron.zero",0,1000000,PRUNTIME_OK)) goto done;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.zero")!=PRUNTIME_OK) goto done;
    storage=AppRuntimeInstall_TakeStorage(i);
    if(test_sql(storage,"SELECT 7","7")) goto done;
    (void)PRuntime_StorageClose(&storage); AppRuntimeInstall_Close(&i);
    t->phase=2;
    if(test_install(t,"sql","org.positron.sql_demo",0,1000000,PRUNTIME_OK) ||
        test_install(t,"lost","org.positron.lost",1,1000000,PRUNTIME_OK) ||
        test_install(t,"unresolved","org.positron.unresolved",2,1000000,PRUNTIME_UNRESOLVED)) goto done;
    t->phase=3;
    /* Low initial VM budget fails only the expensive suffix. Recovery uses
     * the SAME bytes/identity, and CREATE/INSERT prefix must not replay. */
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALL_DIRECTORY,"prefix")!=PRUNTIME_OK) goto done;
    AppRuntimeInstall_DebugOptions(i,0,1000,0);
    rc=AppRuntimeInstall_Confirm(i); AppRuntimeInstall_Close(&i);
    if(rc!=PRUNTIME_LIMIT || AppRuntimeRegistry_FindApp(registry,"org.positron.prefix",&call->record)!=PRUNTIME_OK ||
        call->record.state!=PRUNTIME_REG_FAILED) goto done;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.prefix")!=PRUNTIME_OK ||
        AppRuntimeInstall_Action(i)!=PRUNTIME_INTENT_RETRY_FAILED || AppRuntimeInstall_Confirm(i)!=PRUNTIME_OK) goto done;
    storage=AppRuntimeInstall_TakeStorage(i);
    if(test_sql(storage,"SELECT count(*) FROM marker","8") || test_sql(storage,"SELECT count(*) FROM bulk","4096")) goto done;
    (void)PRuntime_StorageClose(&storage); AppRuntimeInstall_Close(&i);
    t->phase=4;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALL_DIRECTORY,"cancel")!=PRUNTIME_OK) goto done;
    AppRuntimeInstall_DebugOptions(i,0,1000000,1);
    rc=AppRuntimeInstall_Confirm(i); AppRuntimeInstall_Close(&i);
    if(rc!=PRUNTIME_CANCELLED || AppRuntimeRegistry_FindApp(registry,"org.positron.cancel",&call->record)!=PRUNTIME_OK ||
        call->record.state!=PRUNTIME_REG_CANCELLED) goto done;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.cancel")>=0) goto done;
    t->phase=5;
    /* Exact CAS rejects stale full snapshots and generation skips. */
    memset(call,0,sizeof(*call)); call->operation=APP_INSTALL_READ_APP; strcpy(call->key,"org.positron.nosql");
    if(AppSettingsStore_InstallCall(t->store,call)!=PRUNTIME_OK) goto done;
    call->operation=APP_INSTALL_CAS; call->previous_exists=1; call->previous=call->record; call->proposed=call->record;
    call->proposed.generation+=2;
    if(AppSettingsStore_InstallCall(t->store,call)!=PRUNTIME_INVALID || call->outcome!=PRUNTIME_COMMIT_NOT_COMMITTED || call->unsafe) goto done;
    call->proposed.generation=call->previous.generation+1; call->previous.target_schema++;
    if(AppSettingsStore_InstallCall(t->store,call)!=PRUNTIME_INVALID || call->unsafe) goto done;
    t->phase=6;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.zero")!=PRUNTIME_OK) goto done;
    if(test_open(t,&second,&duplicate,APP_RUNTIME_INSTALLED,"org.positron.zero")!=PRUNTIME_BUSY) goto done;
    storage=AppRuntimeInstall_TakeStorage(i);
    storage_info.size=sizeof(storage_info); storage_info.version=1;
    if(PRuntime_StorageGetInfo(storage,&storage_info)!=PRUNTIME_OK) goto done;
    strcpy(path,t->root); strcat(path,"\\data\\"); strcat(path,storage_info.key);
    for(bytes=0;path[bytes];++bytes) if(path[bytes]=='/') path[bytes]='\\';
    (void)PRuntime_StorageClose(&storage); AppRuntimeInstall_Close(&i);
    if(!MultiByteToWideChar(CP_UTF8,0,path,-1,wide,MAX_PATH) || !DeleteFileW(wide)) goto done;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.zero")>=0 || GetFileAttributesW(wide)!=INVALID_FILE_ATTRIBUTES) goto done;
    strcpy(missing,path);
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.prefix")!=PRUNTIME_OK) goto done;
    storage=AppRuntimeInstall_TakeStorage(i);
    if(PRuntime_StorageGetInfo(storage,&storage_info)!=PRUNTIME_OK) goto done;
    strcpy(source,t->root); strcat(source,"\\data\\"); strcat(source,storage_info.key);
    for(bytes=0;source[bytes];++bytes) if(source[bytes]=='/') source[bytes]='\\';
    (void)PRuntime_StorageClose(&storage); AppRuntimeInstall_Close(&i);
    if(!MultiByteToWideChar(CP_UTF8,0,source,-1,source_wide,MAX_PATH) || !CopyFileW(source_wide,wide,TRUE)) goto done;
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.zero")>=0 || GetFileAttributesW(wide)==INVALID_FILE_ATTRIBUTES) goto done;
    _snprintf(line,sizeof(line)-1,"runtime-install-native wrong-data source=%s target=%s\r\n",source,missing);
    line[sizeof(line)-1]=0; AppDebug_Log(line);
    t->phase=7;
    if(test_install(t,"snapshot","org.positron.snapshot",0,1000000,PRUNTIME_OK) ||
        test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.snapshot")!=PRUNTIME_OK) goto done;
    strcpy(path,t->root); strcat(path,"\\snapshot\\new.txt");
    if(test_write(path,"unapproved")) goto done;
    ref.size=sizeof(ref); ref.version=1; bytes=0;
    if(AppRuntimeInstall_Read(i,NULL,"index.html",body,sizeof(body),&bytes,&ref)!=PRUNTIME_OK ||
        AppRuntimeInstall_Read(i,NULL,"new.txt",body,sizeof(body),&bytes,&ref)>=0) goto done;
    AppRuntimeInstall_Close(&i);
    if(test_open(t,&p,&i,APP_RUNTIME_INSTALLED,"org.positron.snapshot")!=PRUNTIME_INVALID) goto done;
    t->phase=8;
    if(AppRuntimePlatform_Init(&p)!=PRUNTIME_OK) goto done;
    strcpy(path,t->root); strcat(path,"\\capture"); strcpy(t->capture_file,path); strcat(t->capture_file,"\\new.txt");
    p.capture_hook=capture_change; p.capture_user=t;
    AppRuntimePlatform_RevisionProvider(&p,&provider);
    if(PRuntime_PrepareRevision(path,(unsigned int)strlen(path),&provider,NULL,&revision)!=PRUNTIME_OK) goto done;
    do { rc=PRuntime_RevisionStep(revision); } while(rc==PRUNTIME_OK);
    if(rc!=PRUNTIME_IO || !t->changed || p.revision_root) goto done;
    (void)PRuntime_RevisionClose(&revision);
    t->phase=9;
    strcpy(source,t->root); strcat(source,"\\bounds\\manifest.json"); padding=test_file_size(source);
    strcpy(source,t->root); strcat(source,"\\bounds\\index.html"); padding+=test_file_size(source);
    if(padding>8192) goto done;
    padding=4U*1024U*1024U-padding;
    strcpy(source,t->root); strcat(source,"\\bounds\\padding.bin");
    if(test_resize(source,padding,1) || AppRuntimePlatform_Init(&p)!=PRUNTIME_OK) goto done;
    AppRuntimePlatform_RevisionProvider(&p,&provider); strcpy(path,t->root); strcat(path,"\\bounds");
    if(PRuntime_PrepareRevision(path,(unsigned int)strlen(path),&provider,NULL,&revision)!=PRUNTIME_OK) goto done;
    do { rc=PRuntime_RevisionStep(revision); } while(rc==PRUNTIME_OK);
    revision_info.size=sizeof(revision_info); revision_info.version=1;
    if(rc!=PRUNTIME_REVISION_DONE || PRuntime_RevisionGetInfo(revision,&revision_info)!=PRUNTIME_OK ||
            revision_info.file_count!=128 || revision_info.total_bytes!=4U*1024U*1024U) goto done;
    (void)PRuntime_RevisionClose(&revision);
    strcpy(missing,path); strcat(missing,"\\one-too-many.txt"); if(test_write(missing,"")) goto done;
    if(PRuntime_PrepareRevision(path,(unsigned int)strlen(path),&provider,NULL,&revision)!=PRUNTIME_OK) goto done;
    do { rc=PRuntime_RevisionStep(revision); } while(rc==PRUNTIME_OK);
    if(rc!=PRUNTIME_LIMIT || p.revision_root) goto done;
    (void)PRuntime_RevisionClose(&revision);
    if(!MultiByteToWideChar(CP_UTF8,0,missing,-1,wide,MAX_PATH) || !DeleteFileW(wide) || test_resize(source,padding+1,0)) goto done;
    if(PRuntime_PrepareRevision(path,(unsigned int)strlen(path),&provider,NULL,&revision)!=PRUNTIME_OK) goto done;
    do { rc=PRuntime_RevisionStep(revision); } while(rc==PRUNTIME_OK);
    if(rc!=PRUNTIME_LIMIT || p.revision_root) goto done;
    (void)PRuntime_RevisionClose(&revision);
    t->phase=10; t->result=0;
done:
    if(revision) (void)PRuntime_RevisionClose(&revision);
    (void)PRuntime_StorageClose(&storage);
    AppRuntimeInstall_Close(&duplicate); AppRuntimeInstall_Close(&i);
    free(call); free(registry); return 0;
}
int AppRuntimeInstall_DebugRun(void)
{
    InstallTest *t;
    AppSettingsResult result;
    WCHAR executable[MAX_PATH],dll[MAX_PATH],*slash;
    HANDLE worker;
    char database[1024],line[192];
    const WCHAR *prefix;
    int rc;
    t=(InstallTest *)calloc(1,sizeof(*t));
    if(!t) return 1;
    t->result=1; prefix=L"\\Temp\\Positron-device-gate\\app-runtime-install-";
    if(!GetModuleFileNameW(NULL,executable,MAX_PATH) || wcsncmp(executable,prefix,wcslen(prefix))) { free(t); return 1; }
    slash=wcsrchr(executable,L'\\'); if(!slash) { free(t); return 1; } *slash=0;
    wcscpy(dll,executable); wcscat(dll,L"\\positron_db.dll");
    if(!WideCharToMultiByte(CP_UTF8,0,executable,-1,t->root,sizeof(t->root),NULL,NULL)) { free(t); return 1; }
    strcpy(database,t->root); strcat(database,"\\positron.db");
    if(AppSettingsStore_Create(dll,database,&t->store)!=APP_SETTINGS_OK) { free(t); return 1; }
    memset(&result,0,sizeof(result));
    do { rc=AppSettingsStore_Poll(t->store,&result); if(rc==APP_SETTINGS_PENDING) Sleep(10); } while(rc==APP_SETTINGS_PENDING);
    worker=NULL;
    if(rc==APP_SETTINGS_OK && result.result==APP_SETTINGS_OK) worker=CreateThread(NULL,0,test_install_worker,t,0,NULL);
    AppSettingsResult_Release(&result);
    if(worker) { WaitForSingleObject(worker,INFINITE); CloseHandle(worker); }
    AppSettingsStore_RequestClose(t->store);
    while(AppSettingsStore_TryDestroy(&t->store)==APP_SETTINGS_PENDING) Sleep(10);
    _snprintf(line,sizeof(line)-1,"runtime-install-native %s phase=%d registry=persistent cas=exact recovery=explicit snapshot=immutable\r\n",
        t->result ? "FAIL" : "PASS",t->phase); line[sizeof(line)-1]=0; AppDebug_Log(line);
    rc=t->result; free(t); return rc;
}
#endif
