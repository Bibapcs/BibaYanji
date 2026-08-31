/* ECM stage 1 using GWNUM -- for use by GMP-ECM

  Copyright 1996-2024 Mersenne Research, Inc.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2 of the License, or (at your
  option) any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along
  with this program; see the file COPYING.  If not, write to the Free
  Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

/**************************************************************
 *
 *	ecm.c
 *
 *	ECM stage 1 factoring program
 *
 *	Original author:  Richard Crandall - www.perfsci.com
 *	Adapted to Mersenne numbers and optimized by George Woltman
 *	Further optimizations from Paul Zimmerman's GMP-ECM program
 *	Other important ideas courtesy of Peter Montgomery.
 *
 *************************************************************/

#include <stdlib.h>
#include "cpuid.h"
#include "gmp.h"		// GMP library
#include "gwnum.h"
#include "math.h"
#include "memory.h"

/* Global variables */

giant	N = NULL;		/* Number being factored */
giant	FAC = NULL;		/* Found factor */

gwhandle gwdata;
gwnum	Ad4 = NULL;

/* Bit manipulation macros */

#define bitset(a,i)	{ a[(i) >> 3] |= (1 << ((i) & 7)); }
#define bitclr(a,i)	{ a[(i) >> 3] &= ~(1 << ((i) & 7)); }
#define bittst(a,i)	(a[(i) >> 3] & (1 << ((i) & 7)))


/* Perform cleanup functions. */

void ecm_cleanup ()
{
	free (N);
	N = NULL;
	free (FAC);
	FAC = NULL;
	gwdone (&gwdata);
}


/* Determine if a number is prime */

int isPrime (
	unsigned long p)
{
	unsigned long i;
	for (i = 2; i * i <= p; i = (i + 1) | 1)
		if (p % i == 0) return (FALSE);
	return (TRUE);
}

/* Use a simple sieve to find prime numbers */

#define MAX_PRIMES	6542
static	unsigned int *primes = NULL;
static	struct sieve_info {
	uint64_t first_number;
	unsigned int bit_number;
	unsigned int num_primes;
	uint64_t start;
	char	array[4096];
} si = {0};

/* Fill up the sieve array */

void fill_sieve (void)
{
	unsigned int i, fmax;

/* Determine the first bit to clear */

	fmax = (unsigned int)
		sqrt ((double) (si.first_number + sizeof (si.array) * 8 * 2));
	for (i = si.num_primes; i < MAX_PRIMES * 2; i += 2) {
		unsigned long f, r, bit;
		f = primes[i];
		if (f > fmax) break;
		if (si.first_number == 3) {
			bit = (f * f - 3) >> 1;
		} else {
			r = (unsigned long) (si.first_number % f);
			if (r == 0) bit = 0;
			else if (r & 1) bit = (f - r) / 2;
			else bit = (f + f - r) / 2;
			if (f == si.first_number + 2 * bit) bit += f;
		}
		primes[i+1] = bit;
	}
	si.num_primes = i;

/* Fill the sieve with ones, then zero out the composites */

	memset (si.array, 0xFF, sizeof (si.array));
	for (i = 0; i < si.num_primes; i += 2) {
		unsigned int f, bit;
		f = primes[i];
		for (bit = primes[i+1]; bit < sizeof (si.array) * 8; bit += f)
			bitclr (si.array, bit);
		primes[i+1] = bit - sizeof (si.array) * 8;
	}
	si.bit_number = 0;
}

/* Start sieve by allocate a sieve info structure */

void start_sieve (
	uint64_t start)
{
	unsigned int i;

/* Remember starting point (in case its 2) and make real start odd */

	if (start < 2) start = 2;
	si.start = start;
	start |= 1;

/* See if we can just reuse the existing sieve */

	if (si.first_number &&
	    start >= si.first_number &&
	    start < si.first_number + sizeof (si.array) * 8 * 2) {
		si.bit_number = (unsigned int) (start - si.first_number) / 2;
		return;
	}

/* Initialize sieve */

	if (primes == NULL) {
		unsigned int f;
		primes = (unsigned int *)
			malloc (MAX_PRIMES * 2 * sizeof (unsigned int));
		for (i = 0, f = 3; i < MAX_PRIMES * 2; f += 2)
			if (isPrime (f)) primes[i] = f, i += 2;
	}

	si.first_number = start;
	si.num_primes = 0;
	fill_sieve ();
}

/* Return next prime from the sieve */

uint64_t sieve (void)
{
	if (si.start == 2) {
		si.start = 3;
		return (2);
	}
	for ( ; ; ) {
		unsigned int bit;
		if (si.bit_number == sizeof (si.array) * 8) {
			si.first_number += 2 * sizeof (si.array) * 8;
			fill_sieve ();
		}
		bit = si.bit_number++;
		if (bittst (si.array, bit))
			return (si.first_number + 2 * bit);
	}
}

/**************************************************************
 *
 *	Functions
 *
 **************************************************************/

/* computes 2P=(x2:z2) from P=(x1:z1), uses the global variables Ad4 */

void ell_dbl (
	gwnum	x1,
	gwnum	z1,
	gwnum	x2,
	gwnum	z2)
{					/* 10 FFTs */
	gwnum	t1, t3;
	t1 = gwalloc (&gwdata);
	t3 = gwalloc (&gwdata);
	gwaddsub4 (&gwdata, x1, z1, t1, x2);
	gwsquare (&gwdata, t1);			/* t1 = (x1 + z1)^2 */
	gwsquare (&gwdata, x2);			/* t2 = (x1 - z1)^2 (store in x2) */
	gwsub3 (&gwdata, t1, x2, t3);		/* t3 = t1 - t2 = 4 * x1 * z1 */
	gwfft (&gwdata, t3, t3);
	gwfft (&gwdata, x2, x2);
	gwfftadd3 (&gwdata, t3, x2, t1);	/* Compute the fft of t1! */
	gwfftfftmul (&gwdata, Ad4, x2, x2);	/* x2 = t2 * Ad4 */
	gwfft (&gwdata, x2, x2);
	gwfftadd3 (&gwdata, x2, t3, z2);	/* z2 = (t2 * Ad4 + t3) */
	gwfftfftmul (&gwdata, t3, z2, z2);	/* z2 = z2 * t3 */
	gwfftfftmul (&gwdata, t1, x2, x2);	/* x2 = x2 * t1 */
	gwfree (&gwdata, t1);
	gwfree (&gwdata, t3);
}

