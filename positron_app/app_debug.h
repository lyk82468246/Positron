/*
 * positron_app/app_debug.h - EXE-private Debug diagnostics.
 *
 * Diagnostics are deliberately absent from Release.  The Debug sink mirrors
 * each message to the WM6 debugger and to a bounded device file so a RAPI
 * capture can run without an attached Visual Studio session.
 */

#ifndef POSITRON_APP_DEBUG_H
#define POSITRON_APP_DEBUG_H

/* Per-getter tracing performs synchronous device I/O. Opt in only when
 * diagnosing the DOM bridge, not when measuring ordinary interaction. */
#ifndef APP_DEBUG_DOM_TRACE
#define APP_DEBUG_DOM_TRACE 0
#endif

#ifdef _DEBUG
void AppDebug_BeginSession(void);
void AppDebug_EndSession(void);
void AppDebug_Log(const char *message);
void AppDebug_LogElapsed(const char *phase, unsigned long started);
#else
#define AppDebug_BeginSession() ((void) 0)
#define AppDebug_EndSession() ((void) 0)
#define AppDebug_Log(message) ((void) 0)
#define AppDebug_LogElapsed(phase, started) ((void) 0)
#endif

#endif /* POSITRON_APP_DEBUG_H */
