/**************************************************************
 *
 *  giants.c
 *
 *  Library for large-integer arithmetic.
 * 
 *  Massive rewrite by G. Woltman for 32-bit support
 *
 *  c. 1997,1998 Perfectly Scientific, Inc.
 *  c. 1998-2023 Mersenne Research, Inc.
 *  All Rights Reserved.
 *
 **************************************************************/

/* Include Files */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include "giants.h"
#include "gwutil.h"
#include "fftsg.c"

/**************************************************************
 *
 * Preprocessor definitions
 *
 **************************************************************/

#define TWOPI (double)(2*3.1415926535897932384626433)
#define SQRT2 (double)(1.414213562373095048801688724209)
#define SQRTHALF (double)(0.707106781186547524400844362104)
#define TWO_TO_MINUS_19 (double)(0.0000019073486328125)
#define ONE_MINUS_TWO_TO_MINUS_19 (double)(0.9999980926513671875)

/* Next, number of words at which Karatsuba becomes better than GRAMMAR. */
#define KARAT_BREAK_SQUARE	36
#define KARAT_BREAK_MULT	39

/* Next, mumber of words at which FFT becomes better than Karatsuba. */
#define FFT_BREAK_SQUARE	47
#define FFT_BREAK_MULT1		40
#define FFT_BREAK_MULT2		100
#define FFT_BREAK_MULT3		99

/* The limit (in 32-bit words) below which hgcd is too ponderous */
#define GCDLIMIT 150

/* Size by which to increment the stack used in pushg() and popg(). */
#define	STACK_GROW	100

/* Size at which a brute force hgcd is done */
#define CHGCD_BREAK	24

typedef struct gmatrixstruct {
	 giant	ul;			/* upper left */
	 giant	ur;			/* upper right */
	 giant	ll;			/* lower left */
	 giant	lr;			/* lower right */
} gmatrixstruct;
typedef gmatrixstruct *gmatrix;

/* Routines to help manage pushing the right number of temporaries */
/* back on the stack.  Especially handy for routines where user can */
/* hit escape in the middle of the computation. */

#define stackstart(g)	(g)->num_popgs
#define pushall(g,x)	pushg(g,(g)->num_popgs - x)


/* Global variables */

int	mulmode = AUTO_MUL;

/* Private function prototypes. */

/* r becomes the steady-state reciprocal 2^(2b)/d, where
 * b = bit-length of d-1. */
void	make_recip(ghandle *, giant d, giant r);

/* n := [n/d], d positive, using stored reciprocal directly. */
void	divg_via_recip(ghandle *, giant d, giant r, giant n);

void		normal_addg(giant, giant);
void		normal_subg(giant, giant);
void		reverse_subg(giant, giant, int);
int 		automulg(ghandle *, giant a, giant b);
void 		grammarmulg(ghandle *, giant a, giant b);
void		grammarsquareg(ghandle *, giant b);
void 		karatmulg(ghandle *, giant a, giant b);
void 		karatsquareg(ghandle *, giant b);

int		init_sincos(ghandle *,int);
int 		lpt(int);
void 		addsignal(giant, int, double *, int);
int 		FFTsquareg(ghandle *, giant x);
int 		FFTmulg(ghandle *, giant y, giant x);
void 		giant_to_double(giant x, int sizex, double *z, int L);

#define gswap(p,q)  {giant tgq; tgq = *(p); *(p) = *(q); *(q) = tgq;}
#define ulswap(p,q)  {unsigned long tgq; tgq = p; p = q; q = tgq;}

int		invg_common (ghandle *, giant, giant, int);
int		gcdg_common (ghandle *, giant, giant, int);
int	 	cextgcdg (ghandle *, giant *, giant *, gmatrix A, int);
int		ggcd (ghandle *, giant *, giant *, gmatrix, int);
void 		onestep (ghandle *, giant *, giant *, gmatrix);
int 		mulvM (ghandle *, gmatrix, giant, giant);
int 		mulmM (ghandle *, gmatrix, gmatrix);
int 		mulmMsp (ghandle *, gmatrix, gmatrix, int);
void		punch (ghandle *, giant, gmatrix);
int		hgcd (ghandle *, int, giant *, giant *, gmatrix, int);
int		rhgcd (ghandle *, giant *, giant *, gmatrix, int);

#define sintstackg(i,g) if(i<0){i=-i;g.sign=-1;}else g.sign=1;g.n=(uint32_t*)&i;setmaxsize(&g,1);
#define uintstackg(i,g) g.sign=1;g.n=(uint32_t*)&i;setmaxsize(&g,1);
#define ullstackg(i,g) g.n=(uint32_t*)&i;g.sign=(g.n[1]?2:1);setmaxsize(&g,2);

/* Assembly routines */

int gcdhlp (uint32_t, uint32_t *, uint32_t, uint32_t *, void *);

/* External routines */

int (*StopCheckRoutine)(int) = NULL;
#define stopCheck(t)	((*StopCheckRoutine)(t-0x80000000))

/**************************************************************
 *
 *	Functions
 *
 **************************************************************/

giant allocgiant (		/* Create a new giant */
	int 	count)
{
	int 	size;
	giant 	thegiant;

	ASSERTG (count > 0);

	size = sizeof (giantstruct) + count * sizeof (uint32_t);
	thegiant = (giant) malloc (size);
	thegiant->sign = 0;
	thegiant->n = (uint32_t *) ((char *) thegiant + sizeof (giantstruct));
	setmaxsize (thegiant, count);
	return (thegiant);
}

void itog (		/* The giant g becomes set to the integer value i. */
	int32_t	i,
	giant	g)
{
	if (i > 0) {
		g->sign = 1;
		g->n[0] = i;
	} else if (i == 0) {
		g->sign = 0;
		g->n[0] = 0;
	} else {
		g->sign = -1;
		g->n[0] = -i;
	}
}

void ultog (		/* The giant g becomes set to the integer value i. */
	uint32_t i,
	giant	g)
{
	if (i == 0) {
		g->sign = 0;
		g->n[0] = 0;
	} else {
		g->sign = 1;
		g->n[0] = i;
	}
}

void slltog (		/* The giant g becomes set to the integer value i. */
	int64_t i,
	giant	g)
{
	if (i >= 0) {
		ulltog (i, g);
	} else {
		ulltog (-i, g);
		g->sign = -g->sign;
	}
}

void ulltog (		/* The giant g becomes set to the integer value i. */
	uint64_t i,
	giant	g)
{
	if (i == 0) {
		g->sign = 0;
		g->n[0] = 0;
		return;
	}
	g->n[0] = (uint32_t) i;
	g->n[1] = (uint32_t) (i >> 32);
	g->sign = g->n[1] ? 2 : 1;
}

void dbltog (		/* The giant g becomes set to the double i. */
	double	i,
	giant	g)
{
	if (i < 0.0) {
		dbltog (-i, g);
		negg (g);
	} else if (i < 4294967296.0) {
		ultog ((uint32_t) i, g);
	} else {
		g->sign = 2;
		g->n[1] = (uint32_t) (i / 4294967296.0);
		g->n[0] = (uint32_t) (i - g->n[1] * 4294967296.0);
	}
}

void ctog (		/* The giant g is set to string s. */
	const char *s,
	giant	g)
{
	uint32_t multiplier = 1;
	uint32_t addin = 0;
	for (g->sign = 0; isdigit (*s); s++) {
		multiplier = multiplier * 10;
		addin = addin * 10 + (*s - '0');
		if (multiplier == 1000000000) {
			ulmulg (multiplier, g);
			uladdg (addin, g);
			multiplier = 1;
			addin = 0;
		}
	}
	if (multiplier != 1) {
		ulmulg (multiplier, g);
		uladdg (addin, g);
	}
}

void gtoc (		/* The giant g is converted to string s. */
	giant	g,
	char	*s,
	int	sbufsize)
{
	ghandle gdata;
	giant	x, y, ten;
	int	i, len;
	char	c;

	ASSERTG (g->sign >= 0);

	init_ghandle (&gdata);

	x = popg (&gdata, g->sign); gtog (g, x);
	y = popg (&gdata, g->sign);
	ten = popg (&gdata, 1); itog (10, ten);
	sbufsize--;
	for (len = 0; len < sbufsize && x->sign; len++) {
		gtog (x, y);
		modgi (&gdata, ten, y);
		s[len] = (char) (y->n[0] + '0');
		divgi (&gdata, ten, x);
	}
	for (i = 0; i < len / 2; i++) {
		c = s[i];
		s[i] = s[len-1-i];
		s[len-1-i] = c;
	}
	s[len] = 0;
	pushg (&gdata, 3);

	term_ghandle (&gdata);
}

void gtog (			/* destgiant becomes equal to srcgiant. */
	giant	srcgiant,
	giant	destgiant)
{
	destgiant->sign = srcgiant->sign;
	memmove (destgiant->n, srcgiant->n, abs (srcgiant->sign) * sizeof (uint32_t));
	ASSERTG (abs (destgiant->sign) <= destgiant->maxsize);
}

int gcompg (		/* Returns -1,0,1 if a<b, a=b, a>b, respectively. */
	giant	a,
	giant	b)
{
	int	sa = a->sign, j, sb = b->sign, sgn;
	uint32_t va, vb;

	if (sa > sb)
		return (1);
	if (sa < sb)
		return (-1);
	if (sa < 0) {
		sa = -sa; /* Take absolute value of sa. */
		sgn = -1;
	} else {
		sgn = 1;
	}
	for (j = sa-1; j >= 0; j--) {
		va = a->n[j];
		vb = b->n[j];
		if (va > vb) return (sgn);
		if (va < vb) return (-sgn);
	}
	return (0);
}

/* New add/subtract routines.
	The basic subtract "subg()" uses the following logic table:

     a      b          if (b > a)          if (a > b)
     
     +      +          b := b - a          b := -(a - b)
     -      +          b := b + (-a)       N.A.
     +      -          N.A.                b := -((-b) + a)
     -      -          b := (-a) - (-b)    b := -((-b) - (-a))

   The basic addition routine "addg()" uses:

     a      b          if(b > -a)          if(-a > b)
     
     +      +          b := b + a          N.A. 
     -      +          b := b - (-a)       b := -((-a) - b)
     +      -          b := a - (-b)       b := -((-b) - a)
     -      -          N.A.                b := -((-b) + (-a))

   In this way, internal routines "normal_addg," "normal_subg," 
	and "reverse_subg;" each of which assumes non-negative
	operands and a non-negative result, are now used for greater
	efficiency.
*/

void normal_addg (	/* b := a + b, both a,b assumed non-negative. */
	giant	a,
	giant	b)
{
	int 	j, asize = a->sign, bsize = b->sign;
	uint32_t *aptr = a->n, *bptr = b->n;
	uint32_t res, carry;

	ASSERTG (asize >= 0 && bsize >= 0);
	ASSERTG (a->sign == 0 || a->n[abs(a->sign)-1] != 0);
	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);

	res = carry = 0;
	for (j = 0; j < asize || res; j++) {
		if (j < asize) addhlp (&res, &carry, *aptr++);
		if (j < bsize) addhlp (&res, &carry, *bptr);
		*bptr++ = res;
		res = carry;
		carry = 0;
	}
	if (j > bsize) b->sign = j;

	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);
}