/* adds Q=(x2:z2) and R=(x1:z1) and puts the result in (x3:z3),
   Assumes that Q-R=P or R-Q=P where P=(xdiff:zdiff). */

#ifdef ELL_ADD_USED
void ell_add (
	gwnum 	x1,
	gwnum 	z1,
	gwnum 	x2,
	gwnum 	z2,
	gwnum	xdiff,
	gwnum	zdiff,
	gwnum	x3,
	gwnum	z3)
{					/* 16 FFTs */
	gwnum	t1, t2, t3;
	t1 = gwalloc (&gwdata);
	t2 = gwalloc (&gwdata);
	t3 = gwalloc (&gwdata);
	gwaddsub4 (&gwdata, x1, z1, t1, t2);	/* t1 = (x1 + z1)(x2 - z2) */
						/* t2 = (x1 - z1)(x2 + z2) */
	gwsub3 (&gwdata, x2, z2, t3);
	gwmul (&gwdata, t3, t1);
	gwadd3 (&gwdata, x2, z2, t3);
	gwmul (&gwdata, t3, t2);
	gwaddsub (&gwdata, t2, t1);		/* x3 = (t2 + t1)^2 * zdiff */
	gwsquare (&gwdata, t2);
	gwmul (&gwdata, zdiff, t2);
	gwsquare (&gwdata, t1);			/* z3 = (t2 - t1)^2 * xdiff */
	gwmul (&gwdata, xdiff, t1);
	gwcopy (&gwdata, t2, x3);
	gwcopy (&gwdata, t1, z3);
	gwfree (&gwdata, t1);
	gwfree (&gwdata, t2);
	gwfree (&gwdata, t3);
}
#endif

/* Like ell_add except that x1, z1, xdiff, and zdiff have been FFTed */
/* NOTE: x2 and z2 represent the FFTs of (x2+z2) and (x2-z2) respectively. */

void ell_add_special (
	gwnum 	x1,
	gwnum 	z1,
	gwnum 	x2,
	gwnum 	z2,
	gwnum	xdiff,
	gwnum	zdiff,
	gwnum	x3,
	gwnum	z3)
{				/* 10 FFTs */
	gwnum	t1, t2;
	t1 = gwalloc (&gwdata);
	t2 = gwalloc (&gwdata);
	gwfftaddsub4 (&gwdata, x1, z1, t1, t2);	/* t1 = (x1 + z1)(x2 - z2) */
						/* t2 = (x1 - z1)(x2 + z2) */
	gwfftfftmul (&gwdata, z2, t1, t1);
	gwfftfftmul (&gwdata, x2, t2, t2);
	gwaddsub (&gwdata, t2, t1);		/* x3 = (t2 + t1)^2 * zdiff */
	gwsquare (&gwdata, t2);
	gwfftmul (&gwdata, zdiff, t2);
	gwsquare (&gwdata, t1);			/* z3 = (t2 - t1)^2 * xdiff */
	gwfftmul (&gwdata, xdiff, t1);
	gwcopy (&gwdata, t2, x3);
	gwcopy (&gwdata, t1, z3);
	gwfree (&gwdata, t1);
	gwfree (&gwdata, t2);
}

/* This routine is called prior to a series of many ell_add_fft and */
/* ell_dbl_fft calls.  The sequence ends by calling ell_add_fft_last. */
/* Note: We used to simply just FFT x1 and z1.  However, convolution error */
/* in computing (x1+z1)^2 and the like was too great.  Instead, we now */
/* save the FFTs of (x1+z1) and (x1-z1).  The multiplication by xdiff */
/* and zdiff is now more complicated, but convolution errors are reduced */
/* since only one argument of any multiply will involve a value that is */
/* the sum of two FFTs rather than computing a properly normalized sum */
/* and then taking the FFT. */

void ell_begin_fft (
	gwnum	x1,
	gwnum	z1,
	gwnum	x2,
	gwnum	z2)
{
	gwaddsub4 (&gwdata, x1, z1, x2, z2); /* x2 = x1 + z1, z2 = x1 - z1 */
	gwfft (&gwdata, x2, x2);
	gwfft (&gwdata, z2, z2);
}

/* Like ell_dbl, but the input arguments are FFTs of x1=x1+z1, z1=x1-z1 */
/* The output arguments are also FFTs of x2=x2+z2, z2=x2-z2 */

void ell_dbl_fft (
	gwnum	x1,
	gwnum	z1,
	gwnum	x2,
	gwnum	z2)
{					/* 10 FFTs, 4 adds */
	gwnum	t1, t3;
	t1 = gwalloc (&gwdata);
	t3 = gwalloc (&gwdata);
	gwfftfftmul (&gwdata, x1, x1, t1);	/* t1 = (x1 + z1)^2 */
	gwfftfftmul (&gwdata, z1, z1, x2);	/* t2 = (x1 - z1)^2 (store in x2) */
	gwsub3 (&gwdata, t1, x2, t3);		/* t3 = t1 - t2 = 4 * x1 * z1 */
	gwfft (&gwdata, t3, t3);
	gwfft (&gwdata, x2, x2);
	gwfftadd3 (&gwdata, t3, x2, t1);	/* Compute fft of t1! */
	gwfftfftmul (&gwdata, Ad4, x2, x2);	/* x2 = t2 * Ad4 */
	gwfft (&gwdata, x2, x2);
	gwfftadd3 (&gwdata, x2, t3, z2);	/* z2 = (t2 * Ad4 + t3) * t3 */
	gwfftfftmul (&gwdata, t3, z2, z2);
	gwfftfftmul (&gwdata, t1, x2, x2);	/* x2 = x2 * t1 */
	gwaddsub (&gwdata, x2, z2);		/* x2 = x2 + z2, z2 = x2 - z2 */
	gwfft (&gwdata, x2, x2);
	gwfft (&gwdata, z2, z2);
	gwfree (&gwdata, t1);
	gwfree (&gwdata, t3);
}

