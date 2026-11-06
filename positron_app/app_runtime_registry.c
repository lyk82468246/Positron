#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wincrypt.h>
#include "app_runtime_registry.h"

typedef struct AppRegistrationLease {
    HANDLE app, installation;
    char id[64], installation_id[33];
} AppRegistrationLease;

void AppRuntimeRegistry_Init(AppRuntimeRegistry *r,AppSettingsStore *store)
{
    memset(r,0,sizeof(*r)); r->store=store;
}

static int registry_load(AppRuntimeRegistry *r,int app,const char *id,
        PRuntimeRegistrationRecord *record)
{
    int rc;
#ifdef _DEBUG
    if(r->fail_reads) { --r->fail_reads; return PRUNTIME_IO; }
#endif
    if(!id || strlen(id)>=sizeof(r->call.key)) return PRUNTIME_ARGUMENT;
    memset(&r->call,0,sizeof(r->call));
    r->call.operation=app ? APP_INSTALL_READ_APP : APP_INSTALL_READ_ID;
    strcpy(r->call.key,id);
    rc=AppSettingsStore_InstallCall(r->store,&r->call);
    if(rc==PRUNTIME_OK) *record=r->call.record;
    return rc;
}
int AppRuntimeRegistry_FindApp(AppRuntimeRegistry *r,const char *id,PRuntimeRegistrationRecord *record)
{
    return registry_load(r,1,id,record);
}
static int registry_mutex(const char *prefix,const char *id,HANDLE *output)
{
    WCHAR name[192];
    int count;
    HANDLE mutex;
    DWORD error;
    count=MultiByteToWideChar(CP_UTF8,0,prefix,-1,name,192);
    if(count<1 || MultiByteToWideChar(CP_UTF8,0,id,-1,name+count-1,193-count)<1) return PRUNTIME_LIMIT;
    SetLastError(0); mutex=CreateMutexW(NULL,FALSE,name); error=GetLastError();
    if(!mutex) return PRUNTIME_IO;
    if(error==ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return PRUNTIME_BUSY; }
    *output=mutex; return PRUNTIME_OK;
}
static int registry_acquire(void *user,const char *id,const char *installation,void **out)
{
    AppRuntimeRegistry *r;
    AppRegistrationLease *lease;
    int rc;
    r=(AppRuntimeRegistry *)user;
    if(!id || strlen(id)>63 || !installation || strlen(installation)!=32) return PRUNTIME_INVALID;
    lease=(AppRegistrationLease *)calloc(1,sizeof(*lease));
    if(!lease) return PRUNTIME_MEMORY;
    strcpy(lease->id,id); strcpy(lease->installation_id,installation);
    *out=lease; ++r->leases;
    rc=registry_mutex("Positron.Runtime.App.",id,&lease->app);
    if(rc==PRUNTIME_OK) rc=registry_mutex("Positron.Runtime.Install.",installation,&lease->installation);
    return rc;
}
static void registry_close(void *user,void *token)
{
    AppRuntimeRegistry *r;
    AppRegistrationLease *lease;
    r=(AppRuntimeRegistry *)user; lease=(AppRegistrationLease *)token;
    if(!lease) return;
    if(lease->installation) CloseHandle(lease->installation);
    if(lease->app) CloseHandle(lease->app);
    --r->leases; free(lease);
}
static int registry_read(void *user,void *token,PRuntimeRegistrationRecord *record)
{
    AppRuntimeRegistry *r;
    AppRegistrationLease *lease;
    int rc;
    r=(AppRuntimeRegistry *)user; lease=(AppRegistrationLease *)token;
    if(!lease || !lease->app || !lease->installation) return PRUNTIME_STATE;
    rc=registry_load(r,0,lease->installation_id,record);
    if(rc==PRUNTIME_OK && strcmp(record->approval.id,lease->id)) return PRUNTIME_INVALID;
    return rc;
}
static int registry_cas(void *user,void *token,const PRuntimeRegistrationRecord *old,
        const PRuntimeRegistrationRecord *next,unsigned int *outcome)
{
    AppRuntimeRegistry *r;
    AppRegistrationLease *lease;
    int rc;
    *outcome=PRUNTIME_COMMIT_NOT_COMMITTED;
    r=(AppRuntimeRegistry *)user; lease=(AppRegistrationLease *)token;
    if(!lease || !lease->app || !lease->installation ||
            strcmp(next->approval.id,lease->id) || strcmp(next->approval.installation_id,lease->installation_id)) return PRUNTIME_INVALID;
    memset(&r->call,0,sizeof(r->call)); r->call.operation=APP_INSTALL_CAS;
    r->call.previous_exists=old!=NULL;
    if(old) r->call.previous=*old;
    r->call.proposed=*next; r->call.outcome=*outcome;
#ifdef _DEBUG
    ++r->writes;
    if(r->fault && next->state==PRUNTIME_REG_READY) r->call.fault=1;
#endif
    rc=AppSettingsStore_InstallCall(r->store,&r->call);
    *outcome=r->call.outcome;
#ifdef _DEBUG
    if(r->fault && next->state==PRUNTIME_REG_READY) {
        if(r->fault==2) r->fail_reads=1;
        r->fault=0;
    }
#endif
    return rc;
}
void AppRuntimeRegistry_IntentProvider(AppRuntimeRegistry *r,PRuntimeInstallIntentProvider *p)
{
    memset(p,0,sizeof(*p)); p->size=sizeof(*p); p->version=1; p->user_data=r;
    p->acquire=registry_acquire; p->read=registry_read; p->compare_exchange=registry_cas; p->close=registry_close;
}
static int registry_active(void *user,void *token,PRuntimeInstalledRecord *out)
{
    PRuntimeRegistrationRecord *record;
    int rc;
    record=(PRuntimeRegistrationRecord *)malloc(sizeof(*record));
    if(!record) return PRUNTIME_MEMORY;
    rc=registry_read(user,token,record);
    if(rc==PRUNTIME_OK && record->state!=PRUNTIME_REG_READY) rc=PRUNTIME_DENIED;
    if(rc==PRUNTIME_OK) {
        memset(out,0,sizeof(*out)); out->size=sizeof(*out); out->version=1;
        out->generation=record->generation; out->active=1;
        strcpy(out->directory,record->directory); out->approval=record->approval;
    }
    free(record); return rc;
}
static int registry_open(void *user,const char *installation,PRuntimeInstalledRecord *out,void **token)
{
    AppRuntimeRegistry *r;
    int rc;
    r=(AppRuntimeRegistry *)user;
    rc=registry_load(r,0,installation,&r->call.record);
    if(rc!=PRUNTIME_OK) return rc;
    /* acquire copies id before the next RPC may overwrite its scratch. */
    rc=registry_acquire(user,r->call.record.approval.id,installation,token);
    return rc==PRUNTIME_OK ? registry_active(user,*token,out) : rc;
}
void AppRuntimeRegistry_InstalledProvider(AppRuntimeRegistry *r,PRuntimeInstalledProvider *p)
{
    memset(p,0,sizeof(*p)); p->size=sizeof(*p); p->version=1; p->user_data=r;
    p->open=registry_open; p->refresh=registry_active; p->close=registry_close;
}
int AppRuntimeRegistry_NewId(char output[33])
{
    HCRYPTPROV provider;
    unsigned char bytes[16];
    unsigned int i,nonzero;
    const char *digits;
    provider=0; nonzero=0; digits="0123456789abcdef";
    if(!CryptAcquireContext(&provider,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)) return PRUNTIME_UNSUPPORTED;
    if(!CryptGenRandom(provider,16,bytes)) { CryptReleaseContext(provider,0); return PRUNTIME_IO; }
    CryptReleaseContext(provider,0);
    for(i=0;i<16;++i) { nonzero|=bytes[i]; output[i*2]=digits[bytes[i]>>4]; output[i*2+1]=digits[bytes[i]&15]; }
    output[32]=0; return nonzero ? PRUNTIME_OK : PRUNTIME_IO;
}