void normal_subg (	/* b := b - a; assumes b, a positive and b >= a. */
	giant	a,
	giant	b)
{
	int 	j, asize = a->sign, bsize = b->sign;
	uint32_t *aptr = a->n, *bptr = b->n;
	uint32_t res, carry;

	ASSERTG (asize >= 0 && bsize >= asize);

	res = carry = 0;
	for (j = 0; j < asize; j++) {
		addhlp (&res, &carry, *bptr);
		subhlp (&res, &carry, *aptr++);
		*bptr++ = res;
		res = carry;
		carry = (int) carry >> 4;	/* Maintain sign bit */
	}
	for ( ; res; j++)
	{
		if (j < bsize) addhlp (&res, &carry, *bptr);
		*bptr++ = res;
		res = carry;
		carry = (int) carry >> 4;	/* Maintain sign bit */
	}
	if (j < bsize) return;

	while (j > 0 && b->n[j-1] == 0) j--;
	b->sign = j;

	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);
}

void reverse_subg (	/* b := a - b; assumes b, a positive and a >= b. */
	giant	a,
	giant	b,
	int	azeros)
{
	int 	j, asize = a->sign + azeros, bsize = b->sign;
	uint32_t *aptr = a->n, *bptr = b->n;
	uint32_t res, carry;

	ASSERTG (bsize >= 0 && asize >= bsize);

	res = carry = 0;
	for (j = 0; j < asize; j++) {
		if (azeros)
			azeros--;
		else
			addhlp (&res, &carry, *aptr++);
		if (j < bsize)
			subhlp (&res, &carry, *bptr);
		*bptr++ = res;
		res = carry;
		carry = (int) carry >> 4;	/* Maintain sign bit */
	}

	while (j > 0 && b->n[j-1] == 0) j--;
	b->sign = j;

	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);
}

void addg (		/* b := b + a, any signs any result. */
	giant	a,
	giant	b)
{
	int 	asgn = a->sign, bsgn = b->sign;

	ASSERTG (a->sign == 0 || a->n[abs(a->sign)-1] != 0);
	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);

	if (asgn == 0) return;
	if (bsgn == 0) {
		gtog (a, b);
		return;
	}
	if ((asgn < 0) == (bsgn < 0)) {
		if (bsgn > 0) {
			normal_addg (a, b);
		} else {
			absg (b);
			if (a != b) absg (a);
			normal_addg (a, b);
			negg (b);
			if (a != b) negg (a);
		}
	} else if (bsgn > 0) {
		negg (a);
		if (gcompg (b, a) >= 0) {
			normal_subg (a, b);
			negg (a);
		} else {
			reverse_subg (a, b, 0);
			negg (a);
			negg (b);
		}
	} else {
		negg (b);
		if (gcompg(b,a) < 0) {
			reverse_subg (a, b, 0);
		} else {
			normal_subg (a, b);
			negg (b);
		}
	}

	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);
	ASSERTG (abs (b->sign) <= b->maxsize);
}

void sladdg (			/* Giant g becomes g + i. */
	int32_t i,
	giant	g)
{
	giantstruct tmp;

	if (i == 0) return;

	sintstackg (i, tmp);
	addg (&tmp, g);
}

void uladdg (			/* Giant g becomes g + i. */
	uint32_t i,
	giant	g)
{
	giantstruct tmp;

	if (i == 0) return;

	uintstackg (i, tmp);
	addg (&tmp, g);
}

void ulsubg (			/* Giant g becomes g - i. */
	uint32_t i,
	giant	g)
{
	giantstruct tmp;

	if (i == 0) return;

	uintstackg (i, tmp);
	subg (&tmp, g);
}

void subg (		/* b := b - a, any signs, any result. */
	giant	a,
	giant	b)
{
	int	asgn = a->sign, bsgn = b->sign;

	ASSERTG (a->sign == 0 || a->n[abs(a->sign)-1] != 0);
	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);

	if (asgn == 0) return;
	if (bsgn == 0) {
		gtog (a, b);
		negg (b);
		return;
	}
	if ((asgn < 0) != (bsgn < 0)) {
		if (bsgn > 0) {
			negg (a);
			normal_addg (a, b);
			negg (a);
		} else {
			negg (b);
			normal_addg (a, b);
			negg (b);
		}
	} else if (bsgn > 0) {
		if (gcompg (b, a) >= 0) {
			normal_subg (a, b);
		} else {
			reverse_subg (a, b, 0);
			negg (b);
		}
	} else {
		negg (a);
		negg (b);
		if (gcompg (b, a) >= 0) {
			normal_subg (a, b);
			negg (a);
			negg (b);
		} else {
			reverse_subg (a, b, 0);
			negg (a);
		}
	}

	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);
	ASSERTG (abs (b->sign) <= b->maxsize);
}

void setmulmode (
	int 	mode)
{
	mulmode = mode;
}

/* Optimized general square, b becomes b*b. Modes are:
 * AUTO_MUL: switch according to empirical speed criteria.
 * GRAMMAR_MUL: force grammar-school algorithm.
 * KARAT_MUL: force Karatsuba divide-conquer method.
 * FFT_MUL: force floating point FFT method. */

int squareg (			/* b becomes b*b */
	giant	b)
{
	ghandle gdata;
	int	stop_reason;

	init_ghandle (&gdata);
	stop_reason = squaregi (&gdata, b);
	term_ghandle (&gdata);
	return (stop_reason);
}

int squaregi (			/* b becomes b*b */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	b)
{
	int 	bsize, stop_reason;

	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);

	if (b->sign == 0) return (0);

	absg (b);
	switch (mulmode) {
	case GRAMMAR_MUL:
		grammarsquareg (gdata, b);
		break;
	case FFT_MUL:
		FFTsquareg (gdata, b);
		break;
	case KARAT_MUL:
		karatsquareg (gdata, b);
		break;
	case AUTO_MUL:
		bsize = b->sign;
		if (bsize >= FFT_BREAK_SQUARE) {
			stop_reason = FFTsquareg (gdata, b);
			if (stop_reason) return (stop_reason);
		}
		else if (bsize >= KARAT_BREAK_SQUARE)
			karatsquareg (gdata, b);
		else
			grammarsquareg (gdata, b);
		break;
	}

	ASSERTG (b->sign > 0 && b->n[b->sign-1] != 0);
	return (0);
}

/* Optimized general multiply, b becomes a*b. Modes are:
 * AUTO_MUL: switch according to empirical speed criteria.
 * GRAMMAR_MUL: force grammar-school algorithm.
 * KARAT_MUL: force Karatsuba divide-conquer method.
 * FFT_MUL: force floating point FFT method. */

int mulg (			/* b becomes a*b */
	giant	a,
	giant	b)
{
	ghandle gdata;
	int	stop_reason;

	init_ghandle (&gdata);
	stop_reason = mulgi (&gdata, a, b);
	term_ghandle (&gdata);
	return (stop_reason);
}

int mulgi (			/* b becomes a*b */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	a,
	giant	b)
/* Optimized general multiply, b becomes a*b. Modes are:
 * AUTO_MUL: switch according to empirical speed criteria.
 * GRAMMAR_MUL: force grammar-school algorithm.
 * KARAT_MUL: force Karatsuba divide-conquer method.
 * FFT_MUL: force floating point FFT method. */
{
	int 	neg, asign, stop_reason;

	ASSERTG (a->sign == 0 || a->n[abs(a->sign)-1] != 0);
	ASSERTG (b->sign == 0 || b->n[abs(b->sign)-1] != 0);

	if (a == b) {
		return (squaregi (gdata, b));
	}

	if (a->sign == 0 || b->sign == 0) {
		b->sign = 0;
		return (0);
	}

	neg = 0;
	asign = a->sign;
	if (a->sign < 0) { neg = 1; a->sign = -a->sign; }
	if (b->sign < 0) { neg = !neg; b->sign = -b->sign; }

	switch (mulmode) {
	case GRAMMAR_MUL:
		grammarmulg (gdata, a, b);
		break;
	case FFT_MUL:
		FFTmulg (gdata, a, b);
		break;
	case KARAT_MUL:
		karatmulg (gdata, a, b);
		break;
	case AUTO_MUL:
		stop_reason = automulg (gdata, a, b);
		if (stop_reason) {
			if (asign < 0) a->sign = -a->sign;	/* Restore a's sign */
			return (stop_reason);
		}
		break;
	}

	if (asign < 0) a->sign = -a->sign;	/* Restore a's sign */
	if (neg) b->sign = -b->sign;
	ASSERTG (b->sign != 0 && b->n[abs(b->sign)-1] != 0);
	ASSERTG (abs (b->sign) <= b->maxsize);
	return (0);
}

void ulmulg (			/* Giant g becomes g * i. */
	uint32_t i,
	giant	g)
{
	giantstruct tmp;

	if (i == 0) {g->sign = 0; return;}
	if (i == 1) return;

	uintstackg (i, tmp);
	mulg (&tmp, g);
}

void imulg (			/* Giant g becomes g * i. */
	int32_t i,
	giant	g)
{
	giantstruct tmp;

	if (i == 0) {g->sign = 0; return;}
	if (i == 1) return;

	sintstackg (i, tmp);
	mulg (&tmp, g);
}

/* This only works on Intel architecture.  That's OK because gwnum */
/* library has tons of Intel assembly language code. */

void ullmulg (			/* Giant g becomes g * i. */
	uint64_t i,
	giant	g)
{
	giantstruct tmp;

	if (i == 0) {g->sign = 0; return;}
	if (i == 1) return;

	ullstackg (i, tmp);
	mulg (&tmp, g);
}

void dblmulg (			/* Giant g becomes g * i. */
	double	i,
	giant	g)
{
	stackgiant(tmp,2);

	dbltog (i, tmp);
	mulg (tmp, g);
}

void modg (		/* n becomes n%d. n is arbitrary, but the
			 * denominator d must be positive! returned
			 * n will always be positive. */
	giant 	d,
	giant 	n)
{
	ghandle gdata;

	init_ghandle (&gdata);
	modgi (&gdata, d, n);
	term_ghandle (&gdata);
}

void modgi (		/* n becomes n%d. n is arbitrary, but the
			 * denominator d must be positive! returned
			 * n will always be positive. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	d,
	giant 	n)
{
	giant	tmp;

	ASSERTG (d->sign > 0);
	ASSERTG (d->n[d->sign-1] != 0);
	ASSERTG (n->sign == 0 || n->n[abs(n->sign)-1] != 0);

/* Quick check for n already less than d */

	if (n->sign >= 0 && (n->sign < d->sign || (n->sign == d->sign && gcompg (n, d) < 0))) return;

/* Use divg to compute the mod */

	tmp = popg (gdata, abs(n->sign));
	gtog (n, tmp);
	divgi (gdata, d, tmp);
	mulgi (gdata, d, tmp);
	subg (tmp, n);
	if (n->sign < 0) addg (d, n);
	pushg (gdata, 1);

	ASSERTG (n->sign >= 0);
	ASSERTG (n->sign == 0 || n->n[n->sign-1] != 0);
}

void dbldivg (			/* Giant g becomes g / i. */
	double	i,
	giant	g)
{
	stackgiant(tmp,2);

	dbltog (i, tmp);
	divg (tmp, g);
}

void divg (		/* n becomes n/d. n is arbitrary, but the
			 * denominator d must be positive! */
	giant 	d,
	giant 	n)
{
	ghandle gdata;

	init_ghandle (&gdata);
	divgi (&gdata, d, n);
	term_ghandle (&gdata);
}