/* Like ell_add but input arguments are FFTs of x1=x1+z1, z1=x1-z1, */
/* x2=x2+z2, z2=x2-z2, xdiff=xdiff+zdiff, zdiff=xdiff-zdiff. */
/* The output arguments are also FFTs of x3=x3+z3, z3=x3-z3 */

void ell_add_fft (
	gwnum 	x1,
	gwnum 	z1,
	gwnum 	x2,
	gwnum 	z2,
	gwnum 	xdiff,
	gwnum 	zdiff,
	gwnum 	x3,
	gwnum 	z3)
{				/* 12 FFTs, 6 adds */
	gwnum	t1, t2;
	t1 = gwalloc (&gwdata);
	t2 = gwalloc (&gwdata);
	gwfftfftmul (&gwdata, x1, z2, t1);/* t1 = (x1 + z1)(x2 - z2) */
	gwfftfftmul (&gwdata, x2, z1, t2);/* t2 = (x1 - z1)(x2 + z2) */
	gwaddsub (&gwdata, t2, t1);
	gwsquare (&gwdata, t2);		/* t2 = (t2 + t1)^2 (will become x3) */
	gwsquare (&gwdata, t1);		/* t1 = (t2 - t1)^2 (will become z3) */
	gwfftaddsub4 (&gwdata, xdiff, zdiff, x3, z3);
					/* x3 = xdiff = (xdiff + zdiff) */
					/* z3 = zdiff = (xdiff - zdiff) */
	gwfftmul (&gwdata, z3, t2);	/* t2 = t2 * zdiff (new x3) */
	gwfftmul (&gwdata, x3, t1);	/* t1 = t1 * xdiff (new z3) */
	gwaddsub (&gwdata, t2, t1);	/* t2 = x3 + z3, t1 = x3 - z3 */
	gwfft (&gwdata, t2, x3);
	gwfft (&gwdata, t1, z3);
	gwfree (&gwdata, t1);
	gwfree (&gwdata, t2);
}

/* Like ell_add_fft but output arguments are not FFTed. */

void ell_add_fft_last (
	gwnum 	x1,
	gwnum 	z1,
	gwnum 	x2,
	gwnum 	z2,
	gwnum 	xdiff,
	gwnum 	zdiff,
	gwnum 	x3,
	gwnum 	z3)
{				/* 10 FFTs, 6 adds */
	gwnum	t1, t2;
	t1 = gwalloc (&gwdata);
	t2 = gwalloc (&gwdata);
	gwfftfftmul (&gwdata, x1, z2, t1);/* t1 = (x1 + z1)(x2 - z2) */
	gwfftfftmul (&gwdata, x2, z1, t2);/* t2 = (x1 - z1)(x2 + z2) */
	if (xdiff != x3) {
		gwaddsub4 (&gwdata, t2, t1, x3, z3);
		gwsquare (&gwdata, x3);		/* x3 = (t2 + t1)^2 */
		gwsquare (&gwdata, z3);		/* z3 = (t2 - t1)^2 */
		gwfftaddsub4 (&gwdata, xdiff, zdiff, t1, t2);
				/* t1 = xdiff = (xdiff + zdiff) */
				/* t2 = zdiff = (xdiff - zdiff) */
		gwfftmul (&gwdata, t2, x3);	/* x3 = x3 * zdiff */
		gwfftmul (&gwdata, t1, z3);	/* z3 = z3 * xdiff */
	} else {
		gwaddsub (&gwdata, t2, t1);
		gwsquare (&gwdata, t2); gwfft (&gwdata, t2, t2);
		gwsquare (&gwdata, t1); gwfft (&gwdata, t1, t1);
		gwfftaddsub4 (&gwdata, xdiff, zdiff, z3, x3);
		gwfftfftmul (&gwdata, t2, x3, x3);
		gwfftfftmul (&gwdata, t1, z3, z3);
	}
	gwfree (&gwdata, t1);
	gwfree (&gwdata, t2);
}

/* Perform an elliptic multiply using an algorithm developed by */
/* Peter Montgomery.  Basically, we try to find a near optimal */
/* Lucas chain of additions that generates the number we are */
/* multiplying by.  This minimizes the number of calls to ell_dbl */
/* and ell_add. */

/* The costing function assigns an ell_dbl call a cost of 12 and */
/* an ell_add call a cost of 12.  This cost estimates the number */
/* of forward and inverse transforms performed. */

#define swap(a,b)	{t=a;a=b;b=t;}

unsigned long lucas_cost (
	uint64_t n,
	double	v)
{
	uint64_t d, e, t, dmod3, emod3;
	unsigned long c;

	c = 0;
	while (n != 1) {
	    d = (uint64_t) (n/v+0.5); e = n - d;
	    d = d - e;

	    c += 12;

	    while (d != e) {
		if (d < e) {
			swap (d,e);
		}
		if (d <= e + (e >> 2)) {
			if ((dmod3 = d%3) == 3 - (emod3 = e%3)) {
				t = d;
				d = (d+d-e)/3;
				e = (e+e-t)/3;
				c += 36;
				continue;
			}
			if (dmod3 == emod3 && (d&1) == (e&1)) {
				d = (d-e) >> 1;
				c += 22;
				continue;
			}
		}
		if (d <= (e << 2)) {
			d = d-e;
			c += 12;
		} else if ((d&1) == (e&1)) {
			d = (d-e) >> 1;
			c += 22;
		} else if ((d&1) == 0) {
			d = d >> 1;
			c += 22;
		} else if ((dmod3 = d%3) == 0) {
			d = d/3-e;
			c += 46;
		} else if (dmod3 == 3 - (emod3 = e%3)) {
			d = (d-e-e)/3;
			c += 46;
		} else if (dmod3 == emod3) {
			d = (d-e)/3;
			c += 46;
		} else {
			e = e >> 1;
			c += 22;
		}
	    }
	    c += 12;
	    n = d;
	}

	return (c);
}

