/**************************************************************
 *
 *  gwdbldbl.cpp
 *
 *  This file contains all the gwnum initialization routines that require
 *  extended precision floating point.  We want to initialize our sin/cos
 *  and FFT weight arrays with doubles that are as accurate as possible.
 *
 *  This is the only C++ routine in the gwnum library.  Since gwnum is
 *  a C based library, we declare all routines here as extern "C".
 *
 *  Copyright 2005-2023 Mersenne Research, Inc.  All rights reserved.
 *
 **************************************************************/

/* Include files */

#include "gwdbldbl.h"
#include <cstring>
#include <memory>

/* Pick which doubledouble package we will use. */

#define QD
//#define KEITH_BRIGGS

/* Turn on #define that will disable extended precision floating point */
/* registers, as they wreak havoc with double-double library routines. */

#ifndef X86_64
#define x86
#endif

/* Use Hida, Li & Bailey's QD doubledouble C++ package. */

#ifdef QD
#include "dd.cc"
#endif

/* Use Keith Briggs' doubledouble C++ package.  I find the QD package a better choice. */

#ifdef KEITH_BRIGGS
#define DD_INLINE
#include "doubledouble.h"
#include "doubledouble.cc"
#include "math.cc"
#define	dd_real doubledouble
#define	_2pi TwoPi
#define	_log2 Log2
#endif

/* Epsilon value, 2^-250, should have an exact representation as a double */

#define epsilon 5.5271478752604445602472651921923E-76

/* Structure for internal use only!  "Global" data that gwfft_weight_setup initalizes and is passed to several gwdbldbl routines. */
/* Plus a macro used to type the cast untyped data pointer input argument of inlined routines. */

#define NUM_SLOPPY_MULTS	20
struct gwdbldbl_constants {
	uint32_t frac_bpw3;		/* Third 32 bits of fractional b-per-FFT-word */
	uint32_t frac_bpw2;		/* Second 32 bits of fractional b-per-FFT-word */
	uint32_t frac_bpw1;		/* First 32 bits of fractional b-per-FFT-word */
	uint32_t int_bpw;		/* Integer part of b-per-FFT-word */
	dd_real	gw__b;
	dd_real	gw__logb;
	dd_real	gw__num_b_per_word;
	int	gw__c_is_one;
	dd_real gw__logb_abs_c_div_fftlen;
	dd_real gw__fftlen_inverse;
	dd_real gw__over_fftlen;
	double	gwdbl__b;
	double	gwdbl__b_inverse;
	double	gwdbl__logb;
	double	gwdbl__logb_abs_c_div_fftlen;
	double	fast_sloppy_multiplier[NUM_SLOPPY_MULTS];
	double	fast_inv_sloppy_multiplier[NUM_SLOPPY_MULTS];
};
#define dd_data		((struct gwdbldbl_constants *) dd_data_arg)

/* Forward declarations */

double gwfft_weight_over_b (void *dd_data_arg, unsigned long j);

/* Now write all the routines that use the dd_real package. */

/* This routine is used by me to analyze the rounding error due to converting from dd_real to double. */
/* Sometimes I have a choice of how to arrange sin/cos values in the building block macros. */
/* I use this routine to select an arrangement that minimizes roundoff errors. */

double dderr (dd_real x) {
	double	y = double (x);
	double	err = double (fabs (x - y));
	double	absx = double (fabs (x));
	double	pcterr = err / absx * pow (2, 53.0);
	double	rounderr = err * pow (2.0, 53.0 - ceil (log(absx)/log(2.0)));
	return (y);
}

/* Utility routine to compute many of the constants used by the assembly language code */

extern "C"
void gwasm_constants (
	double	*asm_values)
{
	dd_real arg, sine1, cosine1, sine2, cosine2, sine3, cosine3;
#define P951			asm_values[0]
#define P618			asm_values[1]
#define P309			asm_values[2]
#define M262			asm_values[3]
#define P588			asm_values[4]
#define M162			asm_values[5]
#define M809			asm_values[6]
#define M382			asm_values[7]
#define P866			asm_values[8]
#define P433			asm_values[9]
#define P577			asm_values[10]
#define P975			asm_values[11]
#define P434_P975		asm_values[12]
#define P180			asm_values[13]
#define P623			asm_values[14]
#define M358			asm_values[15]
#define P404			asm_values[16]
#define M223			asm_values[17]
#define M901			asm_values[18]
#define M691			asm_values[19]
#define P924			asm_values[20]
#define P383			asm_values[21]
#define P782			asm_values[22]
#define P434			asm_values[23]
#define P975_P434		asm_values[24]
#define P782_P434		asm_values[25]
#define P259			asm_values[26]
#define P966			asm_values[27]
#define P259_P707		asm_values[28]
#define P966_P707		asm_values[29]
#define P924_P383		asm_values[30]
#define P981_P195		asm_values[31]
#define P981			asm_values[32]
#define P195			asm_values[33]
#define P831_P556		asm_values[34]
#define P831			asm_values[35]
#define P556			asm_values[36]
#define P556_P195		asm_values[37]
#define P901_P975		asm_values[38]
#define P623_P975		asm_values[39]
#define P223_P975		asm_values[40]
#define P1_P975			asm_values[41]
#define P782_P975		asm_values[42]

/* Do some initial setup */

	x86_FIX

/* Initialize the five-complex sine/cosine data. */
/* NOTE: When computing cosine / sine, divide by the 64-bit sine not the */
/* extra precision sine since macros will multiply by the 64-bit sine. */

	arg = dd_real::_2pi / 5.0;		// 2*PI * 1 / 5
	sincos (arg, sine1, cosine1);

	arg = arg * 2.0;			// 2*PI * 2 / 5
	sincos (arg, sine2, cosine2);

	P951 = double (sine1);
	P618 = double (sine2 / P951);		// 0.588 / 0.951

	P309 = double (cosine1);
	M262 = double (cosine2 / P309);		// -0.809 / 0.309

	P588 = double (sine2);
	M162 = double (-sine1 / P588);		// -0.951 / 0.588

	M809 = double (cosine2);
	M382 = double (cosine1 / M809);		// 0.309 / -0.809

/* Initialize the six_reals sine-cosine data. */

	arg = dd_real::_2pi / 3.0;		// 2*PI / 3
	sine1 = sin (arg);			// Compute sine (0.866)

	P866 = double (sine1);
	P433 = double (sine1 * 0.5);		// 0.5 * P866
	P577 = double (dd_real (0.5) / sine1);	// 0.5 / 0.866

/* Initialize the seven-complex sine/cosine data. */

	arg = dd_real::_2pi / 7.0;		// 2*PI * 1 / 7
	sincos (arg, sine1, cosine1);		// cosine (0.623), sine (0.782)

	arg = arg * 2.0;			// 2*PI * 2 / 7
	sincos (arg, sine2, cosine2);		// cosine (-.223), sine (0.975)

	arg = arg * 1.5;			// 2*PI * 3 / 7
	sincos (arg, sine3, cosine3);		// cosine (-.901), sine (0.434)

	P782 = double (sine1);
	P975 = double (sine2);
	P434 = double (sine3);
	P623 = double (cosine1);
	M223 = double (cosine2);
	M901 = double (cosine3);

	P434_P975 = double (sine3 / P975);	// 0.434 / 0.975
	P180 = double (sine1 / P975 / P434_P975); // 0.782 / (0.975 * 0.445)
	M358 = double (cosine2 / P623);		// -0.223 / 0.623
	P404 = double (cosine3 / P623 / M358);	// -.901 / (.623 * -.358)
	M691 = double (cosine1 / M901);		// 0.623 / -0.901
	P782_P434 = double (sine1 / P434);	// 0.782 / 0.434
	P975_P434 = double (sine2 / P434);	// 0.975 / 0.434

	P782_P975 = double (sine1 / P975);	// Radix-7 constants for AVX-512
	P901_P975 = double (-cosine3 / P975);
	P623_P975 = double (cosine1 / P975);
	P223_P975 = double (-cosine2 / P975);
	P1_P975 = 1.0 / P975;

/* Initialize the 24-reals sine-cosine data. */

	arg = dd_real::_2pi / 24.0;		// 2*PI * 1 / 24
	sincos (arg, sine1, cosine1);		// cosine (0.966), sine (0.259)

	P259 = double (sine1);
	P966 = double (cosine1);

	P259_P707 = double (sine1 / sqrt (0.5));
	P966_P707 = double (cosine1 / sqrt (0.5));

/* Initialize the roots of -1 used by r4_four_complex_first_fft and zr8_eight_complex_first_fft. */

	arg = dd_real::_2pi / 16.0;		// 2*PI / 16
	sincos (arg, sine1, cosine1);		// cosine (0.924), sine (0.383)
	P924 = double (cosine1);
	P383 = double (sine1);
	P924_P383 = double (cosine1 / P383);

	arg = dd_real::_2pi / 32.0;		// 2*PI / 32
	sincos (arg, sine1, cosine1);		// cosine (0.981), sine (0.195)
	P981 = double (cosine1);
	P195 = double (sine1);
	P981_P195 = double (cosine1 / P195);

	arg = dd_real::_2pi / 32.0;		// 2*PI / 32
	arg = 3.0 * arg;			// 3 * 2*PI / 32
	sincos (arg, sine1, cosine1);		// cosine (0.831), sine (0.556)
	P831 = double (cosine1);
	P556 = double (sine1);
	P831_P556 = double (cosine1 / P556);
	P556_P195 = double (sine1 / P195);

	END_x86_FIX
}

