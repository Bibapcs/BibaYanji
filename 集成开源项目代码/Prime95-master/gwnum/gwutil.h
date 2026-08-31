/*----------------------------------------------------------------------
| This file contains various utility routines that may be used by gwnum
| routines, prime95, or other consumers of gwnum.
| 
|  Copyright 2004-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWUTIL_H
#define _GWUTIL_H

/* This is a C library.  If used in a C++ program, don't let the C++ */
/* compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* Handy macros to improve readability */

//These are now included in math.h
//#define CONST_LOG2			0.69314718055994530941723212145818
//#define CONST_ONE_OVER_LOG2		1.4426950408889634073599246810019
//#define log2(n)			(log((double)(n)) * CONST_ONE_OVER_LOG2)
//#define log2f(n)			(logf((float)(n)) * (float) CONST_ONE_OVER_LOG2)
#define divide_rounding_up(a,b)		(((a) + (b) - 1) / (b))
#define divide_rounding_down(a,b)	((a) / (b))
#define round_up_to_multiple_of(a,b)	(divide_rounding_up (a, b) * (b))
#define round_down_to_multiple_of(a,b)	(divide_rounding_down (a, b) * (b))
#define fltmod(a,b)			((double)(a) - floor ((double)(a) / (double)(b)) * (double)(b))
#define round_to_cache_line(p)		(void *) (((intptr_t)(p) + 63) & ~63)
#define intmin(a,b)			(((int)(a) < (int)(b)) ? (int)(a) : (int)(b))
#define intmax(a,b)			(((int)(a) > (int)(b)) ? (int)(a) : (int)(b))
#define fltmax(a,b)			(((double)(a) > (double)(b)) ? (double)(a) : (double)(b))
#define round_double_to_int32(d,i)	{double t = ((d) + 6755399441055744.0); i = *((int32_t *)(&t));}

/* MSVC6 has trouble with the pow function using integer arguments. */
/* For example, "(unsigned long) pow (5.0, 7.0)" returns 78124 instead */
/* of the correct 78125.  This macro, works around this trouble. */

#define intpow(b,n)	((long) floor (pow ((double)(b), (double)(n)) + 0.1))

/* Align a pointer to the given boundary (boundary must be a power of 2) */

#define align_ptr(p,n)	(void *) (((intptr_t)(p) + (n)-1) & ~((n)-1))

/* Aligned malloc routines.  MSVC 8 supports these in the C runtime library. */
/* Emulate these routines for other ports. */

void * aligned_offset_malloc (size_t size, size_t alignment, size_t mod);
void * aligned_malloc (size_t size, size_t alignment);
void  aligned_free (void *ptr);

/* Large/huge page allocation routines */

int large_pages_supported ();
void * large_pages_malloc (size_t size);
void large_pages_free (void *ptr);
void * aligned_offset_large_pages_malloc (size_t size, size_t alignment, size_t mod);
void * aligned_large_pages_malloc (size_t size, size_t alignment);
void aligned_large_pages_free (void *ptr);

/* Utility string routines */

void truncated_strcpy (char *buf, unsigned int bufsize, const char *val);
void truncated_strcpy_with_len (char *buf, unsigned int bufsize, const char *val, unsigned int valsize);

/* Utility time routine available in Linux but not Windows */

#ifdef _WIN32
#if defined (__MINGW32__) || defined (__MINGW64__)
#include <sys/time.h>
#endif
int gettimeofday (struct timeval *tp, void *tzp);
#endif

#ifdef __cplusplus
}
#endif

#endif
