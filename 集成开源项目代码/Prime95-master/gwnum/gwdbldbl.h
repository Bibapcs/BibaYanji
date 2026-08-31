/*----------------------------------------------------------------------
| gwdbldbl.h
|
| This file contains the headers for the gwnum helper routines that use
| extended-precision floats.
| 
|  Copyright 2005-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWDBLDBL_H
#define _GWDBLDBL_H

/* This is a C library.  If used in a C++ program, don't let the C++ */
/* compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* Include common definitions */

#include "gwcommon.h"
#ifdef USE_INTRINSICS
#include <immintrin.h>
#endif

/* Extended precision helper routines */

void gwasm_constants (double *);
void gwsincos (unsigned long, unsigned long, double *);

#define gwsincos1by1(a,b,c)	gwsincos1by(a,b,c,1)
#define gwsincos1by2(a,b,c)	gwsincos1by(a,b,c,2)
#define gwsincos1by4(a,b,c)	gwsincos1by(a,b,c,4)
#define gwsincos1by8(a,b,c)	gwsincos1by(a,b,c,8)
void gwsincos1by (unsigned long, unsigned long, double *, int);

#define gwsincos12by1(a,b,c)		gwsincos12345by(a,b,c,1,2)
#define gwsincos12by2(a,b,c)		gwsincos12345by(a,b,c,2,2)
#define gwsincos12by4(a,b,c)		gwsincos12345by(a,b,c,4,2)
#define gwsincos12by8(a,b,c)		gwsincos12345by(a,b,c,8,2)
#define gwsincos123by1(a,b,c)		gwsincos12345by(a,b,c,1,3)
#define gwsincos123by2(a,b,c)		gwsincos12345by(a,b,c,2,3)
#define gwsincos123by4(a,b,c)		gwsincos12345by(a,b,c,4,3)
#define gwsincos123by8(a,b,c)		gwsincos12345by(a,b,c,8,3)
#define gwsincos1234by1(a,b,c)		gwsincos12345by(a,b,c,1,4)
#define gwsincos1234by2(a,b,c)		gwsincos12345by(a,b,c,2,4)
#define gwsincos1234by4(a,b,c)		gwsincos12345by(a,b,c,4,4)
#define gwsincos1234by8(a,b,c)		gwsincos12345by(a,b,c,8,4)
#define gwsincos12345by8(a,b,c)		gwsincos12345by(a,b,c,8,5)
#define gwsincos123456by8(a,b,c)	gwsincos12345by(a,b,c,8,6)
#define gwsincos1234567by2(a,b,c)	gwsincos12345by(a,b,c,2,7)
#define gwsincos1234567by4(a,b,c)	gwsincos12345by(a,b,c,4,7)
#define gwsincos1234567by8(a,b,c)	gwsincos12345by(a,b,c,8,7)
#define gwsincos12345678by8(a,b,c)	gwsincos12345by(a,b,c,8,8)
void gwsincos12345by (unsigned long, unsigned long, double *, int, int);

#define gwsincos13by1(a,b,c)		gwsincos13579by(a,b,c,1,2)
#define gwsincos13by2(a,b,c)		gwsincos13579by(a,b,c,2,2)
#define gwsincos13by4(a,b,c)		gwsincos13579by(a,b,c,4,2)
#define gwsincos13by8(a,b,c)		gwsincos13579by(a,b,c,8,2)
#define gwsincos135by1(a,b,c)		gwsincos13579by(a,b,c,1,3)
#define gwsincos135by8(a,b,c)		gwsincos13579by(a,b,c,8,3)
#define gwsincos1357by1(a,b,c)		gwsincos13579by(a,b,c,1,4)
#define gwsincos1357by8(a,b,c)		gwsincos13579by(a,b,c,8,4)
#define gwsincos13579by8(a,b,c)		gwsincos13579by(a,b,c,8,5)
#define gwsincos13579Bby8(a,b,c)	gwsincos13579by(a,b,c,8,6)
#define gwsincos13579BDFby8(a,b,c)	gwsincos13579by(a,b,c,8,8)
void gwsincos13579by (unsigned long, unsigned long, double *, int, int);