// Utility routines to compute a sin/cos multipliers

extern "C"
void gwsincos (
	unsigned long x,
	unsigned long N,
	double	*results)
{
	gwsincos1by (x, N, results, 1);
}

extern "C"
void gwsincos1by (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr)
{
	gwsincos12345by (x, N, results, incr, 1);
}

extern "C"
void gwsincos12345by (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr,
	int	amt)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine3, cosine3, sine4, cosine4, sine5, cosine5, sine6, cosine6, sine7, cosine7;
	dd_real	sine8, cosine8;

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	results[0] = sine;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine / results[0];
	results += incr + incr;
	if (amt == 1) goto done;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);
	results[0] = sine2;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine2 / results[0];
	results += incr + incr;
	if (amt == 2) goto done;

	sine3 = sine * cosine2 + sine2 * cosine;
	cosine3 = cosine * cosine2 - sine * sine2;
	results[0] = sine3;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine3 / results[0];
	results += incr + incr;
	if (amt == 3) goto done;

	sine4 = sine2 * cosine2 * 2.0;
	cosine4 = sqr (cosine2) - sqr (sine2);
	results[0] = sine4;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine4 / results[0];
	results += incr + incr;
	if (amt == 4) goto done;

	sine5 = sine * cosine4 + sine4 * cosine;
	cosine5 = cosine * cosine4 - sine * sine4;
	results[0] = sine5;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine5 / results[0];
	results += incr + incr;
	if (amt == 5) goto done;

	sine6 = sine2 * cosine4 + sine4 * cosine2;
	cosine6 = cosine2 * cosine4 - sine2 * sine4;
	results[0] = sine6;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine6 / results[0];
	results += incr + incr;
	if (amt == 6) goto done;

	sine7 = sine3 * cosine4 + sine4 * cosine3;
	cosine7 = cosine3 * cosine4 - sine3 * sine4;
	results[0] = sine7;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine7 / results[0];
	results += incr + incr;
	if (amt == 7) goto done;

	sine8 = sine4 * cosine4 + sine4 * cosine4;
	cosine8 = cosine4 * cosine4 - sine4 * sine4;
	results[0] = sine8;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine8 / results[0];
done:	;
	END_x86_FIX
}

extern "C"
void gwsincos13579by (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr,
	int	amt)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine3, cosine3, sine5, cosine5, sine7, cosine7, sine9, cosine9, sine11, cosine11;
	dd_real sine13, cosine13, sine15, cosine15;

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	results[0] = sine;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine / results[0];
	results += incr + incr;
	if (amt == 1) goto done;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);

	sine3 = sine * cosine2 + sine2 * cosine;
	cosine3 = cosine * cosine2 - sine * sine2;

	results[0] = sine3;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine3 / results[0];
	results += incr + incr;
	if (amt == 2) goto done;

	sine5 = sine3 * cosine2 + sine2 * cosine3;
	cosine5 = cosine3 * cosine2 - sine3 * sine2;

	results[0] = sine5;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine5 / results[0];
	results += incr + incr;
	if (amt == 3) goto done;

	sine7 = sine5 * cosine2 + sine2 * cosine5;
	cosine7 = cosine5 * cosine2 - sine5 * sine2;

	results[0] = sine7;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine7 / results[0];
	results += incr + incr;
	if (amt == 4) goto done;

	sine9 = sine7 * cosine2 + sine2 * cosine7;
	cosine9 = cosine7 * cosine2 - sine7 * sine2;

	results[0] = sine9;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine9 / results[0];
	results += incr + incr;
	if (amt == 5) goto done;

	sine11 = sine9 * cosine2 + sine2 * cosine9;
	cosine11 = cosine9 * cosine2 - sine9 * sine2;

	results[0] = sine11;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine11 / results[0];
	results += incr + incr;
	if (amt == 6) goto done;

	sine13 = sine11 * cosine2 + sine2 * cosine11;
	cosine13 = cosine11 * cosine2 - sine11 * sine2;

	results[0] = sine13;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine13 / results[0];
	results += incr + incr;
	if (amt == 7) goto done;

	sine15 = sine13 * cosine2 + sine2 * cosine13;
	cosine15 = cosine13 * cosine2 - sine13 * sine2;

	results[0] = sine15;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine15 / results[0];
done:	;
	END_x86_FIX
}

extern "C"
void gwsincos159by (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr,
	int	amt)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine4, cosine4, sine5, cosine5, sine9, cosine9, sine13, cosine13;

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	results[0] = sine;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine / results[0];
	results += incr + incr;
	if (amt == 1) goto done;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);

	sine4 = sine2 * cosine2 * 2.0;
	cosine4 = sqr (cosine2) - sqr (sine2);

	sine5 = sine * cosine4 + sine4 * cosine;
	cosine5 = cosine * cosine4 - sine * sine4;

	results[0] = sine5;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine5 / results[0];
	results += incr + incr;
	if (amt == 2) goto done;

	sine9 = sine5 * cosine4 + sine4 * cosine5;
	cosine9 = cosine5 * cosine4 - sine5 * sine4;

	results[0] = sine9;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine9 / results[0];
	results += incr + incr;
	if (amt == 3) goto done;

	sine13 = sine9 * cosine4 + sine4 * cosine9;
	cosine13 = cosine9 * cosine4 - sine9 * sine4;

	results[0] = sine13;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine13 / results[0];