void lucas_mul (
	gwnum	xx,
	gwnum	zz,
	uint64_t n,
	double	v)
{
	uint64_t d, e, t, dmod3, emod3;
	gwnum	xA, zA, xB, zB, xC, zC, xs, zs, xt, zt;

	xA = gwalloc (&gwdata);
	zA = gwalloc (&gwdata);
	xB = gwalloc (&gwdata);
	zB = gwalloc (&gwdata);
	xC = gwalloc (&gwdata);
	zC = gwalloc (&gwdata);
	xs = xx;
	zs = zz;
	xt = gwalloc (&gwdata);
	zt = gwalloc (&gwdata);

	while (n != 1) {
	    ell_begin_fft (xx, zz, xA, zA);			/* A */
	    ell_dbl_fft (xA, zA, xB, zB);			/* B = 2*A */
	    gwcopy (&gwdata, xA, xC); gwcopy (&gwdata, zA, zC);	/* C = A */

	    d = (uint64_t) (n/v+0.5); e = n - d;
	    d = d - e;

	    while (d != e) {
		if (d < e) {
			swap (d, e);
			gwswap (xA, xB); gwswap (zA, zB);
		}
		if (d <= e + (e >> 2)) {
			if ((dmod3 = d%3) == 3 - (emod3 = e%3)) {
				ell_add_fft (xA, zA, xB, zB, xC, zC, xs, zs);/* S = A+B */
				ell_add_fft (xA, zA, xs, zs, xB, zB, xt, zt);/* T = A+S */
				ell_add_fft (xs, zs, xB, zB, xA, zA, xB, zB);/* B = B+S */
				gwswap (xt, xA); gwswap (zt, zA);/* A = T */
				t = d;
				d = (d+d-e)/3;
				e = (e+e-t)/3;
				continue;
			}
			if (dmod3 == emod3 && (d&1) == (e&1)) {
				ell_add_fft (xA, zA, xB, zB, xC, zC, xB, zB);/* B = A+B */
				ell_dbl_fft (xA, zA, xA, zA);	/* A = 2*A */
				d = (d-e) >> 1;
				continue;
			}
		}
		if (d <= (e << 2)) {
			ell_add_fft (xA, zA, xB, zB, xC, zC, xC, zC);/* B = A+B */
			gwswap (xB, xC); gwswap (zB, zC);	/* C = B */
			d = d-e;
		} else if ((d&1) == (e&1)) {
			ell_add_fft (xA, zA, xB, zB, xC, zC, xB, zB);/* B = A+B */
			ell_dbl_fft (xA, zA, xA, zA);		/* A = 2*A */
			d = (d-e) >> 1;
		} else if ((d&1) == 0) {
			ell_add_fft (xA, zA, xC, zC, xB, zB, xC, zC);/* C = A+C */
			ell_dbl_fft (xA, zA, xA, zA);		/* A = 2*A */
			d = d >> 1;
		} else if ((dmod3 = d%3) == 0) {
			ell_dbl_fft (xA, zA, xs, zs);		/* S = 2*A */
			ell_add_fft (xA, zA, xB, zB, xC, zC, xt, zt);/* T = A+B */
			ell_add_fft (xs, zs, xA, zA, xA, zA, xA, zA);/* A = S+A */
			ell_add_fft (xs, zs, xt, zt, xC, zC, xC, zC);/* B = S+T */
			gwswap (xB, xC); gwswap (zB, zC);	/* C = B */
			d = d/3-e;
		} else if (dmod3 == 3 - (emod3 = e%3)) {
			ell_add_fft (xA, zA, xB, zB, xC, zC, xs, zs);/* S = A+B */
			ell_add_fft (xA, zA, xs, zs, xB, zB, xB, zB);/* B = A+S */
			ell_dbl_fft (xA, zA, xs, zs);		/* S = 2*A */
			ell_add_fft (xs, zs, xA, zA, xA, zA, xA, zA);/* A = S+A */
			d = (d-e-e)/3;
		} else if (dmod3 == emod3) {
			ell_add_fft (xA, zA, xB, zB, xC, zC, xt, zt);/* T = A+B */
			ell_add_fft (xA, zA, xC, zC, xB, zB, xC, zC);/* C = A+C */
			gwswap (xt, xB); gwswap (zt, zB);	/* B = T */
			ell_dbl_fft (xA, zA, xs, zs);		/* S = 2*A */
			ell_add_fft (xs, zs, xA, zA, xA, zA, xA, zA);/* A = S+A */
			d = (d-e)/3;
		} else {
			ell_add_fft (xB, zB, xC, zC, xA, zA, xC, zC);/* C = C-B */
			ell_dbl_fft (xB, zB, xB, zB);		/* B = 2*B */
			e = e >> 1;
		}
	    }

	    ell_add_fft_last (xB, zB, xA, zA, xC, zC, xx, zz);	/* A = A+B */

	    n = d;
	}
	gwfree (&gwdata, xA);
	gwfree (&gwdata, zA);
	gwfree (&gwdata, xB);
	gwfree (&gwdata, zB);
	gwfree (&gwdata, xC);
	gwfree (&gwdata, zC);
	gwfree (&gwdata, xt);
	gwfree (&gwdata, zt);
}

/* Multiplies the point (xx,zz) by n using a combination */
/* of ell_dbl and ell_add calls */

