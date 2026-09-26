/*
 * compat/sys/stat.h - the FFmpeg WM6 port does not use filesystem URLs.
 *
 * FFmpeg 3.4 includes <sys/stat.h> from its generic os_support.h even when
 * the application supplies a custom AVIOContext.  Windows Mobile has no
 * POSIX sys/stat.h, and the desktop VS2008 header is not ABI-compatible with
 * the CE CRT.  Keep only the layout needed by headers; the file protocol is
 * disabled from the Positron build.
 */

#ifndef POSITRON_COMPAT_SYS_STAT_H
#define POSITRON_COMPAT_SYS_STAT_H

#include <time.h>

#ifndef S_IREAD
#define S_IREAD 0400
#endif
#ifndef S_IWRITE
#define S_IWRITE 0200
#endif

typedef unsigned long _dev_t;
typedef unsigned long _ino_t;
typedef long _off_t;

struct stat {
	_dev_t st_dev;
	_ino_t st_ino;
	unsigned short st_mode;
	short st_nlink;
	short st_uid;
	short st_gid;
	_dev_t st_rdev;
	_off_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
};

#ifndef S_ISFIFO
#define S_ISFIFO(mode) (0)
#endif

#endif /* POSITRON_COMPAT_SYS_STAT_H */
