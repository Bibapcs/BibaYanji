/*----------------------------------------------------------------------
| common.h
|
| This file contains handy #defines that I use in all my projects
| 
|  Copyright 2005-2023 Mersenne Research, Inc.
|  All Rights Reserved.
+---------------------------------------------------------------------*/

#ifndef _COMMON_H
#define _COMMON_H

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gwnum.h"		// GWnum FFT library
#ifndef NO_GMP
#include "gmp.h"		// GMP library
#endif
#ifndef NO_HWLOC
#include "hwloc.h"		// hwloc library
#endif
#include "cpuid.h"
#include "gwini.h"
#include "gwutil.h"
#ifdef WIN32
#include <io.h>
#endif

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#define TRUE	1
#define FALSE	0

/* Define the world/group/owner read/write attributes for creating files */
/* I've always used 0666 in Unix (everyone gets R/W access), but MSVC 8 */
/* now refuses to work with that setting -- insisting on 0600 instead. */

#ifdef _WIN32
#define	CREATE_FILE_ACCESS	0600
#else
#define	CREATE_FILE_ACCESS	0666
#endif

/* Define the ASSERT macro I use while debugging */

#ifdef GDEBUG
#define ASSERTG		assert
#else
#define ASSERTG(a)
#endif

/* Bit manipulation macros */

#define bitset(a,i)	{ ((char *)a)[(i) >> 3] |= (1 << ((i) & 7)); }
#define bitclr(a,i)	{ ((char *)a)[(i) >> 3] &= ~(1 << ((i) & 7)); }
#define bittst(a,i)	(((char *)a)[(i) >> 3] & (1 << ((i) & 7)))

/* Handy macros to improve readability (some copied from gwutil.h) */

//#define _log2(n)			(log ((double)(n)) / log ((double)2.0))
#define _log10(n)			(log ((double)(n)) / log ((double)10.0))
#define divide_rounding_up(a,b)		(((a) + (b) - 1) / (b))
#define divide_rounding_down(a,b)	((a) / (b))
#define divide_rounding(a,b)		(((a) + (b) / 2) / (b))
#define round_up_to_multiple_of(a,b)	(divide_rounding_up (a, b) * (b))
#define round_down_to_multiple_of(a,b)	(divide_rounding_down (a, b) * (b))
#define is_multiple_of(a,b)		((a) % (b) == 0)
#define one_based_modulo(a,b)		((((a) - 1) % (b)) + 1)					// Return value between 1 and b
#define _intmin(a,b)			(((int)(a) < (int)(b)) ? (int)(a) : (int)(b))
#define _intmax(a,b)			(((int)(a) > (int)(b)) ? (int)(a) : (int)(b))
#define primes_less_than(x)		((double)(x) / (log ((double)(x)) - 1.0))		// This is only an estimate
#define isMersenne(k,b,n,c)		((k) == 1.0 && (b) == 2 && (c) == -1)
#define isGeneralizedFermat(k,b,n,c)	((k) == 1.0 && isPowerOf2 (n) && (c) == 1)
#define isPowerOf2(n)			(((n) & ((n)-1)) == 0)
#define round_up_to_power_of_2(n)	(1ULL << (int) ceil(log2((double)(n))))

/* Define a "safe" strcpy.  The official C runtime library says that overlapping */
/* buffers produce undefined results.  This safe strcpy allows overlapping */
/* buffers by using memmove instead. */

#define safe_strcpy(d,s)	memmove (d, s, strlen (s) + 1)
#ifdef GDEBUG
#undef strcpy
//#define strcpy(d,s)	assert((d) >= ((s)+strlen(s)+1) || (s) >= (d)+strlen(s)+1), safe_strcpy(d,s)
#define strcpy(d,s)	debug_strcpy(d,s)
__inline char *debug_strcpy(char *d, const char *s) {assert((d) >= ((s)+strlen(s)+1) || (s) >= (d)+strlen(s)+1); return (char *) safe_strcpy(d,s); }
#endif

/* Routines missing from GMP */

#define mpz_set_u64(d,s)	{ uint64_t = s; mpz_import (m, 1, -1, sizeof (uint64_t), 0, 0, &t); }
#define mpz_add_si(d,s,addin)	if (addin >= 0) mpz_add_ui(d,s,(unsigned int)addin); else mpz_sub_ui(d,s,(unsigned int)-addin);
#define mpz_sub_si(d,s,addin)	if (addin >= 0) mpz_sub_ui(d,s,(unsigned int)addin); else mpz_add_ui(d,s,(unsigned int)-addin);
#define mpz_mul_d(d,s,flt)	{ mpz_t t; mpz_init_set_d(t,flt); mpz_mul(d,s,t); mpz_clear(t); }
#define mpz_eq(a,b)		(mpz_cmp(a,b) == 0)
#define mpz_eq_ui(a,b)		(mpz_cmp_ui(a,b) == 0)
// Work around bug in mpz_tstbit accessing bits above 2^32.  Presumably, mpz_setbit and mpz_clrbit has the same problem.
#define mpz_tstbit64(a,b)	(mpz_getlimbn ((a), (mp_size_t) ((b) / GMP_LIMB_BITS)) & (1ULL << ((b) % GMP_LIMB_BITS)))
#define mpz_setbit64(a,b)	mpz_limbs_modify(a,1)[(b) / GMP_LIMB_BITS] |= (1ULL << ((b) % GMP_LIMB_BITS))
#define mpz_clrbit64(a,b)	mpz_limbs_modify(a,1)[(b) / GMP_LIMB_BITS] &= ~(1ULL << ((b) % GMP_LIMB_BITS))

/* Windows/Linux differences */

#ifndef WIN32

/* Handle differences between Windows and Linux runtime libraries */

#define _commit(f)	fsync(f)
#define _open		open
#define _close		close
#define __read		read
#define __write		write
#define _read		read
#define _write		write
#define _lseek		lseek
#define _lseeki64	lseek
#define _fseeki64	fseek
#define _chsize_s	ftruncate
#define _unlink		unlink
#define _creat		creat
#define _chdir		chdir
#define closesocket	close
#define IsCharAlphaNumeric(c) isalnum(c)
#define _stricmp	strcasecmp

/* Borland compiler and linux */

#ifndef _O_APPEND
#define _O_APPEND	O_APPEND
#define _O_RDONLY	O_RDONLY
#define _O_WRONLY	O_WRONLY
#define _O_RDWR		O_RDWR
#define _O_CREAT	O_CREAT
#define _O_TRUNC	O_TRUNC
#define _O_BINARY 	0
#define _O_TEXT		0
#endif

/* Handle differences between Windows and OS/2 runtime libraries */

#ifdef __IBMC__
#define stricmp(x,y)  stricmp(x,y)
#define _commit(f)    /* no commit/fsync on OS/2 */
#define _ftime        _ftime
#endif

#endif

#endif