done:	;
	END_x86_FIX
}

extern "C"
void gwsincos125by (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine4, cosine4, sine5, cosine5;

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	results[0] = sine;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine / results[0];
	results += incr + incr;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);

	results[0] = sine2;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine2 / results[0];
	results += incr + incr;

	sine4 = sine2 * cosine2 * 2.0;
	cosine4 = sqr (cosine2) - sqr (sine2);

	sine5 = sine * cosine4 + sine4 * cosine;
	cosine5 = cosine * cosine4 - sine * sine4;

	results[0] = sine5;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine5 / results[0];
	END_x86_FIX
}

extern "C"
void gwsincos1234by_raw (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr,
	int	amt)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine3, cosine3, sine4, cosine4;

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	results[0] = sine;
	results[incr] = cosine;
	results += incr + incr;
	if (amt == 1) goto done;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);
	results[0] = sine2;
	results[incr] = cosine2;
	results += incr + incr;
	if (amt == 2) goto done;

	sine3 = sine * cosine2 + sine2 * cosine;
	cosine3 = cosine * cosine2 - sine * sine2;
	results[0] = sine3;
	results[incr] = cosine3;
	results += incr + incr;
	if (amt == 3) goto done;

	sine4 = sine2 * cosine2 * 2.0;
	cosine4 = sqr (cosine2) - sqr (sine2);
	results[0] = sine4;
	results[incr] = cosine4;
done:	;
	END_x86_FIX
}


extern "C"
void gwsincos1plusby (
	unsigned long x,
	unsigned long inc,
	unsigned long N,
	double	*results,
	int	incr,
	int	amt)
{
	dd_real twopi_over_N, sineinc, cosineinc, sine0, cosine0;
	int	num_output;

	x86_FIX
	twopi_over_N = dd_real::_2pi / (double) N;
	sincos (twopi_over_N * (double) inc, sineinc, cosineinc);
	sincos (twopi_over_N * (double) x, sine0, cosine0);

	for (num_output = 0; ; ) {
		dd_real last_sine;
		/* Output the sine and cosine/sine value while protecting against divide by zero */
		results[0] = sine0;
		results[0] += epsilon;
		results[incr] = cosine0 / results[0];
		results += incr + incr;
		num_output++;
		/* Break if we've output the requested amount */
		if (num_output == amt) break;
		/* Compute next cosine and sine value */
		last_sine = sine0;
		sine0 = sine0 * cosineinc + cosine0 * sineinc;
		cosine0 = cosine0 * cosineinc - last_sine * sineinc;
	}
	END_x86_FIX
}

/* Like gwsincos1plusby but only the cos/sin value is output */

extern "C"
void gwcos1plusby (
	unsigned long x,
	unsigned long inc,
	unsigned long N,
	double	*results,
	int	incr,
	int	amt)
{
	dd_real twopi_over_N, sineinc, cosineinc, sine0, cosine0;
	int	num_output;

	x86_FIX
	twopi_over_N = dd_real::_2pi / (double) N;
	sincos (twopi_over_N * (double) inc, sineinc, cosineinc);
	sincos (twopi_over_N * (double) x, sine0, cosine0);

	for (num_output = 0; ; ) {
		dd_real last_sine;
		/* Output the cosine / sine value while protecting against divide by zero */
		results[0] = cosine0 / (sine0 + epsilon);
		results += incr;
		num_output++;
		/* Break if we've output the requested amount */
		if (num_output == amt) break;
		/* Compute next cosine and sine value */
		last_sine = sine0;
		sine0 = sine0 * cosineinc + cosine0 * sineinc;
		cosine0 = cosine0 * cosineinc - last_sine * sineinc;
	}
	END_x86_FIX
}

/* Special version for 8-complex and 16-real macros that multiplies some sine values by SQRTHALF */

#ifdef TRY_SQRT2_TO_REDUCE_ROUNDOFF
extern "C"
void gwsincos1234by_sqrthalf (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine3, cosine3, sine4, cosine4;
	dd_real sqrthalf;

	x86_FIX
	sqrthalf = sqrt (dd_real (0.5));
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	results[0] = sine * sqrthalf;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine * sqrthalf / results[0];
	results += incr + incr;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);
	results[0] = sine2;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine2 / results[0];
	results += incr + incr;

	sine3 = sine * cosine2 + sine2 * cosine;
	cosine3 = cosine * cosine2 - sine * sine2;
	results[0] = sine3 * sqrthalf;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine3 * sqrthalf / results[0];
	results += incr + incr;

	sine4 = sine2 * cosine2 * 2.0;
	cosine4 = sqr (cosine2) - sqr (sine2);
	results[0] = sine4;
	results[0] += epsilon;		/* Protect against divide by zero */
	results[incr] = cosine4 / results[0];
	END_x86_FIX
}
#endif

/* Special version for 14-reals (and 7-complex) macro, multiplies sine values by 0.975 */

extern "C"
void gwsincos123by_special7 (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine3, cosine3, tmp;
	dd_real sine975;

	arg1 = dd_real::_2pi * 2.0 / 7.0;		// 2*PI * 2 / 7
	sine975 = sin (arg1);				// sine (0.975)

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	tmp = sine + epsilon;		/* Protect against divide by zero */
	results[0] = tmp * sine975;
	results[incr] = cosine / tmp;
	results += incr + incr;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);
	tmp = sine2 + epsilon;		/* Protect against divide by zero */
	results[0] = tmp * sine975;
	results[incr] = cosine2 / tmp;
	results += incr + incr;

	sine3 = sine * cosine2 + sine2 * cosine;
	cosine3 = cosine * cosine2 - sine * sine2;
	tmp = sine3 + epsilon;		/* Protect against divide by zero */
	results[0] = tmp * sine975;
	results[incr] = cosine3 / tmp;
	END_x86_FIX
}

/* Special version for 14-reals (and 7-complex) macro, multiplies sine values by 0.975 */

extern "C"
void gwsincos135by_special7 (
	unsigned long x,
	unsigned long N,
	double	*results,
	int	incr)
{
	dd_real arg1, sine, cosine, sine2, cosine2, sine3, cosine3, sine5, cosine5, tmp;
	dd_real sine975;

	arg1 = dd_real::_2pi * 2.0 / 7.0;		// 2*PI * 2 / 7
	sine975 = sin (arg1);				// sine (0.975)

	x86_FIX
	arg1 = dd_real::_2pi * (double) x / (double) N;
	sincos (arg1, sine, cosine);
	tmp = sine + epsilon;		/* Protect against divide by zero */
	results[0] = tmp * sine975;
	results[incr] = cosine / tmp;
	results += incr + incr;

	sine2 = sine * cosine * 2.0;
	cosine2 = sqr (cosine) - sqr (sine);

	sine3 = sine * cosine2 + sine2 * cosine;
	cosine3 = cosine * cosine2 - sine * sine2;
	tmp = sine3 + epsilon;		/* Protect against divide by zero */
	results[0] = tmp * sine975;
	results[incr] = cosine3 / tmp;
	results += incr + incr;

	sine5 = sine3 * cosine2 + sine2 * cosine3;
	cosine5 = cosine3 * cosine2 - sine3 * sine2;
	tmp = sine5 + epsilon;		/* Protect against divide by zero */
	results[0] = tmp * sine975;
	results[incr] = cosine5 / tmp;
	END_x86_FIX
}