void divgi (		/* n becomes n/d. n is arbitrary, but the
			 * denominator d must be positive! */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	d,
	giant 	n)
{
	int	nsign, nsize, dsize;
	giant	r;

	ASSERTG (d->sign > 0);
	ASSERTG (d->n[d->sign-1] != 0);
	ASSERTG (n->sign == 0 || n->n[abs(n->sign)-1] != 0);

/* Handle simple cases */

	if (isone (d)) return;
	nsign = n->sign;
	nsize = abs (nsign);
	dsize = d->sign;
	if (nsize < dsize) {
		n->sign = 0;
		return;
	}

/* Handle cases where we can (probably) guess the quotient using the FPU */
/* I say probably because the floating point result is accurate to 53 bits. */
/* If, for example, qflt is exactly 3 then it could really be */
/* 2.999999999999999995 and we need to do a full divide to find out whether */
/* we should return 2 or 3. */
/* This speedup is especially important during GCDs which tend to generate */
/* very small quotients. */

	if (nsize - dsize <= 1) {
		double	dflt, nflt, qflt;
		uint32_t q;

		nflt = n->n[nsize-1];
		if (nsize > 1) nflt = nflt * 4294967296.0 + n->n[nsize-2];
		if (nsize > 2) nflt = nflt * 4294967296.0 + n->n[nsize-3];
		if (nsize > dsize && nsize > 3) nflt *= 4294967296.0;
		dflt = d->n[dsize-1];
		if (dsize > 1) dflt = dflt * 4294967296.0 + d->n[dsize-2];
		if (dsize > 2) dflt = dflt * 4294967296.0 + d->n[dsize-3];

		qflt = nflt / dflt;
		if (qflt < 4294967295.0) {
			q = (uint32_t) qflt;
			qflt = qflt - (double) q;
			if (nsize == 1 ||	/* We have an exact result */
			    (qflt >= TWO_TO_MINUS_19 &&
			     qflt <= ONE_MINUS_TWO_TO_MINUS_19)) {
				n->sign = (q == 0) ? 0 : 1;
				n->n[0] = q;
				if (nsign < 0) negg (n);
				return;
			}
		}
	}

/* The reciprocal code is very inefficient when n is large and d is small. */
/* Compensate for that here by using "schoolboy" division */

	if (nsize > dsize + dsize) {
		giant	tmp;
		int	i, chunks;

		r = popg (gdata, dsize + dsize);
		tmp = popg (gdata, dsize + dsize);

		chunks = nsize / dsize - 1;

/* This code only works on positive n values */
		
		if (nsign < 0) negg (n);

/* Shift n right and do the first chunk */

		n->sign -= chunks * dsize;
		n->n += chunks * dsize;

		gtog (n, r);
		divgi (gdata, d, n);

		gtog (n, tmp);
		mulgi (gdata, d, tmp);
		subg (tmp, r);

/* For each remaining chunk, shift the remainder r left and copy in words */
/* from the input number n. */

		for (i = 0; i < chunks; i++) {
			gshiftleft (dsize << 5, r);
			n->n -= dsize;
			n->sign += dsize;
			memmove (r->n, n->n, dsize * sizeof (uint32_t));
			if (r->sign == 0) {
				r->sign = dsize;
				while (r->sign && r->n[r->sign-1] == 0) r->sign--;
			}

/* Now compute our next chunk of result and copy it (zero padded) */
/* to our output giant n */

			gtog (r, tmp);
			divgi (gdata, d, tmp);
			memset ((char *) n->n, 0, dsize * sizeof (uint32_t));
			memmove (n->n, tmp->n, tmp->sign * sizeof (uint32_t));

/* Now compute the remainder for the next iteration */

			mulgi (gdata, d, tmp);
			subg (tmp, r);
		}

/* Negate n if it was originally negative */

		if (nsign < 0) negg (n);

		pushg (gdata, 2);
	}

/* Divide using reciprocals */

	else if (d->sign < 30) {
		r = popg (gdata, (d->sign << 1) + 1);
		make_recip (gdata, d, r);
		divg_via_recip (gdata, d, r, n);
		pushg (gdata, 1);
	}
	else {
		if (gdata->cur_recip == NULL || gcompg (d, gdata->cur_den)) {
			free (gdata->cur_recip);
			free (gdata->cur_den);
			gdata->cur_recip = allocgiant (d->sign + 1);
			gdata->cur_den = allocgiant (d->sign);
			gtog (d, gdata->cur_den);
			make_recip (gdata, d, gdata->cur_recip);
		}
		divg_via_recip (gdata, d, gdata->cur_recip, n);
	}

	ASSERTG (n->sign == 0 || n->n[abs(n->sign)-1] != 0);
}

void powerg (			/* x becomes x^n, NO mod performed. */
	giant	x,
	giant	n)
{
	ghandle gdata;
	int 	len;
	giant	scratch;

	ASSERTG (x->sign > 0);

	init_ghandle (&gdata);

	scratch = popg (&gdata, x->sign << 1);
	gtog (x, scratch);
	for (len = bitlen (n) - 1; len; len--) {
		squaregi (&gdata, x);
		if (bitval (n, len-1)) mulgi (&gdata, scratch, x);
	}
	pushg (&gdata, 1);

	term_ghandle (&gdata);
}

void power (			/* x becomes x^n. */
	giant	x,
	int 	n)
{
	giantstruct ng;

	ASSERTG (n >= 0);

	if (n == 0) {itog (1, x); return;}
	if (n == 1) return;

	if (istwo (x)) {
		itog (1, x);
		gshiftleft (n, x);
	} else {
		uintstackg (n, ng);
		powerg (x, &ng);
	}
}

void powermodg (		/* x becomes x^n (mod g). */
	giant	x,
	giant	n,
	giant	g)
{
	ghandle gdata;
	int 	len;
	giant	scratch;

	ASSERTG (x->sign > 0);
	ASSERTG (g->sign > 0);

	init_ghandle (&gdata);

	scratch = popg (&gdata, g->sign << 1);
	gtog (x, scratch);
	for (len = bitlen (n) - 1; len; len--) {
		squaregi (&gdata, scratch);
		modgi (&gdata, g, scratch);
		if (bitval (n, len-1)) {
			mulgi (&gdata, x, scratch);
			modgi (&gdata, g, scratch);
		}
	}
	gtog (scratch, x);
	pushg (&gdata, 1);

	term_ghandle (&gdata);
}

void powermod (			/* x becomes x^n (mod g). */
	giant	x,
	int 	n,
	giant 	g)
{
	giantstruct ng;

	ASSERTG (n >= 0);

	if (n == 0) {itog (1, x); return;}
	if (n == 1) {modg (g, x); return;}

	uintstackg (n, ng);
	powermodg (x, &ng, g);
}

int automulg (			/* b becomes a*b */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	a,
	giant	b)
{
	int 	asize, bsize, stop_reason;

	ASSERTG (a->sign >= 0);
	ASSERTG (b->sign >= 0);
	ASSERTG (a->sign == 0 || a->n[a->sign-1] != 0);
	ASSERTG (b->sign == 0 || b->n[b->sign-1] != 0);

/* Use grammar multiply for small arguments */

	asize = a->sign;
	bsize = b->sign;
	if (asize < KARAT_BREAK_MULT || bsize < KARAT_BREAK_MULT) {
		grammarmulg (gdata, a, b);
	}

/* This special case when one argument is more than roughly twice as big */
/* as the other (which happens frequently in ggcd) is not much faster than */
/* one big multiply, but it does use less memory which is important. */

	else if (asize + asize <= bsize + 2) {
		giant	d;

		d = popg (gdata, bsize);	/* d is upper half of b */
		if (d == NULL) return (GIANT_OUT_OF_MEMORY);
		gtogshiftright (asize << 5, b, d);
		b->sign = asize;		/* b is lower half of b */
		while (b->sign && b->n[b->sign-1] == 0) b->sign--;

		stop_reason = automulg (gdata, a, d);	/* Compute a * upper part of b */
		if (stop_reason) goto done;
		if (b->sign) {
			stop_reason = automulg (gdata, a, b);/* Compute a * lower part of b */
			if (stop_reason) goto done;
		} else {
			memset (b->n, 0, asize * sizeof (uint32_t));
			b->sign = asize;
		}

		b->sign -= asize;	/* Trick to add shifted upper */
		b->n += asize;
		normal_addg (d, b);
		b->sign += asize;	/* Undo the trick */
		b->n -= asize;

done:		pushg (gdata, 1);
		return (stop_reason);
	}

/* Do a Karatsuba or FFT multiply */

	else if ((asize < FFT_BREAK_MULT1 && bsize < FFT_BREAK_MULT1) ||
	         (asize >= FFT_BREAK_MULT2 && bsize >= FFT_BREAK_MULT2 &&
	          asize < FFT_BREAK_MULT3 && bsize < FFT_BREAK_MULT3))
		karatmulg (gdata, a, b);
	else {
		stop_reason = FFTmulg (gdata, a, b);
		if (stop_reason) return (stop_reason);
	}

	ASSERTG (b->sign != 0 && b->n[abs(b->sign)-1] != 0);
	return (0);
}

void grammarsquareg (		/* a := a^2. */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	a)
{
	int	i, asize = a->sign, max = asize * 2 - 1;
	uint32_t *ptr, *ptr1, *ptr2;
	giant	scratch;
	uint32_t res, carryl, carryh;

	ASSERTG (a->sign >= 0);
	ASSERTG (a->sign == 0 || a->n[a->sign-1] != 0);

	if (asize == 0) return;

	scratch = popg (gdata, asize);
	gtog (a, scratch);
	ptr = scratch->n;

	asize--;
	res = carryl = carryh = 0;
	for (i = 0; i < max; i++) {
		if (i <= asize) {
			ptr1 = ptr;
			ptr2 = ptr + i;
		} else {
			ptr1 = ptr + i - asize;
			ptr2 = ptr + asize;
		}
		while (ptr1 < ptr2) {
			muladd2hlp (&res, &carryl, &carryh, *ptr1, *ptr2);
			ptr1++;
			ptr2--;
		}
		if (ptr1 == ptr2) {
			muladdhlp (&res, &carryl, &carryh, *ptr1, *ptr1);
		}
		a->n[i] = res;
		res = carryl;
		carryl = carryh;
		carryh = 0;
	}
	if (res) {
		a->n[i] = res;
		a->sign = i+1;
	} else
		a->sign = i;

	pushg (gdata, 1);
	ASSERTG (a->sign == 0 || a->n[a->sign-1] != 0);
}

void grammarmulg (		/* b becomes a*b. */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	a,
	giant	b)
{
	int 	i, max;
	int 	asize = a->sign, bsize = b->sign;
	uint32_t *aptr, *bptr, *destptr;
	giant	scratch;
	uint32_t res, carryl, carryh;

	ASSERTG (a->sign >= 0);
	ASSERTG (a->sign == 0 || a->n[a->sign-1] != 0);
	ASSERTG (b->sign >= 0);
	ASSERTG (b->sign == 0 || b->n[b->sign-1] != 0);

	if (bsize == 0) return;
	if (asize == 0) {b->sign = 0; return;}
	if (asize == 1 && a->n[0] == 1) return;

	scratch = popg (gdata, bsize);
	gtog (b, scratch);

	destptr = b->n;
	max = asize + bsize - 1;
	bsize--;
	res = carryl = carryh = 0;
	for (i = 0; i < max; i++) {
		if (i <= bsize) {
			aptr = a->n;
			bptr = scratch->n + i;
		} else {
			aptr = a->n + i - bsize;
			bptr = scratch->n + bsize;
		}
		while (aptr < a->n + asize && bptr >= scratch->n) {
			muladdhlp (&res, &carryl, &carryh, *aptr, *bptr);
			aptr++;
			bptr--;
		}
		*destptr++ = res;
		res = carryl;
		carryl = carryh;
		carryh = 0;
	}
	if (res) {
		*destptr++ = res;
		b->sign = i+1;
	} else
		b->sign = i;

	pushg (gdata, 1);
	ASSERTG (b->sign == 0 || b->n[b->sign-1] != 0);
}