void bin_ell_mul (
	gwnum	xx,
	gwnum	zz,
	uint64_t n)
{
	uint64_t c;
	unsigned long zeros;
	gwnum	xorg, zorg, xs, zs;

	xorg = gwalloc (&gwdata);
	zorg = gwalloc (&gwdata);
	xs = gwalloc (&gwdata);
	zs = gwalloc (&gwdata);

	for (zeros = 0; (n & 1) == 0; zeros++) n >>= 1;

	if (n > 1) {
		ell_begin_fft (xx, zz, xorg, zorg);

		c = 1; c <<= 63;
		while ((c&n) == 0) c >>= 1;
		c >>= 1;

		/* If the second bit is zero, we can save one ell_dbl call */

		if (c&n) {
			gwcopy (&gwdata, xorg, xx); gwcopy (&gwdata, zorg, zz);
			ell_dbl_fft (xx, zz, xs, zs);
		} else {
			ell_dbl_fft (xorg, zorg, xx, zz);
			ell_add_fft (xorg, zorg, xx, zz, xorg, zorg, xs, zs);
			c >>= 1;
		}

		/* Do the rest of the bits */

		do {
			if (c&n) {
				if (c == 1) {
					ell_add_fft_last (xs, zs, xx, zz, xorg, zorg, xx, zz);
				} else {
					ell_add_fft (xs, zs, xx, zz, xorg, zorg, xx, zz);
					ell_dbl_fft (xs, zs, xs, zs);
				}
			} else {
				ell_add_fft (xx, zz, xs, zs, xorg, zorg, xs, zs);
				ell_dbl_fft (xx, zz, xx, zz);
			}
			c >>= 1;
		} while (c);
	}

	gwfree (&gwdata, xorg); 
	gwfree (&gwdata, zorg); 
	gwfree (&gwdata, xs); 
	gwfree (&gwdata, zs); 

	while (zeros--) ell_dbl (xx, zz, xx, zz);
}

/* Try a series of Lucas chains to find the cheapest. */
/* First try v = (1+sqrt(5))/2, then (2+v)/(1+v), then (3+2*v)/(2+v), */
/* then (5+3*v)/(3+2*v), etc.  Finally, execute the cheapest. */
/* This is much faster than bin_ell_mul, but uses more memory. */

void ell_mul (
	gwnum	xx,
	gwnum	zz,
	uint64_t n)
{
	unsigned long zeros;

	for (zeros = 0; (n & 1) == 0; zeros++) n >>= 1;

	if (n > 1) {
		unsigned long c, min;
		double	minv;

		min = lucas_cost (n, minv = 1.61803398875);/*v=(1+sqrt(5))/2*/

		c = lucas_cost (n, 1.38196601125);	/*(2+v)/(1+v)*/
		if (c < min) min = c, minv = 1.38196601125;

		c = lucas_cost (n, 1.72360679775);	/*(3+2*v)/(2+v)*/
		if (c < min) min = c, minv = 1.72360679775;

		c = lucas_cost (n, 1.580178728295);	/*(5+3*v)/(3+2*v)*/
		if (c < min) min = c, minv = 1.580178728295;

		c = lucas_cost (n, 1.632839806089);	/*(8+5*v)/(5+3*v)*/
		if (c < min) min = c, minv = 1.632839806089;

		c = lucas_cost (n, 1.612429949509);	/*(13+8*v)/(8+5*v)*/
		if (c < min) min = c, minv = 1.612429949509;

		c = lucas_cost (n, 1.620181980807);	/*(21+13*v)/(13+8*v)*/
		if (c < min) min = c, minv = 1.620181980807;

		c = lucas_cost (n, 1.617214616534);	/*(34+21*v)/(21+13*v)*/
		if (c < min) min = c, minv = 1.617214616534;

		c = lucas_cost (n, 1.618347119656);	/*(55+34*v)/(34+21*v)*/
		if (c < min) min = c, minv = 1.618347119656;

		c = lucas_cost (n, 1.617914406529);	/*(89+55*v)/(55+34*v)*/
		if (c < min) min = c, minv = 1.617914406529;

		lucas_mul (xx, zz, n, minv);
	}
	while (zeros--) ell_dbl (xx, zz, xx, zz);
}

/* Test if factor divides N, return TRUE if it does.  Destroys N. */

int testFactor (
	giant	f)
{
	modg (f, N);
	return (isZero (N));
}

/* Computes the modular inverse of a number */
/* This is done using the extended GCD algorithm */
/* The GCD is returned in FAC.  Function returns FALSE */
/* if it was interrupted by an escape. */

int modinv (
	gwnum b)
{
	giant	v;

/* Convert input number to binary */

	v = popg (&gwdata.gdata, ((unsigned long) gwdata.bit_length >> 5) + 5);
	gwtogiant (&gwdata, b, v);

#ifdef MODINV_USING_GIANTS

	int	stop_reason;

/* Let the invg code use gwnum b's memory. */
/* Compute 1/v mod N */

	gwfree_temporarily (&gwdata, b);
	stop_reason = invgi (&gwdata.gdata, 0, N, v);
	gwrealloc_temporarily (&gwdata, b);
	if (stop_reason) {
		pushg (&gwdata.gdata, 1);
		return (FALSE);
	}

/* If a factor was found, save it in FAC */

	if (v->sign < 0) {
		negg (v);
		FAC = allocgiant (v->sign);
		gtog (v, FAC);
	}

/* Otherwise, convert the inverse to FFT-ready form */

	else {
		gianttogw (&gwdata, v, b);
	}

/* Use the faster GMP library to do an extended GCD which gives us 1/v mod N */

#else
	{
	mpz_t	__v, __N, __gcd, __inv;

/* Do the extended GCD */

	mpz_init (__v);
	mpz_init (__N);
	mpz_init (__gcd);
	mpz_init (__inv);
	gtompz (v, __v);
	gtompz (N, __N);
	mpz_gcdext (__gcd, __inv, NULL, __v, __N);
	mpz_clear (__v);

/* If a factor was found (gcd != 1 && gcd != N), save it in FAC */

	if (mpz_cmp_ui (__gcd, 1) && mpz_cmp (__gcd, __N)) {
		FAC = allocgiant ((int) mpz_sizeinbase (__gcd, 32));
		mpztog (__gcd, FAC);
	}

/* Otherwise, convert the inverse to FFT-ready form */

	else {
		if (mpz_sgn (__inv) < 0) mpz_add (__inv, __inv, __N);
		mpztog (__inv, v);
		gianttogw (&gwdata, v, b);
	}

/* Cleanup and return */

	mpz_clear (__gcd);
	mpz_clear (__inv);
	mpz_clear (__N);
	}
#endif

/* Clean up */

	pushg (&gwdata.gdata, 1);

/* Increment count and return */

	return (TRUE);
}

