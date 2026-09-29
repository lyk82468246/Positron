/*
 * positron_app/app_debug.h - EXE-private Debug diagnostics.
 *
 * Diagnostics are deliberately absent from Release.  The Debug sink mirrors
 * each message to the WM6 debugger and to a bounded device file so a RAPI
 * capture can run without an attached Visual Studio session.
 */

#ifndef POSITRON_APP_DEBUG_H
#define POSITRON_APP_DEBUG_H

#ifdef _DEBUG
void AppDebug_BeginSession(void);
void AppDebug_Log(const char *message);
#else
#define AppDebug_BeginSession() ((void) 0)
#define AppDebug_Log(message) ((void) 0)
#endif

#endif /* POSITRON_APP_DEBUG_H */
