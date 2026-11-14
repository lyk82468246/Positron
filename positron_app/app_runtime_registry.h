#ifndef POSITRON_APP_RUNTIME_REGISTRY_H
#define POSITRON_APP_RUNTIME_REGISTRY_H
#include "app_settings_store.h"
typedef struct AppRuntimeRegistry {
    AppSettingsStore *store;
    AppInstallCall call;
    unsigned int leases;
#ifdef _DEBUG
    int fault, fail_reads;
    unsigned int writes;
#endif
} AppRuntimeRegistry;
void AppRuntimeRegistry_Init(AppRuntimeRegistry *registry, AppSettingsStore *store);
int AppRuntimeRegistry_FindApp(AppRuntimeRegistry *registry, const char *id,
        PRuntimeRegistrationRecord *record);
void AppRuntimeRegistry_IntentProvider(AppRuntimeRegistry *registry,
        PRuntimeInstallIntentProvider *provider);
void AppRuntimeRegistry_InstalledProvider(AppRuntimeRegistry *registry,
        PRuntimeInstalledProvider *provider);
int AppRuntimeRegistry_FindUpdate(AppRuntimeRegistry *registry,
        const char *installation, PRuntimeUpdateRegistration *record);
void AppRuntimeRegistry_UpdateProvider(AppRuntimeRegistry *registry,
        PRuntimeUpdateProvider *provider);
int AppRuntimeRegistry_NewId(char output[33]);
int AppRuntimeRegistry_Revoke(AppRuntimeRegistry *registry,
        const PRuntimeRegistrationRecord *expected,int (*cancelled)(void *),void *user);
int AppRuntimeRegistry_Restore(AppRuntimeRegistry *registry,PRuntimeRevisionHandle prepared,
        const PRuntimeRegistrationRecord *expected,const char *root,
        const PRuntimeInstalledStorageProvider *storage,int (*cancelled)(void *),void *user);
#endif