/* Takes a point (a,b) and multiplies it by a value such that b will be one */
/* If we find a factor it is returned in FAC.  Function returns FALSE if it */
/* was interrupted. */

int normalize (
	gwnum	a,
	gwnum	b)
{
	giant	g;

/* Compute the modular inverse and scale up the first input value */

	if (!modinv (b)) return (FALSE);
	if (FAC != NULL) return (TRUE);
	gwmul (&gwdata, b, a);

/* Now make sure value is less than N */

	g = popg (&gwdata.gdata, ((unsigned long) gwdata.bit_length >> 5) + 5);
	gwtogiant (&gwdata, a, g);
	modg (N, g);
	gianttogw (&gwdata, g, a);
	pushg (&gwdata.gdata, 1);

/* All done */

	return (TRUE);
}


/**************************************************************
 *
 *	ECM Function for 32-bit inputs...
 *
 **************************************************************/

/* Do ECM stage 1 for GMP-ECM using gwnum library.  See gwnum.h for */
/* a detailed explanation of inputs and outputs. */

int gwnum_ecmStage1_u32 (
	double	k,			/* K in K*B^N+C */
	unsigned long b,		/* B in K*B^N+C */
	unsigned long n,		/* N in K*B^N+C */
	signed long c,			/* C in K*B^N+C */
	uint32_t *num_being_factored_array, /* Number to factor */
	unsigned long num_being_factored_array_len,
	uint64_t B1,			/* Stage 1 bound */
	uint64_t *B1_done,		/* Stage 1 that is already done */
	uint32_t *A_array,		/* A - caller derives it from sigma */
	unsigned long A_array_len,
	uint32_t *x_array,		/* X value of point */
	unsigned long *x_array_len,
	uint32_t *z_array,		/* Z value of point */
	unsigned long *z_array_len,
	int	(*stop_check_proc)(int),/* Ptr to proc that returns TRUE */
					/* if user interrupts processing */
	unsigned long options)
{
	unsigned long bits, SQRT_B1;
	uint64_t prime;
	int	res;
	long	reslong;
	gwnum	x, z;

/* Calculate an upper bound on the number of bits in the numbers we will be */
/* FFTing.  Note: We allocate 60 extra bits to handle any possible k value. */

	if (b) 
		bits = (unsigned long) (n * log ((double) b) / log ((double) 2.0)) + 60;
	else
		bits = num_being_factored_array_len * 8 * sizeof (uint32_t);

/* Setup the assembly code */

	guessCpuType ();
	gwinit (&gwdata);
	if (b)
		res = gwsetup (&gwdata, k, b, n, c);
	else if (sizeof (unsigned long) == sizeof (uint32_t))
		res = gwsetup_general_mod (&gwdata,
					   (uint32_t *) num_being_factored_array,
					   num_being_factored_array_len);
	else
		res = gwsetup_general_mod (&gwdata,
					   (uint32_t *) num_being_factored_array,
					   num_being_factored_array_len * 2);
	if (res == GWERROR_MALLOC) return (ES1_MEMORY);
	if (res) return (ES1_CANNOT_DO_IT);
	StopCheckRoutine = stop_check_proc;

/* If we cannot handle this very efficiently, let caller know it */

	if (gwdata.GENERAL_MOD && ! (options & ES1_DO_SLOW_CASE)) {
		ecm_cleanup ();
		return (ES1_CANNOT_DO_QUICKLY);
	}

/* Allocate memory */

	Ad4 = gwalloc (&gwdata);
	if (Ad4 == NULL) goto no_mem;
	x = gwalloc (&gwdata);
	if (x == NULL) goto no_mem;
	z = gwalloc (&gwdata);
	if (z == NULL) goto no_mem;

/* Turn the input number we are factoring into a giant.  Either use the */
/* number we were passed in or calculate k*b^n+c */

	N = allocgiant ((bits >> 5) + 1);
	if (N == NULL) goto no_mem;
	if (num_being_factored_array != NULL && num_being_factored_array_len) {
		giantstruct tmp;
		tmp.sign = num_being_factored_array_len;
		tmp.n = (uint32_t *) num_being_factored_array;
		while (tmp.sign && tmp.n[tmp.sign-1] == 0) tmp.sign--;
		gtog (&tmp, N);
	} else {
		ultog (b, N);
		power (N, n);
		dblmulg (k, N);
		iaddg (c, N);
	}

/* Convert the input A value to a gwnum.  For extra speed we precompute */
/* A * 4 and FFT that value. */

	binarytogw (&gwdata, A_array, A_array_len, Ad4);
	gwaddsmall (&gwdata, Ad4, 2);	/* Compute A+2 */
	modinv (Ad4);
	if (FAC != NULL) goto bingo;

	dbltogw (&gwdata, 4.0, x);	/* For extra speed, precompute 4 / (A+2) */
	gwmul (&gwdata, x, Ad4);
	gwfft (&gwdata, Ad4, Ad4);	/* Even more speed, save FFT of Ad4 */

/* Convert the input x value to a gwnum */

	binarytogw (&gwdata, x_array, *x_array_len, x);

/* Convert the input z value to a gwnum.  If the input z value was not */
/* given, then assume z is one. */

	if (z_array != NULL && z_array_len != NULL && *z_array_len)
		binarytogw (&gwdata, z_array, *z_array_len, z);
	else
		dbltogw (&gwdata, 1.0, z);

/* Set other constants */

	SQRT_B1 = (unsigned long) sqrt ((double) B1);

/* Output a startup message */

//	{
//		char	fft_desc[100];
//		gwfft_description (fft_desc);
//		sprintf (buf, "Using %s\n", fft_desc);
//		OutputStr (buf);
//	}

/* Do ECM stage 1 */

	start_sieve (B1_done != NULL ? *B1_done + 1 : 2);
	for ( ; ; ) {
		prime = sieve ();
		if (prime > B1) break;

/* Apply as many powers of prime as long as prime^n <= B */
/* MEMUSED: 3 gwnums (x, z, AD4) + 10 for ell_mul */

		ell_mul (x, z, prime);
		if (prime <= SQRT_B1) {
			uint64_t mult, max;
			mult = prime;
			max = B1 / prime;
			for ( ; ; ) {
				ell_mul (x, z, prime);
				mult *= prime;
				if (mult > max) break;
			}
		}

/* Check for errors */

		if (gw_test_for_error (&gwdata)) goto error;

/* Check for interrupt.  If one occurs return normalized x OR x,z pair. */

		if (stop_check_proc != NULL && (*stop_check_proc)(0)) {
			if (B1_done != NULL)
				*B1_done = prime;

			if (z_array == NULL) {
				StopCheckRoutine = NULL;
				normalize (x, z);
				if (FAC != NULL) goto bingo;
				reslong = gwtobinary (&gwdata, x, x_array, (bits >> 5) + 1);
				if (reslong < 0) goto error;
				*x_array_len = reslong;
			}

			else {
				reslong = gwtobinary (&gwdata, x, x_array, (bits >> 5) + 1);
				if (reslong < 0) goto error;
				*x_array_len = reslong;

				reslong = gwtobinary (&gwdata, z, z_array, (bits >> 5) + 1);
				if (reslong < 0) goto error;
				*z_array_len = reslong;
			}

			ecm_cleanup ();
			return (ES1_INTERRUPT);
		}
	}
	*B1_done = B1;

/* Normalize the x value OR return the x,z pair */

	if (z_array == NULL) {
		StopCheckRoutine = NULL;
		normalize (x, z);
		if (FAC != NULL) goto bingo;
		reslong = gwtobinary (&gwdata, x, x_array, (bits >> 5) + 1);
		if (reslong < 0) goto error;
		*x_array_len = reslong;
	} else {
		reslong = gwtobinary (&gwdata, x, x_array, (bits >> 5) + 1);
		if (reslong < 0) goto error;
		*x_array_len = reslong;

		reslong = gwtobinary (&gwdata, z, z_array, (bits >> 5) + 1);
		if (reslong < 0) goto error;
		*z_array_len = reslong;
	}

/* Free memory and return */

	ecm_cleanup ();
	return (ES1_SUCCESS);

/* Print a message if we found a factor! */

bingo:	//printf ("ECM found a factor\n");
	if (!testFactor (FAC)) goto error;
	gianttogw (&gwdata, FAC, x);
	reslong = gwtobinary (&gwdata, x, x_array, (bits >> 5) + 1);
	if (reslong < 0) goto error;
	*x_array_len = reslong;
	if (z_array != NULL) {
		z_array[0] = 1;
		*z_array_len = 1;
	}
	return (ES1_FACTOR_FOUND);

/* Return a hardware error occurred code */

error:	ecm_cleanup ();
	return (ES1_HARDWARE_ERROR);

/* Return out-of-memory error */

no_mem:	ecm_cleanup ();
	return (ES1_MEMORY);
}

