/* EXE registration persistence messages; Runtime owns transition semantics. */
#ifndef POSITRON_APP_INSTALL_STORE_H
#define POSITRON_APP_INSTALL_STORE_H
#include "positron_runtime.h"
#include "../positron_db/positron_db.h"

#define APP_INSTALL_READ_ID 1
#define APP_INSTALL_READ_APP 2
#define APP_INSTALL_CAS 3
typedef struct AppInstallCall {
    int operation;
    char key[64];
    int previous_exists;
    PRuntimeRegistrationRecord previous;
    PRuntimeRegistrationRecord proposed;
    PRuntimeRegistrationRecord record;
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
/* Settings DB owner only. Settles every statement/transaction before return. */
void AppInstallStore_Process(HMODULE module, PDbHandle db, AppInstallCall *call);
int AppInstallStore_RecordEqual(const PRuntimeRegistrationRecord *a,
        const PRuntimeRegistrationRecord *b);
#endif