void karatsquareg (		/* x becomes x^2. */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	x)
{
	int	s = x->sign, w;
 	giantstruct a, b;
	giant	c;

	ASSERTG (x->sign >= 0);
	ASSERTG (x->sign == 0 || x->n[x->sign-1] != 0);

				/* IDEA: save a memcpy by having b point */
				/* to x's memory and writing its square to */
				/* the upper half of x */
	w = (s + 1) / 2;

	a.sign = w;		/* a is the lower half of the number */
	a.n = x->n;
	setmaxsize (&a, w);
	while (a.sign && a.n[a.sign-1] == 0) a.sign--;

	b.sign = s - w;		/* b is the upper half of the number */
	b.n = x->n + w + w;
	setmaxsize (&b, b.sign);
	memmove (b.n, a.n + w, b.sign * sizeof (uint32_t));

	c = popg (gdata, w + w + 3);	/* c is the upper and lower half added */
	gtog (&a, c);
	normal_addg (&b, c);

	if (a.sign < KARAT_BREAK_SQUARE)
		grammarsquareg (gdata, &a);	/* recurse */
	else
		karatsquareg (gdata, &a);	/* recurse */
	if (b.sign < KARAT_BREAK_SQUARE)
		grammarsquareg (gdata, &b);
	else
		karatsquareg (gdata, &b);
	if (c->sign < KARAT_BREAK_SQUARE)
		grammarsquareg (gdata, c);
	else
		karatsquareg (gdata, c);

	subg (&a, c);		/* Isolate 2 * upper * lower */
	subg (&b, c);

	while (a.sign < w + w)	/* zero pad squared lower half */
		a.n[a.sign++] = 0;

	x->sign = b.sign + w;	/* Trick to add in a shifted value of c */
	x->n = a.n + w;
	normal_addg (c, x);
	x->sign += w;		/* Undo the trick */
	x->n = a.n;

	pushg (gdata, 1);
	ASSERTG (x->sign == 0 || x->n[x->sign-1] != 0);
}

/* Next, improved Karatsuba routines from A. Powell. */

void karatmulg (		/* y becomes x*y. */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	x,
	giant	y)
{
	int	s = x->sign, t = y->sign, w;
	giantstruct a, b, c;
	giant	d, e, f;

	ASSERTG (x->sign >= 0);
	ASSERTG (x->sign == 0 || x->n[x->sign-1] != 0);
	ASSERTG (y->sign >= 0);
	ASSERTG (y->sign == 0 || y->n[y->sign-1] != 0);

	w = (s + t + 2) / 4;

	a.sign = (s < w) ? s : w;	/* a is the lower half of x */
	a.n = x->n;
	setmaxsize (&a, a.sign);
	while (a.sign && a.n[a.sign-1] == 0) a.sign--;

	b.sign = (s > w) ? s - w : 0;	/* b is the upper half of x */
	b.n = a.n + w;
	setmaxsize (&b, b.sign);

	c.sign = (t < w) ? t : w;	/* c is the lower half of y */
	c.n = y->n;
	setmaxsize (&c, y->maxsize);
	while (c.sign && c.n[c.sign-1] == 0) c.sign--;

	d = popg (gdata, s + t);	/* d is the upper half of y */
	d->sign = (t > w) ? t - w : 0;
	memmove (d->n, c.n + w, d->sign * sizeof (uint32_t));

	e = popg (gdata, s + t);	/* e is the x halves added */
	gtog (&a, e); normal_addg (&b, e);

	f = popg (gdata, s + t + 1);	/* f is the y halves added */
	gtog (&c, f); normal_addg (d, f);

	if (a.sign < KARAT_BREAK_MULT || c.sign < KARAT_BREAK_MULT)
		grammarmulg (gdata, &a, &c);	/* Recurse: mul lowers */
	else
		karatmulg (gdata, &a, &c);	/* Recurse: mul lowers */
	if (b.sign < KARAT_BREAK_MULT || d->sign < KARAT_BREAK_MULT)
		grammarmulg (gdata, &b, d);	/* mul uppers */
	else
		karatmulg (gdata, &b, d);	/* mul uppers */
	if (e->sign < KARAT_BREAK_MULT || f->sign < KARAT_BREAK_MULT)
		grammarmulg (gdata, e, f);	/* mul sums */
	else
		karatmulg (gdata, e, f);	/* mul sums */

	normal_subg (&c, f);		/* Isolate cross product */
	normal_subg (d, f);

	if (d->sign) {
		memmove (c.n + w + w, d->n, d->sign * sizeof (uint32_t)); /* Copy muled uppers to result */
		while (c.sign < w + w)	/* zero pad mul'ed lowers */
			c.n[c.sign++] = 0;
		y->sign = d->sign + w;	/* Trick to add shifted f */
	} else {
		while (c.sign < w)	/* zero pad mul'ed lowers */
			c.n[c.sign++] = 0;
		y->sign = c.sign - w;	/* Trick to add shifted f */
	}
	y->n = c.n + w;
	normal_addg (f, y);
	y->sign += w;			/* Undo the trick */
	y->n = c.n;

	pushg (gdata, 3);
	ASSERTG (y->sign == 0 || y->n[y->sign-1] != 0);
	ASSERTG (y->sign <= y->maxsize);
}

void gmaskbits (	/* keep rightmost bits. Equivalent to g = g%2^bits. */
	int	bits,
	giant	g)
{
	int 	rem = bits&31, words = bits>>5;
	int 	size = abs (g->sign);

	ASSERTG (bits >= 0);
	ASSERTG (g->sign >= 0 || g->n[abs(g->sign)-1] != 0);

	if (size <= words) return;

	if (rem) {
		g->n[words] &= ((1 << rem) - 1);
		words++;
	}
	g->sign = (g->sign < 0) ? -words : words;
	while (g->sign && g->n[g->sign-1] == 0) g->sign--;
}

void gshiftleft (	/* shift g left bits. Equivalent to g = g*2^bits. */
	int	bits,
	giant	g)
{
	int 	rem = bits&31, crem = 32-rem, words = bits>>5;
	int 	size = abs (g->sign), j;
	uint32_t carry;

	ASSERTG (bits >= 0);
	ASSERTG (g->sign == 0 || g->n[abs(g->sign)-1] != 0);

	if (bits == 0) return;
	if (size == 0) return;

	if (rem == 0) {
		memmove (g->n + words, g->n, size * sizeof (uint32_t));
		memset (g->n, 0, words * sizeof (uint32_t));
		g->sign += (g->sign < 0) ? -words : words;
		return;
	}
	carry = g->n[size-1] >> crem;
	for (j = size-1; j > 0; j--) {
		g->n[j+words] = (g->n[j] << rem) | (g->n[j-1] >> crem);
	}
	g->n[words] = g->n[0] << rem;
	memset (g->n, 0, words * sizeof (uint32_t));
	size = size + words;
	if (carry) g->n[size++] = carry;
	g->sign = (g->sign < 0) ? -size : size;

	ASSERTG (g->sign == 0 || g->n[abs(g->sign)-1] != 0);
}

void gtogshiftright (	/* shift src right. Equivalent to dest = src/2^bits. */
	int	bits,
	giant	src,
	giant	dest)
{
	register int j, size = abs (src->sign);
	register uint32_t carry, *sptr, *dptr;
	int 	words = bits >> 5;
	int 	remain = bits & 31, cremain = (32-remain);

	ASSERTG (bits >= 0);
	ASSERTG (bits || src != dest);
	ASSERTG (src->sign == 0 || src->n[abs(src->sign)-1] != 0);

	if (words >= size) {
		dest->sign = 0;
		return;
	}
	if (remain == 0) {
		memmove (dest->n, src->n + words, (size - words) * sizeof (uint32_t));
		dest->sign = src->sign + (src->sign < 0 ? words : -words);
		return;
	}

	size -= words;
	sptr = src->n + words;
	dptr = dest->n;
	for (j = 0; j < size-1; j++, sptr++, dptr++) {
		carry = sptr[1] << cremain;
		*dptr = (sptr[0] >> remain) | carry;
	}
	*dptr = *sptr >> remain;
	if (*dptr == 0) size--;
	dest->sign = (src->sign > 0) ? size : -size;

	ASSERTG (dest->sign == 0 || dest->n[abs(dest->sign)-1] != 0);
}

// Highly specialized routine for gwnum's gwtogiant.  Like gtogshiftright extract top bits of src and copy them to dest.
// However if accum == -1 then flip dest bits and make dest negative.  Also clear src bits that were copied to dest.

void gtogshiftrightsplit (
	int	bits,
	giant	src,			// Must be positive
	giant	dest,
	int64_t	accum)			// Must be 0 or -1
{
	int	src_size = src->sign;
	int 	word = bits >> 5;
	int 	bit = bits & 31;
	uint32_t carry, *sptr, *dptr;

	ASSERTG (bits > 0);
	ASSERTG (src != dest);
	ASSERTG (src->sign >= 0 && (src->sign == 0 || src->n[src->sign-1] != 0));

	if (word >= src_size) {
		// The normal case is accum is zero and we set dest = 0.  The abnormal case is accum = -1.  This only happens for generic reduction
		// where gwtogiant is converting roughly half of the FFT data.  In this case, make src negative and set dest = 0.
		if (accum == -1) {
			for (int i = 0; i < src_size; i++) src->n[i] = ~src->n[i];
			while (src->sign && src->n[src->sign-1] == 0) src->sign--;
			iaddg (1, src);
			src->sign = -src->sign;
		}
		dest->sign = 0;
		return;
	}

	// Init pointers
	sptr = src->n + word;
	dptr = dest->n;

	// Split first source word
	if (bit) {
		carry = *sptr >> bit;
		*sptr -= (carry << bit);
		word++;
		sptr++;
	}
	src->sign = word;

	// Split subsequent words without modifying source
	for ( ; word < src_size; word++, sptr++, dptr++) {
		if (bit == 0) *dptr = *sptr;
		else *dptr = (*sptr << (32 - bit)) + carry, carry = *sptr >> bit;
		if (accum == -1) *dptr = ~*dptr;
	}

	// Output the carry
	if (bit) {
		if (accum == 0) *dptr = carry;
		else *dptr = (~(carry << bit)) >> bit;		// Bit flip only the relevant carry bits 
		dptr++;
	}

	// Trim source
	while (src->sign && src->n[src->sign-1] == 0) src->sign--;

	// Trim dest, increment result if accum = -1
	dest->sign = (int) (dptr - dest->n);
	while (dest->sign && dest->n[dest->sign-1] == 0) dest->sign--;
	if (accum == -1) iaddg (1, dest), dest->sign = -dest->sign;

	ASSERTG (src->sign == 0 || src->n[src->sign-1] != 0);
	ASSERTG (dest->sign == 0 || dest->n[abs(dest->sign)-1] != 0);
}

