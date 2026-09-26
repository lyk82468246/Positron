/*
 * FFmpeg 3.4.14 runtime bridge for the Windows Mobile 6 CRT.
 *
 * The FFmpeg source set is compiled without the POSIX file/network layer.
 * This file supplies only the small C/POSIX surface still referenced by the
 * selected libavutil/libavformat objects.  It deliberately has no public
 * Positron ABI and is not used by the other DLLs.
 */

#include <windows.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* The WM6 ARMV4I SDK predates the standard stdint.h header. */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

#ifdef isnan
#undef isnan
#endif
#ifdef isinf
#undef isinf
#endif
#ifdef isfinite
#undef isfinite
#endif
#ifdef round
#undef round
#endif
#ifdef roundf
#undef roundf
#endif
#ifdef trunc
#undef trunc
#endif
#ifdef truncf
#undef truncf
#endif

/* The CE CRT has no errno object compatible with the preprocessed FFmpeg. */
int errno = 0;

static double pm_round_double(double value)
{
    if (value >= 0.0) {
        return floor(value + 0.5);
    }
    return ceil(value - 0.5);
}

static double pm_trunc_double(double value)
{
    if (value >= 0.0) {
        return floor(value);
    }
    return ceil(value);
}

static int pm_digit_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

int _isatty(int fd)
{
    (void)fd;
    return 0;
}

int isatty(int fd)
{
    return _isatty(fd);
}

long long strtoll(const char *text, char **end_text, int base)
{
    const char *p;
    int negative;
    int digit;
    int any;
    unsigned long long value;

    p = text;
    negative = 0;
    any = 0;
    value = 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        p++;
    }
    if (base == 0) {
        base = 10;
        if (*p == '0') {
            base = 8;
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
            }
        }
    } else if (base == 16 && p[0] == '0' &&
               (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    while ((digit = pm_digit_value((unsigned char)*p)) >= 0 && digit < base) {
        any = 1;
        value = value * (unsigned int)base + (unsigned int)digit;
        p++;
    }
    if (end_text != NULL) {
        *end_text = (char *)(any ? p : text);
    }
    if (!any) {
        errno = 22;
        return 0;
    }
    if (negative) {
        return -(long long)value;
    }
    return (long long)value;
}

int strerror_r(int error_number, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) return 22;
    _snprintf(buffer, buffer_size, "errno %d", error_number);
    buffer[buffer_size - 1] = '\0';
    return 0;
}

int snprintf(char *buffer, size_t buffer_size, const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = _vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
    if (buffer != NULL && buffer_size != 0) buffer[buffer_size - 1] = '\0';
    return result;
}

int vsnprintf(char *buffer, size_t buffer_size, const char *format, va_list args)
{
    int result;
    result = _vsnprintf(buffer, buffer_size, format, args);
    if (buffer != NULL && buffer_size != 0) buffer[buffer_size - 1] = '\0';
    return result;
}

int isnan(double value)
{
    return _isnan(value);
}

int isinf(double value)
{
    if (isnan(value)) return 0;
    return value == HUGE_VAL || value == -HUGE_VAL;
}

int isfinite(double value)
{
    return !isnan(value) && !isinf(value);
}

double round(double value)
{
    return pm_round_double(value);
}

float roundf(float value)
{
    return (float)pm_round_double((double)value);
}

double trunc(double value)
{
    return pm_trunc_double(value);
}

float truncf(float value)
{
    return (float)pm_trunc_double((double)value);
}

long lrint(double value)
{
    return (long)pm_round_double(value);
}

long lrintf(float value)
{
    return (long)pm_round_double((double)value);
}

long long llrint(double value)
{
    return (long long)pm_round_double(value);
}

long long llrintf(float value)
{
    return (long long)pm_round_double((double)value);
}

double exp2(double value)
{
    return pow(2.0, value);
}

float exp2f(float value)
{
    return (float)pow(2.0, (double)value);
}

float log2f(float value)
{
    return (float)(log((double)value) / log(2.0));
}