#define gwsincos15by1(a,b,c)		gwsincos159by(a,b,c,1,2)
#define gwsincos15by2(a,b,c)		gwsincos159by(a,b,c,2,2)
#define gwsincos15by4(a,b,c)		gwsincos159by(a,b,c,4,2)
#define gwsincos159Dby2(a,b,c)		gwsincos159by(a,b,c,2,4)
#define gwsincos159Dby4(a,b,c)		gwsincos159by(a,b,c,4,4)
#define gwsincos159Dby8(a,b,c)		gwsincos159by(a,b,c,8,4)
void gwsincos159by (unsigned long, unsigned long, double *, int, int);

#define gwsincos125by2(a,b,c)		gwsincos125by(a,b,c,2)
#define gwsincos125by4(a,b,c)		gwsincos125by(a,b,c,4)
#define gwsincos125by8(a,b,c)		gwsincos125by(a,b,c,8)
void gwsincos125by (unsigned long, unsigned long, double *, int);

#define gwsincos1by1_raw(a,b,c)		gwsincos1234by_raw(a,b,c,1,1)
#define gwsincos1by2_raw(a,b,c)		gwsincos1234by_raw(a,b,c,2,1)
#define gwsincos1by4_raw(a,b,c)		gwsincos1234by_raw(a,b,c,4,1)
#define gwsincos1by8_raw(a,b,c)		gwsincos1234by_raw(a,b,c,8,1)
#define gwsincos12by1_raw(a,b,c)	gwsincos1234by_raw(a,b,c,1,2)
#define gwsincos12by2_raw(a,b,c)	gwsincos1234by_raw(a,b,c,2,2)
#define gwsincos12by4_raw(a,b,c)	gwsincos1234by_raw(a,b,c,4,2)
#define gwsincos1234by2_raw(a,b,c)	gwsincos1234by_raw(a,b,c,2,4)
#define gwsincos1234by4_raw(a,b,c)	gwsincos1234by_raw(a,b,c,4,4)
#define gwsincos1234by8_raw(a,b,c)	gwsincos1234by_raw(a,b,c,8,4)
void gwsincos1234by_raw (unsigned long, unsigned long, double *, int, int);

#define gwsincos1plus0123by1(a,b,c,d)		gwsincos1plusby(a,b,c,d,1,4)
#define gwsincos1plus0123by2(a,b,c,d)		gwsincos1plusby(a,b,c,d,2,4)
#define gwsincos1plus0123by4(a,b,c,d)		gwsincos1plusby(a,b,c,d,4,4)
#define gwsincos1plus0123by8(a,b,c,d)		gwsincos1plusby(a,b,c,d,8,4)
#define gwsincos1plus01234by1(a,b,c,d)		gwsincos1plusby(a,b,c,d,1,5)
#define gwsincos1plus01234by2(a,b,c,d)		gwsincos1plusby(a,b,c,d,2,5)
#define gwsincos1plus01234by4(a,b,c,d)		gwsincos1plusby(a,b,c,d,4,5)
#define gwsincos1plus01234by8(a,b,c,d)		gwsincos1plusby(a,b,c,d,8,5)
#define gwsincos1plus01234567by2(a,b,c,d)	gwsincos1plusby(a,b,c,d,2,8)
#define gwsincos1plus01234567by4(a,b,c,d)	gwsincos1plusby(a,b,c,d,4,8)
#define gwsincos1plus01234567by8(a,b,c,d)	gwsincos1plusby(a,b,c,d,8,8)
#define gwsincos1plus0123456789ABby8(a,b,c,d)	gwsincos1plusby(a,b,c,d,8,12)
void gwsincos1plusby (unsigned long, unsigned long, unsigned long, double *, int, int);

