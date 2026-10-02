/* EXE-private About identity. No public DLL ABI or package version changes. */
#ifndef POSITRON_APP_VERSION_H
#define POSITRON_APP_VERSION_H

#define APP_VERSION_TEXT_MAX 64
/* 0: complete ASCII value copied; 1: unavailable/invalid/insufficient space.
 * Failure clears the caller's output when writable. Debug uses a frozen build
 * host local timestamp; Release reads the CAB-installed registry value only. */
int AppVersion_Get(char *out_text, unsigned int capacity);
#ifdef _DEBUG
int AppVersion_DebugCheck(void);
#endif

#endif