//
// Utility routines to compute fft weights
//
// The FFT weight for the j-th FFT word doing a b^n+c weighted transform is
//	b ^ (ceil (j*n/FFTLEN) - j*n/FFTLEN)   *   abs(c) ^ j/FFTLEN
//
// NOTE:  Simply using the dd_real ceil function is not a good idea.  This is
// because when we calculate (j * (n/FFTLEN)) and the result is an exact integer,
// then if FFTLEN is not a power of 2, the dd_real result is exact integer +/- epsilon.
// If it is +epsilon then the ceil function returns the wrong value for us.
// There are numerous examples, one being testing M26000208 using 1440K FFT.
// We also cannot simply convert to a real value before applying the ceil function
// because sometimes we need more than 53 bits of precision as happens when
// testing 10223*2^29588045-1.
// Our solution is to subtract a smidge before applying the ceil function.
// Since j*n/FFTLEN is always less than FFTLEN and we don't envision handling
// FFTs above 64M, the integer part the input to ceil will be 26 bits or less.
// We think dd_real routines support 106 bits of precision, leaving a fractional
// part accurate to at least 2^-80 or about 8e-25.  Thus, if we define "smidge" as
// 1e-22 then smidge will swamp +/- epsilon and force ceil to give us the correct
// answer while still providing a LOT more than 53 bits of precision for handling
// j*n/FFTLEN values that are very, very close to an integer.
// An alternative solution would be to not precompute n/FFTLEN in our calculations
// and assume that the dd_real routines will return an exact integer when appropriate
// for ((j*n) / FFTLEN).

extern "C"
void *gwdbldbl_data_alloc (void)
{
	return (malloc (sizeof (struct gwdbldbl_constants)));
}

extern "C"
void gwfft_weight_setup (
	void	*dd_data_arg,
	int	zero_pad,
	double	k,
	unsigned long b,
	unsigned long n,
	signed long c,
	unsigned long fftlen)
{
	dd_real	tmp;

	x86_FIX
	dd_data->gw__b = dd_real ((double) b);
	dd_data->gwdbl__b = (double) b;
	dd_data->gw__logb = log (dd_real ((double) b));
	dd_data->gwdbl__logb = double (dd_data->gw__logb);
	dd_data->gw__fftlen_inverse = dd_real (1.0) / dd_real ((double) fftlen);
	if (zero_pad) {
		dd_data->gw__num_b_per_word = dd_real ((double) (n + n)) / dd_real ((double) fftlen);
		dd_data->gw__c_is_one = TRUE;
		dd_data->gw__over_fftlen = dd_real (2.0) * dd_data->gw__fftlen_inverse;
	} else {
		// For rational FFTs, we cannot multiply by FFT inverse or that could result in a non-integral 128-bit bpw value 
		dd_data->gw__num_b_per_word = (dd_real ((double) n) + log (dd_real (k)) / dd_data->gw__logb) / dd_real ((double) fftlen);
		dd_data->gw__c_is_one = (abs ((int) c) == 1);
		dd_data->gw__over_fftlen = dd_real (k * 2.0) / dd_real ((double) fftlen);
		dd_data->gw__logb_abs_c_div_fftlen = log (dd_real (abs ((int) c))) / dd_data->gw__logb / dd_real ((double) fftlen);
		dd_data->gwdbl__logb_abs_c_div_fftlen = double (dd_data->gw__logb_abs_c_div_fftlen);
	}
	dd_data->gwdbl__b_inverse = 1.0 / (double) b;

	tmp = dd_data->gw__num_b_per_word;
	dd_data->int_bpw = (uint32_t) double (floor (tmp));		/* Integer part of b-per-FFT-word */
	tmp = (tmp - (double) dd_data->int_bpw) * 4294967296.0;
	dd_data->frac_bpw1 = (uint32_t) double (floor (tmp));		/* First 32 bits of fractional b-per-FFT-word */
	tmp = (tmp - (double) dd_data->frac_bpw1) * 4294967296.0;
	dd_data->frac_bpw2 = (uint32_t) double (floor (tmp));		/* Second 32 bits of fractional b-per-FFT-word */
	tmp = (tmp - (double) dd_data->frac_bpw2) * 4294967296.0;
	dd_data->frac_bpw3 = (uint32_t) double (floor (tmp));		/* Third 32 bits of fractional b-per-FFT-word */

	for (int i = 0; i < NUM_SLOPPY_MULTS; i++) {
		dd_data->fast_sloppy_multiplier[i] = gwfft_weight_over_b (dd_data, i+1);
		dd_data->fast_inv_sloppy_multiplier[i] = gwfft_weight_inverse (dd_data, i+1);
	}
	END_x86_FIX
}

// The power for the weight of the j-th FFT word doing a b^n+c weighted transform is ceil (j*n/FFTLEN) - (j*n/FFTLEN)
// There is also a correction if c is not one.

dd_real inline map_to_weight_power (
	void	*dd_data_arg,
	unsigned long j)
{
	uint32_t tmp1;
	uint64_t tmp2, tmp3;
	dd_real	result;

	// Use fractional value accurate to 96-bits.  Assuming j is at most 30 bits, we'll end up with a fractional part accurate to 66 bits.
	tmp1 = (uint32_t) j * (uint32_t) dd_data->frac_bpw1;		// 0.A
	tmp2 = (uint64_t) j * (uint64_t) dd_data->frac_bpw2;		// 0.BC
	tmp3 = (uint64_t) j * (uint64_t) dd_data->frac_bpw3;		// 0.0DE
	tmp2 += (tmp3 >> 32);						// Cannot overflow
	tmp2 += (((uint64_t) tmp1) << 32);				// Overflow into integer portion is irrelevant
	tmp2 = 0 - tmp2;						// Simulate a ceil(tmp2) - tmp2 operation

	result = (double) (tmp2 & ~(uint64_t) 0xFFFFFFFF);
	result += (double) (tmp2 & (uint64_t) 0xFFFFFFFF);
	result *= 5.4210108624275221700372640043497e-20;		// Times 2^-64

	if (! dd_data->gw__c_is_one) result += dd_data->gw__logb_abs_c_div_fftlen * (double) j;

	return (result);
}

double inline map_to_weight_power_sloppy (
	void	*dd_data_arg,
	unsigned long j)
{
	uint32_t tmp1;
	uint64_t tmp2, tmp3;
	double	result;

	tmp1 = (uint32_t) j * (uint32_t) dd_data->frac_bpw1;		// 0.A
	tmp2 = (uint64_t) j * (uint64_t) dd_data->frac_bpw2;		// 0.BC
	tmp3 = (uint64_t) j * (uint64_t) dd_data->frac_bpw3;		// 0.0DE
	tmp2 += (tmp3 >> 32);						// Cannot overflow
	tmp2 += (((uint64_t) tmp1) << 32);				// Overflow into integer portion is irrelevant
	tmp2 = 0 - tmp2;						// Simulate a ceil(tmp2) - tmp2 operation

	result = (double) (int64_t) (tmp2 >> 1);			// I think Intel can convert int64 to double easier than uint64
	result *= 1.0842021724855044340074528008699e-19;		// Times 2^-63

	if (! dd_data->gw__c_is_one) result += dd_data->gwdbl__logb_abs_c_div_fftlen * (double) j;

	return (result);
}

// Like map_to_weight_power but the weight for c is not factored in

