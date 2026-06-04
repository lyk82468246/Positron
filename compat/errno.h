/*
 * compat/errno.h - C errno shim for VS2008 + WinCE (WM6 SDK ships none).
 *
 * WinCE has no real errno facility. NetSurf includes <errno.h> defensively
 * (e.g. input/filter.c) but on our build the only code paths that actually
 * read errno are the iconv filter, which we disable via WITHOUT_ICONV_FILTER.
 * This header therefore exists mainly to satisfy the #include and to give the
 * standard C error constants their conventional MSVC values, should any
 * later-ported code reference them.
 *
 * `errno` itself is provided as a plain global. It is NOT thread-local and is
 * NOT updated by the CRT on WinCE - treat it as a best-effort placeholder.
 * If a future source genuinely depends on the CRT setting errno, revisit this.
 */

#ifndef POSITRON_COMPAT_ERRNO_H
#define POSITRON_COMPAT_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

extern int errno;

#ifdef __cplusplus
}
#endif

/* Standard C / POSIX error numbers - values match desktop MSVC <errno.h>. */
#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define EBADF           9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define EDOM            33
#define ERANGE          34
#define EDEADLK         36
#define ENAMETOOLONG    38
#define ENOLCK          39
#define ENOSYS          40
#define ENOTEMPTY       41
#define EILSEQ          42

#endif /* POSITRON_COMPAT_ERRNO_H */
