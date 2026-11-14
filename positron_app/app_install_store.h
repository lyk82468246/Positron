/* EXE registration persistence messages; Runtime owns transition semantics. */
#ifndef POSITRON_APP_INSTALL_STORE_H
#define POSITRON_APP_INSTALL_STORE_H
#include "positron_runtime.h"
#include "../positron_db/positron_db.h"

#define APP_INSTALL_READ_ID 1
#define APP_INSTALL_READ_APP 2
#define APP_INSTALL_CAS 3
#define APP_UPDATE_READ 4
#define APP_UPDATE_CAS 5
#define APP_INSTALL_CHECK_REFERENCES 6
#define APP_INSTALLED_LIST_MAX 16
typedef struct AppInstalledList {
    int count, more;
    struct AppInstalledRow {
        char id[64];
        char installation_id[33];
        unsigned int version;
        unsigned int revision, generation;
    } rows[APP_INSTALLED_LIST_MAX];
} AppInstalledList;
int AppInstallStore_List(HMODULE module,PDbHandle db,AppInstalledList *list);
/* DB-owner read only. BUSY means a live root overlaps the candidate.
 * This result is only a snapshot and does not authorize file deletion. */
int AppInstallStore_CheckResourceReferences(HMODULE module,PDbHandle db,const char *root);
typedef struct AppInstallCall {
    int operation;
    char key[64];
    const char *resource_root; /* borrowed until synchronous RPC returns */
    int previous_exists;
    PRuntimeRegistrationRecord previous;
    PRuntimeRegistrationRecord proposed;
    PRuntimeRegistrationRecord record;
    PRuntimeUpdateRegistration *update_record;
    const PRuntimeUpdateRegistration *update_expected;
    const PRuntimeUpdateRegistration *update_proposed;
    int result;
    unsigned int outcome;
    PDbErrorInfo error;
    int cleanup_result;
    int unsafe;
#ifdef _DEBUG
    int fault; /* native fixture only: 1 lost reply, 2 readback unavailable */
#endif
} AppInstallCall;
const char *AppInstallStore_Schema(void);
const char *AppInstallStore_UpdateSchema(void);
/* Settings DB owner only. Settles every statement/transaction before return. */
void AppInstallStore_Process(HMODULE module, PDbHandle db, AppInstallCall *call);
int AppInstallStore_RecordEqual(const PRuntimeRegistrationRecord *a,
        const PRuntimeRegistrationRecord *b);
#endif
