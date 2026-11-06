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
int AppRuntimeRegistry_NewId(char output[33]);
#endif