#define gwcos1plus01234567by8(a,b,c,d)		gwcos1plusby(a,b,c,d,8,8)
#define gwcos1plus0123456789ABby8(a,b,c,d)	gwcos1plusby(a,b,c,d,8,12)
void gwcos1plusby (unsigned long, unsigned long, unsigned long, double *, int, int);

#define gwsincos012by2_weighted(a,b,c,d,e,f)	gwsincos012by_weighted(a,b,c,d,e,f,2)
#define gwsincos012by4_weighted(a,b,c,d,e,f)	gwsincos012by_weighted(a,b,c,d,e,f,4)
void gwsincos012by_weighted (void *, unsigned long, unsigned long, unsigned long, unsigned long, double *, int);

#define gwsincos15by2_weighted(a,b,c,d,e,f)	gwsincos15by_weighted(a,b,c,d,e,f,2)
#define gwsincos15by4_weighted(a,b,c,d,e,f)	gwsincos15by_weighted(a,b,c,d,e,f,4)
void gwsincos15by_weighted (void *, unsigned long, unsigned long, unsigned long, unsigned long, double *, int);

#define gwsincos1plus01234567by8_colweighted(a,b,c,d,e)	gwsincos1plusby_colweighted(a,b,c,d,e,8,8)
void gwsincos1plusby_colweighted (unsigned long, unsigned long, unsigned long, void *, double *, int, int);

#define gwsincos1234by8_sqrthalf(a,b,c)		gwsincos1234by_sqrthalf(a,b,c,8)
void gwsincos1234by_sqrthalf (unsigned long, unsigned long, double *, int);

#define gwsincos123by1_special7(a,b,c)		gwsincos123by_special7(a,b,c,1)
#define gwsincos123by8_special7(a,b,c)		gwsincos123by_special7(a,b,c,8)
void gwsincos123by_special7 (unsigned long, unsigned long, double *, int);

#define gwsincos135by1_special7(a,b,c)		gwsincos135by_special7(a,b,c,1)
#define gwsincos135by8_special7(a,b,c)		gwsincos135by_special7(a,b,c,8)
void gwsincos135by_special7 (unsigned long, unsigned long, double *, int);

void *gwdbldbl_data_alloc (void);
void gwfft_weight_setup (void *, int, double, unsigned long, unsigned long, signed long, unsigned long);
double gwfft_weight (void *, unsigned long);
double gwfft_weight_sloppy (void *, unsigned long);
double gwfft_weight_over_weight (void *, unsigned long, unsigned long);
double gwfft_weight_inverse (void *, unsigned long);
double gwfft_weight_inverse_squared (void *, unsigned long);
double gwfft_weight_inverse_sloppy (void *, unsigned long);
double gwfft_weight_inverse_over_fftlen (void *, unsigned long);
void gwfft_weights3 (void *, unsigned long, double *, double *, double *);
double gwfft_weight_exponent (void *, unsigned long);
double gwfft_weight_no_c (void *, unsigned long);
unsigned long gwfft_base (void *, unsigned long);
void gwfft_weights_fudged (void	*, unsigned long, unsigned long, double *, double *, double *, double *);
void gwfft_weights_times_sine (void *, unsigned long, unsigned long, unsigned long, double *, double *);
double gwfft_partial_weight (void *, unsigned long, unsigned long);
double gwfft_partial_weight_sloppy (void *, unsigned long, unsigned long);
double gwfft_partial_weight_inverse (void *, unsigned long, unsigned long);
double gwfft_partial_weight_inverse_sloppy (void *, unsigned long, unsigned long);
void gwfft_colweights (void *, void *, int);

/* Structure for internal use only!  Part of the "global" data that gwfft_weight_setup initalizes and is passed to several gwdbldbl routines. */
/* This partial definition must match the full definition within gwdbldbl.cpp. */
/* Plus a macro used to type the cast untyped data pointer input argument of inlined routines. */

