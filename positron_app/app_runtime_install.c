#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "app_runtime_install.h"
#include "app_debug.h"

struct AppRuntimeInstall {
    AppRuntimePlatform *platform;
    AppRuntimeRegistry registry;
    PRuntimeRevisionProvider resources;
    PRuntimeInstalledProvider installed_provider;
    PRuntimeRevisionHandle revision;
    PRuntimeInstallIntentHandle intent;
    PRuntimeInstallDataHandle data;
    PRuntimeInstallCommitHandle commit;
    PRuntimeStorageHandle storage;
    PRuntimeManifestHandle manifest;
    PRuntimeRegistrationRecord registration;
    PRuntimeRevisionInfo revision_info;
    PRuntimeManifestInfo info;
    PRuntimeInstallIntentInfo intent_info;
    PRuntimeInstallDataInfo data_info;
    PRuntimeInstallCommitInfo commit_info;
    PRuntimeStorageError error;
    char manifest_bytes[PRUNTIME_MANIFEST_BYTES];
    unsigned int manifest_length;
    int action;
    int (*cancelled)(void *);
    void *cancel_user;
    unsigned int budget;
#ifdef _DEBUG
    int cancel_after_first;
#endif
};
static int install_cancelled(AppRuntimeInstall *i)
{
    return i->cancelled && i->cancelled(i->cancel_user);
}
static void install_debug(AppRuntimeInstall *i,const char *phase,int rc)
{
#ifdef _DEBUG
    char line[768];
    _snprintf(line,sizeof(line)-1,
        "runtime-install phase=%s rc=%d id=%s action=%d state=%u provider=%d outcome=%u db=%d sqlite=%d/%d cleanup=%d\r\n",
        phase,rc,i->info.id,i->action,i->commit_info.state,i->commit_info.provider_result,
        i->commit_info.commit_outcome,i->error.db_result,i->error.sqlite_code,
        i->error.sqlite_extended_code,i->error.cleanup_result);
    line[sizeof(line)-1]=0; AppDebug_Log(line);
#else
    (void)i; (void)phase; (void)rc;
#endif
}
static int install_revision_complete(AppRuntimeInstall *i)
{
    int rc;
    do { rc=PRuntime_RevisionStep(i->revision); } while(rc==PRUNTIME_OK);
    if(rc!=PRUNTIME_REVISION_DONE) return rc;
    i->revision_info.size=sizeof(i->revision_info); i->revision_info.version=1;
    return PRuntime_RevisionGetInfo(i->revision,&i->revision_info);
}
static int install_metadata(AppRuntimeInstall *i)
{
    int rc;
    rc=AppRuntimePlatform_CopyManifest(i->platform,i->manifest_bytes,sizeof(i->manifest_bytes),&i->manifest_length);
    if(rc==PRUNTIME_OK) rc=PRuntime_ManifestCreate(i->manifest_bytes,i->manifest_length,&i->manifest);
    i->info.size=sizeof(i->info); i->info.version=1;
    if(rc==PRUNTIME_OK) rc=PRuntime_ManifestGetInfo(i->manifest,&i->info);
    return rc;
}
static int install_open_ready(AppRuntimeInstall *i)
{
    PRuntimeRevisionOptions options;
    PRuntimeStorageOptions storage_options;
    PRuntimeInstalledStorageProvider existing;
    int rc;
    memset(&options,0,sizeof(options)); options.size=sizeof(options); options.version=1;
    options.cancelled=i->cancelled; options.user_data=i->cancel_user;
    rc=PRuntime_OpenInstalled(i->registration.approval.installation_id,32,
        i->registration.generation,&i->installed_provider,&i->resources,&options,&i->revision);
    if(rc==PRUNTIME_OK) rc=install_revision_complete(i);
    if(rc!=PRUNTIME_OK) return rc;
    if(i->registration.approval.approved_permissions & PRUNTIME_PERMISSION_SQL) {
        memset(&storage_options,0,sizeof(storage_options)); storage_options.size=sizeof(storage_options);
        storage_options.version=1; storage_options.granted_permissions=PRUNTIME_PERMISSION_SQL;
        storage_options.vm_budget=1000000; storage_options.cancelled=i->cancelled;
        storage_options.user_data=i->cancel_user;
        AppRuntimePlatform_ExistingStorageProvider(i->platform,&existing);
        memset(&i->error,0,sizeof(i->error)); i->error.size=sizeof(i->error); i->error.version=1;
        rc=PRuntime_InstalledStorageOpen(i->revision,&storage_options,&existing,&i->storage,&i->error);
    }
    if(rc==PRUNTIME_OK) i->action=0;
    install_debug(i,"open-installed",rc); return rc;
}
int AppRuntimeInstall_Open(AppRuntimePlatform *platform,AppSettingsStore *store,
        int kind,const char *target,int (*cancelled)(void *),void *user,AppRuntimeInstall **out)
{
    AppRuntimeInstall *i;
    PRuntimeRevisionOptions options;
    const char *directory;
    int rc,found;
    if(!store || (kind!=APP_RUNTIME_INSTALL_DIRECTORY && kind!=APP_RUNTIME_INSTALLED)) return PRUNTIME_ARGUMENT;
    i=(AppRuntimeInstall *)calloc(1,sizeof(*i));
    if(!i) return PRUNTIME_MEMORY;
    i->platform=platform; i->cancelled=cancelled; i->cancel_user=user; i->budget=1000000;
    AppRuntimeRegistry_Init(&i->registry,store);
    AppRuntimeRegistry_InstalledProvider(&i->registry,&i->installed_provider);
    AppRuntimePlatform_RevisionProvider(platform,&i->resources);
    memset(&options,0,sizeof(options)); options.size=sizeof(options); options.version=1;
    options.cancelled=cancelled; options.user_data=user;
    directory=target;
    found=PRUNTIME_NOT_FOUND;
    if(kind==APP_RUNTIME_INSTALLED) {
        found=AppRuntimeRegistry_FindApp(&i->registry,target,&i->registration);
        if(found!=PRUNTIME_OK) { rc=found; goto failed; }
        if(i->registration.state==PRUNTIME_REG_READY) {
            rc=install_open_ready(i);
            if(rc==PRUNTIME_OK) rc=install_metadata(i);
            if(rc!=PRUNTIME_OK) goto failed;
            *out=i; return PRUNTIME_OK;
        }
        directory=i->registration.directory;
    }
    rc=PRuntime_PrepareRevision(directory,(unsigned int)strlen(directory),&i->resources,&options,&i->revision);
    if(rc==PRUNTIME_OK) rc=install_revision_complete(i);
    if(rc==PRUNTIME_OK) rc=install_metadata(i);
    if(rc!=PRUNTIME_OK) goto failed;
    if(kind==APP_RUNTIME_INSTALL_DIRECTORY)
        found=AppRuntimeRegistry_FindApp(&i->registry,i->info.id,&i->registration);
    if(found==PRUNTIME_NOT_FOUND) {
        i->action=PRUNTIME_INTENT_NEW;
        rc=AppRuntimeRegistry_NewId(i->registration.approval.installation_id);
        if(rc!=PRUNTIME_OK) goto failed;
    } else if(found!=PRUNTIME_OK) { rc=found; goto failed; }
    else {
        /* Selecting a recovery UI is policy, not transition admission. The
         * retained intent independently validates root/content/approval. */
        if(strcmp(i->registration.directory,i->revision_info.canonical_root)) { rc=PRUNTIME_BUSY; goto failed; }
        if(i->registration.state==PRUNTIME_REG_PREPARING) i->action=PRUNTIME_INTENT_RESUME;
        else if(i->registration.state==PRUNTIME_REG_FAILED) i->action=PRUNTIME_INTENT_RETRY_FAILED;
        else { rc=PRUNTIME_DENIED; goto failed; }
    }
    install_debug(i,"confirmation-required",PRUNTIME_OK);
    *out=i; return PRUNTIME_OK;
failed:
    install_debug(i,"open-failed",rc); AppRuntimeInstall_Close(&i); return rc;
}
int AppRuntimeInstall_Action(AppRuntimeInstall *i) { return i->action; }
int AppRuntimeInstall_CopyMetadata(AppRuntimeInstall *i,PRuntimeManifestInfo *info,
        char *manifest,unsigned int capacity,unsigned int *bytes)
{
    if(capacity<i->manifest_length) return PRUNTIME_LIMIT;
    *info=i->info; memcpy(manifest,i->manifest_bytes,i->manifest_length); *bytes=i->manifest_length;
    return PRUNTIME_OK;
}
static void install_close_preparation(AppRuntimeInstall *i)
{
    (void)PRuntime_InstallCommitClose(&i->commit);
    (void)PRuntime_InstallDataClose(&i->data);
    (void)PRuntime_InstallIntentClose(&i->intent);
    (void)PRuntime_RevisionClose(&i->revision);
}
int AppRuntimeInstall_Confirm(AppRuntimeInstall *i)
{
    PRuntimeInstallIntentOptions options;
    PRuntimeInstallIntentProvider registry;
    PRuntimeInstallDataOptions data_options;
    PRuntimeStorageProvider storage_provider;
    int rc;
    if(!i->action) return PRUNTIME_STATE;
    memset(&options,0,sizeof(options)); options.size=sizeof(options); options.version=1; options.mode=i->action;
    options.approval.size=sizeof(options.approval); options.approval.version=1;
    options.approval.confirmed=1; options.approval.revision=1;
    strcpy(options.approval.installation_id,i->registration.approval.installation_id);
    AppRuntimeRegistry_IntentProvider(&i->registry,&registry);
    rc=PRuntime_InstallIntentBegin(i->revision,&options,&registry,&i->intent);
    if(rc!=PRUNTIME_OK) return rc;
    do {
        if(install_cancelled(i)) (void)PRuntime_InstallIntentCancel(i->intent);
        rc=PRuntime_InstallIntentStep(i->intent);
    } while(rc==PRUNTIME_OK);
    i->intent_info.size=sizeof(i->intent_info); i->intent_info.version=1;
    (void)PRuntime_InstallIntentGetInfo(i->intent,&i->intent_info);
    if(rc!=PRUNTIME_INTENT_DONE) { install_debug(i,"intent-failed",rc); return rc; }
    if(i->info.requested_permissions & PRUNTIME_PERMISSION_SQL) {
        memset(&data_options,0,sizeof(data_options)); data_options.size=sizeof(data_options); data_options.version=1;
        data_options.vm_budget=i->budget; data_options.cancelled=i->cancelled; data_options.user_data=i->cancel_user;
        AppRuntimePlatform_StorageProvider(i->platform,&storage_provider);
        rc=PRuntime_InstallDataBegin(i->intent,&data_options,&storage_provider,&i->data);
        if(rc!=PRUNTIME_OK) return rc;
        do {
            if(install_cancelled(i)) (void)PRuntime_InstallDataCancel(i->data);
            rc=PRuntime_InstallDataStep(i->data);
#ifdef _DEBUG
            if(i->cancel_after_first) {
                i->data_info.size=sizeof(i->data_info); i->data_info.version=1;
                if(PRuntime_InstallDataGetInfo(i->data,&i->data_info)==PRUNTIME_OK && i->data_info.applied_files) {
                    (void)PRuntime_InstallDataCancel(i->data); i->cancel_after_first=0;
                    rc=PRUNTIME_OK; /* keep stepping until cancellation settles */
                }
            }
#endif
        } while(rc==PRUNTIME_OK);
        i->data_info.size=sizeof(i->data_info); i->data_info.version=1;
        (void)PRuntime_InstallDataGetInfo(i->data,&i->data_info); i->error=i->data_info.error;
        if(rc!=PRUNTIME_INSTALL_DATA_DONE) { install_debug(i,"data-failed",rc); return rc; }
    }
    rc=PRuntime_InstallCommitBegin(i->intent,i->data,&i->commit);
    if(rc!=PRUNTIME_OK) return rc;
    do {
        if(install_cancelled(i)) (void)PRuntime_InstallCommitCancel(i->commit);
        rc=PRuntime_InstallCommitStep(i->commit);
    } while(rc==PRUNTIME_OK);
    i->commit_info.size=sizeof(i->commit_info); i->commit_info.version=1;
    (void)PRuntime_InstallCommitGetInfo(i->commit,&i->commit_info); i->error=i->commit_info.data_error;
    install_debug(i,"commit-terminal",rc);
    if(rc!=PRUNTIME_INSTALL_COMMIT_DONE) return rc;
    i->registration=i->commit_info.record;
    install_close_preparation(i);
    /* Successful COMMIT never converts a prepare handle into authority. */
    rc=install_open_ready(i);
    return rc;
}
PRuntimeStorageHandle AppRuntimeInstall_TakeStorage(AppRuntimeInstall *i)
{
    PRuntimeStorageHandle storage;
    storage=i->storage; i->storage=NULL; return storage;
}
int AppRuntimeInstall_Read(AppRuntimeInstall *i,const char *base,const char *reference,
        void *buffer,unsigned int capacity,unsigned int *bytes,PRuntimeResourceReference *resolved)
{
    PRuntimeResourceReference ref;
    PRuntimeRevisionFile file;
    unsigned char *copy;
    unsigned int index,n,offset,got;
    int rc;
    ref.size=sizeof(ref); ref.version=1;
    rc=PRuntime_ManifestResolveReference(i->manifest,base,base ? (unsigned int)strlen(base) : 0,
        reference,(unsigned int)strlen(reference),&ref);
    if(rc!=PRUNTIME_OK) return rc;
    for(index=0;index<i->revision_info.file_count;++index) {
        file.size=sizeof(file); file.version=1;
        rc=PRuntime_RevisionGetFile(i->revision,index,&file);
        if(rc!=PRUNTIME_OK) return rc;
        if(!strcmp(file.path,ref.path)) break;
    }
    if(index==i->revision_info.file_count) return PRUNTIME_NOT_FOUND;
    if(file.file_bytes>capacity) return PRUNTIME_LIMIT;
    copy=(unsigned char *)malloc(file.file_bytes ? file.file_bytes : 1);
    if(!copy) return PRUNTIME_MEMORY;
    offset=0;
    do {
        n=file.file_bytes-offset; if(n>65536U) n=65536U;
        got=0;
        rc=PRuntime_InstalledReadFile(i->revision,index,offset,copy+offset,n ? n : 1,&got);
        if(rc!=PRUNTIME_OK || got!=n) break;
        offset+=got;
    } while(offset<file.file_bytes);
    if(rc==PRUNTIME_OK && offset==file.file_bytes) {
        if(offset) memcpy(buffer,copy,offset);
        *bytes=offset; *resolved=ref;
    } else if(rc==PRUNTIME_OK) rc=PRUNTIME_IO;
    free(copy); return rc;
}
void AppRuntimeInstall_Close(AppRuntimeInstall **slot)
{
    AppRuntimeInstall *i;
    if(!slot || !*slot) return;
    i=*slot;
    (void)PRuntime_StorageClose(&i->storage);
    install_close_preparation(i);
    (void)PRuntime_ManifestClose(&i->manifest);
    free(i); *slot=NULL;
}
#ifdef _DEBUG
void AppRuntimeInstall_DebugOptions(AppRuntimeInstall *i,int fault,unsigned int budget,int cancel_after_first)
{
    i->registry.fault=fault; i->budget=budget; i->cancel_after_first=cancel_after_first;
}
#endif