inline dd_real map_to_weight_power_no_c (
	void	*dd_data_arg,
	unsigned long j)
{
	uint32_t tmp1;
	uint64_t tmp2, tmp3;
	dd_real	result;

	// Use fractional value accurate to 96-bits.  Assuming j is at most 30 bits, we'll end up with a fractional part accurate to 66 bits.
	tmp1 = (uint32_t) j * (uint32_t) dd_data->frac_bpw1;		// 0.A
	tmp2 = (uint64_t) j * (uint64_t) dd_data->frac_bpw2;		// 0.BC
	tmp3 = (uint64_t) j * (uint64_t) dd_data->frac_bpw3;		// 0.0DE
	tmp2 += (tmp3 >> 32);						// Cannot overflow
	tmp2 += (((uint64_t) tmp1) << 32);				// Overflow into integer portion is irrelevant
	tmp2 = 0 - tmp2;						// Simulate a ceil(tmp2) - tmp2 operation

	result = (double) (tmp2 & ~(uint64_t) 0xFFFFFFFF);
	result += (double) (tmp2 & (uint64_t) 0xFFFFFFFF);
	result *= 5.4210108624275221700372640043497e-20;		// Times 2^-64

	return (result);
}

inline double map_to_weight_power_no_c_sloppy (
	void	*dd_data_arg,
	unsigned long j)
{
	uint32_t tmp1;
	uint64_t tmp2, tmp3;
	double	result;

	// Use fractional value accurate to 96-bits.  Assuming j is at most 30 bits, we'll end up with a fractional part accurate to 66 bits.
	tmp1 = (uint32_t) j * (uint32_t) dd_data->frac_bpw1;		// 0.A
	tmp2 = (uint64_t) j * (uint64_t) dd_data->frac_bpw2;		// 0.BC
	tmp3 = (uint64_t) j * (uint64_t) dd_data->frac_bpw3;		// 0.0DE
	tmp2 += (tmp3 >> 32);						// Cannot overflow
	tmp2 += (((uint64_t) tmp1) << 32);				// Overflow into integer portion is irrelevant
	tmp2 = 0 - tmp2;						// Simulate a ceil(tmp2) - tmp2 operation

	result = (double) (int64_t) (tmp2 >> 1);			// I think Intel can convert int64 to double easier than uint64
	result *= 1.0842021724855044340074528008699e-19;		// Times 2^-63

	return (result);
}

// The FFT base for the j-th FFT word doing a b^n+c weighted transform is ceil (j*n/FFTLEN)

inline uint32_t map_to_fft_base (
	void	*dd_data_arg,
	unsigned long j)
{
	uint32_t result;
	uint64_t tmp1, tmp2, tmp3;

	result = j * dd_data->int_bpw;					// X.
	tmp1 = (uint64_t) j * (uint64_t) dd_data->frac_bpw1;		// A.B
	tmp2 = (uint64_t) j * (uint64_t) dd_data->frac_bpw2;		// 0.CD
	tmp3 = (uint64_t) j * (uint64_t) dd_data->frac_bpw3;		// 0.0EF
	tmp2 += (tmp3 >> 32);						// Cannot overflow
	tmp1 += (tmp2 >> 32);						// Cannot overflow
	result += (uint32_t) (tmp1 >> 32);				// Cannot overflow

	// Simulate a ceil() operation
	return (result + (((uint32_t) tmp1 | (uint32_t) tmp2) ? 1 : 0));
}

// Return the FFT weight for the j-th word:  b ^ (ceil (j*n/FFTLEN) - (j*n/FFTLEN))

extern "C"
double gwfft_weight (
	void	*dd_data_arg,
	unsigned long j)
{
	dd_real	bpower, result;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	result = exp (dd_data->gw__logb * bpower);
	END_x86_FIX
	return (double (result));
}

double gwfft_weight_over_b (
	void	*dd_data_arg,
	unsigned long j)
{
	dd_real	bpower, result;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	result = exp (dd_data->gw__logb * (bpower - 1.0));
	END_x86_FIX
	return (double (result));
}

// Like gwfft_weight, but slightly faster and does not guarantee quite as much accuracy.
// We cannot be very sloppy as these weights are used to set FFT data values.  If we are too sloppy we quickly get roundoff errors > 0.5.

extern "C"
double gwfft_weight_sloppy (
	void	*dd_data_arg,
	unsigned long j)
{
	double	bpower = map_to_weight_power_sloppy (dd_data_arg, j);
	return (exp (dd_data->gwdbl__logb * bpower));
}

// Return the FFT weight for the j-th word divided by the weight for the k-th word.
// NOTE: The k-th word weight is first converted to a double because the zr8 sixteen reals assembly code will multiply by that double.

extern "C"
double gwfft_weight_over_weight (
	void	*dd_data_arg,
	unsigned long j,
	unsigned long k)
{
	dd_real	bpower, div_bpower, result;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	div_bpower = map_to_weight_power (dd_data_arg, k);
	result = exp (dd_data->gw__logb * bpower) / (double) exp (dd_data->gw__logb * div_bpower);
	END_x86_FIX
	return (double (result));
}

// Compute the inverse of the fft weight

extern "C"
double gwfft_weight_inverse (
	void	*dd_data_arg,
	unsigned long j)
{
	dd_real bpower, result;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	result = exp (dd_data->gw__logb * -bpower);
	END_x86_FIX
	return (double (result));
}

// Compute the inverse of the fft weight squared over fftlen (special code for one pass AVX-512 negacyclic wrapper)

extern "C"
double gwfft_weight_inverse_squared (
	void	*dd_data_arg,
	unsigned long j)
{
	dd_real bpower, result;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	result = exp (dd_data->gw__logb * -2.0 * bpower);
	result = result * dd_data->gw__over_fftlen;
	END_x86_FIX
	return (double (result));
}

// Like gwfft_weight_inverse, but faster and does not guarantee quite as much accuracy.
// Use gwcached version (used by gwiter) for much better performance!

extern "C"
double gwfft_weight_inverse_sloppy (
	void	*dd_data_arg,
	unsigned long j)
{
	double bpower = map_to_weight_power_sloppy (dd_data_arg, j);
	return (exp (dd_data->gwdbl__logb * -bpower));
}


// This computes the inverse FFT weight multiplied by the appropriate constant
// to produce an integer during an FFT multiply's normalize stage.  This
// constant is 2/FFTLEN for a zero-padded FFT and k*2/FFTLEN for a
// non-zero-padded FFT.

extern "C"
double gwfft_weight_inverse_over_fftlen (
	void	*dd_data_arg,
	unsigned long j)
{
	dd_real bpower, result;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	result = exp (dd_data->gw__logb * -bpower) * dd_data->gw__over_fftlen;
	END_x86_FIX
	return (double (result));
}

// This computes the three FFT weights in one call.  It is faster than calling the above individually.

extern "C"
void gwfft_weights3 (
	void	*dd_data_arg,
	unsigned long j,
	double	*fft_weight,
	double	*fft_weight_inverse,
	double	*fft_weight_inverse_over_fftlen)
{
	dd_real bpower, weight;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	weight = exp (dd_data->gw__logb * bpower);
	if (fft_weight != NULL) *fft_weight = double (weight);
	if (fft_weight_inverse != NULL) *fft_weight_inverse = double (1.0 / weight);
	if (fft_weight_inverse_over_fftlen != NULL) *fft_weight_inverse_over_fftlen = double (dd_data->gw__over_fftlen / weight);
	END_x86_FIX
}

// Returns logb(fft_weight).  This is used in determining the FFT weight
// fudge factor in two-pass FFTs.  This is much faster than computing the
// fft_weight because it eliminates a call to the double-double exp routine.