// Defines to access the cached data

#define base128			cached_data				/* 128-bit value representing j * n / FFTLEN */
#define dwpncol_counter		((uint32_t *) &cached_data[2])[0]	/* Counter used in reducing internal dwpn_col calls */
#define dwpncol_value		((uint32_t *) &cached_data[2])[1]	/* Last returned internal dwpn_col value */
#define partial_transition	cached_data[3]				/* Fractional FFT base above which col_bpower >= j_bpower */
#define partial_weight		((double *) &cached_data[4])		/* Two saved weights when using partial weights */
#define partial_inv_weight	((double *) &cached_data[6])		/* Two saved inverse weights when using partial weights */
#define last_weight_sloppy	(* (double *) &cached_data[8])		/* Last non-partial sloppy weight */
#define last_weight_j		((uint32_t *) &cached_data[9])[0]	/* Last non-partial sloppy weight index */
#define last_inv_weight_j	((uint32_t *) &cached_data[9])[1]	/* Last non-partial sloppy inv weight index */
#define last_inv_weight_sloppy	(* (double *) &cached_data[10])		/* Last non-partial sloppy inv weight */
#define inv_weight_sloppy_muls	((uint32_t *) &cached_data[11])[0]	/* Count of multiplies used in creating last_inv_weight_sloppy */

// Extract middle 64 bits of base 128 (the fractional part).  Three methods provided below.  On my Windows laptop, the unaligned load was the slowest.
//#define base128middle		(* (uint64_t *) ((char *) base128 + sizeof (uint32_t)))
//#define base128middle		((base128[1] << 32) + (base128[0] >> 32))
#define base128middle		((base128[1] << 32) + ((uint32_t *)base128)[1])

#ifdef DBLDBL_INLINES

struct gwdbldbl_constants_partial {
	uint32_t frac_bpw3;		/* Third 32 bits of fractional b-per-FFT-word */
	uint32_t frac_bpw2;		/* Second 32 bits of fractional b-per-FFT-word */
	uint32_t frac_bpw1;		/* First 32 bits of fractional b-per-FFT-word */
	uint32_t int_bpw;		/* Integer part of b-per-FFT-word */
};

// Faster routines for sequential access with an iterator

// Init FFT base counter for element zero
void inline gwcached_init_zero (uint64_t *cached_data) {
	base128[0] = 0xFFFFFFFFFFFFFFFFULL;	// Faster is_big_word & weight calculations if counter starts at just under 1.0 rather than the more logical 0.0
	base128[1] = 0xFFFFFFFFULL;
	partial_weight[0] = 0.0;		// Flag that cached partial weights are not set
	partial_inv_weight[0] = 0.0;
	last_weight_j = 0xFFFFFFF0;		// Init weight_sloppy cache
	last_inv_weight_j = 0xFFFFFFF0;		// Init inv_weight_sloppy cache
}

// Init FFT base counter to arbitary element
void inline gwcached_init (void *dd_data_arg, uint64_t *cached_data, unsigned long n) {
	struct gwdbldbl_constants_partial *dd_data = (struct gwdbldbl_constants_partial *) dd_data_arg;

	base128[0] = 0xFFFFFFFFFFFFFFFFULL;	// Faster is_big_word & weight calculations if counter starts at just under 1.0 rather than the more logical 0.0
	base128[1] = 0xFFFFFFFFULL;

	uint64_t tmp = (uint64_t) n * (uint64_t) dd_data->frac_bpw3;
	base128[0] += tmp;			// Add all 64-bits of tmp to low base word
	base128[1] += (base128[0] < tmp);	// Propagate carry

	tmp = (uint64_t) n * (uint64_t) dd_data->frac_bpw2;	// This product will need splitting and shifting
	base128[1] += tmp >> 32;		// Add upper 32-bits of tmp to lower 32-bits of high base word
	base128[0] += tmp << 32;		// Add lower 32-bits of tmp to upper 32-bits of low base word
	base128[1] += (base128[0] < tmp);	// Propagate carry

	tmp = (uint64_t) n * (* ((uint64_t *) &dd_data->frac_bpw1));
	base128[1] += tmp;			// Add all 64-bits of tmp to high base word

	last_weight_j = 0xFFFFFFF0;		// Init weight_sloppy cache
	last_inv_weight_j = 0xFFFFFFF0;		// Init inv_weight_sloppy cache
}