// Highly specialized routine for gwnum's gwtogiant, sets top bits of dest.  dest = (dest % 2^bits) + (src * 2^bits)

void gtogshiftleftunsplit (
	int	bits,
	giant	src,			// Must be positive
	giant	dest)			// Must be positive
{
	int	src_size = src->sign;
	int	dest_size = dest->sign;
	int 	word = bits >> 5;
	int 	bit = bits & 31;
	uint32_t carry, *sptr, *dptr;

	ASSERTG (bits > 0);
	ASSERTG (src != dest);
	ASSERTG (src->sign >= 0 && (src->sign == 0 || src->n[src->sign-1] != 0));
	ASSERTG (dest->sign >= 0 && (dest->sign == 0 || dest->n[dest->sign-1] != 0));

	// We assume dest has already been trimmed.  If src is zero, then we're done.
	if (src_size == 0) return;

	// Zero upper word of dest if needed
	if (word > dest_size) memset (dest->n + dest_size, 0, (word - dest_size) * sizeof (uint32_t));

	// Split source words while copying to dest
	sptr = src->n;
	dptr = dest->n + word;
	carry = (bit == 0) ? 0 : *dptr;
	for ( ; src_size; src_size--, sptr++, dptr++) {
		*dptr = (*sptr << bit) + carry;
		carry = (bit == 0) ? 0 : *sptr >> (32 - bit);
	}
	if (carry) *dptr++ = carry;

	// Trim dest, increment result if accum = -1
	dest->sign = (int) (dptr - dest->n);
	while (dest->sign && dest->n[dest->sign-1] == 0) dest->sign--;

	ASSERTG (dest->sign == 0 || dest->n[dest->sign-1] != 0);
}

int invg (		/* Computes 1/y, that is the number n such that */
			/* n * y mod x = 1.  If x and y are not */
			/* relatively prime, y is the -1 * GCD (x, y). */
	giant 	xx,
	giant 	yy)
{
	ghandle gdata;
	int	stop_reason;

	init_ghandle (&gdata);
	stop_reason = invg_common (&gdata, xx, yy, 0);
	term_ghandle (&gdata);
	return (stop_reason);
}

int invgi (		/* Interruptable version of invg */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	int	thread_num,
	giant 	xx,
	giant 	yy)
{
	return (invg_common (gdata, xx, yy, 0x80000000 + thread_num));
}

