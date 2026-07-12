/*
 * Expat entropy adapter for Windows Mobile.
 *
 * Desktop Expat uses rand_s(), which is unavailable in WinCE coredll.
 * CryptoAPI is the same proven primary entropy source as positron_tls.
 */

#include <windows.h>

/* WinCE has no CRT errno export. Expat only clears/checks this on paths
 * which are inactive here (environment debug options and non-Windows random
 * providers), but the reference still has to remain linkable. The project
 * maps errno to this private symbol instead of exporting a process-wide one. */
#ifdef __cplusplus
extern "C" {
#endif
int positron_expat_errno = 0;
#ifdef __cplusplus
}
#endif
#include <wincrypt.h>
#include <stdbool.h>
#include <stddef.h>

#include "expat/random_rand_s.h"

void positron_GetSystemTimeAsFileTime(FILETIME *file_time)
{
    SYSTEMTIME system_time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, file_time);
}

char *getenv(const char *name)
{
    (void) name;
    return NULL;
}

bool writeRandomBytes_rand_s(void *target, size_t count)
{
    HCRYPTPROV provider;
    BOOL ok;

    if (target == NULL || count == 0 || count > 0xffffffffUL) {
        return false;
    }

    provider = 0;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        return false;
    }

    ok = CryptGenRandom(provider, (DWORD) count, (BYTE *) target);
    CryptReleaseContext(provider, 0);
    return ok ? true : false;
}