// Update cached data for next FFT element
void inline gwcached_next (void *dd_data_arg, uint64_t *cached_data, unsigned long n) {
	struct gwdbldbl_constants_partial *dd_data = (struct gwdbldbl_constants_partial *) dd_data_arg;

	// Maintain cached data for calculating FFT base (index * n / FFTLEN)
#ifdef USE_INTRINSICS
	unsigned char carry = _addcarry_u64 (0, base128[0], * ((uint64_t *) &dd_data->frac_bpw3), base128[0]);
	_addcarry_u64 (carry, base128[1], * ((uint64_t *) &dd_data->frac_bpw1), base128[1]);
#else
	uint64_t frac_low = * ((uint64_t *) &dd_data->frac_bpw3);
	base128[0] = base128[0] + frac_low;
	uint64_t carry = (base128[0] < frac_low);
	base128[1] = base128[1] + * ((uint64_t *) &dd_data->frac_bpw1) + carry;
#endif

}

// Is this a big word?  Yes if adding fractional bpw to current base will cause a carry into integer part of base.
int inline gwcached_is_big_word (void *dd_data_arg, uint64_t *cached_data) {
	struct gwdbldbl_constants_partial *dd_data = (struct gwdbldbl_constants_partial *) dd_data_arg;
	uint64_t next_base128[2];
#ifdef USE_INTRINSICS
	unsigned char carry = _addcarry_u64 (0, base128[0], * ((uint64_t *) &dd_data->frac_bpw3), next_base128[0]);
	_addcarry_u64 (carry, (uint64_t) ((uint32_t) base128[1]), (uint64_t) dd_data->frac_bpw1, next_base128[1]);
#else
	uint64_t frac_low = * ((uint64_t *) &dd_data->frac_bpw3);
	next_base128[0] = base128[0] + frac_low;
	uint64_t carry = (next_base128[0] < frac_low);
	next_base128[1] = (uint64_t) ((uint32_t) base128[1]) + (uint64_t) dd_data->frac_bpw1 + carry;
#endif
	return (next_base128[1] >> 32);
}

inline uint32_t get_cached_dwpncol_counter (uint64_t *cached_data) { return (dwpncol_counter); }
inline void inc_cached_dwpncol_counter (uint64_t *cached_data) { dwpncol_counter++; }
inline void set_cached_dwpncol_counter (uint64_t *cached_data, uint32_t val) { dwpncol_counter = val; }
inline uint32_t get_cached_dwpncol_value (uint64_t *cached_data) { return (dwpncol_value); }
inline void inc_cached_dwpncol_value (uint64_t *cached_data) { dwpncol_value++; }
inline void set_cached_dwpncol_value (uint64_t *cached_data, uint32_t val) { dwpncol_value = val; }
inline void clear_cached_partial_weights (uint64_t *cached_data) { partial_weight[0] = partial_inv_weight[0] = 0.0; }

double gwcached_weight_sloppy (void *, uint64_t *, unsigned long, int32_t);
double gwcached_weight_inverse_sloppy (void *, uint64_t *, unsigned long);
double gwcached_partial_weight_sloppy (void *, uint64_t *, unsigned long);
double gwcached_partial_weight_inverse_sloppy (void *, uint64_t *, unsigned long);

#endif

#ifdef __cplusplus
}
#endif

#endif
