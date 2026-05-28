/*
 * inttypes.h - Minimal C99 format-specifier macros for MSVC 9.0 (VS2008).
 *
 * VS2008 does not ship <inttypes.h>; mbedtls/debug.h requires it.
 * Adapted from public-domain msinttypes (Alexander Chemeris) with only
 * the macros mbedTLS 2.28 actually references.
 *
 * 32-bit Win32/WinCE only (no Win64 on WM6).
 */

#ifndef _POSITRON_INTTYPES_H
#define _POSITRON_INTTYPES_H

#if _MSC_VER > 1000
#pragma once
#endif

#if _MSC_VER >= 1800
/* VS2013+ ships a real inttypes.h - this shim should not be used. */
#  error "inttypes.h shim included on MSVC >= 1800; remove .\inttypes.h from project dir"
#endif

#include "stdint.h"

/* MSVC uses "I64" prefix for 64-bit printf specifiers. */
#define __PRI64_PREFIX     "I64"
#define __PRIPTR_PREFIX    ""        /* 32-bit only on WinCE */

/* Signed decimal */
#define PRId8     "d"
#define PRId16    "d"
#define PRId32    "d"
#define PRId64    __PRI64_PREFIX "d"

#define PRIdLEAST8   "d"
#define PRIdLEAST16  "d"
#define PRIdLEAST32  "d"
#define PRIdLEAST64  __PRI64_PREFIX "d"

#define PRIdFAST8    "d"
#define PRIdFAST16   "d"
#define PRIdFAST32   "d"
#define PRIdFAST64   __PRI64_PREFIX "d"

#define PRIdMAX      __PRI64_PREFIX "d"
#define PRIdPTR      __PRIPTR_PREFIX "d"

#define PRIi8     "i"
#define PRIi16    "i"
#define PRIi32    "i"
#define PRIi64    __PRI64_PREFIX "i"

#define PRIiLEAST8   "i"
#define PRIiLEAST16  "i"
#define PRIiLEAST32  "i"
#define PRIiLEAST64  __PRI64_PREFIX "i"

#define PRIiFAST8    "i"
#define PRIiFAST16   "i"
#define PRIiFAST32   "i"
#define PRIiFAST64   __PRI64_PREFIX "i"

#define PRIiMAX      __PRI64_PREFIX "i"
#define PRIiPTR      __PRIPTR_PREFIX "i"

/* Unsigned */
#define PRIo8     "o"
#define PRIo16    "o"
#define PRIo32    "o"
#define PRIo64    __PRI64_PREFIX "o"
#define PRIoMAX   __PRI64_PREFIX "o"
#define PRIoPTR   __PRIPTR_PREFIX "o"

#define PRIu8     "u"
#define PRIu16    "u"
#define PRIu32    "u"
#define PRIu64    __PRI64_PREFIX "u"

#define PRIuLEAST8   "u"
#define PRIuLEAST16  "u"
#define PRIuLEAST32  "u"
#define PRIuLEAST64  __PRI64_PREFIX "u"

#define PRIuFAST8    "u"
#define PRIuFAST16   "u"
#define PRIuFAST32   "u"
#define PRIuFAST64   __PRI64_PREFIX "u"

#define PRIuMAX      __PRI64_PREFIX "u"
#define PRIuPTR      __PRIPTR_PREFIX "u"

#define PRIx8     "x"
#define PRIx16    "x"
#define PRIx32    "x"
#define PRIx64    __PRI64_PREFIX "x"

#define PRIxLEAST8   "x"
#define PRIxLEAST16  "x"
#define PRIxLEAST32  "x"
#define PRIxLEAST64  __PRI64_PREFIX "x"

#define PRIxFAST8    "x"
#define PRIxFAST16   "x"
#define PRIxFAST32   "x"
#define PRIxFAST64   __PRI64_PREFIX "x"

#define PRIxMAX      __PRI64_PREFIX "x"
#define PRIxPTR      __PRIPTR_PREFIX "x"

#define PRIX8     "X"
#define PRIX16    "X"
#define PRIX32    "X"
#define PRIX64    __PRI64_PREFIX "X"
#define PRIXMAX   __PRI64_PREFIX "X"
#define PRIXPTR   __PRIPTR_PREFIX "X"

/* Scanf specifiers - mbedTLS doesn't seem to use these but provide for completeness. */
#define SCNd8     "hhd"
#define SCNd16    "hd"
#define SCNd32    "d"
#define SCNd64    __PRI64_PREFIX "d"
#define SCNdPTR   __PRIPTR_PREFIX "d"

#define SCNu8     "hhu"
#define SCNu16    "hu"
#define SCNu32    "u"
#define SCNu64    __PRI64_PREFIX "u"
#define SCNuPTR   __PRIPTR_PREFIX "u"

#define SCNx8     "hhx"
#define SCNx16    "hx"
#define SCNx32    "x"
#define SCNx64    __PRI64_PREFIX "x"
#define SCNxPTR   __PRIPTR_PREFIX "x"

/* imaxdiv / imaxabs - not provided; mbedTLS does not use them. */

#endif /* _POSITRON_INTTYPES_H */
