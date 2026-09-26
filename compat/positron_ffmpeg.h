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

#endif /* POSITRON_FFMPEG_CE_H */