extern "C"
double gwfft_weight_exponent (
	void	*dd_data_arg,
	unsigned long j)
{
	if (j == 0) return (0);
	return (map_to_weight_power_no_c_sloppy (dd_data_arg, j));
}

// Funky version that does not factor in the weight for c != 1.  Required for building the AVX-512 XOR masks in the compressed fudge table,

extern "C"
double gwfft_weight_no_c (
	void	*dd_data_arg,
	unsigned long j)
{
	dd_real bpower, result;

	x86_FIX
	bpower = map_to_weight_power_no_c (dd_data_arg, j);
	result = exp (dd_data->gw__logb * bpower);
	END_x86_FIX
	return (double (result));
}

//
// Utility routine to compute fft base for j-th fft word
//
// The FFT base for the j-th FFT word doing a b^n+c weighted transform is
//	ceil (j*n/FFTLEN)
// This routine returns ceil (j*n/FFTLEN) taking great care to return a
// value accurate to 53 bits.  This is important when j*n is really close to
// a multiple of FFTLEN (admittedly quite rare).  It would be really bad if
// rounding differences caused this routine to compute ceil (j*n/FFTLEN)
// differently than the weighting functions.
//

extern "C"
unsigned long gwfft_base (
	void	*dd_data_arg,
	unsigned long j)
{

// Handle easy case

	if (j == 0) return (0);

// Call the routine that calculates this using integer instructions

	return (map_to_fft_base (dd_data_arg, j));
}

extern "C"
void gwsincos012by_weighted (
	void	*dd_data_arg,
	unsigned long x,
	unsigned long upper_x_incr,
	unsigned long N,
	unsigned long col,
	double	*results,
	int	incr)
{
	dd_real twopi_over_N, sine, cosine, sine2, cosine2;
	dd_real bpower, weight, inv_weight;
	int	i;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, col);
	weight = exp (dd_data->gw__logb * bpower);
	inv_weight = dd_data->gw__over_fftlen / weight;

	// This hack causes SSE2 code to get duplicated weight and inv_weight values, while
	// AVX code does not (it uses the vbroadcastsd instruction to reduce memory usage)
	if (incr >= 4) {
		results[24] = weight;
		results[25] = inv_weight;
	} else {
		for (i = 0; i < incr; i++) {
			results[i] = weight;
			results[incr+i] = inv_weight;
		}
		results += 2*incr;
	}

	// Now output the weighted sin/cos data
	twopi_over_N = dd_real::_2pi / (double) N;
	for (i = 0; i < incr; i++) {
		sincos (twopi_over_N * (double) (x + i * upper_x_incr), sine, cosine);

		sine2 = sine * cosine * 2.0;
		cosine2 = sqr (cosine) - sqr (sine);

		sine = sine + epsilon;		// Hack to avoid divide-by-zero errors
		sine2 = sine2 + epsilon;

		// This hack outputs the weights in a different order for SSE2 and AVX.
		// We changed the AVX output order for better grouping of data in the cache lines.
		if (incr < 4) {
			results[i] = sine * weight;
			results[incr+i] = cosine / sine;
			results[2*incr+i] = sine * inv_weight;
			results[3*incr+i] = sine2 * weight;
			results[4*incr+i] = cosine2 / sine2;
			results[5*incr+i] = sine2 * inv_weight;
		} else {
			results[i] = cosine / sine;
			results[incr+i] = cosine2 / sine2;
			results[2*incr+i] = sine * weight;
			results[3*incr+i] = sine2 * weight;
			results[4*incr+i] = sine * inv_weight;
			results[5*incr+i] = sine2 * inv_weight;
		}
	}
	END_x86_FIX
}

extern "C"
void gwsincos15by_weighted (
	void	*dd_data_arg,
	unsigned long x,
	unsigned long upper_x_incr,
	unsigned long N,
	unsigned long col,
	double	*results,
	int	incr)
{
	dd_real twopi_over_N, sine, cosine, sine2, cosine2, sine4, cosine4, sine5, cosine5;
	dd_real bpower, weight, inv_weight;
	int	i;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, col);
	weight = exp (dd_data->gw__logb * bpower);
	inv_weight = dd_data->gw__over_fftlen / weight;

	twopi_over_N = dd_real::_2pi / (double) N;
	for (i = 0; i < incr; i++) {
		sincos (twopi_over_N * (double) (x + i * upper_x_incr), sine, cosine);

		sine2 = sine * cosine * 2.0;
		cosine2 = sqr (cosine) - sqr (sine);

		sine4 = sine2 * cosine2 * 2.0;
		cosine4 = sqr (cosine2) - sqr (sine2);

		sine5 = sine * cosine4 + sine4 * cosine;
		cosine5 = cosine * cosine4 - sine * sine4;

		sine = sine + epsilon;		// Hack to avoid divide-by-zero errors
		sine5 = sine5 + epsilon;

		// This hack outputs the weights in a different order for SSE2 and AVX.
		// We changed the AVX output order for better grouping of data in the cache lines.
		if (incr < 4) {
			results[i] = sine * weight;
			results[incr+i] = cosine / sine;
			results[2*incr+i] = sine * inv_weight;
			results[3*incr+i] = sine5 * weight;
			results[4*incr+i] = cosine5 / sine5;
			results[5*incr+i] = sine5 * inv_weight;
		} else {
			results[i] = cosine / sine;
			results[incr+i] = cosine5 / sine5;
			results[2*incr+i] = sine * weight;
			results[3*incr+i] = sine5 * weight;
			results[4*incr+i] = sine * inv_weight;
			results[5*incr+i] = sine5 * inv_weight;
		}
	}
	END_x86_FIX
}

// Compute several column weights to later be applied to sine values of negacyclic pre-multipliers.  One pass AVX-512 FFTs.

extern "C"
void gwfft_colweights (
	void	*dd_data_arg,
	void	*colweights_arg,	/* Area to output column weights */
	int	count)			/* Number of column weights to generate */
{
	unsigned long j;
	dd_real	*colweights;
	dd_real temp, bpower;

	x86_FIX
	colweights = (dd_real *) colweights_arg;
	for (j = 0; j < (unsigned long) count; j++) {
		bpower = map_to_weight_power (dd_data_arg, j);
		*colweights++ = exp (dd_data->gw__logb * bpower);
	}
	END_x86_FIX
}

// Apply the column weights to the negacyclic premultipliers.  One pass AVX-512 FFTs.

extern "C"
void gwsincos1plusby_colweighted (
	unsigned long x,
	unsigned long inc,
	unsigned long N,
	void	*colweights_arg,	/* Column weights generated by gwfft_colweights */
	double	*results,
	int	incr,
	int	amt)
{
	dd_real	*colweights;
	dd_real twopi_over_N, sineinc, cosineinc, sine0, cosine0;
	int	num_output;

	x86_FIX
	twopi_over_N = dd_real::_2pi / (double) N;
	sincos (twopi_over_N * (double) inc, sineinc, cosineinc);
	sincos (twopi_over_N * (double) x, sine0, cosine0);

	colweights = (dd_real *) colweights_arg;
	for (num_output = 0; ; colweights++) {
		dd_real last_sine;
		/* Output the sine * column weight and cosine/sine value while protecting against divide by zero */
		results[0] = sine0;
		results[0] += epsilon;
		results[incr] = cosine0 / results[0];
		results[0] = sine0 * *colweights;
		results[0] += epsilon;
		results += incr + incr;
		num_output++;
		/* Break if we've output the requested amount */
		if (num_output == amt) break;
		/* Compute next cosine and sine value */
		last_sine = sine0;
		sine0 = sine0 * cosineinc + cosine0 * sineinc;
		cosine0 = cosine0 * cosineinc - last_sine * sineinc;
	}
	END_x86_FIX
}