float log10f(float value)
{
    return (float)log10((double)value);
}

float powf(float x, float y)
{
    return (float)pow((double)x, (double)y);
}

double cbrt(double value)
{
    if (value < 0.0) return -pow(-value, 1.0 / 3.0);
    return pow(value, 1.0 / 3.0);
}

float cbrtf(float value)
{
    return (float)cbrt((double)value);
}

float sinf(float value)
{
    return (float)sin((double)value);
}

float cosf(float value)
{
    return (float)cos((double)value);
}

float atanf(float value)
{
    return (float)atan((double)value);
}

float atan2f(float y, float x)
{
    return (float)atan2((double)y, (double)x);
}

double hypot(double x, double y)
{
    return _hypot(x, y);
}

long long llabs(long long value)
{
    return value < 0 ? -value : value;
}

clock_t clock(void)
{
    return (clock_t)GetTickCount();
}

static int pm_is_leap_year(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int pm_days_before_month(int year, int month)
{
    static const int days[] = { 0, 31, 59, 90, 120, 151,
                                 181, 212, 243, 273, 304, 334 };
    int result;
    result = days[month - 1];
    if (month > 2 && pm_is_leap_year(year)) result++;
    return result;
}

static int pm_filetime_to_tm(time_t seconds, struct tm *result)
{
    SYSTEMTIME system_time;
    FILETIME file_time;
    unsigned long long ticks;
    long long absolute_seconds;
    long long days;
    int wday;

    absolute_seconds = (long long)seconds + 11644473600LL;
    if (absolute_seconds < 0) return 0;
    ticks = (unsigned long long)absolute_seconds * 10000000ULL;
    file_time.dwLowDateTime = (DWORD)ticks;
    file_time.dwHighDateTime = (DWORD)(ticks >> 32);
    if (!FileTimeToSystemTime(&file_time, &system_time)) return 0;
    result->tm_sec = system_time.wSecond;
    result->tm_min = system_time.wMinute;
    result->tm_hour = system_time.wHour;
    result->tm_mday = system_time.wDay;
    result->tm_mon = system_time.wMonth - 1;
    result->tm_year = system_time.wYear - 1900;
    result->tm_yday = pm_days_before_month(system_time.wYear,
                                            system_time.wMonth) +
                      system_time.wDay - 1;
    days = (long long)seconds / 86400LL;
    if (seconds < 0 && seconds % 86400LL) days--;
    wday = (int)((days + 4) % 7);
    if (wday < 0) wday += 7;
    result->tm_wday = wday;
    result->tm_isdst = 0;
    return 1;
}

static time_t pm_tm_to_time(const struct tm *value)
{
    SYSTEMTIME system_time;
    FILETIME file_time;
    unsigned long long ticks;
    int year;
    int month;

    if (value == NULL) return (time_t)-1;
    year = value->tm_year + 1900;
    month = value->tm_mon + 1;
    if (year < 1601 || month < 1 || month > 12) return (time_t)-1;
    memset(&system_time, 0, sizeof(system_time));
    system_time.wYear = (WORD)year;
    system_time.wMonth = (WORD)month;
    system_time.wDay = (WORD)value->tm_mday;
    system_time.wHour = (WORD)value->tm_hour;
    system_time.wMinute = (WORD)value->tm_min;
    system_time.wSecond = (WORD)value->tm_sec;
    if (!SystemTimeToFileTime(&system_time, &file_time)) return (time_t)-1;
    ticks = ((unsigned long long)file_time.dwHighDateTime << 32) |
            (unsigned long long)file_time.dwLowDateTime;
    return (time_t)(ticks / 10000000ULL - 11644473600ULL);
}

int64_t gethrtime(void)
{
    return (int64_t)GetTickCount() * 1000;
}

int64_t av_gettime(void)
{
    SYSTEMTIME system_time;
    FILETIME file_time;
    unsigned long long ticks;

    GetSystemTime(&system_time);
    if (!SystemTimeToFileTime(&system_time, &file_time)) return -1;
    ticks = ((unsigned long long)file_time.dwHighDateTime << 32) |
            (unsigned long long)file_time.dwLowDateTime;
    return (int64_t)(ticks / 10ULL - 11644473600000000ULL);
}

int64_t av_gettime_relative(void)
{
    return (int64_t)GetTickCount() * 1000;
}

int av_gettime_relative_is_monotonic(void)
{
    return 1;
}

int av_usleep(unsigned usec)
{
    DWORD milliseconds;
    milliseconds = (DWORD)(usec / 1000U);
    if (milliseconds == 0 && usec != 0) milliseconds = 1;
    Sleep(milliseconds);
    return 0;
}

struct tm *gmtime_r(const time_t *clock_value, struct tm *result)
{
    if (clock_value == NULL || result == NULL ||
        !pm_filetime_to_tm(*clock_value, result)) return NULL;
    return result;
}

struct tm *localtime_r(const time_t *clock_value, struct tm *result)
{
    /* WM6 timezone conversion is device-specific; FFmpeg uses this only for
     * optional metadata.  UTC keeps parsing deterministic and bounded. */
    return gmtime_r(clock_value, result);
}

struct tm *gmtime(const time_t *clock_value)
{
    static struct tm result;
    return gmtime_r(clock_value, &result);
}

struct tm *localtime(const time_t *clock_value)
{
    static struct tm result;
    return localtime_r(clock_value, &result);
}

time_t mktime(struct tm *value)
{
    return pm_tm_to_time(value);
}

void *bsearch(const void *key, const void *base, size_t count, size_t width,
              int (__cdecl *compare)(const void *, const void *))
{
    size_t low;
    size_t high;
    size_t middle;
    const unsigned char *element;
    int result;

    if (key == NULL || base == NULL || width == 0 || compare == NULL) {
        return NULL;
    }
    low = 0;
    high = count;
    while (low < high) {
        middle = low + (high - low) / 2;
        element = (const unsigned char *)base + middle * width;
        result = compare(key, element);
        if (result == 0) return (void *)element;
        if (result < 0) high = middle;
        else low = middle + 1;
    }
    return NULL;
}

static int pm_two_digits(char *buffer, size_t capacity, size_t *at, int value)
{
    if (*at + 2 >= capacity) return 0;
    buffer[(*at)++] = (char)('0' + (value / 10) % 10);
    buffer[(*at)++] = (char)('0' + value % 10);
    return 1;
}

size_t strftime(char *buffer, size_t capacity, const char *format,
                const struct tm *value)
{
    size_t at;
    const char *p;
    int number;

    if (buffer == NULL || capacity == 0 || format == NULL || value == NULL) {
        return 0;
    }
    at = 0;
    p = format;
    while (*p != '\0') {
        if (*p != '%') {
            if (at + 1 >= capacity) return 0;
            buffer[at++] = *p++;
            continue;
        }
        p++;
        switch (*p++) {
        case '%':
            if (at + 1 >= capacity) return 0;
            buffer[at++] = '%';
            break;
        case 'Y':
            number = value->tm_year + 1900;
            if (at + 4 >= capacity) return 0;
            _snprintf(buffer + at, capacity - at, "%04d", number);
            at += 4;
            break;
        case 'm':
            if (!pm_two_digits(buffer, capacity, &at, value->tm_mon + 1)) return 0;
            break;
        case 'd':
            if (!pm_two_digits(buffer, capacity, &at, value->tm_mday)) return 0;
            break;
        case 'H':
            if (!pm_two_digits(buffer, capacity, &at, value->tm_hour)) return 0;
            break;
        case 'M':
            if (!pm_two_digits(buffer, capacity, &at, value->tm_min)) return 0;
            break;
        case 'S':
            if (!pm_two_digits(buffer, capacity, &at, value->tm_sec)) return 0;
            break;
        default:
            return 0;
        }
    }
    buffer[at] = '\0';
    return at;
}

void abort(void)
{
    TerminateProcess((HANDLE)-1, 3);
    for (;;) Sleep(1000);
}
