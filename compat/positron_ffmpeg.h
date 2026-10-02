/*
 * FFmpeg 3.4.14 / WM6 compiler bridge.
 * The CE CRT headers expose time_t and clock_t in different include paths;
 * make the small standard declarations used by libavutil available before
 * c99-to-c89 parses the preprocessed translation unit.
 */

#ifndef POSITRON_FFMPEG_CE_H
#define POSITRON_FFMPEG_CE_H

#ifndef _CLOCK_T_DEFINED
typedef long clock_t;
#define _CLOCK_T_DEFINED
#endif

#ifndef _TM_DEFINED
struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};
#define _TM_DEFINED
#endif

#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000
#endif

/* HAVE_* also requires a matching declaration at every call site.  Without
 * these, C89 assumes int returns and promotes float arguments to double,
 * corrupting the ARM calling convention of the runtime bridge. */
#include <stddef.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>

long long strtoll(const char *text, char **end_text, int base);
long long llabs(long long value);
int strerror_r(int error_number, char *buffer, size_t buffer_size);
int snprintf(char *buffer, size_t buffer_size, const char *format, ...);
int vsnprintf(char *buffer, size_t buffer_size, const char *format, va_list args);
int isnan(double value);
int isinf(double value);
int isfinite(double value);
double round(double value);
float roundf(float value);
double trunc(double value);
float truncf(float value);
long lrint(double value);
long lrintf(float value);
long long llrint(double value);
long long llrintf(float value);
double exp2(double value);
float exp2f(float value);
float expf(float value);
float logf(float value);
float log2f(float value);
float log10f(float value);
float powf(float x, float y);
double cbrt(double value);
float cbrtf(float value);
float sinf(float value);
float cosf(float value);
float atanf(float value);
float atan2f(float y, float x);
double hypot(double x, double y);
struct tm *gmtime_r(const time_t *clock_value, struct tm *result);
struct tm *localtime_r(const time_t *clock_value, struct tm *result);
struct tm *gmtime(const time_t *clock_value);
struct tm *localtime(const time_t *clock_value);
time_t mktime(struct tm *value);
clock_t clock(void);
size_t strftime(char *buffer, size_t capacity, const char *format,
                const struct tm *value);
void abort(void);
int rename(const char *old_path, const char *new_path);
void *bsearch(const void *key, const void *base, size_t count, size_t width,
              int (__cdecl *compare)(const void *, const void *));

#endif /* POSITRON_FFMPEG_CE_H */