// This computes the two FFT weights and the two fudged weights in one call.

extern "C"
void gwfft_weights_fudged (
	void	*dd_data_arg,
	unsigned long j,
	unsigned long b,
	double	*fft_weight,
	double	*fft_weight_inverse,
	double	*fft_weight_over_b,
	double	*fft_weight_inverse_times_b)
{
	dd_real bpower, weight, weight_inverse;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	weight = exp (dd_data->gw__logb * bpower);
	*fft_weight = double (weight);
	*fft_weight_over_b = double (weight / (double) b);
	weight_inverse = 1.0 / weight;
	*fft_weight_inverse = double (weight_inverse);
	*fft_weight_inverse_times_b = double (weight_inverse * (double) b);
	END_x86_FIX
}

/* Special version that merges group multiplier with the sine value from gwsincos1plus0123456789ABby. */
/* Used in AVX-512 negacyclic FFTs. */

extern "C"
void gwfft_weights_times_sine (
	void	*dd_data_arg,
	unsigned long j,		/* For computing group multiplier */
	unsigned long x,		/* For computing roots-of-minus-one sine value */
	unsigned long N,		/* For computing sine value */
	double	*fft_weight,		/* Returned group multiplier * sine */
	double	*fft_weight_inverse)	/* Returned 1/group multiplier * sine */
{
	dd_real bpower, weight, sine0;

	x86_FIX
	bpower = map_to_weight_power (dd_data_arg, j);
	weight = exp (dd_data->gw__logb * bpower);

	if (x == 0) {
		sine0 = epsilon;	/* Compensate for the way we calculated cos/sin */
	} else {
		sine0 = sin (dd_real::_2pi * (double) x / (double) N);
	}

	if (fft_weight != NULL) *fft_weight = double (weight * sine0);
	if (fft_weight_inverse != NULL) *fft_weight_inverse = double (sine0 / weight);

	END_x86_FIX
}

// Like the gwfft_weight routines but used for r4dwpn FFTs where part of the weight is applied
// during the FFT process.

extern "C"
double gwfft_partial_weight (
	void	*dd_data_arg,
	unsigned long j,
	unsigned long col)
{
	dd_real j_bpower, col_bpower, result;

	x86_FIX
	j_bpower = map_to_weight_power (dd_data_arg, j);
	col_bpower = map_to_weight_power (dd_data_arg, col);
	result = exp (dd_data->gw__logb * (j_bpower - col_bpower));
	END_x86_FIX
	return (double (result));
}

// Like gwfft_partial_weight, but faster and does not guarantee quite as much accuracy.

extern "C"
double gwfft_partial_weight_sloppy (
	void	*dd_data_arg,
	unsigned long j,
	unsigned long col)
{

	double j_bpower = map_to_weight_power_sloppy (dd_data_arg, j);
	double col_bpower = map_to_weight_power_sloppy (dd_data_arg, col);
	return (exp (dd_data->gwdbl__logb * (j_bpower - col_bpower)));
}

// Compute the inverse of the fft partial weight

extern "C"
double gwfft_partial_weight_inverse (
	void	*dd_data_arg,
	unsigned long j,
	unsigned long col)
{
	dd_real j_bpower, col_bpower, result;

	x86_FIX
	j_bpower = map_to_weight_power (dd_data_arg, j);
	col_bpower = map_to_weight_power (dd_data_arg, col);
	result = exp (dd_data->gw__logb * (col_bpower - j_bpower));
	END_x86_FIX
	return (double (result));
}

// Like the gwfft_partial_weight_inverse, but faster and does not guarantee quite as much accuracy.
// We can be fairly sloppy as these weights are used to read FFT data values.

extern "C"
double gwfft_partial_weight_inverse_sloppy (
	void	*dd_data_arg,
	unsigned long j,
	unsigned long col)
{
	double j_bpower = map_to_weight_power_sloppy (dd_data_arg, j);
	double col_bpower = map_to_weight_power_sloppy (dd_data_arg, col);
	return (exp (dd_data->gwdbl__logb * (col_bpower - j_bpower)));
}


// Reimplementation of several routines above, but using cached data for faster results

double inline cached_map_to_weight_power_sloppy (
	void	*dd_data_arg,
	uint64_t *cached_data,
	unsigned long j)
{
	// Use middle 64-bits (the fractional part) of cached 128-bit FFT base counter to calculate the weight power
	uint64_t tmp2 = base128middle;
	tmp2 = ~tmp2;							// Simulate a ceil(tmp2) - tmp2 operation
	double result = (double) (int64_t) (tmp2 >> 1);			// I think Intel can convert int64 to double easier than uint64
	result *= 1.0842021724855044340074528008699e-19;		// Times 2^-63
	if (! dd_data->gw__c_is_one) result += dd_data->gwdbl__logb_abs_c_div_fftlen * (double) j;
	return (result);
}

extern "C"
double gwcached_weight_sloppy (
	void	*dd_data_arg,
	uint64_t *cached_data,
	unsigned long j,
	int32_t	val)
{
	double	bpower, weight;

// Sloppy optimizations won't work if abs(c) is not one.  This is because the result is not in the range 1.0 to b.

	if (! dd_data->gw__c_is_one) {
		bpower = cached_map_to_weight_power_sloppy (dd_data_arg, cached_data, j);
		return ((double) val * exp (dd_data->gw__logb * bpower));
	}

// Compute weight from previous weight, but don't do too many of these in a row as floating point roundoff errors will accumulate
// NOTE: the fast_sloppy_multiplier values are pre-multiplied by 1/b, this is done so that correcting computed weights requires a
// multiplication by b which can be done without any floating point roundoff error.

	if (j > last_weight_j && j <= last_weight_j + NUM_SLOPPY_MULTS) {
		weight = last_weight_sloppy * dd_data->fast_sloppy_multiplier[j - last_weight_j - 1];

// Just to be safe, if result is at all close to the boundaries return the carefully computed weight.

		if (double (weight) < dd_data->gwdbl__b_inverse + 0.001 || (double (weight) > 0.999 && double (weight) < 1.001)) {
			bpower = cached_map_to_weight_power_sloppy (dd_data_arg, cached_data, j);
			weight = exp (dd_data->gwdbl__logb * bpower);
			last_weight_j = j;
			last_weight_sloppy = weight;
		}

// Multiply val by b as that can be done without roundoff error, then apply the computed weight

		if (double (weight) < 1.0) return (((double) val * dd_data->gwdbl__b) * weight);
	}

// Test for getting the cached value (which likely never happens)

	else if (j == last_weight_j)
		weight = last_weight_sloppy;

// Get an accurate value to start off a series of computing next weights

	else {
		bpower = cached_map_to_weight_power_sloppy (dd_data_arg, cached_data, j);
		weight = exp (dd_data->gwdbl__logb * bpower);
		last_weight_j = j;
		last_weight_sloppy = weight;
	}

	return ((double) val * weight);
}