int invg_common (	/* Common invg code */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	xx,
	giant 	yy,
	int	interruptable)
{
	giant	x, y;
	gmatrixstruct A;
	giantstruct ul, ll;
	int	ss, stop_reason;

	ASSERTG (xx->sign > 0 && xx->n[xx->sign-1] != 0);
	ASSERTG (yy->sign != 0 && yy->n[abs(yy->sign)-1] != 0);

/* Copy the first argument, we can trash the second */

	ss = stackstart (gdata);
	x = popg (gdata, xx->sign);
	if (x == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	gtog (xx, x);
	y = yy;

/* Make y positive and less than x */

	while (y->sign < 0) addg (x, y);
	modgi (gdata, x, y);

/* Compute the GCD and inverse the fastest way possible.  The inverse is computed in the matrix A, and only the */
/* right side of the matrix is needed.  However, the recursive ggcd code needs the left side of the array allocated. */

	A.ur = popg (gdata, x->sign + 1);			// In theory, +1 not needed.  See second test_paul.c in mersenne forum thread mentioned below.
	if (A.ur == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	setzero (A.ur);
	A.lr = popg (gdata, x->sign + 1);			// In theory, +1 not needed.  But https://www.mersenneforum.org/showpost.php?p=638742&postcount=75 fails.
	if (A.lr == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	setone (A.lr);
	if (y->sign <= GCDLIMIT) {
		A.ul = &ul; setzero (A.ul); setmaxsize (A.ul, 1);
		A.ll = &ll; setzero (A.ll); setmaxsize (A.ll, 1);
		stop_reason = cextgcdg (gdata, &x, &y, &A, interruptable);
		if (stop_reason) goto done;
	} else {
		A.ul = popg (gdata, x->sign);
		if (A.ul == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
		setone (A.ul);
		A.ll = popg (gdata, x->sign);
		if (A.ll == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
		setzero (A.ll);
		stop_reason = ggcd (gdata, &x, &y, &A, interruptable);
		if (stop_reason) goto done;
		pushg (gdata, 2);
	}

/* If a GCD was found, return that in yy (times -1) */

	if (! isone (x)) {
		if (x != yy) gtog (x, yy);
		yy->sign = -yy->sign;
		ASSERTG (yy->sign < 0 && yy->n[-yy->sign-1] != 0);
	}

/* Otherwise, return the inverse of yy in yy */

	else {
		gtog (A.ur, yy);
		if (A.ur->sign < 0) addg (xx, yy);
		ASSERTG (yy->sign > 0 && yy->n[yy->sign-1] != 0);
	}

/* Cleanup and return */

done:	pushall (gdata, ss);
	return (stop_reason);
}

/* A wrapper routine for the assembly language gcdhlp routine */
/* Gcdhlp returns A,B,C,D as defined in Knuth vol. 2's description */
/* of extended GCD for large numbers. */

int gcdhlp_wrapper (
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	u,
	giant	v,
	gmatrix	A)
{
	int	i;
	uint32_t ures, ucarryl, ucarryh;
	uint32_t vres, vcarryl, vcarryh;
	struct {
		uint32_t A;
		uint32_t B;
		uint32_t C;
		uint32_t D;
		uint32_t ODD;
	} ret;

	if (!gcdhlp (u->sign, u->n, v->sign, v->n, &ret)) return (FALSE);
	if (A != NULL) {
		gmatrixstruct B;
		giantstruct ul, ur, ll, lr;
		setmaxsize (&ul, 1); setmaxsize (&ur, 1);
		setmaxsize (&ll, 1); setmaxsize (&lr, 1);
		B.ul = &ul; B.ur = &ur;
		B.ll = &ll; B.lr = &lr;
		ul.sign = ret.A ? 1 : 0; ul.n = &ret.A;
		ur.sign = ret.B ? 1 : 0; ur.n = &ret.B;
		ll.sign = ret.C ? 1 : 0; ll.n = &ret.C;
		lr.sign = ret.D ? 1 : 0; lr.n = &ret.D;
		if (ret.ODD) {
			ul.sign = -ul.sign;
			lr.sign = -lr.sign;
		} else {
			ur.sign = -ur.sign;
			ll.sign = -ll.sign;
		}
		mulmM (gdata, &B, A);
	}

/* Now do a mulvM (&B, u, v) using the special known properties */
/* of matrix B.  Not only is this code faster than mulvM, but it */
/* uses less memory and does not require u and v to be copied */
/* prior to calling mulvM. */

	ures = ucarryl = ucarryh = 0;
	vres = vcarryl = vcarryh = 0;
	for (i = 0; i < v->sign; i++) {
		if (ret.ODD) {
			mulsubhlp (&ures, &ucarryl, &ucarryh, u->n[i], ret.A);
			muladdhlp (&ures, &ucarryl, &ucarryh, v->n[i], ret.B);
			muladdhlp (&vres, &vcarryl, &vcarryh, u->n[i], ret.C);
			mulsubhlp (&vres, &vcarryl, &vcarryh, v->n[i], ret.D);
		} else {
			muladdhlp (&ures, &ucarryl, &ucarryh, u->n[i], ret.A);
			mulsubhlp (&ures, &ucarryl, &ucarryh, v->n[i], ret.B);
			mulsubhlp (&vres, &vcarryl, &vcarryh, u->n[i], ret.C);
			muladdhlp (&vres, &vcarryl, &vcarryh, v->n[i], ret.D);
		}
		u->n[i] = ures;
		ures = ucarryl;
		ucarryl = ucarryh;
		ucarryh = (int) ucarryh >> 4;	/* Maintain sign bit */
		v->n[i] = vres;
		vres = vcarryl;
		vcarryl = vcarryh;
		vcarryh = (int) vcarryh >> 4;	/* Maintain sign bit */
	}
	u->sign = v->sign;
	while (u->sign > 0 && u->n[u->sign-1] == 0) u->sign--;
	while (v->sign > 0 && v->n[v->sign-1] == 0) v->sign--;
	ASSERTG (u->sign > v->sign ||
		 (u->sign == v->sign && u->n[u->sign-1] >= v->n[v->sign-1]));
	return (TRUE);
}


int gcdg (	/* Computes the GCD of x and y and returns the GCD in y */
		/* The x argument is not destroyed */
	giant	xx,
	giant	yy)
{
	ghandle gdata;
	int	stop_reason;

	init_ghandle (&gdata);
	stop_reason = gcdg_common (&gdata, xx, yy, 0);
	term_ghandle (&gdata);
	return (stop_reason);
}

int gcdgi (			/* Interruptable version of the above */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	int	thread_num,
	giant	xx,
	giant	yy)
{
	return (gcdg_common (gdata, xx, yy, 0x80000000 + thread_num));
}

int gcdg_common (		/* Common code for above */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	xx,
	giant	yy,
	int	interruptable)
{
	giant	x, y;
	int	ss, stop_reason;

	ASSERTG (xx->sign > 0 && xx->n[xx->sign-1] != 0);
	ASSERTG (yy->sign > 0 && yy->n[yy->sign-1] != 0);

/* Copy the first argument, we can trash the second */

	ss = stackstart (gdata);
	x = popg (gdata, xx->sign + 1);
	if (x == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	gtog (xx, x);
	y = yy;

/* Make y less than x */

	if (gcompg (x, y) < 0) gswap (&x, &y);

/* Compute the GCD the fastest way possible */

	if (abs (y->sign) <= GCDLIMIT) {
		stop_reason = cextgcdg (gdata, &x, &y, NULL, interruptable);
		if (stop_reason) goto done;
	} else {
		stop_reason = ggcd (gdata, &x, &y, NULL, interruptable);
		if (stop_reason) goto done;
	}

/* If the routines we called happened to return the result in yy, then great */
/* Otherwise, copy the result to yy */

	if (x != yy) gtog (x, yy);

/* Cleanup and return */

	ASSERTG (yy->sign > 0 && yy->n[yy->sign-1] != 0);
done:	pushall (gdata, ss);
	return (stop_reason);
}

int cextgcdg (		/* Classical Euclid GCD. a becomes gcd(a, b). */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	*a,
	giant 	*b,
	gmatrix	A,
	int	interruptable)
{
	giant	u, v;
	int	stop_reason;

	ASSERTG ((*a)->sign > 0);
	ASSERTG ((*b)->sign >= 0);

/* Dereference arguments for faster access */

	u = *a;
	v = *b;

/* Do the extended GCD */

	while (v->sign > 0) {
		unsigned int diff_length;

/* Check for an interrupt */

		if (interruptable && StopCheckRoutine != NULL) {
			stop_reason = stopCheck (interruptable);
			if (stop_reason) return (stop_reason);
		}

/* If the quotient will fit in one word, use the GCD helper */
/* function which can do several GCD steps in single precision, */
/* postponing multi-precision operations as long as possible. */

/* There is a remote chance that the GCD helper routine can not */
/* correctly determine a single quotient.  In that case gcdhlp */
/* returns FALSE and we use the slower code that can compute */
/* the proper quotient. */

		diff_length = u->sign - v->sign;
		if (diff_length > 1 ||
		    (diff_length == 1 && u->n[u->sign-1] >= v->n[v->sign-1]) ||
		    ! gcdhlp_wrapper (gdata, u, v, A))
			onestep (gdata, &u, &v, A);
	}

/* Dereference arguments for faster access */

	*a = u;
	*b = v;

/* Return not-stopped code */

	return (0);
}


/**************************************************************
 *
 * Initialization and utility functions
 *
 **************************************************************/

void init_ghandle (
	ghandle *gdata)
{
	memset (gdata, 0, sizeof (ghandle));
}

void term_ghandle (
	ghandle *gdata)
{
	aligned_free (gdata->ooura_sincos);
	free (gdata->ooura_ip);
	free (gdata->cur_recip);
	free (gdata->cur_den);
}

giant popg (
	ghandle *gdata,		/* Free memory blocks for temporaries */
	int	size)		/* Giant size in 32-bit words */
{
	giant	g;
	gstacknode *s;
	unsigned long memsize;

	ASSERTG (size >= 0);

/* Malloc our giant */

	memsize = sizeof (gstacknode) + sizeof (giantstruct) + size * sizeof (uint32_t);
	if (memsize > gdata->blksize) {
		s = (gstacknode *) malloc (memsize);
		memmove (s, &gdata->stack, sizeof (gstacknode));
	} else if (gdata->stack.memblk == NULL ||
		   memsize > gdata->blksize - gdata->stack.offset) {
		s = (gstacknode *) ((*gdata->allocate) (gdata->handle));
		memmove (s, &gdata->stack, sizeof (gstacknode));
		gdata->stack.memblk = s;
		gdata->stack.offset = memsize;
	} else {
		s = (gstacknode *) ((char *) gdata->stack.memblk + gdata->stack.offset);
		memmove (s, &gdata->stack, sizeof (gstacknode));
		gdata->stack.offset += memsize;
	}
	gdata->num_popgs++;

	g = (giant) ((char *) s + sizeof (gstacknode));
	g->n = (uint32_t *) ((char *) g + sizeof (giantstruct));
	setmaxsize (g, size);

	gdata->stack.prev = g;
	return (g);
}

void pushg (
	ghandle *gdata,		/* Free memory blocks for temporaries */
	int	a)
{
	gstacknode *s;

	while (a--) {
		s = (gstacknode *) ((char *) gdata->stack.prev - sizeof (gstacknode));
		if (gdata->stack.memblk == s->memblk &&
		    gdata->stack.offset == s->offset) {
			memmove (&gdata->stack, s, sizeof (gstacknode));
			free (s);
		} 
		else if (gdata->stack.memblk != s->memblk) {
			memmove (&gdata->stack, s, sizeof (gstacknode));
			(*gdata->free)(gdata->handle, s);
		} 
		else {
			memmove (&gdata->stack, s, sizeof (gstacknode));
		}
		gdata->num_popgs--;
	}
}

int bitlen (
	giant	g)
{
	int 	b = 32, c = 1<<31, w, size;

	ASSERTG (g->sign == 0 || g->n[abs(g->sign)-1] != 0);

	size = abs (g->sign);
	if (size == 0) return (0);
	w = g->n[size - 1];
	while ((w&c) == 0) {
		b--;
		c >>= 1;
	}
	return (32 * (size-1) + b);
}

int gsign (		/* Returns the sign of g. */
	giant 	g)
{
	if (isZero(g)) return (0);
	if (g->sign > 0) return (1);
	return (-1);
}


/**************************************************************
 *
 * Private Math Functions
 *
 **************************************************************/


void make_recip (	/* r becomes the steady-state reciprocal
			 * 2^(2b)/d, where b = bit-length of d-1. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	d, 
	giant 	r)
{
	int	b;
	giant 	tmp, tmp2;

	ASSERTG (d->sign > 0);

	tmp = popg (gdata, (d->sign << 1) + 1);
	tmp2 = popg (gdata, (d->sign << 1) + 1);
	setone (r);
	normal_subg (r, d);
	b = bitlen (d);
	normal_addg (r, d);
	gshiftleft (b, r);
	do {
		gtog (r, tmp2);
		gtog (r, tmp);
		squaregi (gdata, tmp);
		gshiftright (b, tmp);
		mulgi (gdata, d, tmp);
		gshiftright (b, tmp);
		addg (r, r); 
		subg (tmp, r);
	} while (gcompg (r, tmp2) > 0);
	setone (tmp);
	gshiftleft (2*b, tmp);
	gtog (r, tmp2); 
	mulgi (gdata, d, tmp2);
	subg (tmp2, tmp);
	while (tmp->sign < 0) {
		ulsubg (1, r);
		addg (d, tmp);
	}
	pushg (gdata, 2);
}

void divg_via_recip (		/* n := n/d, where r is the precalculated
				 * steady-state reciprocal of d. */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant 	d, 
	giant 	r, 
	giant 	n)
{
	int 	s = 2*(bitlen(r)-1), sign = gsign(n);
	giant 	tmp, tmp2;

	ASSERTG (d->sign > 0);

	tmp = popg (gdata, abs (n->sign) + r->sign);
	tmp2 = popg (gdata, abs (n->sign) + r->sign);

	n->sign = abs (n->sign);
	setzero (tmp2);
	while (1) {
		gtog (n, tmp);	
		mulgi (gdata, r, tmp);
		gshiftright (s, tmp);
		addg (tmp, tmp2);
		mulgi (gdata, d, tmp);
		subg (tmp, n);
		if (gcompg (n,d) >= 0) {
			subg (d,n);
			iaddg (1, tmp2);
		}
		if (gcompg (n,d) < 0) break;
	}
	gtog (tmp2, n);
	n->sign *= sign;
	pushg (gdata, 2);
}


/**************************************************************
 *
 * FFT multiply Functions
 *
 **************************************************************/

int lpt (		/* Returns least power of two greater than n. */
	int	n)
{
	register int	i = 1;

	while (i < n) i <<= 1;
	return (i);
}

void makewt (int nw, int *ip, double *w);
void makect (int nc, int *ip, double *c);

int init_sincos (
	ghandle	*gdata,
	int 	n)
{
	if (n <= gdata->ooura_fft_size) return (0);

/* Free the old sin/cos data */

	aligned_free (gdata->ooura_sincos);
	gdata->ooura_sincos = NULL;
	free (gdata->ooura_ip);
	gdata->ooura_ip = NULL;
	gdata->ooura_fft_size = 0;

/* If there are any freed gwnum's deallocate one so that */
/* aligned_malloc can access the memory.  We do this because */
/* ECM and P-1 allocate gobs of memory then they call GCD. */
/* There may not be memory available on the heap, but there may be */
/* some freed-but-cached gwnums.  We uncache one so that we will */
/* have plenty of memory available. */

	if (gdata->deallocate != NULL)
		(*gdata->deallocate) (gdata->handle);

/* Allocate new arrays for sin/cos data */

	gdata->ooura_sincos = (double *) aligned_malloc ((n/2) * sizeof (double), sizeof (double));
	if (gdata->ooura_sincos == NULL) return (GIANT_OUT_OF_MEMORY);
	gdata->ooura_ip = (int *) malloc (((int) sqrt((double)(n/2)) + 2) * sizeof (int));
	if (gdata->ooura_ip == NULL) {
		aligned_free (gdata->ooura_sincos);
		gdata->ooura_sincos = NULL;
		return (GIANT_OUT_OF_MEMORY);
	}

/* Init the new sin/cos data */

	gdata->ooura_ip[0] = 0;
	gdata->ooura_fft_size = n;
	makewt (n >> 2, gdata->ooura_ip, gdata->ooura_sincos);
	makect (n >> 2, gdata->ooura_ip, gdata->ooura_sincos + (n >> 2));
	return (0);
}

void mp_squ_cmul (int nfft, double dinout[])
{
	int j;
	double xr, xi;
    
	dinout[0] *= dinout[0];
	dinout[1] *= dinout[1];
	for (j = 2; j < nfft; j += 2) {
		xr = dinout[j];
		xi = dinout[j + 1];
		dinout[j] = xr * xr - xi * xi;
		dinout[j + 1] = xr * xi * 2.0;
	}
}

void mp_mul_cmul (int nfft, double din[], double dinout[])
{
	int j;
	double xr, xi, yr, yi;
    
	dinout[0] *= din[0];
	dinout[1] *= din[1];
	for (j = 2; j < nfft; j += 2) {
		xr = din[j];
		xi = din[j + 1];
		yr = dinout[j];
		yi = dinout[j + 1];
		dinout[j] = xr * yr - xi * yi;
		dinout[j + 1] = xr * yi + xi * yr;
	}
}

void addsignal (
	giant	x,
	int	size,
	double 	*z,
	int 	n)
{
#define TWO24		((double)(16777216.0))
#define TWO24PLUSHALF	((double)(16777216.5))
#define TWOM24		(double)(0.000000059604644775390625)
	register int	j, m1, m2, value, carry;
	register double scale, f, fltcarry;

/* Scale each element down */

	scale = 2.0 / (double) n;

/* Convert each double to an integer.  Extract lower 16 bits and leave the */
/* rest as a carry.  Each pair of conversions results in one output value.*/
/* This is tricky as we can't convert the 53-bit double to a 31-bit int. */
/* Therefore we convert in two stages - the upper 29 bits and the */
/* lower 24-bits. */

	carry = 0;
	fltcarry = 0.0;
	for (j = 0; ; ) {
		f = z[j+j] * scale + fltcarry * 256.0;
		fltcarry = (double) ((int) (f * TWOM24));
		f = f - fltcarry * TWO24;
		m1 = (int) (f + TWO24PLUSHALF) + carry;
		carry = (m1 >> 16) - 256;

		f = z[j+j+1] * scale + fltcarry * 256.0;
		fltcarry = (double) ((int) (f * TWOM24));
		f = f - fltcarry * TWO24;
		m2 = (int) (f + TWO24PLUSHALF) + carry;
		carry = (m2 >> 16) - 256;

		value = ((m2 & 0xFFFF) << 16) + (m1 & 0xFFFF);
		if (++j == size) break;
		x->n[j-1] = value;
	}
	if (value == 0) j--;
	else x->n[j-1] = value;
	x->sign = j;
	ASSERTG (x->n[x->sign-1] != 0);
}

int FFTsquareg (
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant	x)
{
	int	size = x->sign;
	double	*z1;
	int 	ss, L, stop_reason;

	ASSERTG (x->sign >= 4 && x->n[x->sign-1] != 0);

	ss = stackstart (gdata);

	L = lpt (size+size) << 1;
	stop_reason = init_sincos (gdata, L);
	if (stop_reason) goto done;

	z1 = (double *) popg (gdata, (L+1) * sizeof (double) / sizeof (uint32_t));
	if (z1 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	z1 = align_ptr (z1, sizeof (double));

	giant_to_double (x, size, z1, L);
	rdft (L, 1, z1, gdata->ooura_ip, gdata->ooura_sincos);
	mp_squ_cmul (L, z1);
	rdft (L, -1, z1, gdata->ooura_ip, gdata->ooura_sincos);
	addsignal (x, size+size, z1, L);

done:	pushall (gdata, ss);
	return (stop_reason);
}

int FFTmulg (			/* x becomes y*x. */
	ghandle *gdata,		/* Free memory blocks for temporaries */
	giant	y,
	giant	x)
{
	int	sizex = x->sign, sizey = y->sign;
	double	*z1, *z2;
	int	ss, L, stop_reason;

	ASSERTG (y->sign >= 4 && y->n[y->sign-1] != 0);
	ASSERTG (x->sign >= 4 && x->n[x->sign-1] != 0);

/* Allocate FFT arrays.  Make sure the arrays of doubles are aligned on */
/* an eight byte boundaries. */

	ss = stackstart (gdata);

	L = lpt (sizex+sizey) << 1;
	stop_reason = init_sincos (gdata, L);
	if (stop_reason) goto done;

	z1 = (double *) popg (gdata, (L+1) * sizeof (double) / sizeof (uint32_t));
	if (z1 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	z1 = align_ptr (z1, sizeof (double));

	z2 = (double *) popg (gdata, (L+1) * sizeof (double) / sizeof (uint32_t));
	if (z2 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	z2 = align_ptr (z2, sizeof (double));

/* Do the FFT multiply. */

	giant_to_double (x, sizex, z1, L);
	giant_to_double (y, sizey, z2, L);
	rdft (L, 1, z1, gdata->ooura_ip, gdata->ooura_sincos);
	rdft (L, 1, z2, gdata->ooura_ip, gdata->ooura_sincos);
	mp_mul_cmul (L, z2, z1);
	rdft (L, -1, z1, gdata->ooura_ip, gdata->ooura_sincos);
	addsignal (x, sizex+sizey, z1, L);

	ASSERTG (y->sign > 0 && y->n[y->sign-1] != 0);
done:	pushall (gdata, ss);
	return (stop_reason);
}

void giant_to_double (
	giant 	x,
	int 	sizex,
	double 	*z,
	int 	L)
{
	register int j, w, carry;

	carry = 0;
	for (j = 0; j < sizex; j++) {
		w = (x->n[j] & 0xFFFF) + carry;
		if (w < 32768) {
			z[j+j] = w;
			carry = 0;
		} else {
			z[j+j] = w - 65536;
			carry = 1;
		}
		w = (x->n[j] >> 16) + carry;
		if (w < 32768) {
			z[j+j+1] = w;
			carry = 0;
		} else {
			z[j+j+1] = w - 65536;
			carry = 1;
		}
	}
	if (carry) z[j+j-1] += 65536;
	for (j = sizex + sizex; j < L; j++) {
		z[j] = 0.0;
	}
}

void gsetlength (	/* Set the length of g to n bits (g = g mod 2^n) */
	int	n,
	giant	g)
{
	int	size;

	ASSERTG (g->sign >= (n >> 5));

	size = (n + 31) >> 5;
	if (n & 31) g->n[size-1] &= (1 << (n & 31)) - 1;
	while (size && g->n[size-1] == 0) size--;
	g->sign = size;
}

void addshiftedg (	/* Shift x left n words then add to g */
			/* This lets us allocate smaller temporaries in hgcd */
	int	n,
	giant	x,	/* x must be positive */
	giant	g)
{
	ASSERTG (n >= 0 && x->sign >= 0);

	if (x->sign == 0) return;

	if (g->sign >= 0) {
		while (g->sign < n) g->n[g->sign++] = 0;
		g->sign -= n;
		g->n += n;
		normal_addg (x, g);
		g->sign += n;
		g->n -= n;
		while (g->sign && g->n[g->sign-1] == 0) g->sign--;
		return;
	}

	negg (g);
	if (g->sign < n) {
		reverse_subg (x, g, n);
		return;
	} 

	g->sign -= n;
	g->n += n;
	if (gcompg (g, x) >= 0) {
		normal_subg (x, g);
		g->sign += n;
		g->n -= n;
		while (g->sign && g->n[g->sign-1] == 0) g->sign--;
		negg (g);
	} else {
		g->sign += n;
		g->n -= n;
		reverse_subg (x, g, n);
	}
}

void onestep (		/* Do one step of the euclidean algorithm and modify
			 * the matrix A accordingly. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	*x,
	giant 	*y,
	gmatrix A)
{
	giant q = popg (gdata, (*x)->sign);

	ASSERTG ((*x)->sign >= (*y)->sign);

	gtog (*x, q);		/* Set q = x / y */ 
	divgi (gdata, *y, q);
	if (A != NULL) punch (gdata, q, A);
	mulgi (gdata, *y, q);		/* Now set x = x - q * y */
	subg (q, *x);
	gswap (x, y);
	pushg (gdata, 1);
}

int mulvM (		/* Multiply vector by Matrix; changes x,y. */
			/* Caller must make sure x and y variables */
			/* can hold larger intermediate results */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	gmatrix A,
	giant 	x,
	giant 	y)
{
	giant	s0, s1;
	int	ss, stop_reason;

	ss = stackstart (gdata);
	s0 = popg (gdata, abs(A->ll->sign) + x->sign);
	if (s0 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	s1 = popg (gdata, abs(A->ur->sign) + y->sign);
	if (s1 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }

	gtog (x, s0);
	gtog (y, s1);
	stop_reason = mulgi (gdata, A->ul, x);
	if (stop_reason) goto done;
	mulgi (gdata, A->ur, s1);
	if (stop_reason) goto done;
	addg (s1, x);

	stop_reason = mulgi (gdata, A->lr, y);
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, A->ll, s0);
	if (stop_reason) goto done;
	addg (s0, y);

done:	pushall (gdata, ss);
	return (stop_reason);
}

int mulmM (		/* Multiply matrix by Matrix; changes second matrix. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	gmatrix A,
	gmatrix B)
{
	giant	s0, s1;
	int	ss, stop_reason;

	ss = stackstart (gdata);
	s0 = popg (gdata, abs(A->ll->sign) + abs(B->ur->sign));
	if (s0 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	s1 = popg (gdata, abs(A->ur->sign) + abs(B->lr->sign));
	if (s1 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }

	gtog (B->ul, s0);
	gtog (B->ll, s1);
	stop_reason = mulgi (gdata, A->ul, B->ul);
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, A->ur, s1);
	if (stop_reason) goto done;
	addg (s1, B->ul);

	stop_reason = mulgi (gdata, A->lr, B->ll);
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, A->ll, s0);
	if (stop_reason) goto done;
	addg (s0, B->ll);

	gtog (B->ur, s0);
	gtog (B->lr, s1);
	stop_reason = mulgi (gdata, A->ul, B->ur);
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, A->ur, s1);
	if (stop_reason) goto done;
	addg (s1, B->ur);
	stop_reason = mulgi (gdata, A->lr, B->lr);
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, A->ll, s0);
	if (stop_reason) goto done;
	addg (s0, B->lr);

done:	pushall (gdata, ss);
	return (stop_reason);
}

int mulmMsp (		/* Like mulmM except that the data areas of A */
			/* are in the upper half B (see hgcd) */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	gmatrix A,
	gmatrix B,
	int	maxsize)
{
	giant	tmp0, tmp1, tmp2, tmp3;
	int	ss, stop_reason;

	ss = stackstart (gdata);
	tmp0 = popg (gdata, maxsize);
	if (tmp0 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	tmp1 = popg (gdata, maxsize);
	if (tmp1 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	tmp2 = popg (gdata, maxsize);
	if (tmp2 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	tmp3 = popg (gdata, maxsize);
	if (tmp3 == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }

	gtog (A->ur, tmp0);	/* Copy A->ur before the mul destroys it */
	gtog (A->lr, tmp2);	/* Copy A->lr before the mul destroys it */
	gtog (B->ur, tmp1);	/* Copy B->ur before the mul destroys it */

	stop_reason = mulgi (gdata, A->ul, B->ur);	/* A->ul * B->ur (destroys A->ur & A->lr) */
	if (stop_reason) goto done;
	gtog (tmp0, tmp3);
	stop_reason = mulgi (gdata, B->lr, tmp3);	/* A->ur * B->lr */
	if (stop_reason) goto done;
	addg (tmp3, B->ur);				/* B->ur = A->ul * B->ur + A->ur * B->lr */

	stop_reason = mulgi (gdata, A->ll, tmp1);	/* A->ll * B->ur */
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, tmp2, B->lr);	/* A->lr * B->lr */
	if (stop_reason) goto done;
	addg (tmp1, B->lr);				/* B->lr = A->ll * B->ur + A->lr * B->lr */

	stop_reason = mulgi (gdata, B->ll, tmp0);	/* A->ur * B->ll */
	if (stop_reason) goto done;
	gtog (B->ul, tmp1);				/* Copy B->ul before the mul destroys it */
	gtog (A->ll, tmp3);				/* Copy A->ll before the mul destroys it */
	stop_reason = mulgi (gdata, A->ul, B->ul);	/* A->ul * B->ul (destroys A->ul & A->ll) */
	if (stop_reason) goto done;
	addg (tmp0, B->ul);				/* B->ul = A->ul * B->ul + A->ur * B->ll */

	stop_reason = mulgi (gdata, tmp3, tmp1);	/* A->ll * B->ul */
	if (stop_reason) goto done;
	stop_reason = mulgi (gdata, tmp2, B->ll);	/* A->lr * B->ll */
	if (stop_reason) goto done;
	addg (tmp1, B->ll);				/* B->ll = A->ll * B->ul + A->lr * B->ll */

done:	pushall (gdata, ss);
	return (stop_reason);
}

void punch (		/* Multiply the matrix A on the left by [0,1,1,-q]. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	q,
	gmatrix A)
{
	giant tmp = popg (gdata, abs(A->lr->sign) + q->sign);

	gtog (q, tmp);
	mulgi (gdata, A->ll, tmp);
	subg (tmp, A->ul);
	gswap (&A->ul, &A->ll);

	gtog (q, tmp);
	mulgi (gdata, A->lr, tmp);
	subg (tmp, A->ur);
	gswap (&A->ur, &A->lr);

	pushg (gdata, 1);
}

int ggcd (		/* A giant gcd.  Modifies its arguments. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant 	*x,
	giant 	*y,
	gmatrix	R,
	int	interruptable)
{
	gmatrixstruct A;
	int	ss, stop_reason;

/* Remember stack pointer in case of error */

	ss = stackstart (gdata);

/* To avoid continually expanding the sincos array, figure out (roughly) */
/* the maximum size table we will need and allocate it now. */

	if ((*x)->sign / 2 > FFT_BREAK_MULT1) {
		stop_reason = init_sincos (gdata, lpt ((*x)->sign / 2) << 1);
		if (stop_reason) return (stop_reason);
	}

/* If R is not NULL then we are doing an extended GCD.  Recursively */
/* do half GCDs and then multiply the matrices in reverse order for */
/* optimum efficiency. */

	if (R != NULL) {
		stop_reason = hgcd (gdata, 0, x, y, R, interruptable);
		if (stop_reason) return (stop_reason);
		return (rhgcd (gdata, x, y, R, interruptable));
	}

/* Call half GCDs until the numbers get pretty small.  The half GCD code */
/* is most efficient when computing 1/3 of the GCD result size.  But more */
/* importantly, the FFT code is most efficient when operating on a power */
/* of two words.  Thus, if x->size is near a power of two we make the */
/* traditional two hgcd calls each computing 1/4 of the result size. */
/* Otherwise, we make one hgcd call computing 1/3 of the result size. */

/* If FFTmulg ever implemented non-power-of-2 FFTs, then we likely would */
/* always do a 1/3 hgcd call */

	while ((*y)->sign > GCDLIMIT) {
		int 	quarter_size, third_size, a_size;

		quarter_size = ((*y)->sign - 1) >> 2;
		third_size = ((*y)->sign - 1) / 3;
		if (lpt (quarter_size) == lpt (third_size))
			a_size = third_size;
		else
			a_size = quarter_size;

		A.ul = popg (gdata, a_size);
		if (A.ul == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
		setone (A.ul);
		A.ll = popg (gdata, a_size);
		if (A.ll == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
		setzero (A.ll);
		A.ur = popg (gdata, a_size);
		if (A.ur == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
		setzero (A.ur);
		A.lr = popg (gdata, a_size);
		if (A.lr == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
		setone (A.lr);

/* Do the first recursion */

		stop_reason = hgcd (gdata, (*y)->sign - (a_size + a_size + 1),
				    x, y, &A, interruptable);
		if (stop_reason) goto done;

/* Do a single step if the hgcd call didn't make any progress */

		if (isone (A.lr)) {
			pushg (gdata, 4);		/* Free matrix A */
			onestep (gdata, x, y, NULL);
			continue;
		}

/* If we did a 1/3 bits hgcd call, then we're done.  If we did a 1/4 bits */
/* hgcd call, then the next iteration will do the matching 1/4 bits hgcd */
/* call (since result x is now 3/4 size and 1/3 of 3/4 is 1/4! */

		pushg (gdata, 4);		/* Free matrix A */
	}

/* Do the last few words in a brute force way */

	return (cextgcdg (gdata, x, y, NULL, interruptable));

/* Error exit */

done:	pushall (gdata, ss);
	return (stop_reason);
}

int rhgcd (	/* recursive hgcd calls accumulating extended GCD info */
		/* when done. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	giant	*x,
	giant 	*y,
	gmatrix R,
	int	interruptable)
{
	gmatrixstruct A;
	int	ss, stop_reason;

	ss = stackstart (gdata);
	A.ul = popg (gdata, (*x)->sign);
	if (A.ul == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	setone (A.ul);
	A.ll = popg (gdata, (*x)->sign);
	if (A.ll == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	setzero (A.ll);
	A.ur = popg (gdata, (*x)->sign + 1);					// In theory, +1 not needed.  But general mod on 27*2^100000-1 will then fail.
	if (A.ur == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	setzero (A.ur);
	A.lr = popg (gdata, (*x)->sign + 1);					// In theory, +1 not needed.  But general mod on 2163*2^1255556+1 will then fail.
	if (A.lr == NULL) { stop_reason = GIANT_OUT_OF_MEMORY; goto done; }
	setone (A.lr);

	if ((*y)->sign <= GCDLIMIT) {
		stop_reason = cextgcdg (gdata, x, y, &A, interruptable);
		if (stop_reason) goto done;
	} else {
		stop_reason = hgcd (gdata, 0, x, y, &A, interruptable);
		if (stop_reason) goto done;
		if (isone (A.lr)) onestep (gdata, x, y, &A);
		stop_reason = rhgcd (gdata, x, y, &A, interruptable);
		if (stop_reason) goto done;
	}
	stop_reason = mulmM (gdata, &A, R);
	if (stop_reason) goto done;

done:	pushall (gdata, ss);
	return (stop_reason);
}

int hgcd (	/* hgcd(n,x,y,A) chops n words off x and y and computes the
		 * 2 by 2 matrix A such that A[x y] is the pair of terms
		 * in the remainder sequence starting with x,y that is
		 * half the size of x. */
	ghandle *gdata,	/* Free memory blocks for temporaries */
	int 	n,
	giant	*xx,
	giant 	*yy,
	gmatrix A,
	int	interruptable)
{
	giant 	x, y;
	int	half_size, stop_reason;

	ASSERTG (n >= 0 && n < (*xx)->sign && n < (*yy)->sign);

#ifdef DEBUG_CODE
giant t1 = allocgiant (10000);
giant t2 = allocgiant (10000);
int A1 = (A->ur->sign == 0);
gtog (*xx, t1);
gtog (*yy, t2);
#endif

/* Don't do anything if y isn't more than n words long */

	if ((*yy)->sign <= n) return (0);

/* Finagle x and y giant structures to emulate a shift right n words */

	x = *xx;
	x->sign -= n;
	x->n += n;
	setmaxsize (x, x->maxsize - n);
	y = *yy;
	y->sign -= n;
	y->n += n;
	setmaxsize (y, y->maxsize - n);

/* If the numbers are small or the size of the first quotient won't let */
/* split the numbers further, then do the final level of recursion */
/* using a brute force algorithm */

	half_size = (y->sign - 1) >> 1;
	if (half_size <= CHGCD_BREAK || x->sign - y->sign > half_size >> 1) {
		for ( ; ; ) {
			int	quot_length, a_length;

/* Determine if applying quotient to the matrix will put us over half_size. */
/* Be wary of the final matrix multiply of A in gcdhlp and onestep.  It */
/* adds two multiplied products together - so that a 32-bit quotient applied */
/* to a 20 word A->lr value can produce a 22 word result if the top word */
/* of A->lr is also 32 bits long. */

			quot_length = x->sign - y->sign;
			if (x->n[x->sign-1] >= y->n[y->sign-1]) quot_length++;
			a_length = abs (A->lr->sign);
			if (A->lr->n[a_length-1] & 0x80000000) a_length++;
			if (a_length + quot_length > half_size) break;

/* If the quotient will fit in one word, use the GCD helper */
/* function which can do several GCD steps in single precision, */
/* postponing multi-precision operations as long as possible. */

			if (quot_length > 1 || !gcdhlp_wrapper (gdata, x, y, A))
				onestep (gdata, &x, &y, A);
		}
	}

/* Otherwise do two recursions to compute the half-gcd */

	else {
		gmatrixstruct B;
		giantstruct ul, ur, ll, lr;
		int	a_size, b_size;

/* Check for an interrupt */

		if (interruptable && StopCheckRoutine != NULL) {
			stop_reason = stopCheck (interruptable);
			if (stop_reason) return (stop_reason);
		}

/* Do the first recursion */

		a_size = half_size >> 1;
		stop_reason = hgcd (gdata, y->sign - (a_size + a_size + 1), &x, &y, A, interruptable);
		if (stop_reason) return (stop_reason);
		a_size = intmax (abs (A->lr->sign), abs (A->ur->sign));
		ASSERTG (a_size >= abs (A->ll->sign) && a_size >= abs (A->ul->sign));

/* Do the second recursion.  Note how we use the upper half of the gmatrix A */
/* in order to save a lot of memory.  Be wary of the matrix multiply of B */
/* and A in mulmMsp.  It adds two multiplied products together - so that */
/* if B->lr's top word is 32 bits A->lr's top word is 32 bits, then a */
/* carry overflow will occur.  Also be careful that we don't ask hgcd to */
/* compute more than y->sign / 2 words. */

		b_size = half_size - a_size;
		if (A->lr->n[a_size-1] & 0x80000000) b_size--;
		ul.n = A->ul->n + a_size; setmaxsize (&ul, b_size);
		ur.n = A->ur->n + a_size; setmaxsize (&ur, b_size);
		ll.n = A->ll->n + a_size; setmaxsize (&ll, b_size);
		lr.n = A->lr->n + a_size; setmaxsize (&lr, b_size);
		B.ul = &ul; setone (&ul);
		B.ur = &ur; setzero (&ur);
		B.ll = &ll; setzero (&ll);
		B.lr = &lr; setone (&lr);
		if (b_size > ((y->sign - 1) >> 1)) b_size = (y->sign - 1) >> 1;

/* It is a problem if x and y are nearly equal such that the returned y from a second hgcd call is smaller than half */
/* the size of the orginal input.  I'm not exactly sure why, but the code below is designed to work around this. */
/* The test case that triggered this error was GMP-ECM compiled with GWNUM's ECM stage 1 using giants for modular inverse. */
/* echo "((2^10199-1)*(2^47-1)*(2^31-1)*(2^7-1)/((2-1)*(2^1457-1)*(2^329-1)*(2^217-1)))/(326369*1468657*20957557937*167308774088203301401)" | ./ecm -v -sigma 0:14166007541194524826 11e3 */
/* NOTE: I've changed ECM stage 1 to use GMP's modular inverse function in the same way prime95 does.  Hopefully no one */
/* uses the giants GCD code any more. */

		if (x->sign >= 2 && (x->sign != y->sign || x->n[x->sign-1] != y->n[y->sign-1] || x->n[x->sign-2] != y->n[y->sign-2])) {
			stop_reason = hgcd (gdata, y->sign - (b_size + b_size + 1), &x, &y, &B, interruptable);
			if (stop_reason) return (stop_reason);
			stop_reason = mulmMsp (gdata, &B, A, half_size);
			if (stop_reason) return (stop_reason);
		}
	}

/* Copy the x and y values, then undo the changes we made to the input */
/* giants to emulate a shift right n words and instead point to just */
/* the lower n words. */

	if (n) {
		giant 	xinp, yinp, tmp;

		tmp = x; x = popg (gdata, x->sign); gtog (tmp, x);
		tmp = y; y = popg (gdata, y->sign); gtog (tmp, y);
		xinp = *xx;
		xinp->sign = n;
		xinp->n -= n;
		while (xinp->sign && xinp->n[xinp->sign-1] == 0) xinp->sign--;
		setmaxsize (xinp, xinp->maxsize + n);
		yinp = *yy;
		yinp->sign = n;
		yinp->n -= n;
		while (yinp->sign && yinp->n[yinp->sign-1] == 0) yinp->sign--;
		setmaxsize (yinp, yinp->maxsize + n);

/* Now apply the matrix A to the bits of xx and yy that were shifted off */
/* This lets us compute the final x and y values for the caller */

		stop_reason = mulvM (gdata, A, xinp, yinp);
		if (stop_reason) {
			pushg (gdata, 2);
			return (stop_reason);
		}
		addshiftedg (n, x, xinp);
		addshiftedg (n, y, yinp);
		if (xinp->sign < 0) {
			negg (xinp); negg (A->ul); negg (A->ur);
		}
		if (yinp->sign < 0) {
			negg (yinp); negg (A->ll); negg (A->lr);
		}
		if (gcompg (xinp, yinp) < 0) {
			gswap (xx, yy);
			gswap (&A->ul, &A->ll);
			gswap (&A->ur, &A->lr);
		}
		pushg (gdata, 2);
	}

/* If we didn't do any shifting, make sure *xx is the larger value */

	else {
		if (*xx != x) gswap (xx, yy);
	}

#ifdef DEBUG_CODE
if (A1) {
giant t3 = allocgiant (10000);
giant t4 = allocgiant (10000);
gtog (t1, t3);  mulg (A->ul, t3);
gtog (t2, t4);  mulg (A->ur, t4);
addg (t3, t4);
ASSERTG (gcompg (*xx, t4) == 0);
gtog (t1, t3);  mulg (A->ll, t3);
gtog (t2, t4);  mulg (A->lr, t4);
addg (t3, t4);
ASSERTG (gcompg (*yy, t4) == 0);
free (t3);
free (t4);
}
free (t1);
free (t2);
#endif

/* All done */

	return (0);
}
