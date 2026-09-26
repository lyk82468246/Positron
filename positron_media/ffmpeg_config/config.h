/*
 * Public-header configuration for the fixed Positron FFmpeg 3.4.14 build.
 * The complete configure result is recorded in POSITRON_PORT.md; this small
 * header supplies the values needed when a consumer includes FFmpeg headers
 * from the ARMV4I DLL project.
 */
#ifndef POSITRON_FFMPEG_CONFIG_H
#define POSITRON_FFMPEG_CONFIG_H

#define HAVE_AV_CONFIG_H 1
#define CONFIG_SMALL 1
#define CONFIG_SAFE_BITSTREAM_READER 1
#define CONFIG_SHARED 0
#define CONFIG_NETWORK 0
#define CONFIG_THREADS 0
#define HAVE_THREADS 0
#define HAVE_PTHREADS 0
#define HAVE_W32THREADS 0
#define HAVE_OS2THREADS 0
#define HAVE_INLINE_ASM 0
#define HAVE_FAST_UNALIGNED 0
#define HAVE_ARMV5TE 0
#define HAVE_ARMV6 0
#define HAVE_ARMV6T2 0
#define HAVE_NEON 0
#define HAVE_VFP 0
#define HAVE_VFPV3 0
#define HAVE_ARMV5TE_INLINE 0
#define HAVE_ARMV6_INLINE 0
#define HAVE_ARMV6T2_INLINE 0
#define HAVE_NEON_INLINE 0
#define HAVE_VFP_INLINE 0
#define HAVE_VFPV3_INLINE 0
#define HAVE_ATOMICS_NATIVE 0
#define HAVE_ATOMICS_GCC 0
#define HAVE_ATOMICS_WIN32 0
#define HAVE_ATOMICS_SUNCC 0
#define HAVE_BIGENDIAN 0
#define HAVE_MMX 0
#define HAVE_SSE 0
#define HAVE_SSE2 0
#define HAVE_SSE3 0
#define HAVE_SSSE3 0
#define HAVE_SSE4 0
#define HAVE_SSE42 0
#define HAVE_AVX 0
#define HAVE_ALTIVEC 0
#define HAVE_MACH_MACH_TIME_H 0
#define HAVE_MACH_ABSOLUTE_TIME 0
#define HAVE_GETHRTIME 0
#define HAVE_GMTIME_R 0
#define HAVE_LOCALTIME_R 0
#define HAVE_GETTIMEOFDAY 0
#define HAVE_GETSYSTEMTIMEASFILETIME 0
#define HAVE_USLEEP 0
#define HAVE_NANOSLEEP 0
#define HAVE_SLEEP 1
#define HAVE_STRERROR_R 0
#define HAVE_ISATTY 0
#define HAVE_POSIX_MEMALIGN 0
#define HAVE_MEMALIGN 0
#define HAVE_ALIGNED_MALLOC 0
#define HAVE_MALLOC_H 0
#define HAVE_UNISTD_H 0
#define HAVE_WINDOWS_H 1
#define HAVE_DOS_PATHS 1
#define HAVE_PRAGMA_DEPRECATED 1
#define HAVE_SYMVER 0
#define HAVE_SYMVER_ASM_LABEL 0
#define HAVE_SYMVER_GNU_ASM 0
#define HAVE_CABS 0
#define HAVE_CEXP 0
#define HAVE_RINT 1
#define HAVE_ATANF 1
#define HAVE_ATAN2F 1
#define HAVE_CBRT 1
#define HAVE_CBRTF 1
#define HAVE_COPYSIGN 1
#define HAVE_COSF 1
#define HAVE_EXP2 1
#define HAVE_EXP2F 1
#define HAVE_EXPF 1
#define HAVE_HYPOT 1
#define HAVE_ISFINITE 1
#define HAVE_ISINF 1
#define HAVE_ISNAN 1
#define HAVE_LLRINT 1
#define HAVE_LLRINTF 1
#define HAVE_LOG2 0
#define HAVE_LOG2F 1
#define HAVE_LOG10F 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define HAVE_POWF 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_SINF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1

#endif /* POSITRON_FFMPEG_CONFIG_H */