extern "C"
double gwcached_weight_inverse_sloppy (
	void	*dd_data_arg,
	uint64_t *cached_data,
	unsigned long j)
{
	double	bpower, result;

// Sequential sloppy optimizations won't work if abs(c) is not one.  This is because the result is not in the range 1/b to 1.0.

	if (! dd_data->gw__c_is_one) {
		bpower = cached_map_to_weight_power_sloppy (dd_data_arg, cached_data, j);
		return (exp (dd_data->gwdbl__logb * -bpower));
	}

// Compute weight from previous weight, but don't do too many of these in a row as floating point roundoff errors will accumulate.
// By my reckoning, a string of 49 multiplications can lose up to 49*log2(1.5) = 28.66 bits of precision.  Since this sloppy weight will be used by
// get_fft_value to recover an up to 24-bit integer from a double with 53 bits of precision.  When b=2 where multiplying by b is error free.

	if (j > last_inv_weight_j && j <= last_inv_weight_j + NUM_SLOPPY_MULTS) {
		result = last_inv_weight_sloppy * dd_data->fast_inv_sloppy_multiplier[j - last_inv_weight_j - 1];
		int mul_by_b_required = (double (result) <= dd_data->gwdbl__b_inverse);
		if (mul_by_b_required) result *= dd_data->gwdbl__b;

		// If calculated last member of the set, see if we can make this the base result in the next set
		if (j == last_inv_weight_j + NUM_SLOPPY_MULTS) {
			inv_weight_sloppy_muls++;
			if (dd_data->gwdbl__b == 2.0) {
				if (inv_weight_sloppy_muls <= 48) {
					last_inv_weight_j = j;
					last_inv_weight_sloppy = result;
				}
			} else {
				if (mul_by_b_required) inv_weight_sloppy_muls++;
				if (inv_weight_sloppy_muls <= 47) {
					last_inv_weight_j = j;
					last_inv_weight_sloppy = result;
				}
			}
		}

// Just to be safe, if result is at all close to the boundaries return the carefully computed weight.

		if (double (result) < dd_data->gwdbl__b_inverse + 0.001 || double (result) > 0.999) {
			bpower = cached_map_to_weight_power_sloppy (dd_data_arg, cached_data, j);
			result = exp (dd_data->gwdbl__logb * -bpower);
			last_inv_weight_j = j;
			last_inv_weight_sloppy = result;
			inv_weight_sloppy_muls = 0;
		}
	}

// Test for getting the cached value (which likely never happens)

	else if (j == last_inv_weight_j)
		result = last_inv_weight_sloppy;

// Get an accurate value to start off a series of computing next weights

	else {
		bpower = cached_map_to_weight_power_sloppy (dd_data_arg, cached_data, j);
		result = exp (dd_data->gwdbl__logb * -bpower);
		last_inv_weight_j = j;
		last_inv_weight_sloppy = result;
		inv_weight_sloppy_muls = 0;
	}

	return (result);
}

double cached_partial_weight_init (
	void	*dd_data_arg,
	uint64_t *cached_data,
	unsigned long j,
	unsigned long col)
{
	// Calculate the weight power partial weights (group multipliers).  Each group multiplier will need two weights cached (bpower and bpower-1).
	// Cache data needed for quickly deciding which of the two weights to use in future partial weight calls.

	// Use middle 64-bits (the fractional part) of cached 128-bit FFT base counter to calculate j's weight power
	uint64_t jtmp2 = base128middle;
	jtmp2 = ~jtmp2;								// Simulate a ceil(jtmp2) - jtmp2 operation

	// Calculate col's weight power from scratch.
	uint32_t ctmp1 = (uint32_t) col * (uint32_t) dd_data->frac_bpw1;	// 0.A
	uint64_t ctmp2 = (uint64_t) col * (uint64_t) dd_data->frac_bpw2;	// 0.BC
	uint64_t ctmp3 = (uint64_t) col * (uint64_t) dd_data->frac_bpw3;	// 0.0DE
	ctmp2 += (ctmp3 >> 32);							// Cannot overflow
	ctmp2 += (((uint64_t) ctmp1) << 32);					// Overflow into integer portion is irrelevant
	ctmp2 = 0 - ctmp2;							// Simulate a ceil(tmp2) - tmp2 operation

	// Figure out which FFT base fractional value delineates where bpower switches from positive to negative.  Example:
	// ctmp2 = 0.41,  jtmp2 = 0.16.  This implies ctmp2 is always 0.25 more than jtmp2.
	// This implies if jtmp2 < 0.75 then col_bpower >= j_bpower.
	// This implies if pre_not_jtmp2 >= 0.25 then col_bpower >= j_bpower.  Remember this 0.25 value for cached_partial_which_weight.
	partial_transition = ctmp2 - jtmp2;

	// Convert bpowers into doubles
     	double j_bpower = (double) (int64_t) (jtmp2 >> 1);			// I think Intel can convert int64 to double easier than uint64
	j_bpower *= 1.0842021724855044340074528008699e-19;			// Times 2^-63
     	double col_bpower = (double) (int64_t) (ctmp2 >> 1);			// I think Intel can convert int64 to double easier than uint64
	col_bpower *= 1.0842021724855044340074528008699e-19;			// Times 2^-63

	// Return a positive value for bpower.  Caller will cache weights for bpower and bpower-1.
	double bpower = col_bpower - j_bpower;
	if (bpower < 0.0) bpower += 1.0;

	// Final adjustment to group multiplier's bpower for abs(c) != 1 case
	if (! dd_data->gw__c_is_one) bpower += dd_data->gwdbl__logb_abs_c_div_fftlen * (double) ((double) col - (double) j);

	return bpower;
}

// Return 0 if col_bpower >= j_bpower.  Return 1 if col_bpower < j_power.  This will tell caller which cached weight to return.
int inline cached_partial_which_weight (
	uint64_t *cached_data)
{
	// See cached_partial_init for example on how we determine if col_bpower >= j_bpower.
	// Compare middle 64 bits of base 128 (the fractional part) to precomputed transition point.
	return (base128middle < partial_transition);
}

extern "C"
double gwcached_partial_weight_sloppy (
	void	*dd_data_arg,
	uint64_t *cached_data,
	unsigned long j)
{
	// If cached weights have not been calculated, then calculate and cache the two weights as well as
	// caching data needed for quickly deciding which weight to use in future calls
	if (((uint64_t *) partial_weight)[0] == 0) {		// Check for weight not yet cached
		unsigned long col = dwpncol_value;
		double bpower = cached_partial_weight_init (dd_data_arg, cached_data, j, col);
		partial_weight[0] = exp (dd_data->gwdbl__logb * -bpower);
		partial_weight[1] = partial_weight[0] * dd_data->gwdbl__b;
	}

	return (partial_weight[cached_partial_which_weight (cached_data)]);
}

extern "C"
double gwcached_partial_weight_inverse_sloppy (
	void	*dd_data_arg,
	uint64_t *cached_data,
	unsigned long j)
{
	// If cached inverse weights have not been calculated, then calculate and cache the two inverse weights as well as
	// caching data needed for quickly deciding which weight to use in future calls
	if (((uint64_t *) partial_inv_weight)[0] == 0) {	// Check for inverse weight not yet cached
		unsigned long col = dwpncol_value;
		double bpower = cached_partial_weight_init (dd_data_arg, cached_data, j, col);
		partial_inv_weight[0] = exp (dd_data->gwdbl__logb * bpower);
		partial_inv_weight[1] = partial_inv_weight[0] * dd_data->gwdbl__b_inverse;
	}

	// There are two cached partial weight values.  Return one of them.
	return (partial_inv_weight[cached_partial_which_weight (cached_data)]);
}

