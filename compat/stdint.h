/*
 * stdint.h - Minimal C99 fixed-width integer types shim for MSVC 9.0 (VS2008).
 *
 * VS2008 does not ship <stdint.h>; mbedTLS 2.28 requires it.
 * Adapted from public-domain msinttypes (Alexander Chemeris) with only
 * what mbedTLS + Positron actually use.
 *
 * This file is resolved via AdditionalIncludeDirectories=".\mbedtls\include;."
 * - the project dir comes first, so #include <stdint.h> picks this up.
 */

#ifndef _POSITRON_STDINT_H
#define _POSITRON_STDINT_H

#if _MSC_VER > 1000
#pragma once
#endif

#if _MSC_VER >= 1600
/* VS2010+ has a real stdint.h - this shim should not be used. */
#  error "stdint.h shim included on MSVC >= 1600; remove .\stdint.h from project dir"
#endif

/* --------------------------------------------------------------------- */
/* Exact-width integer types                                              */
/* --------------------------------------------------------------------- */

typedef signed   __int8    int8_t;
typedef signed   __int16   int16_t;
typedef signed   __int32   int32_t;
typedef signed   __int64   int64_t;
typedef unsigned __int8    uint8_t;
typedef unsigned __int16   uint16_t;
typedef unsigned __int32   uint32_t;
typedef unsigned __int64   uint64_t;

/* --------------------------------------------------------------------- */
/* Minimum-width                                                          */
/* --------------------------------------------------------------------- */

typedef int8_t   int_least8_t;
typedef int16_t  int_least16_t;
typedef int32_t  int_least32_t;
typedef int64_t  int_least64_t;
typedef uint8_t  uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

/* --------------------------------------------------------------------- */
/* Fastest                                                                */
/* --------------------------------------------------------------------- */

typedef int8_t   int_fast8_t;
typedef int16_t  int_fast16_t;
typedef int32_t  int_fast32_t;
typedef int64_t  int_fast64_t;
typedef uint8_t  uint_fast8_t;
typedef uint16_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

/* --------------------------------------------------------------------- */
/* Pointer-width (WinCE / WM6 is 32-bit only, no _W64 portability hint)   */
/* --------------------------------------------------------------------- */

#ifdef _WIN64
typedef signed   __int64   intptr_t;
typedef unsigned __int64   uintptr_t;
#else
typedef signed   int       intptr_t;
typedef unsigned int       uintptr_t;
#endif

/* --------------------------------------------------------------------- */
/* Greatest-width                                                         */
/* --------------------------------------------------------------------- */

typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

/* --------------------------------------------------------------------- */
/* Limit macros (only when not in C++ or when __STDC_LIMIT_MACROS defined,*/
/* but mbedTLS defines them unconditionally so we always expose them).    */
/* --------------------------------------------------------------------- */

#define INT8_MIN      ((int8_t)(-127i8 - 1))
#define INT16_MIN     ((int16_t)(-32767i16 - 1))
#define INT32_MIN     ((int32_t)(-2147483647i32 - 1))
#define INT64_MIN     ((int64_t)(-9223372036854775807i64 - 1))

#define INT8_MAX      127i8
#define INT16_MAX     32767i16
#define INT32_MAX     2147483647i32
#define INT64_MAX     9223372036854775807i64

#define UINT8_MAX     0xffui8
#define UINT16_MAX    0xffffui16
#define UINT32_MAX    0xffffffffui32
#define UINT64_MAX    0xffffffffffffffffui64

#define INT_LEAST8_MIN    INT8_MIN
#define INT_LEAST16_MIN   INT16_MIN
#define INT_LEAST32_MIN   INT32_MIN
#define INT_LEAST64_MIN   INT64_MIN
#define INT_LEAST8_MAX    INT8_MAX
#define INT_LEAST16_MAX   INT16_MAX
#define INT_LEAST32_MAX   INT32_MAX
#define INT_LEAST64_MAX   INT64_MAX
#define UINT_LEAST8_MAX   UINT8_MAX
#define UINT_LEAST16_MAX  UINT16_MAX
#define UINT_LEAST32_MAX  UINT32_MAX
#define UINT_LEAST64_MAX  UINT64_MAX

#define INT_FAST8_MIN     INT8_MIN
#define INT_FAST16_MIN    INT16_MIN
#define INT_FAST32_MIN    INT32_MIN
#define INT_FAST64_MIN    INT64_MIN
#define INT_FAST8_MAX     INT8_MAX
#define INT_FAST16_MAX    INT16_MAX
#define INT_FAST32_MAX    INT32_MAX
#define INT_FAST64_MAX    INT64_MAX
#define UINT_FAST8_MAX    UINT8_MAX
#define UINT_FAST16_MAX   UINT16_MAX
#define UINT_FAST32_MAX   UINT32_MAX
#define UINT_FAST64_MAX   UINT64_MAX

#ifdef _WIN64
#  define INTPTR_MIN    INT64_MIN
#  define INTPTR_MAX    INT64_MAX
#  define UINTPTR_MAX   UINT64_MAX
#else
#  define INTPTR_MIN    INT32_MIN
#  define INTPTR_MAX    INT32_MAX
#  define UINTPTR_MAX   UINT32_MAX
#endif

#define INTMAX_MIN     INT64_MIN
#define INTMAX_MAX     INT64_MAX
#define UINTMAX_MAX    UINT64_MAX

#define PTRDIFF_MIN    INTPTR_MIN
#define PTRDIFF_MAX    INTPTR_MAX

#ifndef SIZE_MAX
#  ifdef _WIN64
#    define SIZE_MAX  UINT64_MAX
#  else
#    define SIZE_MAX  UINT32_MAX
#  endif
#endif

#define SIG_ATOMIC_MIN  INT32_MIN
#define SIG_ATOMIC_MAX  INT32_MAX

#ifndef WCHAR_MIN
#  define WCHAR_MIN  0
#  define WCHAR_MAX  0xffff
#endif

#define WINT_MIN  0
#define WINT_MAX  0xffff

/* --------------------------------------------------------------------- */
/* Constant macros                                                        */
/* --------------------------------------------------------------------- */

#define INT8_C(val)    val##i8
#define INT16_C(val)   val##i16
#define INT32_C(val)   val##i32
#define INT64_C(val)   val##i64

#define UINT8_C(val)   val##ui8
#define UINT16_C(val)  val##ui16
#define UINT32_C(val)  val##ui32
#define UINT64_C(val)  val##ui64

#define INTMAX_C   INT64_C
#define UINTMAX_C  UINT64_C

#endif /* _POSITRON_STDINT_H */