/**************************************************************
 *
 *	ECM Function for 64-bit inputs...
 *
 **************************************************************/

/* Do ECM stage 1 for GMP-ECM using gwnum library.  See gwnum.h for */
/* a detailed explanation of inputs and outputs. */

int gwnum_ecmStage1_u64 (
	double	k,			/* K in K*B^N+C */
	unsigned long b,		/* B in K*B^N+C */
	unsigned long n,		/* N in K*B^N+C */
	signed long c,			/* C in K*B^N+C */
	uint64_t *num_being_factored_array, /* Number to factor */
	unsigned long num_being_factored_array_len,
	uint64_t B1,			/* Stage 1 bound */
	uint64_t *B1_done,		/* Stage 1 that is already done */
	uint64_t *A_array,	/* A - caller derives it from sigma */
	unsigned long A_array_len,
	uint64_t *x_array,	/* X value of point */
	unsigned long *x_array_len,
	uint64_t *z_array,	/* Z value of point */
	unsigned long *z_array_len,
	int	(*stop_check_proc)(int),/* Ptr to proc that returns TRUE */
					/* if user interrupts processing */
	unsigned long options)
{
	unsigned long bits, SQRT_B1;
	uint64_t prime;
	int	res;
	long	reslong;
	gwnum	x, z;

/* Calculate an upper bound on the number of bits in the numbers we will be */
/* FFTing.  Note: We allocate 60 extra bits to handle any possible k value. */

	if (b) 
		bits = (unsigned long) (n * log ((double) b) / log ((double) 2.0)) + 60;
	else
		bits = num_being_factored_array_len * 8 * sizeof (uint64_t);

/* Setup the assembly code */

	guessCpuType ();
	gwinit (&gwdata);

	if (b)
		res = gwsetup (&gwdata, k, b, n, c);
	else
		res = gwsetup_general_mod_64 (&gwdata,
					      (uint64_t *) num_being_factored_array,
					      num_being_factored_array_len);

	if (res == GWERROR_MALLOC) return (ES1_MEMORY);
	if (res) return (ES1_CANNOT_DO_IT);
	StopCheckRoutine = stop_check_proc;

/* If we cannot handle this very efficiently, let caller know it */

	if (gwdata.GENERAL_MOD && ! (options & ES1_DO_SLOW_CASE)) {
		ecm_cleanup ();
		return (ES1_CANNOT_DO_QUICKLY);
	}

/* Allocate memory */

	Ad4 = gwalloc (&gwdata);
	if (Ad4 == NULL) goto no_mem;
	x = gwalloc (&gwdata);
	if (x == NULL) goto no_mem;
	z = gwalloc (&gwdata);
	if (z == NULL) goto no_mem;

/* Turn the input number we are factoring into a giant.  Either use the */
/* number we were passed in or calculate k*b^n+c */

	N = allocgiant ((bits >> 5) + 1);
	if (N == NULL) goto no_mem;
	if (num_being_factored_array != NULL && num_being_factored_array_len) {
		int	i;
		for (i = 0; i < (int) (num_being_factored_array_len * 2); i += 2) {
			N->n[i] = (uint32_t) num_being_factored_array[i/2];  /* bottom half of the 64-bit value */
			N->n[i+1] = (uint32_t) (num_being_factored_array[i/2] >> 32); /* top half of the 64-bit value */
		}
		N->sign = num_being_factored_array_len * 2;
		while (N->sign && N->n[N->sign-1] == 0) N->sign--;
	} else {
		ultog (b, N);
		power (N, n);
		dblmulg (k, N);
		iaddg (c, N);
	}

/* Convert the input A value to a gwnum.  For extra speed we precompute */
/* A * 4 and FFT that value. */

	binary64togw (&gwdata, A_array, A_array_len, Ad4);
	gwaddsmall (&gwdata, Ad4, 2);	/* Compute A+2 */
	modinv (Ad4);
	if (FAC != NULL) goto bingo;

	dbltogw (&gwdata, 4.0, x);	/* For extra speed, precompute 4 / (A+2) */
	gwmul (&gwdata, x, Ad4);
	gwfft (&gwdata, Ad4, Ad4);	/* Even more speed, save FFT of Ad4 */

/* Convert the input x value to a gwnum */

	binary64togw (&gwdata, x_array, *x_array_len, x);

/* Convert the input z value to a gwnum.  If the input z value was not */
/* given, then assume z is one. */

	if (z_array != NULL && z_array_len != NULL && *z_array_len)
		binary64togw (&gwdata, z_array, *z_array_len, z);
	else
		dbltogw (&gwdata, 1.0, z);

/* Set other constants */

	SQRT_B1 = (unsigned long) sqrt ((double) B1);

/* Output a startup message */

//	{
//		char	fft_desc[100];
//		gwfft_description (fft_desc);
//		sprintf (buf, "Using %s\n", fft_desc);
//		OutputStr (buf);
//	}

/* Do ECM stage 1 */

	start_sieve (B1_done != NULL ? *B1_done + 1 : 2);
	for ( ; ; ) {
		prime = sieve ();
		if (prime > B1) break;

/* Apply as many powers of prime as long as prime^n <= B */
/* MEMUSED: 3 gwnums (x, z, AD4) + 10 for ell_mul */

		ell_mul (x, z, prime);
		if (prime <= SQRT_B1) {
			uint64_t mult, max;
			mult = prime;
			max = B1 / prime;
			for ( ; ; ) {
				ell_mul (x, z, prime);
				mult *= prime;
				if (mult > max) break;
			}
		}

/* Check for errors */

		if (gw_test_for_error (&gwdata)) goto error;

/* Check for interrupt.  If one occurs return normalized x OR x,z pair. */

		if (stop_check_proc != NULL && (*stop_check_proc)(0)) {
			if (B1_done != NULL)
				*B1_done = prime;

			if (z_array == NULL) {
				StopCheckRoutine = NULL;
				normalize (x, z);
				if (FAC != NULL) goto bingo;
				reslong = gwtobinary64 (&gwdata, x, x_array, (bits >> 6) + 1);
				if (reslong < 0) goto error;
				*x_array_len = reslong;
			}

			else {
				reslong = gwtobinary64 (&gwdata, x, x_array, (bits >> 6) + 1);
				if (reslong < 0) goto error;
				*x_array_len = reslong;

				reslong = gwtobinary64 (&gwdata, z, z_array, (bits >> 6) + 1);
				if (reslong < 0) goto error;
				*z_array_len = reslong;
			}

			ecm_cleanup ();
			return (ES1_INTERRUPT);
		}
	}
	*B1_done = B1;

/* Normalize the x value OR return the x,z pair */

	if (z_array == NULL) {
		StopCheckRoutine = NULL;
		normalize (x, z);
		if (FAC != NULL) goto bingo;
		reslong = gwtobinary64 (&gwdata, x, x_array, (bits >> 6) + 1);
		if (reslong < 0) goto error;
		*x_array_len = reslong;
	} else {
		reslong = gwtobinary64 (&gwdata, x, x_array, (bits >> 6) + 1);
		if (reslong < 0) goto error;
		*x_array_len = reslong;

		reslong = gwtobinary64 (&gwdata, z, z_array, (bits >> 6) + 1);
		if (reslong < 0) goto error;
		*z_array_len = reslong;
	}

/* Free memory and return */

	ecm_cleanup ();
	return (ES1_SUCCESS);

/* Print a message if we found a factor! */

bingo:	//printf ("ECM found a factor\n");
	if (!testFactor (FAC)) goto error;
	gianttogw (&gwdata, FAC, x);
	reslong = gwtobinary64 (&gwdata, x, x_array, (bits >> 6) + 1);
	if (reslong < 0) goto error;
	*x_array_len = reslong;
	if (z_array != NULL) {
		z_array[0] = 1;
		*z_array_len = 1;
	}
	return (ES1_FACTOR_FOUND);

/* Return a hardware error occurred code */

error:	ecm_cleanup ();
	return (ES1_HARDWARE_ERROR);

/* Return out-of-memory error */

no_mem:	ecm_cleanup ();
	return (ES1_MEMORY);
}
